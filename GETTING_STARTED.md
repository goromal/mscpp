# Getting Started with mscpp Reactor Framework

This guide will walk you through creating your first reactors with the mscpp framework. We'll progress from simple to complex examples.

## Table of Contents

1. [Hello World: Basic Reactor](#1-hello-world-basic-reactor)
2. [Producer/Consumer: Port Communication](#2-producerconsumer-port-communication)
3. [Transform Pipeline: Chained Reactors](#3-transform-pipeline-chained-reactors)
4. [FSM Reactor: State Machine with Ports](#4-fsm-reactor-state-machine-with-ports)
5. [Using Auto-Clear Ports](#5-using-auto-clear-ports)
6. [Convenient Port APIs](#6-convenient-port-apis)
7. [Common Patterns and Best Practices](#7-common-patterns-and-best-practices)
8. [Troubleshooting](#8-troubleshooting)

---

## 1. Hello World: Basic Reactor

Let's create the simplest possible reactor that prints a message on each heartbeat.

### Step 1: Define the Reactor Components

```cpp
#include "mscpp/MicroServiceReactors.h"
#include "mscpp/Ports.h"
#include <iostream>

using namespace services;

// Step 1.1: Define the reactor's internal state
struct HelloStore
{
    int heartbeat_count = 0;
};

// Step 1.2: Define the reactor's ports (none for this simple example)
struct HelloPorts
{
    // Empty - no inputs or outputs
};

// Step 1.3: Use the DEFINE_REACTOR macro to reduce boilerplate
DEFINE_REACTOR(HelloReactor, HelloStore, HelloPorts, MicroServiceContainer<>)
{
public:
    void doHeartbeat(const LogicalTag& tag) override
    {
        mStore.heartbeat_count++;
        std::cout << "Hello from reactor! Heartbeat #"
                  << mStore.heartbeat_count
                  << " at tag " << tag << std::endl;
    }

    // No ports to clear
    void clearPorts() override {}
};
```

### Step 2: Run the Reactor

```cpp
#include "mscpp/ReactorScheduler.h"

int main()
{
    // Create scheduler
    ReactorScheduler scheduler;

    // Create and register reactor
    auto reactor = std::make_shared<HelloReactor>();
    reactor->setScheduler(&scheduler);
    scheduler.registerReactor(reactor);

    // Run for a few heartbeats (will stop when queue empties)
    scheduler.run();

    return 0;
}
```

**Key Concepts:**
- **Store**: Holds reactor's mutable state
- **Ports**: Define inputs/outputs (empty here)
- **doHeartbeat()**: Called periodically at logical time intervals
- **LogicalTag**: Represents (time, microstep) for deterministic execution

---

## 2. Producer/Consumer: Port Communication

Now let's create two reactors that communicate via ports.

### Producer Reactor

```cpp
struct ProducerStore
{
    int counter = 0;
};

struct ProducerPorts
{
    OutputPort<int> value_out;  // Produces integers
};

DEFINE_REACTOR(Producer, ProducerStore, ProducerPorts, MicroServiceContainer<>)
{
public:
    void doHeartbeat(const LogicalTag& tag) override
    {
        mStore.counter++;
        mPorts.value_out.set(mStore.counter);  // Send value
        std::cout << "Producer: sent " << mStore.counter << std::endl;
    }

    void clearPorts() override {}
};
```

### Consumer Reactor

```cpp
struct ConsumerStore
{
    int last_received = 0;
    int receive_count = 0;
};

struct ConsumerPorts
{
    InputPort<int> value_in;  // Receives integers
};

DEFINE_REACTOR(Consumer, ConsumerStore, ConsumerPorts, MicroServiceContainer<>)
{
public:
    void doHeartbeat(const LogicalTag& tag) override
    {
        if (mPorts.value_in.is_present())
        {
            mStore.last_received = mPorts.value_in.get();
            mStore.receive_count++;
            std::cout << "Consumer: received " << mStore.last_received << std::endl;
        }
    }

    void clearPorts() override
    {
        mPorts.value_in.clear();
    }
};
```

### Connecting Reactors

```cpp
#include "mscpp/Topology.h"

int main()
{
    ReactorScheduler scheduler;

    auto producer = std::make_shared<Producer>();
    auto consumer = std::make_shared<Consumer>();

    producer->setScheduler(&scheduler);
    consumer->setScheduler(&scheduler);

    scheduler.registerReactor(producer);
    scheduler.registerReactor(consumer);

    // Wire output to input
    ConnectionManager manager(&scheduler);
    manager.connect(
        producer->getPorts().value_out,
        consumer->getPorts().value_in,
        producer->getId(),
        consumer->getId(),
        "producer_to_consumer"
    );

    scheduler.run();
    return 0;
}
```

**Key Concepts:**
- **OutputPort**: Set values that become available at next microstep
- **InputPort**: Check presence with `is_present()`, get value with `get()`
- **ConnectionManager**: Wires ports together for automatic event scheduling
- **Port clearing**: Must clear input ports each tag (or use ENABLE_AUTO_CLEAR_PORTS)

---

## 3. Transform Pipeline: Chained Reactors

Create a pipeline where each reactor transforms the data.

```cpp
// Double: multiplies input by 2
struct DoublerStore { int multiplier = 2; };
struct DoublerPorts
{
    InputPort<int> value_in;
    OutputPort<int> value_out;
};

DEFINE_REACTOR(Doubler, DoublerStore, DoublerPorts, MicroServiceContainer<>)
{
public:
    void doHeartbeat(const LogicalTag& tag) override
    {
        if (mPorts.value_in.is_present())
        {
            int doubled = mPorts.value_in.get() * mStore.multiplier;
            mPorts.value_out.set(doubled);
        }
    }

    void clearPorts() override { mPorts.value_in.clear(); }
};

// Similar definitions for Incrementer, etc.

// Wire them: Producer -> Doubler -> Incrementer -> Consumer
```

---

## 4. FSM Reactor: State Machine with Ports

Let's create a simple protocol handler using a finite state machine combined with port-based I/O.

### Define Protocol States and Inputs

```cpp
#include "mscpp/MicroServiceReactors.h"
#include "mscpp/StateMachine.h"

// Protocol message types
enum class Message { CONNECT, DISCONNECT, DATA };

struct ProtocolStore {
    int messages_received = 0;
    bool authenticated = false;
};

struct ProtocolPorts {
    InputPort<Message> msg_in;
    OutputPort<std::string> response_out;
};
```

### Define FSM States

```cpp
// State 0: Disconnected
struct DisconnectedState {
    size_t step(ProtocolStore& store, ProtocolPorts& ports,
                const MicroServiceContainer<>& c, Message& msg) {
        if (msg == Message::CONNECT) {
            ports.response_out.set("Connected");
            return 1; // Transition to ConnectedState
        }
        ports.response_out.set("Error: Not connected");
        return 0; // Stay in Disconnected
    }
};

// State 1: Connected
struct ConnectedState {
    size_t step(ProtocolStore& store, ProtocolPorts& ports,
                const MicroServiceContainer<>& c, Message& msg) {
        if (msg == Message::DISCONNECT) {
            ports.response_out.set("Disconnected");
            store.authenticated = false;
            return 0; // Back to DisconnectedState
        }
        if (msg == Message::DATA) {
            store.messages_received++;
            ports.response_out.set("Data received: " +
                                   std::to_string(store.messages_received));
        }
        return 1; // Stay in Connected
    }
};

using ProtocolStates = StateSet<DisconnectedState, ConnectedState>;
```

### Create FSM Reactor

```cpp
DEFINE_FSM_REACTOR(ProtocolHandler, ProtocolStore, ProtocolPorts,
                   MicroServiceContainer<>, ProtocolStates)
{
public:
    void doHeartbeat(const LogicalTag& tag) override {
        // Process incoming messages
        mPorts.msg_in.if_present([this](Message msg) {
            EXECUTE_FSM_INPUT(msg);
        });
    }
};

// Auto-clear ports
ENABLE_AUTO_CLEAR_PORTS(ProtocolPorts, msg_in);
```

### Use the FSM Reactor

```cpp
int main() {
    ReactorScheduler scheduler;

    auto protocol = std::make_shared<ProtocolHandler>();
    protocol->setScheduler(&scheduler);
    scheduler.registerReactor(protocol);

    // Send messages to the FSM
    protocol->getPorts().msg_in.set(Message::CONNECT);

    scheduler.run();
    return 0;
}
```

**Key Points:**
- FSM reactors combine deterministic state machines with reactive port I/O
- State transitions are explicit (return state index)
- States can access ports, store, and container
- Use `DEFINE_FSM_REACTOR` macro for clean syntax
- Check current state with `getCurrentState()`

---

## 5. Using Auto-Clear Ports

Instead of manually clearing ports, use the `ENABLE_AUTO_CLEAR_PORTS` macro:

```cpp
struct MyPorts
{
    InputPort<int> counter_in;
    InputPort<std::string> message_in;
    OutputPort<int> result_out;
};

// Enable automatic clearing (list all input ports)
ENABLE_AUTO_CLEAR_PORTS(MyPorts, counter_in, message_in);

DEFINE_REACTOR(MyReactor, MyStore, MyPorts, MicroServiceContainer<>)
{
public:
    void doHeartbeat(const LogicalTag& tag) override
    {
        // Process inputs
    }

    // No need to override clearPorts() - automatic!
};
```

---

## 6. Convenient Port APIs

The framework provides several convenience methods:

### if_present(): Callback-based processing

```cpp
void doHeartbeat(const LogicalTag& tag) override
{
    mPorts.counter_in.if_present([&](int value) {
        std::cout << "Received: " << value << std::endl;
        mStore.total += value;
    });
}
```

### transform(): Functional transformation

```cpp
void doHeartbeat(const LogicalTag& tag) override
{
    auto doubled = mPorts.counter_in.transform([](int x) { return x * 2; });
    if (doubled) {
        mPorts.counter_out.set(*doubled);
    }
}
```

### map_to(): Direct port-to-port mapping

```cpp
void doHeartbeat(const LogicalTag& tag) override
{
    // Automatically transform and send if present
    mPorts.counter_in.map_to(mPorts.counter_out, [](int x) { return x * 2; });
}
```

### try_get(): Optional-like access

```cpp
void doHeartbeat(const LogicalTag& tag) override
{
    if (auto value = mPorts.counter_in.try_get())
    {
        // Use *value
        mStore.last = *value;
    }
}
```

---

## 7. Common Patterns and Best Practices

### Pattern 1: Initialization

Override `initialize()` to set up initial state:

```cpp
void initialize() override
{
    mStore.start_time = std::chrono::steady_clock::now();
    mStore.status = "initialized";
}
```

### Pattern 2: Custom Heartbeat Rate

Override `heartbeatDuration()`:

```cpp
LogicalTime heartbeatDuration() const override
{
    return LogicalTime(100'000'000);  // 100ms
}
```

### Pattern 3: State Machines with Ports

Combine FSM states with port-based I/O using the `DEFINE_FSM_REACTOR` macro:

```cpp
#include "mscpp/MicroServiceReactors.h"
#include "mscpp/StateMachine.h"

// Define command input type
enum class Command { START, STOP, RESET };

// Define state store
struct TrafficLightStore {
    int cycle_count = 0;
    LogicalTime state_start_time{0};
};

// Define ports
struct TrafficLightPorts {
    InputPort<Command> command_in;
    OutputPort<std::string> status_out;
};

// Define states using StateSet
struct IdleState {
    size_t step(TrafficLightStore& store, TrafficLightPorts& ports,
                const MicroServiceContainer<>& c, Command& cmd) {
        if (cmd == Command::START) {
            ports.status_out.set("GREEN");
            return 1; // Transition to GreenState
        }
        return 0; // Stay in IdleState
    }
};

struct GreenState {
    size_t step(TrafficLightStore& store, TrafficLightPorts& ports,
                const MicroServiceContainer<>& c, Command& cmd) {
        if (cmd == Command::STOP) {
            ports.status_out.set("IDLE");
            return 0; // Back to IdleState
        }
        // Auto-transition to yellow after timeout
        ports.status_out.set("YELLOW");
        return 2; // Transition to YellowState
    }
};

struct YellowState {
    size_t step(TrafficLightStore& store, TrafficLightPorts& ports,
                const MicroServiceContainer<>& c, Command& cmd) {
        if (cmd == Command::STOP) {
            ports.status_out.set("IDLE");
            return 0;
        }
        // Auto-transition to red
        ports.status_out.set("RED");
        store.cycle_count++;
        return 3; // Transition to RedState
    }
};

struct RedState {
    size_t step(TrafficLightStore& store, TrafficLightPorts& ports,
                const MicroServiceContainer<>& c, Command& cmd) {
        if (cmd == Command::STOP) {
            ports.status_out.set("IDLE");
            return 0;
        }
        if (cmd == Command::RESET) {
            store.cycle_count = 0;
            ports.status_out.set("GREEN");
            return 1;
        }
        // Auto-transition back to green
        ports.status_out.set("GREEN");
        return 1;
    }
};

using TrafficLightStates = StateSet<IdleState, GreenState, YellowState, RedState>;

// Define FSM reactor using the macro
DEFINE_FSM_REACTOR(TrafficLightReactor, TrafficLightStore, TrafficLightPorts,
                   MicroServiceContainer<>, TrafficLightStates)
{
public:
    void doHeartbeat(const LogicalTag& tag) override {
        // Process commands from input port
        ports.command_in.if_present([this](Command cmd) {
            EXECUTE_FSM_INPUT(cmd);
        });
    }

    void clearPorts() override {
        mPorts.command_in.clear();
    }
};

// Or use ENABLE_AUTO_CLEAR_PORTS
ENABLE_AUTO_CLEAR_PORTS(TrafficLightPorts, command_in);
```

**Key FSM Concepts:**
- States return the index of the next state (0 = stay, other = transition)
- States can read input ports and write output ports
- Use `EXECUTE_FSM_INPUT(input)` macro to dispatch inputs
- FSM state is preserved across heartbeats
- Get current state with `getCurrentState()`

### Pattern 4: Conditional Output

Only set output when condition met:

```cpp
void doHeartbeat(const LogicalTag& tag) override
{
    if (mPorts.trigger_in.is_present())
    {
        int result = compute();
        if (result > threshold)
        {
            mPorts.alert_out.set(result);
        }
    }
}
```

---

## 8. Troubleshooting

### Problem: Stale data in input ports

**Symptom**: Input port has value from previous tag

**Solution**: Make sure you're clearing ports! Use `ENABLE_AUTO_CLEAR_PORTS`:

```cpp
ENABLE_AUTO_CLEAR_PORTS(MyPorts, input1, input2, input3);
```

### Problem: Reactor not executing

**Symptom**: doHeartbeat() never called

**Solution**: Check that:
1. Reactor is registered with scheduler: `scheduler.registerReactor(reactor)`
2. Scheduler has setScheduler: `reactor->setScheduler(&scheduler)`
3. Scheduler is running: `scheduler.run()`

### Problem: Port connection not working

**Symptom**: Consumer never receives values

**Solution**: Verify connection is established:
```cpp
manager.connect(
    producer->getPorts().out,
    consumer->getPorts().in,
    producer->getId(),  // Source reactor ID
    consumer->getId(),  // Destination reactor ID
    "connection_name"
);
```

### Problem: Compile error with DEFINE_REACTOR

**Symptom**: Template errors about missing types

**Solution**: Ensure Store, Ports, and Container types are fully defined before DEFINE_REACTOR:

```cpp
// Define these FIRST
struct MyStore { ... };
struct MyPorts { ... };

// Then use macro
DEFINE_REACTOR(MyReactor, MyStore, MyPorts, MicroServiceContainer<>)
{
    // ...
};
```

### Problem: Microstep overflow

**Symptom**: `std::overflow_error: Microstep overflow`

**Cause**: More than 4.3 billion microsteps at same logical time (cascading zero-delay events)

**Solution**: Review your reaction logic for infinite loops or cascading events. Add logical time delays.

---
