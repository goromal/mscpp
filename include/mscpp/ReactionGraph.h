#pragma once

#include "Reaction.h"
#include "LogicalTime.h"
#include "Logging.h"
#include "internal/ThreadPool.h"
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <stdexcept>
#include <memory>
#include <future>

namespace services
{

/**
 * Reaction Dependency Graph
 *
 * Builds a dependency graph from Reaction declarations and computes
 * topological ordering for deterministic execution.
 *
 * The dependency graph ensures:
 * 1. Reactions execute in causal order (dependencies before dependents)
 * 2. Independent reactions can be identified for parallel execution
 * 3. Cycles are detected at compile-time or runtime
 */

/**
 * Dependency Graph Node
 *
 * Represents a single reaction in the dependency graph with its
 * dependencies and dependents.
 */
struct DependencyNode
{
    std::string reaction_id;           // Unique reaction identifier
    size_t reactor_id;                 // Which reactor owns this reaction
    size_t reaction_index;             // Index within reactor
    std::vector<size_t> dependencies;  // Indices of reactions this depends on
    std::vector<size_t> dependents;    // Indices of reactions that depend on this
    size_t level{0};                   // Level in DAG (for parallel execution)
};

/**
 * Topological Sort Result
 *
 * Contains the execution order and additional metadata for scheduling.
 */
struct TopologicalOrder
{
    std::vector<size_t> execution_order;  // Global indices in execution order
    std::vector<std::vector<size_t>> levels;  // Grouped by DAG level (for parallelism)
    size_t max_level{0};
};

/**
 * Reaction Dependency Graph
 *
 * Runtime representation of the compile-time dependency graph.
 * Built from Reaction metadata and used by the scheduler.
 */
class ReactionGraph
{
public:
    ReactionGraph() = default;

    /**
     * Add a reaction node to the graph
     */
    size_t addReaction(const std::string& reaction_id,
                      size_t reactor_id,
                      size_t reaction_index)
    {
        DependencyNode node;
        node.reaction_id = reaction_id;
        node.reactor_id = reactor_id;
        node.reaction_index = reaction_index;

        size_t global_index = mNodes.size();
        mNodes.push_back(node);
        mReactionIdToIndex[reaction_id] = global_index;

        LOG_TRACE("ReactionGraph: Added reaction {} (global_index={}, reactor_id={}, reaction_index={})",
                  reaction_id, global_index, reactor_id, reaction_index);

        return global_index;
    }

    /**
     * Add a dependency edge: from depends on to
     */
    void addDependency(size_t from_index, size_t to_index)
    {
        if (from_index >= mNodes.size() || to_index >= mNodes.size())
        {
            throw std::out_of_range("Invalid reaction index in dependency");
        }

        mNodes[from_index].dependencies.push_back(to_index);
        mNodes[to_index].dependents.push_back(from_index);

        LOG_TRACE("ReactionGraph: {} depends on {}",
                  mNodes[from_index].reaction_id,
                  mNodes[to_index].reaction_id);
    }

    /**
     * Add dependency by reaction ID
     */
    void addDependency(const std::string& from_id, const std::string& to_id)
    {
        auto from_it = mReactionIdToIndex.find(from_id);
        auto to_it = mReactionIdToIndex.find(to_id);

        if (from_it == mReactionIdToIndex.end() || to_it == mReactionIdToIndex.end())
        {
            throw std::runtime_error("Reaction ID not found in graph");
        }

        addDependency(from_it->second, to_it->second);
    }

    /**
     * Compute topological order using Kahn's algorithm
     *
     * Returns execution order where dependencies come before dependents.
     * Also computes DAG levels for parallel execution.
     */
    TopologicalOrder computeTopologicalOrder() const
    {
        TopologicalOrder result;

        if (mNodes.empty())
        {
            return result;
        }

        // Count in-degrees (number of dependencies)
        std::vector<size_t> in_degree(mNodes.size(), 0);
        std::vector<size_t> node_levels(mNodes.size(), 0);

        for (size_t i = 0; i < mNodes.size(); i++)
        {
            in_degree[i] = mNodes[i].dependencies.size();
        }

        // Initialize queue with reactions that have no dependencies
        std::vector<size_t> current_level;
        for (size_t i = 0; i < mNodes.size(); i++)
        {
            if (in_degree[i] == 0)
            {
                current_level.push_back(i);
                node_levels[i] = 0;
            }
        }

        size_t level = 0;

        while (!current_level.empty())
        {
            // Add current level to results
            result.levels.push_back(current_level);
            result.execution_order.insert(result.execution_order.end(),
                                         current_level.begin(),
                                         current_level.end());

            std::vector<size_t> next_level;

            // Process each reaction in current level
            for (size_t node_idx : current_level)
            {
                // For each dependent, decrease in-degree
                for (size_t dependent_idx : mNodes[node_idx].dependents)
                {
                    in_degree[dependent_idx]--;

                    // If all dependencies satisfied, add to next level
                    if (in_degree[dependent_idx] == 0)
                    {
                        next_level.push_back(dependent_idx);
                        node_levels[dependent_idx] = level + 1;
                    }
                }
            }

            current_level = std::move(next_level);
            level++;
        }

        result.max_level = level;

        // Check for cycles (if not all reactions were processed)
        if (result.execution_order.size() != mNodes.size())
        {
            throw std::runtime_error("Cycle detected in reaction dependency graph");
        }

        LOG_INFO("ReactionGraph: Computed topological order with {} reactions in {} levels",
                 result.execution_order.size(), result.max_level);

        return result;
    }

    /**
     * Get reaction node by index
     */
    const DependencyNode& getNode(size_t index) const
    {
        if (index >= mNodes.size())
        {
            throw std::out_of_range("Invalid reaction index");
        }
        return mNodes[index];
    }

    /**
     * Get global index by reaction ID
     */
    size_t getIndex(const std::string& reaction_id) const
    {
        auto it = mReactionIdToIndex.find(reaction_id);
        if (it == mReactionIdToIndex.end())
        {
            throw std::runtime_error("Reaction ID not found: " + reaction_id);
        }
        return it->second;
    }

    /**
     * Get number of reactions in graph
     */
    size_t size() const
    {
        return mNodes.size();
    }

    /**
     * Print graph for debugging
     */
    void printGraph() const
    {
        LOG_INFO("ReactionGraph with {} reactions:", mNodes.size());
        for (size_t i = 0; i < mNodes.size(); i++)
        {
            const auto& node = mNodes[i];
            std::string deps_str = "[";
            for (size_t dep : node.dependencies)
            {
                deps_str += mNodes[dep].reaction_id + ", ";
            }
            if (!node.dependencies.empty())
            {
                deps_str.pop_back();
                deps_str.pop_back();
            }
            deps_str += "]";

            LOG_INFO("  {} (level={}): depends on {}", node.reaction_id, node.level, deps_str);
        }
    }

private:
    std::vector<DependencyNode> mNodes;
    std::unordered_map<std::string, size_t> mReactionIdToIndex;
};

/**
 * Compile-time Reaction Graph Builder
 *
 * Template metaprogramming utilities to build dependency graphs at compile-time.
 * This is partially implemented; full compile-time topological sort is complex
 * and may be deferred to runtime.
 */

/**
 * Extract all reactions from a list of reactors
 */
template<typename... Reactors>
struct ExtractAllReactions;

template<>
struct ExtractAllReactions<>
{
    using type = TypeList<>;
};

template<typename Reactor, typename... Rest>
struct ExtractAllReactions<Reactor, Rest...>
{
    using RestReactions = typename ExtractAllReactions<Rest...>::type;
    using type = Concat_t<typename Reactor::Reactions, RestReactions>;
};

template<typename... Reactors>
using ExtractAllReactions_t = typename ExtractAllReactions<Reactors...>::type;

/**
 * Build runtime dependency graph from reaction metadata
 *
 * This function inspects Reaction types at runtime using their static
 * constexpr metadata to build the dependency graph.
 */
template<typename ReactionSet>
void buildReactionGraph(ReactionGraph& graph, size_t reactor_id)
{
    // This is a placeholder for runtime graph construction
    // Actual implementation will iterate through ReactionSet and register reactions
    // For now, we'll build graphs manually in the MicroService implementation
}

/**
 * Reaction Executor
 *
 * Executes reactions in topological order, respecting dependencies.
 * Used by the scheduler to process reactions at each logical tag.
 */
class ReactionExecutor
{
public:
    using ReactionCallback = std::function<void()>;

    ReactionExecutor(const ReactionGraph& graph)
        : mGraph(graph)
    {
        mTopologicalOrder = graph.computeTopologicalOrder();
    }

    /**
     * Register a reaction callback
     * Automatically resizes callback storage if needed for better performance.
     */
    void registerReaction(size_t global_index, ReactionCallback callback)
    {
        // Ensure vector is large enough
        if (global_index >= mCallbacks.size())
        {
            mCallbacks.resize(global_index + 1);
        }
        mCallbacks[global_index] = std::move(callback);
    }

    /**
     * Execute all registered reactions in topological order
     *
     * Currently single-threaded.
     * Parallel execution will be added in future.
     */
    void executeAll()
    {
        for (size_t index : mTopologicalOrder.execution_order)
        {
            if (index < mCallbacks.size() && mCallbacks[index])
            {
                mCallbacks[index]();
            }
        }
    }

    /**
     * Execute reactions level-by-level (sequential)
     *
     * Processes reactions level-by-level sequentially.
     * For parallel execution, use executeByLevelsParallel().
     */
    void executeByLevels()
    {
        for (const auto& level : mTopologicalOrder.levels)
        {
            for (size_t index : level)
            {
                if (index < mCallbacks.size() && mCallbacks[index])
                {
                    mCallbacks[index]();
                }
            }
        }
    }

    /**
     * Execute reactions level-by-level with parallel execution
     *
     * For each level in the dependency graph:
     * 1. Submit all reactions in the level to the thread pool
     * 2. Wait for all reactions to complete (barrier synchronization)
     * 3. Move to next level
     *
     * This ensures:
     * - Independent reactions (same level) execute in parallel
     * - Dependencies are respected (level ordering)
     * - Deterministic execution (same logical results as sequential)
     * - Exception propagation through futures
     *
     * @param pool Thread pool for parallel execution
     */
    void executeByLevelsParallel(ThreadPool& pool)
    {
        for (const auto& level : mTopologicalOrder.levels)
        {
            // Skip empty levels
            if (level.empty())
            {
                continue;
            }

            // Single reaction in level: execute directly (no parallelism overhead)
            if (level.size() == 1)
            {
                size_t index = level[0];
                if (index < mCallbacks.size() && mCallbacks[index])
                {
                    mCallbacks[index]();
                }
                continue;
            }

            // Multiple reactions: execute in parallel
            std::vector<std::future<void>> futures;
            futures.reserve(level.size());

            for (size_t index : level)
            {
                if (index < mCallbacks.size() && mCallbacks[index])
                {
                    // Capture callback by value to avoid dangling references
                    auto callback = mCallbacks[index];
                    futures.push_back(pool.enqueue([callback]() {
                        callback();
                    }));
                }
            }

            // Barrier: wait for all reactions in this level to complete
            for (auto& future : futures)
            {
                future.get();  // Also propagates exceptions
            }
        }
    }

    /**
     * Get topological order
     */
    const TopologicalOrder& getTopologicalOrder() const
    {
        return mTopologicalOrder;
    }

private:
    const ReactionGraph& mGraph;
    TopologicalOrder mTopologicalOrder;
    // Changed from unordered_map to vector for O(1) access and better cache locality
    // std::optional would be ideal, but std::function is already nullable
    std::vector<ReactionCallback> mCallbacks;
};

} // namespace services
