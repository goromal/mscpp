#pragma once

#include "ServiceFactory.h"
#include "ReactorScheduler.h"
#include "Logging.h"

#if !REACTOR_MODE
#error "ReactorFactory requires REACTOR_MODE to be enabled"
#endif

namespace services
{

/**
 * Phase 2: Reactor Factory with Centralized Scheduler
 *
 * Wraps ServiceFactory and adds reactor scheduler support.
 * Instead of each service running on its own thread, all reactors
 * are managed by a single ReactorScheduler that processes events
 * in deterministic logical time order.
 *
 * Usage:
 *   ReactorFactory<ServiceA, ServiceB> factory;
 *   factory.run();  // Runs until stopped or events exhausted
 *   factory.stop();
 */
template<typename... MicroServices>
class ReactorFactory
{
public:
    ReactorFactory() : mServiceFactory(__handle_later{})
    {
        LOG_INFO("ReactorFactory: Creating with {} reactors", sizeof...(MicroServices));

        // Register all reactors with the scheduler
        registerReactors<MicroServices...>(__place_holder<MicroServices>()...);
    }

    ~ReactorFactory()
    {
        stop();
    }

    ReactorFactory(const ReactorFactory&)            = delete;
    ReactorFactory& operator=(const ReactorFactory&) = delete;

    /**
     * Get a reactor by type.
     */
    template<typename T>
    std::shared_ptr<T> get() const
    {
        return mServiceFactory.template get<T>();
    }

    /**
     * Get the reactor scheduler.
     */
    ReactorScheduler& getScheduler()
    {
        return mScheduler;
    }

    /**
     * Start the reactor system.
     * Runs the scheduler until stopped or event queue is empty.
     * This call blocks until execution completes.
     */
    void run()
    {
        LOG_INFO("ReactorFactory: Starting reactor system");
        mScheduler.run();
        LOG_INFO("ReactorFactory: Reactor system stopped");
    }

    /**
     * Stop the reactor system.
     */
    void stop()
    {
        if (mScheduler.running())
        {
            LOG_INFO("ReactorFactory: Stopping reactor system");
            mScheduler.stop();
        }
    }

    /**
     * Check if scheduler is running.
     */
    bool running() const
    {
        return mScheduler.running();
    }

private:
    ServiceFactory<MicroServices...> mServiceFactory;
    ReactorScheduler mScheduler;

    /**
     * Register all reactors with the scheduler.
     */
    template<typename T, typename... Rest>
    void registerReactors([[maybe_unused]] const __place_holder<T>& var1, const __place_holder<Rest>&... var2)
    {
        auto reactor = mServiceFactory.template get<T>();

        // Set scheduler reference
        reactor->setScheduler(&mScheduler);

        // Register with scheduler
        mScheduler.registerReactor(reactor);

        registerReactors(var2...);
    }

    template<typename T>
    void registerReactors([[maybe_unused]] const __place_holder<T>& var1)
    {
        auto reactor = mServiceFactory.template get<T>();

        // Set scheduler reference
        reactor->setScheduler(&mScheduler);

        // Register with scheduler
        mScheduler.registerReactor(reactor);
    }
};

} // namespace services
