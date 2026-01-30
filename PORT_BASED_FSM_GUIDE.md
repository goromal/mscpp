# Port-Based I/O with FSM and Reactions

## Overview

Phase 4 introduces port-based I/O that works seamlessly with **both** the FSM model and the reaction-based model. You don't lose any functionality - in fact, you gain more expressiveness and type safety.

## Two Complementary Approaches

### 1. **FSM Model with Ports** (ServiceF example)

**Best for:** Services with clear state transitions and state-dependent behavior

**Key Features:**
- Multiple states (Init, Running, Stopped, etc.)
- Each state can handle multiple input types differently
- State transitions based on inputs
- Ports accessible in all state step() functions

**Example:**

```cpp
struct RunningState : public State<RunningState, 1>
{
    // Handle heartbeat in Running state
    size_t step(Store& store, Ports& ports, const Container& c, HeartbeatInput& input)
    {
        // Check input port
        if (ports.value_in.is_present()) {
            int value = ports.value_in.get();
            store.counter += value;

            // Output on port
            ports.counter_out.set(store.counter);
        }

        return index();  // Stay in Running
    }

    // Handle increment in Running state
    size_t step(Store& store, Ports& ports, const Container& c, IncrementInput& input)
    {
        store.counter++;
        ports.counter_out.set(store.counter);
        return index();
    }

    // Transition to another state
    size_t step(Store& store, Ports& ports, const Container& c, TransitionInput& input)
    {
        size_t target = input.state();
        ports.state_out.set("transitioning");
        return target;  // Move to target state
    }
};
```

**Service Definition (No Boilerplate!):**

```cpp
class ServiceF : public ReactorWithPortsAndFSM<NameServiceF, StoreFSM, PortsFSM,
                                                Container, StateSet<...>>
{
public:
    using Base = ReactorWithPortsAndFSM<...>;
    using Base::Base;  // Inherit constructors - that's it!

    // Optional: convenience methods
    void executeHeartbeat(const LogicalTag& tag) {
        HeartbeatInput input;
        executeInput(input);  // Dispatches to current state's step()
    }
};
```

**Benefits:**
- ✅ Multiple reactions per service (one per input type per state)
- ✅ State-dependent behavior
- ✅ No boilerplate (template base class handles it)
- ✅ Pure functions for business logic
- ✅ Ports for type-safe communication

### 2. **Reaction Model with Ports** (ServiceG example)

**Best for:** Services with independent reactions and explicit dependencies

**Key Features:**
- Pure functions for business logic
- Explicit reactions as types
- Compile-time dependency declarations
- Stateless reactions (use store for state)

**Example:**

```cpp
// Pure functions (100% testable, no framework)
namespace ServiceGLogic {
    void add(Store& store, int value) {
        store.accumulator += value;
    }

    void multiply(Store& store, int factor) {
        store.accumulator *= factor;
    }
}

// Reaction wrapper (minimal framework integration)
struct AddReaction : public Reaction<
    ServiceG, 0,
    TypeList<HeartbeatInput>,  // Triggered by
    TypeList<>,                 // Produces
    TypeList<>                  // Depends on
>
{
    void execute(Store& store, Ports& ports, const Container& c, HeartbeatInput& input)
    {
        if (ports.operand_in.is_present()) {
            int value = ports.operand_in.get();
            ServiceGLogic::add(store, value);  // Call pure function
            ports.result_out.set(store.accumulator);
        }
    }
};
```

**Service Definition (Also No Boilerplate!):**

```cpp
class ServiceG : public ReactorWithPorts<NameServiceG, StoreG, PortsG,
                                         Container, ReactionSet<...>>
{
public:
    using Base = ReactorWithPorts<...>;
    using Base::Base;
};
```

## Template Base Classes Eliminate Boilerplate

### Before (Manual Implementation):

```cpp
class ServiceC {
public:
    using Store = StoreC;
    using Ports = PortsC;

    static constexpr const char* name() { return "ServiceC"; }

    ServiceC() : mStore{}, mPorts{} {}

    Store& getStore() { return mStore; }
    const Store& getStore() const { return mStore; }

    Ports& getPorts() { return mPorts; }
    const Ports& getPorts() const { return mPorts; }

    void clearPorts() { mPorts.input.clear(); }

private:
    Store mStore;
    Ports mPorts;
};
```

**~20 lines of boilerplate per service!**

### After (Template Base Class):

```cpp
DECLARE_REACTOR_NAME(ServiceC);

class ServiceC : public ReactorWithPorts<NameServiceC, StoreC, PortsC, Container>
{
public:
    using Base = ReactorWithPorts<...>;
    using Base::Base;
};
```

**~4 lines total!**

All accessor methods inherited from base class:
- `getStore()` / `getStore() const`
- `getPorts()` / `getPorts() const`
- `getContainer() const`
- `name()`
- `clearInputPorts()`

## Choosing Between FSM and Reaction Models

| Aspect | FSM Model | Reaction Model |
|--------|-----------|----------------|
| **State Transitions** | Explicit states, clear transitions | Implicit in store, no FSM machinery |
| **Multiple Reactions** | One step() per input per state | One Reaction type per trigger |
| **Dependencies** | Implicit (state order) | Explicit (TypeList) |
| **Testability** | Test states separately | Test pure functions separately |
| **Determinism** | Depends on state order | Topological dependency order |
| **Best For** | Protocol implementations, modes | Data pipelines, transformations |

## Can You Mix Both? YES!

You can have different services use different models in the same system:

```cpp
// FSM-based authentication service
class AuthService : public ReactorWithPortsAndFSM<
    NameAuthService, AuthStore, AuthPorts, Container, AuthStates
> { /* ... */ };

// Reaction-based data processing
class DataProcessor : public ReactorWithPorts<
    NameDataProcessor, DataStore, DataPorts, Container, DataReactions
> { /* ... */ };

// Connect them via ports
ConnectionManager manager(&scheduler);
manager.connect(
    auth_service.getPorts().status_out,
    data_processor.getPorts().auth_status_in,
    0, 1, "auth_to_processor"
);
```

## FSM State Signature with Ports

When using `ReactorWithPortsAndFSM`, your state step() functions have this signature:

```cpp
struct MyState : public State<MyState, 0> {
    size_t step(Store& store,        // Reactor state
                Ports& ports,        // Port collection
                const Container& c,  // Dependency injection
                InputType& input)    // Current input
    {
        // Access store
        store.value++;

        // Read from input ports
        if (ports.data_in.is_present()) {
            int data = ports.data_in.get();
        }

        // Write to output ports
        ports.result_out.set(42);

        // Return next state
        return NextState::index;  // Or index() to stay
    }
};
```

## Reaction Signature with Ports

When using `ReactorWithPorts` with reactions, your reaction execute() function has this signature:

```cpp
struct MyReaction : public Reaction<...> {
    void execute(Store& store,        // Reactor state
                 Ports& ports,        // Port collection
                 const Container& c,  // Dependency injection
                 InputType& input)    // Current input
    {
        // Same pattern as FSM
        if (ports.data_in.is_present()) {
            int data = ports.data_in.get();
            MyLogic::process(store, data);  // Pure function
            ports.result_out.set(MyLogic::getResult(store));
        }
    }
};
```

**Notice:** Both signatures are identical except return type!
- FSM: Returns `size_t` (next state index)
- Reaction: Returns `void` (no state transitions)

## Port Clearing

Both base classes provide `clearInputPorts()` method:

```cpp
void executeTag(const LogicalTag& tag) {
    // Process inputs
    executeHeartbeat(tag);

    // Clear ports at end of tag
    clearInputPorts();
}
```

For custom clearing logic, override in derived class:

```cpp
class MyReactor : public ReactorWithPorts<...> {
public:
    void clearInputPorts() {
        mPorts.input1.clear();
        mPorts.input2.clear();
        // Base class version would be:
        // Base::clearInputPorts();
    }
};
```

## Complete FSM Example

See [ServiceWithPortsFSM.h](tests/example-services/ServiceWithPortsFSM.h) for:
- **ServiceF**: 3-state FSM (Init, Running, Stopped) with multiple reactions per state
- **ServiceG**: Reaction-based with pure functions

Both use:
- `ReactorWithPorts` or `ReactorWithPortsAndFSM` base class
- Input/Output ports for communication
- Zero boilerplate service definitions

## Summary

**You DON'T lose the FSM model** - it's enhanced:

### FSM Model Now Has:
✅ Multiple step() functions per state (one per input type)
✅ Port-based I/O instead of ad-hoc sendInput()
✅ Template base class eliminates boilerplate
✅ Pure functions for business logic
✅ Type-safe connections

### Reaction Model Adds:
✅ Explicit dependency declarations
✅ Compile-time topological ordering
✅ Same port-based I/O
✅ Same template base class benefits
✅ Choice of stateless reactions or FSM states

**Best of both worlds!**
