#pragma once
#include <vector>
#include <cstddef>
#include <deque>
#include <utility>
#include <stdexcept>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#if defined(__GNUG__)
#include <cxxabi.h>
#include <cstdlib>
#include <memory>
inline std::string demangle(const char* name)
{
    int                                    status = 0;
    std::unique_ptr<char, void (*)(void*)> res{abi::__cxa_demangle(name, nullptr, nullptr, &status), std::free};
    return (status == 0) ? res.get() : name;
}
#else
inline std::string demangle(const char* name)
{
    return name;
}
#endif

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
class CircularBuffer
{
public:
    explicit CircularBuffer(size_t capacity) : mBuffer(capacity), mHead(0), mSize(0) {}

    // --- Capacity ---
    size_t size() const noexcept
    {
        return mSize;
    }
    size_t capacity() const noexcept
    {
        return mBuffer.size();
    }
    bool empty() const noexcept
    {
        return mSize == 0;
    }
    bool full() const noexcept
    {
        return mSize == mBuffer.size();
    }

    // --- Element Access ---
    T& operator[](size_t index)
    {
        return mBuffer[(mHead + index) % mBuffer.size()];
    }

    const T& operator[](size_t index) const
    {
        return mBuffer[(mHead + index) % mBuffer.size()];
    }

    T& front()
    {
        if (empty())
            throw std::runtime_error("CircularBuffer is empty");
        return mBuffer[mHead];
    }

    const T& front() const
    {
        if (empty())
            throw std::runtime_error("CircularBuffer is empty");
        return mBuffer[mHead];
    }

    T& back()
    {
        if (empty())
            throw std::runtime_error("CircularBuffer is empty");
        return mBuffer[(mHead + mSize - 1) % mBuffer.size()];
    }

    const T& back() const
    {
        if (empty())
            throw std::runtime_error("CircularBuffer is empty");
        return mBuffer[(mHead + mSize - 1) % mBuffer.size()];
    }

    // --- Modifiers ---
    void clear() noexcept
    {
        mHead = 0;
        mSize = 0;
    }

    void push_back(const T& value)
    {
        emplace_back(value);
    }

    void push_back(T&& value)
    {
        emplace_back(std::move(value));
    }

    void push_front(const T& value)
    {
        emplace_front(value);
    }

    void push_front(T&& value)
    {
        emplace_front(std::move(value));
    }

    void pop_back()
    {
        if (empty())
            throw std::runtime_error("CircularBuffer underflow on pop_back");
        --mSize;
    }

    void pop_front()
    {
        if (empty())
            throw std::runtime_error("CircularBuffer underflow on pop_front");
        mHead = (mHead + 1) % mBuffer.size();
        --mSize;
    }

private:
    std::vector<T> mBuffer;
    size_t         mHead;
    size_t         mSize;

    template<typename U>
    void emplace_back(U&& value)
    {
        if (mBuffer.empty())
            return;
        if (full())
        {
            mBuffer[(mHead + mSize) % mBuffer.size()] = std::forward<U>(value);
            mHead                                     = (mHead + 1) % mBuffer.size();
        }
        else
        {
            mBuffer[(mHead + mSize) % mBuffer.size()] = std::forward<U>(value);
            ++mSize;
        }
    }

    template<typename U>
    void emplace_front(U&& value)
    {
        if (mBuffer.empty())
            return;
        if (full())
        {
            mHead          = (mHead + mBuffer.size() - 1) % mBuffer.size();
            mBuffer[mHead] = std::forward<U>(value);
        }
        else
        {
            mHead          = (mHead + mBuffer.size() - 1) % mBuffer.size();
            mBuffer[mHead] = std::forward<U>(value);
            ++mSize;
        }
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

    bool empty() const noexcept
    {
        std::scoped_lock lock(mMutex);
        return mBuffer.empty();
    }

    // Try to pop front element without blocking
    // Returns true if an element was popped, false if buffer was empty
    bool try_pop_front(T& out) noexcept
    {
        std::scoped_lock lock(mMutex);
        if (mBuffer.empty())
        {
            return false;
        }
        out = std::move(mBuffer[0]);
        mBuffer.pop_front();
        return true;
    }

    __threadsafe_circular_buffer(const __threadsafe_circular_buffer&)            = delete;
    __threadsafe_circular_buffer& operator=(const __threadsafe_circular_buffer&) = delete;

private:
    bool                    mRunning;
    CircularBuffer<T>       mBuffer;
    mutable std::mutex              mMutex;  // Mutable for const methods
    std::condition_variable mCondition;
};
