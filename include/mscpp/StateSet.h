#pragma once
#include "internal/utils.h"

namespace services
{

template<class S, size_t Index>
struct State
{
    using DerivedType = S;

    static constexpr size_t index()
    {
        return Index;
    }
};

template<typename... States>
class StateSet
{
public:
    size_t                mActiveState = 0; // Init state is the first one in the template list.
    std::tuple<States...> mStates;

    template<typename T>
    static constexpr size_t index()
    {
        return Index<T, std::tuple<States...>>::value;
    }

    void transition(const size_t& state)
    {
        mActiveState = state;
    }

    size_t activeState() const
    {
        return mActiveState;
    }

    /**
     * Execute an input on the active state (with ports support)
     *
     * Template Parameters:
     * - Store: State storage type
     * - Ports: Port collection type
     * - Container: Dependency injection container
     * - InputType: Input type to process
     *
     * Calls the active state's step(store, ports, container, input) method
     * and transitions to the returned state.
     */
    template<typename Store, typename Ports, typename Container, typename InputType>
    void execute(Store& store, Ports& ports, const Container& container, InputType& input)
    {
        size_t nextState = runOnActiveState([&](auto& state) {
            return state.step(store, ports, container, input);
        });
        transition(nextState);
    }

    /**
     * Execute an input on the active state (original signature without ports)
     *
     * For backward compatibility with existing FSM code that doesn't use ports.
     */
    template<typename Store, typename Container, typename InputType>
    void execute(Store& store, const Container& container, InputType& input)
    {
        size_t nextState = runOnActiveState([&](auto& state) {
            return state.step(store, container, input);
        });
        transition(nextState);
    }

    template<typename F>
    decltype(auto) runOnActiveState(F&& f)
    {
        return runOnActiveStateImpl<sizeof...(States) - 1>(std::forward<F>(f));
    }

    template<typename F>
    decltype(auto) runOnActiveState(F&& f) const
    {
        return runOnActiveStateImpl<sizeof...(States) - 1>(std::forward<F>(f));
    }

private:
    template<size_t I, typename F>
    decltype(auto) runOnActiveStateImpl(F&& f)
    {
        if (I == mActiveState)
        {
            return f(std::get<I>(mStates));
        }

        if constexpr (I > 0)
        {
            return runOnActiveStateImpl<I - 1>(std::forward<F>(f));
        }
        else
        {
            throw std::out_of_range("State index out of range");
        }
    }

    template<size_t I, typename F>
    decltype(auto) runOnActiveStateImpl(F&& f) const
    {
        if (I == mActiveState)
        {
            return f(std::get<I>(mStates));
        }

        if constexpr (I > 0)
        {
            return runOnActiveStateImpl<I - 1>(std::forward<F>(f));
        }
        else
        {
            throw std::out_of_range("State index out of range");
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

} // namespace services
