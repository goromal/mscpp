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

PURPOSE
- Eliminate side effects within a microservice
- Ensure that all developer logic is contained within highly testable pure functions in the form of state machine
definitions
- Make illegal states unrepresentable and force the microservice developer to:
  - consider every corner case from outside inputs
  - partition functionality responsibly in order to keep the FSMs tractable
- Impose time limits on every action the microservice takes

GOTCHAS (^^^^TODO)
- Right now the time limits are not strictly enforced--should they be?
- Are we sure that a developer cannot introduce side effects?
*/

namespace services
{

template<typename Store, typename ContainerType, typename States, typename Inputs>
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

        // There is no need for the store to be lock-protected because it is only accessible by the main thread.
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
    virtual const Inputs::Heartbeat
    getHeartbeatInput() const = 0; // ^^^^ TODO make this no longer virtual as it is now unambiguous. make it private

    virtual void setup() {} // ^^^^ TODO superseded by initStore? Aside from input constructors, a developer has no way
                            // of touching MicroService class variables--only the store
    virtual void initStore(Store& store) {}
    virtual void preStop() {} // ^^^^ TODO necessary? Aside from input constructors, a developer has no way of touching
                              // MicroService class variables--only the store
    virtual const uint8_t getQueueWindowSize() const
    {
        return 5; // ^^^^ TODO assert that it's > 0 and <= max queue size...can we make this a constexpr with static
                  // asserts? BETTER YET, can we accomplish all that by making this ANOTHER template parameter??
    }

    // ^^^^ TODO Aside from the argument that maybe a developer would want to provision some state outside if the store
    // to provide a more sophisticated (?) front door to inputs from dependent microservices, what's stopping us from
    // just making this function public and the sole interface into the service (and also just putting the semaphore
    // stuff in here)?
    void addInputToQueue(Inputs::TypesVariant&& input)
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
            // TODO this is where the nexus of input, state, and store recording and logging can be
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

    __threadsafe_circular_buffer<typename Inputs::TypesVariant> mInputBuffer{MAX_INPUT_QUEUE_SIZE};

    template<typename... InputOptions>
    void
    applyApplicableInput(Store& store, const typename Inputs::TypesVariant& inputVariant, __type_list<InputOptions...>)
    {
        ((std::holds_alternative<InputOptions>(inputVariant)
              ? (mMachine.execute(store, std::get<InputOptions>(inputVariant)))
              : void()),
         ...);
    }

    template<typename T>
    void applyApplicableInputWrapper(Store& store, const typename Inputs::TypesVariant& inputVariant)
    {
        applyApplicableInput(store, inputVariant, T{});
    }

    void mainLoop(Store store)
    {
        using namespace std::chrono;

        assert(getQueueWindowSize() > 0);
        assert(getQueueWindowSize() <= MAX_INPUT_QUEUE_SIZE);
        assert(getHeartbeatInput().duration() > milliseconds(0));

        const auto heartbeatInput  = getHeartbeatInput();
        const auto queueWindowSize = getQueueWindowSize();

        auto heartbeatDur = duration_cast<duration<double>>(heartbeatInput.duration());

        auto next = steady_clock::now();

        while (running())
        {
            auto startTime = steady_clock::now();
            mMachine.execute(store, heartbeatInput);
            auto endTime = steady_clock::now();
            auto dur     = duration_cast<duration<double>>(endTime - startTime);
            if (dur >= heartbeatDur)
            {
                // TODO report a warning...make this an assert??
                continue;
            }
            else
            {
                next = startTime + heartbeatInput.duration();
                if (!mInputBuffer.empty())
                {
                    auto now = steady_clock::now();

                    typename Inputs::TypesVariant nextViable; // ^^^^ TODO use move semantics in this chain
                    while (getNextViableInput(nextViable, duration_cast<duration<double>>(next - now)))
                    {
                        // ^^^^ ^^^^
                        // https://stackoverflow.com/questions/7230621/how-can-i-iterate-over-a-packed-variadic-template-argument-list
                        // OR
                        // https://stackoverflow.com/questions/36526400/is-there-a-way-to-make-a-function-return-a-typename
                        /*
                        template<typename… Ts>
                        void iterate() {
                            (do_something<Ts>(),…);
                        }
                        */
                        // const auto input = std::visit([](auto&& inp) -> decltype(auto) { return inp; }, nextViable);
                        // mMachine.execute(store, input);
                        applyApplicableInputWrapper<typename Inputs::GenericInputs>(store, nextViable);
                        now = steady_clock::now();
                    }
                }
                std::this_thread::sleep_until(next);
            }
        }
    }

    // TODO there needs to be am observable metric / warning for if the input queue is growing in size or if it is full
    // What happens if if's full, again?
    // ^^^^ TODO make this semaphore-based so that the input queue is not allowed to get full, and make the max queue
    // size a class template parameter instead of a compiler def.

    // Find the highest priority input within getQueueWindowSize() for which we still have enough time to wait on the
    // current service tick
    bool getNextViableInput(Inputs::TypesVariant& nextViable, const std::chrono::duration<double> timeLimit)
    {
        using namespace std::chrono;
        if (timeLimit < duration<double>(0))
        {
            return false;
        }

        uint8_t highestPriority = std::numeric_limits<uint8_t>::max();
        uint8_t drainIdx        = 0;
        uint8_t nextIdx         = -1;

        std::vector<typename Inputs::TypesVariant> nextCandidates;

        bool fullyDrained = mInputBuffer.drainUntil([&](typename Inputs::TypesVariant&& v) {
            typename Inputs::TypesVariant input = std::move(v);
            nextCandidates.push_back(input);
            const milliseconds inputDuration =
                std::visit([](auto&& inp) -> milliseconds { return inp.duration(); }, input);
            if (duration_cast<duration<double>>(inputDuration) <= timeLimit)
            {
                const uint8_t inputPriority = std::visit([](auto&& inp) -> uint8_t { return inp.priority(); }, input);
                if (inputPriority < highestPriority)
                {
                    highestPriority = inputPriority;
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
