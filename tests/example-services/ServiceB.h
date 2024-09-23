#pragma once

#include "mscpp/MicroService.h"
#include "mscpp/MicroServiceContainer.h"

#include "ServiceA.h"

using ContainerTypeB = services::MicroServiceContainer<ServiceA>;

struct StoreB
{
    std::string  name    = "B";
    std::string  state   = "init";
    unsigned int counter = 0;
};

struct InitStateB : public services::State<InitStateB>
{
    const size_t stateIndex() const;
    const size_t step(StoreB& s, const ContainerTypeB& c, const HeartbeatInput& i);
};

class ServiceB : public services::MicroService<StoreB, ContainerTypeB, InitStateB>
{
public:
    const std::string name() const override;

private:
    const std::shared_ptr<services::Input> getHeartbeatInput() const override;
};
