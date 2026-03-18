#pragma once

#include "internal/utils.h"
#include "LogicalTime.h"
#include <type_traits>
#include <tuple>

namespace services
{

/**
 * Reaction Abstraction
 *
 * Provides explicit Reaction types that declare their triggers, effects,
 * and dependencies at compile-time.
 *
 * This enables:
 * - Static dependency graph construction
 * - Topological ordering of reactions
 * - Deterministic execution within each logical tag
 * - Parallel execution of independent reactions
 */

/**
 * TypeList - Compile-time list of types
 */
template<typename... Ts>
struct TypeList
{
    static constexpr size_t size = sizeof...(Ts);
    using tuple = std::tuple<Ts...>;  // Convert to tuple for std::tuple_element
};

/**
 * Empty type list
 */
using EmptyTypeList = TypeList<>;

/**
 * Check if a type is in a TypeList
 */
template<typename T, typename List>
struct Contains;

template<typename T>
struct Contains<T, TypeList<>>
{
    static constexpr bool value = false;
};

template<typename T, typename Head, typename... Tail>
struct Contains<T, TypeList<Head, Tail...>>
{
    static constexpr bool value = std::is_same_v<T, Head> || Contains<T, TypeList<Tail...>>::value;
};

/**
 * Concatenate two TypeLists
 */
template<typename List1, typename List2>
struct Concat;

template<typename... Ts1, typename... Ts2>
struct Concat<TypeList<Ts1...>, TypeList<Ts2...>>
{
    using type = TypeList<Ts1..., Ts2...>;
};

template<typename List1, typename List2>
using Concat_t = typename Concat<List1, List2>::type;

/**
 * Base Reaction Type
 *
 * Template Parameters:
 * - ReactorType: The reactor this reaction belongs to
 * - Index: Unique index for this reaction within the reactor
 * - TriggerList: TypeList of input types that trigger this reaction
 * - EffectList: TypeList of output types this reaction produces
 * - DependencyList: TypeList of other Reaction types this depends on
 *
 * Example:
 *   struct HeartbeatReaction : public Reaction<
 *       ServiceA,           // ReactorType
 *       0,                  // Index
 *       TypeList<HeartbeatInput>,  // Triggers
 *       TypeList<>,         // Effects (none)
 *       TypeList<>          // Dependencies (none)
 *   > {
 *       template<typename Store, typename Container>
 *       void execute(Store& store, Container& container, HeartbeatInput& input) {
 *           // Reaction implementation
 *       }
 *   };
 */
template<typename ReactorType,
         size_t Index,
         typename TriggerList = EmptyTypeList,
         typename EffectList = EmptyTypeList,
         typename DependencyList = EmptyTypeList>
struct Reaction
{
    using Reactor = ReactorType;
    using Triggers = TriggerList;
    using Effects = EffectList;
    using Dependencies = DependencyList;

    static constexpr size_t index = Index;

    /**
     * Get unique identifier for this reaction
     * Format: "ReactorName::ReactionIndex"
     */
    static std::string id()
    {
        return std::string(Reactor::name()) + "::" + std::to_string(Index);
    }

    /**
     * Check if this reaction is triggered by a given input type
     */
    template<typename InputType>
    static constexpr bool is_triggered_by()
    {
        return Contains<InputType, Triggers>::value;
    }

    /**
     * Check if this reaction produces a given effect type
     */
    template<typename EffectType>
    static constexpr bool produces()
    {
        return Contains<EffectType, Effects>::value;
    }

    /**
     * Check if this reaction depends on another reaction
     */
    template<typename OtherReaction>
    static constexpr bool depends_on()
    {
        return Contains<OtherReaction, Dependencies>::value;
    }
};

/**
 * ReactionSet - Collection of reactions for a reactor
 *
 * Similar to StateSet, but for reactions instead of states.
 * Stores all reactions and provides runtime dispatch based on input type.
 */
template<typename... Reactions>
class ReactionSet
{
public:
    std::tuple<Reactions...> mReactions;

    static constexpr size_t size = sizeof...(Reactions);

    /**
     * Get the index of a reaction type in the set
     */
    template<typename T>
    static constexpr size_t index()
    {
        return Index<T, std::tuple<Reactions...>>::value;
    }

    /**
     * Execute the appropriate reaction for a given input type
     *
     * Searches through all reactions at compile-time to find one
     * that is triggered by the input type, then executes it.
     */
    template<typename Store, typename Container, typename InputType>
    void execute(Store& store, Container& container, InputType& input)
    {
        executeImpl<0>(store, container, input);
    }

    /**
     * Get all reactions as a TypeList
     */
    using AllReactions = TypeList<Reactions...>;

private:
    template<size_t I, typename Store, typename Container, typename InputType>
    void executeImpl(Store& store, Container& container, InputType& input)
    {
        using CurrentReaction = std::tuple_element_t<I, std::tuple<Reactions...>>;

        if constexpr (CurrentReaction::template is_triggered_by<InputType>())
        {
            // Found a reaction that handles this input type
            auto& reaction = std::get<I>(mReactions);
            reaction.execute(store, container, input);
            return;
        }

        // Try next reaction
        if constexpr (I + 1 < sizeof...(Reactions))
        {
            executeImpl<I + 1>(store, container, input);
        }
        else
        {
            // No reaction found for this input type
            throw std::runtime_error("No reaction registered for input type");
        }
    }

    template<class T, class Tuple>
    struct Index;

    template<class T, class... Types>
    struct Index<T, std::tuple<T, Types...>>
    {
        static constexpr std::size_t value = 0;
    };

    template<class T, class U, class... Types>
    struct Index<T, std::tuple<U, Types...>>
    {
        static constexpr std::size_t value = 1 + Index<T, std::tuple<Types...>>::value;
    };
};

/**
 * Helper to create a ReactionSet from a TypeList
 */
template<typename TypeList>
struct MakeReactionSet;

template<typename... Reactions>
struct MakeReactionSet<TypeList<Reactions...>>
{
    using type = ReactionSet<Reactions...>;
};

template<typename TypeList>
using MakeReactionSet_t = typename MakeReactionSet<TypeList>::type;

/**
 * State-aware Reaction
 *
 * Extends Reaction to include state information for FSM-based reactors.
 * This allows mapping from old State::step() model to new Reaction model.
 *
 * Template Parameters:
 * - StateType: The FSM state this reaction belongs to
 * - (same as Reaction base class)
 */
template<typename StateType,
         typename ReactorType,
         size_t Index,
         typename TriggerList = EmptyTypeList,
         typename EffectList = EmptyTypeList,
         typename DependencyList = EmptyTypeList>
struct StateReaction : public Reaction<ReactorType, Index, TriggerList, EffectList, DependencyList>
{
    using State = StateType;

    /**
     * Get the state this reaction belongs to
     */
    static constexpr size_t state_index()
    {
        return StateType::index();
    }
};

} // namespace services
