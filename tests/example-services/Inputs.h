#pragma once

#include <chrono>
#include "mscpp/MicroService.h"

struct HeartbeatInput : public services::Input
{
    const uint8_t                   priority() const override;
    const std::chrono::milliseconds duration() const override;
};

struct IncrementInput : public services::Input
{
    const uint8_t                   priority() const override;
    const std::chrono::milliseconds duration() const override;
};

struct TransitionInput : public services::Input
{
    TransitionInput(const size_t& state) : mState(state) {}
    const uint8_t                   priority() const override;
    const std::chrono::milliseconds duration() const override;
    const size_t                    state() const;
    size_t                          mState;
};
