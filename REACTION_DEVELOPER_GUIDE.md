# Phase 3 Developer Guide: Pure Functions + Reactions

## Overview

Phase 3 introduces **explicit dependency metadata** while keeping your core service logic as **pure, testable functions**. You write the business logic; the framework handles dependency ordering.

## Quick Start: The Developer Workflow

### Step 1: Write Pure Functions (Your Business Logic)

This is where you spend most of your time. Write plain C++ functions:

```cpp
// Pure business logic - no inheritance, no templates, no framework
namespace MyServiceLogic {
    void incrementCounter(Store& store, IncrementInput& input) {
        store.value++;
        store.last_operation = "increment";
        input.setResult(BooleanResult{true});
    }

    void doubleValue(Store& store) {
        store.value *= 2;
        store.last_operation = "double";
    }
}
```

**Benefits:**
- ✅ Easy to test (no framework needed)
- ✅ Easy to understand (just functions)
- ✅ Easy to reuse (no coupling)
- ✅ Fast compilation (no template metaprogramming)

### Step 2: Add Reaction Wrappers (Minimal Boilerplate)

Create thin wrappers that add dependency metadata:

```cpp
struct IncrementReaction : public services::Reaction<
    MyService,                           // Reactor name
    0,                                   // Reaction index
    services::TypeList<IncrementInput>,  // Triggers
    services::TypeList<>,                // Produces
    services::TypeList<>                 // Dependencies
> {
    void execute(Store& store, Container& container, IncrementInput& input) {
        // Just call your pure function!
        MyServiceLogic::incrementCounter(store, input);
    }
};

struct DoubleReaction : public services::Reaction<
    MyService,
    1,
    services::TypeList<HeartbeatInput>,
    services::TypeList<>,
    services::TypeList<IncrementReaction>  // Depends on increment
> {
    void execute(Store& store, Container& container, HeartbeatInput&) {
        // Just call your pure function!
        MyServiceLogic::doubleValue(store);
    }
};
```

**What you're declaring:**
- **Triggers**: Which inputs trigger this reaction
- **Dependencies**: Which reactions must run before this one
- **Effects**: Which outputs this reaction produces (future phase)

### Step 3: Framework Handles the Rest

The system automatically:
1. ✅ Builds dependency graph from metadata
2. ✅ Computes topological execution order
3. ✅ Detects cycles (compile-time error)
4. ✅ Executes reactions deterministically

## Testing Strategy

### Unit Test Pure Functions (Fast & Simple)

```cpp
TEST_CASE("Business logic") {
    Store store;

    MyServiceLogic::doubleValue(store);
    REQUIRE(store.value == 0);  // 0 * 2 = 0

    IncrementInput input;
    MyServiceLogic::incrementCounter(store, input);
    REQUIRE(store.value == 1);

    MyServiceLogic::doubleValue(store);
    REQUIRE(store.value == 2);  // 1 * 2 = 2
}
```

No framework, no setup, just test your logic.

### Integration Test Reactions (When Needed)

```cpp
TEST_CASE("Reaction execution order") {
    services::ReactionGraph graph;

    size_t inc = graph.addReaction("Increment", 0, 0);
    size_t dbl = graph.addReaction("Double", 0, 1);
    graph.addDependency(dbl, inc);  // Double depends on Increment

    auto order = graph.computeTopologicalOrder();

    REQUIRE(order.execution_order[0] == inc);  // Inc runs first
    REQUIRE(order.execution_order[1] == dbl);  // Dbl runs second
}
```

Test the framework when you need dependency guarantees.

## FSM State Functions (Classic Pattern)

You can keep your existing FSM pattern:

```cpp
namespace MyStates {
    struct IdleState {
        static void onHeartbeat(Store& store) {
            store.state = "idle";
        }

        static void onIncrement(Store& store, IncrementInput& input) {
            store.value++;
            input.setResult(BooleanResult{true});
        }
    };

    struct ActiveState {
        static void onHeartbeat(Store& store) {
            store.value++;  // Auto-increment when active
        }
    };
}
```

Then add reaction wrappers:

```cpp
struct IdleHeartbeatReaction : public services::Reaction<
    MyService, 0,
    services::TypeList<HeartbeatInput>
> {
    void execute(Store& store, Container&, HeartbeatInput&) {
        MyStates::IdleState::onHeartbeat(store);
    }
};

struct ActiveHeartbeatReaction : public services::Reaction<
    MyService, 1,
    services::TypeList<HeartbeatInput>,
    services::TypeList<>,
    services::TypeList<IdleHeartbeatReaction>  // Active depends on Idle
> {
    void execute(Store& store, Container&, HeartbeatInput&) {
        MyStates::ActiveState::onHeartbeat(store);
    }
};
```

## Example: Counter Service

### Pure Functions

```cpp
namespace CounterLogic {
    void increment(CounterStore& store, IncrementInput& input) {
        store.value++;
        input.setResult(BooleanResult{true});
    }

    void multiply(CounterStore& store, int factor) {
        store.value *= factor;
    }

    void reset(CounterStore& store) {
        store.value = 0;
    }
}
```

### Reaction Wrappers

```cpp
struct IncrementReaction : public services::Reaction<
    CounterService, 0,
    services::TypeList<IncrementInput>
> {
    void execute(CounterStore& s, Container& c, IncrementInput& i) {
        CounterLogic::increment(s, i);
    }
};

struct MultiplyReaction : public services::Reaction<
    CounterService, 1,
    services::TypeList<HeartbeatInput>,
    services::TypeList<>,
    services::TypeList<IncrementReaction>  // Must increment before multiplying
> {
    void execute(CounterStore& s, Container& c, HeartbeatInput&) {
        CounterLogic::multiply(s, 2);
    }
};

struct ResetReaction : public services::Reaction<
    CounterService, 2,
    services::TypeList<TransitionInput>
> {
    void execute(CounterStore& s, Container& c, TransitionInput&) {
        CounterLogic::reset(s);
    }
};
```

### Dependency Graph (Automatic)

```
Level 0: IncrementReaction, ResetReaction
Level 1: MultiplyReaction (depends on IncrementReaction)
```

**Execution Order:** Increment/Reset → Multiply

## What You Get

### Developer Experience
- ✅ Write pure functions (90% of your time)
- ✅ Add minimal metadata (10% of your time)
- ✅ Test functions standalone (fast, simple)
- ✅ No need to think about execution order

### System Guarantees
- ✅ Deterministic execution (same inputs → same order)
- ✅ Dependencies enforced (can't run out of order)
- ✅ Cycles detected (compile-time or runtime error)
- ✅ Parallel execution ready (future phase)

### Code Quality
- ✅ Pure functions (easy to test)
- ✅ Explicit dependencies (no hidden coupling)
- ✅ Compile-time metadata (type-safe)
- ✅ Self-documenting (dependencies in type)

## Common Patterns

### Pattern 1: Pure Function + Simple Wrapper

```cpp
// Pure function (business logic)
void doWork(Store& store) {
    store.result = store.value * 2;
}

// Reaction wrapper (metadata)
struct WorkReaction : public Reaction<
    MyService, 0,
    TypeList<HeartbeatInput>
> {
    void execute(Store& s, Container&, HeartbeatInput&) {
        doWork(s);  // Just call it!
    }
};
```

### Pattern 2: FSM State + Wrapper

```cpp
// FSM state function
struct IdleState {
    static void process(Store& store) {
        store.state = "idle";
    }
};

// Reaction wrapper
struct IdleReaction : public Reaction<
    MyService, 0,
    TypeList<HeartbeatInput>
> {
    void execute(Store& s, Container&, HeartbeatInput&) {
        IdleState::process(s);
    }
};
```

### Pattern 3: Dependent Reactions

```cpp
// Reaction A (no dependencies)
struct ReactionA : public Reaction<
    MyService, 0,
    TypeList<InputA>,
    TypeList<>,
    TypeList<>  // No dependencies
> { /* ... */ };

// Reaction B (depends on A)
struct ReactionB : public Reaction<
    MyService, 1,
    TypeList<InputB>,
    TypeList<>,
    TypeList<ReactionA>  // Must run after A
> { /* ... */ };

// Result: A always runs before B
```

## Migration Path

### From Phase 1-2 (FSM Model)

**Before:**
```cpp
struct InitState : public State<InitState, 0> {
    size_t step(Store& s, Container& c, Input& i) {
        s.value++;
        return index();
    }
};
```

**After (Option 1): Extract pure function**
```cpp
// Pure function
void incrementValue(Store& s, Input& i) {
    s.value++;
}

// Reaction wrapper
struct IncrementReaction : public Reaction<
    MyService, 0,
    TypeList<Input>
> {
    void execute(Store& s, Container& c, Input& i) {
        incrementValue(s, i);
    }
};
```

**After (Option 2): Wrap existing state**
```cpp
// Keep existing state
struct InitState {
    static void process(Store& s, Input& i) {
        s.value++;
    }
};

// Add reaction wrapper
struct InitReaction : public StateReaction<
    InitState, MyService, 0,
    TypeList<Input>
> {
    void execute(Store& s, Container& c, Input& i) {
        InitState::process(s, i);
    }
};
```

## Summary

**What you write:**
- Pure functions (business logic)
- Thin reaction wrappers (metadata)

**What the framework does:**
- Builds dependency graph
- Computes execution order
- Enforces determinism
- Enables parallelism (future)

**Result:** Clean, testable code with automatic dependency management.

---

## See Also

- [ReactionHybridTest.cpp](tests/ReactionHybridTest.cpp) - Comprehensive examples
- [REACTION_SYSTEM_SUMMARY.md](REACTION_SYSTEM_SUMMARY.md) - Technical details
- [ServiceAReactions.h](tests/example-services/ServiceAReactions.h) - Full example

## Running Tests

```bash
cd /data/andrew/dev/reactors/sources/mscpp
cpp-helper make reaction-hybrid-tests
cd build
./reaction-hybrid-tests
```

Expected output:
```
===============================================================================
All tests passed (47 assertions in 5 test cases)
```
