#include "Inputs.h"

const uint8_t HeartbeatInput::priority() const
{
    return 1;
}

const std::chrono::milliseconds HeartbeatInput::duration() const
{
    return std::chrono::milliseconds(200); // ^^^^ TODO 150
}

const uint8_t IncrementInput::priority() const
{
    return 2;
}

const std::chrono::milliseconds IncrementInput::duration() const
{
    return std::chrono::milliseconds(5);
}

const uint8_t TransitionInput::priority() const
{
    return 2;
}

const std::chrono::milliseconds TransitionInput::duration() const
{
    return std::chrono::milliseconds(5);
}

const size_t TransitionInput::state() const
{
    return mState;
}
