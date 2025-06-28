#include "gtest/gtest.h"
#include "BlockingQueue.h" // 确保这个头文件名与您保存的文件一致
#include <algorithm> 
#include <thread>
#include <vector>
#include <chrono>
#include <numeric>
#include <future>
#include <atomic>

using namespace core;
using namespace std::chrono_literals;

// --- 测试套件 ---
class BlockingQueueTest : public ::testing::Test {
protected:
    BlockingQueue<int> queue;
};

// --- 测试用例 ---

// 1. 测试基本的 Push 和 Pop 操作 (单线程)
TEST_F(BlockingQueueTest, BasicPushAndPop) {
    queue.push(10);
    queue.push(20);
    EXPECT_EQ(queue.pop(), 10);
    EXPECT_EQ(queue.pop(), 20);
}

// 2. 测试移动语义的 Push
TEST_F(BlockingQueueTest, MovePush) {
    std::unique_ptr<int> item = std::make_unique<int>(100);
    BlockingQueue<std::unique_ptr<int>> uq_queue;
    uq_queue.push(std::move(item));
    std::unique_ptr<int> popped_item = uq_queue.pop();
    ASSERT_NE(popped_item, nullptr);
    EXPECT_EQ(*popped_item, 100);
}


// 3. 测试 peek() 函数
TEST_F(BlockingQueueTest, Peek) {
    queue.push(99);
    queue.push(101);
    EXPECT_EQ(queue.peek(), 99);
    // 确认 peek 不会移除元素
    EXPECT_EQ(queue.peek(), 99);
    queue.pop();
    EXPECT_EQ(queue.peek(), 101);
}

// 4. 测试 close() 机制
// 消费者线程应该在队列关闭且为空后正确退出
TEST_F(BlockingQueueTest, CloseUnblocksPop) {
    std::thread consumer([this]() {
        // pop() 在队列关闭时会抛出异常
        ASSERT_THROW(queue.pop(), BlockingQueueClosed);
    });

    // 等待一小会儿确保消费者线程已经进入等待状态
    std::this_thread::sleep_for(10ms);
    queue.close();
    consumer.join();
}

// 5. 测试向已关闭的队列 push 会抛出异常
TEST_F(BlockingQueueTest, PushToClosedQueueThrows) {
    queue.close();
    ASSERT_THROW(queue.push(1), BlockingQueueClosed);
}

// 6. 多生产者、单消费者并发测试
TEST_F(BlockingQueueTest, MultiProducerSingleConsumer) {
    std::vector<std::thread> producers;
    const int num_producers = 5;
    const int items_per_producer = 100;

    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([this, i, items_per_producer]() {
            for (int j = 0; j < items_per_producer; ++j) {
                // 每个生产者生产自己范围内的数字
                queue.push(i * items_per_producer + j);
            }
        });
    }

    std::vector<int> received_items;
    received_items.reserve(num_producers * items_per_producer);
    for (int i = 0; i < num_producers * items_per_producer; ++i) {
        received_items.push_back(queue.pop());
    }

    for (auto& t : producers) {
        t.join();
    }
    
    // 验证所有元素是否都已收到 (顺序不保证)
    std::sort(received_items.begin(), received_items.end());
    for (int i = 0; i < num_producers * items_per_producer; ++i) {
        ASSERT_EQ(received_items[i], i);
    }
}

// 7. 单生产者、多消费者并发测试
TEST_F(BlockingQueueTest, SingleProducerMultiConsumer) {
    const int num_consumers = 5;
    const int items_to_produce = 500;
    std::atomic<int> consumed_count = 0;

    std::thread producer([this, items_to_produce]() {
        for (int i = 0; i < items_to_produce; ++i) {
            queue.push(i);
        }
        // 生产完毕后关闭队列，以唤醒所有消费者
        queue.close();
    });

    std::vector<std::thread> consumers;
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([this, &consumed_count]() {
            while (true) {
                try {
                    queue.pop(); // 消费并丢弃
                    consumed_count++;
                } catch (const BlockingQueueClosed&) {
                    // 队列关闭，正常退出
                    break;
                }
            }
        });
    }

    producer.join();
    for (auto& t : consumers) {
        t.join();
    }

    // 验证所有生产的元素是否都被消费了
    EXPECT_EQ(consumed_count, items_to_produce);
}
 
 

// 10. 测试关闭后，pop() 会先清空剩余元素，然后才抛出异常
TEST_F(BlockingQueueTest, PopDrainsQueueBeforeThrowingWhenClosed) {
    // 1. 向队列中放入元素
    queue.push(1);
    queue.push(2);

    // 2. 关闭队列
    queue.close();

    // 3. 验证 pop() 会成功返回队列中剩余的元素
    EXPECT_EQ(queue.pop(), 1);
    EXPECT_EQ(queue.pop(), 2);

    // 4. 此时队列已空，再次调用 pop() 应该抛出异常
    ASSERT_THROW(queue.pop(), BlockingQueueClosed);
}