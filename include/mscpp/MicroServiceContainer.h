#pragma once
#include "internal/utils.h"

namespace services
{

template<typename... MicroServices>
class MicroServiceContainer : private __instance_wrapper<MicroServices>...
{
public:
    template<typename T>
    struct ContainsOrContainsParent
    {
        static constexpr bool value = __one_of_or_derived_from<T, MicroServices...>::value;
    };

    template<typename T>
    struct Contains
    {
        static constexpr bool value = __one_of<T, MicroServices...>::value;
    };

    template<typename T>
    struct ContainsParent
    {
        static constexpr bool value = __derived_from<T, MicroServices...>::value;
    };

    MicroServiceContainer(__handle_later) {}

    MicroServiceContainer(std::shared_ptr<MicroServices>... all)
    {
        unpack<MicroServices...>(all...);
    }

    template<typename T>
    const std::shared_ptr<T>& get() const
    {
        validateType<T>();
        return __instance_wrapper<T>::getInstance();
    }

    template<typename T>
    void set(std::shared_ptr<T> instance)
    {
        validateType<T>();
        return __instance_wrapper<T>::setInstance(instance);
    }

    template<typename T>
    size_t index()
    {
        validateType<T>();
        return __index_of<T, MicroServices...>::value;
    }

    size_t size()
    {
        return mNumMicroServices;
    }

    template<typename T>
    struct ContainsAllOf
    {
        static constexpr bool value = true;
    };

    template<typename T>
    struct ContainsAllOf<MicroServiceContainer<T>>
    {
        static constexpr bool value = __one_of_or_parent_of<T, MicroServices...>::value;
    };

    template<typename T, typename... Rest>
    struct ContainsAllOf<MicroServiceContainer<T, Rest...>>
    {
        static constexpr bool value =
            __one_of_or_parent_of<T, MicroServices...>::value && ContainsAllOf<MicroServiceContainer<Rest...>>::value;
    };

private:
    template<typename T>
    void validateType() const
    {
        static_assert(std::is_base_of<__instance_wrapper<T>, MicroServiceContainer<MicroServices...>>::value,
                      "Micro service type not found");
    }

    template<typename T, typename... Remaining>
    void unpack(std::shared_ptr<T> current, std::shared_ptr<Remaining>... remaining)
    {
        __instance_wrapper<T>::setInstance(current);
        unpack(remaining...);
    }
    template<typename T>
    void unpack(std::shared_ptr<T> current)
    {
        __instance_wrapper<T>::setInstance(current);
    }

    template<typename... Blank>
    void unpack()
    {
    }

    const size_t mNumMicroServices = sizeof...(MicroServices);
};

} // namespace services
