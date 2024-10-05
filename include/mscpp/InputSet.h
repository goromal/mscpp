#pragma once
#include "internal/utils.h"
#include <future>

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
    using ResultType  = std::variant<ErrorResult, OutputType>;

    std::promise<ResultType> promise;
    std::future<ResultType>  getFuture()
    {
        return std::move(promise.get_future());
    }
    void setPromise(ResultType&& result)
    {
        promise.set_value(std::move(result));
    }

    virtual const uint8_t                   priority() const = 0;
    virtual const std::chrono::milliseconds duration() const = 0;
};

template<typename HeartbeatInput, typename... Inputs>
class InputSet : Inputs...
{
public:
    using Heartbeat     = HeartbeatInput;
    using TypesVariant  = std::variant<HeartbeatInput, Inputs...>;
    using GenericInputs = __type_list<Inputs...>;
};

} // namespace services
