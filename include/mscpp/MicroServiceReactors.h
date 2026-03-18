#pragma once

#include "Ports.h"
#include "Reaction.h"
#include "ReactorScheduler.h"
#include "MicroServiceContainer.h"
#include "StepTrigger.h"
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <any>

/**
 * MicroServiceReactor - Template base class for reactors using port-based I/O
 *
 * Inherits from IReactor so that port-based services can be registered
 * directly with the ReactorScheduler — no external adapter needed.
 *
 * Lifecycle when driven by the scheduler:
 *   1. Scheduler calls executeHeartbeat(tag)  [IReactor virtual]
 *   2. Base class calls doHeartbeat(tag)      [pure virtual, subclass logic]
 *   3. Base class calls clearPorts()          [resets input presence]
 *   4. Base class reschedules next heartbeat at tag + heartbeatDuration()
 *
 * Lifecycle when called directly in unit tests (no scheduler attached):
 *   executeHeartbeat(tag) just calls doHeartbeat(tag).
 *   No rescheduling or port clearing — the caller controls both.
 *
 * Template Parameters:
 * - Name: Reactor name (compile-time string constant)
 * - Store: Reactor state type
 * - Ports: Port collection type
 * - Container: Dependency injection container
 * - ReactionSet: Set of reactions (optional, for reaction-based model)
 *
 * Usage:
 *   class MyReactor : public MicroServiceReactor<NameMyReactor, StoreMyReactor,
 *                                              PortsMyReactor, ContainerType> {
 *   public:
 *       using Base = MicroServiceReactor<...>;
 *       using Base::Base;
 *
 *       // Implement the heartbeat logic (reactions or direct port manipulation)
 *       void doHeartbeat(const LogicalTag& tag) override { ... }
 *
 *       // Implement clearPorts() to reset all input ports
 *       void clearPorts() override { mPorts.some_input.clear(); ... }
 *   };
 */

namespace services
{

template<const char* Name,
         typename StoreType,
         typename PortsType,
         typename ContainerType,
         typename ReactionSetType = void>
class MicroServiceReactor : public IReactor
{
public:
    using Store = StoreType;
    using Ports = PortsType;
    using Container = ContainerType;
    using Reactions = ReactionSetType;

    static constexpr const char* name() { return Name; }

    /**
     * Default constructor.
     * Initializes store, ports, and container with defaults.
     * Automatically assigns a unique reactor ID.
     */
    MicroServiceReactor()
        : mStore{}
        , mPorts{}
        , mContainer(__handle_later{})
    {
        mReactorId = getGlobalReactorIdCounter().fetch_add(1);
    }

    /**
     * Constructor with dependency injection container.
     *
     * @param container Dependency injection container for services/resources
     */
    MicroServiceReactor(const Container& container)
        : mStore{}
        , mPorts{}
        , mContainer(container)
    {
        mReactorId = getGlobalReactorIdCounter().fetch_add(1);
    }

    /**
     * Virtual destructor for proper cleanup of derived classes.
     */
    virtual ~MicroServiceReactor() = default;

    // ── IReactor interface ───────────────────────────────────────────

    /**
     * Get the unique reactor ID.
     *
     * @return Globally unique reactor identifier
     */
    size_t getId() const override
    {
        return mReactorId;
    }

    /**
     * Get the reactor name.
     *
     * @return Name string from template parameter
     */
    std::string getName() const override
    {
        return std::string(Name);
    }

    /**
     * Initialize reactor state before scheduling begins.
     *
     * Called once by the scheduler before the first heartbeat.
     * Override to set initial state or perform setup.
     */
    void initialize() override
    {
        // Default: nothing to do.  Subclasses may override to seed state.
    }

    /**
     * IReactor entry point called by the scheduler.
     *
     * Calls doHeartbeat() for the subclass logic, then — if a scheduler
     * is attached — clears input ports and reschedules the next heartbeat.
     * When no scheduler is attached (unit-test usage) this is a bare
     * delegation to doHeartbeat() with no side effects.
     */
    void executeHeartbeat(const LogicalTag& tag) override
    {
        doHeartbeat(tag);

        if (mScheduler)
        {
            // Clear input ports using custom override
            clearPorts();
            // Also call generic helper for ENABLE_AUTO_CLEAR_PORTS (function name in global namespace)
            services::clearInputPorts(mPorts);

            LogicalTag next = tag.advance_time(heartbeatDuration());
            mScheduler->scheduleEvent(next, mReactorId, [this, next]() {
                this->executeHeartbeat(next);
            }, getName() + " heartbeat");
        }
    }

    /**
     * Execute a logical action (event-triggered reaction).
     *
     * Logical actions enable immediate intra-tag event scheduling without physical time delay.
     * They advance only the microstep counter, allowing event-driven request-response patterns
     * to complete within the same logical time instant.
     *
     * Override this method to handle specific logical actions by name.
     * The base implementation does nothing.
     *
     * @param tag Current logical tag
     * @param action_name Name of the logical action to execute
     */
    void executeLogicalAction(const LogicalTag& tag, const std::string& action_name) override // ^^^^ why a string?
    {
        // Default implementation: no-op
        // Subclasses override to handle specific actions
        (void)tag;
        (void)action_name;
    }

    /**
     * Check if reactor has pending inputs (legacy queue-based model).
     *
     * @return false (port-based reactors don't use input queues)
     */
    bool hasPendingInputs() const override { return false; }

    /**
     * Process next queued input (legacy queue-based model).
     *
     * @param tag Current logical tag
     * @return false (port-based reactors don't use input queues)
     */
    bool processNextInput(const LogicalTag&) override { return false; }

    // ── Scheduler wiring ─────────────────────────────────────────────

    /**
     * Attach a scheduler.  Must be called before the scheduler's run().
     * When set, executeHeartbeat() will reschedule and clear ports.
     *
     * @param scheduler Pointer to the ReactorScheduler managing this reactor
     */
    void setScheduler(ReactorScheduler* scheduler)
    {
        mScheduler = scheduler;
    }

    // ── Logical Actions ──────────────────────────────────────────────

    /**
     * Schedule a logical action (microstep advancement only, no physical time delay).
     *
     * Logical actions enable immediate event-driven reactions within the same logical time.
     * This is useful for request-response patterns where A→B→A should complete at the
     * same logical instant without waiting for heartbeat intervals.
     *
     * Example:
     *   scheduleLogicalAction("on_request"); // Executes at (current_time, microstep+1)
     *
     * @param action_name Name of the logical action to schedule
     */
    void scheduleLogicalAction(const std::string& action_name)
    {
        if (mScheduler)
        {
            LogicalTag current = mScheduler->getCurrentTag();
            LogicalTag next_microstep = current.next_microstep();

            mScheduler->scheduleEvent(next_microstep, mReactorId,
                [this, next_microstep, action_name]() {
                    this->executeLogicalAction(next_microstep, action_name);
                },
                getName() + "::" + action_name);
        }
    }

    /**
     * Schedule a logical action with data from external thread (thread-safe).
     *
     * This method enables thread-safe logical action scheduling from I/O adapters
     * running in separate threads. The action and its data are queued and will be
     * processed on the next reactor heartbeat.
     *
     * THREAD SAFETY:
     * - Safe to call from any thread
     * - Uses mutex to protect pending action queue
     * - Actions are processed during doHeartbeat() via processPendingActions()
     *
     * USAGE PATTERN:
     * - I/O adapters call this when external events arrive
     * - Data is stored as std::any for type erasure
     * - Reactor processes actions during heartbeat
     *
     * Example:
     *   // From gRPC thread:
     *   reactor->scheduleLogicalActionWithData("job_request", job_data);
     *
     * @tparam ActionData Type of data payload
     * @param action_name Name of the logical action to schedule
     * @param data Data payload for the action
     */
    template<typename ActionData>
    void scheduleLogicalActionWithData(const std::string& action_name, ActionData&& data)
    {
        std::lock_guard<std::mutex> lock(mActionQueueMutex);
        mPendingActions.emplace_back(PendingAction{
            action_name,
            std::make_any<ActionData>(std::forward<ActionData>(data))
        });
    }

    /**
     * Schedule a physical action (advances both time and resets microstep).
     *
     * Physical actions schedule reactions at a future logical time.
     * Unlike logical actions, these advance physical time and are useful for
     * timeouts, delays, and other time-dependent operations.
     *
     * Example:
     *   schedulePhysicalAction(std::chrono::milliseconds(100), "timeout");
     *
     * @param delay Time delay from current tag
     * @param action_name Name of the physical action to schedule
     */
    void schedulePhysicalAction(LogicalTime delay, const std::string& action_name)
    {
        if (mScheduler)
        {
            LogicalTag current = mScheduler->getCurrentTag();
            LogicalTag future = current.advance_time(delay);

            mScheduler->scheduleEvent(future, mReactorId,
                [this, future, action_name]() {
                    this->executeLogicalAction(future, action_name);
                },
                getName() + "::" + action_name);
        }
    }

    // ── Accessors ────────────────────────────────────────────────────

    /**
     * Get mutable reference to reactor state.
     *
     * WARNING: Direct state access violates reactor encapsulation. Use with caution.
     * Prefer message passing via ports for inter-reactor communication.
     *
     * @return Reference to state store
     */
    Store& getStore() { return mStore; }

    /**
     * Get const reference to reactor state.
     *
     * @return Const reference to state store
     */
    const Store& getStore() const { return mStore; }

    /**
     * Get mutable reference to reactor ports.
     *
     * @return Reference to port collection
     */
    Ports& getPorts() { return mPorts; }

    /**
     * Get const reference to reactor ports.
     *
     * @return Const reference to port collection
     */
    const Ports& getPorts() const { return mPorts; }

    /**
     * Get reference to dependency injection container.
     *
     * @return Const reference to container
     */
    const Container& getContainer() const { return mContainer; }

    /**
     * Legacy helper — does nothing by default.
     * Kept for backward compatibility; prefer overriding clearPorts().
     */
    void clearInputPorts()
    {
        clearPorts();
    }

protected:
    // ── Pure virtuals for subclasses ─────────────────────────────────

    /**
     * Subclass heartbeat logic.  This is the only method you must override.
     * Write your reactions or FSM dispatch here.
     *
     * @param tag Current logical tag for this heartbeat
     */
    virtual void doHeartbeat(const LogicalTag& tag) = 0;

    /**
     * Reset all input ports so presence semantics are fresh for the next tag.
     *
     * Default implementation does nothing. You can:
     * 1. Override this method to manually clear ports, OR
     * 2. Use ENABLE_AUTO_CLEAR_PORTS macro after defining your Ports struct
     *
     * The base class automatically calls both clearPorts() and clearInputPorts()
     * so either approach works.
     */
    virtual void clearPorts() {}

    /**
     * Heartbeat period in logical time.  Override to change the rate.
     * Default: 10 ms.
     *
     * @return Time interval between heartbeats
     */
    virtual LogicalTime heartbeatDuration() const
    {
        return LogicalTime(10'000'000);   // 10 ms
    }

protected:
    /**
     * Process pending logical actions queued from external threads.
     *
     * This method is called at the beginning of each heartbeat to process
     * any logical actions that were queued via scheduleLogicalActionWithData()
     * from I/O adapter threads.
     *
     * Thread-safe: Yes (uses mutex to safely extract queued actions)
     *
     * @param tag Current logical tag for executing actions
     */
    void processPendingActions(const LogicalTag& tag)
    {
        std::vector<PendingAction> actions;
        {
            std::lock_guard<std::mutex> lock(mActionQueueMutex);
            actions.swap(mPendingActions);
        }

        for (auto& action : actions)
        {
            // Allow derived class to process action data before executing action
            // This enables transfer of action.data to input ports
            processActionData(action.name, action.data);

            // Execute the logical action
            executeLogicalAction(tag, action.name);
        }
    }

    /**
     * Process action data (e.g., write to input ports).
     *
     * This virtual method is called for each pending action before executeLogicalAction().
     * Override this in derived classes to transfer action.data to appropriate input ports.
     *
     * Default implementation is a no-op.
     *
     * @param action_name Name of the action
     * @param action_data Type-erased data payload (use std::any_cast to extract)
     */
    virtual void processActionData(const std::string& action_name, const std::any& action_data)
    {
        // Default: no-op
        (void)action_name;
        (void)action_data;
    }

private:
    /**
     * Pending action data structure.
     *
     * Stores action name and associated data payload for actions
     * queued from external threads.
     */
    struct PendingAction
    {
        std::string name;  ///< Action name
        std::any data;     ///< Action data payload (type-erased)
    };

    Store mStore;        ///< Reactor's mutable state
    Ports mPorts;        ///< Input and output port collection
    Container mContainer; ///< Dependency injection container

    size_t            mReactorId{0};       ///< Unique reactor identifier
    ReactorScheduler* mScheduler{nullptr}; ///< Scheduler managing this reactor

    std::mutex mActionQueueMutex;           ///< Mutex protecting pending action queue
    std::vector<PendingAction> mPendingActions; ///< Queue of pending actions from external threads
};

/**
 * MicroServiceFSMReactor - Adds FSM support to MicroServiceReactor
 *
 * Extends MicroServiceReactor with FSM state machine functionality.
 * Combines port-based I/O with traditional FSM state transitions.
 *
 * ARCHITECTURAL ENFORCEMENT:
 * - doHeartbeat() and executeLogicalAction() are FINAL and cannot be overridden
 * - FSM states must use signature: step(Store&, Ports&, Container&, LogicalTag&, StepTrigger&)
 * - This enforces FSM-driven patterns where states coordinate Store + Ports
 *
 * Template Parameters:
 * - Name, Store, Ports, Container: Same as MicroServiceReactor
 * - StateSet: Set of FSM states
 *
 * Usage:
 *   class MyReactor : public MicroServiceFSMReactor<Name, Store, Ports,
 *                                                    Container, States> {
 *   public:
 *       using Base = MicroServiceFSMReactor<...>;
 *       using Base::Base;
 *
 *       // Optionally override doPeriodicMaintenance() for non-business-logic periodic work
 *   };
 */
template<const char* Name,
         typename StoreType,
         typename PortsType,
         typename ContainerType,
         typename StateSetType>
class MicroServiceFSMReactor : public MicroServiceReactor<Name, StoreType, PortsType,
                                                        ContainerType, void>
{
public:
    using Base = MicroServiceReactor<Name, StoreType, PortsType, ContainerType, void>;
    using States = StateSetType;
    using Store = StoreType;
    using Ports = PortsType;
    using Container = ContainerType;

    /**
     * Inherit constructors from base class.
     */
    using Base::Base;

    /**
     * Execute heartbeat logic through FSM (FINAL - cannot be overridden)
     *
     * This enforces the FSM-driven pattern:
     * 1. Processes any pending logical actions from I/O adapters
     * 2. Invokes FSM state step() with StepTrigger::heartbeat()
     * 3. Clears input ports after FSM step
     * 4. Calls doPeriodicMaintenance() for optional non-business-logic work
     *
     * @param tag Current logical tag
     */
    void doHeartbeat(const LogicalTag& tag) final override
    {
        // Process any pending actions queued from I/O adapters
        this->processPendingActions(tag);

        // Run FSM step with heartbeat trigger
        auto trigger = StepTrigger::heartbeat();
        mStateMachine.step(this->getStore(), this->getPorts(), this->getContainer(), tag, trigger);

        // Periodic maintenance hook (for metrics, logging, etc. - NOT business logic)
        doPeriodicMaintenance(tag);
    }

    /**
     * Execute logical action through FSM (FINAL - cannot be overridden)
     *
     * This enforces the FSM-driven pattern for event-driven reactions.
     * Invokes FSM state step() with StepTrigger::logicalAction(action_name).
     *
     * @param tag Current logical tag
     * @param action_name Name of the logical action to execute
     */
    void executeLogicalAction(const LogicalTag& tag, const std::string& action_name) final override
    {
        // Run FSM step with logical action trigger
        auto trigger = StepTrigger::logicalAction(action_name);
        mStateMachine.step(this->getStore(), this->getPorts(), this->getContainer(), tag, trigger);
    }

    /**
     * Execute an input through the FSM (DEPRECATED - for backward compatibility)
     *
     * DEPRECATED: This old pattern is kept for migration purposes.
     * New code should use FSM states with StepTrigger instead.
     *
     * Dispatches to the current state's step() function for this input type.
     *
     * @tparam InputType Type of input to process
     * @param input Input to process through current state
     */
    template<typename InputType>
    void executeInput(InputType& input)
    {
        // Dispatch to state machine (old signature)
        // The state's step() function signature:
        // size_t step(Store& store, Ports& ports, const Container& c, InputType& input)
        mStateMachine.execute(this->getStore(), this->getPorts(), this->getContainer(), input);
    }

    /**
     * Get current FSM state index
     *
     * @return Index of the active state in the state set
     */
    size_t getCurrentState() const
    {
        return mStateMachine.activeState();
    }

protected:
    /**
     * Optional hook for periodic maintenance work (NOT business logic)
     *
     * Called after every heartbeat FSM step. Override to perform:
     * - Logging
     * - Metrics collection
     * - Database maintenance (vacuum, cleanup)
     * - Health checks
     *
     * DO NOT put business logic here! Business logic belongs in Store pure functions.
     *
     * @param tag Current logical tag
     */
    virtual void doPeriodicMaintenance([[maybe_unused]] const LogicalTag& tag)
    {
        // Default: nothing to do
    }

    StateSetType mStateMachine; ///< Finite state machine instance
};

/**
 * Helper macro for declaring a reactor name
 *
 * Usage:
 *   DECLARE_REACTOR_NAME(MyReactor);
 *   // Creates: NameMyReactor
 */
#define DECLARE_REACTOR_NAME(name) \
    inline constexpr char Name##name[] = #name

/**
 * Comprehensive macro for declaring a reactor with minimal boilerplate
 *
 * Usage:
 *   DEFINE_REACTOR(MyReactor, MyStore, MyPorts, MyContainer)
 *   {
 *       void doHeartbeat(const LogicalTag& tag) override {
 *           // Your heartbeat logic
 *       }
 *
 *       void clearPorts() override {
 *           mPorts.input_port.clear();
 *       }
 *   };
 *
 * This macro automatically:
 * - Declares the reactor name constant
 * - Creates the reactor class inheriting from MicroServiceReactor
 * - Sets up the Base typedef
 * - Inherits constructors
 * - Provides access to mStore, mPorts, mContainer
 */
#define DEFINE_REACTOR(ReactorName, StoreType, PortsType, ContainerType)         \
    DECLARE_REACTOR_NAME(ReactorName);                                            \
    class ReactorName : public services::MicroServiceReactor<                     \
        Name##ReactorName, StoreType, PortsType, ContainerType>                   \
    {                                                                              \
    public:                                                                        \
        using Base = services::MicroServiceReactor<Name##ReactorName,             \
            StoreType, PortsType, ContainerType>;                                 \
        using Base::Base;                                                          \
        using Store = StoreType;                                                   \
        using Ports = PortsType;                                                   \
        using Container = ContainerType;                                           \
    private:

/**
 * Helper macro to execute a reaction with automatic instantiation
 *
 * Usage in doHeartbeat():
 *   EXECUTE_REACTION(HeartbeatReaction, HeartbeatInput);
 *
 * Expands to:
 *   {
 *       HeartbeatInput input;
 *       HeartbeatReaction reaction;
 *       reaction.execute(mStore, mPorts, mContainer, input);
 *   }
 */
#define EXECUTE_REACTION(ReactionType, InputType)                                 \
    do {                                                                           \
        InputType input;                                                           \
        ReactionType reaction;                                                     \
        reaction.execute(this->mStore, this->mPorts, this->mContainer, input);   \
    } while(0)

/**
 * Simplified reaction declaration for common case (no effects/dependencies)
 *
 * Usage:
 *   SIMPLE_REACTION(MyReactor, 0, HeartbeatInput)
 *   {
 *       void execute(MyStore& store, MyPorts& ports,
 *                    const Container& c, HeartbeatInput& input) {
 *           // reaction body
 *       }
 *   };
 */
#define SIMPLE_REACTION(ReactorType, Index, TriggerType)                          \
    struct ReactorType##Reaction##Index : public services::Reaction<              \
        ReactorType, Index, services::TypeList<TriggerType>,                      \
        services::TypeList<>, services::TypeList<>>

/**
 * Comprehensive macro for declaring an FSM reactor with minimal boilerplate
 *
 * Usage:
 *   DEFINE_FSM_REACTOR(MyFSMReactor, MyStore, MyPorts, MyContainer, MyStates)
 *   {
 *       void doHeartbeat(const LogicalTag& tag) override {
 *           // Your heartbeat logic - can dispatch inputs to FSM
 *       }
 *   };
 *
 * This macro automatically:
 * - Declares the reactor name constant
 * - Creates the FSM reactor class inheriting from MicroServiceFSMReactor
 * - Sets up the Base typedef
 * - Inherits constructors
 * - Provides access to mStore, mPorts, mContainer, mStateMachine
 */
#define DEFINE_FSM_REACTOR(ReactorName, StoreType, PortsType, ContainerType, StateSetType) \
    DECLARE_REACTOR_NAME(ReactorName);                                                      \
    class ReactorName : public services::MicroServiceFSMReactor<                            \
        Name##ReactorName, StoreType, PortsType, ContainerType, StateSetType>               \
    {                                                                                        \
    public:                                                                                  \
        using Base = services::MicroServiceFSMReactor<Name##ReactorName,                    \
            StoreType, PortsType, ContainerType, StateSetType>;                             \
        using Base::Base;                                                                    \
        using Store = StoreType;                                                             \
        using Ports = PortsType;                                                             \
        using Container = ContainerType;                                                     \
        using States = StateSetType;                                                         \
    private:

/**
 * Helper macro to execute an input through the FSM
 *
 * Usage in doHeartbeat():
 *   if (ports.command_in.is_present()) {
 *       EXECUTE_FSM_INPUT(ports.command_in.get());
 *   }
 *
 * Expands to:
 *   this->executeInput(input);
 */
#define EXECUTE_FSM_INPUT(input) \
    this->executeInput(input)

} // namespace services
