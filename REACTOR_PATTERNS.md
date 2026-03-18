# mscpp Reactor Patterns Guide

This document provides comprehensive architectural patterns for building deterministic, testable reactors using mscpp's FSM-driven framework.

## Table of Contents

1. [Architecture Principles](#architecture-principles)
2. [Three-Layer Pattern](#three-layer-pattern)
3. [Store Patterns](#store-patterns)
4. [FSM State Patterns](#fsm-state-patterns)
5. [StepTrigger Patterns](#steptrigger-patterns)
6. [Port Patterns](#port-patterns)
7. [Testing Patterns](#testing-patterns)
8. [Anti-Patterns](#anti-patterns)

---

## Architecture Principles

### Core Tenets

mscpp enforces four key principles through compile-time and runtime mechanisms:

1. **Deterministic Execution**: Tag-based logical time with deterministic event ordering
2. **Testable Business Logic**: All business logic in pure Store functions
3. **FSM-Driven Coordination**: States enforce what can happen when
4. **Compile-Time Enforcement**: Final methods and private members prevent violations

### The Enforcement Model

```cpp
// ═══════════════════════════════════════════════════════════════
// ENFORCEMENT: How mscpp prevents architectural violations
// ═══════════════════════════════════════════════════════════════

template<typename Store, typename Ports, typename Container, typename States>
class MicroServiceFSMReactor : public IReactor {
public:
    // FINAL: Cannot override - enforces FSM pattern
    void doHeartbeat(const LogicalTag& tag) final override;
    void executeLogicalAction(const LogicalTag& tag, const std::string& action) final override;

    // PUBLIC: Accessors for testing
    Store& getStore() { return mStore; }
    const Store& getStore() const { return mStore; }
    Ports& getPorts() { return mPorts; }
    const Ports& getPorts() const { return mPorts; }

protected:
    // VIRTUAL: Optional hook for periodic maintenance (NOT business logic)
    virtual void doPeriodicMaintenance(const LogicalTag& tag) {}

private:
    // PRIVATE: Only FSM states can access via step() parameters
    Store mStore;
    Ports mPorts;
    Container mContainer;
    size_t mCurrentState;
};
```

**What This Prevents:**
- ❌ Overriding `doHeartbeat()` or `executeLogicalAction()` to bypass FSM
- ❌ Direct access to `mStore`/`mPorts`/`mContainer` from reactor methods
- ❌ Business logic outside of FSM states

**What This Allows:**
- ✅ Testing via public `getStore()` and `getPorts()` accessors
- ✅ Periodic maintenance via `doPeriodicMaintenance()` hook
- ✅ All business logic coordinated through FSM states

---

## Three-Layer Pattern

### Layer 1: Store (Pure Business Logic)

**Pattern: All business logic lives in Store as pure functions**

```cpp
// ═══════════════════════════════════════════════════════════════
// STORE PATTERN: Pure business logic (100% unit testable)
// ═══════════════════════════════════════════════════════════════

struct JobQueueStore {
    std::vector<Job> pending_jobs;
    std::map<JobId, JobStatus> job_status;
    int next_job_id{1};

    // ────────────────────────────────────────────────────────────
    // PURE FUNCTIONS: Business logic that returns results
    // ────────────────────────────────────────────────────────────

    struct EnqueueResult {
        bool success;
        JobId job_id;
        std::string error_message;
    };

    // Pure function: validates and enqueues job
    EnqueueResult enqueueJob(const JobRequest& request, size_t max_queue_size) const {
        // Validation logic (pure)
        if (request.priority < 0 || request.priority > 100) {
            return {false, JobId{0}, "Invalid priority"};
        }

        if (pending_jobs.size() >= max_queue_size) {
            return {false, JobId{0}, "Queue full"};
        }

        // Create job (pure transformation)
        Job job{
            .id = JobId{next_job_id},
            .priority = request.priority,
            .payload = request.payload
        };

        return {true, job.id, ""};
    }

    // ────────────────────────────────────────────────────────────
    // MUTATION FUNCTIONS: Minimal state updates
    // ────────────────────────────────────────────────────────────

    void addJob(const Job& job) {
        pending_jobs.push_back(job);
        job_status[job.id] = JobStatus::PENDING;
        next_job_id++;
    }

    void removeJob(JobId id) {
        auto it = std::find_if(pending_jobs.begin(), pending_jobs.end(),
                              [id](const Job& j) { return j.id == id; });
        if (it != pending_jobs.end()) {
            pending_jobs.erase(it);
        }
        job_status[id] = JobStatus::COMPLETED;
    }

    // ────────────────────────────────────────────────────────────
    // QUERY FUNCTIONS: Read-only const methods
    // ────────────────────────────────────────────────────────────

    bool hasJob(JobId id) const {
        return job_status.find(id) != job_status.end();
    }

    std::optional<Job> getNextJob() const {
        if (pending_jobs.empty()) return std::nullopt;

        // Find highest priority job (pure logic)
        auto it = std::max_element(pending_jobs.begin(), pending_jobs.end(),
                                   [](const Job& a, const Job& b) {
                                       return a.priority < b.priority;
                                   });

        return *it;
    }

    size_t queueSize() const {
        return pending_jobs.size();
    }
};
```

**Key Principles:**
1. **Pure Functions**: Validate, transform, compute - no mutation
2. **Minimal Mutation**: Separate mutation functions (addJob, removeJob)
3. **Const Queries**: All read-only methods are const
4. **No Dependencies**: No ports, reactor, or I/O dependencies
5. **100% Testable**: Every function can be unit tested in isolation

### Layer 2: FSM States (Coordination Logic)

**Pattern: FSM states coordinate Store + Ports - NO business logic**

```cpp
// ═══════════════════════════════════════════════════════════════
// FSM STATE PATTERN: Coordination logic (NO business logic!)
// ═══════════════════════════════════════════════════════════════

struct ProcessingState : public State<ProcessingState, 1> {
    size_t step(JobQueueStore& store,
                JobQueuePorts& ports,
                const MicroServiceContainer<>& container,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        // ────────────────────────────────────────────────────────
        // PATTERN: Check trigger → Read ports → Call Store → Write ports
        // ────────────────────────────────────────────────────────

        // Handle enqueue request (logical action)
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_enqueue_request") {

            if (ports.enqueue_request_in.is_present()) {
                auto request = ports.enqueue_request_in.get();

                // 1. Call Store pure function (business logic!)
                auto result = store.enqueueJob(request, 1000);

                // 2. Handle result (coordination!)
                if (result.success) {
                    // 3a. Update Store state (minimal mutation)
                    Job job{result.job_id, request.priority, request.payload};
                    store.addJob(job);

                    // 3b. Write output port
                    ports.enqueue_response_out.set(EnqueueResponse{
                        .success = true,
                        .job_id = result.job_id
                    });
                } else {
                    // 3c. Write error port
                    ports.error_out.set(result.error_message);
                }
            }
        }

        // Handle job execution request
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_execute_request") {

            // 1. Query Store (pure function)
            auto next_job = store.getNextJob();

            if (next_job) {
                // 2. Write output port
                ports.job_out.set(*next_job);

                // 3. Update Store (mutation)
                store.removeJob(next_job->id);
            } else {
                // No jobs available
                ports.error_out.set("Queue empty");
            }
        }

        // Periodic heartbeat maintenance
        if (trigger.type == StepTrigger::Type::HEARTBEAT) {
            // Query Store for status (pure)
            size_t queue_size = store.queueSize();

            // Write status port
            ports.status_out.set(QueueStatus{
                .pending_jobs = queue_size,
                .timestamp = tag.time
            });
        }

        // State transition logic (coordination)
        if (store.queueSize() == 0) {
            // Transition to idle state
            return IdleState::index();
        }

        return ProcessingState::index();  // Stay in current state
    }
};
```

**Key Pattern:**
1. **Check Trigger**: Determine context (heartbeat, logical action, action name)
2. **Read Ports**: Use `is_present()` and `get()` to read inputs
3. **Call Store**: Delegate to Store pure functions for business logic
4. **Write Ports**: Use `set()` to write outputs
5. **State Transitions**: Return next state index based on Store queries

### Layer 3: Reactor (Thin Enforcement Shell)

**Pattern: Reactor is just a type definition - no overrides needed**

```cpp
// ═══════════════════════════════════════════════════════════════
// REACTOR PATTERN: Thin wrapper (minimal code!)
// ═══════════════════════════════════════════════════════════════

class JobQueueReactor : public MicroServiceFSMReactor<
    JobQueueStore,
    JobQueuePorts,
    MicroServiceContainer<>,
    StateSet<IdleState, ProcessingState>
> {
public:
    using Base = MicroServiceFSMReactor<JobQueueStore, JobQueuePorts,
                                        MicroServiceContainer<>,
                                        StateSet<IdleState, ProcessingState>>;

    JobQueueReactor(const std::string& name)
        : Base(name,
               MicroServiceContainer<>{},
               {
                   {"enqueue_request_in", "on_enqueue_request"},
                   {"execute_request_in", "on_execute_request"}
               }) {}

protected:
    // OPTIONAL: Periodic maintenance (NOT business logic!)
    void doPeriodicMaintenance(const LogicalTag& tag) override {
        // Logging
        auto time_s = tag.time.count() / 1'000'000'000;
        if (time_s % 60 == 0) {
            SPDLOG_INFO("Queue size: {}", getStore().queueSize());
        }

        // Metrics
        metrics_.record("queue_size", getStore().queueSize());
    }

private:
    MetricsCollector metrics_;  // OK: non-business-logic infrastructure
};
```

**Key Points:**
- Reactor is just a constructor + optional `doPeriodicMaintenance()`
- Port action map: `{"port_name", "action_name"}`
- No business logic - all logic is in Store and FSM States
- `doPeriodicMaintenance()` is for logging/metrics ONLY (not business logic)

---

## Store Patterns

### Pattern 1: Result Objects for Complex Logic

**Use result objects to return multiple values from pure functions:**

```cpp
struct ProcessResult {
    bool success;
    OutputData data;
    std::string error_message;
    std::vector<Warning> warnings;
};

ProcessResult processRequest(const InputData& input) const {
    if (!validateInput(input)) {
        return {false, {}, "Invalid input", {}};
    }

    auto output = transform(input);
    auto warnings = checkWarnings(output);

    return {true, output, "", warnings};
}
```

### Pattern 2: Query Functions (Const Methods)

**All read-only logic should be const methods:**

```cpp
struct Store {
    std::vector<Item> items;

    // Query: find items matching predicate
    std::vector<Item> findItems(std::function<bool(const Item&)> predicate) const {
        std::vector<Item> result;
        std::copy_if(items.begin(), items.end(),
                    std::back_inserter(result), predicate);
        return result;
    }

    // Query: check if item exists
    bool hasItem(ItemId id) const {
        return std::find_if(items.begin(), items.end(),
                           [id](const Item& item) { return item.id == id; }) != items.end();
    }

    // Query: get statistics
    Statistics getStatistics() const {
        return {
            .total_items = items.size(),
            .average_value = computeAverage(items)
        };
    }
};
```

### Pattern 3: Separate Mutation from Logic

**Keep mutation minimal and separate from business logic:**

```cpp
struct Store {
    // ────────────────────────────────────────────────────────────
    // PURE LOGIC: No mutation (const methods)
    // ────────────────────────────────────────────────────────────

    struct ValidationResult {
        bool valid;
        std::string error;
    };

    ValidationResult validateRequest(const Request& req) const {
        if (req.amount < 0) return {false, "Negative amount"};
        if (req.amount > balance) return {false, "Insufficient balance"};
        return {true, ""};
    }

    int computeNewBalance(int current_balance, int amount) const {
        return current_balance - amount;
    }

    // ────────────────────────────────────────────────────────────
    // MUTATION: Minimal state updates (non-const)
    // ────────────────────────────────────────────────────────────

    void updateBalance(int new_balance) {
        balance = new_balance;
        last_update = std::chrono::system_clock::now();
    }

    void recordTransaction(const Transaction& txn) {
        transactions.push_back(txn);
    }

private:
    int balance{0};
    std::vector<Transaction> transactions;
    std::chrono::system_clock::time_point last_update;
};
```

**FSM State uses this pattern:**

```cpp
// In FSM state step() function:
auto validation = store.validateRequest(request);
if (!validation.valid) {
    ports.error_out.set(validation.error);
    return CurrentState::index();
}

int new_balance = store.computeNewBalance(store.balance, request.amount);
store.updateBalance(new_balance);
store.recordTransaction(Transaction{...});
```

---

## FSM State Patterns

### Pattern 1: Multi-State FSM with Transitions

**Use state transitions to model protocols and workflows:**

```cpp
// State 0: Idle (waiting for requests)
struct IdleState : public State<IdleState, 0> {
    size_t step(...) {
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "start_processing") {
            return ProcessingState::index();  // Transition to state 1
        }
        return IdleState::index();  // Stay in idle
    }
};

// State 1: Processing (active work)
struct ProcessingState : public State<ProcessingState, 1> {
    size_t step(...) {
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "pause") {
            return PausedState::index();  // Transition to state 2
        }

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "complete") {
            return CompletedState::index();  // Transition to state 3
        }

        // Do work
        if (ports.work_in.is_present()) {
            auto result = store.processWork(ports.work_in.get());
            ports.result_out.set(result);
        }

        return ProcessingState::index();  // Stay in processing
    }
};

// State 2: Paused (can resume or cancel)
struct PausedState : public State<PausedState, 2> {
    size_t step(...) {
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "resume") {
            return ProcessingState::index();  // Back to state 1
        }

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "cancel") {
            return IdleState::index();  // Back to state 0
        }

        return PausedState::index();  // Stay paused
    }
};

// State 3: Completed (final state)
struct CompletedState : public State<CompletedState, 3> {
    size_t step(...) {
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "reset") {
            return IdleState::index();  // Back to state 0
        }
        return CompletedState::index();  // Stay completed
    }
};

using WorkflowStates = StateSet<IdleState, ProcessingState, PausedState, CompletedState>;
```

**State Transition Diagram:**
```
       ┌─────────┐  start_processing  ┌──────────────┐
   ┌───┤  Idle   ├───────────────────>│  Processing  │
   │   └─────────┘                     └──────┬───────┘
   │                                          │
   │                                    pause │    complete
   │                                          │        │
   │   ┌─────────┐      resume        ┌──────▼───┐   │
   │   │ Paused  │<────────────────────┤ Paused   │   │
   │   └────┬────┘                     └──────────┘   │
   │        │ cancel                                   │
   │        │                                          │
   │        └──────────────┐                          │
   │                       │                          │
   │   ┌───────────┐       │   ┌────────────┐        │
   └───┤ Completed │<──────┴───┤ Completed  │<───────┘
       └───────────┘  reset    └────────────┘
```

### Pattern 2: Guard Conditions for Transitions

**Use Store functions to determine if transitions should occur:**

```cpp
struct ActiveState : public State<ActiveState, 1> {
    size_t step(MyStore& store, MyPorts& ports,
                const MicroServiceContainer<>& container,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        // Handle requests
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
            // ... process requests
        }

        // Check if should transition (guard condition from Store)
        if (trigger.type == StepTrigger::Type::HEARTBEAT) {
            // Use Store pure function to determine transition
            if (store.shouldEnterIdleMode(tag.time)) {
                ports.status_out.set("Entering idle mode");
                return IdleState::index();
            }

            if (store.shouldEnterErrorMode()) {
                ports.error_out.set("Entering error mode");
                return ErrorState::index();
            }
        }

        return ActiveState::index();  // Stay active
    }
};
```

---

## StepTrigger Patterns

### Pattern 1: Separating Heartbeat and Event Logic

**Use StepTrigger to handle different execution contexts:**

```cpp
struct MyState : public State<MyState, 0> {
    size_t step(MyStore& store, MyPorts& ports,
                const MicroServiceContainer<>& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        // ────────────────────────────────────────────────────────
        // HEARTBEAT: Periodic work (time-triggered)
        // ────────────────────────────────────────────────────────

        if (trigger.type == StepTrigger::Type::HEARTBEAT) {
            // Periodic status updates
            ports.status_out.set(store.getStatus());

            // Check timeouts
            auto expired = store.getExpiredItems(tag.time);
            for (const auto& item : expired) {
                ports.timeout_out.set(item);
                store.removeItem(item.id);
            }

            // Periodic cleanup
            store.cleanupOldData(tag.time);
        }

        // ────────────────────────────────────────────────────────
        // LOGICAL ACTIONS: Event-driven work (event-triggered)
        // ────────────────────────────────────────────────────────

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
            if (trigger.action_name == "process_request") {
                if (ports.request_in.is_present()) {
                    auto result = store.processRequest(ports.request_in.get());
                    ports.response_out.set(result);
                }
            }
            else if (trigger.action_name == "cancel_operation") {
                if (ports.cancel_in.is_present()) {
                    auto id = ports.cancel_in.get();
                    store.cancelOperation(id);
                    ports.cancel_response_out.set(true);
                }
            }
        }

        return MyState::index();
    }
};
```

### Pattern 2: Action Name Routing

**Use action names to route different types of logical actions:**

```cpp
struct ProcessingState : public State<ProcessingState, 0> {
    size_t step(..., const StepTrigger& trigger) override {

        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
            // Route based on action name
            if (trigger.action_name == "on_port_request_in") {
                handleRequest(store, ports);
            }
            else if (trigger.action_name == "on_port_cancel_in") {
                handleCancel(store, ports);
            }
            else if (trigger.action_name == "on_timeout") {
                handleTimeout(store, ports);
            }
            else if (trigger.action_name == "on_retry") {
                handleRetry(store, ports);
            }
        }

        return ProcessingState::index();
    }

private:
    void handleRequest(MyStore& store, MyPorts& ports) {
        if (ports.request_in.is_present()) {
            auto result = store.processRequest(ports.request_in.get());
            ports.response_out.set(result);
        }
    }

    void handleCancel(MyStore& store, MyPorts& ports) {
        if (ports.cancel_in.is_present()) {
            store.cancelOperation(ports.cancel_in.get());
        }
    }

    void handleTimeout(MyStore& store, MyPorts& ports) {
        store.handleTimeout();
        ports.error_out.set("Operation timed out");
    }

    void handleRetry(MyStore& store, MyPorts& ports) {
        auto pending = store.getPendingRetries();
        for (const auto& item : pending) {
            ports.retry_out.set(item);
        }
    }
};
```

---

## Port Patterns

### Pattern 1: Request-Response with Logical Actions

**Use logical actions for zero-latency request-response:**

```cpp
// Port action mapping
std::map<std::string, std::string> port_actions = {
    {"request_in", "on_request"},
    {"response_in", "on_response"}
};

// Client state (sends requests, receives responses)
struct ClientState : public State<ClientState, 0> {
    size_t step(ClientStore& s, ClientPorts& p,
                const MicroServiceContainer<>& c,
                const LogicalTag& tag,
                const StepTrigger& trigger) override {

        // Heartbeat: send periodic request
        if (trigger.type == StepTrigger::Type::HEARTBEAT) {
            auto request = s.createRequest(tag.time);
            p.request_out.set(request);
        }

        // Logical action: handle response immediately (zero-delay!)
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "on_response") {
            if (p.response_in.is_present()) {
                s.handleResponse(p.response_in.get());
            }
        }

        return ClientState::index();
    }
};

// Server state (receives requests, sends responses)
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

**Timeline:**
```
Tag (0ms, 0): Client heartbeat → sends request
Tag (0ms, 1): Server receives request (logical action "on_request")
Tag (0ms, 2): Server processes and sends response (same logical time!)
Tag (0ms, 3): Client receives response (logical action "on_response")
Total: 0ms logical latency (4 microsteps at same time)
```

### Pattern 2: Conditional Port Output

**Only set output ports when conditions are met:**

```cpp
struct ProcessingState : public State<ProcessingState, 0> {
    size_t step(...) {
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "process") {
            if (ports.input_in.is_present()) {
                auto input = ports.input_in.get();

                // Call Store for validation and processing
                auto validation = store.validate(input);
                if (!validation.valid) {
                    // Only set error port if validation fails
                    ports.error_out.set(validation.error_message);
                    return ProcessingState::index();
                }

                auto result = store.process(input);

                // Conditional output based on result
                if (result.success) {
                    ports.output_out.set(result.data);
                    ports.success_count_out.set(store.success_count);
                } else {
                    ports.error_out.set(result.error_message);
                }

                // Optional: set alert port if threshold exceeded
                if (store.shouldAlert()) {
                    ports.alert_out.set(AlertData{...});
                }
            }
        }

        return ProcessingState::index();
    }
};
```

---

## Testing Patterns

### Pattern 1: Unit Test Store Functions

**Test all Store functions in isolation:**

```cpp
#include <catch2/catch.hpp>

TEST_CASE("JobQueueStore: enqueueJob validation", "[Store][Unit]") {
    JobQueueStore store;

    SECTION("Valid job is accepted") {
        JobRequest request{.priority = 50, .payload = "data"};
        auto result = store.enqueueJob(request, 1000);

        REQUIRE(result.success);
        REQUIRE(result.job_id.value == 1);
        REQUIRE(result.error_message.empty());
    }

    SECTION("Invalid priority is rejected") {
        JobRequest request{.priority = -5, .payload = "data"};
        auto result = store.enqueueJob(request, 1000);

        REQUIRE_FALSE(result.success);
        REQUIRE(result.error_message == "Invalid priority");
    }

    SECTION("Queue full is rejected") {
        JobRequest request{.priority = 50, .payload = "data"};
        auto result = store.enqueueJob(request, 0);  // max_queue_size = 0

        REQUIRE_FALSE(result.success);
        REQUIRE(result.error_message == "Queue full");
    }
}

TEST_CASE("JobQueueStore: getNextJob priority ordering", "[Store][Unit]") {
    JobQueueStore store;

    // Add jobs with different priorities
    store.addJob(Job{JobId{1}, 10, "low"});
    store.addJob(Job{JobId{2}, 90, "high"});
    store.addJob(Job{JobId{3}, 50, "medium"});

    SECTION("Returns highest priority job") {
        auto next = store.getNextJob();

        REQUIRE(next.has_value());
        REQUIRE(next->id.value == 2);  // Highest priority
        REQUIRE(next->priority == 90);
    }
}
```

### Pattern 2: Integration Test Reactor Behavior

**Test complete reactor behavior:**

```cpp
TEST_CASE("JobQueueReactor: enqueue and dequeue flow", "[Reactor][Integration]") {
    JobQueueReactor reactor("test_queue");
    LogicalTag tag{LogicalTime{1000000000}};

    SECTION("Enqueue job via logical action") {
        // Set input port
        JobRequest request{.priority = 75, .payload = "test_data"};
        reactor.getPorts().enqueue_request_in.set(request);

        // Trigger logical action
        reactor.executeLogicalAction(tag, "on_enqueue_request");

        // Verify Store was updated
        REQUIRE(reactor.getStore().queueSize() == 1);

        // Verify output port
        REQUIRE(reactor.getPorts().enqueue_response_out.is_present());
        auto response = reactor.getPorts().enqueue_response_out.get();
        REQUIRE(response.success);
        REQUIRE(response.job_id.value == 1);
    }

    SECTION("Dequeue returns highest priority job") {
        // Pre-populate queue
        reactor.getStore().addJob(Job{JobId{1}, 10, "low"});
        reactor.getStore().addJob(Job{JobId{2}, 90, "high"});

        // Trigger execute request
        reactor.getPorts().execute_request_in.set(true);
        reactor.executeLogicalAction(tag, "on_execute_request");

        // Verify highest priority job was returned
        REQUIRE(reactor.getPorts().job_out.is_present());
        auto job = reactor.getPorts().job_out.get();
        REQUIRE(job.id.value == 2);  // Highest priority
        REQUIRE(job.priority == 90);

        // Verify job was removed from queue
        REQUIRE(reactor.getStore().queueSize() == 1);
    }
}
```

---

## Anti-Patterns

### Anti-Pattern 1: Business Logic in FSM States

**❌ DON'T DO THIS:**

```cpp
struct BadState : public State<BadState, 0> {
    size_t step(...) {
        if (ports.request_in.is_present()) {
            auto request = ports.request_in.get();

            // ❌ Business logic HERE - can't unit test!
            if (request.amount < 0) {
                ports.error_out.set("Invalid amount");
                return BadState::index();
            }

            if (store.balance < request.amount) {
                ports.error_out.set("Insufficient funds");
                return BadState::index();
            }

            store.balance -= request.amount;  // ❌ Direct mutation
            ports.success_out.set(true);
        }
        return BadState::index();
    }
};
```

**✅ DO THIS INSTEAD:**

```cpp
struct GoodStore {
    struct WithdrawalResult {
        bool success;
        std::string error_message;
        int new_balance;
    };

    // Pure business logic (unit testable!)
    WithdrawalResult withdraw(int amount) const {
        if (amount < 0) {
            return {false, "Invalid amount", balance};
        }
        if (balance < amount) {
            return {false, "Insufficient funds", balance};
        }
        return {true, "", balance - amount};
    }

    void updateBalance(int new_balance) {
        balance = new_balance;
    }

private:
    int balance{0};
};

struct GoodState : public State<GoodState, 0> {
    size_t step(...) {
        if (ports.request_in.is_present()) {
            auto request = ports.request_in.get();

            // ✅ Delegate to Store (business logic)
            auto result = store.withdraw(request.amount);

            // ✅ Just coordination here
            if (result.success) {
                store.updateBalance(result.new_balance);
                ports.success_out.set(true);
            } else {
                ports.error_out.set(result.error_message);
            }
        }
        return GoodState::index();
    }
};
```

### Anti-Pattern 2: Overriding doHeartbeat()

**❌ DON'T DO THIS:**

```cpp
class BadReactor : public MicroServiceFSMReactor<...> {
    void doHeartbeat(const LogicalTag& tag) override {  // ❌ Can't override (final!)
        // This won't compile - doHeartbeat() is final
    }
};
```

**✅ DO THIS INSTEAD:**

```cpp
class GoodReactor : public MicroServiceFSMReactor<...> {
protected:
    // ✅ Use doPeriodicMaintenance() hook
    void doPeriodicMaintenance(const LogicalTag& tag) override {
        // Logging (not business logic)
        SPDLOG_INFO("Heartbeat at {}", tag.time.count());

        // Metrics (not business logic)
        metrics_.record("queue_size", getStore().queueSize());
    }
};
```

### Anti-Pattern 3: Mixing Async I/O with Reactor

**❌ DON'T DO THIS:**

```cpp
class BadReactor : public MicroServiceFSMReactor<...> {
    grpc::ServerCompletionQueue* cq_;  // ❌ Async I/O in reactor!
    std::thread grpc_thread_;          // ❌ Threading in reactor!

    void someMethod() {
        // ❌ Non-deterministic async I/O
        cq_->Next(&tag, &ok);
    }
};
```

**✅ DO THIS INSTEAD:**

```cpp
// ✅ Use I/O Adapter (separate thread)
int main() {
    auto reactor = std::make_shared<GoodReactor>("service", port_actions);

    // ✅ gRPC adapter runs in separate thread
    GrpcAdapter<GoodReactor> grpc_adapter(reactor, "0.0.0.0:50051");
    grpc_adapter.start();

    // ✅ Reactor remains deterministic
    ReactorScheduler scheduler;
    reactor->setScheduler(&scheduler);
    scheduler.registerReactor(reactor);
    scheduler.run();

    grpc_adapter.stop();
    return 0;
}
```

---

## Summary

### Checklist for FSM-Driven Reactors

✅ **Store:**
- [ ] All business logic in Store functions
- [ ] Pure functions return results (no side effects where possible)
- [ ] Minimal mutation functions separate from logic
- [ ] No dependencies on ports, reactor, or I/O
- [ ] Every function can be unit tested

✅ **FSM States:**
- [ ] Coordination logic only (read ports → call Store → write ports)
- [ ] NO business logic in states
- [ ] Use StepTrigger to determine context
- [ ] State transitions based on Store queries
- [ ] Clear pattern: Check trigger → Read → Call Store → Write → Transition

✅ **Reactor:**
- [ ] Just a constructor (no overrides needed)
- [ ] Port action map defined
- [ ] Optional `doPeriodicMaintenance()` for logging/metrics only
- [ ] No business logic in reactor

✅ **Testing:**
- [ ] Unit test all Store functions
- [ ] Integration test reactor behavior
- [ ] Test state transitions
- [ ] Test error handling paths

---

**Remember:** The three-layer pattern ensures testability and determinism:
**Store (business logic) → FSM States (coordination) → Reactor (enforcement)**
