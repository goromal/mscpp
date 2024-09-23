#include "ServiceB.h"

const size_t InitStateB::stateIndex() const
{
    return 0;
}

const size_t InitStateB::step(StoreB& s, const ContainerTypeB& c, const HeartbeatInput& i)
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

const std::string ServiceB::name() const
{
    return "ServiceB";
}

const std::shared_ptr<services::Input> ServiceB::getHeartbeatInput() const
{
    return std::make_shared<HeartbeatInput>();
}
