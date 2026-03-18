#pragma once

#include "../IOAdapter.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>

namespace services
{

/**
 * GrpcAdapter - Thread-safe boundary between gRPC and deterministic reactors
 *
 * The GrpcAdapter provides a concrete implementation of IOAdapter for integrating
 * gRPC services with mscpp's deterministic reactor model. It manages a gRPC server
 * in a dedicated thread and provides thread-safe communication with the reactor.
 *
 * ARCHITECTURAL ROLE:
 * - Runs gRPC completion queue in a dedicated thread
 * - Routes incoming RPC calls to reactor via logical actions
 * - Routes reactor responses back to gRPC clients
 * - Maintains deterministic reactor execution despite async RPC arrivals
 *
 * USAGE PATTERN:
 * 1. Define your gRPC service implementation class
 * 2. Create GrpcAdapter<ReactorType, ServiceImpl> instance
 * 3. Service implementation calls handleRpc() for request-response pattern
 * 4. Adapter schedules logical action on reactor and waits for response
 * 5. Reactor processes request and writes to output port
 * 6. Adapter retrieves response and returns to gRPC client
 *
 * THREAD SAFETY:
 * - gRPC server runs in separate I/O thread
 * - RPC handlers are thread-safe (use IOAdapter's synchronization)
 * - Multiple concurrent RPCs are supported
 *
 * EXAMPLE:
 * ```cpp
 * // Define gRPC service
 * class MyServiceImpl : public MyService::Service {
 * public:
 *     explicit MyServiceImpl(GrpcAdapter<MyReactor, MyServiceImpl>* adapter)
 *         : adapter_(adapter) {}
 *
 *     grpc::Status ProcessRequest(grpc::ServerContext* context,
 *                                const Request* request,
 *                                Response* response) override {
 *         return adapter_->handleRpc(*request, response,
 *                                   "process_request", "response_out");
 *     }
 *
 * private:
 *     GrpcAdapter<MyReactor, MyServiceImpl>* adapter_;
 * };
 *
 * // Create adapter and start
 * auto reactor = std::make_shared<MyReactor>();
 * GrpcAdapter<MyReactor, MyServiceImpl> adapter(reactor, "0.0.0.0:50051");
 * adapter.start();  // gRPC server runs in background thread
 * ```
 *
 * Template Parameters:
 * - ReactorType: The reactor type this adapter interfaces with
 * - ServiceImpl: The gRPC service implementation class
 */
template<typename ReactorType, typename ServiceImpl>
class GrpcAdapter : public IOAdapter<ReactorType>
{
public:
    /**
     * Construct a GrpcAdapter for the given reactor and server address.
     *
     * @param reactor Shared pointer to the reactor this adapter interfaces with
     * @param server_address gRPC server address (e.g., "0.0.0.0:50051")
     */
    GrpcAdapter(std::shared_ptr<ReactorType> reactor,
                const std::string& server_address)
        : IOAdapter<ReactorType>(reactor)
        , server_address_(server_address)
        , service_(this)
    {
    }

    /**
     * Virtual destructor ensures proper gRPC cleanup.
     * Must call stop() here to ensure cleanup happens before derived class destruction.
     */
    virtual ~GrpcAdapter()
    {
        // IMPORTANT: Call stop() here, not in base class destructor.
        // This ensures stopIOEventLoop() is called while GrpcAdapter is still valid.
        this->stop();
    }

    /**
     * Handle a gRPC RPC with request-response pattern.
     *
     * This is the primary method for RPC handlers. It:
     * 1. Schedules a logical action on the reactor with the request data
     * 2. Waits for the reactor to process and write response to output port
     * 3. Returns the response to the gRPC client
     *
     * THREAD SAFETY: Safe to call from multiple RPC handler threads concurrently
     *
     * @tparam Request gRPC request message type
     * @tparam Response gRPC response message type
     * @param request The RPC request message
     * @param response Pointer to response message (will be filled)
     * @param action_name Name of logical action to trigger on reactor
     * @param response_port Name of reactor output port for response
     * @param timeout Maximum time to wait for reactor response (default: 5s)
     * @return gRPC status (OK on success, error on timeout or failure)
     */
    template<typename Request, typename Response>
    grpc::Status handleRpc(const Request& request,
                          Response* response,
                          const std::string& action_name,
                          const std::string& response_port,
                          std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        // BOUNDARY: async gRPC → deterministic reactor
        this->scheduleReactorAction(action_name, request);

        // Wait for reactor to process and produce response
        auto result = this->template waitForReactorResponse<Response>(
            response_port, timeout
        );

        if (!result)
        {
            return grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED,
                              "Reactor response timeout");
        }

        *response = *result;
        return grpc::Status::OK;
    }

    /**
     * Get the gRPC service implementation.
     *
     * This allows access to the service for custom RPC handling patterns.
     *
     * @return Reference to the service implementation
     */
    ServiceImpl& getService()
    {
        return service_;
    }

protected:
    /**
     * Run the gRPC server event loop.
     *
     * This method is called in a dedicated thread and:
     * - Builds and starts the gRPC server
     * - Registers the service implementation
     * - Blocks waiting for RPCs until stop() is called
     */
    void runIOEventLoop() override
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
        builder.RegisterService(&service_);

        server_ = builder.BuildAndStart();
        if (!server_)
        {
            // Log error - server failed to start
            return;
        }

        // Block until Shutdown() is called
        server_->Wait();
    }

    /**
     * Stop the gRPC server event loop.
     *
     * Triggers server shutdown, causing Wait() to return and the I/O thread to exit.
     */
    void stopIOEventLoop() override
    {
        if (server_)
        {
            server_->Shutdown();
        }
    }

private:
    std::string server_address_;           ///< gRPC server address (host:port)
    ServiceImpl service_;                  ///< gRPC service implementation
    std::unique_ptr<grpc::Server> server_; ///< gRPC server instance
};

} // namespace services
