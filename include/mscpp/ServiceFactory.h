#pragma once
#include <iostream>
#include <tuple>

#include "MicroServiceContainer.h"
#include "internal/utils.h"
#include "internal/logger.h"

template<typename... Types>
struct __factory_helper;

template<typename... Types>
struct __factory_helper<__type_list<Types...>>
{
    using type = MicroServiceContainer<Types...>;
};

template<typename... MicroServices>
class ServiceFactory : private __instance_wrapper<MicroServices>...
{
public:
    template<typename T>
    using FactoryType = typename __factory_helper<typename __recurse_to_type<T, MicroServices...>::type>::type;

    ServiceFactory() : __instance_wrapper<MicroServices>()...
    {
        mslogSetLevel(LOG_WARN);
        mslogRun();
        validateDependencies<MicroServices...>(__place_holder<MicroServices>()...);
        createServices<MicroServices...>(__place_holder<MicroServices>()...);
        runServices<MicroServices...>(__place_holder<MicroServices>()...);
    }

    ServiceFactory(log_level_t logLevel) : __instance_wrapper<MicroServices>()...
    {
        mslogSetLevel(logLevel);
        mslogRun();
        validateDependencies<MicroServices...>(__place_holder<MicroServices>()...);
        createServices<MicroServices...>(__place_holder<MicroServices>()...);
        runServices<MicroServices...>(__place_holder<MicroServices>()...);
    }

    ~ServiceFactory()
    {
        stop();
    }

    ServiceFactory(const ServiceFactory&) = delete;
    ServiceFactory& operator=(const ServiceFactory&) = delete;

    template<typename T>
    std::shared_ptr<T> get() const
    {
        validateType<T>();
        return __instance_wrapper<T>::getInstance();
    }

    template<typename T>
    size_t index()
    {
        validateType<T>();
        return __index_of<T, MicroServices...>::value;
    }

    template<typename T>
    std::string name()
    {
        return get<T>()->name();
    }

    size_t size()
    {
        return mNumMicroServices;
    }

    void run()
    {
        runServices<MicroServices...>(__place_holder<MicroServices>()...);
    }

    void stop()
    {
        for (size_t i = 0; i < size(); i++)
        {
            stopServices<MicroServices...>(__place_holder<MicroServices>()...);
        }
        msLogStop();
    }

private:
    template<typename T>
    void validateType() const
    {
        static_assert(std::is_base_of<__instance_wrapper<T>, ServiceFactory<MicroServices...>>::value,
                      "Micro service type not found");
    }

    const size_t mNumMicroServices = sizeof...(MicroServices);

    template<typename T, typename... Rest>
    void validateDependencies([[maybe_unused]] const __place_holder<T>& var1, const __place_holder<Rest>&... var2)
    {
        typename T::Container container{__handle_later{}};
        static_assert(FactoryType<T>::template ContainsAllOf<typename T::Container>::value,
                      "Micro services specified in incorrect order");
        validateDependencies(var2...);
    }

    template<typename T>
    void validateDependencies([[maybe_unused]] const __place_holder<T>& var1)
    {
        static_assert(FactoryType<T>::template ContainsAllOf<typename T::Container>::value,
                      "Micro services specified in incorrect order");
        typename T::Container container{__handle_later{}};
    }

    template<typename T, typename... Rest>
    void createServices([[maybe_unused]] const __place_holder<T>& var1, const __place_holder<Rest>&... var2)
    {
        typename T::Container container{__handle_later{}};
        populateServices<T, typename T::Container, MicroServices...>(container, __place_holder<MicroServices>()...);
        __instance_wrapper<T>::setInstance(std::make_shared<T>(container));
        createServices(var2...);
    }

    template<typename T>
    void createServices([[maybe_unused]] const __place_holder<T>& var1)
    {
        typename T::Container container{__handle_later{}};
        populateServices<T, typename T::Container, MicroServices...>(container, __place_holder<MicroServices>()...);
        __instance_wrapper<T>::setInstance(std::make_shared<T>(container));
    }

    template<typename O, typename C, typename T, typename... Rest>
    typename std::enable_if<C::template ContainsOrContainsParent<T>::value, void>::type
    populateServices(C& container, [[maybe_unused]] const __place_holder<T>& var1, const __place_holder<Rest>&... var2)
    {
        setService<C, T>(container);
        populateServices<O, C, Rest...>(container, var2...);
    }

    template<typename O, typename C, typename T, typename... Rest>
    typename std::enable_if<!C::template ContainsOrContainsParent<T>::value, void>::type
    populateServices(C& container, [[maybe_unused]] const __place_holder<T>& var1, const __place_holder<Rest>&... var2)
    {
        populateServices<O, C, Rest...>(container, var2...);
    }

    template<typename O, typename C, typename T>
    typename std::enable_if<C::template ContainsOrContainsParent<T>::value, void>::type
    populateServices(C& container, [[maybe_unused]] const __place_holder<T>& var1)
    {
        setService<C, T>(container);
    }

    template<typename O, typename C, typename T>
    typename std::enable_if<!C::template ContainsOrContainsParent<T>::value, void>::type
    populateServices([[maybe_unused]] C& container, [[maybe_unused]] const __place_holder<T>& var1)
    {
    }

    template<typename C, typename T>
    typename std::enable_if<!C::template Contains<T>::value && C::template ContainsParent<T>::value, void>::type
    setService(C& container)
    {
        container.template set<typename T::Parent>(get<T>());
    }

    template<typename C, typename T>
    typename std::enable_if<C::template Contains<T>::value, void>::type setService(C& container)
    {
        container.template set<T>(get<T>());
    }

    template<typename T, typename... Rest>
    void runServices([[maybe_unused]] const __place_holder<T>& var1, const __place_holder<Rest>&... var2)
    {
        get<T>()->run();
        runServices<Rest...>(var2...);
    }

    template<typename T>
    void runServices([[maybe_unused]] const __place_holder<T>& var1)
    {
        get<T>()->run();
    }

    template<typename T, typename... Rest>
    void stopServices([[maybe_unused]] const __place_holder<T>& var1, const __place_holder<Rest>&... var2)
    {
        if (get<T>()->running())
        {
            if (!downstreamRunning(false, var2...))
            {
                get<T>()->stop();
            }
        }
        stopServices<Rest...>(var2...);
    }

    template<typename T>
    void stopServices([[maybe_unused]] const __place_holder<T>& var1)
    {
        if (get<T>()->running())
        {
            get<T>()->stop();
        }
    }

    template<typename T, typename... Rest>
    bool downstreamRunning(bool                                      foundRunning,
                           [[maybe_unused]] const __place_holder<T>& var1,
                           const __place_holder<Rest>&... var2)
    {
        bool orThisRunning = get<T>()->running() || foundRunning;
        return downstreamRunning<Rest...>(orThisRunning, var2...);
    }

    template<typename T>
    bool downstreamRunning(bool foundRunning, [[maybe_unused]] const __place_holder<T>& var1)
    {
        return get<T>()->running() || foundRunning;
    }
};
