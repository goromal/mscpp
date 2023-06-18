#include <boost/test/unit_test.hpp>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include "mscpp/MicroService.h"
#include "mscpp/MicroServiceContainer.h"
#include "mscpp/ServiceFactory.h"

// Service inputs

struct HeartbeatInput : public services::Input
{
    const uint8_t priority() const override
    {
        return 1;
    }
    const std::chrono::milliseconds duration() const override
    {
        return std::chrono::milliseconds(100);
    }
};

struct IncrementInput : public services::Input
{
    const uint8_t priority() const override
    {
        return 2;
    }
    const std::chrono::milliseconds duration() const override
    {
        return std::chrono::milliseconds(5);
    }
};

struct TransitionInput : public services::Input
{
    TransitionInput(const size_t& state) : mState(state) {}
    const uint8_t priority() const override
    {
        return 2;
    }
    const std::chrono::milliseconds duration() const override
    {
        return std::chrono::milliseconds(5);
    }
    const size_t state() const
    {
        return mState;
    }
    size_t mState;
};

// Service stores

struct StoreA
{
    std::string  name    = "A";
    std::string  state   = "init";
    unsigned int counter = 0;
};

struct StoreB
{
    std::string  name    = "B";
    std::string  state   = "init";
    unsigned int counter = 0;
};

// Service containers (can't be circularly dependent)

class ServiceA;
class ServiceB;

using ContainerTypeA = services::MicroServiceContainer<>;

using ContainerTypeB = services::MicroServiceContainer<ServiceA>;

// Service states

class InitStateA;
class RunningStateA;
class StoppedStateA;
class InitStateB;
class RunningStateB;
class StoppedStateB;

template<typename StoreType>
void universalReportStep(const StoreType& store)
{
    std::cout << "Service " << store.name << " is in state " << store.state << " with counter " << store.counter
              << std::endl;
}

struct InitStateA : public services::State<InitStateA>
{
    const size_t stateIndex() const
    {
        return 0;
    }
    size_t step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i)
    {
        universalReportStep(s);
        return index();
    }
    size_t step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i)
    {
        s.counter++;
        universalReportStep(s);
        return index();
    }
    size_t step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i)
    {
        universalReportStep(s);
        return i.state();
    }
};

struct RunningStateA : public services::State<RunningStateA>
{
    const size_t stateIndex() const
    {
        return 1;
    }
    size_t step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i)
    {
        universalReportStep(s);
        return index();
    }
    size_t step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i)
    {
        s.counter++;
        universalReportStep(s);
        return index();
    }
    size_t step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i)
    {
        universalReportStep(s);
        return i.state();
    }
};

struct StoppedStateA : public services::State<StoppedStateA>
{
    const size_t stateIndex() const
    {
        return 2;
    }
    size_t step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i)
    {
        universalReportStep(s);
        return index();
    }
    size_t step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i)
    {
        s.counter++;
        universalReportStep(s);
        return index();
    }
    size_t step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i)
    {
        universalReportStep(s);
        return i.state();
    }
};

struct InitStateB : public services::State<InitStateB>
{
    const size_t stateIndex() const
    {
        return 0;
    }
    size_t step(StoreB& s, const ContainerTypeB& c, const HeartbeatInput& i)
    {
        s.counter++;
        universalReportStep(s);
        if (s.counter == 4)
        {
            c.get<ServiceA>()->increment();
        }
        else if (s.counter == 5)
        {
            c.get<ServiceA>()->transition(RunningStateA::index());
        }
        else if (s.counter == 8)
        {
            c.get<ServiceA>()->transition(StoppedStateA::index());
        }
        return InitStateB::index();
    }
};

// Service instantiations

class ServiceA : public services::MicroService<StoreA, ContainerTypeA, InitStateA, RunningStateA, StoppedStateA>
{
public:
    std::string name() const override
    {
        return "ServiceA";
    }
    void increment()
    {
        addInputToQueue(std::make_shared<IncrementInput>());
    }
    void transition(const size_t& state)
    {
        addInputToQueue(std::make_shared<TransitionInput>(state));
    }

private:
    const std::shared_ptr<services::Input> getHeartbeatInput() const override
    {
        return std::make_shared<HeartbeatInput>();
    }
};

class ServiceB : public services::MicroService<StoreB, ContainerTypeB, InitStateB>
{
public:
    std::string name() const override
    {
        return "ServiceB";
    }

private:
    const std::shared_ptr<services::Input> getHeartbeatInput() const override
    {
        return std::make_shared<HeartbeatInput>();
    }
};

BOOST_AUTO_TEST_SUITE(TestServices)

BOOST_AUTO_TEST_CASE(TestFactory)
{
    services::ServiceFactory<ServiceA, ServiceB> factory;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    factory.stop();
}

BOOST_AUTO_TEST_SUITE_END()
