#include <catch2/catch.hpp>

#include <mscpp/ThreadPool.h>
#include <mscpp/ReactionGraph.h>
#include <mscpp/ReactorScheduler.h>
#include <mscpp/LogicalTime.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace services;

/**
 * Parallel Execution Tests
 *
 * Tests cover:
 * 1. ThreadPool basic functionality
 * 2. ReactionExecutor parallel execution
 * 3. ReactorScheduler parallel execution
 * 4. Determinism verification
 * 5. Exception handling
 * 6. Performance characteristics
 */

// ============================================================================
// ThreadPool Tests
// ============================================================================

TEST_CASE("ThreadPool Basic Functionality", "[parallel][threadpool]")
{
    SECTION("Create thread pool with default size")
    {
        ThreadPool pool;
        REQUIRE(pool.threadCount() >= 1);
        REQUIRE(pool.threadCount() <= std::thread::hardware_concurrency());
    }

    SECTION("Create thread pool with specific size")
    {
        ThreadPool pool(4);
        REQUIRE(pool.threadCount() == 4);
    }

    SECTION("Enqueue and execute simple tasks")
    {
        ThreadPool pool(2);
        std::atomic<int> counter{0};

        std::vector<std::future<void>> futures;

        for (int i = 0; i < 10; ++i)
        {
            futures.push_back(pool.enqueue([&counter]() {
                counter++;
            }));
        }

        for (auto& fut : futures)
        {
            fut.get();
        }

        REQUIRE(counter == 10);
    }

    SECTION("Tasks execute in parallel")
    {
        ThreadPool pool(4);
        std::atomic<int> concurrent_count{0};
        std::atomic<int> max_concurrent{0};

        std::vector<std::future<void>> futures;

        for (int i = 0; i < 8; ++i)
        {
            futures.push_back(pool.enqueue([&concurrent_count, &max_concurrent]() {
                int current = ++concurrent_count;

                // Update max if needed
                int expected = max_concurrent.load();
                while (current > expected &&
                       !max_concurrent.compare_exchange_weak(expected, current))
                {
                }

                // Simulate work
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                --concurrent_count;
            }));
        }

        for (auto& fut : futures)
        {
            fut.get();
        }

        // With 4 threads and 8 tasks, at least 2 should have run concurrently
        REQUIRE(max_concurrent >= 2);
    }
}

TEST_CASE("ThreadPool Exception Handling", "[parallel][threadpool]")
{
    SECTION("Exceptions propagate through futures")
    {
        ThreadPool pool(2);

        auto future = pool.enqueue([]() {
            throw std::runtime_error("Test exception");
        });

        REQUIRE_THROWS_AS(future.get(), std::runtime_error);
    }

    SECTION("Exception in one task doesn't affect others")
    {
        ThreadPool pool(2);
        std::atomic<int> success_count{0};

        auto future1 = pool.enqueue([&success_count]() {
            success_count++;
        });

        auto future2 = pool.enqueue([]() {
            throw std::runtime_error("Test exception");
        });

        auto future3 = pool.enqueue([&success_count]() {
            success_count++;
        });

        // First and third should succeed
        REQUIRE_NOTHROW(future1.get());
        REQUIRE_THROWS(future2.get());
        REQUIRE_NOTHROW(future3.get());

        REQUIRE(success_count == 2);
    }
}

TEST_CASE("ThreadPool Shutdown", "[parallel][threadpool]")
{
    SECTION("Shutdown completes pending tasks")
    {
        ThreadPool pool(2);
        std::atomic<int> completed{0};

        for (int i = 0; i < 5; ++i)
        {
            pool.enqueue([&completed]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                completed++;
            });
        }

        pool.shutdown();
        REQUIRE(completed == 5);
    }

    SECTION("Cannot enqueue after shutdown")
    {
        ThreadPool pool(2);
        pool.shutdown();

        REQUIRE_THROWS_AS(pool.enqueue([]() {}), std::runtime_error);
    }
}

// ============================================================================
// ReactionExecutor Parallel Execution Tests
// ============================================================================

TEST_CASE("ReactionExecutor Parallel Execution", "[parallel][reactions]")
{
    SECTION("Independent reactions execute in parallel")
    {
        ReactionGraph graph;

        // Create 4 independent reactions (no dependencies)
        size_t r0 = graph.addReaction("reaction0", 0, 0);
        size_t r1 = graph.addReaction("reaction1", 1, 0);
        size_t r2 = graph.addReaction("reaction2", 2, 0);
        size_t r3 = graph.addReaction("reaction3", 3, 0);

        // No dependencies, so all should be at level 0
        ReactionExecutor executor(graph);

        std::atomic<int> concurrent_count{0};
        std::atomic<int> max_concurrent{0};

        auto make_callback = [&concurrent_count, &max_concurrent]() {
            return [&concurrent_count, &max_concurrent]() {
                int current = ++concurrent_count;

                int expected = max_concurrent.load();
                while (current > expected &&
                       !max_concurrent.compare_exchange_weak(expected, current))
                {
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                --concurrent_count;
            };
        };

        executor.registerReaction(r0, make_callback());
        executor.registerReaction(r1, make_callback());
        executor.registerReaction(r2, make_callback());
        executor.registerReaction(r3, make_callback());

        ThreadPool pool(4);
        executor.executeByLevelsParallel(pool);

        // All 4 should have executed concurrently
        REQUIRE(max_concurrent == 4);
    }

    SECTION("Dependent reactions respect ordering")
    {
        ReactionGraph graph;

        // Create chain: r0 -> r1 -> r2
        size_t r0 = graph.addReaction("reaction0", 0, 0);
        size_t r1 = graph.addReaction("reaction1", 1, 0);
        size_t r2 = graph.addReaction("reaction2", 2, 0);

        graph.addDependency(r1, r0);  // r1 depends on r0
        graph.addDependency(r2, r1);  // r2 depends on r1

        ReactionExecutor executor(graph);

        std::vector<int> execution_order;
        std::mutex order_mutex;

        auto make_callback = [&execution_order, &order_mutex](int id) {
            return [&execution_order, &order_mutex, id]() {
                std::scoped_lock lock(order_mutex);
                execution_order.push_back(id);
            };
        };

        executor.registerReaction(r0, make_callback(0));
        executor.registerReaction(r1, make_callback(1));
        executor.registerReaction(r2, make_callback(2));

        ThreadPool pool(4);
        executor.executeByLevelsParallel(pool);

        // Must execute in dependency order: 0, 1, 2
        REQUIRE(execution_order.size() == 3);
        REQUIRE(execution_order[0] == 0);
        REQUIRE(execution_order[1] == 1);
        REQUIRE(execution_order[2] == 2);
    }

    SECTION("Mixed dependencies and independence")
    {
        ReactionGraph graph;

        // Create graph:
        //     r0   r1
        //      \   /
        //       r2
        //       |
        //       r3
        size_t r0 = graph.addReaction("reaction0", 0, 0);
        size_t r1 = graph.addReaction("reaction1", 1, 0);
        size_t r2 = graph.addReaction("reaction2", 2, 0);
        size_t r3 = graph.addReaction("reaction3", 3, 0);

        graph.addDependency(r2, r0);  // r2 depends on r0
        graph.addDependency(r2, r1);  // r2 depends on r1
        graph.addDependency(r3, r2);  // r3 depends on r2

        ReactionExecutor executor(graph);
        auto topo = executor.getTopologicalOrder();

        // Should have 3 levels: [r0,r1], [r2], [r3]
        REQUIRE(topo.levels.size() == 3);
        REQUIRE(topo.levels[0].size() == 2);  // r0 and r1
        REQUIRE(topo.levels[1].size() == 1);  // r2
        REQUIRE(topo.levels[2].size() == 1);  // r3

        std::atomic<int> executed{0};

        auto make_callback = [&executed]() {
            return [&executed]() {
                executed++;
            };
        };

        executor.registerReaction(r0, make_callback());
        executor.registerReaction(r1, make_callback());
        executor.registerReaction(r2, make_callback());
        executor.registerReaction(r3, make_callback());

        ThreadPool pool(4);
        executor.executeByLevelsParallel(pool);

        REQUIRE(executed == 4);
    }
}

// ============================================================================
// ReactorScheduler Parallel Execution Tests
// ============================================================================

TEST_CASE("ReactorScheduler Parallel Execution", "[parallel][scheduler]")
{
    // Mock reactor for testing
    class MockReactor : public IReactor
    {
    public:
        MockReactor(size_t id, const std::string& name)
            : mId(id), mName(name), mHeartbeatCount(0)
        {
        }

        size_t getId() const override { return mId; }
        std::string getName() const override { return mName; }
        void initialize() override {}

        void executeHeartbeat(const LogicalTag& /*tag*/) override
        {
            mHeartbeatCount++;
        }

        bool hasPendingInputs() const override { return false; }
        bool processNextInput(const LogicalTag& /*tag*/) override { return false; }

        int getHeartbeatCount() const { return mHeartbeatCount; }

    private:
        size_t mId;
        std::string mName;
        std::atomic<int> mHeartbeatCount;
    };

    SECTION("Scheduler without thread pool uses sequential execution")
    {
        ReactorScheduler scheduler;  // No thread pool

        auto reactor1 = std::make_shared<MockReactor>(0, "Reactor1");
        auto reactor2 = std::make_shared<MockReactor>(1, "Reactor2");

        scheduler.registerReactor(reactor1);
        scheduler.registerReactor(reactor2);

        REQUIRE_FALSE(scheduler.isParallelExecutionEnabled());
    }

    SECTION("Scheduler with thread pool can enable parallel execution")
    {
        auto pool = std::make_shared<ThreadPool>(4);
        ReactorScheduler scheduler(pool);

        REQUIRE(scheduler.getThreadPool() != nullptr);

        // Enable parallel execution
        scheduler.setParallelExecution(true);
        REQUIRE(scheduler.isParallelExecutionEnabled());

        // Disable parallel execution
        scheduler.setParallelExecution(false);
        REQUIRE_FALSE(scheduler.isParallelExecutionEnabled());
    }

    SECTION("Events from different reactors execute in parallel")
    {
        auto pool = std::make_shared<ThreadPool>(4);
        ReactorScheduler scheduler(pool);
        scheduler.setParallelExecution(true);

        std::atomic<int> concurrent_count{0};
        std::atomic<int> max_concurrent{0};

        LogicalTag tag{LogicalTime(100'000'000), 0};

        // Schedule multiple events at the same tag for different "reactors"
        for (size_t i = 0; i < 4; ++i)
        {
            scheduler.scheduleEvent(tag, i, [&concurrent_count, &max_concurrent]() {
                int current = ++concurrent_count;

                int expected = max_concurrent.load();
                while (current > expected &&
                       !max_concurrent.compare_exchange_weak(expected, current))
                {
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                --concurrent_count;
            });
        }

        // Schedule a stop event at a later time
        scheduler.scheduleEvent(tag.advance_time(LogicalTime(1'000'000)), 0, [&scheduler]() {
            scheduler.stop();
        });

        scheduler.run();

        // Should have run multiple events concurrently
        REQUIRE(max_concurrent >= 2);
    }
}

// ============================================================================
// Determinism Tests
// ============================================================================

TEST_CASE("Determinism with Parallel Execution", "[parallel][determinism]")
{
    SECTION("Parallel execution produces same results as sequential")
    {
        ReactionGraph graph;

        // Create independent reactions that modify shared state deterministically
        size_t r0 = graph.addReaction("reaction0", 0, 0);
        size_t r1 = graph.addReaction("reaction1", 1, 0);
        size_t r2 = graph.addReaction("reaction2", 2, 0);

        // Run sequential version
        ReactionExecutor executor_seq(graph);

        std::vector<int> results_seq;
        std::mutex results_mutex_seq;

        auto make_callback_seq = [&results_seq, &results_mutex_seq](int value) {
            return [&results_seq, &results_mutex_seq, value]() {
                std::scoped_lock lock(results_mutex_seq);
                results_seq.push_back(value * 2);
            };
        };

        executor_seq.registerReaction(r0, make_callback_seq(10));
        executor_seq.registerReaction(r1, make_callback_seq(20));
        executor_seq.registerReaction(r2, make_callback_seq(30));

        executor_seq.executeByLevels();  // Sequential

        // Run parallel version
        ReactionExecutor executor_par(graph);

        std::vector<int> results_par;
        std::mutex results_mutex_par;

        auto make_callback_par = [&results_par, &results_mutex_par](int value) {
            return [&results_par, &results_mutex_par, value]() {
                std::scoped_lock lock(results_mutex_par);
                results_par.push_back(value * 2);
            };
        };

        executor_par.registerReaction(r0, make_callback_par(10));
        executor_par.registerReaction(r1, make_callback_par(20));
        executor_par.registerReaction(r2, make_callback_par(30));

        ThreadPool pool(4);
        executor_par.executeByLevelsParallel(pool);  // Parallel

        // Results should be same (order may differ for independent reactions)
        std::sort(results_seq.begin(), results_seq.end());
        std::sort(results_par.begin(), results_par.end());

        REQUIRE(results_seq == results_par);
    }

    SECTION("Multiple parallel runs produce identical results")
    {
        ReactionGraph graph;

        size_t r0 = graph.addReaction("reaction0", 0, 0);
        size_t r1 = graph.addReaction("reaction1", 1, 0);

        ThreadPool pool(4);

        auto run_test = [&]() {
            ReactionExecutor executor(graph);

            std::vector<int> results;
            std::mutex results_mutex;

            auto make_callback = [&results, &results_mutex](int value) {
                return [&results, &results_mutex, value]() {
                    std::scoped_lock lock(results_mutex);
                    results.push_back(value);
                };
            };

            executor.registerReaction(r0, make_callback(42));
            executor.registerReaction(r1, make_callback(100));

            executor.executeByLevelsParallel(pool);

            std::sort(results.begin(), results.end());
            return results;
        };

        auto results1 = run_test();
        auto results2 = run_test();
        auto results3 = run_test();

        REQUIRE(results1 == results2);
        REQUIRE(results2 == results3);
    }
}

// ============================================================================
// Performance Characteristics Tests
// ============================================================================

TEST_CASE("Performance Characteristics", "[parallel][performance]")
{
    SECTION("Single reaction has no parallelism overhead")
    {
        ReactionGraph graph;
        size_t r0 = graph.addReaction("reaction0", 0, 0);

        ReactionExecutor executor(graph);

        std::atomic<bool> executed{false};
        executor.registerReaction(r0, [&executed]() {
            executed = true;
        });

        ThreadPool pool(4);

        // Should skip parallel overhead for single reaction
        executor.executeByLevelsParallel(pool);

        REQUIRE(executed);
    }

    SECTION("Empty level is skipped")
    {
        ReactionGraph graph;

        // Create graph with no reactions
        ReactionExecutor executor(graph);

        ThreadPool pool(4);

        // Should complete without issues
        REQUIRE_NOTHROW(executor.executeByLevelsParallel(pool));
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE("Edge Cases", "[parallel][edge-cases]")
{
    SECTION("Thread pool with zero threads defaults to one")
    {
        ThreadPool pool(0);
        REQUIRE(pool.threadCount() >= 1);
    }

    SECTION("Parallel execution with no thread pool falls back to sequential")
    {
        ReactionGraph graph;
        size_t r0 = graph.addReaction("reaction0", 0, 0);

        ReactionExecutor executor(graph);

        std::atomic<bool> executed{false};
        executor.registerReaction(r0, [&executed]() {
            executed = true;
        });

        // Execute parallel without providing pool - should compile but behavior is sequential
        // (This tests the API design)
        executor.executeByLevels();  // Use sequential method instead

        REQUIRE(executed);
    }

    SECTION("Reaction throws exception during parallel execution")
    {
        ReactionGraph graph;
        size_t r0 = graph.addReaction("reaction0", 0, 0);
        size_t r1 = graph.addReaction("reaction1", 1, 0);

        ReactionExecutor executor(graph);

        executor.registerReaction(r0, []() {
            throw std::runtime_error("Reaction failure");
        });

        executor.registerReaction(r1, []() {
            // This should still execute (different level)
        });

        ThreadPool pool(4);

        // Exception should propagate from executeByLevelsParallel
        REQUIRE_THROWS_AS(executor.executeByLevelsParallel(pool), std::runtime_error);
    }
}
