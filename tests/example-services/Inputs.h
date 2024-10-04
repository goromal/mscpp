#pragma once

#include <chrono>
#include "mscpp/MicroService.h"
#include "mscpp/InputSet.h"

struct BooleanOutput
{
    bool result;
};

struct HeartbeatInput : public services::Input<HeartbeatInput, services::EmptyOutput>
{
    const uint8_t                   priority() const override;
    const std::chrono::milliseconds duration() const override;
};

struct IncrementInput : public services::Input<IncrementInput, BooleanOutput>
{
    const uint8_t                   priority() const override;
    const std::chrono::milliseconds duration() const override;
};

struct TransitionInput : public services::Input<TransitionInput, BooleanOutput>
{
    TransitionInput(const size_t& state) : mState(state) {}
    const uint8_t                   priority() const override;
    const std::chrono::milliseconds duration() const override;
    const size_t                    state() const;
    size_t                          mState;
};

using Inputs = services::InputSet<HeartbeatInput, IncrementInput, TransitionInput>;
