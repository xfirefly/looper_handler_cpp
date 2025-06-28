#pragma once

#include <cassert>
#include <condition_variable> 
#include <deque>              
#include <exception>          
#include <mutex>              

namespace core {

class BlockingQueueClosed : public std::exception
{
public:
    const char * what() const noexcept override
    {
        return "BlockingQueueClosed";
    }
};

/**
 * @class BlockingQueue
 * @brief 一个为“单生产者、单消费者”(SPSC)场景优化的线程安全阻塞队列。
 *
 * @tparam T 队列中存储的元素类型。
 *
 * 该队列保证了在一个生产者线程和一个消费者线程之间进行数据交换时的线程安全。
 * 它不适用于多生产者或多消费者的场景。
 * - 生产者通过 `push` 向队列尾部添加元素。
 * - 消费者通过 `pop` 从队列头部取出元素。
 * - 如果队列为空，`pop` 操作会阻塞消费者线程，直到有新元素被推入或队列被关闭。
 */
template <typename T>
class BlockingQueue
{
private:
    std::deque<T> queue;            
    std::condition_variable cv;     
    mutable std::mutex mutex;       
    bool closed = false;            

public:
    void push(T&& item)
    {
        std::lock_guard lock(mutex);
        if (closed) {
            throw BlockingQueueClosed{};
        }
        queue.push_back(std::move(item));
        
        // 对于SPSC，只有一个消费者在等待，notify_one 是最高效的选择。
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

    T pop()
    {
        std::unique_lock lock(mutex);
        
        cv.wait(lock, [&]() { return !queue.empty() || closed; });

        if (!queue.empty())
        {
            T item = std::move(queue.front());
            queue.pop_front();
            return item;
        }

        assert(closed);
        throw BlockingQueueClosed{};
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
        
        // 对于SPSC，只有一个消费者需要被唤醒，所以 notify_one 足够且高效。
        cv.notify_one();
    }
};

} // namespace core