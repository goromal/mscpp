#pragma once

#include "LogicalTime.h"
#include "Logging.h"
#include "internal/ThreadPool.h"
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace services
{

// Global reactor ID counter shared by all reactor base classes.
// Defined here (rather than in any single reactor header) so that
// ReactorWithPorts, ReactorWithReactions, etc. can all participate
// in the same ID space without circular includes.
inline std::atomic<size_t>& getGlobalReactorIdCounter()
{
    static std::atomic<size_t> nextId{0};
    return nextId;
}

/**
 * Phase 2: Centralized Reactor Scheduler
 *
 * Replaces per-service threads with a centralized tag-based event scheduler.
 * Events are processed in logical time order (tag order), ensuring deterministic
 * execution across all reactors.
 *
 * Key Properties:
 * - Tag-ordered event queue (priority queue sorted by LogicalTag)
 * - Batch processing: all events at same tag processed together
 * - Deterministic execution order within each tag
 * - Optional parallel execution via thread pool
 */

/**
 * A tagged event represents a scheduled reaction in a reactor.
 *
 * Events are ordered by their logical tag (time, microstep).
 * Multiple events at the same tag are processed in the order they
 * were enqueued (FIFO within tag).
 */
struct TaggedEvent
{
    LogicalTag tag;                            // When this event should execute
    size_t reactor_id;                         // Which reactor owns this event
    std::function<void()> reaction;            // The reaction to execute
    std::string debug_info;                    // For logging/debugging

    // Ordering: events compared by tag, then by reactor_id for determinism
    bool operator>(const TaggedEvent& other) const
    {
        if (tag != other.tag)
            return tag > other.tag;  // Earlier tags have higher priority
        return reactor_id > other.reactor_id;  // Deterministic tie-breaking
    }
};

/**
 * Interface for reactors managed by the scheduler.
 */
class IReactor
{
public:
    virtual ~IReactor() = default;

    virtual size_t getId() const = 0;
    virtual std::string getName() const = 0;

    // Initialize reactor state
    virtual void initialize() = 0;

    // Execute one heartbeat reaction at the given tag
    virtual void executeHeartbeat(const LogicalTag& tag) = 0;

    // Check if there are any pending inputs
    virtual bool hasPendingInputs() const = 0;

    // Process one input and return true if more inputs remain
    virtual bool processNextInput(const LogicalTag& tag) = 0;
};

/**
 * Centralized reactor scheduler with tag-based event processing.
 *
 * The scheduler maintains a priority queue of tagged events and processes
 * them in strict logical time order. This ensures deterministic execution:
 * given the same inputs with the same tags, the same reactions execute in
 * the same order, regardless of physical timing or thread scheduling.
 */
class ReactorScheduler
{
public:
    ReactorScheduler() = default;

    /**
     * Construct scheduler with optional thread pool for parallel execution.
     *
     * @param thread_pool Optional thread pool (nullptr for sequential execution)
     */
    explicit ReactorScheduler(std::shared_ptr<ThreadPool> thread_pool)
        : mThreadPool(std::move(thread_pool))
    {
    }

    ~ReactorScheduler()
    {
        if (mRunning)
        {
            stop();
        }
    }

    ReactorScheduler(const ReactorScheduler&) = delete;
    ReactorScheduler& operator=(const ReactorScheduler&) = delete;

    /**
     * Register a reactor with the scheduler.
     * Must be called before run().
     */
    void registerReactor(std::shared_ptr<IReactor> reactor)
    {
        if (mRunning)
        {
            throw std::runtime_error("Cannot register reactors while scheduler is running");
        }

        size_t id = reactor->getId();
        mReactors[id] = reactor;

        LOG_INFO("ReactorScheduler: Registered reactor {} (id={})", reactor->getName(), id);
    }

    /**
     * Schedule an event to execute at the given tag.
     */
    void scheduleEvent(const LogicalTag& tag, size_t reactor_id,
                      std::function<void()> reaction,
                      const std::string& debug_info = "")
    {
        std::scoped_lock lock(mMutex);

        TaggedEvent event{tag, reactor_id, std::move(reaction), debug_info};
        mEventQueue.push(event);

#if LOG_LOGICAL_TIME
        LOG_TRACE("ReactorScheduler: Scheduled event at tag {} for reactor {} ({})",
                  tag, reactor_id, debug_info);
#endif
    }

    /**
     * Start the scheduler with initial heartbeat events for all reactors.
     * Runs until stop() is called or event queue is empty.
     */
    void run()
    {
        if (mRunning)
        {
            return;
        }

        LOG_INFO("ReactorScheduler: Starting with {} reactors", mReactors.size());

        // Initialize all reactors
        for (auto& [id, reactor] : mReactors)
        {
            reactor->initialize();
        }

        mRunning = true;
        mCurrentTag = LogicalTag{LogicalTime(0), 0};

        // Schedule initial heartbeats for each reactor
        scheduleInitialHeartbeats();

        // Main scheduling loop runs on this thread
        mainLoop();
    }

    /**
     * Stop the scheduler gracefully.
     */
    void stop()
    {
        if (!mRunning)
        {
            return;
        }

        LOG_INFO("ReactorScheduler: Stopping at tag {}", getCurrentTag());
        mRunning = false;
    }

    /**
     * Check if scheduler is running.
     */
    bool running() const
    {
        return mRunning;
    }

    /**
     * Get current logical tag.
     */
    LogicalTag getCurrentTag() const
    {
        return LogicalTag{LogicalTime(mLogicalTimeNanos.load()), mLogicalMicrostep.load()};
    }

    /**
     * Get number of pending events in queue.
     */
    size_t pendingEventCount() const
    {
        std::scoped_lock lock(mMutex);
        return mEventQueue.size();
    }

    /**
     * Enable or disable parallel execution.
     * Only effective if thread pool was provided at construction.
     *
     * @param enabled True to enable parallel execution, false for sequential
     */
    void setParallelExecution(bool enabled)
    {
        mParallelExecutionEnabled = enabled && (mThreadPool != nullptr);
    }

    /**
     * Check if parallel execution is enabled.
     */
    bool isParallelExecutionEnabled() const
    {
        return mParallelExecutionEnabled && (mThreadPool != nullptr);
    }

    /**
     * Get the thread pool (if any).
     */
    std::shared_ptr<ThreadPool> getThreadPool() const
    {
        return mThreadPool;
    }

private:
    std::unordered_map<size_t, std::shared_ptr<IReactor>> mReactors;

    // Tag-ordered event queue (min-heap)
    std::priority_queue<TaggedEvent, std::vector<TaggedEvent>, std::greater<TaggedEvent>> mEventQueue;

    mutable std::mutex mMutex;
    std::atomic<bool> mRunning{false};

    LogicalTag mCurrentTag{LogicalTime(0), 0};
    std::atomic<uint64_t> mLogicalTimeNanos{0};
    std::atomic<uint32_t> mLogicalMicrostep{0};

    // Optional thread pool for parallel execution
    std::shared_ptr<ThreadPool> mThreadPool{nullptr};
    bool mParallelExecutionEnabled{false};

    /**
     * Schedule initial heartbeat events for all reactors at tag (0, 0).
     */
    void scheduleInitialHeartbeats()
    {
        LogicalTag initialTag{LogicalTime(0), 0};

        for (auto& [id, reactor] : mReactors)
        {
            scheduleEvent(initialTag, id, [reactor, initialTag]() {
                reactor->executeHeartbeat(initialTag);
            }, reactor->getName() + " initial heartbeat");
        }
    }

    /**
     * Main event processing loop.
     *
     * Algorithm:
     * 1. Dequeue all events at current tag
     * 2. Execute them (sequential or parallel based on configuration)
     * 3. Advance to next tag
     * 4. Repeat until stopped or queue empty
     */
    void mainLoop()
    {
        while (mRunning)
        {
            std::vector<TaggedEvent> eventsAtCurrentTag;

            {
                std::scoped_lock lock(mMutex);

                // Check if queue is empty
                if (mEventQueue.empty())
                {
                    LOG_INFO("ReactorScheduler: Event queue empty, stopping");
                    mRunning = false;
                    break;
                }

                // Peek at next event's tag
                LogicalTag nextTag = mEventQueue.top().tag;

                // Dequeue all events at this tag
                while (!mEventQueue.empty() && mEventQueue.top().tag == nextTag)
                {
                    eventsAtCurrentTag.push_back(mEventQueue.top());
                    mEventQueue.pop();
                }

                mCurrentTag = nextTag;
                mLogicalTimeNanos.store(mCurrentTag.time.count());
                mLogicalMicrostep.store(mCurrentTag.microstep);
            }

#if LOG_LOGICAL_TIME
            LOG_DEBUG("ReactorScheduler: Processing {} events at tag {}",
                     eventsAtCurrentTag.size(), getCurrentTag());
#endif

            // Execute events at this tag
            if (isParallelExecutionEnabled() && eventsAtCurrentTag.size() > 1)
            {
                executeEventsParallel(eventsAtCurrentTag);
            }
            else
            {
                executeEventsSequential(eventsAtCurrentTag);
            }
        }

        LOG_INFO("ReactorScheduler: Stopped at tag {}", getCurrentTag());
    }

    /**
     * Execute events sequentially in reactor ID order (deterministic).
     */
    void executeEventsSequential(std::vector<TaggedEvent>& events)
    {
        // Events are already sorted by reactor_id due to priority queue ordering
        for (auto& event : events)
        {
            if (!mRunning)
            {
                break;
            }

#if LOG_LOGICAL_TIME
            LOG_TRACE("ReactorScheduler: Executing event for reactor {} at tag {}: {}",
                     event.reactor_id, event.tag, event.debug_info);
#endif
            event.reaction();
        }
    }

    /**
     * Execute events in parallel using thread pool.
     *
     * NOTE: This parallel execution is at the TAG level, not the reaction level.
     * For true reaction-level parallelism, use ReactionExecutor::executeByLevelsParallel().
     *
     * This method executes independent events (different reactors) at the same tag
     * in parallel. Events for the same reactor are still sequential.
     */
    void executeEventsParallel(std::vector<TaggedEvent>& events)
    {
        if (!mThreadPool)
        {
            executeEventsSequential(events);
            return;
        }

        // Group events by reactor ID (events for same reactor must be sequential)
        std::unordered_map<size_t, std::vector<TaggedEvent*>> eventsByReactor;
        for (auto& event : events)
        {
            eventsByReactor[event.reactor_id].push_back(&event);
        }

        // If only one reactor, no parallelism possible
        if (eventsByReactor.size() == 1)
        {
            executeEventsSequential(events);
            return;
        }

        // Execute events for each reactor in parallel
        std::vector<std::future<void>> futures;
        futures.reserve(eventsByReactor.size());

        for (auto& [reactor_id, reactor_events] : eventsByReactor)
        {
            futures.push_back(mThreadPool->enqueue([this, reactor_events]() {
                for (TaggedEvent* event : reactor_events)
                {
                    if (!mRunning)
                    {
                        break;
                    }

#if LOG_LOGICAL_TIME
                    LOG_TRACE("ReactorScheduler: Executing event for reactor {} at tag {}: {}",
                             event->reactor_id, event->tag, event->debug_info);
#endif
                    event->reaction();
                }
            }));
        }

        // Wait for all reactors to complete (barrier synchronization)
        for (auto& future : futures)
        {
            future.get();  // Also propagates exceptions
        }
    }
};

} // namespace services
