#pragma once

#include "Ports.h"
#include "Reaction.h"
#include "ReactorScheduler.h"
#include <type_traits>
#include <functional>
#include <unordered_map>
#include <vector>

/**
 * Static Connection Topology
 *
 * This file provides compile-time declarations for port-to-port connections
 * between reactors, creating a static topology that can be validated and
 * analyzed at compile-time.
 *
 * Key Features:
 * - Connection<From, To>: Declare a connection between two ports
 * - ConnectionSet<Connections...>: Collection of connections
 * - Type validation: Ensures compatible port types
 * - Automatic event routing: Values flow through connections
 * - Dependency inference: Connections imply reaction dependencies
 *
 * Benefits:
 * - Compile-time validation of connections
 * - Static topology analysis
 * - Automatic dependency graph construction
 * - Self-documenting system structure
 *
 * Usage:
 *   // Define connection
 *   using MyConnection = Connection<
 *       PortRef<ReactorA, decltype(ReactorA::ports.output)>,
 *       PortRef<ReactorB, decltype(ReactorB::ports.input)>
 *   >;
 *
 *   // Define system topology
 *   using SystemTopology = ConnectionSet<
 *       MyConnection,
 *       OtherConnection
 *   >;
 */

namespace services
{

/**
 * Connection - Declares a connection from one port to another
 *
 * Template Parameters:
 * - FromPortRef: PortRef<ReactorA, OutputPort<T>>
 * - ToPortRef: PortRef<ReactorB, InputPort<T>>
 *
 * Static validation ensures:
 * - From is an OutputPort
 * - To is an InputPort
 * - Value types match
 */
template<typename FromPortRef, typename ToPortRef>
struct Connection
{
    using From = FromPortRef;
    using To = ToPortRef;

    using FromReactor = typename FromPortRef::Reactor;
    using ToReactor = typename ToPortRef::Reactor;
    using FromPort = typename FromPortRef::Port;
    using ToPort = typename ToPortRef::Port;

    // Static assertions to validate connection
    static_assert(IsOutputPort<FromPort>::value,
                  "Connection source must be an OutputPort");
    static_assert(IsInputPort<ToPort>::value,
                  "Connection destination must be an InputPort");

    // Extract value types
    using FromValueType = PortValueType_t<FromPort>;
    using ToValueType = PortValueType_t<ToPort>;

    // Ensure types are compatible
    static_assert(std::is_convertible_v<FromValueType, ToValueType>,
                  "Connection port types must be compatible");
};

/**
 * ConnectionSet - Collection of connections
 *
 * Template Parameters:
 * - Connections...: Pack of Connection<From, To> types
 */
template<typename... Connections>
struct ConnectionSet
{
    using Tuple = std::tuple<Connections...>;
    static constexpr size_t size = sizeof...(Connections);

    /**
     * Check if a connection exists from a specific reactor to another
     */
    template<typename FromReactor, typename ToReactor>
    static constexpr bool has_connection()
    {
        return ((std::is_same_v<FromReactor, typename Connections::FromReactor> &&
                 std::is_same_v<ToReactor, typename Connections::ToReactor>) || ...);
    }
};

/**
 * ConnectionManager - Runtime management of port connections
 *
 * Wires up OutputPorts to InputPorts and manages event delivery
 * through the scheduler.
 */
class ConnectionManager
{
public:
    ConnectionManager(ReactorScheduler* scheduler)
        : mScheduler(scheduler)
    {
    }

    /**
     * Connect an OutputPort to an InputPort
     *
     * Sets up the OutputPort's callback to schedule events that
     * deliver values to the InputPort at the next microstep.
     *
     * Template Parameters:
     * - T: Value type of the ports
     * - FromReactorId: ID of source reactor
     * - ToReactorId: ID of destination reactor
     */
    template<typename T>
    void connect(OutputPort<T>& output_port,
                 InputPort<T>& input_port,
                 size_t from_reactor_id,
                 size_t to_reactor_id,
                 const std::string& connection_name)
    {
        // Store connection metadata
        ConnectionInfo info;
        info.from_reactor_id = from_reactor_id;
        info.to_reactor_id = to_reactor_id;
        info.connection_name = connection_name;
        mConnections.push_back(info);

        // Set up the callback on the output port
        output_port.setScheduleCallback(
            [this, &input_port, to_reactor_id, connection_name](T&& value)
            {
                // Schedule event to deliver value to input port
                this->schedulePortEvent(std::move(value), input_port,
                                       to_reactor_id, connection_name);
            }
        );
    }

private:
    struct ConnectionInfo
    {
        size_t from_reactor_id;
        size_t to_reactor_id;
        std::string connection_name;
    };

public:
    /**
     * Get list of all connections (for debugging/visualization)
     */
    const std::vector<ConnectionInfo>& getConnections() const
    {
        return mConnections;
    }

    /**
     * Print all connections
     */
    void printConnections() const
    {
        LOG_INFO("ConnectionManager: {} connections registered", mConnections.size());
        for (const auto& conn : mConnections)
        {
            LOG_INFO("  Reactor {} -> Reactor {}: {}",
                     conn.from_reactor_id,
                     conn.to_reactor_id,
                     conn.connection_name);
        }
    }

private:
    /**
     * Schedule an event to deliver a value to an input port
     */
    template<typename T>
    void schedulePortEvent(T&& value,
                          InputPort<T>& input_port,
                          size_t reactor_id,
                          const std::string& connection_name)
    {
        if (!mScheduler)
        {
            LOG_ERROR("ConnectionManager: No scheduler set for connection: {}",
                     connection_name);
            return;
        }

        // Get current tag from scheduler
        LogicalTag current_tag = mScheduler->getCurrentTag();

        // Compute next tag (same time, next microstep)
        LogicalTag next_tag = current_tag.next_microstep();

        // Schedule event in scheduler to deliver value to input port
        mScheduler->scheduleEvent(next_tag, reactor_id,
            [&input_port, v = std::move(value)]() mutable
            {
                input_port.set(std::move(v));
            }
        );
    }

    ReactorScheduler* mScheduler;
    std::vector<ConnectionInfo> mConnections;
};

/**
 * Helper to build connections from a ConnectionSet
 *
 * This would be specialized for specific connection sets to wire up
 * the actual port instances at runtime.
 */
template<typename ConnectionSetType>
class TopologyBuilder
{
public:
    TopologyBuilder(ConnectionManager& manager)
        : mManager(manager)
    {
    }

    /**
     * Build connections for a specific reactor system
     * This is typically called by the reactor factory after all
     * reactors are created.
     */
    template<typename... Reactors>
    void buildConnections(Reactors&... reactors)
    {
        // This would use template metaprogramming to iterate over
        // ConnectionSetType and wire up the actual port instances
        // For now, this is a placeholder that derived classes override
    }

private:
    ConnectionManager& mManager;
};

/**
 * Reactor Factory with Topology Support
 *
 * Extends the reactor factory to automatically wire up port connections
 * based on a static topology.
 */
template<typename ConnectionSetType, typename... Reactors>
class ReactorFactoryWithTopology
{
public:
    ReactorFactoryWithTopology()
        : mScheduler()
        , mConnectionManager(&mScheduler)
    {
        // Create reactors (implementation specific)
    }

    /**
     * Wire up all connections in the topology
     */
    void buildTopology()
    {
        TopologyBuilder<ConnectionSetType> builder(mConnectionManager);
        // Would wire up actual port instances here
    }

    /**
     * Get the connection manager
     */
    ConnectionManager& getConnectionManager()
    {
        return mConnectionManager;
    }

    /**
     * Get the scheduler
     */
    ReactorScheduler& getScheduler()
    {
        return mScheduler;
    }

private:
    ReactorScheduler mScheduler;
    ConnectionManager mConnectionManager;
};

} // namespace services
