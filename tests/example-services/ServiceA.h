#pragma once

// #include <iostream>

#include "mscpp/StateSet.h"
#include "mscpp/MicroService.h"
#include "mscpp/MicroServiceContainer.h"

#include "Inputs.h"

template<typename StoreType>
inline void universalReportStep(const StoreType&)
{
    // std::cout << "Service " << store.name << " is in state " << store.state << " with input " << store.input
    //           << " and counter " << store.counter << std::endl;
}

using ContainerTypeA = services::MicroServiceContainer<>;

struct StoreA
{
    std::string  name    = "A";
    std::string  state   = "initt";
    std::string  input   = "NONE";
    unsigned int counter = 0;
};

struct InitStateA : public services::State<InitStateA, 0>
{
    size_t step(StoreA& s, const ContainerTypeA& c, HeartbeatInput& i);
    size_t step(StoreA& s, const ContainerTypeA& c, IncrementInput& i);
    size_t step(StoreA& s, const ContainerTypeA& c, TransitionInput& i);
};

struct RunningStateA : public services::State<RunningStateA, 1>
{
    size_t step(StoreA& s, const ContainerTypeA& c, HeartbeatInput& i);
    size_t step(StoreA& s, const ContainerTypeA& c, IncrementInput& i);
    size_t step(StoreA& s, const ContainerTypeA& c, TransitionInput& i);
};

struct StoppedStateA : public services::State<StoppedStateA, 2>
{
    size_t step(StoreA& s, const ContainerTypeA& c, HeartbeatInput& i);
    size_t step(StoreA& s, const ContainerTypeA& c, IncrementInput& i);
    size_t step(StoreA& s, const ContainerTypeA& c, TransitionInput& i);
};

using StatesA = services::StateSet<InitStateA, RunningStateA, StoppedStateA>;

using ServiceABase = services::MicroService<StoreA, ContainerTypeA, StatesA, Inputs>;

class ServiceA : public ServiceABase
{
public:
    ServiceA(const ContainerTypeA& container) : ServiceABase(container) {}
    const std::string name() const override;
};
