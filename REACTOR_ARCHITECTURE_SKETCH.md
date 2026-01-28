# Reactor Architecture Sketch for mscpp

## Document Purpose

This document contains the **original architectural analysis and design sketch** created before implementing Phases 1 and 2. It analyzes the existing mscpp actor-like model and provides a detailed roadmap for retrofitting it into a deterministic reactor-based model following the principles from UC Berkeley's Ptolemy project and Lingua Franca.

**Status:** Design document - used to guide Phase 1 and Phase 2 implementation
**Date:** January 2026

---

## Executive Summary

The **mscpp** library is a well-structured actor-like microservice framework built on FSMs, thread-per-service execution, and priority-based message processing. It already exhibits some reactor-like properties (structured reactions, time budgets, explicit FSM states), but lacks the core determinism guarantees of the reactor model: **logical timestamps**, **centralized tag-based scheduling**, and **causal ordering**.

This document sketches the architectural transformation needed to retrofit mscpp into a true **reactor model** following the principles outlined in the project's CLAUDE.md file.

---

## Background: Reactor Model Principles

From [.claude/CLAUDE.md](/.claude/CLAUDE.md):

### Core Reactor Concepts

- **Core Idea**: Reactors are components similar to actors but with structured reactions to timed events. Reactions are triggered by inputs, timers, or actions, producing outputs on ports.

- **Communication**: Message passing via ports/connections, but events are timestamped and processed in logical or physical time order (discrete-event or synchronous-reactive semantics).

- **Execution**: Deterministic by default—given the same inputs and timestamps, the same sequence of reactions occurs, regardless of scheduling. Dependencies form a graph; reactions at the same "tag" (time + microstep) execute in declared order.

- **Concurrency**: Supports parallelism where dependencies allow (e.g., independent reactions on multi-core), but enforces causality to avoid nondeterminism. Often static topology (fixed connections), though extensions support dynamics.

### Retrofitting Strategy

1. **Introduce Explicit Time and Timestamps**
   - Tag all messages with logical (or physical) timestamps
   - Use centralized or decentralized scheduler to process messages in timestamp order
   - Creates discrete-event semantics, ensuring reactions fire in fixed, causal order

2. **Replace Mailbox Queues with Port-Based Connections**
   - Define explicit input/output ports on reactors
   - Build dependency graph to determine reaction firing order
   - Reactions trigger only when all required inputs at a given tag are present

3. **Enforce Deterministic Execution Semantics**
   - Scheduler advances logical time, processing events tag-by-tag
   - Independent reactions can run in parallel on multi-core
   - Avoid mutable shared state; rely on message passing

---

## Current Architecture Analysis

### Repository Structure

```
/data/andrew/dev/reactors/sources/mscpp/
├── include/mscpp/
│   ├── MicroService.h           # Main service template class
│   ├── MicroServiceContainer.h  # Dependency injection container
│   ├── InputSet.h               # Input definitions
│   ├── StateSet.h               # Finite state machine state definitions
│   ├── ServiceFactory.h         # Factory for creating/managing services
│   ├── Logging.h                # Logging utilities using spdlog
│   └── internal/utils.h         # Meta-programming utilities and circular buffers
├── tests/
│   ├── example-services/
│   │   ├── ServiceA.h/.cpp      # Example service implementation
│   │   ├── ServiceB.h/.cpp      # Example dependent service
│   │   └── Inputs.h/.cpp        # Input definitions for services
│   └── ServiceTest.cpp          # Unit test using Catch2
├── CMakeLists.txt
└── README.md
```

### Current Actor-Like Multi-Threaded Model

#### Core Execution Model

The system implements a **multi-threaded microservice architecture** based on finite state machines (FSMs) with message passing:

**Key Features:**
- Each microservice runs on its own dedicated thread
- Services communicate through message passing (input sending)
- No explicit time/timestamp mechanism (currently)
- Services are independent and loosely coupled
- Execution is asynchronous with eventual consistency

#### Component Definition

Components are defined through a **template-based architecture** with several key parts:

**[MicroService.h:21-283](mscpp/include/mscpp/MicroService.h#L21-L283)**

```cpp
template<const char* Name,
         typename Store,
         typename ContainerType,
         typename States,
         typename Inputs,
         size_t InputWindow = 5,
         size_t MaxInputs   = 100>
class MicroService
```

**Template Parameters:**
- `Name`: Compile-time string constant for service name
- `Store`: Mutable state/data structure for the service
- `ContainerType`: Dependency injection container with other services
- `States`: Finite state machine states
- `Inputs`: Input types this service accepts
- `InputWindow`: Priority window for input selection (default 5)
- `MaxInputs`: Maximum queued inputs (default 100)

**Example Service Definition:**

```cpp
inline constexpr char NameA[] = "ServiceA";

using ContainerTypeA = services::MicroServiceContainer<>;

struct StoreA {
    std::string  name    = "A";
    std::string  state   = "initt";
    std::string  input   = "NONE";
    unsigned int counter = 0;
};

struct InitStateA : public services::State<InitStateA, 0> {
    size_t step(StoreA& s, const ContainerTypeA& c, HeartbeatInput& i);
    size_t step(StoreA& s, const ContainerTypeA& c, IncrementInput& i);
    size_t step(StoreA& s, const ContainerTypeA& c, TransitionInput& i);
};

using StatesA = services::StateSet<InitStateA, RunningStateA, StoppedStateA>;

using ServiceABase = services::MicroService<NameA, StoreA, ContainerTypeA, StatesA, Inputs>;

class ServiceA : public ServiceABase {
public:
    ServiceA(const ContainerTypeA& container) : ServiceABase(container) {}
};
```

#### Message Communication

**[InputSet.h:14-38](mscpp/include/mscpp/InputSet.h#L14-L38)**

```cpp
template<class T, class ResultType, uint8_t PRIORITY, uint64_t DURATION_MILLIS>
struct Input {
    using DerivedType = T;
    using Result      = std::variant<ErrorResult, ResultType>;

    std::promise<Result> promise;
    std::future<Result>  getFuture() { return promise.get_future(); }
    void setResult(Result&& result) { promise.set_value(std::move(result)); }

    constexpr uint8_t priority() const { return DURATION_MILLIS; }
    constexpr std::chrono::milliseconds duration() const {
        return std::chrono::milliseconds(DURATION_MILLIS);
    }
};
```

**Input Example:**

```cpp
struct HeartbeatInput : public services::Input<HeartbeatInput, EmptyResult, 1, 100> {};

struct IncrementInput : public services::Input<IncrementInput, BooleanResult, 2, 5> {};

struct TransitionInput : public services::Input<TransitionInput, BooleanResult, 2, 5> {
    TransitionInput(const size_t& state) : mState(state) {}
    size_t state() const;
    size_t mState;
};
```

**Sending Messages (Inter-Service Communication):**

```cpp
// ServiceB sends to ServiceA
IncrementInput input;
auto inputResult = input.getFuture();
if (!c.get<ServiceA>()->sendInput(std::move(input))) {
    throw std::runtime_error("Failed to push input; queue full.");
}
```

#### Concurrency & Threading Model

**[MicroService.h:59-96](mscpp/include/mscpp/MicroService.h#L59-L96)**

```cpp
void run() {
    if (running()) return;

    {
        std::scoped_lock lock(mMutex);
        initStore(mStore);
    }

    mRunning = true;
    mMainThread = std::thread([this]() {
        static constexpr int maxNameLength{15};
        pthread_setname_np(pthread_self(), this->name().substr(0, maxNameLength).c_str());
        mainLoop(this->mStore);
    });
}

void stop() {
    if (!running()) return;
    mRunning = false;
    if (mMainThread.joinable()) {
        mMainThread.join();
    }
}
```

**Threading Primitives:**
- **One thread per service**: Each microservice runs on its own dedicated thread (`std::thread`)
- **Atomic boolean**: `std::atomic_bool mRunning` for thread-safe shutdown signaling
- **Mutex protection**: `std::mutex mMutex` protects the service store during execution
- **Thread naming**: Uses `pthread_setname_np` for debugging (max 15 chars)

#### Scheduling & Message Processing

**Main Event Loop [MicroService.h:172-219](mscpp/include/mscpp/MicroService.h#L172-L219):**

```cpp
void mainLoop(Store& store) {
    auto heartbeatInput = getHeartbeatInput();
    constexpr auto heartbeatDur = /* ... */;

    while (running()) {
        // Execute heartbeat at fixed intervals
        auto startTime = std::chrono::steady_clock::now();
        auto next = startTime + heartbeatDur;
        {
            std::scoped_lock lock(mMutex);
            mMachine.execute(store, heartbeatInput);
        }

        auto endTime = std::chrono::steady_clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
        if (dur > heartbeatDur) {
            throw std::runtime_error(name() + ": Heartbeat execution took more than allotted time.");
        }

        auto now = std::chrono::steady_clock::now();

        // Process input messages before next heartbeat
        typename Inputs::TypesVariant nextViable;
        std::chrono::milliseconds nextDuration;
        while (getNextViableInput(nextViable, nextDuration,
                                  std::chrono::duration_cast<std::chrono::duration<double>>(next - now))) {
            startTime = std::chrono::steady_clock::now();
            std::scoped_lock lock(mMutex);
            applyApplicableInput(store, nextViable, typename Inputs::GenericInputs());
            endTime = std::chrono::steady_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::duration<double>>(endTime - startTime);
            if (dur > nextDuration) {
                throw std::runtime_error(name() + ": Input execution took more than allotted time.");
            }
            now = std::chrono::steady_clock::now();
        }
    }
}
```

**Execution Strategy:**
1. **Periodic heartbeat**: Fixed interval timer (defined in Input template)
2. **Event processing window**: Between heartbeats, process queued inputs
3. **Time budgeting**: Each reaction must complete within allocated time duration
4. **Prioritization**: Inputs with lower priority values execute first

#### Input Queue & Message Buffers

**[utils.h:312-494](mscpp/include/mscpp/internal/utils.h#L312-L494)**

```cpp
template<typename T>
class __threadsafe_circular_buffer final {
    bool push_back_if_not_full(T&& val) noexcept;
    bool timedDrainUntil(std::function<bool(T&&)> checkFunc,
                         const std::chrono::duration<double> timeLimit) noexcept;
private:
    bool                    mRunning;
    CircularBuffer<T>       mBuffer;
    std::mutex              mMutex;
    std::condition_variable mCondition;
};
```

**Key Features:**
- **Circular buffer**: Fixed-size FIFO queue with wraparound
- **Thread-safe**: Mutex-protected with condition variables
- **Bounded queue**: Returns `false` if queue is full (back-pressure mechanism)
- **Timed drain**: Processes inputs within time limit for this tick

#### Input Priority Selection

**Priority Algorithm [MicroService.h:226-280](mscpp/include/mscpp/MicroService.h#L226-L280):**

1. Examine up to `InputWindow` messages in the queue
2. Find input with **lowest priority value** that fits in remaining time
3. Process that input
4. Re-queue all other examined inputs to the front

### What mscpp Already Has (Actor Model Strengths)

| Feature | Current Implementation | Location |
|---------|----------------------|----------|
| **Component Definition** | Template-based `MicroService<>` with FSM states | [MicroService.h:21-28](mscpp/include/mscpp/MicroService.h#L21-L28) |
| **Message Passing** | Type-safe `Input<>` variants sent via `sendInput()` | [InputSet.h:14-38](mscpp/include/mscpp/InputSet.h#L14-L38) |
| **Threading** | One thread per service, `std::thread` based | [MicroService.h:72-76](mscpp/include/mscpp/MicroService.h#L72-L76) |
| **Mailbox** | Bounded FIFO `__threadsafe_circular_buffer<>` | [utils.h:312-494](mscpp/include/mscpp/internal/utils.h#L312-L494) |
| **Scheduling** | Per-service event loop with heartbeat + input window | [MicroService.h:172-219](mscpp/include/mscpp/MicroService.h#L172-L219) |
| **Priority** | Static priority values per input type | [InputSet.h:27-30](mscpp/include/mscpp/InputSet.h#L27-L30) |
| **Time Budgeting** | Physical time constraints per reaction | [MicroService.h:198-202](mscpp/include/mscpp/MicroService.h#L198-L202) |
| **Dependencies** | Compile-time service references via containers | [MicroServiceContainer.h:1-109](mscpp/include/mscpp/MicroServiceContainer.h#L1-L109) |

### What's Missing for Reactors

| Reactor Requirement | Current Gap |
|-------------------|-------------|
| **Logical Timestamps (Tags)** | Messages have no `(time, microstep)` tags; ordering is FIFO + priority |
| **Centralized Scheduler** | Each service schedules itself independently on its own thread |
| **Tag-Based Event Processing** | Events processed by physical time windows, not logical tags |
| **Deterministic Ordering** | Non-deterministic due to thread interleaving and race conditions |
| **Dependency Graph** | Services know dependencies, but no explicit reaction-level DAG |
| **Microstep Semantics** | No concept of simultaneous events at same logical time |
| **Causal Consistency** | No guarantees about cross-service event ordering |
| **Port-Based I/O** | Inputs exist, but outputs are ad-hoc `sendInput()` calls |

---

## Proposed Reactor Architecture

### Phase 1: Introduce Explicit Time and Tags

#### New Concept: Event Tags

Every event (input, timer, output) gets tagged with `(logical_time, microstep)`:

```cpp
// New Tag abstraction
struct LogicalTag {
    uint64_t time;      // Logical time in nanoseconds (or custom unit)
    uint32_t microstep; // For simultaneous events at same time

    auto operator<=>(const LogicalTag&) const = default;
};
```

#### Modified Input Structure

```cpp
template<class T, class ResultType, uint8_t PRIORITY, uint64_t DURATION_MILLIS>
struct TaggedInput {
    using DerivedType = T;
    using Result      = std::variant<ErrorResult, ResultType>;

    LogicalTag tag;  // NEW: Logical timestamp

    std::promise<Result> promise;
    std::future<Result> getFuture() { return promise.get_future(); }
    void setResult(Result&& result) { promise.set_value(std::move(result)); }

    // Keep existing priority/duration for scheduling hints
    constexpr uint8_t priority() const { return PRIORITY; }
    constexpr std::chrono::nanoseconds duration() const {
        return std::chrono::milliseconds(DURATION_MILLIS);
    }
};
```

**Impact:**
- All messages in the system carry explicit logical time
- Inputs can be sorted/processed by tag order, not arrival order
- Deterministic replay becomes possible

---

### Phase 2: Centralized Logical-Time Scheduler

#### Replace Per-Service Threads with Global Event Queue

Instead of N independent threads (one per service), introduce a **centralized tag-based scheduler**:

```cpp
class ReactorScheduler {
public:
    // Tag-ordered priority queue of all events across all reactors
    struct TaggedEvent {
        LogicalTag tag;
        ReactorID reactor_id;
        ReactionID reaction_id;
        InputVariant input;  // Type-erased input

        auto operator<=>(const TaggedEvent& other) const {
            return tag <=> other.tag;
        }
    };

    std::priority_queue<TaggedEvent> event_queue_;
    LogicalTag current_tag_{0, 0};

    // Main scheduling loop
    void run() {
        while (!event_queue_.empty()) {
            auto events_at_tag = dequeue_all_at_current_tag();

            // Process all events at same tag in dependency order
            auto ordered_reactions = topological_sort(events_at_tag);

            for (auto& reaction : ordered_reactions) {
                execute_reaction(reaction);
            }

            advance_logical_time();
        }
    }

private:
    std::vector<TaggedEvent> dequeue_all_at_current_tag() {
        std::vector<TaggedEvent> batch;
        while (!event_queue_.empty() && event_queue_.top().tag == current_tag_) {
            batch.push_back(event_queue_.top());
            event_queue_.pop();
        }
        return batch;
    }

    void advance_logical_time() {
        if (event_queue_.empty()) return;

        auto next_tag = event_queue_.top().tag;

        // Advance to next event time, reset microstep
        if (next_tag.time > current_tag_.time) {
            current_tag_ = {next_tag.time, 0};
        } else {
            current_tag_.microstep++;
        }
    }
};
```

**Key Changes:**
- **Single scheduler thread** replaces N service threads
- Events from all reactors go into one **tag-ordered queue**
- Scheduler processes events **batch-by-batch at each tag**
- **Microstep advancement** handles simultaneous events

---

### Phase 3: Build Static Dependency Graph

#### Compile-Time Reaction Dependencies

Currently, services declare which other services they depend on via `ContainerType`, but there's no tracking of **which reactions trigger which other reactions**.

**New Abstraction: Reaction Graph**

```cpp
// Each reactor has multiple reactions (currently implicit in FSM step functions)
template<typename ReactorType, size_t ReactionIndex>
struct Reaction {
    using Reactor = ReactorType;
    static constexpr size_t index = ReactionIndex;

    // Declare which inputs/triggers this reaction responds to
    using Triggers = TypeList<Input1, Input2, Timer3>;

    // Declare which outputs/actions this reaction produces
    using Effects = TypeList<Output1, Action2>;

    // Compile-time dependencies (which reactions must run before this one)
    using DependsOn = TypeList<OtherReaction1, OtherReaction2>;
};

// Build dependency DAG at compile-time
template<typename... Reactions>
class ReactionGraph {
    // Use template metaprogramming to compute topological order
    using TopologicalOrder = /* compute at compile time */;

    // At runtime, execute reactions in this order
    void execute_batch(const std::vector<TaggedEvent>& events) {
        for_each_in_order<TopologicalOrder>([&](auto reaction) {
            if (reaction.is_triggered_by(events)) {
                reaction.execute();
            }
        });
    }
};
```

**Mapping from Current FSM Model:**

Current model:
```cpp
size_t InitStateA::step(StoreA& s, const ContainerTypeA& c, IncrementInput& i) {
    s.counter++;
    i.setResult(BooleanResult{true});
    return index();  // Next state
}
```

Reactor model equivalent:
```cpp
struct IncrementReaction : Reaction<ServiceA, 0> {
    using Triggers = TypeList<IncrementInput>;
    using Effects = TypeList<>;  // No outputs to other reactors
    using DependsOn = TypeList<>;  // No dependencies

    StateTransition execute(StoreA& s, const ContainerTypeA& c, IncrementInput& i) {
        s.counter++;
        i.setResult(BooleanResult{true});
        return StayInState{};
    }
};
```

**Benefits:**
- **Static dependency analysis** at compile-time
- **Deterministic execution order** within each tag
- **Parallel execution** of independent reactions possible

---

### Phase 4: Port-Based Connections

#### Replace Ad-Hoc sendInput() with Explicit Ports

Current inter-service communication:
```cpp
// ServiceB sends to ServiceA (ad-hoc, runtime method call)
IncrementInput input;
c.get<ServiceA>()->sendInput(std::move(input));
```

**Reactor model with ports:**

```cpp
// Declare output ports at compile-time
template<typename ReactorType>
struct Ports {
    // Output ports
    OutputPort<IncrementInput> increment_out;
    OutputPort<TransitionInput> transition_out;

    // Input ports (currently covered by Inputs template param)
    InputPort<HeartbeatInput> heartbeat_in;
};

// Reactions set outputs on ports
struct SomeReaction {
    void execute(Store& s, Ports<ServiceB>& ports) {
        // Set value on output port (creates tagged event)
        ports.increment_out.set(IncrementInput{});
    }
};

// Connections defined at compile-time in reactor graph
struct SystemTopology {
    // ServiceB.increment_out -> ServiceA.increment_in
    using Connections = TypeList<
        Connection<ServiceB::increment_out, ServiceA::increment_in>,
        Connection<ServiceA::transition_out, ServiceB::transition_in>
    >;
};
```

**Implementation Details:**

```cpp
template<typename InputType>
class OutputPort {
    LogicalTag current_tag_;

public:
    void set(InputType&& value, ReactorScheduler& scheduler) {
        // Create tagged event at current_tag + microstep
        TaggedEvent event{
            .tag = {current_tag_.time, current_tag_.microstep + 1},
            .input = std::move(value)
        };

        scheduler.enqueue(std::move(event));
    }
};

template<typename InputType>
class InputPort {
    std::optional<InputType> value_at_current_tag_;

public:
    bool is_present() const { return value_at_current_tag_.has_value(); }
    const InputType& get() const { return *value_at_current_tag_; }
};
```

**Benefits:**
- **Static connection topology** known at compile-time
- **Automatic tag propagation** (output events get tag = current_tag + 1 microstep)
- **Port presence semantics** (reactions check if input is present at this tag)

---

### Phase 5: Deterministic Execution Semantics

#### Tag-Based Processing Loop

```cpp
void ReactorScheduler::execute_tag(const LogicalTag& tag) {
    // 1. Collect all events at this tag
    auto events = dequeue_all_events_at_tag(tag);

    // 2. Group by reactor
    auto events_by_reactor = group_by_reactor(events);

    // 3. For each reactor, set input port values
    for (auto& [reactor_id, reactor_events] : events_by_reactor) {
        reactor_id->set_input_ports(reactor_events);
    }

    // 4. Execute reactions in topological order
    auto ordered_reactions = dependency_graph_.topological_order(events);

    for (auto& reaction : ordered_reactions) {
        // Single-threaded deterministic execution
        reaction.execute();
    }

    // 5. Collect output events (tag = current + 1 microstep)
    auto output_events = collect_output_events();

    // 6. Enqueue outputs for next microstep or time
    for (auto& event : output_events) {
        event_queue_.push(event);
    }

    // 7. Clear input ports
    for (auto& reactor : all_reactors_) {
        reactor->clear_input_ports();
    }
}
```

**Key Guarantees:**

1. **Deterministic Ordering:** Given same input events with same tags → same execution order
2. **Causal Consistency:** Reactions at tag T always see outputs from reactions at tag T-1
3. **No Races:** Single-threaded execution within each tag (parallelism comes later)
4. **Reproducibility:** Record/replay by saving event trace with tags

---

### Phase 6: Optional Parallel Execution

For **independent reactions** (no dependency path between them), enable parallel execution:

```cpp
void ReactorScheduler::execute_tag_parallel(const LogicalTag& tag) {
    auto events = dequeue_all_events_at_tag(tag);
    auto ordered_reactions = dependency_graph_.topological_order(events);

    // Find independent reaction sets (levels in DAG)
    auto levels = dependency_graph_.compute_levels(ordered_reactions);

    for (auto& level : levels) {
        // All reactions in this level are independent
        std::vector<std::future<void>> futures;

        for (auto& reaction : level) {
            futures.push_back(std::async(std::launch::async, [&]() {
                reaction.execute();
            }));
        }

        // Wait for all reactions in this level to complete
        for (auto& fut : futures) {
            fut.get();
        }
    }
}
```

**Parallelism Strategy:**
- **Level-based parallelization:** Execute reactions at same DAG level in parallel
- **Barrier synchronization:** Wait for level completion before advancing
- **No data races:** Reactions at same level guaranteed independent

---

## Migration Strategy

### Phase 1: Add Logical Time (Non-Breaking)

**Goal:** Track logical time alongside physical time without changing behavior

**Changes:**
1. Add `LogicalTag` to all inputs (optional field initially)
2. Track logical time in scheduler alongside physical time
3. Log both physical and logical time for debugging

**Files to Modify:**
- [InputSet.h](mscpp/include/mscpp/InputSet.h) - Add `LogicalTag` field
- [MicroService.h](mscpp/include/mscpp/MicroService.h) - Track `current_tag_` in main loop

**New Files:**
- `LogicalTime.h` - Tag definitions and utilities

**Testing:**
- Verify logical time advances correctly
- Verify tags are assigned to inputs
- Verify backward compatibility (existing tests still pass)

---

### Phase 2: Centralize Event Queue (Breaking)

**Goal:** Replace per-service threads with centralized scheduler

**Changes:**
1. Create `ReactorScheduler` class
2. Move event queues from per-service to centralized
3. Replace service threads with worker threads controlled by scheduler

**Files to Modify:**
- [MicroService.h](mscpp/include/mscpp/MicroService.h) - Make services react to scheduler events
- [ServiceFactory.h](mscpp/include/mscpp/ServiceFactory.h) - Create scheduler instead of starting threads

**New Files:**
- `ReactorScheduler.h` - Centralized scheduler
- `ReactorFactory.h` - Factory that uses scheduler

**Testing:**
- Verify services execute correctly under scheduler
- Verify logical time advances
- Verify events processed in tag order

---

### Phase 3: Build Dependency Graph (Breaking)

**Goal:** Explicit reaction dependencies for deterministic ordering

**Changes:**
1. Refactor FSM `step()` functions into explicit `Reaction<>` types
2. Declare trigger/effect/dependency metadata
3. Compute topological order at compile-time

**Files to Modify:**
- [StateSet.h](mscpp/include/mscpp/StateSet.h) - Replace with `Reaction<>` abstraction
- [MicroService.h](mscpp/include/mscpp/MicroService.h) - Use reaction graph instead of FSM
- Example services - Convert to reaction-based model

**New Files:**
- `Reaction.h` - Reaction abstraction
- `ReactionGraph.h` - Dependency graph and topological sort

**Testing:**
- Verify reactions execute in topological order
- Verify determinism (same inputs → same order)
- Performance testing (compile-time vs runtime overhead)

---

### Phase 4: Port-Based I/O (Breaking)

**Goal:** Static connection topology with automatic tag propagation

**Changes:**
1. Replace `sendInput()` with `OutputPort::set()`
2. Add `InputPort` abstraction with presence semantics
3. Define static connection topology

**Files to Modify:**
- [InputSet.h](mscpp/include/mscpp/InputSet.h) - Add port types
- [MicroService.h](mscpp/include/mscpp/MicroService.h) - Use ports in reactions
- Example services - Use port-based I/O

**New Files:**
- `Ports.h` - Input/output port definitions
- `Topology.h` - Static connection declarations

**Testing:**
- Verify port connections work correctly
- Verify tag propagation (outputs at current_tag + 1 microstep)
- Verify presence semantics

---

### Phase 5: Enable Determinism Testing

**Goal:** Empirical verification of deterministic execution

**Changes:**
1. Add event trace recording
2. Implement replay mechanism
3. Write unit tests that verify determinism

**New Files:**
- `Trace.h` - Event recording/replay
- `DeterminismTest.cpp` - Determinism verification tests

**Testing:**
- Run same scenario multiple times
- Verify identical traces
- Fuzz testing with random inputs

---

## Code Structure Comparison

### Before (Actor Model)

```
MicroService (per-thread)
├── Mailbox (FIFO + priority)
├── Heartbeat Loop
│   ├── Execute heartbeat reaction
│   └── Process input window
│       └── Select highest priority input
│           └── Execute FSM step()
└── Physical Time Budgets
```

### After (Reactor Model)

```
ReactorScheduler (single/multi-threaded)
├── Tag-Ordered Event Queue
└── For each Tag:
    ├── Dequeue all events at tag
    ├── Set input port values
    ├── Execute reactions (topological order)
    │   ├── Level 0: Independent reactions (parallel)
    │   ├── Level 1: Dependent on Level 0 (parallel within level)
    │   └── Level N: Final reactions
    ├── Collect output port values
    └── Enqueue outputs with tag+1
```

---

## Key Design Decisions

### 1. Keep Template-Based Architecture

**Rationale:** The current compile-time type safety and zero-overhead abstractions are valuable. Extend templates to include reaction dependencies, not replace them.

**Example:**
```cpp
template<typename... Reactors>
class ReactorSystem {
    // Compute dependency graph at compile-time
    using DependencyGraph = BuildDependencyGraph<Reactors...>;
    using TopologicalOrder = ComputeTopologicalOrder<DependencyGraph>;
};
```

### 2. Preserve FSM Semantics

**Rationale:** The FSM model is a strength of mscpp. Map states to reactor modes, and `step()` functions to reactions.

**Mapping:**
- **State** → Reactor mode (changes reaction set availability)
- **step(State, Input)** → Reaction triggered by Input when in State
- **State transitions** → Mode transitions (change active reactions)

### 3. Backward Compatibility Path

**Rationale:** Allow gradual migration by supporting both actor and reactor modes.

**Strategy:**
- Add `REACTOR_MODE` compile-time flag
- When disabled, fall back to current actor implementation
- When enabled, use new reactor scheduler

```cpp
#ifdef REACTOR_MODE
    ReactorScheduler scheduler;
#else
    // Old per-service threading
#endif
```

### 4. Logical Time Units

**Rationale:** Use **nanoseconds** as base unit for logical time to match `std::chrono` and provide fine granularity.

```cpp
using LogicalTime = std::chrono::nanoseconds;

struct LogicalTag {
    LogicalTime time{0};
    uint32_t microstep{0};
};
```

---

## Testing Strategy

### 1. Determinism Verification

```cpp
TEST_CASE("Deterministic execution across runs") {
    // Run 1
    ReactorSystem system1;
    system1.inject_event({.tag = {0, 0}, .input = StartInput{}});
    auto trace1 = system1.run_and_record_trace();

    // Run 2 (identical inputs)
    ReactorSystem system2;
    system2.inject_event({.tag = {0, 0}, .input = StartInput{}});
    auto trace2 = system2.run_and_record_trace();

    // Traces must be identical
    REQUIRE(trace1 == trace2);
}
```

### 2. Causality Verification

```cpp
TEST_CASE("Output events have tag > input tag") {
    ReactorSystem system;
    system.inject_event({.tag = {100, 0}, .input = TriggerInput{}});

    auto trace = system.run_and_record_trace();

    for (const auto& event : trace.output_events) {
        REQUIRE(event.tag > LogicalTag{100, 0});
    }
}
```

### 3. Dependency Graph Validation

```cpp
TEST_CASE("Reactions execute in topological order") {
    // Reaction A depends on Reaction B
    using Graph = DependencyGraph<ReactionA, ReactionB>;

    auto order = Graph::topological_order();

    // B must come before A
    REQUIRE(index_of<ReactionB>(order) < index_of<ReactionA>(order));
}
```

---

## Summary

The **mscpp → reactor retrofit** leverages the existing strengths of the library (FSMs, type safety, structured reactions) while adding the missing determinism guarantees of the reactor model. The key transformations are:

| Aspect | Current (Actor) | Proposed (Reactor) |
|--------|----------------|-------------------|
| **Time** | Physical time only | Logical tags `(time, microstep)` |
| **Scheduling** | Per-service threads | Centralized tag-based scheduler |
| **Ordering** | FIFO + priority | Topological dependency order |
| **Communication** | Ad-hoc `sendInput()` | Static port connections |
| **Determinism** | Non-deterministic (thread races) | Deterministic (tag ordering) |
| **Concurrency** | Thread-per-service | Level-based parallelism (DAG) |
| **Topology** | Runtime service wiring | Compile-time connection graph |

This approach maintains the elegance and safety of the current design while enabling **true deterministic execution** for safety-critical or reproducible systems.

---

## References

- [CLAUDE.md](/.claude/CLAUDE.md) - Reactor model principles
- [Lingua Franca Documentation](https://www.lf-lang.org/)
- [Ptolemy Project](https://ptolemy.berkeley.edu/)
- UC Berkeley Research Papers on Deterministic Concurrent Programming

---

**Document Status:** ✅ Complete - Used to guide Phase 1 and Phase 2 implementation
