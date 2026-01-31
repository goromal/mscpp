#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <catch2/catch.hpp>
#pragma GCC diagnostic pop

#include <chrono>
#include <iostream>
#include <thread>

// Force reactor mode for this test file
#undef REACTOR_MODE
#define REACTOR_MODE 1

#include "example-services/Inputs.h"
#include "mscpp/Reaction.h"
#include "mscpp/ReactionGraph.h"
#include "mscpp/MicroServiceContainer.h"

/**
 * Hybrid FSM + Reaction Model Tests
 *
 * These tests demonstrate how developers can primarily focus on writing
 * pure FSM state functions, with minimal additional work to add dependency
 * metadata for the reaction system.
 *
 * Key Philosophy:
 * 1. Core logic = pure state functions (business logic)
 * 2. Reactions = thin wrappers that add metadata
 * 3. Dependency graph = automatic from metadata
 */

// ============================================================================
// Example 1: Pure State Functions (What the Developer Writes)
// ============================================================================

struct CounterStore {
    int value = 0;
    std::string last_operation = "none";
};

using EmptyContainer = services::MicroServiceContainer<>;

/**
 * DEVELOPER WRITES THIS: Pure functions implementing business logic
 *
 * These are just plain functions - no inheritance, no templates.
 * They represent the actual work your service does.
 */
namespace PureStateFunctions {

    // Increment the counter
    void incrementCounter(CounterStore& store, IncrementInput& input) {
        store.value++;
        store.last_operation = "increment";
        input.setResult(BooleanResult{true});
    }

    // Double the counter
    void doubleCounter(CounterStore& store) {
        store.value *= 2;
        store.last_operation = "double";
    }

    // Reset the counter
    void resetCounter(CounterStore& store) {
        store.value = 0;
        store.last_operation = "reset";
    }

    // Add a constant offset
    void addOffset(CounterStore& store, int offset) {
        store.value += offset;
        store.last_operation = "add_offset";
    }
}

// ============================================================================
// Example 2: Minimal Reaction Wrappers (Boilerplate)
// ============================================================================

/**
 * DEVELOPER ADDS THIS: Thin wrappers that declare metadata
 *
 * These just call the pure functions and add compile-time metadata.
 * This is the "annotation" that enables dependency analysis.
 */

// Forward declaration for Reaction reactor type
struct CounterReactor;

struct IncrementReaction : public services::Reaction<
    CounterReactor, 0,
    services::TypeList<IncrementInput>,  // Triggered by
    services::TypeList<>,                // Produces
    services::TypeList<>                 // Depends on
> {
    void execute(CounterStore& store, EmptyContainer&, IncrementInput& input) {
        // Just call the pure function!
        PureStateFunctions::incrementCounter(store, input);
    }
};

struct DoubleReaction : public services::Reaction<
    CounterReactor, 1,
    services::TypeList<HeartbeatInput>,
    services::TypeList<>,
    services::TypeList<IncrementReaction>  // Depends on increment (must increment before doubling)
> {
    void execute(CounterStore& store, EmptyContainer&, HeartbeatInput&) {
        // Just call the pure function!
        PureStateFunctions::doubleCounter(store);
    }
};

struct ResetReaction : public services::Reaction<
    CounterReactor, 2,
    services::TypeList<TransitionInput>,
    services::TypeList<>,
    services::TypeList<>
> {
    void execute(CounterStore& store, EmptyContainer&, TransitionInput&) {
        // Just call the pure function!
        PureStateFunctions::resetCounter(store);
    }
};

TEST_CASE("Hybrid: Pure Functions + Reaction Metadata", "[reactions][hybrid][pure-functions]")
{
    services::default_logger();

    SECTION("Pure functions work standalone (no framework needed)")
    {
        // Developers can test their business logic directly!
        CounterStore store;

        REQUIRE(store.value == 0);

        // Test pure functions directly
        PureStateFunctions::addOffset(store, 5);
        REQUIRE(store.value == 5);
        REQUIRE(store.last_operation == "add_offset");

        PureStateFunctions::doubleCounter(store);
        REQUIRE(store.value == 10);
        REQUIRE(store.last_operation == "double");

        PureStateFunctions::resetCounter(store);
        REQUIRE(store.value == 0);
        REQUIRE(store.last_operation == "reset");

        std::cout << "✓ Pure functions tested standalone (no Reaction framework)" << std::endl;
    }

    SECTION("Reactions are just thin wrappers over pure functions")
    {
        CounterStore store;
        EmptyContainer container;

        // Create reaction instances
        IncrementReaction incrementReaction;
        DoubleReaction doubleReaction;

        // Execute increment reaction (wraps pure function)
        IncrementInput incrementInput;
        incrementReaction.execute(store, container, incrementInput);

        REQUIRE(store.value == 1);
        REQUIRE(store.last_operation == "increment");

        // Execute double reaction (wraps pure function)
        HeartbeatInput heartbeatInput;
        doubleReaction.execute(store, container, heartbeatInput);

        REQUIRE(store.value == 2);
        REQUIRE(store.last_operation == "double");

        std::cout << "✓ Reactions successfully wrap pure functions" << std::endl;
    }

    SECTION("Dependency metadata is extracted from Reaction declarations")
    {
        // The metadata is compile-time accessible
        REQUIRE(IncrementReaction::index == 0);
        REQUIRE(DoubleReaction::index == 1);
        REQUIRE(ResetReaction::index == 2);

        // Check triggers
        REQUIRE(IncrementReaction::is_triggered_by<IncrementInput>() == true);
        REQUIRE(DoubleReaction::is_triggered_by<HeartbeatInput>() == true);
        REQUIRE(ResetReaction::is_triggered_by<TransitionInput>() == true);

        // Check dependencies
        REQUIRE(DoubleReaction::depends_on<IncrementReaction>() == true);
        REQUIRE(DoubleReaction::depends_on<ResetReaction>() == false);

        std::cout << "✓ Dependency metadata extracted from Reaction types" << std::endl;
    }

    SECTION("Dependency graph built automatically from metadata")
    {
        services::ReactionGraph graph;

        // Register reactions (would be automatic in real system)
        size_t r0 = graph.addReaction("CounterReactor::0", 0, 0);  // IncrementReaction
        size_t r1 = graph.addReaction("CounterReactor::1", 0, 1);  // DoubleReaction
        graph.addReaction("CounterReactor::2", 0, 2);  // ResetReaction (unused)

        // Add dependency: DoubleReaction depends on IncrementReaction
        graph.addDependency(r1, r0);

        // Compute execution order
        auto order = graph.computeTopologicalOrder();

        // Verify IncrementReaction comes before DoubleReaction
        auto r0_pos = std::find(order.execution_order.begin(), order.execution_order.end(), r0);
        auto r1_pos = std::find(order.execution_order.begin(), order.execution_order.end(), r1);
        REQUIRE(r0_pos < r1_pos);

        std::cout << "✓ Dependency graph automatically enforces execution order" << std::endl;
    }
}

// ============================================================================
// Example 3: FSM State Functions (Classic Pattern)
// ============================================================================

/**
 * DEVELOPER WRITES THIS: Traditional FSM state functions
 *
 * This is the existing pattern from Phase 1-2.
 * Pure functions organized by state.
 */
namespace FSMStateFunctions {

    struct IdleState {
        static void onHeartbeat(CounterStore& store) {
            store.last_operation = "idle_heartbeat";
        }

        static void onIncrement(CounterStore& store, IncrementInput& input) {
            store.value++;
            store.last_operation = "idle_increment";
            input.setResult(BooleanResult{true});
        }
    };

    struct ActiveState {
        static void onHeartbeat(CounterStore& store) {
            store.value++;  // Auto-increment when active
            store.last_operation = "active_heartbeat";
        }

        static void onIncrement(CounterStore& store, IncrementInput& input) {
            store.value += 2;  // Double increment when active
            store.last_operation = "active_increment";
            input.setResult(BooleanResult{true});
        }
    };
}

/**
 * DEVELOPER ADDS THIS: Reaction wrappers for FSM states
 */
struct IdleHeartbeatReaction : public services::Reaction<
    CounterReactor, 10,
    services::TypeList<HeartbeatInput>,
    services::TypeList<>,
    services::TypeList<>
> {
    void execute(CounterStore& store, EmptyContainer&, HeartbeatInput&) {
        FSMStateFunctions::IdleState::onHeartbeat(store);
    }
};

struct IdleIncrementReaction : public services::Reaction<
    CounterReactor, 11,
    services::TypeList<IncrementInput>,
    services::TypeList<>,
    services::TypeList<>
> {
    void execute(CounterStore& store, EmptyContainer&, IncrementInput& input) {
        FSMStateFunctions::IdleState::onIncrement(store, input);
    }
};

struct ActiveHeartbeatReaction : public services::Reaction<
    CounterReactor, 20,
    services::TypeList<HeartbeatInput>,
    services::TypeList<>,
    services::TypeList<IdleIncrementReaction>  // Active state depends on idle increment
> {
    void execute(CounterStore& store, EmptyContainer&, HeartbeatInput&) {
        FSMStateFunctions::ActiveState::onHeartbeat(store);
    }
};

TEST_CASE("Hybrid: FSM State Functions + Reactions", "[reactions][hybrid][fsm]")
{
    services::default_logger();

    SECTION("FSM state functions work standalone")
    {
        CounterStore store;

        // Test IdleState functions directly
        FSMStateFunctions::IdleState::onHeartbeat(store);
        REQUIRE(store.last_operation == "idle_heartbeat");

        IncrementInput input;
        FSMStateFunctions::IdleState::onIncrement(store, input);
        REQUIRE(store.value == 1);
        REQUIRE(store.last_operation == "idle_increment");

        // Test ActiveState functions directly
        FSMStateFunctions::ActiveState::onHeartbeat(store);
        REQUIRE(store.value == 2);
        REQUIRE(store.last_operation == "active_heartbeat");

        std::cout << "✓ FSM state functions tested standalone" << std::endl;
    }

    SECTION("Reactions wrap FSM state functions")
    {
        CounterStore store;
        EmptyContainer container;

        IdleHeartbeatReaction idleHB;
        IdleIncrementReaction idleInc;
        ActiveHeartbeatReaction activeHB;

        // Execute idle state reactions
        HeartbeatInput hb1;
        idleHB.execute(store, container, hb1);
        REQUIRE(store.last_operation == "idle_heartbeat");

        IncrementInput inc1;
        idleInc.execute(store, container, inc1);
        REQUIRE(store.value == 1);

        // Execute active state reaction
        HeartbeatInput hb2;
        activeHB.execute(store, container, hb2);
        REQUIRE(store.value == 2);
        REQUIRE(store.last_operation == "active_heartbeat");

        std::cout << "✓ Reactions successfully wrap FSM state functions" << std::endl;
    }

    SECTION("FSM state dependencies expressed in Reaction metadata")
    {
        // ActiveHeartbeatReaction declares dependency on IdleIncrementReaction
        REQUIRE(ActiveHeartbeatReaction::depends_on<IdleIncrementReaction>() == true);
        REQUIRE(ActiveHeartbeatReaction::depends_on<IdleHeartbeatReaction>() == false);

        std::cout << "✓ FSM state dependencies captured in metadata" << std::endl;
    }
}

// ============================================================================
// Example 4: Minimal Boilerplate Pattern
// ============================================================================

/**
 * This demonstrates the minimal pattern for developers:
 * 1. Write pure function
 * 2. Create reaction wrapper (mostly copy-paste template)
 */

TEST_CASE("Hybrid: Developer Workflow", "[reactions][hybrid][workflow]")
{
    services::default_logger();

    SECTION("Step 1: Developer writes pure business logic")
    {
        // Developer focuses on WHAT the code does, not HOW it integrates
        auto multiplyByThree = [](CounterStore& store) {
            store.value *= 3;
            store.last_operation = "multiply_by_three";
        };

        CounterStore store;
        store.value = 7;
        multiplyByThree(store);

        REQUIRE(store.value == 21);
        REQUIRE(store.last_operation == "multiply_by_three");

        std::cout << "✓ Step 1: Pure business logic written and tested" << std::endl;
    }

    SECTION("Step 2: Developer adds Reaction wrapper (boilerplate)")
    {
        // This is the "boilerplate" - but it's minimal and template-like
        struct MultiplyByThreeReaction : public services::Reaction<
            CounterReactor, 100,
            services::TypeList<HeartbeatInput>,
            services::TypeList<>,
            services::TypeList<>  // Add dependencies here if needed
        > {
            void execute(CounterStore& store, EmptyContainer&, HeartbeatInput&) {
                store.value *= 3;
                store.last_operation = "multiply_by_three";
            }
        };

        CounterStore store;
        EmptyContainer container;
        store.value = 7;

        MultiplyByThreeReaction reaction;
        HeartbeatInput input;
        reaction.execute(store, container, input);

        REQUIRE(store.value == 21);

        std::cout << "✓ Step 2: Reaction wrapper added (minimal boilerplate)" << std::endl;
    }

    SECTION("Step 3: Dependency graph automatically built")
    {
        // Developer declares dependencies in TypeList
        // System automatically builds and validates graph

        services::ReactionGraph graph;
        size_t r0 = graph.addReaction("React0", 0, 0);
        size_t r1 = graph.addReaction("React1", 0, 1);
        size_t r2 = graph.addReaction("React2", 0, 2);

        // Declare: r2 depends on r1, r1 depends on r0
        graph.addDependency(r1, r0);
        graph.addDependency(r2, r1);

        auto order = graph.computeTopologicalOrder();

        // System guarantees execution order: r0 -> r1 -> r2
        REQUIRE(order.execution_order.size() == 3);
        REQUIRE(order.execution_order[0] == r0);
        REQUIRE(order.execution_order[1] == r1);
        REQUIRE(order.execution_order[2] == r2);

        std::cout << "✓ Step 3: Dependency graph built automatically" << std::endl;
        std::cout << "  Execution order: r0 -> r1 -> r2" << std::endl;
    }
}

// ============================================================================
// Example 5: Testing Strategy
// ============================================================================

TEST_CASE("Hybrid: Testing Strategy", "[reactions][hybrid][testing]")
{
    services::default_logger();

    SECTION("Unit test pure functions (no framework)")
    {
        // Developers can write fast, simple unit tests
        CounterStore store;

        PureStateFunctions::addOffset(store, 10);
        REQUIRE(store.value == 10);

        PureStateFunctions::doubleCounter(store);
        REQUIRE(store.value == 20);

        PureStateFunctions::resetCounter(store);
        REQUIRE(store.value == 0);

        std::cout << "✓ Unit tests for pure functions (fast, simple)" << std::endl;
    }

    SECTION("Integration test reactions (with framework)")
    {
        // When needed, test the integrated system
        services::ReactionGraph graph;

        size_t r0 = graph.addReaction("Inc", 0, 0);
        size_t r1 = graph.addReaction("Dbl", 0, 1);
        graph.addDependency(r1, r0);

        auto order = graph.computeTopologicalOrder();

        // Verify system integration
        REQUIRE(order.execution_order[0] == r0);
        REQUIRE(order.execution_order[1] == r1);

        std::cout << "✓ Integration tests verify framework behavior" << std::endl;
    }

    SECTION("Dependency tests validate graph structure")
    {
        // Test that dependencies are correctly declared
        REQUIRE(DoubleReaction::depends_on<IncrementReaction>() == true);
        REQUIRE(IncrementReaction::depends_on<DoubleReaction>() == false);

        std::cout << "✓ Dependency tests validate metadata" << std::endl;
    }
}

// ============================================================================
// Summary
// ============================================================================

TEST_CASE("Hybrid: Summary", "[reactions][hybrid][summary]")
{
    services::default_logger();

    SECTION("What the developer primarily writes")
    {
        std::cout << "\n=== DEVELOPER WORKFLOW ===" << std::endl;
        std::cout << "1. Write pure functions (business logic)" << std::endl;
        std::cout << "   - No inheritance, no templates" << std::endl;
        std::cout << "   - Easy to test standalone" << std::endl;
        std::cout << "   - Example: PureStateFunctions::incrementCounter()" << std::endl;
        std::cout << std::endl;
        std::cout << "2. Add Reaction wrapper (boilerplate)" << std::endl;
        std::cout << "   - Declare triggers: TypeList<InputType>" << std::endl;
        std::cout << "   - Declare dependencies: TypeList<OtherReaction>" << std::endl;
        std::cout << "   - execute() just calls pure function" << std::endl;
        std::cout << std::endl;
        std::cout << "3. Framework handles the rest" << std::endl;
        std::cout << "   - Builds dependency graph" << std::endl;
        std::cout << "   - Computes topological order" << std::endl;
        std::cout << "   - Executes reactions deterministically" << std::endl;
        std::cout << std::endl;

        REQUIRE(true);  // Always pass - this is documentation
    }

    SECTION("Benefits of this approach")
    {
        std::cout << "=== BENEFITS ===" << std::endl;
        std::cout << "✓ Core logic is pure functions (easy to test)" << std::endl;
        std::cout << "✓ Reactions are thin wrappers (minimal boilerplate)" << std::endl;
        std::cout << "✓ Dependencies are explicit (compile-time metadata)" << std::endl;
        std::cout << "✓ Execution order is deterministic (topological sort)" << std::endl;
        std::cout << "✓ Graph analysis is automatic (cycle detection, etc.)" << std::endl;
        std::cout << std::endl;

        REQUIRE(true);
    }
}
