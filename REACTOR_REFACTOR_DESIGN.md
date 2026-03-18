# mscpp Reactor Architecture Refactor Design

**Version:** 2.0
**Date:** 2026-03-06
**Status:** Design Document - Proposed Architecture
**Author:** Based on orchestrator-cpp implementation experience

## Executive Summary

This document proposes a comprehensive refactor of the mscpp reactor architecture to enforce **FSM-driven deterministic patterns** and provide clean integration points for **async I/O frameworks** (gRPC, ROS2, etc.). The refactor addresses architectural issues discovered during orchestrator-cpp implementation where developers could bypass the FSM structure, leading to non-deterministic behavior and poor testability.

## Table of Contents

1. [Problem Statement](#problem-statement)
2. [Design Principles](#design-principles)
3. [Proposed Architecture](#proposed-architecture)
4. [I/O Adapter Pattern](#io-adapter-pattern)
5. [Implementation Plan](#implementation-plan)
6. [Migration Guide](#migration-guide)
7. [Examples](#examples)

---

## Problem Statement

### Current Issues with mscpp Reactors

#### 1. **FSM States Are Optional**
Developers can override `executeLogicalAction()` and `doHeartbeat()` to bypass the FSM entirely:

```cpp
// BAD: Current pattern allows this
class MyReactor : public MicroServiceFSMReactor<...> {
    void executeLogicalAction(const LogicalTag& tag, const std::string& action) override {
        // Business logic here - bypassing FSM!
        if (action == "process") {
            mStore.data = mPorts.input.get();  // Direct access!
            mPorts.output.set(processData());   // No FSM state constraints!
        }
    }
};
```

**Problems:**
- ❌ FSM states never get called
- ❌ No state-based constraints on what can happen when
- ❌ Business logic scattered across reactor methods
- ❌ Not deterministic - no clear event ordering
- ❌ Not testable - business logic mixed with I/O

#### 2. **Store/Ports/Container Are Accessible Everywhere**
Protected members `mStore`, `mPorts`, `mContainer` can be accessed from any reactor method, not just FSM states:

```cpp
// BAD: Reactor methods have unrestricted access
void executeLogicalAction(...) override {
    mStore.counter++;              // Direct state mutation!
    auto data = mPorts.input.get(); // Direct port access!
}
```

**Problems:**
- ❌ No enforcement that business logic lives in Store pure functions
- ❌ FSM states don't provide meaningful constraints
- ❌ Hard to unit test - everything depends on reactor infrastructure

#### 3. **No Clear I/O Framework Integration Pattern**
Async I/O frameworks (gRPC, ROS2) are mixed directly into reactor logic:

```cpp
// BAD: Async I/O contaminating reactor
class MyReactor : public MicroServiceFSMReactor<...> {
    grpc::ServerCompletionQueue* cq_;  // Async I/O in reactor!
    std::thread grpc_thread_;          // Threading in reactor!

    void executeLogicalAction(...) {
        // Mixing async callbacks with deterministic logic
    }
};
```

**Problems:**
- ❌ Race conditions between I/O threads and reactor thread
- ❌ Non-deterministic execution order
- ❌ Hard to test - async I/O mixed with business logic
- ❌ No clean separation of concerns

---

## Design Principles

### Core Tenets

1. **FSM States Own Coordination Logic**
   - All port I/O happens in FSM state `step()` functions
   - States determine what can happen in each state
   - Clear state transitions based on events

2. **Store Contains Pure Business Logic**
   - All business logic in Store as pure functions (const methods)
   - FSM states call Store functions, never contain business logic
   - Easy to unit test Store in isolation

3. **Reactor Is Thin Wrapper**
   - Reactor only schedules FSM transitions
   - No business logic in reactor methods
   - Optional hooks for non-business-logic periodic work

4. **I/O Adapters Are External**
   - Async I/O frameworks live outside reactor (separate threads)
   - Thread-safe boundary via `scheduleLogicalAction()` and `waitForPortData()`
   - Reactor remains single-threaded and deterministic

5. **Compile-Time Enforcement**
   - Use `final` to prevent overriding critical methods
   - Use `private` to restrict access to Store/Ports/Container
   - Use concepts/static_assert to validate Store purity

---

## Proposed Architecture

### Layer 1: Store (Pure Business Logic)

```cpp
// ═══════════════════════════════════════════════════════════════
// STORE: All business logic lives here (PURE FUNCTIONS)
// ═══════════════════════════════════════════════════════════════

struct Store {
    // State
    int counter{0};
    std::map<int, Data> cache;

    // ────────────────────────────────────────────────────────────
    // PURE FUNCTIONS: Business logic (testable!)
    // ────────────────────────────────────────────────────────────

    struct ProcessResult {
        std::string output;
        bool success;
        std::string error_message;
    };

    // Pure transformation - no side effects
    ProcessResult processData(const Input& input) const {
        // Business logic here
        if (input.value < 0) {
            return {"", false, "Invalid input"};
        }
        return {std::to_string(input.value * 2), true, ""};
    }

    // Simple state updates (mutable, but minimal)
    void incrementCounter() {
        counter++;
    }

    void cacheData(int key, const Data& data) {
        cache[key] = data;
    }

    // ────────────────────────────────────────────────────────────
    // NO PORT ACCESS! NO EXTERNAL I/O! NO REACTOR LOGIC!
    // ────────────────────────────────────────────────────────────
};
```

### Layer 2: FSM States (Coordination)

```cpp
// ═══════════════════════════════════════════════════════════════
// FSM STATES: Coordinate Store + Ports (NO business logic!)
// ═══════════════════════════════════════════════════════════════

struct InitState : public State<InitState, 0> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) {

        // Initialization logic
        s.incrementCounter();

        // Transition to running
        return RunningState::index();
    }
};

struct RunningState : public State<RunningState, 1> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) {

        // ────────────────────────────────────────────────────────
        // Pattern: Check trigger → Read ports → Call Store → Write ports
        // ────────────────────────────────────────────────────────

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
            if (trigger.action_name == "on_port_input") {
                // 1. Read input port
                if (p.input.is_present()) {
                    auto data = p.input.get();

                    // 2. Delegate to Store (pure function!)
                    auto result = s.processData(data);

                    // 3. Write output port
                    if (result.success) {
                        p.output.set(result.output);
                    } else {
                        p.error_out.set(result.error_message);
                    }

                    // 4. Update state (via Store)
                    s.cacheData(data.id, data);
                }
            }
        }

        return RunningState::index();
    }
};
```

### Layer 3: Reactor (Thin Wrapper)

```cpp
// ═══════════════════════════════════════════════════════════════
// REACTOR: Thin wrapper - just FSM scheduling
// ═══════════════════════════════════════════════════════════════

template<typename Name, typename Store, typename Ports, typename Container, typename States>
class MicroServiceFSMReactor : public IReactor {
public:
    // ────────────────────────────────────────────────────────────
    // Constructor
    // ────────────────────────────────────────────────────────────

    MicroServiceFSMReactor(const Container& container)
        : mContainer(container)
        , mCurrentState(0)
    {}

    // ────────────────────────────────────────────────────────────
    // FINAL: Cannot be overridden - enforces FSM pattern
    // ────────────────────────────────────────────────────────────

    void doHeartbeat(const LogicalTag& tag) final override {
        // 1. Process pending logical actions from I/O adapters
        processPendingActions(tag);

        // 2. Run FSM step (heartbeat trigger)
        auto trigger = StepTrigger::heartbeat();
        mCurrentState = States::step(mCurrentState, mStore, mPorts, mContainer, tag, trigger);

        // 3. Clear input ports (for next cycle)
        clearInputPorts(mPorts);

        // 4. Notify I/O adapters of port updates
        notifyPortUpdates();

        // 5. Optional periodic maintenance hook
        doPeriodicMaintenance(tag);
    }

    void executeLogicalAction(const LogicalTag& tag, const std::string& action) final override {
        // Run FSM step with logical action trigger
        auto trigger = StepTrigger::logical_action(action);
        mCurrentState = States::step(mCurrentState, mStore, mPorts, mContainer, tag, trigger);

        // Clear input ports
        clearInputPorts(mPorts);

        // Notify I/O adapters
        notifyPortUpdates();
    }

    // ────────────────────────────────────────────────────────────
    // Thread-safe I/O Adapter Interface
    // ────────────────────────────────────────────────────────────

    // Called from I/O adapter threads
    void scheduleLogicalAction(const std::string& action, const PortData& data) {
        std::lock_guard<std::mutex> lock(action_mutex_);
        pending_actions_.push({action, data});
        action_cv_.notify_one();
    }

    // Called from I/O adapter threads (blocking)
    template<typename T>
    T waitForPortData(const std::string& port_name, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(port_mutex_);

        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (!hasPortData(port_name)) {
            if (port_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                throw std::runtime_error("Port timeout: " + port_name);
            }
        }

        return getPortData<T>(port_name);
    }

    // ────────────────────────────────────────────────────────────
    // Optional Hook: Non-business-logic periodic work
    // ────────────────────────────────────────────────────────────

protected:
    virtual void doPeriodicMaintenance(const LogicalTag& tag) {
        // Override for cleanup, logging, metrics, etc.
        // NOT for business logic!
    }

    // ────────────────────────────────────────────────────────────
    // PRIVATE: Store/Ports/Container only accessible to FSM states
    // ────────────────────────────────────────────────────────────

private:
    Store mStore;
    Ports mPorts;
    Container mContainer;
    size_t mCurrentState;

    // Thread synchronization for I/O adapters
    std::mutex action_mutex_;
    std::condition_variable action_cv_;
    std::queue<PendingAction> pending_actions_;

    std::mutex port_mutex_;
    std::condition_variable port_cv_;
    std::map<std::string, PortData> port_data_;

    void processPendingActions(const LogicalTag& tag) {
        std::lock_guard<std::mutex> lock(action_mutex_);

        while (!pending_actions_.empty()) {
            auto action = pending_actions_.front();
            pending_actions_.pop();

            // Set input port from I/O adapter data
            setInputPortFromData(action.port_name, action.data);

            // Process via FSM
            auto trigger = StepTrigger::logical_action(action.name);
            mCurrentState = States::step(mCurrentState, mStore, mPorts, mContainer, tag, trigger);

            // Clear input ports
            clearInputPorts(mPorts);
        }
    }

    void notifyPortUpdates() {
        std::lock_guard<std::mutex> lock(port_mutex_);
        // Extract port data for I/O adapters
        extractPortData();
        port_cv_.notify_all();
    }

    void setInputPortFromData(const std::string& port_name, const PortData& data);
    void extractPortData();
    bool hasPortData(const std::string& port_name) const;
    template<typename T> T getPortData(const std::string& port_name);
};
```

### Layer 4: StepTrigger (Event Context)

```cpp
// ═══════════════════════════════════════════════════════════════
// STEP TRIGGER: Why was FSM step invoked?
// ═══════════════════════════════════════════════════════════════

struct StepTrigger {
    enum class Type {
        HEARTBEAT,           // Regular periodic tick
        LOGICAL_ACTION,      // Event-driven logical action
        PHYSICAL_ACTION      // Scheduled physical action (future work)
    };

    Type type;
    std::string action_name;  // Only set if type == LOGICAL_ACTION

    static StepTrigger heartbeat() {
        return {Type::HEARTBEAT, ""};
    }

    static StepTrigger logical_action(const std::string& name) {
        return {Type::LOGICAL_ACTION, name};
    }

    static StepTrigger physical_action(const std::string& name) {
        return {Type::PHYSICAL_ACTION, name};
    }
};
```

---

## I/O Adapter Pattern

### Problem: Integrating Async I/O Frameworks

**Challenge:** How do we integrate async I/O frameworks (gRPC, ROS2, WebSockets, etc.) without contaminating the deterministic reactor with threading and async callbacks?

**Solution:** The **I/O Adapter Pattern** provides a clean thread-safe boundary between async I/O and the deterministic reactor.

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│          I/O Adapters (Async, Separate Threads)         │
│                                                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │ GrpcAdapter  │  │ Ros2Adapter  │  │ WSAdapter    │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
│                                                           │
│  - Run in separate threads                               │
│  - Handle async callbacks                                │
│  - Bridge: async → sync via scheduleLogicalAction()     │
│  - Block: wait for responses via waitForPortData()      │
└─────────────────────────────────────────────────────────┘
                              ↓
                  ┌───────────────────────┐
                  │  Thread-Safe Boundary  │
                  │                        │
                  │  scheduleLogicalAction()│
                  │  waitForPortData()     │
                  └───────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────┐
│        Reactor (Deterministic, Single-Threaded)         │
│                                                           │
│  - FSM states coordinate logic                           │
│  - Store contains business logic                         │
│  - Ports for all I/O                                     │
│  - No async code!                                        │
└─────────────────────────────────────────────────────────┘
```

### IOAdapter Base Class

```cpp
// ═══════════════════════════════════════════════════════════════
// IO ADAPTER: Base class for all I/O framework integrations
// ═══════════════════════════════════════════════════════════════

namespace services {

template<typename ReactorType>
class IOAdapter {
public:
    IOAdapter(ReactorType* reactor) : reactor_(reactor) {}

    virtual ~IOAdapter() = default;

    // ────────────────────────────────────────────────────────────
    // Lifecycle
    // ────────────────────────────────────────────────────────────

    // Start the I/O framework (typically spawns thread)
    virtual void start() = 0;

    // Stop the I/O framework gracefully
    virtual void stop() = 0;

protected:
    ReactorType* reactor_;

    // ────────────────────────────────────────────────────────────
    // Helpers: Bridge async I/O to deterministic reactor
    // ────────────────────────────────────────────────────────────

    // Bridge async event to reactor (thread-safe)
    void scheduleReactorAction(const std::string& action, const PortData& data) {
        reactor_->scheduleLogicalAction(action, data);
    }

    // Wait for reactor response (blocking, thread-safe)
    template<typename T>
    T waitForReactorResponse(const std::string& port_name,
                             std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        return reactor_->template waitForPortData<T>(port_name, timeout);
    }
};

} // namespace services
```

### Example: gRPC Adapter

```cpp
// ═══════════════════════════════════════════════════════════════
// GRPC ADAPTER: Bridges async gRPC to deterministic reactor
// ═══════════════════════════════════════════════════════════════

template<typename ReactorType>
class GrpcAdapter : public IOAdapter<ReactorType> {
public:
    GrpcAdapter(ReactorType* reactor, const std::string& server_address)
        : IOAdapter<ReactorType>(reactor)
        , server_address_(server_address)
    {}

    void start() override {
        // Start gRPC server in separate thread
        server_thread_ = std::thread([this]() {
            RunGrpcEventLoop();
        });
    }

    void stop() override {
        running_ = false;
        server_->Shutdown();
        cq_->Shutdown();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

private:
    std::string server_address_;
    std::unique_ptr<grpc::Server> server_;
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};

    void RunGrpcEventLoop() {
        running_ = true;

        // gRPC async event loop
        while (running_) {
            void* tag;
            bool ok;

            // Block waiting for gRPC event
            if (!cq_->Next(&tag, &ok)) {
                break;  // CQ shutdown
            }

            if (!ok) {
                continue;
            }

            // Process gRPC request
            auto* call = static_cast<GrpcCall*>(tag);

            // ────────────────────────────────────────────────────
            // BOUNDARY: Convert async gRPC to sync reactor event
            // ────────────────────────────────────────────────────

            // 1. Convert gRPC request to PortData
            PortData request_data = call->request.toPortData();

            // 2. Schedule logical action in reactor (thread-safe!)
            this->scheduleReactorAction("handle_grpc_request", request_data);

            // 3. Wait for reactor response (blocking, thread-safe!)
            try {
                auto response_data = this->template waitForReactorResponse<ResponseData>(
                    "grpc_response_out",
                    std::chrono::seconds(5)
                );

                // 4. Send gRPC response
                call->sendResponse(response_data);

            } catch (const std::exception& e) {
                // Timeout or error
                call->sendError(grpc::StatusCode::DEADLINE_EXCEEDED, e.what());
            }

            // Cleanup
            delete call;
        }
    }
};
```

### Example: ROS2 Adapter

```cpp
// ═══════════════════════════════════════════════════════════════
// ROS2 ADAPTER: Bridges ROS2 callbacks to deterministic reactor
// ═══════════════════════════════════════════════════════════════

template<typename ReactorType>
class Ros2Adapter : public IOAdapter<ReactorType> {
public:
    Ros2Adapter(ReactorType* reactor, rclcpp::Node::SharedPtr node)
        : IOAdapter<ReactorType>(reactor)
        , node_(node)
    {}

    void start() override {
        // Create ROS2 subscription
        sub_ = node_->create_subscription<std_msgs::msg::String>(
            "input_topic",
            10,
            [this](const std_msgs::msg::String::SharedPtr msg) {
                this->handleRosMessage(msg);
            }
        );

        // Spin ROS2 executor in separate thread
        executor_thread_ = std::thread([this]() {
            rclcpp::spin(node_);
        });
    }

    void stop() override {
        rclcpp::shutdown();
        if (executor_thread_.joinable()) {
            executor_thread_.join();
        }
    }

private:
    rclcpp::Node::SharedPtr node_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr sub_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr pub_;
    std::thread executor_thread_;

    void handleRosMessage(const std_msgs::msg::String::SharedPtr msg) {
        // ────────────────────────────────────────────────────────
        // BOUNDARY: Convert async ROS2 callback to sync reactor event
        // ────────────────────────────────────────────────────────

        // 1. Convert ROS message to PortData
        PortData data;
        data.string_value = msg->data;

        // 2. Schedule in reactor (thread-safe!)
        this->scheduleReactorAction("handle_ros_message", data);

        // 3. Optionally wait for response
        try {
            auto response = this->template waitForReactorResponse<std::string>(
                "ros_response_out",
                std::chrono::milliseconds(100)
            );

            // 4. Publish ROS response
            auto response_msg = std_msgs::msg::String();
            response_msg.data = response;
            pub_->publish(response_msg);

        } catch (const std::exception& e) {
            RCLCPP_ERROR(node_->get_logger(), "Response timeout: %s", e.what());
        }
    }
};
```

---

## Implementation Plan

### Phase 1: Core Reactor Refactor

**Goal:** Enforce FSM-driven pattern and make Store/Ports/Container private.

#### Tasks:
1. ✅ Add `StepTrigger` struct to mscpp
2. ✅ Update FSM state `step()` signature to include `StepTrigger` parameter
3. ✅ Make `doHeartbeat()` and `executeLogicalAction()` **final**
4. ✅ Add `doPeriodicMaintenance()` virtual hook
5. ✅ Make `mStore`, `mPorts`, `mContainer` **private**
6. ✅ Update all FSM step calls to pass `StepTrigger`
7. ✅ Add compile-time checks for Store purity (optional)

**Files to Modify:**
- `include/mscpp/MicroServiceReactors.h`
- `include/mscpp/StateSet.h`
- `include/mscpp/StepTrigger.h` (new file)

### Phase 2: I/O Adapter Infrastructure

**Goal:** Add thread-safe boundary for async I/O frameworks.

#### Tasks:
1. ✅ Create `IOAdapter<ReactorType>` base class
2. ✅ Add `scheduleLogicalAction(action, port_data)` to reactor
3. ✅ Add `waitForPortData<T>(port_name, timeout)` to reactor
4. ✅ Add mutex/condition variable for thread safety
5. ✅ Add `PortData` serialization helpers
6. ✅ Implement `processPendingActions()` in doHeartbeat()
7. ✅ Implement `notifyPortUpdates()` after FSM steps

**Files to Create:**
- `include/mscpp/IOAdapter.h`
- `include/mscpp/PortData.h`

**Files to Modify:**
- `include/mscpp/MicroServiceReactors.h`

### Phase 3: Example I/O Adapters

**Goal:** Provide reference implementations for common I/O frameworks.

#### Tasks:
1. ✅ Implement `GrpcAdapter<ReactorType>`
2. ✅ Implement `Ros2Adapter<ReactorType>`
3. ✅ Create example applications using adapters
4. ✅ Write integration tests

**Files to Create:**
- `include/mscpp/adapters/GrpcAdapter.h`
- `include/mscpp/adapters/Ros2Adapter.h`
- `examples/grpc_reactor/`
- `examples/ros2_reactor/`

### Phase 4: Documentation

**Goal:** Update all documentation to reflect new patterns.

#### Tasks:
1. ✅ Update `README.md` with new architecture overview
2. ✅ Update `GETTING_STARTED.md` with FSM-driven examples
3. ✅ Create `REACTOR_PATTERNS.md` design guide
4. ✅ Create `IO_ADAPTERS.md` integration guide
5. ✅ Update all example code to follow new patterns
6. ✅ Add migration guide for existing code

**Files to Create/Update:**
- `README.md`
- `GETTING_STARTED.md`
- `REACTOR_PATTERNS.md` (new)
- `IO_ADAPTERS.md` (new)
- `MIGRATION_GUIDE.md` (new)

### Phase 5: Testing & Validation

**Goal:** Ensure refactor doesn't break existing functionality.

#### Tasks:
1. ✅ Update existing tests to new API
2. ✅ Add tests for `StepTrigger` functionality
3. ✅ Add tests for I/O adapter thread safety
4. ✅ Add integration tests with gRPC and ROS2
5. ✅ Performance benchmarks (ensure no regression)

---

## Migration Guide

### For Existing Reactors

#### Before (Old Pattern):
```cpp
class MyReactor : public MicroServiceFSMReactor<Name, Store, Ports, Container, States> {
    void executeLogicalAction(const LogicalTag& tag, const std::string& action) override {
        // Business logic here - BAD!
        if (action == "process") {
            auto data = mPorts.input.get();
            auto result = processData(data);
            mPorts.output.set(result);
        }
    }
};
```

#### After (New Pattern):
```cpp
// 1. Move business logic to Store
struct Store {
    ProcessResult processData(const Input& input) const {
        // Business logic here - testable!
        return {...};
    }
};

// 2. Move coordination to FSM state
struct RunningState : public State<RunningState, 1> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag, const StepTrigger& trigger) {

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
            if (trigger.action_name == "process") {
                if (p.input.is_present()) {
                    auto data = p.input.get();
                    auto result = s.processData(data);  // Call Store!
                    p.output.set(result);
                }
            }
        }

        return RunningState::index();
    }
};

// 3. Reactor is now thin wrapper (no override needed!)
class MyReactor : public MicroServiceFSMReactor<Name, Store, Ports, Container, States> {
    using Base::Base;  // Just inherit constructor
};
```

---

## Examples

### Example 1: Simple Reactor (No I/O Adapter)

```cpp
// Store with pure business logic
struct CounterStore {
    int count{0};

    int increment() {
        return ++count;
    }

    int getValue() const {
        return count;
    }
};

// Ports
struct CounterPorts {
    InputPort<int> increment_in;
    OutputPort<int> count_out;
};

// FSM State
struct RunningState : public State<RunningState, 0> {
    size_t step(CounterStore& s, CounterPorts& p, const Container& c,
                const LogicalTag& tag, const StepTrigger& trigger) {

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
            if (trigger.action_name == "increment") {
                if (p.increment_in.is_present()) {
                    p.increment_in.get();  // Consume
                    int new_count = s.increment();  // Store function
                    p.count_out.set(new_count);     // Output
                }
            }
        }

        return RunningState::index();
    }
};

// Reactor (thin wrapper)
using CounterReactor = MicroServiceFSMReactor<
    Name,
    CounterStore,
    CounterPorts,
    Container,
    StateSet<RunningState>
>;
```

### Example 2: Reactor with gRPC Adapter

```cpp
int main() {
    // 1. Create reactor
    Container container;
    auto reactor = std::make_shared<MyServiceReactor>(container);

    // 2. Create gRPC adapter (partitioned off!)
    GrpcAdapter<MyServiceReactor> grpc_adapter(reactor.get(), "0.0.0.0:50051");

    // 3. Start gRPC adapter (runs in separate thread)
    grpc_adapter.start();

    // 4. Start reactor scheduler (deterministic)
    ReactorScheduler scheduler;
    scheduler.registerReactor(reactor);
    scheduler.run();

    // 5. Cleanup
    grpc_adapter.stop();

    return 0;
}
```

---

## Benefits

### 1. **Deterministic Execution**
- ✅ Single-threaded reactor with tag-based event ordering
- ✅ FSM states enforce what can happen when
- ✅ Clear event flow: trigger → FSM step → Store logic → ports

### 2. **Testability**
- ✅ Store business logic is pure functions (easy to unit test)
- ✅ FSM states are testable (mock Store and Ports)
- ✅ I/O adapters are testable in isolation

### 3. **Maintainability**
- ✅ Clear separation of concerns (Store/FSM/Reactor/I/O)
- ✅ Easy to understand event flow
- ✅ Compile-time enforcement prevents mistakes

### 4. **Flexibility**
- ✅ Easy to add new I/O frameworks (just implement IOAdapter)
- ✅ No contamination of reactor with async code
- ✅ Thread-safe boundary with clear semantics

---

## Open Questions

1. **Backward Compatibility:** Should we maintain old API as deprecated, or force migration?
   - **Recommendation:** Deprecate old API, provide migration guide

2. **Performance:** Does thread synchronization in I/O adapters add measurable overhead?
   - **Action:** Run benchmarks to measure

3. **PortData Serialization:** How should we handle complex port types?
   - **Recommendation:** Use variant or protobuf for serialization

4. **Existing Examples:** Should we update all examples to new pattern?
   - **Recommendation:** Yes - examples should showcase best practices

---

## References

- [orchestrator-cpp PORT_DEFINITIONS_V2.md](../orchestrator-cpp/.claude/PORT_DEFINITIONS_V2.md) - Event-driven reactor design
- [orchestrator-cpp JobExecutor.h](../orchestrator-cpp/include/orchestrator/JobExecutor.h) - FSM state example
- [orchestrator-cpp JobDatabase.h](../orchestrator-cpp/include/orchestrator/JobDatabase.h) - Current pattern issues

---

**End of Design Document**
