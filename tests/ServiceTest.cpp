#include <boost/test/unit_test.hpp>
#include <iostream>
#include "mscpp/MicroService.h"
#include "mscpp/MicroServiceContainer.h"
#include "mscpp/ServiceFactory.h"

class ServiceA : public MicroService
{
public:
    using Container = MicroServiceContainer<>;

    ServiceA(const Container& container) : mContainer{container} {}
    ~ServiceA() {}

    std::string name() const override
    {
        return "ServiceA";
    }

    int getSecretNumber()
    {
        return mSecretNum;
    }

private:
    const Container mContainer;
    int             mSecretNum{4};

    void mainLoop() override
    {
        std::cout << "I, ServiceA, ran!" << std::endl;
    }
};

class ServiceB : public MicroService
{
public:
    using Container = MicroServiceContainer<ServiceA>;

    ServiceB(const Container& container) : mContainer{container} {}
    ~ServiceB() {}

    std::string name() const override
    {
        return "ServiceB";
    }

private:
    const Container mContainer;

    void mainLoop() override
    {
        std::cout << "I, ServiceB which depends on " << mContainer.get<ServiceA>()->name() << " with secret number "
                  << mContainer.get<ServiceA>()->getSecretNumber() << ", ran!" << std::endl;
    }
};

BOOST_AUTO_TEST_SUITE(TestServices)

BOOST_AUTO_TEST_CASE(TestFactory)
{
    ServiceFactory<ServiceA, ServiceB> factory;
}

BOOST_AUTO_TEST_SUITE_END()