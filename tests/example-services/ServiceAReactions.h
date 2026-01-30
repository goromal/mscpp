#pragma once

#include "mscpp/Reaction.h"
#include "mscpp/ReactorWithReactions.h"
#include "mscpp/MicroServiceContainer.h"
#include "Inputs.h"

/**
 * Phase 3: ServiceA with Explicit Reactions
 *
 * Converts ServiceA from FSM-based to Reaction-based model.
 * Each reaction explicitly declares its triggers, effects, and dependencies.
 */

inline constexpr char NameAReactions[] = "ServiceA_Reactions";

using ContainerTypeAReactions = services::MicroServiceContainer<>;

struct StoreAReactions
{
    std::string name = "A";
    std::string state = "init";
    std::string input = "NONE";
    unsigned int counter = 0;
};

// Forward declaration
class ServiceAReactions;

/**
 * Heartbeat Reaction
 *
 * Triggered by: HeartbeatInput
 * Effects: None
 * Dependencies: None
 */
struct HeartbeatReactionA : public services::Reaction<
    ServiceAReactions,
    0,  // Index
    services::TypeList<HeartbeatInput>,  // Triggers
    services::TypeList<>,  // Effects
    services::TypeList<>   // Dependencies
>
{
    void execute(StoreAReactions& store, const ContainerTypeAReactions& container, HeartbeatInput& input)
    {
        store.state = "init";
        store.input = "heartbeat";
    }
};

/**
 * Increment Reaction
 *
 * Triggered by: IncrementInput
 * Effects: None
 * Dependencies: None
 */
struct IncrementReactionA : public services::Reaction<
    ServiceAReactions,
    1,  // Index
    services::TypeList<IncrementInput>,  // Triggers
    services::TypeList<>,  // Effects
    services::TypeList<>   // Dependencies
>
{
    void execute(StoreAReactions& store, const ContainerTypeAReactions& container, IncrementInput& input)
    {
        store.state = "init";
        store.input = "increment";
        store.counter++;
        input.setResult(BooleanResult{true});
    }
};

/**
 * Transition Reaction
 *
 * Triggered by: TransitionInput
 * Effects: None
 * Dependencies: None
 */
struct TransitionReactionA : public services::Reaction<
    ServiceAReactions,
    2,  // Index
    services::TypeList<TransitionInput>,  // Triggers
    services::TypeList<>,  // Effects
    services::TypeList<>   // Dependencies
>
{
    void execute(StoreAReactions& store, const ContainerTypeAReactions& container, TransitionInput& input)
    {
        store.state = "init";
        store.input = "transition";
        input.setResult(services::ErrorResult{"For testing purposes, this failed."});
    }
};

// Define ReactionSet for ServiceA
using ReactionsA = services::ReactionSet<
    HeartbeatReactionA,
    IncrementReactionA,
    TransitionReactionA
>;

/**
 * ServiceA with Reactions
 */
class ServiceAReactions : public services::ReactorWithReactions<
    NameAReactions,
    StoreAReactions,
    ContainerTypeAReactions,
    ReactionsA
>
{
public:
    using Base = services::ReactorWithReactions<
        NameAReactions,
        StoreAReactions,
        ContainerTypeAReactions,
        ReactionsA
    >;

    ServiceAReactions() : Base() {}
    ServiceAReactions(const ContainerTypeAReactions& container) : Base(container) {}

    // Provide static name for Reaction::id()
    static constexpr const char* name()
    {
        return NameAReactions;
    }

protected:
    std::chrono::nanoseconds getHeartbeatDuration() const override
    {
        return std::chrono::milliseconds(100);  // 100ms heartbeat
    }
};
