#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <stdexcept>

namespace services
{

/**
 * Thread Pool for Parallel Execution
 *
 * A fixed-size thread pool for executing independent reactions in parallel.
 * Threads are created once at construction and reused across all tags and levels.
 *
 * Key Properties:
 * - Fixed thread count (typically hardware_concurrency)
 * - Work queue with task stealing semantics
 * - Barrier synchronization for level completion
 * - Exception propagation through std::future
 * - Clean shutdown with proper thread joining
 *
 * Usage Pattern:
 *   ThreadPool pool(4);  // 4 worker threads
 *
 *   // Submit tasks
 *   std::vector<std::future<void>> futures;
 *   futures.push_back(pool.enqueue([]() { // task 1 // }));
 *   futures.push_back(pool.enqueue([]() { // task 2 // }));
 *
 *   // Wait for completion (barrier)
 *   for (auto& fut : futures) {
 *       fut.get();  // Also propagates exceptions
 *   }
 */
class ThreadPool
{
public:
    /**
     * Construct thread pool with specified number of threads.
     *
     * @param num_threads Number of worker threads (defaults to hardware concurrency)
     */
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency())
        : mShutdown(false)
    {
        if (num_threads == 0)
        {
            num_threads = 1;  // At least one thread
        }

        mWorkers.reserve(num_threads);

        for (size_t i = 0; i < num_threads; ++i)
        {
            mWorkers.emplace_back([this]() {
                workerThread();
            });
        }
    }

    /**
     * Destructor: stop all threads and wait for completion.
     */
    ~ThreadPool()
    {
        shutdown();
    }

    // Non-copyable, non-movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * Enqueue a task for execution.
     *
     * @param task Function to execute (void() signature)
     * @return Future for synchronization and exception handling
     */
    template<typename F>
    std::future<void> enqueue(F&& task)
    {
        // Wrap task in a packaged_task for exception propagation
        auto packaged = std::make_shared<std::packaged_task<void()>>(std::forward<F>(task));
        std::future<void> result = packaged->get_future();

        {
            std::unique_lock<std::mutex> lock(mQueueMutex);

            if (mShutdown)
            {
                throw std::runtime_error("ThreadPool: Cannot enqueue on stopped pool");
            }

            mTaskQueue.emplace([packaged]() {
                (*packaged)();
            });
        }

        mCondition.notify_one();
        return result;
    }

    /**
     * Get number of worker threads.
     */
    size_t threadCount() const
    {
        return mWorkers.size();
    }

    /**
     * Get number of pending tasks in queue.
     */
    size_t pendingTaskCount() const
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        return mTaskQueue.size();
    }

    /**
     * Shutdown the thread pool gracefully.
     * Completes all pending tasks before returning.
     */
    void shutdown()
    {
        {
            std::unique_lock<std::mutex> lock(mQueueMutex);
            if (mShutdown)
            {
                return;  // Already shutdown
            }
            mShutdown = true;
        }

        mCondition.notify_all();

        for (std::thread& worker : mWorkers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

private:
    std::vector<std::thread> mWorkers;
    std::queue<std::function<void()>> mTaskQueue;

    mutable std::mutex mQueueMutex;
    std::condition_variable mCondition;
    std::atomic<bool> mShutdown;

    /**
     * Worker thread main loop.
     * Pulls tasks from queue and executes them until shutdown.
     */
    void workerThread()
    {
        while (true)
        {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(mQueueMutex);

                // Wait for task or shutdown
                mCondition.wait(lock, [this]() {
                    return mShutdown || !mTaskQueue.empty();
                });

                if (mShutdown && mTaskQueue.empty())
                {
                    return;  // Exit thread
                }

                if (!mTaskQueue.empty())
                {
                    task = std::move(mTaskQueue.front());
                    mTaskQueue.pop();
                }
            }

            // Execute task outside lock
            if (task)
            {
                task();
            }
        }
    }
};

} // namespace services
