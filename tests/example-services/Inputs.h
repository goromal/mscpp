#pragma once

#include <chrono>
#include "mscpp/MicroService.h"
#include "mscpp/InputSet.h"

struct EmptyResult
{
};

struct BooleanResult
{
    bool result;
};

struct HeartbeatInput : public services::Input<HeartbeatInput, EmptyResult, 1, 100>
{
};

struct IncrementInput : public services::Input<IncrementInput, BooleanResult, 2, 5>
{
};

struct TransitionInput : public services::Input<TransitionInput, BooleanResult, 2, 5>
{
    TransitionInput(const size_t& state) : mState(state) {}
    const size_t state() const;
    size_t       mState;
};

using Inputs = services::InputSet<HeartbeatInput, IncrementInput, TransitionInput>;
