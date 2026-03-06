#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <catch2/catch.hpp>
#pragma GCC diagnostic pop

#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

#include "mscpp/IOAdapter.h"
#include "mscpp/MicroServiceReactors.h"
#include "mscpp/MicroServiceContainer.h"
#include "mscpp/Ports.h"
#include "mscpp/StateSet.h"
#include "mscpp/ReactorScheduler.h"

using namespace services;

// ===========================================================================
// Test Reactor and FSM for IOAdapter testing
// ===========================================================================

DECLARE_REACTOR_NAME(IOTestReactor);

struct StoreIOTest
{
    int request_count{0};
    int last_request_id{0};
    std::vector<std::string> action_log;
};

struct PortsIOTest
{
    InputPort<int> request_in;
    OutputPort<int> response_out;
};

ENABLE_AUTO_CLEAR_PORTS(PortsIOTest, request_in);

using ContainerIOTest = MicroServiceContainer<>;

// FSM States for IO Test Reactor
struct IdleStateIOTest;
struct ProcessingStateIOTest;

using IOTestStates = StateSet<IdleStateIOTest, ProcessingStateIOTest>;

struct IdleStateIOTest : State<IdleStateIOTest, 0>
{
    static constexpr const char* name() { return "Idle"; }

    size_t step(StoreIOTest& s, PortsIOTest& p, const ContainerIOTest& c,
               const LogicalTag& tag,
               const StepTrigger& trigger)
    {
        (void)c;
        (void)tag;

        // Log the trigger type
        if (trigger.type == StepTrigger::Type::HEARTBEAT)
        {
            s.action_log.push_back("heartbeat");
        }
        else if (trigger.type == StepTrigger::Type::LOGICAL_ACTION)
        {
            s.action_log.push_back("logical:" + trigger.action_name);

            // Check for request on input port
            if (p.request_in.is_present())
            {
                int request_id = p.request_in.get();
                s.request_count++;
                s.last_request_id = request_id;

                // Send response
                p.response_out.set(request_id * 2);

                return 1;  // transition to ProcessingState
            }
        }

        return 0;  // stay in Idle
    }
};

struct ProcessingStateIOTest : State<ProcessingStateIOTest, 1>
{
    static constexpr const char* name() { return "Processing"; }

    size_t step(StoreIOTest& s, PortsIOTest& p, const ContainerIOTest& c,
               const LogicalTag& tag,
               const StepTrigger& trigger)
    {
        (void)s;
        (void)p;
        (void)c;
        (void)tag;
        (void)trigger;

        // Return to idle after one step
        return 0;  // return to IdleState
    }
};

// Define the reactor
class IOTestReactor : public MicroServiceFSMReactor<
    NameIOTestReactor,
    StoreIOTest,
    PortsIOTest,
    ContainerIOTest,
    IOTestStates>
{
public:
    using Base = MicroServiceFSMReactor<
        NameIOTestReactor,
        StoreIOTest,
        PortsIOTest,
        ContainerIOTest,
        IOTestStates>;
    using Base::Base;
};

// ===========================================================================
// Mock IOAdapter for testing
// ===========================================================================

class MockIOAdapter : public IOAdapter<IOTestReactor>
{
public:
    explicit MockIOAdapter(std::shared_ptr<IOTestReactor> reactor)
        : IOAdapter(reactor)
        , event_loop_started_(false)
        , event_loop_stopped_(false)
        , event_count_(0)
    {
    }

    ~MockIOAdapter()
    {
        // Must call stop() here to avoid calling pure virtual functions
        // from base class destructor after MockIOAdapter is destroyed
        stop();
    }

    void simulateExternalEvent(int request_id)
    {
        // Simulate an external event (e.g., gRPC request)
        // In a real scenario, the I/O thread would:
        // 1. Receive external input (gRPC request)
        // 2. Write to reactor's input port
        // 3. Schedule logical action to notify reactor

        // Write to reactor's input port
        getReactor()->getPorts().request_in.set(request_id);

        // Schedule logical action to trigger processing
        scheduleReactorAction("external_request", request_id);

        event_count_++;
    }

    void scheduleActionOnly(const std::string& action_name, int data)
    {
        // Just schedule an action without touching ports
        scheduleReactorAction(action_name, data);
        event_count_++;
    }

    int getEventCount() const { return event_count_.load(); }
    bool wasEventLoopStarted() const { return event_loop_started_.load(); }
    bool wasEventLoopStopped() const { return event_loop_stopped_.load(); }

protected:
    void runIOEventLoop() override
    {
        event_loop_started_ = true;

        // Simple event loop that runs while the adapter is active
        while (isRunningFlag())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void stopIOEventLoop() override
    {
        event_loop_stopped_ = true;
    }

private:
    std::atomic<bool> event_loop_started_;
    std::atomic<bool> event_loop_stopped_;
    std::atomic<int> event_count_;
};

// ===========================================================================
// Test Cases
// ===========================================================================

TEST_CASE("IOAdapter: Basic start/stop lifecycle", "[ioadapter]")
{
    auto reactor = std::make_shared<IOTestReactor>();
    MockIOAdapter adapter(reactor);

    REQUIRE_FALSE(adapter.isRunning());
    REQUIRE_FALSE(adapter.wasEventLoopStarted());

    adapter.start();
    REQUIRE(adapter.isRunning());

    // Give event loop time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(adapter.wasEventLoopStarted());

    adapter.stop();
    REQUIRE_FALSE(adapter.isRunning());
    REQUIRE(adapter.wasEventLoopStopped());
}

TEST_CASE("IOAdapter: Multiple start calls are no-op", "[ioadapter]")
{
    auto reactor = std::make_shared<IOTestReactor>();
    MockIOAdapter adapter(reactor);

    adapter.start();
    REQUIRE(adapter.isRunning());

    // Second start should be no-op
    adapter.start();
    REQUIRE(adapter.isRunning());

    adapter.stop();
    REQUIRE_FALSE(adapter.isRunning());
}

TEST_CASE("IOAdapter: Multiple stop calls are no-op", "[ioadapter]")
{
    auto reactor = std::make_shared<IOTestReactor>();
    MockIOAdapter adapter(reactor);

    adapter.start();
    adapter.stop();
    REQUIRE_FALSE(adapter.isRunning());

    // Second stop should be no-op
    adapter.stop();
    REQUIRE_FALSE(adapter.isRunning());
}

TEST_CASE("IOAdapter: Thread-safe action scheduling", "[ioadapter][threadsafety]")
{
    auto reactor = std::make_shared<IOTestReactor>();
    ReactorScheduler scheduler;
    reactor->setScheduler(&scheduler);

    MockIOAdapter adapter(reactor);
    adapter.start();

    // Schedule reactor to run
    LogicalTag start_tag{LogicalTime(0), 0};
    scheduler.scheduleEvent(start_tag, reactor->getId(),
        [reactor, start_tag]() {
            reactor->executeHeartbeat(start_tag);
        },
        "initial heartbeat");

    // Simulate multiple external events from different threads
    const int num_threads = 5;
    const int events_per_thread = 10;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&adapter, t, events_per_thread]() {
            for (int i = 0; i < events_per_thread; ++i)
            {
                int request_id = t * events_per_thread + i;
                adapter.simulateExternalEvent(request_id);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads)
    {
        thread.join();
    }

    // Verify all events were scheduled
    REQUIRE(adapter.getEventCount() == num_threads * events_per_thread);

    adapter.stop();
}

TEST_CASE("IOAdapter: Process pending actions during heartbeat", "[ioadapter][reactor]")
{
    auto reactor = std::make_shared<IOTestReactor>();
    ReactorScheduler scheduler;
    reactor->setScheduler(&scheduler);

    MockIOAdapter adapter(reactor);

    // Schedule some logical actions from "external" thread
    // These simulate actions queued from an I/O adapter thread
    adapter.scheduleActionOnly("action1", 100);
    adapter.scheduleActionOnly("action2", 200);
    adapter.scheduleActionOnly("action3", 300);

    REQUIRE(adapter.getEventCount() == 3);

    // Now run heartbeat to process events
    // doHeartbeat() will:
    // 1. Call processPendingActions() which processes all 3 queued actions
    // 2. Run FSM step with heartbeat trigger
    LogicalTag tag{LogicalTime(0), 0};
    reactor->executeHeartbeat(tag);

    // Check that reactor processed the actions
    const auto& store = reactor->getStore();

    // We expect:
    // - 3 logical actions (from scheduleActionOnly calls)
    // - 1 heartbeat trigger
    // So action_log should have 4 entries minimum
    REQUIRE(store.action_log.size() == 4);

    // Count logical actions
    int logical_action_count = 0;
    for (const auto& entry : store.action_log)
    {
        if (entry.find("logical:") == 0)
        {
            logical_action_count++;
        }
    }
    REQUIRE(logical_action_count == 3);
}

TEST_CASE("IOAdapter: Destructor stops running adapter", "[ioadapter]")
{
    auto reactor = std::make_shared<IOTestReactor>();

    {
        MockIOAdapter adapter(reactor);
        adapter.start();
        REQUIRE(adapter.isRunning());

        // Let it run briefly
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Destructor should stop the adapter
    }

    // Adapter is destroyed, thread should be joined
    // If we get here without hanging, the test passes
    REQUIRE(true);
}

TEST_CASE("IOAdapter: Concurrent action scheduling stress test", "[ioadapter][stress]")
{
    auto reactor = std::make_shared<IOTestReactor>();
    ReactorScheduler scheduler;
    reactor->setScheduler(&scheduler);

    MockIOAdapter adapter(reactor);
    adapter.start();

    // Stress test with many concurrent threads
    const int num_threads = 20;
    const int events_per_thread = 50;
    std::atomic<int> total_events{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&adapter, &total_events, events_per_thread, t]() {
            for (int i = 0; i < events_per_thread; ++i)
            {
                adapter.simulateExternalEvent(t * 1000 + i);
                total_events++;
                // Minimal sleep to create contention
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(total_events == num_threads * events_per_thread);
    REQUIRE(adapter.getEventCount() == num_threads * events_per_thread);

    adapter.stop();
}
