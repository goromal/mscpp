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

    MicroServiceReactor()
        : mStore{}
        , mPorts{}
        , mContainer(__handle_later{})
    {
        mReactorId = getGlobalReactorIdCounter().fetch_add(1);
    }

    MicroServiceReactor(const Container& container)
        : mStore{}
        , mPorts{}
        , mContainer(container)
    {
        mReactorId = getGlobalReactorIdCounter().fetch_add(1);
    }

    virtual ~MicroServiceReactor() = default;

    // ── IReactor interface ───────────────────────────────────────────

    size_t getId() const override
    {
        return mReactorId;
    }

    std::string getName() const override
    {
        return std::string(Name);
    }

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
            clearPorts();

            LogicalTag next = tag.advance_time(heartbeatDuration());
            mScheduler->scheduleEvent(next, mReactorId, [this, next]() {
                this->executeHeartbeat(next);
            }, getName() + " heartbeat");
        }
    }

    bool hasPendingInputs() const override { return false; }
    bool processNextInput(const LogicalTag&) override { return false; }

    // ── Scheduler wiring ─────────────────────────────────────────────

    /**
     * Attach a scheduler.  Must be called before the scheduler's run().
     * When set, executeHeartbeat() will reschedule and clear ports.
     */
    void setScheduler(ReactorScheduler* scheduler)
    {
        mScheduler = scheduler;
    }

    // ── Accessors ────────────────────────────────────────────────────

    Store& getStore() { return mStore; }
    const Store& getStore() const { return mStore; }

    Ports& getPorts() { return mPorts; }
    const Ports& getPorts() const { return mPorts; }

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
    Store mStore;
    Ports mPorts;
    Container mContainer;

    // ── Pure virtuals for subclasses ─────────────────────────────────

    /**
     * Subclass heartbeat logic.  This is the only method you must override.
     * Write your reactions or FSM dispatch here.
     */
    virtual void doHeartbeat(const LogicalTag& tag) = 0;

    /**
     * Reset all input ports so presence semantics are fresh for the next tag.
     * Must clear every InputPort member in your Ports struct.
     * Default implementation does nothing — override this.
     */
    virtual void clearPorts() {}

    /**
     * Heartbeat period in logical time.  Override to change the rate.
     * Default: 10 ms.
     */
    virtual LogicalTime heartbeatDuration() const
    {
        return LogicalTime(10'000'000);   // 10 ms
    }

private:
    size_t            mReactorId{0};
    ReactorScheduler* mScheduler{nullptr};
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

    using Base::Base;  // Inherit constructors

    /**
     * Execute an input through the FSM
     *
     * Dispatches to the current state's step() function for this input type.
     * State step functions can access ports via this->getPorts().
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
     */
    size_t getCurrentState() const
    {
        return mStateMachine.activeState();
    }

protected:
    StateSetType mStateMachine;
};

/**
 * Helper macro for declaring a reactor with ports
 *
 * Usage:
 *   DECLARE_REACTOR_NAME(MyReactor);
 *
 *   struct StoreMyReactor { ... };
 *   struct PortsMyReactor { ... };
 *
 *   class MyReactor : public MicroServiceReactor<NameMyReactor, StoreMyReactor,
 *                                              PortsMyReactor, ContainerType> {
 *       using Base = MicroServiceReactor<...>;
 *       using Base::Base;
 *   };
 */
#define DECLARE_REACTOR_NAME(name) \
    inline constexpr char Name##name[] = #name

} // namespace services
