#include "gtest/gtest.h"
#include "BlockingQueue.h" // 确保这个头文件名与您保存的文件一致
#include <algorithm> 
#include <thread>
#include <vector>
#include <chrono>
#include <numeric>

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

// 8. 测试 pop_if 函数
TEST_F(BlockingQueueTest, PopIf) {
    queue.push(1); // 不满足条件
    queue.push(2); // 满足条件
    queue.push(3); // 不满足条件

    // pop_if 应该只弹出偶数
    auto result = queue.pop_if([](int val) { return val % 2 == 0; });
    
    // 这个测试在单线程下有点微妙，因为 pop_if 不是阻塞等待条件满足
    // 它只检查队首元素。为了更好地测试，我们需要在另一个线程中 pop_if
    BlockingQueue<int> q2;
    std::optional<int> res2;
    std::thread t([&](){
        // 等待一个偶数
        res2 = q2.pop_if([](int v){ return v % 2 == 0; });
    });

    q2.push(1);
    std::this_thread::sleep_for(10ms); // 给 t 一点时间处理 1
    q2.push(3);
    std::this_thread::sleep_for(10ms); // 给 t 一点时间处理 3
    q2.push(4); // 应该能弹出 4
    
    q2.close(); // 关闭以防万一
    t.join();

    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(res2.value(), 4);
}

// 9. 测试 drop_until 函数
TEST_F(BlockingQueueTest, DropUntil) {
    for (int i = 1; i <= 10; ++i) {
        queue.push(i);
    }

    // 丢弃所有小于 7 的元素
    queue.drop_until([](int val) { return val >= 7; });

    // 现在队首应该是 7
    EXPECT_EQ(queue.pop(), 7);
    EXPECT_EQ(queue.pop(), 8);
}

// 10. 测试关闭后，pop 会清空剩余元素再抛出异常
// 注意：这个测试依赖于 pop 的具体实现。根据您修改后的代码，pop() 在关闭后会直接抛异常。
// 因此我们测试这种行为。
TEST_F(BlockingQueueTest, PopThrowsImmediatelyWhenClosedWithItems) {
    queue.push(1);
    queue.push(2);
    queue.close();

    // 第一次 pop 就应该抛出异常
    ASSERT_THROW(queue.pop(), BlockingQueueClosed);
    // 第二次也一样
    ASSERT_THROW(queue.pop(), BlockingQueueClosed);
}