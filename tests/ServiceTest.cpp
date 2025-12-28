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
