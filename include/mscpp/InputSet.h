#pragma once
#include "internal/utils.h"

namespace services
{

template<class T>
struct Input
{
    using DerivedType = T;

    virtual const uint8_t                   priority() const = 0;
    virtual const std::chrono::milliseconds duration() const = 0;
};

template<typename HeartbeatInput, typename... Inputs>
class InputSet
{
public:
    using Heartbeat    = HeartbeatInput;
    using TypesVariant = std::variant<HeartbeatInput, Inputs...>;
};

} // namespace services
