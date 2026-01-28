#pragma once
#include "internal/utils.h"
#include "LogicalTime.h"
#include <future>
#include <variant>

namespace services
{

struct ErrorResult
{
    std::string errorMessage;
};

template<class T, class ResultType, uint8_t PRIORITY, uint64_t DURATION_MILLIS>
struct Input
{
    using DerivedType = T;
    using Result      = std::variant<ErrorResult, ResultType>;

    std::promise<Result> promise;
    std::future<Result>  getFuture()
    {
        return promise.get_future();
    }
    void setResult(Result&& result)
    {
        promise.set_value(std::move(result));
    }

    constexpr uint8_t priority() const
    {
        return DURATION_MILLIS;
    }
    constexpr std::chrono::milliseconds duration() const
    {
        return std::chrono::milliseconds(DURATION_MILLIS);
    }

#if ENABLE_LOGICAL_TIME
    // Phase 1: Add logical tag to all inputs (optional field for backward compatibility)
    LogicalTag tag{};

    // Get the logical tag for this input
    const LogicalTag& getTag() const
    {
        return tag;
    }

    // Set the logical tag for this input
    void setTag(const LogicalTag& t)
    {
        tag = t;
    }
#endif
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
