#pragma once

#include "LogicalTime.h"
#include "Reaction.h"
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <functional>

/**
 * Port-Based I/O for Reactors
 *
 * This file provides InputPort and OutputPort abstractions that replace
 * ad-hoc sendInput() calls with explicit, statically-declared port connections.
 *
 * Key Features:
 * - InputPort: Holds values present at current logical tag
 * - OutputPort: Sets values that become available at next microstep
 * - Presence semantics: Reactions check if inputs are present
 * - Automatic tag propagation: Outputs get tag = current_tag + 1 microstep
 * - Static topology: Connections declared at compile-time
 *
 * Benefits:
 * - Deterministic: Output events scheduled with explicit tags
 * - Type-safe: Connections validated at compile-time
 * - Self-documenting: Ports visible in reactor interface
 * - Analyzable: Static connection graph for visualization
 *
 * Usage:
 *   // Define ports in reactor
 *   struct MyPorts {
 *       InputPort<int> counter_in;
 *       OutputPort<int> counter_out;
 *   };
 *
 *   // In reaction:
 *   if (ports.counter_in.is_present()) {
 *       int value = ports.counter_in.get();
 *       ports.counter_out.set(value + 1);
 *   }
 */

namespace services
{

// Forward declarations
class ReactorScheduler;

// Forward declare OutputPort for InputPort::map_to method
template<typename T>
class OutputPort;

/**
 * InputPort - Holds a value present at the current logical tag
 *
 * Template Parameter:
 * - T: Type of value this port carries
 *
 * Semantics:
 * - Value is present only for one logical tag
 * - Reactions check is_present() before accessing
 * - get() returns const reference to value
 * - clear() called by scheduler at end of tag
 */
template<typename T>
class InputPort
{
public:
    using ValueType = T;

    InputPort() = default;

    /**
     * Check if this port has a value at current tag
     */
    bool is_present() const
    {
        return mValue.has_value();
    }

    /**
     * Get the value at this port (must call is_present() first)
     * @throws std::bad_optional_access if no value present
     */
    const T& get() const
    {
        return mValue.value();
    }

    /**
     * Get the value or a default if not present
     */
    T get_or(const T& default_value) const
    {
        return mValue.value_or(default_value);
    }

    /**
     * Set the value at this port (called by scheduler during event delivery)
     * This is an internal method called by the connection system.
     */
    void set(T&& value)
    {
        mValue = std::move(value);
    }

    /**
     * Set the value at this port (copy version)
     */
    void set(const T& value)
    {
        mValue = value;
    }

    /**
     * Clear the value (called by scheduler at end of tag)
     */
    void clear()
    {
        mValue.reset();
    }

    /**
     * Execute callback if port has a value (presence-conditional execution)
     *
     * Example:
     *   ports.counter_in.if_present([&](int value) {
     *       std::cout << "Received: " << value << "\n";
     *   });
     */
    template<typename F>
    void if_present(F&& callback) const
    {
        if (mValue.has_value())
        {
            callback(mValue.value());
        }
    }

    /**
     * Transform the value if present, returning std::optional<U>
     *
     * Example:
     *   auto doubled = ports.counter_in.transform([](int x) { return x * 2; });
     *   if (doubled) {
     *       ports.counter_out.set(*doubled);
     *   }
     */
    template<typename F>
    auto transform(F&& func) const -> std::optional<std::invoke_result_t<F, const T&>>
    {
        if (mValue.has_value())
        {
            return func(mValue.value());
        }
        return std::nullopt;
    }

    /**
     * Get optional reference for std::optional-like usage
     *
     * Example:
     *   if (auto value = ports.counter_in.try_get()) {
     *       // use *value
     *   }
     */
    const std::optional<T>& try_get() const
    {
        return mValue;
    }

    /**
     * Map the value to output port if present
     *
     * Example:
     *   ports.counter_in.map_to(ports.counter_out, [](int x) { return x * 2; });
     */
    template<typename U, typename F>
    void map_to(OutputPort<U>& output, F&& func) const
    {
        if (mValue.has_value())
        {
            output.set(func(mValue.value()));
        }
    }

private:
    std::optional<T> mValue;
};

/**
 * OutputPort - Sets a value that becomes available at next microstep
 *
 * Template Parameter:
 * - T: Type of value this port produces
 *
 * Semantics:
 * - set() schedules an event at current_tag + 1 microstep
 * - Value delivered to connected InputPorts at next microstep
 * - Automatic tag propagation (no manual tag management)
 * - Events enqueued in scheduler's event queue
 */
template<typename T>
class OutputPort
{
public:
    using ValueType = T;

    OutputPort() = default;

    /**
     * Set the value on this port
     * Creates a tagged event at current_tag.next_microstep()
     * The event will be delivered to connected input ports
     */
    void set(T&& value)
    {
        if (mScheduleCallback)
        {
            mScheduleCallback(std::move(value));
        }
        else
        {
            // Store for later if no scheduler connected yet
            mPendingValue = std::move(value);
            mHasPendingValue = true;
        }
    }

    /**
     * Set the value on this port (copy version)
     */
    void set(const T& value)
    {
        T value_copy = value;
        set(std::move(value_copy));
    }

    /**
     * Check if a value has been set (for testing)
     */
    bool has_pending_value() const
    {
        return mHasPendingValue;
    }

    /**
     * Get pending value (for testing)
     */
    const std::optional<T>& get_pending_value() const
    {
        return mPendingValue;
    }

    /**
     * Internal: Set the callback for scheduling events
     * Called by the connection system to wire this port to the scheduler
     */
    void setScheduleCallback(std::function<void(T&&)> callback)
    {
        mScheduleCallback = std::move(callback);

        // If we had a pending value, schedule it now
        if (mHasPendingValue && mPendingValue.has_value())
        {
            mScheduleCallback(std::move(*mPendingValue));
            mPendingValue.reset();
            mHasPendingValue = false;
        }
    }

private:
    std::function<void(T&&)> mScheduleCallback;
    std::optional<T> mPendingValue;
    bool mHasPendingValue{false};
};

/**
 * PortSet - Collection of ports for a reactor
 *
 * Template Parameters:
 * - InputPorts: TypeList of InputPort types
 * - OutputPorts: TypeList of OutputPort types
 *
 * This is a base class that reactors can inherit from or contain.
 * It provides compile-time metadata about port structure.
 */
template<typename InputPortList = EmptyTypeList,
         typename OutputPortList = EmptyTypeList>
struct PortSet
{
    using Inputs = InputPortList;
    using Outputs = OutputPortList;

    static constexpr size_t num_inputs = InputPortList::size;
    static constexpr size_t num_outputs = OutputPortList::size;
};

/**
 * Port Metadata - Describes a port with a name
 *
 * Used for declaring named ports in reactors
 */
template<typename PortType, const char* PortName>
struct NamedPort
{
    using Type = PortType;
    static constexpr const char* name = PortName;

    PortType port;

    PortType& get() { return port; }
    const PortType& get() const { return port; }
};

/**
 * Helper macro for declaring named ports
 *
 * Usage:
 *   DECLARE_PORT_NAME(counter);
 *   struct MyPorts {
 *       NamedPort<InputPort<int>, counter_name> counter;
 *   };
 */
#define DECLARE_PORT_NAME(name) \
    static constexpr char name##_name[] = #name

/**
 * Port Reference - Identifies a specific port on a specific reactor
 *
 * Used in Connection declarations for the static topology
 */
template<typename ReactorType, typename PortType>
struct PortRef
{
    using Reactor = ReactorType;
    using Port = PortType;
};

/**
 * Helper to extract the value type from a port
 */
template<typename Port>
struct PortValueType;

template<typename T>
struct PortValueType<InputPort<T>>
{
    using type = T;
};

template<typename T>
struct PortValueType<OutputPort<T>>
{
    using type = T;
};

template<typename Port>
using PortValueType_t = typename PortValueType<Port>::type;

/**
 * Type trait to check if a type is an InputPort
 */
template<typename T>
struct IsInputPort : std::false_type {};

template<typename T>
struct IsInputPort<InputPort<T>> : std::true_type {};

/**
 * Type trait to check if a type is an OutputPort
 */
template<typename T>
struct IsOutputPort : std::false_type {};

template<typename T>
struct IsOutputPort<OutputPort<T>> : std::true_type {};

/**
 * Clear all input ports in a port set using template metaprogramming
 * Called by scheduler at end of each tag
 *
 * This function uses compile-time reflection to automatically clear
 * all InputPort<T> members in the ports structure.
 */
namespace detail
{
    // Helper to check if a member is an InputPort and clear it
    template<typename T>
    void clearIfInputPort(T& member)
    {
        if constexpr (IsInputPort<std::remove_reference_t<T>>::value)
        {
            member.clear();
        }
        // OutputPorts and other types are ignored
    }

    // Fold expression to clear all members
    template<typename PortSetType, typename... Members>
    void clearAllInputPortsImpl(PortSetType& ports, Members PortSetType::*... members)
    {
        (clearIfInputPort(ports.*members), ...);
    }
}

/**
 * Macro to define automatic port clearing for a Ports struct.
 *
 * Usage:
 *   struct MyPorts {
 *       InputPort<int> counter_in;
 *       OutputPort<int> counter_out;
 *       InputPort<std::string> msg_in;
 *   };
 *   ENABLE_AUTO_CLEAR_PORTS(MyPorts, counter_in, msg_in)
 *
 * This generates a clearAllInputPorts() specialization that automatically
 * clears the specified input ports without manual implementation.
 */
#define ENABLE_AUTO_CLEAR_PORTS(PortsType, ...)                          \
    namespace services {                                                  \
    template<>                                                            \
    inline void clearInputPorts<PortsType>(PortsType& ports)            \
    {                                                                     \
        detail::clearAllInputPortsImpl(ports, &PortsType::__VA_ARGS__); \
    }                                                                     \
    }

/**
 * Generic fallback that does nothing (for backwards compatibility)
 * Override this for your specific port types using ENABLE_AUTO_CLEAR_PORTS
 */
template<typename PortSetType>
void clearInputPorts([[maybe_unused]] PortSetType& ports)
{
    // Default: no-op (backwards compatible)
    // Use ENABLE_AUTO_CLEAR_PORTS macro to enable automatic clearing
}

} // namespace services
