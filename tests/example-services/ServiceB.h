#pragma once

#include "mscpp/StateSet.h"
#include "mscpp/MicroService.h"
#include "mscpp/MicroServiceContainer.h"

#include "ServiceA.h"

using ContainerTypeB = services::MicroServiceContainer<ServiceA>;

struct StoreB
{
    std::string  name    = "B";
    std::string  state   = "init";
    std::string  input   = "NONE";
    unsigned int counter = 0;
};

struct InitStateB : public services::State<InitStateB>
{
    static const size_t stateIndex();

    const size_t step(StoreB& s, const ContainerTypeB& c, const HeartbeatInput& i);
    const size_t step(StoreB& s, const ContainerTypeB& c, const IncrementInput& i);
    const size_t step(StoreB& s, const ContainerTypeB& c, const TransitionInput& i);
};

using StatesB = services::StateSet<InitStateB>;

class ServiceB : public services::MicroService<StoreB, ContainerTypeB, StatesB, Inputs>
{
public:
    ServiceB(const ContainerTypeB& container)
        : services::MicroService<StoreB, ContainerTypeB, StatesB, Inputs>(container)
    {
    }
    const std::string name() const override;

private:
    const Inputs::Heartbeat getHeartbeatInput() const override;
};
