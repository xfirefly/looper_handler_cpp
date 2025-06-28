#pragma once

#include <cassert>
#include <condition_variable> // 用于线程间的等待和通知
#include <deque>              // 双端队列，用作底层容器，头尾操作高效
#include <exception>          // 用于定义自定义异常
#include <mutex>              // 互斥锁，用于保护共享数据
#include <optional>           // (如果 pop 支持超时，则可能需要)

namespace core {

/**
 * @class BlockingQueueClosed
 * @brief 当队列被关闭后，尝试在空队列上进行操作时抛出的异常。
 */
class BlockingQueueClosed : public std::exception
{
public:
    const char * what() const noexcept override
    {
        return "BlockingQueueClosed";
    }
};

/**
 * @class SPSC_BlockingQueue
 * @brief 一个为“单生产者、单消费者”场景优化的线程安全阻塞队列。
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
    std::deque<T> queue;            // 底层的数据容器
    std::condition_variable cv;     // 条件变量，用于消费者等待和生产者的通知
    mutable std::mutex mutex;       // 互斥锁，保护对 `queue` 和 `closed` 标志的访问
    bool closed = false;            // 标记队列是否已关闭

public:
    /**
     * @brief (生产者) 向队列尾部添加一个元素 (移动语义)。
     * @param item 要添加的元素，通过右值引用传入。
     * @throw BlockingQueueClosed 如果队列已关闭，则抛出异常。
     */
    void push(T&& item)
    {
        // 1. 获取锁，保护队列
        std::lock_guard lock(mutex);
        
        // 2. 如果队列已关闭，不再接受新元素
        if (closed) {
            throw BlockingQueueClosed{};
        }
        
        // 3. 将元素移动到队列尾部
        queue.push_back(std::move(item));
        
        // 4. 通知一个正在等待的消费者线程。
        //    在SPSC模型中，最多只有一个消费者在等待，所以 notify_one 是最高效的选择。
        cv.notify_one();
    }

    /**
     * @brief (生产者) 向队列尾部添加一个元素 (拷贝语义)。
     * @param item 要添加的元素，通过常量引用传入。
     * @throw BlockingQueueClosed 如果队列已关闭，则抛出异常。
     */
    void push(const T& item)
    {
        std::lock_guard lock(mutex);
        if (closed) {
            throw BlockingQueueClosed{};
        }
        queue.push_back(item);
        cv.notify_one();
    }

    /**
     * @brief (消费者) 从队列头部取出一个元素。
     * * 如果队列为空，此方法会阻塞当前线程，直到有新元素被推入或队列被关闭。
     * @return T 取出的元素。
     * @throw BlockingQueueClosed 如果队列已关闭且为空，则抛出异常。
     */
    T pop()
    {
        std::unique_lock lock(mutex);
        
        // 1. 等待条件满足：队列不为空 或 队列已关闭。
        //    cv.wait 会原子地解锁 mutex 并让线程休眠。
        //    当被 notify_one 唤醒或伪唤醒时，它会重新锁住 mutex 并检查 lambda 条件。
        //    如果条件为真，则继续执行；否则，继续休眠。
        cv.wait(lock, [&]() { return !queue.empty() || closed; });

        // 2. 优先处理队列中的剩余元素，即使队列已标记为关闭。
        //    这确保了在关闭后，所有已入队的元素都能被消费（"排干"队列）。
        if (!queue.empty())
        {
            T item = std::move(queue.front());
            queue.pop_front();
            return item;
        }

        // 3. 如果代码执行到这里，意味着 lambda 条件中的 `!queue.empty()` 为 false，
        //    那么 `closed` 必定为 true。
        //    这表示队列已空且已关闭，这是消费者线程终止的信号。
        assert(closed);
        throw BlockingQueueClosed{};
    }

    /**
     * @brief (消费者) 查看队列头部的元素，但不将其移除。
     * * 如果队列为空，此方法会阻塞，行为与 pop() 类似。
     * @return T 队首元素的拷贝或引用（取决于T的类型）。
     * @throw BlockingQueueClosed 如果队列已关闭且为空，则抛出异常。
     */
    T peek()
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&]() { return !queue.empty() || closed; });

        if (queue.empty())
            throw BlockingQueueClosed{};

        return queue.front();
    }

    /**
     * @brief 关闭队列。
     *
     * 调用此方法后，任何 `push` 操作都会抛出异常。
     * 正在 `pop` 或 `peek` 上阻塞的消费者线程会被唤醒。
     */
    void close()
    {
        std::lock_guard lock(mutex);
        closed = true;
        
        // 通知唯一的消费者线程，使其从 cv.wait() 中醒来，
        // 检查到 closed == true 后退出等待。
        cv.notify_one();
    }
};

}