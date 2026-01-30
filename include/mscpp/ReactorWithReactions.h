#pragma once

#include "Reaction.h"
#include "ReactionGraph.h"
#include "ReactorScheduler.h"
#include "LogicalTime.h"
#include "Logging.h"
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

/**
 * Reactor with Explicit Reactions
 *
 * This file provides reaction-based execution support for reactors.
 * Unlike the FSM-based approach, reactions explicitly declare their
 * triggers, effects, and dependencies, enabling:
 *
 * 1. Static dependency graph construction
 * 2. Topological ordering of reactions
 * 3. Deterministic execution within each tag
 * 4. Parallel execution of independent reactions
 *
 * Usage:
 *   - Define reactions using the Reaction template
 *   - Create a ReactorWithReactions with your ReactionSet
 *   - Register with ReactorSchedulerWithGraph
 *   - Scheduler executes reactions in dependency order
 */

namespace services
{

// Forward declare the global reactor ID counter from MicroService.h
#if REACTOR_MODE
std::atomic<size_t>& getGlobalReactorIdCounter();
#endif

/**
 * Extended IReactor interface that supports reaction-based execution
 */
class IReactorWithReactions : public IReactor
{
public:
    virtual ~IReactorWithReactions() = default;

    /**
     * Build this reactor's contribution to the dependency graph
     */
    virtual void buildDependencyGraph(ReactionGraph& graph) = 0;

    /**
     * Register reaction callbacks with the executor
     */
    virtual void registerReactionCallbacks(ReactionExecutor& executor) = 0;

    /**
     * Check if a specific reaction is triggered at this tag
     */
    virtual bool isReactionTriggered(size_t reaction_index, const LogicalTag& tag) = 0;

    /**
     * Execute a specific reaction
     */
    virtual void executeReaction(size_t reaction_index, const LogicalTag& tag) = 0;
};

/**
 * Reactor Scheduler with Dependency Graph Support
 *
 * Extends ReactorScheduler to use dependency graphs and execute
 * reactions in topological order.
 */
class ReactorSchedulerWithGraph : public ReactorScheduler
{
public:
    ReactorSchedulerWithGraph() = default;

    /**
     * Register a reactor with reactions
     */
    void registerReactorWithReactions(std::shared_ptr<IReactorWithReactions> reactor)
    {
        // Register with base scheduler
        registerReactor(reactor);

        // Build dependency graph
        reactor->buildDependencyGraph(mDependencyGraph);

        mReactorsWithReactions[reactor->getId()] = reactor;
    }

    /**
     * Initialize and compute topological order
     */
    void initialize()
    {
        LOG_INFO("ReactorSchedulerWithGraph: Building dependency graph");

        // Print graph for debugging
        mDependencyGraph.printGraph();

        // Compute topological order
        auto topOrder = mDependencyGraph.computeTopologicalOrder();

        LOG_INFO("ReactorSchedulerWithGraph: Topological order computed with {} levels",
                 topOrder.max_level);

        for (size_t level = 0; level < topOrder.levels.size(); level++)
        {
            LOG_INFO("  Level {}: {} reactions", level, topOrder.levels[level].size());
        }

        mTopologicalOrder = topOrder;
        mInitialized = true;
    }

    /**
     * Get the dependency graph
     */
    const ReactionGraph& getDependencyGraph() const
    {
        return mDependencyGraph;
    }

    /**
     * Get topological order
     */
    const TopologicalOrder& getTopologicalOrder() const
    {
        return mTopologicalOrder;
    }

private:
    ReactionGraph mDependencyGraph;
    TopologicalOrder mTopologicalOrder;
    std::unordered_map<size_t, std::shared_ptr<IReactorWithReactions>> mReactorsWithReactions;
    bool mInitialized{false};
};

/**
 * Base class for reactors that use explicit reactions
 *
 * Template Parameters:
 * - Name: Reactor name (compile-time string)
 * - Store: Reactor state type
 * - Container: Dependency injection container
 * - ReactionSetType: Set of reactions (ReactionSet<...>)
 */
template<const char* Name,
         typename Store,
         typename Container,
         typename ReactionSetType>
class ReactorWithReactions : public IReactorWithReactions
{
public:
    using Reactions = ReactionSetType;

    static constexpr const char* name()
    {
        return Name;
    }

    ReactorWithReactions()
    {
        mReactorId = getGlobalReactorIdCounter().fetch_add(1);
    }

    ReactorWithReactions(const Container& container)
        : mContainer(container)
    {
        mReactorId = getGlobalReactorIdCounter().fetch_add(1);
    }

    virtual ~ReactorWithReactions() = default;

    // IReactor interface
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
        std::scoped_lock lock(mMutex);
        initStore(mStore);
        mRunning = true;

        LOG_INFO("{}: Initialized with {} reactions", getName(), ReactionSetType::size);
    }

    void executeHeartbeat(const LogicalTag& tag) override
    {
        // Heartbeat can be a special reaction or handled separately
        // For now, we'll handle it as a special case

        LOG_TRACE("{}: Heartbeat at tag {}", getName(), tag);

        // Schedule next heartbeat
        if (mScheduler && mRunning)
        {
            auto heartbeatDuration = getHeartbeatDuration();
            LogicalTag nextTag = tag.advance_time(heartbeatDuration);

            mScheduler->scheduleEvent(nextTag, mReactorId, [this, nextTag]() {
                this->executeHeartbeat(nextTag);
            }, getName() + " heartbeat");
        }
    }

    bool hasPendingInputs() const override
    {
        // For reaction-based reactors, we don't use input buffers
        // Inputs are scheduled directly as events
        return false;
    }

    bool processNextInput(const LogicalTag& tag) override
    {
        // Not used in reaction-based model
        return false;
    }

    // IReactorWithReactions interface
    void buildDependencyGraph(ReactionGraph& graph) override
    {
        // Register all reactions in this reactor
        buildDependencyGraphImpl(graph, std::make_index_sequence<ReactionSetType::size>{});
    }

    void registerReactionCallbacks(ReactionExecutor& executor) override
    {
        // Register callbacks for all reactions
        registerCallbacksImpl(executor, std::make_index_sequence<ReactionSetType::size>{});
    }

    bool isReactionTriggered(size_t reaction_index, const LogicalTag& tag) override
    {
        // Check if reaction is triggered
        // For now, always return false (to be implemented based on input presence)
        return false;
    }

    void executeReaction(size_t reaction_index, const LogicalTag& tag) override
    {
        // Execute the specified reaction
        executeReactionImpl(reaction_index, tag, std::make_index_sequence<ReactionSetType::size>{});
    }

    void setScheduler(ReactorScheduler* scheduler)
    {
        mScheduler = scheduler;
    }

    const Store& readStore() const
    {
        std::scoped_lock lock(mMutex);
        return mStore;
    }

protected:
    virtual void initStore(Store&) {}

    virtual std::chrono::nanoseconds getHeartbeatDuration() const
    {
        return std::chrono::milliseconds(100);  // Default 100ms
    }

private:
    size_t mReactorId{0};
    Store mStore;
    Container mContainer;
    ReactionSetType mReactions;
    ReactorScheduler* mScheduler{nullptr};
    std::atomic<bool> mRunning{false};
    mutable std::mutex mMutex;

    // Compile-time iteration to build dependency graph
    template<size_t... Is>
    void buildDependencyGraphImpl(ReactionGraph& graph, std::index_sequence<Is...>)
    {
        // Register each reaction
        (registerReactionInGraph<Is>(graph), ...);

        // Add dependencies
        (addReactionDependencies<Is>(graph), ...);
    }

    template<size_t I>
    void registerReactionInGraph(ReactionGraph& graph)
    {
        using ReactionType = std::tuple_element_t<I, typename ReactionSetType::AllReactions::tuple>;

        std::string reaction_id = getName() + "::" + std::to_string(I);
        size_t global_index = graph.addReaction(reaction_id, mReactorId, I);

        mReactionGlobalIndices[I] = global_index;
    }

    template<size_t I>
    void addReactionDependencies(ReactionGraph& graph)
    {
        // Dependencies are declared in the Reaction type
        // For now, we skip adding explicit dependencies
        // In a full implementation, we'd inspect the Dependencies TypeList
    }

    template<size_t... Is>
    void registerCallbacksImpl(ReactionExecutor& executor, std::index_sequence<Is...>)
    {
        (registerCallback<Is>(executor), ...);
    }

    template<size_t I>
    void registerCallback(ReactionExecutor& executor)
    {
        size_t global_index = mReactionGlobalIndices[I];

        executor.registerReaction(global_index, [this]() {
            // Execute reaction I
            this->executeReactionAtIndex<I>();
        });
    }

    template<size_t I>
    void executeReactionAtIndex()
    {
        using ReactionType = std::tuple_element_t<I, typename ReactionSetType::AllReactions::tuple>;

        auto& reaction = std::get<I>(mReactions.mReactions);

        LOG_TRACE("{}: Executing reaction {}", getName(), I);

        // Execute reaction (signature depends on triggers)
        // For now, just call a placeholder
        // In full implementation, we'd dispatch based on available inputs
    }

    template<size_t... Is>
    void executeReactionImpl(size_t index, const LogicalTag& tag, std::index_sequence<Is...>)
    {
        ((index == Is ? (executeReactionAtIndex<Is>(), 0) : 0), ...);
    }

    std::unordered_map<size_t, size_t> mReactionGlobalIndices;  // local -> global index mapping
};

} // namespace services
