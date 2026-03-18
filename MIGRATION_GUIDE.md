# Migration Guide: Old mscpp → FSM-Driven mscpp

This guide helps you migrate existing mscpp reactors to the new FSM-driven architecture with enforced patterns.

## Table of Contents

1. [Overview of Changes](#overview-of-changes)
2. [Breaking Changes](#breaking-changes)
3. [Migration Steps](#migration-steps)
4. [Common Patterns](#common-patterns)
5. [Troubleshooting](#troubleshooting)

---

## Overview of Changes

### What Changed?

The new mscpp enforces FSM-driven patterns through compile-time and runtime mechanisms:

| Aspect | Old Pattern | New Pattern |
|--------|-------------|-------------|
| **Business Logic** | Can be anywhere | MUST be in Store pure functions |
| **Reactor Methods** | Can override `doHeartbeat()`, `executeLogicalAction()` | Methods are **final** (cannot override) |
| **Store/Ports Access** | Protected (`mStore`, `mPorts`, `mContainer`) | **Private** (only FSM states can access via parameters) |
| **FSM States** | Optional | **Required** - all logic flows through FSM states |
| **StepTrigger** | Not available | Required parameter in `step()` functions |
| **I/O Integration** | Mixed with reactor | Separate I/O Adapters (thread-safe boundary) |

### Why These Changes?

**Problems with Old Pattern:**
```cpp
// ❌ OLD: Developer could bypass FSM entirely
class OldReactor : public MicroServiceFSMReactor<...> {
    void executeLogicalAction(...) override {
        // Business logic HERE - bypassing FSM!
        if (action == "process") {
            mStore.data = mPorts.input.get();
            mPorts.output.set(processData());  // Where's the business logic?
        }
    }
};
```

- ❌ No FSM constraints on what can happen when
- ❌ Business logic scattered across reactor methods
- ❌ Hard to test - business logic mixed with I/O
- ❌ Non-deterministic - no clear event ordering

**Benefits of New Pattern:**
```cpp
// ✅ NEW: FSM-driven with enforced patterns
struct Store {
    ProcessResult processData(const Input& input) const {
        // Business logic HERE - 100% testable!
        return {...};
    }
};

struct RunningState : public State<RunningState, 0> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag, const StepTrigger& trigger) {
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "process") {
            if (p.input.is_present()) {
                auto result = s.processData(p.input.get());  // Call Store!
                p.output.set(result);
            }
        }
        return RunningState::index();
    }
};
```

- ✅ FSM states enforce what can happen when
- ✅ Business logic in Store (100% unit testable)
- ✅ Clear separation of concerns
- ✅ Deterministic event ordering

---

## Breaking Changes

### 1. `doHeartbeat()` and `executeLogicalAction()` are Final

**Old Code:**
```cpp
class MyReactor : public MicroServiceFSMReactor<...> {
    void doHeartbeat(const LogicalTag& tag) override {
        // Custom heartbeat logic
        mStore.counter++;
        mPorts.status_out.set(mStore.counter);
    }

    void executeLogicalAction(const LogicalTag& tag, const std::string& action) override {
        if (action == "process") {
            mStore.data = mPorts.input.get();
            mPorts.output.set(processData());
        }
    }
};
```

**New Code:**
```cpp
// ❌ COMPILER ERROR: cannot override final methods!
class MyReactor : public MicroServiceFSMReactor<...> {
    void doHeartbeat(...) override { ... }  // ERROR: method is final
    void executeLogicalAction(...) override { ... }  // ERROR: method is final
};
```

**Solution:** Move logic to FSM states (see Migration Steps below).

### 2. `mStore`, `mPorts`, `mContainer` are Private

**Old Code:**
```cpp
class MyReactor : public MicroServiceFSMReactor<...> {
    void someMethod() {
        mStore.data = mPorts.input.get();  // Direct access
        mPorts.output.set(mStore.data);
    }
};
```

**New Code:**
```cpp
// ❌ COMPILER ERROR: cannot access private members!
class MyReactor : public MicroServiceFSMReactor<...> {
    void someMethod() {
        mStore.data = ...;  // ERROR: mStore is private
        mPorts.output...;   // ERROR: mPorts is private
    }
};
```

**Solution:** Access via `getStore()` and `getPorts()` (for testing only), or move logic to FSM states.

### 3. FSM State `step()` Signature Changed

**Old Code:**
```cpp
struct MyState : public State<MyState, 0> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag) {  // OLD: no StepTrigger
        // How do we know why we were called?
        return MyState::index();
    }
};
```

**New Code:**
```cpp
struct MyState : public State<MyState, 0> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) {  // NEW: StepTrigger parameter
        // Now we know why we were called!
        if (trigger.type == StepTrigger::Type::HEARTBEAT) {
            // Periodic work
        }
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
            // Event-driven work
        }
        return MyState::index();
    }
};
```

**Note:** Old `execute()` methods are still supported for backward compatibility, but `step()` is preferred.

### 4. Constructor Requires Port Action Map

**Old Code:**
```cpp
MyReactor reactor;  // Default constructor
```

**New Code:**
```cpp
std::map<std::string, std::string> port_actions = {
    {"input_port_name", "logical_action_name"}
};

MyReactor reactor("reactor_name", port_actions);
```

---

## Migration Steps

### Step 1: Move Business Logic to Store

**Pattern:** Identify business logic and move it to Store pure functions.

#### Before:
```cpp
class MyReactor : public MicroServiceFSMReactor<...> {
    void executeLogicalAction(const LogicalTag& tag, const std::string& action) override {
        if (action == "process_request") {
            auto request = mPorts.request_in.get();

            // ❌ Business logic in reactor method
            if (request.amount < 0) {
                mPorts.error_out.set("Invalid amount");
                return;
            }

            if (mStore.balance < request.amount) {
                mPorts.error_out.set("Insufficient funds");
                return;
            }

            mStore.balance -= request.amount;
            mStore.transaction_count++;

            mPorts.success_out.set(true);
            mPorts.new_balance_out.set(mStore.balance);
        }
    }
};
```

#### After:
```cpp
// ✅ 1. Move business logic to Store
struct MyStore {
    int balance{0};
    int transaction_count{0};

    struct WithdrawResult {
        bool success;
        std::string error_message;
        int new_balance;
    };

    // Pure function: validation + computation (no side effects)
    WithdrawResult withdraw(int amount) const {
        if (amount < 0) {
            return {false, "Invalid amount", balance};
        }
        if (balance < amount) {
            return {false, "Insufficient funds", balance};
        }
        return {true, "", balance - amount};
    }

    // Minimal mutation function
    void updateBalance(int new_balance) {
        balance = new_balance;
        transaction_count++;
    }
};

// ✅ 2. Move coordination to FSM state
struct ProcessingState : public State<ProcessingState, 0> {
    size_t step(MyStore& store, MyPorts& ports,
                const MicroServiceContainer<>& container,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "process_request") {

            if (ports.request_in.is_present()) {
                auto request = ports.request_in.get();

                // Call Store pure function
                auto result = store.withdraw(request.amount);

                if (result.success) {
                    // Update Store
                    store.updateBalance(result.new_balance);

                    // Write output ports
                    ports.success_out.set(true);
                    ports.new_balance_out.set(result.new_balance);
                } else {
                    // Write error port
                    ports.error_out.set(result.error_message);
                }
            }
        }

        return ProcessingState::index();
    }
};

// ✅ 3. Reactor is now thin wrapper
class MyReactor : public MicroServiceFSMReactor<
    MyStore, MyPorts, MicroServiceContainer<>, StateSet<ProcessingState>
> {
public:
    using Base = MicroServiceFSMReactor<MyStore, MyPorts,
                                        MicroServiceContainer<>, StateSet<ProcessingState>>;

    MyReactor(const std::string& name)
        : Base(name, MicroServiceContainer<>{},
               {{"request_in", "process_request"}}) {}
};
```

### Step 2: Update FSM State Signatures

Add `StepTrigger` parameter to all `step()` functions:

#### Before:
```cpp
struct MyState : public State<MyState, 0> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag) {  // OLD
        // Process input
        if (p.input.is_present()) {
            // ...
        }
        return MyState::index();
    }
};
```

#### After:
```cpp
struct MyState : public State<MyState, 0> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) {  // NEW

        // Use trigger to determine context
        if (trigger.type == StepTrigger::Type::HEARTBEAT) {
            // Periodic work
        }

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_input") {
            if (p.input.is_present()) {
                // Process input
            }
        }

        return MyState::index();
    }
};
```

### Step 3: Replace Heartbeat Logic with doPeriodicMaintenance()

Move non-business-logic periodic work to `doPeriodicMaintenance()`:

#### Before:
```cpp
class MyReactor : public MicroServiceFSMReactor<...> {
    void doHeartbeat(const LogicalTag& tag) override {
        Base::doHeartbeat(tag);

        // Logging
        auto time_s = tag.time.count() / 1'000'000'000;
        if (time_s % 60 == 0) {
            SPDLOG_INFO("Status: {} jobs processed", mStore.job_count);
        }

        // Metrics
        metrics_.record("queue_size", mStore.queue.size());
    }
};
```

#### After:
```cpp
class MyReactor : public MicroServiceFSMReactor<...> {
    // ✅ Use doPeriodicMaintenance() hook
protected:
    void doPeriodicMaintenance(const LogicalTag& tag) override {
        // Logging (NOT business logic)
        auto time_s = tag.time.count() / 1'000'000'000;
        if (time_s % 60 == 0) {
            SPDLOG_INFO("Status: {} jobs processed", getStore().job_count);
        }

        // Metrics (NOT business logic)
        metrics_.record("queue_size", getStore().queue.size());
    }

private:
    MetricsCollector metrics_;
};
```

### Step 4: Define Port Action Map

Create port→action mapping in constructor:

#### Before:
```cpp
MyReactor reactor;  // Default constructor
```

#### After:
```cpp
std::map<std::string, std::string> port_actions = {
    {"request_in", "on_request"},
    {"cancel_in", "on_cancel"}
};

MyReactor reactor("my_reactor", port_actions);
```

This maps:
- When `request_in` receives data → trigger logical action `"on_request"`
- When `cancel_in` receives data → trigger logical action `"on_cancel"`

### Step 5: Move Async I/O to Adapters

Extract async I/O code to dedicated I/O adapters:

#### Before:
```cpp
class MyReactor : public MicroServiceFSMReactor<...> {
    grpc::ServerCompletionQueue* cq_;  // ❌ Async I/O in reactor
    std::thread grpc_thread_;

    void setupGrpc() {
        grpc_thread_ = std::thread([this]() {
            // Async gRPC event loop
            while (running_) {
                void* tag;
                bool ok;
                cq_->Next(&tag, &ok);
                // Process RPC
            }
        });
    }
};
```

#### After:
```cpp
// ✅ Use GrpcAdapter (separate from reactor)
int main() {
    auto reactor = std::make_shared<MyReactor>("service", port_actions);

    // gRPC adapter runs in separate thread
    GrpcAdapter<MyReactor, MyServiceImpl> grpc_adapter(
        reactor, "0.0.0.0:50051"
    );
    grpc_adapter.start();

    // Reactor remains deterministic
    ReactorScheduler scheduler;
    reactor->setScheduler(&scheduler);
    scheduler.registerReactor(reactor);
    scheduler.run();

    grpc_adapter.stop();
    return 0;
}
```

---

## Common Patterns

### Pattern 1: Simple Reactor Migration

**Before:**
```cpp
struct SimpleStore { int count{0}; };
struct SimplePorts {
    InputPort<int> input;
    OutputPort<int> output;
};

class SimpleReactor : public MicroServiceReactor<SimpleStore, SimplePorts, Container> {
    void doHeartbeat(const LogicalTag& tag) override {
        if (mPorts.input.is_present()) {
            mStore.count += mPorts.input.get();
            mPorts.output.set(mStore.count);
        }
    }
};
```

**After:**
```cpp
// 1. Add business logic to Store
struct SimpleStore {
    int count{0};

    int increment(int amount) {  // Pure function
        count += amount;
        return count;
    }
};

struct SimplePorts {
    InputPort<int> input;
    OutputPort<int> output;
};

// 2. Create FSM state
struct SimpleState : public State<SimpleState, 0> {
    size_t step(SimpleStore& s, SimplePorts& p, const Container& c,
                const LogicalTag& tag, const StepTrigger& trigger) override {

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_input") {
            if (p.input.is_present()) {
                int new_count = s.increment(p.input.get());
                p.output.set(new_count);
            }
        }

        return SimpleState::index();
    }
};

// 3. Use MicroServiceFSMReactor
class SimpleReactor : public MicroServiceFSMReactor<
    SimpleStore, SimplePorts, Container, StateSet<SimpleState>
> {
public:
    using Base = MicroServiceFSMReactor<SimpleStore, SimplePorts,
                                        Container, StateSet<SimpleState>>;

    SimpleReactor(const std::string& name)
        : Base(name, Container{}, {{"input", "on_input"}}) {}
};
```

### Pattern 2: Multi-State FSM Migration

**Before:**
```cpp
class StatefulReactor : public MicroServiceFSMReactor<...> {
    enum State { IDLE, ACTIVE, PAUSED };
    State current_state{IDLE};

    void executeLogicalAction(...) override {
        if (action == "start" && current_state == IDLE) {
            current_state = ACTIVE;
        }
        else if (action == "pause" && current_state == ACTIVE) {
            current_state = PAUSED;
        }
        // ... more transitions
    }
};
```

**After:**
```cpp
// Define proper FSM states
struct IdleState : public State<IdleState, 0> {
    size_t step(..., const StepTrigger& trigger) override {
        if (trigger.action_name == "start") {
            return ActiveState::index();  // Transition
        }
        return IdleState::index();
    }
};

struct ActiveState : public State<ActiveState, 1> {
    size_t step(..., const StepTrigger& trigger) override {
        if (trigger.action_name == "pause") {
            return PausedState::index();  // Transition
        }
        // Do active work
        return ActiveState::index();
    }
};

struct PausedState : public State<PausedState, 2> {
    size_t step(..., const StepTrigger& trigger) override {
        if (trigger.action_name == "resume") {
            return ActiveState::index();  // Transition
        }
        if (trigger.action_name == "stop") {
            return IdleState::index();  // Transition
        }
        return PausedState::index();
    }
};

using StatefulStates = StateSet<IdleState, ActiveState, PausedState>;

class StatefulReactor : public MicroServiceFSMReactor<
    Store, Ports, Container, StatefulStates
> { ... };
```

---

## Troubleshooting

### Error: "cannot override final method"

**Problem:**
```cpp
class MyReactor : public MicroServiceFSMReactor<...> {
    void doHeartbeat(...) override { ... }  // ERROR
};
```

**Solution:** Remove override. Move logic to FSM states or `doPeriodicMaintenance()`.

```cpp
class MyReactor : public MicroServiceFSMReactor<...> {
protected:
    void doPeriodicMaintenance(const LogicalTag& tag) override {
        // Non-business-logic periodic work
    }
};
```

### Error: "mStore is private"

**Problem:**
```cpp
class MyReactor : public MicroServiceFSMReactor<...> {
    void someMethod() {
        mStore.data = 42;  // ERROR: mStore is private
    }
};
```

**Solution:** Use `getStore()` for testing, or move logic to FSM states.

```cpp
// For testing:
TEST_CASE("MyReactor test") {
    MyReactor reactor("test", port_actions);
    REQUIRE(reactor.getStore().data == 0);
}

// For logic: move to FSM state
struct MyState : public State<MyState, 0> {
    size_t step(Store& store, ...) {
        store.data = 42;  // FSM states have access
        return MyState::index();
    }
};
```

### Error: "step() missing StepTrigger parameter"

**Problem:**
```cpp
struct MyState : public State<MyState, 0> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag) {  // Missing StepTrigger
        return MyState::index();
    }
};
```

**Solution:** Add `StepTrigger` parameter.

```cpp
struct MyState : public State<MyState, 0> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) {  // Added
        return MyState::index();
    }
};
```

### Reactor Never Executes FSM States

**Problem:** Port action map not defined.

**Solution:** Define port action map in constructor.

```cpp
std::map<std::string, std::string> port_actions = {
    {"input_port", "on_input"}
};

MyReactor reactor("name", port_actions);
```

### Business Logic Hard to Test

**Problem:** Business logic scattered in FSM states.

**Solution:** Extract to Store pure functions.

```cpp
// ❌ BAD: Logic in FSM state
struct BadState : public State<BadState, 0> {
    size_t step(...) {
        if (ports.input.is_present()) {
            auto input = ports.input.get();
            // Complex business logic HERE - can't unit test
            if (input.value > 0 && input.value < 100) {
                store.result = input.value * 2;
            }
        }
        return BadState::index();
    }
};

// ✅ GOOD: Logic in Store
struct GoodStore {
    struct ProcessResult {
        bool valid;
        int result;
    };

    ProcessResult process(int value) const {  // Unit testable!
        if (value <= 0 || value >= 100) {
            return {false, 0};
        }
        return {true, value * 2};
    }
};

struct GoodState : public State<GoodState, 0> {
    size_t step(...) {
        if (ports.input.is_present()) {
            auto input = ports.input.get();
            auto result = store.process(input.value);  // Call Store
            if (result.valid) {
                ports.output.set(result.result);
            }
        }
        return GoodState::index();
    }
};
```

---

## Migration Checklist

Use this checklist to ensure your migration is complete:

### Store
- [ ] All business logic moved to Store pure functions
- [ ] Store functions are const where possible
- [ ] Minimal mutation functions separated from logic
- [ ] No dependencies on ports, reactor, or I/O
- [ ] All Store functions have unit tests

### FSM States
- [ ] All `step()` functions have `StepTrigger` parameter
- [ ] States contain only coordination logic (read ports → call Store → write ports)
- [ ] NO business logic in FSM states
- [ ] State transitions based on Store queries
- [ ] Use `trigger.type` and `trigger.action_name` appropriately

### Reactor
- [ ] No overrides of `doHeartbeat()` or `executeLogicalAction()`
- [ ] Port action map defined in constructor
- [ ] Optional `doPeriodicMaintenance()` for logging/metrics only
- [ ] No async I/O code in reactor (use I/O adapters)

### Testing
- [ ] Unit tests for all Store functions
- [ ] Integration tests for reactor behavior
- [ ] Tests use `getStore()` and `getPorts()` accessors
- [ ] All tests pass

### Async I/O (if applicable)
- [ ] Async I/O code moved to dedicated I/O adapters
- [ ] I/O adapters inherit from `IOAdapter<ReactorType>`
- [ ] I/O adapters use `scheduleReactorAction()` for thread-safe communication
- [ ] Reactor remains deterministic and single-threaded

---

## Summary

**Key Takeaways:**

1. **Store**: All business logic as pure functions (100% unit testable)
2. **FSM States**: Coordination only (read ports → call Store → write ports)
3. **Reactor**: Thin wrapper (no overrides needed, just constructor)
4. **StepTrigger**: Use to determine context (heartbeat vs logical action)
5. **I/O Adapters**: Separate async I/O from deterministic reactor

**Migration Process:**
1. Move business logic to Store pure functions
2. Update FSM state signatures (add `StepTrigger`)
3. Move periodic work to `doPeriodicMaintenance()`
4. Define port action map
5. Extract async I/O to adapters

**Remember:** The three-layer pattern ensures testability and determinism:
**Store (business logic) → FSM States (coordination) → Reactor (enforcement)**

---

**See Also:**
- [GETTING_STARTED.md](./GETTING_STARTED.md) - Learn the new patterns
- [REACTOR_PATTERNS.md](./REACTOR_PATTERNS.md) - FSM-driven architecture patterns
- [IO_ADAPTERS.md](./IO_ADAPTERS.md) - Integrating async I/O frameworks
