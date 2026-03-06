#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <optional>
#include <string>
#include <any>

namespace services
{

/**
 * IOAdapter - Thread-safe boundary between async I/O frameworks and deterministic reactors
 *
 * The IOAdapter provides a base class for integrating asynchronous I/O frameworks
 * (like gRPC, ROS2, ZeroMQ) with mscpp's deterministic reactor model. It manages
 * a separate I/O thread and provides thread-safe communication primitives.
 *
 * ARCHITECTURAL ROLE:
 * - Runs I/O framework event loop in a dedicated thread
 * - Provides thread-safe logical action scheduling from I/O thread → reactor
 * - Provides thread-safe port data retrieval from reactor → I/O thread
 * - Maintains clear separation between non-deterministic I/O and deterministic reactor logic
 *
 * USAGE PATTERN:
 * 1. Create concrete adapter by inheriting from IOAdapter<YourReactorType>
 * 2. Implement runIOEventLoop() to run your framework's event loop
 * 3. Implement stopIOEventLoop() to cleanly shutdown the event loop
 * 4. Use scheduleReactorAction() to send events from I/O thread to reactor
 * 5. Use waitForReactorResponse() to synchronously wait for reactor output
 *
 * THREAD SAFETY:
 * - start()/stop() are safe to call from any thread
 * - scheduleReactorAction() is safe to call from I/O thread
 * - waitForReactorResponse() is safe to call from I/O thread
 * - All methods use appropriate synchronization primitives
 *
 * EXAMPLE (gRPC Service):
 * ```cpp
 * class GrpcJobServerAdapter : public IOAdapter<JobServerReactor> {
 * public:
 *     GrpcJobServerAdapter(std::shared_ptr<JobServerReactor> reactor)
 *         : IOAdapter(reactor) {
 *         // Initialize gRPC server
 *     }
 *
 * protected:
 *     void runIOEventLoop() override {
 *         // Run gRPC completion queue loop
 *         while (running_) {
 *             grpc_server_->HandleRpcs();
 *         }
 *     }
 *
 *     void stopIOEventLoop() override {
 *         grpc_server_->Shutdown();
 *     }
 *
 *     // In RPC handler:
 *     void HandleJobRequest(const JobRequest& req) {
 *         // Schedule logical action on reactor
 *         scheduleReactorAction("job_request", req);
 *
 *         // Wait for reactor to process and produce output
 *         auto response = waitForReactorResponse<JobResponse>(
 *             "job_response_out",
 *             std::chrono::seconds(5)
 *         );
 *         // Send gRPC response
 *     }
 * };
 * ```
 *
 * Template Parameters:
 * - ReactorType: The reactor type this adapter interfaces with
 */
template<typename ReactorType>
class IOAdapter
{
public:
    /**
     * Construct an IOAdapter for the given reactor.
     *
     * @param reactor Shared pointer to the reactor this adapter interfaces with
     */
    explicit IOAdapter(std::shared_ptr<ReactorType> reactor)
        : reactor_(reactor)
        , running_(false)
    {
    }

    /**
     * Virtual destructor ensures proper cleanup.
     *
     * NOTE: Does NOT call stop() automatically to avoid calling pure virtual
     * functions during destruction. Derived classes MUST call stop() in their
     * own destructors if they override runIOEventLoop() or stopIOEventLoop().
     *
     * This follows C++ best practices: never call virtual functions from
     * constructors or destructors.
     */
    virtual ~IOAdapter()
    {
        // Do NOT call stop() here - it would call pure virtual stopIOEventLoop()
        // after the derived class has been destroyed, causing undefined behavior.
        //
        // Derived classes must call stop() in their own destructors.
    }

    // Prevent copying (adapter manages thread resources)
    IOAdapter(const IOAdapter&) = delete;
    IOAdapter& operator=(const IOAdapter&) = delete;

    // Allow moving
    IOAdapter(IOAdapter&&) = default;
    IOAdapter& operator=(IOAdapter&&) = default;

    /**
     * Start the I/O adapter's event loop thread.
     *
     * Creates a new thread running runIOEventLoop(). Safe to call multiple times
     * (subsequent calls are no-ops if already running).
     *
     * Thread-safe: Yes
     */
    void start()
    {
        if (running_.exchange(true))
        {
            return; // Already running
        }

        io_thread_ = std::thread([this]() {
            this->runIOEventLoop();
        });
    }

    /**
     * Stop the I/O adapter's event loop thread.
     *
     * Signals the I/O thread to stop, calls stopIOEventLoop() to clean up
     * framework resources, then joins the thread. Safe to call multiple times
     * (subsequent calls are no-ops if already stopped).
     *
     * Thread-safe: Yes
     */
    void stop()
    {
        if (!running_.exchange(false))
        {
            return; // Already stopped
        }

        stopIOEventLoop();

        if (io_thread_.joinable())
        {
            io_thread_.join();
        }
    }

    /**
     * Check if the I/O adapter is currently running.
     *
     * @return true if the I/O event loop is running
     */
    bool isRunning() const
    {
        return running_.load();
    }

protected:
    /**
     * Schedule a logical action on the reactor (I/O thread → reactor).
     *
     * This is the primary way for the I/O thread to send events to the reactor.
     * The action will be queued and processed on the next reactor heartbeat.
     *
     * USAGE PATTERN:
     * - Call from I/O thread when external events arrive
     * - The reactor's logical action handler will be invoked with the data
     * - Use scheduleLogicalAction() on reactor to trigger FSM transition
     *
     * Thread-safe: Yes (uses reactor's thread-safe scheduling)
     *
     * @tparam ActionData Type of data to send with the action
     * @param action_name Name of the logical action to schedule
     * @param data Data payload for the action
     */
    template<typename ActionData>
    void scheduleReactorAction(const std::string& action_name, ActionData&& data)
    {
        // Forward to reactor's thread-safe scheduling mechanism
        reactor_->template scheduleLogicalActionWithData<ActionData>(
            action_name,
            std::forward<ActionData>(data)
        );
    }

    /**
     * Wait for reactor to produce output on a port (reactor → I/O thread).
     *
     * This enables synchronous request-response patterns from the I/O thread.
     * The I/O thread blocks until the reactor produces data on the specified
     * output port or the timeout expires.
     *
     * USAGE PATTERN:
     * 1. I/O thread receives external request (e.g., gRPC call)
     * 2. I/O thread calls scheduleReactorAction() to notify reactor
     * 3. I/O thread calls waitForReactorResponse() to get the result
     * 4. Reactor processes action and writes to output port
     * 5. waitForReactorResponse() returns with the data
     * 6. I/O thread sends response to external framework
     *
     * Thread-safe: Yes (uses condition variable)
     *
     * @tparam T Type of data expected on the port
     * @param port_name Name of the output port to read from
     * @param timeout Maximum time to wait for data
     * @return Optional containing the data if available, nullopt on timeout
     */
    template<typename T>
    std::optional<T> waitForReactorResponse(
        const std::string& port_name,
        std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(response_mutex_);

        // Wait for port to have data or timeout
        bool has_data = response_cv_.wait_for(lock, timeout, [&]() {
            return hasPortData(port_name);
        });

        if (!has_data)
        {
            return std::nullopt; // Timeout
        }

        // Extract data from port
        return getPortData<T>(port_name);
    }

    /**
     * Notify waiting threads that reactor has updated ports.
     *
     * Call this from your reactor after writing to output ports to wake up
     * any I/O threads waiting in waitForReactorResponse().
     *
     * Thread-safe: Yes
     */
    void notifyPortUpdates()
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        response_cv_.notify_all();
    }

    /**
     * Pure virtual: Implement your I/O framework's event loop.
     *
     * This method runs in a dedicated thread and should:
     * - Initialize your I/O framework (gRPC server, ROS2 node, etc.)
     * - Run the framework's event loop
     * - Respond to events by calling scheduleReactorAction()
     * - Check running_ flag periodically to exit when stop() is called
     *
     * Example:
     * ```cpp
     * void runIOEventLoop() override {
     *     while (running_) {
     *         // Poll for events
     *         auto event = framework_->pollEvent(100ms);
     *         if (event) {
     *             scheduleReactorAction("on_event", event);
     *         }
     *     }
     * }
     * ```
     */
    virtual void runIOEventLoop() = 0;

    /**
     * Pure virtual: Clean up I/O framework resources.
     *
     * This method is called from stop() before joining the I/O thread.
     * It should signal your event loop to exit and clean up resources.
     *
     * Example:
     * ```cpp
     * void stopIOEventLoop() override {
     *     grpc_server_->Shutdown();
     *     ros_node_->shutdown();
     * }
     * ```
     */
    virtual void stopIOEventLoop() = 0;

    /**
     * Access the underlying reactor.
     *
     * Use with caution - prefer scheduleReactorAction() and waitForReactorResponse()
     * for thread-safe communication.
     *
     * @return Shared pointer to the reactor
     */
    std::shared_ptr<ReactorType> getReactor()
    {
        return reactor_;
    }

    /**
     * Access the underlying reactor (const version).
     *
     * @return Const shared pointer to the reactor
     */
    std::shared_ptr<const ReactorType> getReactor() const
    {
        return reactor_;
    }

    /**
     * Check if running flag is set.
     *
     * Use this in runIOEventLoop() to check if stop() has been called.
     *
     * @return true if adapter should continue running
     */
    bool isRunningFlag() const
    {
        return running_.load();
    }

private:
    /**
     * Helper: Check if a port has data available.
     *
     * @param port_name Name of the port to check
     * @return true if port has data
     */
    bool hasPortData(const std::string& port_name) const
    {
        // This will need to be implemented based on reactor's port API
        // For now, we assume reactor provides a way to check port presence
        // This is a placeholder - actual implementation depends on Ports API
        (void)port_name;
        return false; // TODO: Implement based on actual Ports API
    }

    /**
     * Helper: Get data from a port.
     *
     * @tparam T Type of data on the port
     * @param port_name Name of the port to read
     * @return Data from the port
     */
    template<typename T>
    T getPortData(const std::string& port_name) const
    {
        // This will need to be implemented based on reactor's port API
        // For now, this is a placeholder
        // Actual implementation depends on Ports API
        (void)port_name;
        return T{}; // TODO: Implement based on actual Ports API
    }

    std::shared_ptr<ReactorType> reactor_;  ///< Reactor this adapter interfaces with
    std::atomic<bool> running_;              ///< Flag indicating if I/O thread is running

    std::thread io_thread_;                  ///< Thread running I/O event loop

    std::mutex response_mutex_;              ///< Mutex for port response synchronization
    std::condition_variable response_cv_;    ///< Condition variable for port updates
};

} // namespace services
