# Getting Started with mscpp FSM-Driven Reactors

This guide teaches you how to build deterministic, testable reactors using mscpp's FSM-driven architecture. We'll progress from simple to complex examples, emphasizing the three-layer pattern: **Store → FSM States → Reactor**.

## Table of Contents

1. [Core Concepts](#core-concepts)
2. [Your First FSM Reactor](#your-first-fsm-reactor)
3. [Adding Business Logic to Store](#adding-business-logic-to-store)
4. [Multi-State FSM Example](#multi-state-fsm-example)
5. [Understanding StepTrigger](#understanding-steptrigger)
6. [Logical Actions for Event-Driven Patterns](#logical-actions-for-event-driven-patterns)
7. [Port Communication Between Reactors](#port-communication-between-reactors)
8. [Using I/O Adapters for Async Frameworks](#using-io-adapters-for-async-frameworks)
9. [Testing Your Reactor](#testing-your-reactor)
10. [Common Patterns and Best Practices](#common-patterns-and-best-practices)

---

## Core Concepts

### The Three-Layer Architecture

mscpp enforces a strict three-layer architecture for maximum testability and determinism:

```
┌─────────────────────────────────────┐
│  Store (Pure Business Logic)        │  ← 100% unit testable
│  • Pure functions only               │
│  • No port/reactor dependencies      │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│  FSM States (Coordination Logic)    │  ← Integration testable
│  • Reads ports → Calls Store → Sets │
│  • State transitions                 │
│  • NO business logic                 │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│  Reactor (Thin Shell)                │  ← Framework-provided
│  • FINAL methods (can't override)    │
│  • Private Store/Ports/Container     │
│  • Deterministic execution           │
└─────────────────────────────────────┘
```

### Key Terms

- **Store**: Holds all mutable state and business logic as pure functions
- **Ports**: Input/Output communication channels (typed, present/absent semantics)
- **FSM States**: Finite State Machine states that coordinate Store + Ports
- **StepTrigger**: Context about why a state's `step()` function was called
- **Logical Tag**: `(time, microstep)` pair for deterministic event ordering
- **Logical Action**: Event-driven reaction within same logical time (zero-delay)

---

## Your First FSM Reactor

Let's create a simple counter reactor that demonstrates the FSM-driven pattern.

### Step 1: Define the Store (Business Logic)

**All business logic lives in the Store as pure functions:**

```cpp
#include "mscpp/MicroServiceReactors.h"
#include "mscpp/Ports.h"
#include "mscpp/StateSet.h"
#include "mscpp/StepTrigger.h"

using namespace services;

// ═══════════════════════════════════════════════════════════════
// STORE: Pure business logic (100% unit testable!)
// ═══════════════════════════════════════════════════════════════

struct CounterStore {
    int count{0};

    // Pure function: takes input, returns output (no side effects)
    int increment(int amount) {
        count += amount;
        return count;
    }

    // Pure function: check threshold
    bool isAboveThreshold(int threshold) const {
        return count >= threshold;
    }

    void reset() {
        count = 0;
    }
};
```

**Key Points:**
- Store contains ALL business logic
- Functions should be pure (or minimal mutation)
- No dependencies on ports, reactor, or I/O
- Easy to unit test: `CounterStore s; REQUIRE(s.increment(5) == 5);`

### Step 2: Define the Ports (I/O Interface)

```cpp
// ═══════════════════════════════════════════════════════════════
// PORTS: Input/Output communication channels
// ═══════════════════════════════════════════════════════════════

struct CounterPorts {
    InputPort<int> increment_in;
    InputPort<int> threshold_in;
    OutputPort<int> count_out;
    OutputPort<bool> alert_out;
};
```

### Step 3: Define the FSM State (Coordination Logic)

**FSM states coordinate Store + Ports - NO business logic here!**

```cpp
// ═══════════════════════════════════════════════════════════════
// FSM STATE: Coordination logic (NO business logic!)
// ═══════════════════════════════════════════════════════════════

struct RunningState : public State<RunningState, 0> {
    size_t step(CounterStore& store,
                CounterPorts& ports,
                const MicroServiceContainer<>& container,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        // Pattern: Check trigger → Read ports → Call Store → Write ports

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
            if (trigger.action_name == "on_increment") {
                // 1. Read input port
                if (ports.increment_in.is_present()) {
                    int amount = ports.increment_in.get();

                    // 2. Call Store business logic (pure function!)
                    int new_count = store.increment(amount);

                    // 3. Write output port
                    ports.count_out.set(new_count);

                    // 4. Check threshold using Store function
                    if (ports.threshold_in.is_present()) {
                        int threshold = ports.threshold_in.get();
                        if (store.isAboveThreshold(threshold)) {
                            ports.alert_out.set(true);
                        }
                    }
                }
            }
        }

        // Stay in same state (no transition)
        return RunningState::index();
    }
};

// Create StateSet with all FSM states
using CounterStates = StateSet<RunningState>;
```

**Key Pattern:**
1. Check `trigger.type` and `trigger.action_name` to determine context
2. Read from input ports (`is_present()`, `get()`)
3. Call Store pure functions (business logic)
4. Write to output ports (`set()`)
5. Return next state index

### Step 4: Define the Reactor (Thin Wrapper)

**The reactor is just a type alias - no code needed!**

```cpp
// ═══════════════════════════════════════════════════════════════
// REACTOR: Thin wrapper (no overrides needed!)
// ═══════════════════════════════════════════════════════════════

class CounterReactor : public MicroServiceFSMReactor<
    CounterStore,
    CounterPorts,
    MicroServiceContainer<>,
    CounterStates
> {
public:
    using Base = MicroServiceFSMReactor<CounterStore, CounterPorts,
                                        MicroServiceContainer<>, CounterStates>;

    CounterReactor(const std::string& name,
                   const std::map<std::string, std::string>& port_action_map)
        : Base(name, MicroServiceContainer<>{}, port_action_map) {}
};
```

**Important:**
- Reactor inherits from `MicroServiceFSMReactor`
- `doHeartbeat()` and `executeLogicalAction()` are **final** (can't override!)
- `mStore`, `mPorts`, `mContainer` are **private** (only FSM states can access)
- All logic is in Store (business) and FSM States (coordination)

### Step 5: Use Your Reactor

```cpp
int main() {
    // Create reactor with port→action mapping
    std::map<std::string, std::string> port_actions = {
        {"increment_in", "on_increment"}
    };

    CounterReactor reactor("counter", port_actions);

    // Set input ports
    reactor.getPorts().increment_in.set(5);
    reactor.getPorts().threshold_in.set(10);

    // Trigger logical action
    LogicalTag tag{LogicalTime{1000000000}};  // 1 second
    reactor.executeLogicalAction(tag, "on_increment");

    // Check output
    REQUIRE(reactor.getStore().count == 5);

    return 0;
}
```

---

## Adding Business Logic to Store

### Pattern: Keep Store Functions Pure

**BAD - Business logic in FSM state:**

```cpp
// ❌ DON'T DO THIS
struct BadState : public State<BadState, 0> {
    size_t step(...) {
        if (ports.request_in.is_present()) {
            auto request = ports.request_in.get();

            // Business logic HERE - can't unit test!
            if (request.priority > 5 && store.queue.size() < 100) {
                store.queue.push(request);
                ports.success_out.set(true);
            } else {
                ports.error_out.set("Queue full or low priority");
            }
        }
        return BadState::index();
    }
};
```

**GOOD - Business logic in Store:**

```cpp
// ✅ DO THIS
struct GoodStore {
    std::vector<Request> queue;

    struct EnqueueResult {
        bool success;
        std::string error_message;
    };

    // Pure business logic (unit testable!)
    EnqueueResult enqueue(const Request& request, size_t max_queue_size) {
        if (request.priority <= 5) {
            return {false, "Priority too low"};
        }
        if (queue.size() >= max_queue_size) {
            return {false, "Queue full"};
        }
        queue.push_back(request);
        return {true, ""};
    }
};

struct GoodState : public State<GoodState, 0> {
    size_t step(...) {
        if (ports.request_in.is_present()) {
            auto request = ports.request_in.get();

            // Delegate to Store (business logic!)
            auto result = store.enqueue(request, 100);

            // Just coordination here
            if (result.success) {
                ports.success_out.set(true);
            } else {
                ports.error_out.set(result.error_message);
            }
        }
        return GoodState::index();
    }
};
```

**Benefits:**
- ✅ Unit test `enqueue()` without reactor infrastructure
- ✅ Business logic is explicit and reusable
- ✅ FSM state is simple coordination code

---

## Multi-State FSM Example

Let's create a protocol handler with multiple states:

```cpp
// Store with state-specific data
struct ProtocolStore {
    std::string session_id;
    int messages_received{0};
    bool authenticated{false};

    // Business logic
    bool validateCredentials(const std::string& username,
                           const std::string& password) const {
        return username == "admin" && password == "secret";
    }

    void startSession(const std::string& id) {
        session_id = id;
        authenticated = false;
        messages_received = 0;
    }

    void processMessage(const std::string& msg) {
        messages_received++;
    }
};

struct ProtocolPorts {
    InputPort<std::string> command_in;
    OutputPort<std::string> response_out;
};

// State 0: Disconnected
struct DisconnectedState : public State<DisconnectedState, 0> {
    size_t step(ProtocolStore& s, ProtocolPorts& p,
                const MicroServiceContainer<>& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_command") {
            if (p.command_in.is_present()) {
                auto cmd = p.command_in.get();

                if (cmd.substr(0, 7) == "CONNECT") {
                    s.startSession("session_" + std::to_string(tag.time.count()));
                    p.response_out.set("200 Connected. Please authenticate.");
                    return ConnectedState::index();  // Transition to state 1
                } else {
                    p.response_out.set("400 Not connected");
                }
            }
        }

        return DisconnectedState::index();  // Stay in state 0
    }
};

// State 1: Connected (awaiting authentication)
struct ConnectedState : public State<ConnectedState, 1> {
    size_t step(ProtocolStore& s, ProtocolPorts& p,
                const MicroServiceContainer<>& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_command") {
            if (p.command_in.is_present()) {
                auto cmd = p.command_in.get();

                if (cmd.substr(0, 4) == "AUTH") {
                    // Parse credentials (simplified)
                    std::string username = "admin", password = "secret";

                    if (s.validateCredentials(username, password)) {
                        s.authenticated = true;
                        p.response_out.set("200 Authenticated");
                        return AuthenticatedState::index();  // Transition to state 2
                    } else {
                        p.response_out.set("401 Authentication failed");
                    }
                } else if (cmd == "DISCONNECT") {
                    p.response_out.set("200 Disconnected");
                    return DisconnectedState::index();  // Back to state 0
                } else {
                    p.response_out.set("401 Not authenticated");
                }
            }
        }

        return ConnectedState::index();
    }
};

// State 2: Authenticated (can process messages)
struct AuthenticatedState : public State<AuthenticatedState, 2> {
    size_t step(ProtocolStore& s, ProtocolPorts& p,
                const MicroServiceContainer<>& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_command") {
            if (p.command_in.is_present()) {
                auto cmd = p.command_in.get();

                if (cmd == "DISCONNECT") {
                    p.response_out.set("200 Disconnected");
                    return DisconnectedState::index();
                } else if (cmd.substr(0, 4) == "DATA") {
                    s.processMessage(cmd);
                    p.response_out.set("200 Message received (" +
                                      std::to_string(s.messages_received) + " total)");
                } else {
                    p.response_out.set("400 Unknown command");
                }
            }
        }

        return AuthenticatedState::index();
    }
};

using ProtocolStates = StateSet<DisconnectedState, ConnectedState, AuthenticatedState>;

class ProtocolReactor : public MicroServiceFSMReactor<
    ProtocolStore, ProtocolPorts, MicroServiceContainer<>, ProtocolStates
> {
public:
    using Base = MicroServiceFSMReactor<ProtocolStore, ProtocolPorts,
                                        MicroServiceContainer<>, ProtocolStates>;

    ProtocolReactor(const std::string& name)
        : Base(name, MicroServiceContainer<>{}, {{"command_in", "on_command"}}) {}
};
```

**FSM State Transitions:**
```
Disconnected (0) ---CONNECT---> Connected (1) ---AUTH---> Authenticated (2)
       ↑                             |                          |
       |                             |                          |
       +-------------DISCONNECT------+----------DISCONNECT------+
```

---

## Understanding StepTrigger

The `StepTrigger` parameter tells FSM states **why** they were invoked:

```cpp
struct StepTrigger {
    enum class Type {
        HEARTBEAT,        // Regular periodic tick
        LOGICAL_ACTION,   // Event-driven action (zero-delay)
        PHYSICAL_ACTION   // Future: scheduled action
    };

    Type type;
    std::string action_name;  // Set for LOGICAL_ACTION
};
```

### Usage Patterns

```cpp
struct MyState : public State<MyState, 0> {
    size_t step(..., const StepTrigger& trigger) override {

        // Handle heartbeat (periodic work)
        if (trigger.type == StepTrigger::Type::HEARTBEAT) {
            // Periodic maintenance, timeouts, etc.
            store.checkTimeouts(tag.time);
        }

        // Handle specific logical action
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
            if (trigger.action_name == "process_request") {
                // Handle request event
            }
            else if (trigger.action_name == "handle_timeout") {
                // Handle timeout event
            }
        }

        return MyState::index();
    }
};
```

---

## Logical Actions for Event-Driven Patterns

### Why Logical Actions?

**Without logical actions** (heartbeat-only), request-response takes multiple heartbeat periods:

```
Time 0ms:   ServiceA sends request
Time 100ms: ServiceB receives request (heartbeat delay)
Time 200ms: ServiceB sends response (heartbeat delay)
Time 300ms: ServiceA receives response (heartbeat delay)
Total: 300ms latency!
```

**With logical actions** (event-driven), everything happens at same logical time:

```
Tag (0ms, 0): ServiceA sends request
Tag (0ms, 1): ServiceB receives request (next microstep)
Tag (0ms, 2): ServiceB processes and sends response (same logical time!)
Tag (0ms, 3): ServiceA receives response
Total: 0ms logical latency!
```

### Example: Request-Response Pattern

```cpp
// Service that makes requests
class ClientStore {
public:
    int pending_requests{0};
    std::vector<std::string> responses;

    void sendRequest() { pending_requests++; }
    void receiveResponse(const std::string& resp) {
        responses.push_back(resp);
        pending_requests--;
    }
};

struct ClientPorts {
    OutputPort<std::string> request_out;
    InputPort<std::string> response_in;
};

struct ClientState : public State<ClientState, 0> {
    size_t step(ClientStore& s, ClientPorts& p,
                const MicroServiceContainer<>& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        // Heartbeat: send periodic request
        if (trigger.type == StepTrigger::Type::HEARTBEAT) {
            s.sendRequest();
            p.request_out.set("REQUEST_" + std::to_string(tag.time.count()));
        }

        // Logical action: handle response immediately
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_response") {
            if (p.response_in.is_present()) {
                s.receiveResponse(p.response_in.get());
            }
        }

        return ClientState::index();
    }
};

// Service that handles requests
struct ServerStore {
    int requests_handled{0};

    std::string processRequest(const std::string& req) {
        requests_handled++;
        return "RESPONSE_TO_" + req;
    }
};

struct ServerPorts {
    InputPort<std::string> request_in;
    OutputPort<std::string> response_out;
};

struct ServerState : public State<ServerState, 0> {
    size_t step(ServerStore& s, ServerPorts& p,
                const MicroServiceContainer<>& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        // Logical action: handle request immediately
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_request") {
            if (p.request_in.is_present()) {
                auto response = s.processRequest(p.request_in.get());
                p.response_out.set(response);
            }
        }

        return ServerState::index();
    }
};
```

### Port Action Mapping

Connect ports to logical actions using the port action map:

```cpp
std::map<std::string, std::string> client_actions = {
    {"response_in", "on_response"}  // When response_in gets data, trigger "on_response"
};

std::map<std::string, std::string> server_actions = {
    {"request_in", "on_request"}  // When request_in gets data, trigger "on_request"
};

ClientReactor client("client", client_actions);
ServerReactor server("server", server_actions);
```

---

## Port Communication Between Reactors

Use `ConnectionManager` to wire reactors together:

```cpp
#include "mscpp/ReactorScheduler.h"
#include "mscpp/Topology.h"

int main() {
    ReactorScheduler scheduler;

    // Create reactors
    auto client = std::make_shared<ClientReactor>("client", client_actions);
    auto server = std::make_shared<ServerReactor>("server", server_actions);

    // Register with scheduler
    client->setScheduler(&scheduler);
    server->setScheduler(&scheduler);
    scheduler.registerReactor(client);
    scheduler.registerReactor(server);

    // Wire ports together
    ConnectionManager manager(&scheduler);

    // Client request → Server request
    manager.connect(
        client->getPorts().request_out,
        server->getPorts().request_in,
        client->getId(),
        server->getId(),
        "client_to_server"
    );

    // Server response → Client response
    manager.connect(
        server->getPorts().response_out,
        client->getPorts().response_in,
        server->getId(),
        client->getId(),
        "server_to_client"
    );

    // Run scheduler
    scheduler.run();

    return 0;
}
```

---

## Using I/O Adapters for Async Frameworks

For async I/O frameworks (gRPC, ROS2), use **I/O Adapters** to maintain determinism:

```cpp
#include "mscpp/IOAdapters/GrpcAdapter.h"

// Your reactor (deterministic)
class MyServiceReactor : public MicroServiceFSMReactor<...> {
    // FSM-driven implementation
};

int main() {
    // Create reactor
    auto reactor = std::make_shared<MyServiceReactor>("service", port_actions);

    // Create gRPC adapter (runs in separate thread)
    GrpcAdapter<MyServiceReactor> grpc_adapter(reactor, "0.0.0.0:50051");
    grpc_adapter.start();  // Start gRPC server thread

    // Run reactor scheduler (deterministic, single-threaded)
    ReactorScheduler scheduler;
    reactor->setScheduler(&scheduler);
    scheduler.registerReactor(reactor);
    scheduler.run();

    // Cleanup
    grpc_adapter.stop();

    return 0;
}
```

**Architecture:**
```
gRPC Thread (async)         Reactor Thread (deterministic)
     │                               │
     │  scheduleLogicalAction()      │
     ├──────────────────────────────>│
     │                               │ FSM processes
     │  waitForPortData()            │
     │<──────────────────────────────┤
     │                               │
```

See [IO_ADAPTERS.md](./IO_ADAPTERS.md) for detailed guide.

---

## Testing Your Reactor

### Unit Test: Store Functions

```cpp
#include <catch2/catch.hpp>

TEST_CASE("CounterStore: increment pure function", "[Store]") {
    CounterStore store;

    SECTION("Increment by positive amount") {
        int result = store.increment(5);
        REQUIRE(result == 5);
        REQUIRE(store.count == 5);
    }

    SECTION("Multiple increments") {
        store.increment(3);
        store.increment(7);
        REQUIRE(store.count == 10);
    }
}

TEST_CASE("CounterStore: threshold check", "[Store]") {
    CounterStore store;
    store.count = 50;

    REQUIRE_FALSE(store.isAboveThreshold(100));
    REQUIRE(store.isAboveThreshold(25));
}
```

### Integration Test: Reactor Behavior

```cpp
TEST_CASE("CounterReactor: logical action integration", "[Reactor]") {
    std::map<std::string, std::string> actions = {{"increment_in", "on_increment"}};
    CounterReactor reactor("test", actions);

    LogicalTag tag{LogicalTime{1000000000}};

    SECTION("Increment via logical action") {
        reactor.getPorts().increment_in.set(10);
        reactor.executeLogicalAction(tag, "on_increment");

        REQUIRE(reactor.getStore().count == 10);
    }

    SECTION("Threshold alert") {
        reactor.getPorts().increment_in.set(50);
        reactor.getPorts().threshold_in.set(30);
        reactor.executeLogicalAction(tag, "on_increment");

        REQUIRE(reactor.getStore().count == 50);
        REQUIRE(reactor.getPorts().alert_out.is_present());
        REQUIRE(reactor.getPorts().alert_out.get() == true);
    }
}
```

---

## Common Patterns and Best Practices

### Pattern 1: Periodic Maintenance (Non-Business Logic)

Override `doPeriodicMaintenance()` for logging, metrics, cleanup:

```cpp
class MyReactor : public MicroServiceFSMReactor<...> {
protected:
    void doPeriodicMaintenance(const LogicalTag& tag) override {
        // Logging (not business logic)
        auto time_s = tag.time.count() / 1'000'000'000;
        if (time_s % 60 == 0) {
            SPDLOG_INFO("Heartbeat: processed {} requests", getStore().request_count);
        }

        // Metrics (not business logic)
        metrics_collector.record("queue_size", getStore().queue.size());
    }
};
```

**Important:** Don't put business logic here! Business logic belongs in Store.

### Pattern 2: Conditional Port Output

Only set output ports when conditions are met:

```cpp
struct ProcessingState : public State<ProcessingState, 0> {
    size_t step(...) {
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "process") {
            if (ports.input_in.is_present()) {
                auto result = store.process(ports.input_in.get());

                // Conditional output
                if (result.success) {
                    ports.output_out.set(result.data);
                } else {
                    ports.error_out.set(result.error_message);
                }
            }
        }
        return ProcessingState::index();
    }
};
```

### Pattern 3: State Transition Guards

Use Store functions to determine transitions:

```cpp
struct ActiveState : public State<ActiveState, 1> {
    size_t step(...) {
        if (trigger.type == StepTrigger::Type::HEARTBEAT) {
            // Check if should transition to idle
            if (store.shouldEnterIdleMode(tag.time)) {
                ports.status_out.set("Entering idle mode");
                return IdleState::index();  // Transition
            }
        }
        return ActiveState::index();  // Stay
    }
};
```

### Pattern 4: Port Convenience Methods

Use helper methods for cleaner code:

```cpp
// Check and transform in one line
ports.input_in.if_present([&](int value) {
    int result = store.process(value);
    ports.output_out.set(result);
});

// Transform with optional
auto result = ports.input_in.transform([](int x) { return x * 2; });
if (result) {
    ports.output_out.set(*result);
}
```

---

## Best Practices Summary

✅ **DO:**
- Put ALL business logic in Store pure functions
- Use FSM states for coordination only (read ports → call Store → write ports)
- Use StepTrigger to determine context (heartbeat vs logical action)
- Use logical actions for event-driven patterns (request-response)
- Unit test Store functions independently
- Integration test reactor behavior
- Use I/O adapters for async frameworks (gRPC, ROS2)

❌ **DON'T:**
- Put business logic in FSM states (defeats testability!)
- Override `doHeartbeat()` or `executeLogicalAction()` (they're final!)
- Access `mStore`, `mPorts`, `mContainer` directly from reactor (they're private!)
- Mix async I/O code into reactor (use I/O adapters!)
- Create infinite microstep loops (scheduler will throw after 1000)

---

## Next Steps

- Read [REACTOR_PATTERNS.md](./REACTOR_PATTERNS.md) for advanced architectural patterns
- Read [IO_ADAPTERS.md](./IO_ADAPTERS.md) for integrating gRPC, ROS2, and other async frameworks
- See [examples/](./examples/) for complete working examples
- Read [MIGRATION_GUIDE.md](./MIGRATION_GUIDE.md) if migrating existing code

---

**Congratulations!** You now understand mscpp's FSM-driven reactor architecture. Remember the key principle: **Store (business logic) → FSM States (coordination) → Reactor (enforcement)**.
