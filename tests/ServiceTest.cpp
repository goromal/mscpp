#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#pragma GCC diagnostic pop
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include "example-services/Inputs.h"
#include "example-services/ServiceA.h"
#include "example-services/ServiceB.h"
#include "mscpp/ServiceFactory.h"

#if !REACTOR_MODE
// ServiceFactory tests only run in actor mode (REACTOR_MODE=0)
// For reactor mode tests, see ReactorTest.cpp

TEST_CASE("Test service factory")
{
    services::ServiceFactory<ServiceA, ServiceB> factory;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    factory.stop();
    REQUIRE(factory.get<ServiceA>()->readStore().counter == 2);
    REQUIRE(factory.get<ServiceB>()->readStore().counter == 10);
    REQUIRE(factory.get<ServiceA>()->readStore().state == "stopped");
    REQUIRE(factory.get<ServiceB>()->readStore().state == "init");
}

#if ENABLE_LOGICAL_TIME
TEST_CASE("Test logical time tracking - Phase 1")
{
    SECTION("Services track logical time alongside physical time")
    {
        services::ServiceFactory<ServiceA, ServiceB> factory;

        // Let services run for a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Read current logical tags from both services
        auto tagA = factory.get<ServiceA>()->getCurrentTag();
        auto tagB = factory.get<ServiceB>()->getCurrentTag();

        // Both services should have advanced in logical time
        REQUIRE(tagA.time.count() > 0);
        REQUIRE(tagB.time.count() > 0);

        // Log the logical tags for debugging
        std::cout << "ServiceA logical tag: " << tagA << std::endl;
        std::cout << "ServiceB logical tag: " << tagB << std::endl;

        factory.stop();

        // After stopping, logical time should be frozen
        auto finalTagA = factory.get<ServiceA>()->getCurrentTag();
        auto finalTagB = factory.get<ServiceB>()->getCurrentTag();

        std::cout << "ServiceA final tag: " << finalTagA << std::endl;
        std::cout << "ServiceB final tag: " << finalTagB << std::endl;
    }

    SECTION("Logical time advances with heartbeats")
    {
        services::ServiceFactory<ServiceA, ServiceB> factory;

        // Get initial tags
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto tag1A = factory.get<ServiceA>()->getCurrentTag();

        // Wait for more heartbeats
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        auto tag2A = factory.get<ServiceA>()->getCurrentTag();

        // Logical time should have advanced
        REQUIRE(tag2A > tag1A);

        std::cout << "ServiceA tag progression: " << tag1A << " -> " << tag2A << std::endl;

        factory.stop();
    }

    SECTION("Input tags can be set and retrieved")
    {
        HeartbeatInput input;
        services::LogicalTag testTag{services::LogicalTime(1000000), 5};

        input.setTag(testTag);
        auto retrievedTag = input.getTag();

        REQUIRE(retrievedTag == testTag);
        REQUIRE(retrievedTag.time.count() == 1000000);
        REQUIRE(retrievedTag.microstep == 5);
    }

    SECTION("Logical tag comparison operators work correctly")
    {
        services::LogicalTag tag1{services::LogicalTime(100), 0};
        services::LogicalTag tag2{services::LogicalTime(100), 1};
        services::LogicalTag tag3{services::LogicalTime(101), 0};

        REQUIRE(tag1 < tag2);
        REQUIRE(tag2 < tag3);
        REQUIRE(tag1 < tag3);
        REQUIRE(tag1 == tag1);
        REQUIRE(tag1 != tag2);
    }

    SECTION("Logical tag advancement operations")
    {
        services::LogicalTag tag{services::LogicalTime(1000), 5};

        auto nextMicro = tag.next_microstep();
        REQUIRE(nextMicro.time == tag.time);
        REQUIRE(nextMicro.microstep == 6);

        auto advanced = tag.advance_time(services::LogicalTime(500));
        REQUIRE(advanced.time.count() == 1500);
        REQUIRE(advanced.microstep == 0);
    }
}
#endif  // ENABLE_LOGICAL_TIME
#endif  // !REACTOR_MODE
