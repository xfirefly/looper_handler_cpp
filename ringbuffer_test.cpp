#include "gtest/gtest.h"
#include <thread>
#include <vector>
#include <numeric>
#include <atomic>
#include <chrono>

// 因为 ringbuffer 是 C 库，所以需要使用 extern "C"
extern "C" {
#include "ringbuffer.h"
}

// --- 测试夹具 (Test Fixture) ---
// 为每个测试提供一个干净的 ringbuffer 实例
class RingBufferTest : public ::testing::Test {
protected:
    ringbuffer_t* rb;
    const size_t buffer_size = 1024; // 选择一个 2 的幂，但 create 函数会处理

    void SetUp() override {
        // 在每个测试开始前创建一个 ringbuffer
        rb = ringbuffer_create(buffer_size);
        ASSERT_NE(rb, nullptr);
        // ringbuffer_create 会将大小向上舍入到下一个 2 的幂
        ASSERT_EQ(rb->size, 1024);
    }

    void TearDown() override {
        // 在每个测试结束后销毁 ringbuffer
        ringbuffer_destroy(rb);
    }
};

// --- 测试用例 ---

// 1. 测试创建和销毁
TEST_F(RingBufferTest, CreationAndDestruction) {
    ASSERT_NE(rb->buf, nullptr);
    EXPECT_EQ(rb->size, 1024);
    EXPECT_EQ(rb->size_mask, 1023);
    EXPECT_EQ(rb->read_ptr, 0);
    EXPECT_EQ(rb->write_ptr, 0);
}

// 2. 测试初始状态
TEST_F(RingBufferTest, InitialState) {
    EXPECT_TRUE(ringbuffer_is_empty(rb));
    EXPECT_EQ(ringbuffer_read_space(rb), 0);
    EXPECT_EQ(ringbuffer_write_space(rb), rb->size - 1);
}

// 3. 测试基本的写入和读取 (Put/Get)
TEST_F(RingBufferTest, BasicPutAndGet) {
    const char* test_data = "Hello, RingBuffer!";
    size_t data_len = strlen(test_data);

    // 写入数据
    size_t written = ringbuffer_put(rb, test_data, data_len);
    ASSERT_EQ(written, data_len);

    // 验证空间
    EXPECT_FALSE(ringbuffer_is_empty(rb));
    EXPECT_EQ(ringbuffer_read_space(rb), data_len);
    EXPECT_EQ(ringbuffer_write_space(rb), rb->size - 1 - data_len);

    // 读取数据
    char read_buf[100] = {0};
    size_t read_bytes = ringbuffer_get(rb, read_buf, data_len);
    ASSERT_EQ(read_bytes, data_len);

    // 验证数据
    EXPECT_STREQ(read_buf, test_data);
    EXPECT_TRUE(ringbuffer_is_empty(rb));
}

// 4. 测试边界：写满缓冲区
TEST_F(RingBufferTest, WriteToFull) {
    size_t write_size = ringbuffer_write_space(rb);
    std::vector<char> full_data(write_size, 'A');

    size_t written = ringbuffer_put(rb, full_data.data(), write_size);
    ASSERT_EQ(written, write_size);

    // 现在缓冲区应该是满的
    EXPECT_EQ(ringbuffer_write_space(rb), 0);
    EXPECT_EQ(ringbuffer_read_space(rb), write_size);

    // 尝试再次写入，应该失败（返回 0）
    written = ringbuffer_put(rb, "X", 1);
    EXPECT_EQ(written, 0);
}

// 5. 测试边界：从空缓冲区读取
TEST_F(RingBufferTest, ReadFromEmpty) {
    char read_buf[10] = {0};
    size_t read_bytes = ringbuffer_get(rb, read_buf, sizeof(read_buf));
    EXPECT_EQ(read_bytes, 0);
}

// 6. 测试指针回绕 (Wrap-around)
TEST_F(RingBufferTest, WrapAround) {
    size_t half_size = (rb->size - 1) / 2;
    std::vector<char> data_chunk(half_size, 'A');

    // 1. 写满，但不完全满
    ringbuffer_put(rb, data_chunk.data(), half_size);
    ASSERT_EQ(rb->write_ptr, half_size);

    // 2. 读出一部分，让 read_ptr 向前移动
    char temp_buf[100];
    ringbuffer_get(rb, temp_buf, 100);
    ASSERT_EQ(rb->read_ptr, 100);

    // 3. 写入更多数据，足以让 write_ptr 回绕
    size_t space_left = ringbuffer_write_space(rb);
    std::vector<char> wrap_data(space_left, 'B');
    ringbuffer_put(rb, wrap_data.data(), space_left);

    // 此时 write_ptr 应该小于 read_ptr
    EXPECT_LT(rb->write_ptr, rb->read_ptr);
    EXPECT_EQ(ringbuffer_write_space(rb), 0);

    // 4. 读取所有数据并验证
    std::vector<char> final_read_buf(ringbuffer_read_space(rb));
    ringbuffer_get(rb, final_read_buf.data(), final_read_buf.size());

    EXPECT_TRUE(ringbuffer_is_empty(rb));
}

// 7. 测试 Peek (窥视) vs Get (读取)
TEST_F(RingBufferTest, PeekVsGet) {
    const char* test_data = "peek_test";
    size_t data_len = strlen(test_data);
    ringbuffer_put(rb, test_data, data_len);

    char peek_buf[50] = {0};
    char get_buf[50] = {0};

    // 第一次 peek
    size_t peeked = ringbuffer_peek(rb, peek_buf, data_len);
    ASSERT_EQ(peeked, data_len);
    EXPECT_STREQ(peek_buf, test_data);
    EXPECT_EQ(ringbuffer_read_space(rb), data_len); // 空间没变

    // 第二次 peek，结果应该一样
    memset(peek_buf, 0, sizeof(peek_buf));
    peeked = ringbuffer_peek(rb, peek_buf, data_len);
    ASSERT_EQ(peeked, data_len);
    EXPECT_STREQ(peek_buf, test_data);

    // 现在 Get 数据
    size_t got = ringbuffer_get(rb, get_buf, data_len);
    ASSERT_EQ(got, data_len);
    EXPECT_STREQ(get_buf, test_data);
    EXPECT_TRUE(ringbuffer_is_empty(rb)); // Get 之后变空了
}

// 8. 测试 `*_advance` 函数
TEST_F(RingBufferTest, AdvanceFunctions) {
    // 写入一些数据但不读取
    ringbuffer_put(rb, "1234567890", 10);

    // 使用 advance 代替 get
    ringbuffer_read_advance(rb, 5);
    EXPECT_EQ(ringbuffer_read_space(rb), 5);

    // 使用 peek 验证数据是否正确
    char peek_buf[10] = {0};
    ringbuffer_peek(rb, peek_buf, 5);
    EXPECT_STREQ(peek_buf, "67890");

    // 测试 write_advance
    size_t space_before = ringbuffer_write_space(rb);
    ringbuffer_write_advance(rb, 10);
    size_t space_after = ringbuffer_write_space(rb);
    EXPECT_EQ(space_before - 10, space_after);
}

// 9. 测试 `*_vector` 函数
TEST_F(RingBufferTest, VectorFunctions) {
    ringbuffer_data_t vec[2];

    // 场景1: 线性写入空间
    ringbuffer_get_write_vector(rb, vec);
    EXPECT_EQ(vec[0].len, rb->size - 1);
    EXPECT_EQ(vec[1].len, 0);

    // 场景2: 写入数据导致回绕
    size_t pos = rb->size - 10;
    rb->write_ptr = pos; // 手动移动指针以模拟状态
    rb->read_ptr = 10;
    
    // 获取写入空间，应该是两段
    ringbuffer_get_write_vector(rb, vec);
    EXPECT_EQ(vec[0].len, 10); // 从 pos 到缓冲区末尾
    EXPECT_EQ(vec[1].len, 9); // 从缓冲区开头到 read_ptr-1
    
    // 获取读取空间，也应该是两段
    rb->write_ptr = 5; // 移动 write_ptr
    rb->read_ptr = pos;
    ringbuffer_get_read_vector(rb, vec);
    EXPECT_EQ(vec[0].len, 10); // 从 read_ptr 到缓冲区末尾
    EXPECT_EQ(vec[1].len, 5);  // 从缓冲区开头到 write_ptr
}

// 10. 测试 Reset 函数
TEST_F(RingBufferTest, Reset) {
    ringbuffer_put(rb, "some data", 9);
    ASSERT_FALSE(ringbuffer_is_empty(rb));

    ringbuffer_reset(rb);

    EXPECT_TRUE(ringbuffer_is_empty(rb));
    EXPECT_EQ(rb->read_ptr, 0);
    EXPECT_EQ(rb->write_ptr, 0);
}

// 11. 核心测试：单生产者单消费者 (SPSC) 并发测试
// 这个测试对于验证无锁代码的正确性至关重要。
TEST(RingBufferConcurrencyTest, SPSC_Correctness) {
    auto rb = ringbuffer_create(8192); // 使用一个较大的缓冲区
    ASSERT_NE(rb, nullptr);

    const int total_items = 1000000;
    std::atomic<bool> start_signal(false);
    std::atomic<bool> producer_done(false);

    // --- 生产者线程 ---
    std::thread producer([&]() {
        while (!start_signal) { std::this_thread::yield(); }

        for (int i = 0; i < total_items; ++i) {
            uint32_t value = i;
            while (ringbuffer_write_space(rb) < sizeof(value)) {
                std::this_thread::yield(); // 等待空间
            }
            size_t written = ringbuffer_put(rb, (const char*)&value, sizeof(value));
            ASSERT_EQ(written, sizeof(value));
        }
        producer_done = true;
    });

    // --- 消费者线程 ---
    std::thread consumer([&]() {
        while (!start_signal) { std::this_thread::yield(); }

        for (int i = 0; i < total_items; ++i) {
            uint32_t value;
            while (ringbuffer_read_space(rb) < sizeof(value)) {
                // 如果生产者已经完成但队列仍为空，则退出
                if (producer_done && ringbuffer_is_empty(rb)) {
                    FAIL() << "Consumer exited prematurely. Expected item " << i << " but queue is empty and producer is done.";
                    return;
                }
                std::this_thread::yield(); // 等待数据
            }
            size_t read_bytes = ringbuffer_get(rb, (char*)&value, sizeof(value));
            ASSERT_EQ(read_bytes, sizeof(value));
            // 验证接收到的数据是否按顺序
            ASSERT_EQ(value, i);
        }
    });

    // 开始测试
    start_signal = true;

    producer.join();
    consumer.join();

    ringbuffer_destroy(rb);
}