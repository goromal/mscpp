#pragma once

#include <iostream>

#include "mscpp/StateSet.h"
#include "mscpp/MicroService.h"
#include "mscpp/MicroServiceContainer.h"

#include "Inputs.h"

template<typename StoreType>
inline void universalReportStep(const StoreType& store)
{
    std::cout << "Service " << store.name << " is in state " << store.state << " with input " << store.input
              << " and counter " << store.counter << std::endl;
}

using ContainerTypeA = services::MicroServiceContainer<>;

struct StoreA
{
    std::string  name    = "A";
    std::string  state   = "initt";
    std::string  input   = "NONE";
    unsigned int counter = 0;
};

struct InitStateA : public services::State<InitStateA>
{
    static const size_t stateIndex();

    const size_t step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i);
    const size_t step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i);
    const size_t step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i);
};

struct RunningStateA : public services::State<RunningStateA>
{
    static const size_t stateIndex();

    const size_t step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i);
    const size_t step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i);
    const size_t step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i);
};

struct StoppedStateA : public services::State<StoppedStateA>
{
    static const size_t stateIndex();

    const size_t step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i);
    const size_t step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i);
    const size_t step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i);
};

using StatesA = services::StateSet<InitStateA, RunningStateA, StoppedStateA>;

class ServiceA : public services::MicroService<StoreA, ContainerTypeA, StatesA, Inputs>
{
public:
    ServiceA(const ContainerTypeA& container)
        : services::MicroService<StoreA, ContainerTypeA, StatesA, Inputs>(container)
    {
    }
    const std::string name() const override;
    void              increment();
    void              transition(const size_t& state);

private:
    const Inputs::Heartbeat getHeartbeatInput() const override;
};
