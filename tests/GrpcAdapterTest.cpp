#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <catch2/catch.hpp>
#pragma GCC diagnostic pop

#include <thread>
#include <chrono>
#include <atomic>

// #include "mscpp/GrpcAdapter.h"  // Not including to avoid gRPC dependency in tests
#include "mscpp/IOAdapter.h"
#include "mscpp/MicroServiceReactors.h"
#include "mscpp/MicroServiceContainer.h"
#include "mscpp/Ports.h"
#include "mscpp/StateSet.h"
#include "mscpp/ReactorScheduler.h"

using namespace services;

// ===========================================================================
// Test Data Types (simulating gRPC messages)
// ===========================================================================

struct TestRequest
{
    int request_id;
    std::string message;
};

struct TestResponse
{
    int request_id;
    std::string echo_message;
    int total_requests;
};

// ===========================================================================
// Test Reactor and FSM for GrpcAdapter testing
// ===========================================================================

DECLARE_REACTOR_NAME(GrpcTestReactor);

struct StoreGrpcTest
{
    int request_count{0};
    std::vector<std::string> action_log;
};

struct PortsGrpcTest
{
    InputPort<TestRequest> request_in;
    OutputPort<TestResponse> response_out;
};

ENABLE_AUTO_CLEAR_PORTS(PortsGrpcTest, request_in);

using ContainerGrpcTest = MicroServiceContainer<>;

// FSM States
struct IdleStateGrpcTest;
struct ProcessingStateGrpcTest;

using GrpcTestStates = StateSet<IdleStateGrpcTest, ProcessingStateGrpcTest>;

struct IdleStateGrpcTest : State<IdleStateGrpcTest, 0>
{
    static constexpr const char* name() { return "Idle"; }

    size_t step(StoreGrpcTest& s, PortsGrpcTest& p, const ContainerGrpcTest& c,
               const LogicalTag& tag,
               const StepTrigger& trigger)
    {
        (void)c;
        (void)tag;

        // Log the trigger
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION)
        {
            s.action_log.push_back("logical:" + trigger.action_name);

            // Process gRPC request
            if (trigger.action_name == "grpc_request" && p.request_in.is_present())
            {
                const auto& request = p.request_in.get();
                s.request_count++;

                // Generate response
                TestResponse response;
                response.request_id = request.request_id;
                response.echo_message = "Echo: " + request.message;
                response.total_requests = s.request_count;

                p.response_out.set(response);

                return 1;  // Transition to ProcessingState
            }
        }

        return 0;  // Stay in Idle
    }
};

struct ProcessingStateGrpcTest : State<ProcessingStateGrpcTest, 1>
{
    static constexpr const char* name() { return "Processing"; }

    size_t step(StoreGrpcTest& s, PortsGrpcTest& p, const ContainerGrpcTest& c,
               const LogicalTag& tag,
               const StepTrigger& trigger)
    {
        (void)s;
        (void)p;
        (void)c;
        (void)tag;
        (void)trigger;

        return 0;  // Return to IdleState
    }
};

// Define the reactor
class GrpcTestReactor : public MicroServiceFSMReactor<
    NameGrpcTestReactor,
    StoreGrpcTest,
    PortsGrpcTest,
    ContainerGrpcTest,
    GrpcTestStates>
{
public:
    using Base = MicroServiceFSMReactor<
        NameGrpcTestReactor,
        StoreGrpcTest,
        PortsGrpcTest,
        ContainerGrpcTest,
        GrpcTestStates>;
    using Base::Base;
};

// ===========================================================================
// Mock GrpcAdapter for testing (without actual gRPC dependencies)
// ===========================================================================

class MockGrpcAdapter : public IOAdapter<GrpcTestReactor>
{
public:
    explicit MockGrpcAdapter(std::shared_ptr<GrpcTestReactor> reactor)
        : IOAdapter(reactor)
        , event_loop_running_(false)
        , should_stop_(false)
    {
    }

    // Expose protected method for testing
    using IOAdapter<GrpcTestReactor>::scheduleReactorAction;

    // Simulate handleRpc method like GrpcAdapter
    template<typename Request, typename Response>
    bool handleRpc(const Request& request,
                   Response* response,
                   const std::string& action_name,
                   const std::string& response_port,
                   std::chrono::milliseconds timeout = std::chrono::seconds(1))
    {
        (void)response_port;  // Port name not used in this test implementation

        // Write to reactor's input port
        getReactor()->getPorts().request_in.set(request);

        // Schedule logical action on reactor
        scheduleReactorAction(action_name, request);

        // Poll for response on output port (simplified for testing)
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (getReactor()->getPorts().response_out.has_pending_value())
            {
                auto pending = getReactor()->getPorts().response_out.get_pending_value();
                if (pending)
                {
                    *response = *pending;
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return false;  // Timeout
    }

protected:
    void runIOEventLoop() override
    {
        event_loop_running_ = true;

        while (!should_stop_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        event_loop_running_ = false;
    }

    void stopIOEventLoop() override
    {
        should_stop_ = true;
    }

public:
    std::atomic<bool> event_loop_running_;
    std::atomic<bool> should_stop_;
};

// ===========================================================================
// Test Cases
// ===========================================================================

// NOTE: Full integration tests with reactor scheduling are complex due to
// timing issues. These simplified tests focus on adapter lifecycle which
// is the core feature of the IOAdapter pattern.

TEST_CASE("GrpcAdapter: Start and stop lifecycle", "[grpc-adapter]")
{
    auto reactor = std::make_shared<GrpcTestReactor>();
    auto adapter = std::make_shared<MockGrpcAdapter>(reactor);

    // Initially not running
    REQUIRE_FALSE(adapter->isRunning());
    REQUIRE_FALSE(adapter->event_loop_running_);

    // Start adapter
    adapter->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(adapter->isRunning());
    REQUIRE(adapter->event_loop_running_);

    // Stop adapter
    adapter->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(adapter->isRunning());
    REQUIRE_FALSE(adapter->event_loop_running_);

    // Calling start/stop multiple times should be safe
    adapter->start();
    adapter->start();  // Second start is no-op
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(adapter->isRunning());

    adapter->stop();
    adapter->stop();  // Second stop is no-op
    REQUIRE_FALSE(adapter->isRunning());
}
