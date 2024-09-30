#include "ServiceB.h"

const size_t InitStateB::stateIndex()
{
    return 0;
}

const size_t InitStateB::step(StoreB& s, const ContainerTypeB& c, const HeartbeatInput& i)
{
    s.state = "init";
    s.input = "heartbeat";
    s.counter++;
    if (s.counter == 4)
    {
        s.state = "init-SEND_INCREMENT";
        c.get<ServiceA>()->increment();
    }
    else if (s.counter == 5)
    {
        s.state = "init-SEND_TRANSITION_RUNNING";
        c.get<ServiceA>()->transition(RunningStateA::index());
    }
    else if (s.counter == 8)
    {
        s.state = "init-SEND_TRANSITION_STOPPED";
        c.get<ServiceA>()->transition(StoppedStateA::index());
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

const HeartbeatInput ServiceB::getHeartbeatInput() const
{
    return HeartbeatInput();
}
