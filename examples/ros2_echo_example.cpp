/**
 * Ros2Adapter Example - Echo Node
 *
 * This example demonstrates how to use Ros2Adapter to integrate a ROS2 node
 * with a deterministic mscpp reactor. The example implements a simple echo node
 * that subscribes to an input topic, processes messages in a reactor, and publishes
 * responses to an output topic.
 *
 * ARCHITECTURE:
 * - Ros2EchoAdapter: ROS2 node adapter (subscribes/publishes)
 * - EchoReactor: Deterministic FSM reactor that processes messages
 * - Ros2Adapter: Thread-safe boundary between async ROS2 and deterministic reactor
 *
 * USAGE:
 *   ./ros2_echo_example
 *   # In another terminal:
 *   ros2 topic pub /echo_input std_msgs/msg/String "data: 'Hello ROS2'"
 *   ros2 topic echo /echo_output
 */

#include "mscpp/IOAdapters/Ros2Adapter.h"
#include "mscpp/MicroServiceReactors.h"
#include "mscpp/MicroServiceContainer.h"
#include "mscpp/Ports.h"
#include "mscpp/StateSet.h"
#include "mscpp/ReactorScheduler.h"
#include "mscpp/Logging.h"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <string>
#include <memory>
#include <thread>
#include <chrono>

using namespace services;

// ===========================================================================
// Echo Reactor - Deterministic FSM that processes echo messages
// ===========================================================================

DECLARE_REACTOR_NAME(Ros2EchoReactor);

struct StoreRos2Echo
{
    int total_messages{0};
    std::string last_message;
};

struct PortsRos2Echo
{
    InputPort<std_msgs::msg::String> message_in;
    OutputPort<std_msgs::msg::String> message_out;
};

ENABLE_AUTO_CLEAR_PORTS(PortsRos2Echo, message_in);

using ContainerRos2Echo = MicroServiceContainer<>;

// FSM States
struct IdleStateRos2Echo;
struct ProcessingStateRos2Echo;

using Ros2EchoStates = StateSet<IdleStateRos2Echo, ProcessingStateRos2Echo>;

struct IdleStateRos2Echo : State<IdleStateRos2Echo, 0>
{
    static constexpr const char* name() { return "Idle"; }

    size_t step(StoreRos2Echo& s, PortsRos2Echo& p, const ContainerRos2Echo& c,
               const LogicalTag& tag,
               const StepTrigger& trigger)
    {
        (void)c;
        (void)tag;

        // Check for incoming message
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_message")
        {
            if (p.message_in.is_present())
            {
                const auto& msg = p.message_in.get();
                s.total_messages++;
                s.last_message = msg.data;

                SPDLOG_INFO("Ros2EchoReactor: Received message #{}: '{}'",
                           s.total_messages, msg.data);

                // Generate echo response
                std_msgs::msg::String response;
                response.data = "Echo [" + std::to_string(s.total_messages) + "]: " + msg.data;

                p.message_out.set(response);

                return 1;  // Transition to ProcessingState
            }
        }

        return 0;  // Stay in Idle
    }
};

struct ProcessingStateRos2Echo : State<ProcessingStateRos2Echo, 1>
{
    static constexpr const char* name() { return "Processing"; }

    size_t step(StoreRos2Echo& s, PortsRos2Echo& p, const ContainerRos2Echo& c,
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
class Ros2EchoReactor : public MicroServiceFSMReactor<
    NameRos2EchoReactor,
    StoreRos2Echo,
    PortsRos2Echo,
    ContainerRos2Echo,
    Ros2EchoStates>
{
public:
    using Base = MicroServiceFSMReactor<
        NameRos2EchoReactor,
        StoreRos2Echo,
        PortsRos2Echo,
        ContainerRos2Echo,
        Ros2EchoStates>;
    using Base::Base;
};

// ===========================================================================
// ROS2 Adapter Implementation
// ===========================================================================

class Ros2EchoAdapter : public Ros2Adapter<Ros2EchoReactor>
{
public:
    using Ros2Adapter::Ros2Adapter;

protected:
    void setupRos2Interfaces() override
    {
        SPDLOG_INFO("Setting up ROS2 interfaces...");

        // Subscribe to /echo_input topic
        // When messages arrive, trigger "on_message" logical action on reactor
        createSubscription<std_msgs::msg::String>("/echo_input", "on_message");

        // Publish reactor output to /echo_output topic
        // Note: This creates the publisher, but actual publishing needs to be
        // triggered when the reactor updates the port
        createPublisher<std_msgs::msg::String>("/echo_output", "message_out");

        // Create a timer to periodically check for reactor outputs and publish them
        // This is a simplified approach - a production system might use callbacks
        auto timer = getNode()->create_wall_timer(
            std::chrono::milliseconds(100),
            [this]() {
                // Check if reactor has output data and publish it
                publishPort("message_out");
            }
        );

        // Store timer to keep it alive
        timers_.push_back(timer);

        SPDLOG_INFO("ROS2 interfaces configured:");
        SPDLOG_INFO("  Subscribing to: /echo_input");
        SPDLOG_INFO("  Publishing to:  /echo_output");
    }

private:
    std::vector<rclcpp::TimerBase::SharedPtr> timers_;
};

// ===========================================================================
// Main - Run the Echo Node
// ===========================================================================

int main(int argc, char** argv)
{
    SPDLOG_INFO("Starting ROS2 Echo Node example...");

    // Initialize ROS2
    rclcpp::init(argc, argv);

    // Create scheduler
    auto scheduler = std::make_shared<ReactorScheduler>();

    // Create echo reactor
    auto reactor = std::make_shared<Ros2EchoReactor>();
    reactor->setScheduler(scheduler.get());

    // Register reactor with scheduler
    scheduler->registerReactor(reactor);

    // Create ROS2 adapter
    Ros2EchoAdapter adapter(reactor, "echo_node");

    SPDLOG_INFO("Starting ROS2 node...");
    adapter.start();

    // Start scheduler in background thread
    SPDLOG_INFO("Starting reactor scheduler...");
    std::thread scheduler_thread([&scheduler]() {
        scheduler->run();
    });

    // Run for demonstration
    SPDLOG_INFO("Echo node running. Test with:");
    SPDLOG_INFO("  ros2 topic pub /echo_input std_msgs/msg/String \"data: 'Hello ROS2'\"");
    SPDLOG_INFO("  ros2 topic echo /echo_output");
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

    rclcpp::shutdown();

    SPDLOG_INFO("Echo node stopped.");
    return 0;
}
