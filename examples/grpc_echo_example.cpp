/**
 * GrpcAdapter Example - Echo Service
 *
 * This example demonstrates how to use GrpcAdapter to integrate a gRPC service
 * with a deterministic mscpp reactor. The example implements a simple echo service
 * that receives messages via gRPC, processes them in a reactor, and returns responses.
 *
 * ARCHITECTURE:
 * - GrpcEchoService: gRPC service implementation
 * - EchoReactor: Deterministic FSM reactor that processes requests
 * - GrpcAdapter: Thread-safe boundary between async gRPC and deterministic reactor
 *
 * USAGE:
 *   ./grpc_echo_example
 *   # In another terminal:
 *   grpcurl -plaintext -d '{"message": "Hello"}' localhost:50051 EchoService/Echo
 */

#include "mscpp/IOAdapters/GrpcAdapter.h"
#include "mscpp/MicroServiceReactors.h"
#include "mscpp/MicroServiceContainer.h"
#include "mscpp/Ports.h"
#include "mscpp/StateSet.h"
#include "mscpp/ReactorScheduler.h"
#include "mscpp/Logging.h"

#include <grpcpp/grpcpp.h>
#include <string>
#include <memory>
#include <thread>
#include <chrono>

using namespace services;

// ===========================================================================
// gRPC Service Definition (normally generated from .proto)
// For this example, we define simple message types directly
// ===========================================================================

namespace echo
{
    struct EchoRequest
    {
        std::string message;
    };

    struct EchoResponse
    {
        std::string echoed_message;
        int request_count;
    };
}

// ===========================================================================
// Echo Reactor - Deterministic FSM that processes echo requests
// ===========================================================================

DECLARE_REACTOR_NAME(EchoReactor);

struct StoreEcho
{
    int total_requests{0};
    std::string last_message;
};

struct PortsEcho
{
    InputPort<echo::EchoRequest> echo_request_in;
    OutputPort<echo::EchoResponse> echo_response_out;
};

ENABLE_AUTO_CLEAR_PORTS(PortsEcho, echo_request_in);

using ContainerEcho = MicroServiceContainer<>;

// FSM States
struct IdleStateEcho;
struct ProcessingStateEcho;

using EchoStates = StateSet<IdleStateEcho, ProcessingStateEcho>;

struct IdleStateEcho : State<IdleStateEcho, 0>
{
    static constexpr const char* name() { return "Idle"; }

    size_t step(StoreEcho& s, PortsEcho& p, const ContainerEcho& c,
               const LogicalTag& tag,
               const StepTrigger& trigger)
    {
        (void)c;
        (void)tag;

        // Check for incoming echo request
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "echo_request")
        {
            if (p.echo_request_in.is_present())
            {
                const auto& request = p.echo_request_in.get();
                s.total_requests++;
                s.last_message = request.message;

                SPDLOG_INFO("EchoReactor: Received request #{}: '{}'",
                           s.total_requests, request.message);

                // Generate response
                echo::EchoResponse response;
                response.echoed_message = "Echo: " + request.message;
                response.request_count = s.total_requests;

                p.echo_response_out.set(response);

                return 1;  // Transition to ProcessingState
            }
        }

        return 0;  // Stay in Idle
    }
};

struct ProcessingStateEcho : State<ProcessingStateEcho, 1>
{
    static constexpr const char* name() { return "Processing"; }

    size_t step(StoreEcho& s, PortsEcho& p, const ContainerEcho& c,
               const LogicalTag& tag,
               const StepTrigger& trigger)
    {
        (void)s;
        (void)p;
        (void)c;
        (void)tag;
        (void)trigger;

        // Return to idle after one step
        return 0;  // Return to IdleState
    }
};

// Define the reactor
class EchoReactor : public MicroServiceFSMReactor<
    NameEchoReactor,
    StoreEcho,
    PortsEcho,
    ContainerEcho,
    EchoStates>
{
public:
    using Base = MicroServiceFSMReactor<
        NameEchoReactor,
        StoreEcho,
        PortsEcho,
        ContainerEcho,
        EchoStates>;
    using Base::Base;
};

// ===========================================================================
// gRPC Service Implementation (using GrpcAdapter pattern)
// ===========================================================================

// Forward declaration for circular dependency
template<typename ReactorType, typename ServiceImpl>
class GrpcAdapter;

class EchoServiceImpl : public grpc::Service
{
public:
    explicit EchoServiceImpl(GrpcAdapter<EchoReactor, EchoServiceImpl>* adapter)
        : adapter_(adapter)
    {
    }

    grpc::Status Echo(grpc::ServerContext* context,
                     const echo::EchoRequest* request,
                     echo::EchoResponse* response)
    {
        (void)context;

        SPDLOG_INFO("gRPC: Received Echo request: '{}'", request->message);

        // Use GrpcAdapter's handleRpc to:
        // 1. Schedule logical action on reactor with request data
        // 2. Wait for reactor to process and write response
        // 3. Return response to gRPC client
        return adapter_->handleRpc(*request, response,
                                  "echo_request", "echo_response_out");
    }

private:
    GrpcAdapter<EchoReactor, EchoServiceImpl>* adapter_;
};

// ===========================================================================
// Main - Run the Echo Service
// ===========================================================================

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    SPDLOG_INFO("Starting gRPC Echo Service example...");

    // Create scheduler
    auto scheduler = std::make_shared<ReactorScheduler>();

    // Create echo reactor
    auto reactor = std::make_shared<EchoReactor>();
    reactor->setScheduler(scheduler.get());

    // Register reactor with scheduler
    scheduler->registerReactor(reactor);

    // Create gRPC adapter
    std::string server_address = "0.0.0.0:50051";
    GrpcAdapter<EchoReactor, EchoServiceImpl> adapter(reactor, server_address);

    SPDLOG_INFO("Starting gRPC server on {}...", server_address);
    adapter.start();

    // Start scheduler in background thread
    SPDLOG_INFO("Starting reactor scheduler...");
    std::thread scheduler_thread([&scheduler]() {
        scheduler->run();
    });

    // Run for demonstration (in production, this would run indefinitely)
    SPDLOG_INFO("Echo service running. Test with:");
    SPDLOG_INFO("  grpcurl -plaintext -d '{{\"message\": \"Hello\"}}' localhost:50051 EchoService/Echo");
    SPDLOG_INFO("Press Ctrl+C to stop.");

    std::this_thread::sleep_for(std::chrono::minutes(5));

    // Cleanup
    SPDLOG_INFO("Shutting down...");
    scheduler->stop();
    adapter.stop();

    if (scheduler_thread.joinable())
    {
        scheduler_thread.join();
    }

    SPDLOG_INFO("Echo service stopped.");
    return 0;
}
