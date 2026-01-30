#pragma once

#include "Ports.h"
#include "Reaction.h"
#include "MicroServiceContainer.h"
#include <string>

/**
 * ReactorWithPorts - Template base class for reactors using port-based I/O
 *
 * Eliminates boilerplate by providing common functionality:
 * - Store accessor methods
 * - Ports accessor methods
 * - Name method
 * - Reaction execution support
 *
 * Template Parameters:
 * - Name: Reactor name (compile-time string constant)
 * - Store: Reactor state type
 * - Ports: Port collection type
 * - Container: Dependency injection container
 * - ReactionSet: Set of reactions (optional, for reaction-based model)
 *
 * Usage:
 *   class MyReactor : public ReactorWithPorts<NameMyReactor, StoreMyReactor,
 *                                              PortsMyReactor, ContainerType> {
 *   public:
 *       using Base = ReactorWithPorts<...>;
 *       using Base::Base;  // Inherit constructors
 *
 *       // Define reactions or FSM step functions
 *   };
 */

namespace services
{

template<const char* Name,
         typename StoreType,
         typename PortsType,
         typename ContainerType,
         typename ReactionSetType = void>
class ReactorWithPorts
{
public:
    using Store = StoreType;
    using Ports = PortsType;
    using Container = ContainerType;
    using Reactions = ReactionSetType;

    static constexpr const char* name() { return Name; }

    ReactorWithPorts()
        : mStore{}
        , mPorts{}
    {
    }

    ReactorWithPorts(const Container& container)
        : mStore{}
        , mPorts{}
        , mContainer(container)
    {
    }

    // Store accessors
    Store& getStore() { return mStore; }
    const Store& getStore() const { return mStore; }

    // Ports accessors
    Ports& getPorts() { return mPorts; }
    const Ports& getPorts() const { return mPorts; }

    // Container accessor
    const Container& getContainer() const { return mContainer; }

    /**
     * Clear all input ports (called by scheduler at end of tag)
     */
    void clearInputPorts()
    {
        clearInputPortsImpl(mPorts);
    }

protected:
    Store mStore;
    Ports mPorts;
    Container mContainer;

private:
    /**
     * Helper to clear input ports using compile-time iteration
     * This would need to be specialized for specific port structures
     */
    template<typename P>
    void clearInputPortsImpl(P& ports)
    {
        // Default: does nothing
        // Derived classes can override or we can use SFINAE to detect
        // and clear InputPort members
    }
};

/**
 * ReactorWithPortsAndFSM - Adds FSM support to ReactorWithPorts
 *
 * Extends ReactorWithPorts with FSM state machine functionality.
 * Combines port-based I/O with traditional FSM state transitions.
 *
 * Template Parameters:
 * - Name, Store, Ports, Container: Same as ReactorWithPorts
 * - StateSet: Set of FSM states
 *
 * Usage:
 *   class MyReactor : public ReactorWithPortsAndFSM<Name, Store, Ports,
 *                                                    Container, States> {
 *   public:
 *       using Base = ReactorWithPortsAndFSM<...>;
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
class ReactorWithPortsAndFSM : public ReactorWithPorts<Name, StoreType, PortsType,
                                                        ContainerType, void>
{
public:
    using Base = ReactorWithPorts<Name, StoreType, PortsType, ContainerType, void>;
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
 *   class MyReactor : public ReactorWithPorts<NameMyReactor, StoreMyReactor,
 *                                              PortsMyReactor, ContainerType> {
 *       using Base = ReactorWithPorts<...>;
 *       using Base::Base;
 *   };
 */
#define DECLARE_REACTOR_NAME(name) \
    inline constexpr char Name##name[] = #name

} // namespace services
