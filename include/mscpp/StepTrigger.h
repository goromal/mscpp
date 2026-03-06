#pragma once

#include <string>

namespace services
{

/**
 * StepTrigger - Provides context for why an FSM step was invoked
 *
 * The StepTrigger encapsulates the reason for an FSM state transition,
 * enabling states to distinguish between periodic heartbeats, event-driven
 * logical actions, and scheduled physical actions.
 *
 * This enforces deterministic, event-driven reactor patterns by making
 * the trigger reason explicit in the FSM state step() signature.
 *
 * Usage in FSM State:
 *   size_t step(Store& s, Ports& p, const Container& c,
 *              const LogicalTag& tag, const StepTrigger& trigger) {
 *       if (trigger.type == StepTrigger::Type::LOGICAL_ACTION) {
 *           if (trigger.action_name == "on_port_input") {
 *               // Handle input port event
 *           }
 *       }
 *       return CurrentState::index();
 *   }
 */
struct StepTrigger
{
    /**
     * Type of trigger that caused the FSM step
     */
    enum class Type
    {
        HEARTBEAT,        ///< Periodic heartbeat tick
        LOGICAL_ACTION,   ///< Event-driven logical action (microstep only)
        PHYSICAL_ACTION   ///< Scheduled physical action (time-delayed)
    };

    Type type;                  ///< What caused this step?
    std::string action_name;    ///< Name of action (only for LOGICAL/PHYSICAL_ACTION)

    /**
     * Create a heartbeat trigger
     *
     * Used for periodic heartbeat invocations with no specific event.
     *
     * @return StepTrigger for heartbeat
     */
    static StepTrigger heartbeat()
    {
        return {Type::HEARTBEAT, ""};
    }

    /**
     * Create a logical action trigger
     *
     * Used for event-driven reactions that advance only the microstep
     * within the same logical time instant.
     *
     * @param name Name of the logical action
     * @return StepTrigger for logical action
     */
    static StepTrigger logicalAction(const std::string& name)
    {
        return {Type::LOGICAL_ACTION, name};
    }

    /**
     * Create a physical action trigger
     *
     * Used for time-delayed reactions that advance both time and reset microstep.
     *
     * @param name Name of the physical action
     * @return StepTrigger for physical action
     */
    static StepTrigger physicalAction(const std::string& name)
    {
        return {Type::PHYSICAL_ACTION, name};
    }
};

} // namespace services
