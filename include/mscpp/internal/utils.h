#pragma once
#include <memory>
#include <type_traits>

template<typename... T>
struct __place_holder
{
};

template<typename... Types>
struct __type_list
{
};

template<typename... Types>
struct __append_type_list
{
    using type = __type_list<Types...>;
};

template<typename T, typename... Types>
struct __append_type_list<T, __type_list<Types...>>
{
    using type = __type_list<T, Types...>;
};

template<typename T, typename N, typename... Types>
struct __recurse_to_type
{
    using type = typename __append_type_list<N, typename __recurse_to_type<T, Types...>::type>::type;
};

template<typename T, typename... Types>
struct __recurse_to_type<T, T, Types...>
{
    using type = __type_list<>;
};

template<typename...>
struct __one_of
{
    static constexpr bool value = false;
};

template<typename T, typename F, typename... Types>
struct __one_of<T, F, Types...>
{
    static constexpr bool value = std::is_same<T, F>::value || __one_of<T, Types...>::value;
};

template<typename...>
struct __one_of_or_derived_from
{
    static constexpr bool value = false;
};

template<typename T, typename F, typename... Types>
struct __one_of_or_derived_from<T, F, Types...>
{
    static constexpr bool value =
        std::is_same<T, F>::value || std::is_base_of<F, T>::value || __one_of_or_derived_from<T, Types...>::value;
};

template<typename...>
struct __one_of_or_parent_of
{
    static constexpr bool value = false;
};

template<typename T, typename F, typename... Types>
struct __one_of_or_parent_of<T, F, Types...>
{
    static constexpr bool value =
        std::is_same<T, F>::value || std::is_base_of<T, F>::value || __one_of_or_parent_of<T, Types...>::value;
};

template<typename...>
struct __derived_from
{
    static constexpr bool value = false;
};

template<typename T, typename F, typename... Types>
struct __derived_from<T, F, Types...>
{
    static constexpr bool value = std::is_base_of<F, T>::value || __derived_from<T, Types...>::value;
};

template<typename...>
struct __parent_of
{
    static constexpr bool value = false;
};

template<typename T, typename F, typename... Types>
struct __parent_of<T, F, Types...>
{
    static constexpr bool value = std::is_base_of<T, F>::value || __parent_of<T, Types...>::value;
};

template<typename...>
struct __index_of;

template<typename T, typename... R>
struct __index_of<T, T, R...> : std::integral_constant<size_t, 0>
{
};

template<typename T, typename F, typename... R>
struct __index_of<T, F, R...> : std::integral_constant<size_t, 1 + __index_of<T, R...>::value>
{
};

struct __handle_later
{
};

template<typename T>
class __instance_wrapper
{
public:
    __instance_wrapper() {}

private:
    std::shared_ptr<T> mInstance;

protected:
    const std::shared_ptr<T>& getInstance() const
    {
        return mInstance;
    }

    void setInstance(std::shared_ptr<T> instance)
    {
        if (mInstance || !instance)
        {
            return;
        }
        mInstance = std::move(instance);
    }
};
