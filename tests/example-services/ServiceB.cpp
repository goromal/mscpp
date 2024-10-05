#include "ServiceB.h"

const size_t InitStateB::step(StoreB& s, const ContainerTypeB& c, HeartbeatInput& i)
{
    s.state = "init";
    s.input = "heartbeat";
    s.counter++;
    if (s.counter == 4)
    {
        s.state = "init-SEND_INCREMENT";
        IncrementInput input;
        auto           inputResult = input.getFuture();
        c.get<ServiceA>()->sendInput(std::move(input));
        try
        {
            auto result = inputResult.get();
            if (std::holds_alternative<BooleanResult>(result))
            {
                assert(std::get<BooleanResult>(result).result);
            }
            else
            {
                std::cout << "Service A returned an error: " << std::get<services::ErrorResult>(result).errorMessage
                          << std::endl;
                assert(false);
            }
        }
        catch (const std::future_error& e)
        {
            std::cout << "Exception caught: " << e.what() << std::endl;
            if (e.code() == std::make_error_code(std::future_errc::broken_promise))
            {
                std::cout << "Broken promise detected!" << std::endl;
            }
            assert(false);
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

const size_t InitStateB::step(StoreB& s, const ContainerTypeB& c, IncrementInput& i)
{
    s.state = "init";
    s.input = "increment";
    return InitStateB::index();
}

const size_t InitStateB::step(StoreB& s, const ContainerTypeB& c, TransitionInput& i)
{
    s.state = "init";
    s.input = "transition";
    return InitStateB::index();
}

const std::string ServiceB::name() const
{
    return "ServiceB";
}
