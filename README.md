# mscpp (`MicroService`s C++)

![example workflow](https://github.com/goromal/mscpp/actions/workflows/test.yml/badge.svg)

A deterministic, FSM-driven reactor framework for building reliable and testable microservices in C++. mscpp enforces architectural patterns that eliminate side effects, ensure testability, and provide clean integration with async I/O frameworks (gRPC, ROS2, etc.).

## Architecture Overview

mscpp provides a **deterministic, FSM-driven reactor framework** with strict architectural enforcement:

```
┌─────────────────────────────────────────────────────────┐
│               Store (Pure Business Logic)               │
│  • All business logic as pure functions (100% testable) │
│  • No port access, no I/O, no reactor dependencies      │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│          FSM States (Coordination Logic Only)           │
│  • Coordinate Store + Ports based on StepTrigger        │
│  • Handle state transitions                              │
│  • NO business logic - just coordination                 │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│              Reactor (Thin Enforcement Shell)           │
│  • Final methods prevent FSM bypass                      │
│  • Private Store/Ports enforce access patterns          │
│  • Single-threaded deterministic execution               │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│          I/O Adapters (Optional Async Integration)      │
│  • Thread-safe boundary for gRPC, ROS2, etc.            │
│  • Runs in separate threads outside reactor              │
│  • Clean async-to-sync bridge                            │
└─────────────────────────────────────────────────────────┘
```

### Key Principles

1. **Determinism**: Single-threaded FSM execution with tag-based logical time ordering
2. **Testability**: Business logic lives in pure Store functions (100% unit testable)
3. **Enforced Patterns**: Final reactor methods and private members prevent architectural violations
4. **Clean I/O Integration**: Thread-safe I/O adapters for async frameworks (gRPC, ROS2)

### Three-Layer Architecture

1. **Store**: Pure functions containing ALL business logic
   - 100% unit testable in isolation
   - No dependencies on ports, reactor, or I/O
   - Const methods for pure logic, minimal mutation methods

2. **FSM States**: Coordination logic that connects Store + Ports
   - Read input ports → Call Store functions → Write output ports
   - Handle state transitions based on StepTrigger context
   - NO business logic - only coordination

3. **Reactor**: Thin enforcement shell
   - **Final methods** (`doHeartbeat()`, `executeLogicalAction()`) prevent FSM bypass
   - **Private members** (`mStore`, `mPorts`, `mContainer`) enforce access patterns
   - Single-threaded deterministic execution
   - Optional `doPeriodicMaintenance()` hook for non-business-logic work

### Why These Restrictions?

**Without Enforcement (Old Pattern):**
```cpp
// BAD: Developer can bypass FSM entirely
class MyReactor : public MicroServiceFSMReactor<...> {
    void executeLogicalAction(...) override {
        // Business logic here - bypassing FSM!
        mStore.data = mPorts.input.get();
        mPorts.output.set(processData());  // Not deterministic!
    }
};
```
Problems: ❌ No FSM constraints ❌ Business logic scattered ❌ Not testable ❌ Non-deterministic

**With Enforcement (New Pattern):**
```cpp
// GOOD: FSM-driven pattern enforced
struct RunningState : public State<RunningState, 0> {
    size_t step(Store& s, Ports& p, const Container& c,
                const LogicalTag& tag, const StepTrigger& trigger) {
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
            if (p.input.is_present()) {
                auto result = s.processData(p.input.get());  // Store function!
                p.output.set(result);
            }
        }
        return RunningState::index();
    }
};
```
Benefits: ✅ FSM enforced ✅ Business logic in Store ✅ 100% testable ✅ Deterministic

## Quick Start

```cpp
// 1. Define Store with pure business logic
struct MyStore {
    int count{0};

    int increment(int amount) {  // Pure function - testable!
        count += amount;
        return count;
    }
};

// 2. Define Ports for I/O
struct MyPorts {
    InputPort<int> amount_in;
    OutputPort<int> count_out;
};

// 3. Define FSM State (coordination only)
struct RunningState : public State<RunningState, 0> {
    size_t step(MyStore& s, MyPorts& p, const Container& c,
                const LogicalTag& tag, const StepTrigger& trigger) {
        if (trigger.type == StepTrigger::Type::LOGICAL_ACTION &&
            trigger.action_name == "increment") {
            if (p.amount_in.is_present()) {
                int new_count = s.increment(p.amount_in.get());  // Call Store!
                p.count_out.set(new_count);
            }
        }
        return RunningState::index();
    }
};

// 4. Define Reactor (thin wrapper - no overrides needed!)
using MyReactor = MicroServiceFSMReactor<
    MyName, MyStore, MyPorts, Container, StateSet<RunningState>
>;
```

## Documentation

- **[Getting Started Guide](./GETTING_STARTED.md)** - Step-by-step tutorial for creating reactors
- **[Reactor Patterns](./REACTOR_PATTERNS.md)** - FSM-driven architecture patterns and best practices
- **[I/O Adapters Guide](./IO_ADAPTERS.md)** - Integrating async I/O frameworks (gRPC, ROS2)
- **[Migration Guide](./MIGRATION_GUIDE.md)** - Migrating existing code to new patterns

## Features

✅ **Deterministic Execution** - Tag-based logical time with microstep ordering
✅ **FSM-Driven Architecture** - States enforce what can happen when
✅ **Pure Business Logic** - Store functions are 100% unit testable
✅ **Compile-Time Enforcement** - Final methods and private members prevent violations
✅ **Thread-Safe I/O Adapters** - Clean integration with gRPC, ROS2, and other async frameworks
✅ **Zero-Cost Abstractions** - Minimal runtime overhead
✅ **Logical Actions** - Event-driven reactions within same logical time (zero-latency request-response)

## Design Principles

1. **Eliminate side effects**: All business logic in pure Store functions
2. **Testable by design**: FSM states coordinate; Store contains logic (testable separately)
3. **Illegal states unrepresentable**: FSM structure enforced at compile-time
4. **Deterministic time**: Tag-based logical time with deterministic event ordering
5. **Single-threaded reactor**: No locks needed (I/O adapters handle async boundary)

## Examples

See the [examples/](./examples/) directory for complete working examples:
- **[grpc_echo_example.cpp](./examples/grpc_echo_example.cpp)** - gRPC service integration
- **[ros2_echo_example.cpp](./examples/ros2_echo_example.cpp)** - ROS2 node integration

## Building

mscpp uses CMake for building. See the [examples/](./examples/) for build configurations.

## Testing

All tests use Catch2. Run tests with:
```bash
mkdir build && cd build
cmake ..
make
ctest
```

## Contributing

Contributions welcome! Please ensure:
- All business logic stays in Store pure functions
- FSM states contain only coordination logic
- Tests cover both Store functions and reactor integration
- Documentation is updated for new patterns

## License

See LICENSE file for details.
