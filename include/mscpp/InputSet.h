#pragma once
#include "internal/utils.h"
#include "LogicalTime.h"
#include <variant>

namespace services
{

template<class T, class ResultType, uint8_t PRIORITY, uint64_t DURATION_MILLIS>
struct Input
{
    using DerivedType = T;

    constexpr std::chrono::milliseconds duration() const
    {
        return std::chrono::milliseconds(DURATION_MILLIS);
    }

    LogicalTag tag{};

    const LogicalTag& getTag() const
    {
        return tag;
    }

    void setTag(const LogicalTag& t)
    {
        tag = t;
    }
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
