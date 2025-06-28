#pragma once

#include <cassert>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>

class BlockingQueueClosed : public std::exception
{
public:
    const char * what() const noexcept override
    {
        return "BlockingQueueClosed";
    }
};

template <typename T>
class BlockingQueue
{
    std::deque<T> queue;
    std::condition_variable cv;
    std::mutex mutex;
    bool closed = false;

public:
    void push(T&& item)
    {
        std::lock_guard lock(mutex);
        if (closed) {
            throw BlockingQueueClosed{};
        }
        queue.push_back(std::move(item));
        cv.notify_one();
    }

    void push(const T& item)
    {
        std::lock_guard lock(mutex);
        if (closed) {
            throw BlockingQueueClosed{};
        }
        queue.push_back(item);
        cv.notify_one();
    }

    template <typename Pred>
    std::optional<T> pop_if(Pred&& pred)
    {
        std::unique_lock lock(mutex);
        while (true)
        {
            cv.wait(lock, [&]() { return !queue.empty() || closed; });

            if (!queue.empty())
            {
                if (pred(queue.front()))
                {
                    T item = std::move(queue.front());
                    queue.pop_front();
                    return item;
                }
                // 如果队首元素不满足条件，但队列已关闭，我们不能再等了，
                // 因为不会有新元素了。直接返回空，让调用者知道没有符合条件的元素。
                if (closed) {
                    return {};
                }
            }
            else if (closed) // 队列为空，且已关闭
            {
                return {};
            }
            // 如果队列不为空但不满足条件，且未关闭，则循环继续，线程将重新等待
        }
    }

    T pop()
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&]() { return !queue.empty() || closed; });

        // 优先检查队列是否还有元素
        if (!queue.empty())
        {
            T item = std::move(queue.front());
            queue.pop_front();
            return item;
        }

        // 只有当队列为空，并且已经关闭时，才抛出异常
        // 这确保了队列会被完全“排干”(drained)
        assert(closed);
        throw BlockingQueueClosed{};
    }

    template <typename Pred>
    void drop_until(Pred&& pred)
    {
        std::unique_lock lock(mutex);
        while (!queue.empty() && !pred(queue.front()))
            queue.pop_front();
    }

    T peek()
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&]() { return !queue.empty() || closed; });

        if (queue.empty())
            throw BlockingQueueClosed{};

        return queue.front();
    }

    void close()
    {
        std::lock_guard lock(mutex);
        closed = true;
        cv.notify_all();
    }
};