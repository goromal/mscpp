#pragma once
#include <boost/circular_buffer.hpp>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
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

template<typename T>
class __threadsafe_circular_buffer final
{
public:
    explicit __threadsafe_circular_buffer(const unsigned long maxSize) noexcept : mRunning(true), mBuffer(maxSize) {}

    void push_back(T&& val) noexcept
    {
        {
            std::scoped_lock lock(mMutex);
            mBuffer.push_back(std::forward<T&&>(val));
        }
        mCondition.notify_one();
    }

    void push_back(const T& val) noexcept
    {
        {
            std::scoped_lock lock(mMutex);
            mBuffer.push_back(val);
        }
        mCondition.notify_one();
    }

    bool push_back_if_not_full(T&& val) noexcept
    {
        {
            std::scoped_lock lock(mMutex);
            if (mBuffer.full())
            {
                return false;
            }
            mBuffer.push_back(std::forward<T&&>(val));
        }
        mCondition.notify_one();
        return true;
    }

    bool push_back_if_not_full(const T& val) noexcept
    {
        {
            std::scoped_lock lock(mMutex);
            if (mBuffer.full())
            {
                return false;
            }
            mBuffer.push_back(val);
        }
        mCondition.notify_one();
        return true;
    }

    void push_front(T&& val) noexcept
    {
        {
            std::scoped_lock lock(mMutex);
            mBuffer.push_front(std::forward<T&&>(val));
        }
        mCondition.notify_one();
    }

    void push_front(const T& val) noexcept
    {
        {
            std::scoped_lock lock(mMutex);
            mBuffer.push_front(val);
        }
        mCondition.notify_one();
    }

    bool push_front_if_not_full(T&& val) noexcept
    {
        {
            std::scoped_lock lock(mMutex);
            if (mBuffer.full())
            {
                return false;
            }
            mBuffer.push_front(std::forward<T&&>(val));
        }
        mCondition.notify_one();
        return true;
    }

    bool push_front_if_not_full(const T& val) noexcept
    {
        {
            std::scoped_lock lock(mMutex);
            if (mBuffer.full())
            {
                return false;
            }
            mBuffer.push_front(val);
        }
        mCondition.notify_one();
        return true;
    }

    bool drainUntil(std::function<bool(T&&)> checkFunc) noexcept
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mCondition.wait(lock, [=, this]() { return !this->mRunning || !this->mBuffer.empty(); });
        if (!this->mRunning)
        {
            lock.unlock();
            return false;
        }
        while (!mBuffer.empty())
        {
            if (!checkFunc(std::move(mBuffer[0])))
            {
                break;
            }
            mBuffer.pop_front();
        }
        lock.unlock();
        return true;
    }

    bool timedDrainUntil(std::function<bool(T&&)> checkFunc, const std::chrono::duration<double> timeLimit) noexcept
    {
        std::unique_lock<std::mutex> lock(mMutex);
        if (mCondition.wait_for(lock, timeLimit, [=, this]() { return !this->mRunning || !this->mBuffer.empty(); }))
        {
            if (!this->mRunning)
            {
                lock.unlock();
                return false;
            }
            while (!mBuffer.empty())
            {
                if (!checkFunc(std::move(mBuffer[0])))
                {
                    break;
                }
                mBuffer.pop_front();
            }
            lock.unlock();
            return true;
        }
        else
        {
            lock.unlock();
            return false;
        }
    }

    unsigned long size() noexcept
    {
        std::scoped_lock lock(mMutex);
        return mBuffer.size();
    }

    void clear() noexcept
    {
        std::scoped_lock lock(mMutex);
        mBuffer.clear();
    }

    void stop() noexcept
    {
        {
            std::scoped_lock lock(mMutex);
            mRunning = false;
        }
        mCondition.notify_all();
    }

    bool empty() noexcept
    {
        std::scoped_lock lock(mMutex);
        return mBuffer.empty();
    }

    __threadsafe_circular_buffer(const __threadsafe_circular_buffer&)            = delete;
    __threadsafe_circular_buffer& operator=(const __threadsafe_circular_buffer&) = delete;

private:
    bool                      mRunning;
    boost::circular_buffer<T> mBuffer;
    std::mutex                mMutex;
    std::condition_variable   mCondition;
};
