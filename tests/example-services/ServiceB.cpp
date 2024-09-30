#include "ServiceB.h"

const size_t InitStateB::step(StoreB& s, const ContainerTypeB& c, const HeartbeatInput& i)
{
    s.state = "init";
    s.input = "heartbeat";
    s.counter++;
    if (s.counter == 4)
    {
        s.state = "init-SEND_INCREMENT";
        c.get<ServiceA>()->sendInput(IncrementInput());
    }
    else if (s.counter == 5)
    {
        s.state = "init-SEND_TRANSITION_RUNNING-and-SEND_INCREMENT";
        c.get<ServiceA>()->sendInput(TransitionInput(RunningStateA::index()));
        c.get<ServiceA>()->sendInput(IncrementInput());
    }
    else if (s.counter == 8)
    {
        s.state = "init-SEND_TRANSITION_STOPPED";
        c.get<ServiceA>()->sendInput(TransitionInput(StoppedStateA::index()));
    }
    universalReportStep(s);
    return InitStateB::index();
}

const size_t InitStateB::step(StoreB& s, const ContainerTypeB& c, const IncrementInput& i)
{
    s.state = "init";
    s.input = "increment";
    return InitStateB::index();
}

const size_t InitStateB::step(StoreB& s, const ContainerTypeB& c, const TransitionInput& i)
{
    s.state = "init";
    s.input = "transition";
    return InitStateB::index();
}

const std::string ServiceB::name() const
{
    return "ServiceB";
}
