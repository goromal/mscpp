#pragma once
#include "internal/utils.h"

namespace services
{

struct EmptyOutput
{
};

struct ErrorResult
{
    std::string errorMessage;
};

template<class T, class OutputType>
struct Input
{
    using DerivedType = T;
    using ResultType  = OutputType;

    // ^^^^ TODO CAN WE HAVE THE FUTURE EXCHANGE HAPPEN FROM WITHIN HERE?

    virtual const uint8_t                   priority() const = 0;
    virtual const std::chrono::milliseconds duration() const = 0;
};

template<typename HeartbeatInput, typename... Inputs>
class InputSet : Inputs...
{
public:
    using Heartbeat    = HeartbeatInput;
    using TypesVariant = std::variant<HeartbeatInput, Inputs...>;
    // using OutputsVariant =
    //     std::variant<ErrorResult, typename HeartbeatInput::ResultType, typename Inputs::ResultType...>;
    using GenericInputs = __type_list<Inputs...>;
};

} // namespace services
