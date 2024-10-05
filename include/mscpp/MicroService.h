#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <experimental/type_traits>
#include <mutex>
#include <pthread.h>
#include <thread>
#include <vector>

#include "internal/utils.h"

namespace services
{

template<typename Store,
         typename ContainerType,
         typename States,
         typename Inputs,
         size_t InputWindow = 5,
         size_t MaxInputs   = 100>
class MicroService
{
public:
    using Container = ContainerType;

    MicroService() {}
    MicroService(const Container& container) : mMachine(container) {}
    ~MicroService()
    {
        if (running())
        {
            stop();
        }
    }

    MicroService(const MicroService&)            = delete;
    MicroService& operator=(const MicroService&) = delete;
    MicroService(MicroService&&)                 = delete;
    MicroService& operator=(MicroService&&)      = delete;

    virtual const std::string name() const = 0;

    // Place an input on this services queue. Will return false if the queue is full, signaling to try again later.
    bool sendInput(Inputs::TypesVariant&& input)
    {
        return mInputBuffer.push_back_if_not_full(std::move(input));
    }

    void run()
    {
        if (running())
        {
            return;
        }

        {
            std::scoped_lock lock(mMutex);
            initStore(mStore);
        }

        mRunning    = true;
        mMainThread = std::thread([this]() {
            static constexpr int maxNameLength{15};
            pthread_setname_np(pthread_self(), this->name().substr(0, maxNameLength).c_str());
            mainLoop(this->mStore);
        });
    }

    void stop()
    {
        if (!running())
        {
            return;
        }

        mRunning = false;
        if (mMainThread.joinable())
        {
            mMainThread.join();
        }
    }

    bool running()
    {
        return mRunning.load();
    }

    const Store& readStore()
    {
        std::scoped_lock lock(mMutex);
        return mStore;
    }

protected:
    virtual void initStore(Store&) {}

private:
    class FiniteStateMachine
    {
    public:
        FiniteStateMachine() = delete;
        FiniteStateMachine(const Container& container) : mContainer{container} {}

        template<typename InputType>
        void execute(Store& store, InputType& input)
        {
            // TODO: This is where the nexus of input, state, and store recording and logging can be.
            const size_t nextState = mStates.runOnActiveState([this, &store, &input](auto& state) -> size_t {
                using S = typename std::decay<decltype(state)>::type;
                using I = InputType::DerivedType;
                assertStepExists<S, I>();
                return state.step(store, mContainer, input);
            });
            mStates.transition(nextState);
        }

    private:
        States    mStates;
        Container mContainer;

        template<typename S, typename I>
        using StepFuncSignature = decltype(std::declval<S>().step(std::declval<Store&>(),
                                                                  std::declval<const Container&>(),
                                                                  std::declval<I&>()));
        template<typename S, typename I>
        static constexpr bool stepExists()
        {
            return std::experimental::is_detected_exact_v<size_t, StepFuncSignature, S, I>;
        }
        template<typename S, typename I>
        static constexpr void assertStepExists()
        {
            static_assert(stepExists<S, I>(), "State does not define a step function for the given input");
        }
    };

    std::mutex         mMutex;
    Store              mStore;
    FiniteStateMachine mMachine;
    std::thread        mMainThread;
    std::atomic_bool   mRunning{false};

    __threadsafe_circular_buffer<typename Inputs::TypesVariant> mInputBuffer{MaxInputs};

    const Inputs::Heartbeat getHeartbeatInput() const
    {
        return typename Inputs::Heartbeat();
    }

    template<typename... InputOptions>
    void applyApplicableInput(Store& store, typename Inputs::TypesVariant& inputVariant, __type_list<InputOptions...>)
    {
        ((std::holds_alternative<InputOptions>(inputVariant)
              ? (mMachine.execute(store, std::get<InputOptions>(inputVariant)))
              : void()),
         ...);
    }

    void mainLoop(Store& store)
    {
        static_assert(InputWindow > 0, "Input priority evaluation window size must be > 0");
        static_assert(InputWindow <= MaxInputs,
                      "Input priority evaluation window size must be <= max input queue size");

        auto           heartbeatInput = getHeartbeatInput();
        constexpr auto heartbeatDur =
            std::chrono::duration_cast<std::chrono::duration<double>>(heartbeatInput.duration());
        static_assert(heartbeatDur > std::chrono::milliseconds(0), "Heartbeat duration must be greater > 0");

        while (running())
        {
            auto startTime = std::chrono::steady_clock::now();
            auto next      = startTime + heartbeatDur;
            {
                std::scoped_lock lock(mMutex);
                mMachine.execute(store, heartbeatInput);
            }

            auto endTime = std::chrono::steady_clock::now();
            auto dur     = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
            assert(dur <= heartbeatDur); // TODO: Don't rely on runtime asserts.

            auto now = std::chrono::steady_clock::now();

            typename Inputs::TypesVariant nextViable;
            std::chrono::milliseconds     nextDuration;
            while (getNextViableInput(nextViable,
                                      nextDuration,
                                      std::chrono::duration_cast<std::chrono::duration<double>>(next - now)))
            {
                startTime = std::chrono::steady_clock::now();
                std::scoped_lock lock(mMutex);
                applyApplicableInput(store, nextViable, typename Inputs::GenericInputs());
                endTime  = std::chrono::steady_clock::now();
                auto dur = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
                assert(dur <= nextDuration); // TODO: Don't rely on runtime asserts.
                now = std::chrono::steady_clock::now();
            }
        }
    }

    // Find the highest priority input within InputWindow for which we still have enough time to wait on the
    // current service tick.
    // This function should NOT indefinitely block when the input queue is empty, as it's being called in the main
    // thread. It should be beholden to timeLimit.
    // TODO: There should be an observable metric / warning for if the input queue is growing in size or if it is full.
    bool getNextViableInput(Inputs::TypesVariant&               nextViable,
                            std::chrono::milliseconds&          nextDuration,
                            const std::chrono::duration<double> timeLimit)
    {
        if (timeLimit < std::chrono::duration<double>(0))
        {
            return false;
        }

        uint8_t highestPriority = std::numeric_limits<uint8_t>::max();
        uint8_t drainIdx        = 0;
        uint8_t nextIdx         = -1;

        std::vector<typename Inputs::TypesVariant> nextCandidates;

        bool fullyDrained = mInputBuffer.timedDrainUntil(
            [&](typename Inputs::TypesVariant&& v) {
                nextCandidates.push_back(std::move(v));
                const std::chrono::milliseconds inputDuration =
                    std::visit([](const auto& inp) -> std::chrono::milliseconds { return inp.duration(); },
                               nextCandidates.back());
                if (std::chrono::duration_cast<std::chrono::duration<double>>(inputDuration) <= timeLimit)
                {
                    const uint8_t inputPriority =
                        std::visit([](const auto& inp) -> uint8_t { return inp.priority(); }, nextCandidates.back());
                    if (inputPriority < highestPriority)
                    {
                        highestPriority = inputPriority;
                        nextDuration    = inputDuration;
                        nextIdx         = drainIdx;
                    }
                }
                drainIdx++;
                return drainIdx < InputWindow;
            },
            timeLimit);

        for (uint8_t i = drainIdx; i > 0; i--)
        {
            if (i - 1 != nextIdx)
            {
                mInputBuffer.push_front(std::move(nextCandidates[i - 1]));
            }
        }

        if (fullyDrained && nextIdx < nextCandidates.size())
        {
            nextViable = std::move(nextCandidates[nextIdx]);
            return true;
        }
        else
        {
            return false;
        }
    }
};

} // namespace services
