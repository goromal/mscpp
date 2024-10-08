#include "ServiceB.h"

size_t InitStateB::step(StoreB& s, const ContainerTypeB& c, HeartbeatInput&)
{
    s.state = "init";
    s.input = "heartbeat";
    s.counter++;
    if (s.counter == 4)
    {
        s.state = "init-SEND_INCREMENT";
        IncrementInput input;
        auto           inputResult = input.getFuture();
        if (!c.get<ServiceA>()->sendInput(std::move(input)))
        {
            throw std::runtime_error("Failed to push input; queue full.");
        }
        try
        {
            auto result = inputResult.get();
            if (std::holds_alternative<BooleanResult>(result))
            {
                if (!std::get<BooleanResult>(result).result)
                {
                    throw std::runtime_error("Future returned failure.");
                }
            }
            else
            {
                std::cout << "Service A returned an error: " << std::get<services::ErrorResult>(result).errorMessage
                          << std::endl;
                throw std::runtime_error("Service error.");
            }
        }
        catch (const std::future_error& e)
        {
            std::cout << "Exception caught: " << e.what() << std::endl;
            if (e.code() == std::make_error_code(std::future_errc::broken_promise))
            {
                std::cout << "Broken promise detected!" << std::endl;
            }
            throw std::runtime_error("Broken service promise.");
        }
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

size_t InitStateB::step(StoreB& s, const ContainerTypeB&, IncrementInput&)
{
    s.state = "init";
    s.input = "increment";
    return InitStateB::index();
}

size_t InitStateB::step(StoreB& s, const ContainerTypeB&, TransitionInput&)
{
    s.state = "init";
    s.input = "transition";
    return InitStateB::index();
}

const std::string ServiceB::name() const
{
    return "ServiceB";
}
