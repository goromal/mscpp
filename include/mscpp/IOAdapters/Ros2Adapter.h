#pragma once

#include "IOAdapter.h"
#include <rclcpp/rclcpp.hpp>
#include <memory>
#include <string>
#include <map>
#include <functional>

namespace services
{

/**
 * Ros2Adapter - Thread-safe boundary between ROS2 and deterministic reactors
 *
 * The Ros2Adapter provides a concrete implementation of IOAdapter for integrating
 * ROS2 nodes with mscpp's deterministic reactor model. It manages a ROS2 node
 * in a dedicated thread and provides thread-safe communication with the reactor.
 *
 * ARCHITECTURAL ROLE:
 * - Runs ROS2 executor spin loop in a dedicated thread
 * - Routes incoming topic messages to reactor via logical actions
 * - Routes reactor outputs back to ROS2 topics
 * - Maintains deterministic reactor execution despite async message arrivals
 *
 * USAGE PATTERN:
 * 1. Inherit from Ros2Adapter<YourReactorType>
 * 2. Implement setupRos2Interfaces() to create subscriptions/publishers/services
 * 3. Use createSubscription() to route topics → reactor logical actions
 * 4. Use createPublisher() to route reactor ports → ROS2 topics
 * 5. Call start() to run the ROS2 node in background thread
 *
 * THREAD SAFETY:
 * - ROS2 executor runs in separate I/O thread
 * - Subscription callbacks are thread-safe (use IOAdapter's synchronization)
 * - Multiple concurrent messages are supported
 *
 * EXAMPLE:
 * ```cpp
 * class MyRos2Adapter : public Ros2Adapter<MyReactor> {
 * public:
 *     using Ros2Adapter::Ros2Adapter;
 *
 * protected:
 *     void setupRos2Interfaces() override {
 *         // Route "/input_topic" messages to "on_message" logical action
 *         createSubscription<std_msgs::msg::String>("/input_topic", "on_message");
 *
 *         // Publish "output_port" reactor port to "/output_topic"
 *         createPublisher<std_msgs::msg::String>("/output_topic", "output_port");
 *     }
 * };
 *
 * // Create adapter and start
 * auto reactor = std::make_shared<MyReactor>();
 * MyRos2Adapter adapter(reactor, "my_node");
 * adapter.start();  // ROS2 node runs in background thread
 * ```
 *
 * Template Parameters:
 * - ReactorType: The reactor type this adapter interfaces with
 */
template<typename ReactorType>
class Ros2Adapter : public IOAdapter<ReactorType>
{
public:
    /**
     * Construct a Ros2Adapter for the given reactor and node name.
     *
     * @param reactor Shared pointer to the reactor this adapter interfaces with
     * @param node_name ROS2 node name
     */
    Ros2Adapter(std::shared_ptr<ReactorType> reactor,
                const std::string& node_name)
        : IOAdapter<ReactorType>(reactor)
        , node_name_(node_name)
    {
    }

    /**
     * Virtual destructor ensures proper ROS2 cleanup.
     * Must call stop() here to ensure cleanup happens before derived class destruction.
     */
    virtual ~Ros2Adapter()
    {
        // IMPORTANT: Call stop() here, not in base class destructor.
        // This ensures stopIOEventLoop() is called while Ros2Adapter is still valid.
        this->stop();
    }

    /**
     * Get the ROS2 node.
     *
     * Use this to access the node for custom ROS2 operations.
     * Only valid after start() has been called.
     *
     * @return Shared pointer to the ROS2 node (may be null before start())
     */
    rclcpp::Node::SharedPtr getNode()
    {
        return node_;
    }

protected:
    /**
     * Run the ROS2 node event loop.
     *
     * This method is called in a dedicated thread and:
     * - Initializes rclcpp if not already done
     * - Creates the ROS2 node
     * - Calls setupRos2Interfaces() to create subscriptions/publishers
     * - Spins the executor until shutdown
     */
    void runIOEventLoop() override
    {
        // Initialize rclcpp if not already done
        if (!rclcpp::ok())
        {
            rclcpp::init(0, nullptr);
        }

        // Create ROS2 node
        node_ = rclcpp::Node::make_shared(node_name_);

        // Let subclass set up subscriptions, publishers, services, etc.
        setupRos2Interfaces();

        // Spin until shutdown
        rclcpp::spin(node_);
    }

    /**
     * Stop the ROS2 node event loop.
     *
     * Triggers rclcpp shutdown, causing spin() to return and the I/O thread to exit.
     */
    void stopIOEventLoop() override
    {
        if (rclcpp::ok())
        {
            rclcpp::shutdown();
        }
    }

    /**
     * Pure virtual: Set up ROS2 subscriptions, publishers, and services.
     *
     * Implement this method to configure your ROS2 interfaces using the
     * helper methods createSubscription() and createPublisher().
     *
     * Called once when the I/O thread starts, after the node is created.
     *
     * Example:
     * ```cpp
     * void setupRos2Interfaces() override {
     *     createSubscription<std_msgs::msg::String>("/input", "on_message");
     *     createPublisher<std_msgs::msg::String>("/output", "output_port");
     * }
     * ```
     */
    virtual void setupRos2Interfaces() = 0;

    /**
     * Create a ROS2 subscription that routes messages to reactor logical actions.
     *
     * When a message arrives on the topic, it triggers a logical action on the reactor
     * with the message data. The reactor can then process it deterministically.
     *
     * USAGE PATTERN:
     * - Call from setupRos2Interfaces()
     * - MsgType is a ROS2 message type (e.g., std_msgs::msg::String)
     * - topic is the ROS2 topic name (e.g., "/sensor_data")
     * - action_name is the logical action to trigger on reactor
     *
     * @tparam MsgType ROS2 message type
     * @param topic ROS2 topic name
     * @param action_name Logical action name to trigger on reactor
     * @param qos_depth QoS history depth (default: 10)
     */
    template<typename MsgType>
    void createSubscription(const std::string& topic,
                           const std::string& action_name,
                           size_t qos_depth = 10)
    {
        auto callback = [this, action_name](const typename MsgType::SharedPtr msg) {
            // BOUNDARY: async ROS2 → deterministic reactor
            this->scheduleReactorAction(action_name, *msg);
        };

        node_->template create_subscription<MsgType>(topic, qos_depth, callback);
    }

    /**
     * Create a ROS2 publisher that publishes reactor output port data.
     *
     * After the reactor writes to the specified output port, this publisher
     * will publish the data to the ROS2 topic.
     *
     * NOTE: This is a simplified implementation. For production, you'd need to
     * poll the reactor ports periodically or receive notifications when ports update.
     *
     * USAGE PATTERN:
     * - Call from setupRos2Interfaces()
     * - MsgType is a ROS2 message type (e.g., std_msgs::msg::String)
     * - topic is the ROS2 topic name (e.g., "/output_data")
     * - port_name is the reactor output port to publish from
     *
     * @tparam MsgType ROS2 message type
     * @param topic ROS2 topic name
     * @param port_name Reactor output port name
     * @param qos_depth QoS history depth (default: 10)
     */
    template<typename MsgType>
    void createPublisher(const std::string& topic,
                        const std::string& port_name,
                        size_t qos_depth = 10)
    {
        auto publisher = node_->template create_publisher<MsgType>(topic, qos_depth);

        // Store publisher with port association
        // In a real implementation, you'd need a mechanism to trigger publishing
        // when the reactor updates the port. This could be done via:
        // 1. A periodic timer that checks ports and publishes
        // 2. A notification callback from the reactor
        // 3. An explicit publishPort(port_name) method called by reactor
        publishers_[port_name] = [publisher, port_name, this]() {
            // This is a placeholder - actual implementation depends on
            // how to extract data from reactor output ports
            // auto data = this->getReactor()->getPorts().getOutputData<MsgType>(port_name);
            // if (data) {
            //     publisher->publish(*data);
            // }
        };
    }

    /**
     * Manually trigger publication of a reactor port's data.
     *
     * Call this from your reactor or adapter when you want to publish
     * the current value of an output port to ROS2.
     *
     * @param port_name Name of the port to publish
     */
    void publishPort(const std::string& port_name)
    {
        auto it = publishers_.find(port_name);
        if (it != publishers_.end())
        {
            it->second(); // Invoke publisher callback
        }
    }

private:
    std::string node_name_;                                    ///< ROS2 node name
    rclcpp::Node::SharedPtr node_;                            ///< ROS2 node instance
    std::map<std::string, std::function<void()>> publishers_; ///< Port name → publisher callback
};

} // namespace services
