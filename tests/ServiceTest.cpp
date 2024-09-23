#include <boost/test/unit_test.hpp>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include "example-services/Inputs.h"
#include "example-services/ServiceA.h"
#include "example-services/ServiceB.h"
#include "mscpp/ServiceFactory.h"

BOOST_AUTO_TEST_SUITE(TestServices)

BOOST_AUTO_TEST_CASE(TestFactory)
{
    services::ServiceFactory<ServiceA, ServiceB> factory;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    factory.stop();
}

BOOST_AUTO_TEST_SUITE_END()
