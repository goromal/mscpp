#include "ServiceA.h"

const size_t InitStateA::step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i)
{
    s.state = "init";
    s.input = "heartbeat";
    universalReportStep(s);
    return index();
}

const size_t InitStateA::step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i)
{
    s.state = "init";
    s.input = "increment";
    s.counter++;
    universalReportStep(s);
    return index();
}

const size_t InitStateA::step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i)
{
    s.state = "init";
    s.input = "transition";
    universalReportStep(s);
    return i.state();
}

const size_t RunningStateA::step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i)
{
    s.state = "running";
    s.input = "heartbeat";
    universalReportStep(s);
    return index();
}

const size_t RunningStateA::step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i)
{
    s.state = "running";
    s.input = "increment";
    s.counter++;
    universalReportStep(s);
    return index();
}

const size_t RunningStateA::step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i)
{
    s.state = "running";
    s.input = "transition";
    universalReportStep(s);
    return i.state();
}

const size_t StoppedStateA::step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i)
{
    s.state = "stopped";
    s.input = "heartbeat";
    universalReportStep(s);
    return index();
}

const size_t StoppedStateA::step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i)
{
    s.state = "stopped";
    s.input = "increment";
    s.counter++;
    universalReportStep(s);
    return index();
}

const size_t StoppedStateA::step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i)
{
    s.state = "stopped";
    s.input = "transition";
    universalReportStep(s);
    return i.state();
}

const std::string ServiceA::name() const
{
    return "ServiceA";
}
