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

#define MAX_INPUT_QUEUE_SIZE 100

/*
MicroService creation process: ^^^^ move to README
- Define Store struct
- Define Container type = MicroServiceContainer<SiblingServices...>
- Define Inputs, each inheriting from Input
- Define States, each with accept/enter functions with args (Store& s, const Container& c, const [every input type]& i)
- Declare MicroService<States, Inputs, Store, Container>
- Override virtual functions:
  - name() -> str
  - getHeartbeatInput() -> specify the heartbeat input out of Inputs
  - initStore() -> optionally initialize the store with non-default values
  - preStop() -> optional clean-up
  - getQueueWindowSize() -> optionally override queue window size
- Optionally define public functions that add Inputs to mInputBuffer (the only accessible member variable). Can set up
in the virtual setup() function that runs at initialization (e.g., initializing and binding subscribers to callback
functions).
*/

namespace services
{

struct Input
{
    virtual const uint8_t                   priority() const = 0;
    virtual const std::chrono::milliseconds duration() const = 0;
};

template<class S>
struct State
{
    static const size_t index()
    {
        return S::stateIndex();
    }
};

// template<typename States, typename Inputs, typename Store, typename ContainerType>
template<typename Store, typename ContainerType, typename... States>
class MicroService
{
public:
    MicroService()
    {
        setup();
    }
    ~MicroService()
    {
        if (running())
        {
            stop();
        }
    }

    using Container = ContainerType;

    MicroService(const MicroService&)            = delete;
    MicroService& operator=(const MicroService&) = delete;
    MicroService(MicroService&&)                 = delete;
    MicroService& operator=(MicroService&&)      = delete;

    virtual const std::string name() const = 0;

    void run()
    {
        if (running())
        {
            return;
        }

        Store store;

        initStore(store);

        mRunning    = true;
        mMainThread = std::thread([this, store]() {
            static constexpr int maxNameLength{15};
            pthread_setname_np(pthread_self(), this->name().substr(0, maxNameLength).c_str());
            mainLoop(store);
        });
    }

    void stop()
    {
        if (!running())
        {
            return;
        }

        preStop();

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

protected:
    virtual const std::shared_ptr<Input> getHeartbeatInput() const = 0;

    virtual void          setup() {}
    virtual void          initStore(Store& store) {}
    virtual void          preStop() {}
    virtual const uint8_t getQueueWindowSize() const
    {
        return 5;
    }

    void addInputToQueue(std::shared_ptr<Input>&& input)
    {
        mInputBuffer.push_back(std::move(input));
    }

private:
    class FiniteStateMachine
    {
    public:
        template<typename InputType>
        void execute(Store& store, const InputType& input)
        {
            const size_t nextState = mStates.runOnActiveState([this, &store, &input](auto& state) -> size_t {
                using S = typename std::decay<decltype(state)>::type;
                using I = typename std::decay<InputType>::type;
                assertStepExists<S, I>();
                return state.step(store, mContainer, input); // ^^^^ TODO input needs to be a pointer for polymorphism
            });
            mStates.transition(nextState);
        }

    private:
        class StateSet
        {
        public:
            size_t                mActiveState = 0; // Init state is the first one in the template list
            std::tuple<States...> mStates;

            template<typename T>
            static constexpr size_t index()
            {
                return Index<T, std::tuple<States...>>::value;
            }

            void transition(const size_t& state)
            {
                mActiveState = state;
            }

            template<typename F>
            decltype(auto) runOnActiveState(F&& f)
            {
                return runOnActiveStateImpl<sizeof...(States) - 1>(std::forward<F>(f));
            }

            template<typename F>
            decltype(auto) runOnActiveState(F&& f) const
            {
                return runOnActiveStateImpl<sizeof...(States) - 1>(std::forward<F>(f));
            }

        private:
            template<size_t I, typename F>
            decltype(auto) runOnActiveStateImpl(F&& f)
            {
                if (I == mActiveState)
                {
                    return f(std::get<I>(mStates));
                }

                if constexpr (I > 0)
                {
                    return runOnActiveStateImpl<I - 1>(std::forward<F>(f));
                }
                else
                {
                    throw std::out_of_range("State index out of range");
                }
            }

            template<size_t I, typename F>
            decltype(auto) runOnActiveStateImpl(F&& f) const
            {
                if (I == mActiveState)
                {
                    return f(std::get<I>(mStates));
                }

                if constexpr (I > 0)
                {
                    return runOnActiveStateImpl<I - 1>(std::forward<F>(f));
                }
                else
                {
                    throw std::out_of_range("State index out of range");
                }
            }

            template<class T, class Tuple>
            struct Index;

            template<class T, class... Types>
            struct Index<T, std::tuple<T, Types...>>
            {
                static constexpr std::size_t value = 0;
            };

            template<class T, class U, class... Types>
            struct Index<T, std::tuple<U, Types...>>
            {
                static constexpr std::size_t value = 1 + Index<T, std::tuple<Types...>>::value;
            };
        };

        StateSet  mStates;
        Container mContainer;

        template<typename S, typename I>
        using StepFuncSignature = decltype(std::declval<S>().step(std::declval<Store&>(),
                                                                  std::declval<const Container&>(),
                                                                  std::declval<const I&>()));
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

    FiniteStateMachine mMachine;
    std::thread        mMainThread;
    std::atomic_bool   mRunning{false};

    __threadsafe_circular_buffer<std::shared_ptr<Input>> mInputBuffer{MAX_INPUT_QUEUE_SIZE};

    void mainLoop(Store store)
    {
        using namespace std::chrono;

        assert(getQueueWindowSize() > 0);
        assert(getQueueWindowSize() <= MAX_INPUT_QUEUE_SIZE);
        assert(getHeartbeatInput()->duration() > milliseconds(0));

        const auto heartbeatInput  = getHeartbeatInput();
        const auto queueWindowSize = getQueueWindowSize();

        auto heartbeatDur = duration_cast<duration<double>>(heartbeatInput->duration());

        auto next = steady_clock::now();

        while (running())
        {
            auto startTime = steady_clock::now();
            mMachine.execute(store, *heartbeatInput);
            auto endTime = steady_clock::now();
            auto dur     = duration_cast<duration<double>>(endTime - startTime);
            if (dur >= heartbeatDur)
            {
                // TODO report a warning
                continue;
            }
            else
            {
                next = startTime + heartbeatInput->duration();
                if (!mInputBuffer.empty())
                {
                    auto                   now        = steady_clock::now();
                    std::shared_ptr<Input> nextViable = nullptr;
                    while (getNextViableInput(nextViable, duration_cast<duration<double>>(next - now)))
                    {
                        mMachine.execute(store, *nextViable);
                        now = steady_clock::now();
                    }
                }
                std::this_thread::sleep_until(next);
            }
        }
    }

    bool getNextViableInput(std::shared_ptr<Input> nextViable, const std::chrono::duration<double> timeLimit)
    {
        if (timeLimit < std::chrono::duration<double>(0))
        {
            return false;
        }

        uint8_t highestPriority = std::numeric_limits<uint8_t>::max();
        uint8_t drainIdx        = 0;
        uint8_t nextIdx         = -1;

        std::vector<std::shared_ptr<Input>> nextCandidates;

        bool fullyDrained = mInputBuffer.drainUntil([&](std::shared_ptr<Input>&& v) {
            std::shared_ptr<Input> input = std::move(v);
            nextCandidates.push_back(input);
            if (std::chrono::duration_cast<std::chrono::duration<double>>(input->duration()) <= timeLimit)
            {
                if (input->priority() < highestPriority)
                {
                    highestPriority = input->priority();
                    nextIdx         = drainIdx;
                }
            }
            drainIdx++;
            return drainIdx < getQueueWindowSize();
        });

        for (uint8_t i = drainIdx; i > 0; i--)
        {
            if (i - 1 != drainIdx)
            {
                mInputBuffer.push_front(nextCandidates[i - 1]);
            }
        }

        if (fullyDrained && nextIdx >= 0)
        {
            nextViable = nextCandidates[nextIdx];
            return true;
        }
        else
        {
            return false;
        }
    }
};

} // namespace services
