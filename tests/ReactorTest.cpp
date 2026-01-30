#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <catch2/catch.hpp>
#pragma GCC diagnostic pop
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <vector>
#include <string>

#include "example-services/Inputs.h"
#include "example-services/ServiceA.h"
#include "example-services/ServiceB.h"
#include "mscpp/ReactorFactory.h"

TEST_CASE("Test reactor mode with centralized scheduler")
{
    // Initialize logging
    services::default_logger();

    SECTION("Basic reactor execution - ServiceA only (no cross-service blocking)")
    {
        services::ReactorFactory<ServiceA> factory;

        // Run scheduler in background thread
        std::thread runThread([&factory]() {
            factory.run();
        });

        // Give scheduler time to start and execute some events
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Stop the scheduler
        std::thread stopThread([&factory]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            factory.stop();
        });

        runThread.join();
        stopThread.join();

        // Verify service executed
        auto serviceA = factory.get<ServiceA>();

        std::cout << "ServiceA state: " << serviceA->readStore().state << std::endl;
        std::cout << "ServiceA input: " << serviceA->readStore().input << std::endl;

        // ServiceA should have executed heartbeats and updated its state
        REQUIRE(serviceA->readStore().state == "init");
        REQUIRE(serviceA->readStore().input == "heartbeat");
    }

    SECTION("Deterministic execution order")
    {
        // Run the same scenario multiple times and verify identical results
        // Using only ServiceA to avoid cross-service dependencies
        struct ExecutionTrace
        {
            unsigned int counterA;
            std::string stateA;
        };

        std::vector<ExecutionTrace> traces;

        for (int run = 0; run < 3; run++)
        {
            services::ReactorFactory<ServiceA> factory;

            // Run for a fixed logical time
            std::thread stopThread([&factory]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                factory.stop();
            });

            factory.run();
            stopThread.join();

            auto serviceA = factory.get<ServiceA>();

            ExecutionTrace trace{
                serviceA->readStore().counter,
                serviceA->readStore().state
            };

            traces.push_back(trace);

            std::cout << "Run " << run << ": ServiceA counter=" << trace.counterA
                      << " state=" << trace.stateA << std::endl;
        }

        // All runs should produce identical results (determinism)
        for (size_t i = 1; i < traces.size(); i++)
        {
            REQUIRE(traces[i].counterA == traces[0].counterA);
            REQUIRE(traces[i].stateA == traces[0].stateA);
        }
    }

    SECTION("Logical time progression in reactor mode")
    {
        services::ReactorFactory<ServiceA> factory;

        std::thread stopThread([&factory]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            factory.stop();
        });

        factory.run();
        stopThread.join();

        // Check that scheduler processed events
        std::cout << "Scheduler current tag: " << factory.getScheduler().getCurrentTag() << std::endl;

        // Logical time should have advanced
        auto currentTag = factory.getScheduler().getCurrentTag();
        REQUIRE(currentTag.time.count() > 0);
    }
}

TEST_CASE("Test reactor scheduler directly")
{
    SECTION("Event queue ordering")
    {
        services::ReactorScheduler scheduler;

        std::vector<int> executionOrder;

        // Schedule events out of order
        scheduler.scheduleEvent(services::LogicalTag{services::LogicalTime(300), 0}, 0,
                                [&executionOrder]() { executionOrder.push_back(3); },
                                "event 3");

        scheduler.scheduleEvent(services::LogicalTag{services::LogicalTime(100), 0}, 0,
                                [&executionOrder]() { executionOrder.push_back(1); },
                                "event 1");

        scheduler.scheduleEvent(services::LogicalTag{services::LogicalTime(200), 0}, 0,
                                [&executionOrder]() { executionOrder.push_back(2); },
                                "event 2");

        // Events at same time, different microsteps
        scheduler.scheduleEvent(services::LogicalTag{services::LogicalTime(100), 1}, 0,
                                [&executionOrder]() { executionOrder.push_back(4); },
                                "event 1.1");

        scheduler.run();

        // Verify events executed in tag order
        REQUIRE(executionOrder.size() == 4);
        REQUIRE(executionOrder[0] == 1);  // (100, 0)
        REQUIRE(executionOrder[1] == 4);  // (100, 1)
        REQUIRE(executionOrder[2] == 2);  // (200, 0)
        REQUIRE(executionOrder[3] == 3);  // (300, 0)

        std::cout << "Execution order: ";
        for (int val : executionOrder)
        {
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }
}
