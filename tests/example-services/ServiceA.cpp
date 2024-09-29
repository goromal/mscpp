#include "ServiceA.h"

const size_t InitStateA::stateIndex()
{
    return 0;
}

const size_t InitStateA::step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i)
{
    universalReportStep(s);
    return index();
}

const size_t InitStateA::step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i)
{
    s.counter++;
    universalReportStep(s);
    return index();
}

const size_t InitStateA::step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i)
{
    universalReportStep(s);
    return i.state();
}

const size_t RunningStateA::stateIndex()
{
    return 1;
}

const size_t RunningStateA::step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i)
{
    universalReportStep(s);
    return index();
}

const size_t RunningStateA::step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i)
{
    s.counter++;
    universalReportStep(s);
    return index();
}

const size_t RunningStateA::step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i)
{
    universalReportStep(s);
    return i.state();
}

const size_t StoppedStateA::stateIndex()
{
    return 2;
}

const size_t StoppedStateA::step(StoreA& s, const ContainerTypeA& c, const HeartbeatInput& i)
{
    universalReportStep(s);
    return index();
}

const size_t StoppedStateA::step(StoreA& s, const ContainerTypeA& c, const IncrementInput& i)
{
    s.counter++;
    universalReportStep(s);
    return index();
}

const size_t StoppedStateA::step(StoreA& s, const ContainerTypeA& c, const TransitionInput& i)
{
    universalReportStep(s);
    return i.state();
}

const std::string ServiceA::name() const
{
    return "ServiceA";
}

void ServiceA::increment()
{
    addInputToQueue(IncrementInput());
}

void ServiceA::transition(const size_t& state)
{
    addInputToQueue(TransitionInput(state));
}

const HeartbeatInput ServiceA::getHeartbeatInput() const
{
    return HeartbeatInput();
}
