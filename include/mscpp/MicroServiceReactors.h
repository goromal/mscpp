#pragma once

#include "Ports.h"
#include "Reaction.h"
#include "ReactorScheduler.h"
#include "MicroServiceContainer.h"
#include <string>
#include <memory>

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
     * Check if reactor has pending inputs (queue-based model).
     *
     * @return false (port-based reactors don't use input queues)
     */
    bool hasPendingInputs() const override { return false; } // ^^^^ why is this here if not used anywhere anymore?

    /**
     * Process next queued input (queue-based model).
     *
     * @param tag Current logical tag
     * @return false (port-based reactors don't use input queues) // ^^^^ as in inherently can't??
     */ // ^^^^ doesn't that limit processing speed, though?
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

    // ── Accessors ────────────────────────────────────────────────────

    /**
     * Get mutable reference to reactor state.
     *
     * @return Reference to state store
     */
    Store& getStore() { return mStore; } // ^^^^ hm bad idea?

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
    Store mStore;        ///< Reactor's mutable state
    Ports mPorts;        ///< Input and output port collection
    Container mContainer; ///< Dependency injection container

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

private:
    size_t            mReactorId{0};       ///< Unique reactor identifier
    ReactorScheduler* mScheduler{nullptr}; ///< Scheduler managing this reactor
};

/**
 * MicroServiceFSMReactor - Adds FSM support to MicroServiceReactor
 *
 * Extends MicroServiceReactor with FSM state machine functionality.
 * Combines port-based I/O with traditional FSM state transitions.
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
 *       // FSM states can access ports via getPorts()
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
     * Execute an input through the FSM
     *
     * Dispatches to the current state's step() function for this input type.
     * State step functions can access ports via this->getPorts().
     *
     * @tparam InputType Type of input to process
     * @param input Input to process through current state
     */
    template<typename InputType>
    void executeInput(InputType& input)
    {
        // Dispatch to state machine
        // The state's step() function signature:
        // size_t step(Store& store, Ports& ports, const Container& c, InputType& input)
        mStateMachine.execute(this->mStore, this->mPorts, this->mContainer, input);
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
