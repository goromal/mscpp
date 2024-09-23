#include "ServiceA.h"

const size_t InitStateA::stateIndex() const
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

const size_t RunningStateA::stateIndex() const
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

const size_t StoppedStateA::stateIndex() const
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
    addInputToQueue(std::make_shared<IncrementInput>());
}

void ServiceA::transition(const size_t& state)
{
    addInputToQueue(std::make_shared<TransitionInput>(state));
}

const std::shared_ptr<services::Input> ServiceA::getHeartbeatInput() const override
{
    return std::make_shared<HeartbeatInput>();
}
