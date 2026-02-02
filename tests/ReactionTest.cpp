#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#pragma GCC diagnostic pop

#include <iostream>
#include <vector>

#include "mscpp/Reaction.h"
#include "mscpp/ReactionGraph.h"

TEST_CASE("Dependency Graph Construction", "[reactions][graph]")
{
    services::default_logger();

    SECTION("Build dependency graph from reactions")
    {
        services::ReactionGraph graph;

        // Manually add reactions to graph
        size_t r0 = graph.addReaction("ServiceA::0", 0, 0);
        size_t r1 = graph.addReaction("ServiceA::1", 0, 1);
        size_t r2 = graph.addReaction("ServiceA::2", 0, 2);

        REQUIRE(r0 == 0);
        REQUIRE(r1 == 1);
        REQUIRE(r2 == 2);
        REQUIRE(graph.size() == 3);

        // Add dependencies: r1 depends on r0
        graph.addDependency(1, 0);

        // Compute topological order
        auto topOrder = graph.computeTopologicalOrder();

        REQUIRE(topOrder.execution_order.size() == 3);

        // r0 should come before r1
        auto r0_pos = std::find(topOrder.execution_order.begin(), topOrder.execution_order.end(), 0);
        auto r1_pos = std::find(topOrder.execution_order.begin(), topOrder.execution_order.end(), 1);
        REQUIRE(r0_pos < r1_pos);
    }

    SECTION("Detect cycles in dependency graph")
    {
        services::ReactionGraph graph;

        graph.addReaction("A::0", 0, 0);
        graph.addReaction("A::1", 0, 1);
        graph.addReaction("A::2", 0, 2);

        // Create cycle: r0 -> r1 -> r2 -> r0
        graph.addDependency(0, 1);
        graph.addDependency(1, 2);
        graph.addDependency(2, 0);  // Cycle!

        // Should throw when computing topological order
        REQUIRE_THROWS_AS(graph.computeTopologicalOrder(), std::runtime_error);
    }

    SECTION("Complex dependency graph with multiple levels")
    {
        services::ReactionGraph graph;

        // Level 0: r0, r1 (no dependencies)
        graph.addReaction("A::0", 0, 0);
        graph.addReaction("A::1", 0, 1);

        // Level 1: r2 depends on r0
        graph.addReaction("A::2", 0, 2);
        graph.addDependency(2, 0);

        // Level 2: r3 depends on r2
        graph.addReaction("A::3", 0, 3);
        graph.addDependency(3, 2);

        // Level 1: r4 depends on r1
        graph.addReaction("A::4", 0, 4);
        graph.addDependency(4, 1);

        auto topOrder = graph.computeTopologicalOrder();

        REQUIRE(topOrder.execution_order.size() == 5);
        REQUIRE(topOrder.levels.size() == 3);

        // Level 0 should have r0 and r1
        REQUIRE(topOrder.levels[0].size() == 2);

        // Level 1 should have r2 and r4
        REQUIRE(topOrder.levels[1].size() == 2);

        // Level 2 should have r3
        REQUIRE(topOrder.levels[2].size() == 1);
    }
}

TEST_CASE("Reaction Executor", "[reactions][executor]")
{
    services::default_logger();

    SECTION("Execute reactions in topological order")
    {
        services::ReactionGraph graph;

        size_t r0 = graph.addReaction("R0", 0, 0);
        size_t r1 = graph.addReaction("R1", 0, 1);
        size_t r2 = graph.addReaction("R2", 0, 2);

        // r1 depends on r0, r2 depends on r1
        graph.addDependency(1, 0);
        graph.addDependency(2, 1);

        services::ReactionExecutor executor(graph);

        std::vector<int> executionOrder;

        executor.registerReaction(r0, [&]() { executionOrder.push_back(0); });
        executor.registerReaction(r1, [&]() { executionOrder.push_back(1); });
        executor.registerReaction(r2, [&]() { executionOrder.push_back(2); });

        executor.executeAll();

        // Should execute in order: 0, 1, 2
        REQUIRE(executionOrder.size() == 3);
        REQUIRE(executionOrder[0] == 0);
        REQUIRE(executionOrder[1] == 1);
        REQUIRE(executionOrder[2] == 2);
    }

    SECTION("Execute reactions level-by-level")
    {
        services::ReactionGraph graph;

        size_t r0 = graph.addReaction("R0", 0, 0);
        size_t r1 = graph.addReaction("R1", 0, 1);
        size_t r2 = graph.addReaction("R2", 0, 2);
        size_t r3 = graph.addReaction("R3", 0, 3);

        // Level 0: r0, r1
        // Level 1: r2 (depends on r0), r3 (depends on r1)
        graph.addDependency(2, 0);
        graph.addDependency(3, 1);

        services::ReactionExecutor executor(graph);

        std::vector<int> executionOrder;

        executor.registerReaction(r0, [&]() { executionOrder.push_back(0); });
        executor.registerReaction(r1, [&]() { executionOrder.push_back(1); });
        executor.registerReaction(r2, [&]() { executionOrder.push_back(2); });
        executor.registerReaction(r3, [&]() { executionOrder.push_back(3); });

        executor.executeByLevels();

        // Should execute 0,1 before 2,3
        REQUIRE(executionOrder.size() == 4);

        // 0 and 1 should come before 2 and 3
        auto pos_0 = std::find(executionOrder.begin(), executionOrder.end(), 0);
        auto pos_1 = std::find(executionOrder.begin(), executionOrder.end(), 1);
        auto pos_2 = std::find(executionOrder.begin(), executionOrder.end(), 2);
        auto pos_3 = std::find(executionOrder.begin(), executionOrder.end(), 3);

        REQUIRE(pos_0 < pos_2);
        REQUIRE(pos_1 < pos_3);
    }
}

TEST_CASE("Type List Utilities", "[reactions][typelist]")
{
    SECTION("TypeList size")
    {
        using List1 = services::TypeList<int, double, char>;
        REQUIRE(List1::size == 3);

        using List2 = services::TypeList<>;
        REQUIRE(List2::size == 0);
    }

    SECTION("Contains trait")
    {
        using List = services::TypeList<int, double, char>;

        REQUIRE(services::Contains<int, List>::value == true);
        REQUIRE(services::Contains<double, List>::value == true);
        REQUIRE(services::Contains<char, List>::value == true);
        REQUIRE(services::Contains<float, List>::value == false);
    }

    SECTION("Concat trait")
    {
        using List1 = services::TypeList<int, double>;
        using List2 = services::TypeList<char, float>;
        using Combined = services::Concat_t<List1, List2>;

        REQUIRE(Combined::size == 4);
        REQUIRE(services::Contains<int, Combined>::value == true);
        REQUIRE(services::Contains<double, Combined>::value == true);
        REQUIRE(services::Contains<char, Combined>::value == true);
        REQUIRE(services::Contains<float, Combined>::value == true);
    }
}
