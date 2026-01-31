#pragma once

#include "mscpp/ReactorWithPorts.h"
#include "mscpp/Reaction.h"
#include "mscpp/MicroServiceContainer.h"
#include "Inputs.h"
#include <string>

/**
 * Example Services Using Port-Based I/O
 *
 * These services demonstrate the port-based communication model:
 * - Explicit InputPort and OutputPort declarations
 * - No ad-hoc sendInput() calls
 * - Automatic tag propagation through connections
 * - Type-safe port wiring
 * - Template base classes eliminate boilerplate
 */

namespace services
{

// ===========================================================================
// ServiceC - Producer with OutputPort
// ===========================================================================

DECLARE_REACTOR_NAME(ServiceC);

struct StoreC
{
    std::string state = "init";
    int counter = 0;
};

struct PortsC
{
    // Output port that produces integers
    OutputPort<int> counter_out;
};

// Forward declaration
class ServiceC;

/**
 * Heartbeat reaction that increments counter and outputs it
 */
struct HeartbeatReactionC : public Reaction<
    ServiceC,
    0,
    TypeList<HeartbeatInput>,
    TypeList<>,  // Effects declared via ports, not inputs
    TypeList<>
>
{
    void execute(StoreC& store,
                 PortsC& ports,
                 const MicroServiceContainer<>& /*container*/,
                 HeartbeatInput& input)
    {
        store.state = "running";
        store.counter++;

        // Output the counter value on the port
        // This schedules an event at next microstep
        ports.counter_out.set(store.counter);

        input.setResult(EmptyResult{});
    }
};

using ReactionsC = ReactionSet<HeartbeatReactionC>;

/**
 * ServiceC with port-based output (using template base class)
 */
class ServiceC : public ReactorWithPorts<NameServiceC, StoreC, PortsC,
                                         MicroServiceContainer<>, ReactionsC>
{
public:
    using Base = ReactorWithPorts<NameServiceC, StoreC, PortsC,
                                  MicroServiceContainer<>, ReactionsC>;
    using Base::Base;  // Inherit constructors

    void executeHeartbeat(const LogicalTag& /*tag*/)
    {
        HeartbeatInput input;
        HeartbeatReactionC reaction;
        reaction.execute(mStore, mPorts, mContainer, input);
    }
};

// ===========================================================================
// ServiceD - Consumer with InputPort
// ===========================================================================

DECLARE_REACTOR_NAME(ServiceD);

struct StoreD
{
    std::string state = "init";
    int last_received = 0;
    int receive_count = 0;
};

struct PortsD
{
    // Input port that receives integers
    InputPort<int> counter_in;
};

// Forward declaration
class ServiceD;

/**
 * Heartbeat reaction that reads from input port
 */
struct HeartbeatReactionD : public Reaction<
    ServiceD,
    0,
    TypeList<HeartbeatInput>,
    TypeList<>,
    TypeList<>
>
{
    void execute(StoreD& store,
                 PortsD& ports,
                 const MicroServiceContainer<>& /*container*/,
                 HeartbeatInput& input)
    {
        // Check if the input port has a value
        if (ports.counter_in.is_present())
        {
            store.last_received = ports.counter_in.get();
            store.receive_count++;
            store.state = "received";
        }
        else
        {
            store.state = "waiting";
        }

        input.setResult(EmptyResult{});
    }
};

using ReactionsD = ReactionSet<HeartbeatReactionD>;

/**
 * ServiceD with port-based input (using template base class)
 */
class ServiceD : public ReactorWithPorts<NameServiceD, StoreD, PortsD,
                                         MicroServiceContainer<>, ReactionsD>
{
public:
    using Base = ReactorWithPorts<NameServiceD, StoreD, PortsD,
                                  MicroServiceContainer<>, ReactionsD>;
    using Base::Base;

    void executeHeartbeat(const LogicalTag& /*tag*/)
    {
        HeartbeatInput input;
        HeartbeatReactionD reaction;
        reaction.execute(mStore, mPorts, mContainer, input);
    }

    void clearPorts()
    {
        mPorts.counter_in.clear();
    }
};

// ===========================================================================
// ServiceE - Transform (Input and Output ports)
// ===========================================================================

DECLARE_REACTOR_NAME(ServiceE);

struct StoreE
{
    std::string state = "init";
    int multiplier = 2;
};

struct PortsE
{
    InputPort<int> value_in;
    OutputPort<int> value_out;
};

// Forward declaration
class ServiceE;

/**
 * Reaction that doubles the input and outputs it
 */
struct TransformReactionE : public Reaction<
    ServiceE,
    0,
    TypeList<HeartbeatInput>,
    TypeList<>,
    TypeList<>
>
{
    void execute(StoreE& store,
                 PortsE& ports,
                 const MicroServiceContainer<>& /*container*/,
                 HeartbeatInput& input)
    {
        if (ports.value_in.is_present())
        {
            int input_value = ports.value_in.get();
            int output_value = input_value * store.multiplier;
            ports.value_out.set(output_value);
            store.state = "transformed";
        }
        else
        {
            store.state = "waiting";
        }

        input.setResult(EmptyResult{});
    }
};

using ReactionsE = ReactionSet<TransformReactionE>;

/**
 * ServiceE that transforms values (using template base class)
 */
class ServiceE : public ReactorWithPorts<NameServiceE, StoreE, PortsE,
                                         MicroServiceContainer<>, ReactionsE>
{
public:
    using Base = ReactorWithPorts<NameServiceE, StoreE, PortsE,
                                  MicroServiceContainer<>, ReactionsE>;
    using Base::Base;

    void executeHeartbeat(const LogicalTag& /*tag*/)
    {
        HeartbeatInput input;
        TransformReactionE reaction;
        reaction.execute(mStore, mPorts, mContainer, input);
    }

    void clearPorts()
    {
        mPorts.value_in.clear();
        // Note: output ports don't need clearing (they're write-only)
    }
};

} // namespace services
