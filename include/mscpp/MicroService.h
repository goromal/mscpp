#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <pthread.h>
#include <thread>

class MicroService
{
public:
    MicroService() {}
    virtual ~MicroService() {}

    // using Container = ...

    MicroService(const MicroService&) = delete;
    MicroService& operator=(const MicroService&) = delete;
    MicroService(MicroService&&)                 = delete;
    MicroService& operator=(MicroService&&) = delete;

    virtual std::string name() const = 0;

    void run()
    {
        if (mRunning)
        {
            return;
        }

        preRun();

        mRunning    = true;
        mMainThread = std::thread([this]() {
            static constexpr int maxNameLength{15};
            pthread_setname_np(pthread_self(), this->name().substr(0, maxNameLength).c_str());
            mainLoop();
        });
    }

    void stop()
    {
        if (!mRunning)
        {
            return;
        }

        preStop();

        mRunning = false;
        if (mMainThread.joinable())
        {
            mMainThread.join();
        }
    }

    bool running()
    {
        return mRunning.load();
    }

protected:
    virtual void mainLoop() {}
    virtual void preRun() {}
    virtual void preStop() {}

    std::thread                     mMainThread;
    std::atomic_bool                mRunning{false};
    const std::chrono::milliseconds mDefaultSleep{100};
};
