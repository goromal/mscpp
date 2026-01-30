#pragma once

#include "mscpp/ReactorWithPorts.h"
#include "mscpp/StateSet.h"
#include "Inputs.h"

/**
 * Example: FSM-Based Service with Ports
 *
 * Demonstrates how to combine:
 * - Port-based I/O (InputPort, OutputPort)
 * - FSM state transitions (StateSet)
 * - Multiple reactions per state
 * - Template-based reactor to eliminate boilerplate
 *
 * This service has 3 states (Init, Running, Stopped) and uses ports
 * to communicate instead of sendInput().
 */

namespace services
{

// ===========================================================================
// ServiceF - FSM with Ports
// ===========================================================================

DECLARE_REACTOR_NAME(ServiceF);

struct StoreFSM
{
    std::string current_state = "init";
    int counter = 0;
    int last_input_value = 0;
};

struct PortsFSM
{
    InputPort<int> value_in;      // Receives values
    OutputPort<int> counter_out;  // Outputs counter
    OutputPort<std::string> state_out;  // Outputs state changes
};

// Forward declarations for states
struct InitStateFSM;
struct RunningStateFSM;
struct StoppedStateFSM;

/**
 * InitStateFSM - Initial state
 *
 * Handles:
 * - Heartbeat: Do nothing
 * - IncrementInput: Increment counter, output on port, transition to Running
 * - TransitionInput: Check target state and transition
 */
struct InitStateFSM : public State<InitStateFSM, 0>
{
    // Heartbeat reaction
    size_t step(StoreFSM& store, PortsFSM& ports,
                const MicroServiceContainer<>& container,
                HeartbeatInput& input)
    {
        store.current_state = "init";
        input.setResult(EmptyResult{});
        return index();  // Stay in Init
    }

    // Increment reaction
    size_t step(StoreFSM& store, PortsFSM& ports,
                const MicroServiceContainer<>& container,
                IncrementInput& input)
    {
        store.counter++;

        // Output counter value on port
        ports.counter_out.set(store.counter);

        // Output state change
        ports.state_out.set(std::string("running"));

        input.setResult(BooleanResult{true});

        // Transition to Running state
        return RunningStateFSM::index;
    }

    // Transition reaction
    size_t step(StoreFSM& store, PortsFSM& ports,
                const MicroServiceContainer<>& container,
                TransitionInput& input)
    {
        size_t target = input.state();

        // Output state change
        if (target == RunningStateFSM::index) {
            ports.state_out.set(std::string("running"));
        } else if (target == StoppedStateFSM::index) {
            ports.state_out.set(std::string("stopped"));
        }

        input.setResult(BooleanResult{true});
        return target;
    }
};

/**
 * RunningStateFSM - Running state
 *
 * Handles:
 * - Heartbeat: Read from input port if available
 * - IncrementInput: Increment counter and output
 * - TransitionInput: Transition to other states
 */
struct RunningStateFSM : public State<RunningStateFSM, 1>
{
    // Heartbeat reaction - check input port
    size_t step(StoreFSM& store, PortsFSM& ports,
                const MicroServiceContainer<>& container,
                HeartbeatInput& input)
    {
        store.current_state = "running";

        // Check if input port has a value
        if (ports.value_in.is_present()) {
            int value = ports.value_in.get();
            store.last_input_value = value;
            store.counter += value;

            // Output updated counter
            ports.counter_out.set(store.counter);
        }

        input.setResult(EmptyResult{});
        return index();  // Stay in Running
    }

    // Increment reaction
    size_t step(StoreFSM& store, PortsFSM& ports,
                const MicroServiceContainer<>& container,
                IncrementInput& input)
    {
        store.counter++;
        ports.counter_out.set(store.counter);

        input.setResult(BooleanResult{true});
        return index();  // Stay in Running
    }

    // Transition reaction
    size_t step(StoreFSM& store, PortsFSM& ports,
                const MicroServiceContainer<>& container,
                TransitionInput& input)
    {
        size_t target = input.state();

        // Output state change
        if (target == InitStateFSM::index) {
            ports.state_out.set(std::string("init"));
        } else if (target == StoppedStateFSM::index) {
            ports.state_out.set(std::string("stopped"));
        }

        input.setResult(BooleanResult{true});
        return target;
    }
};

/**
 * StoppedStateFSM - Stopped state
 *
 * Handles:
 * - Heartbeat: Do nothing
 * - IncrementInput: Rejected (no-op)
 * - TransitionInput: Can transition back to other states
 */
struct StoppedStateFSM : public State<StoppedStateFSM, 2>
{
    // Heartbeat reaction
    size_t step(StoreFSM& store, PortsFSM& ports,
                const MicroServiceContainer<>& container,
                HeartbeatInput& input)
    {
        store.current_state = "stopped";
        input.setResult(EmptyResult{});
        return index();  // Stay in Stopped
    }

    // Increment reaction - rejected in stopped state
    size_t step(StoreFSM& store, PortsFSM& ports,
                const MicroServiceContainer<>& container,
                IncrementInput& input)
    {
        // Rejected
        input.setResult(BooleanResult{false});
        return index();  // Stay in Stopped
    }

    // Transition reaction
    size_t step(StoreFSM& store, PortsFSM& ports,
                const MicroServiceContainer<>& container,
                TransitionInput& input)
    {
        size_t target = input.state();

        // Output state change
        if (target == InitStateFSM::index) {
            ports.state_out.set(std::string("init"));
        } else if (target == RunningStateFSM::index) {
            ports.state_out.set(std::string("running"));
        }

        input.setResult(BooleanResult{true});
        return target;
    }
};

// StateSet combining all states
using StatesFSM = StateSet<InitStateFSM, RunningStateFSM, StoppedStateFSM>;

/**
 * ServiceF - FSM-based reactor with ports
 *
 * Uses ReactorWithPortsAndFSM template to eliminate boilerplate.
 * State step() functions have signature:
 *   size_t step(Store& store, Ports& ports, const Container& c, InputType& input)
 *
 * This allows states to access both store and ports.
 */
class ServiceF : public ReactorWithPortsAndFSM<NameServiceF, StoreFSM, PortsFSM,
                                                MicroServiceContainer<>, StatesFSM>
{
public:
    using Base = ReactorWithPortsAndFSM<NameServiceF, StoreFSM, PortsFSM,
                                        MicroServiceContainer<>, StatesFSM>;
    using Base::Base;  // Inherit constructors

    /**
     * Execute heartbeat
     */
    void executeHeartbeat(const LogicalTag& tag)
    {
        HeartbeatInput input;
        executeInput(input);
    }

    /**
     * Execute increment
     */
    void executeIncrement()
    {
        IncrementInput input;
        executeInput(input);
    }

    /**
     * Execute transition
     */
    void executeTransition(size_t target_state)
    {
        TransitionInput input(target_state);
        executeInput(input);
    }

    /**
     * Clear ports at end of tag
     */
    void clearPorts()
    {
        mPorts.value_in.clear();
        // Output ports don't need clearing
    }
};

// ===========================================================================
// Example: Pure Function + Reaction Model with Ports
// ===========================================================================

DECLARE_REACTOR_NAME(ServiceG);

struct StoreG
{
    int accumulator = 0;
    int multiply_factor = 1;
};

struct PortsG
{
    InputPort<int> operand_in;
    OutputPort<int> result_out;
};

/**
 * Pure functions (business logic)
 */
namespace ServiceGLogic
{
    void add(StoreG& store, int value)
    {
        store.accumulator += value;
    }

    void multiply(StoreG& store, int factor)
    {
        store.accumulator *= factor;
        store.multiply_factor = factor;
    }

    int getResult(const StoreG& store)
    {
        return store.accumulator;
    }
}

// Forward declaration
class ServiceG;

/**
 * Reaction: Add operand from input port
 */
struct AddReactionG : public Reaction<
    ServiceG,
    0,
    TypeList<HeartbeatInput>,
    TypeList<>,
    TypeList<>
>
{
    void execute(StoreG& store, PortsG& ports,
                 const MicroServiceContainer<>& container,
                 HeartbeatInput& input)
    {
        if (ports.operand_in.is_present()) {
            int value = ports.operand_in.get();
            ServiceGLogic::add(store, value);

            // Output result
            ports.result_out.set(ServiceGLogic::getResult(store));
        }

        input.setResult(EmptyResult{});
    }
};

/**
 * Reaction: Multiply accumulator
 */
struct MultiplyReactionG : public Reaction<
    ServiceG,
    1,
    TypeList<IncrementInput>,
    TypeList<>,
    TypeList<>
>
{
    void execute(StoreG& store, PortsG& ports,
                 const MicroServiceContainer<>& container,
                 IncrementInput& input)
    {
        ServiceGLogic::multiply(store, 2);

        // Output result
        ports.result_out.set(ServiceGLogic::getResult(store));

        input.setResult(BooleanResult{true});
    }
};

using ReactionsG = ReactionSet<AddReactionG, MultiplyReactionG>;

/**
 * ServiceG - Reaction-based reactor with ports
 *
 * Uses ReactorWithPorts template with ReactionSet.
 * Demonstrates pure functions + reactions + ports.
 */
class ServiceG : public ReactorWithPorts<NameServiceG, StoreG, PortsG,
                                         MicroServiceContainer<>, ReactionsG>
{
public:
    using Base = ReactorWithPorts<NameServiceG, StoreG, PortsG,
                                  MicroServiceContainer<>, ReactionsG>;
    using Base::Base;

    void executeHeartbeat(const LogicalTag& tag)
    {
        HeartbeatInput input;
        AddReactionG reaction;
        reaction.execute(mStore, mPorts, mContainer, input);
    }

    void executeMultiply()
    {
        IncrementInput input;
        MultiplyReactionG reaction;
        reaction.execute(mStore, mPorts, mContainer, input);
    }

    void clearPorts()
    {
        mPorts.operand_in.clear();
    }
};

} // namespace services
