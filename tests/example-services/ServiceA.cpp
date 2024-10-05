#include "ServiceA.h"

size_t InitStateA::step(StoreA& s, const ContainerTypeA&, HeartbeatInput&)
{
    s.state = "init";
    s.input = "heartbeat";
    universalReportStep(s);
    return index();
}

size_t InitStateA::step(StoreA& s, const ContainerTypeA&, IncrementInput& i)
{
    s.state = "init";
    s.input = "increment";
    s.counter++;
    i.setResult(BooleanResult{true});
    universalReportStep(s);
    return index();
}

size_t InitStateA::step(StoreA& s, const ContainerTypeA&, TransitionInput& i)
{
    s.state = "init";
    s.input = "transition";
    i.setResult(services::ErrorResult{"For testing purposes, this failed."});
    universalReportStep(s);
    return i.state();
}

size_t RunningStateA::step(StoreA& s, const ContainerTypeA&, HeartbeatInput&)
{
    s.state = "running";
    s.input = "heartbeat";
    universalReportStep(s);
    return index();
}

size_t RunningStateA::step(StoreA& s, const ContainerTypeA&, IncrementInput&)
{
    s.state = "running";
    s.input = "increment";
    s.counter++;
    universalReportStep(s);
    return index();
}

size_t RunningStateA::step(StoreA& s, const ContainerTypeA&, TransitionInput& i)
{
    s.state = "running";
    s.input = "transition";
    universalReportStep(s);
    return i.state();
}

size_t StoppedStateA::step(StoreA& s, const ContainerTypeA&, HeartbeatInput&)
{
    s.state = "stopped";
    s.input = "heartbeat";
    universalReportStep(s);
    return index();
}

size_t StoppedStateA::step(StoreA& s, const ContainerTypeA&, IncrementInput&)
{
    s.state = "stopped";
    s.input = "increment";
    s.counter++;
    universalReportStep(s);
    return index();
}

size_t StoppedStateA::step(StoreA& s, const ContainerTypeA&, TransitionInput& i)
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
