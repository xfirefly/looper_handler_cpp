#include "gtest/gtest.h"
#include "circular-buf.hpp" // 引入要测试的头文件
#include <thread>
#include <vector>
#include <numeric>
#include <atomic>
#include <chrono>

// --- 测试夹具 (Test Fixture) ---
// 定义测试环境，为每个测试用例提供一个干净的 CircularBuffer 实例
class CircularBufferTest : public ::testing::Test {
protected:
    // 定义缓冲区的参数：8个块，每个块1024字节
    static const size_t chunks_count = 8;
    static const size_t chunk_size = 4096;

    // 指向被测试对象的指针
    CircularBuffer<chunks_count, chunk_size>* cb;

    // 在每个测试开始前执行
    void SetUp() override {
        cb = new CircularBuffer<chunks_count, chunk_size>();
    }

    // 在每个测试结束后执行
    void TearDown() override {
        delete cb;
    }
};

// --- 测试用例 ---

// 1. 测试基本的 Push 和 Pop 操作
TEST_F(CircularBufferTest, BasicPushAndPop) {
    std::vector<uint8_t> write_data(1500, 0); // 大于一个块
    std::iota(write_data.begin(), write_data.end(), 0); // 填充 0, 1, 2, ...

    // 写入数据
    size_t written = cb->Push(write_data.data(), write_data.size());
    ASSERT_EQ(written, write_data.size());

    // 准备读取
    std::vector<uint8_t> read_data(write_data.size(), 0);
    size_t popped = cb->Pop(read_data.data(), read_data.size());
    ASSERT_EQ(popped, read_data.size());

    // 验证数据是否一致
    EXPECT_EQ(write_data, read_data);
}

// 2. 测试 Push 操作直到缓冲区满
TEST_F(CircularBufferTest, PushUntilFull) {
    // 整个缓冲区的容量是 (ChunksCount-1) * ChunkSize
    // 因为有一个块总是空闲的，用于区分“满”和“空”
    const size_t full_size = (chunks_count - 1) * chunk_size;
    std::vector<uint8_t> data(full_size, 'A');

    // 写满缓冲区
    size_t written = cb->Push(data.data(), data.size());
    ASSERT_EQ(written, full_size);

    // 此时再尝试写入，应该返回 0，因为没有空闲块了
    uint8_t extra_byte = 'B';
    written = cb->Push(&extra_byte, 1);
    EXPECT_EQ(written, 0);
}

// 3. 测试从空缓冲区 Pop
TEST_F(CircularBufferTest, PopFromEmpty) {
    std::vector<uint8_t> read_buf(100, 0);
    size_t popped = cb->Pop(read_buf.data(), read_buf.size());
    EXPECT_EQ(popped, 0);
}

// 4. 测试分块 Push 和 Pop
// 验证多次小规模写入和读取是否能正确拼接
TEST_F(CircularBufferTest, ChunkedPushAndPop) {
    std::vector<uint8_t> test_data(500, 0);
    std::iota(test_data.begin(), test_data.end(), 100);

    // 分三次写入
    cb->Push(test_data.data(), 100);
    cb->Push(test_data.data() + 100, 200);
    cb->Push(test_data.data() + 300, 200);

    std::vector<uint8_t> read_data(test_data.size(), 0);

    // 分两次读取
    size_t popped1 = cb->Pop(read_data.data(), 300);
    size_t popped2 = cb->Pop(read_data.data() + 300, 200);

    ASSERT_EQ(popped1, 300);
    ASSERT_EQ(popped2, 200);

    // 验证数据
    EXPECT_EQ(test_data, read_data);
}

// 5. 测试 Flush (刷新) 功能
TEST_F(CircularBufferTest, Flush) {
    std::vector<uint8_t> data(100, 'X');
    cb->Push(data.data(), data.size());

    // 确认有数据
    std::vector<uint8_t> read_buf(100);
    ASSERT_EQ(cb->Pop(read_buf.data(), 1), 1);
    // 放回数据，模拟缓冲区非空状态
    cb->Push(read_buf.data(), 1);


    // 刷新
    cb->Flush();

    // 刷新后，缓冲区应该为空
    size_t popped = cb->Pop(read_buf.data(), read_buf.size());
    EXPECT_EQ(popped, 0);

    // 并且可以重新写入全部容量
    const size_t full_size = (chunks_count - 1) * chunk_size;
    size_t written = cb->Push(data.data(), full_size);
    EXPECT_EQ(written, full_size);
}

// 6. 核心测试: 单生产者单消费者 (SPSC) 并发测试
// 这个测试验证了 CircularBuffer 在并发读写下的数据完整性和线程安全性。
TEST(CircularBufferConcurrencyTest, SPSC_CorrectnessAndIntegrity) {
    const size_t TestChunksCount = 16;
    const size_t TestChunkSize = 4096;
    auto cb = new CircularBuffer<TestChunksCount, TestChunkSize>();

    const int total_megabytes = 50; // 总共传输 50MB 数据
    const size_t total_bytes = total_megabytes * 1024 * 1024;
    std::atomic<bool> start_signal(false);
    std::atomic<bool> producer_finished(false);

    // --- 生产者线程 ---
    std::thread producer([&]() {
        std::vector<uint8_t> write_buffer(TestChunkSize);
        uint8_t value = 0;
        while (!start_signal) { std::this_thread::yield(); }

        size_t bytes_written = 0;
        while (bytes_written < total_bytes) {
            // 填充要写入的数据
            for(size_t i = 0; i < write_buffer.size(); ++i) {
                write_buffer[i] = value++;
            }

            size_t remaining_to_write = total_bytes - bytes_written;
            size_t current_write_size = std::min(write_buffer.size(), remaining_to_write);

            size_t written_this_loop = 0;
            while(written_this_loop < current_write_size) {
                size_t written = cb->Push(write_buffer.data() + written_this_loop, current_write_size - written_this_loop);
                written_this_loop += written;
                if(written == 0) {
                     std::this_thread::yield();
                }
            }
            bytes_written += written_this_loop;
        }
        producer_finished = true;
    });

    // --- 消费者线程 ---
    std::thread consumer([&]() {
        std::vector<uint8_t> read_buffer(TestChunkSize);
        uint8_t expected_value = 0;
        while (!start_signal) { std::this_thread::yield(); }

        size_t bytes_read = 0;
        while (bytes_read < total_bytes) {
            size_t to_read = std::min(read_buffer.size(), total_bytes - bytes_read);
            size_t popped = cb->Pop(read_buffer.data(), to_read);

            if (popped > 0) {
                // 验证读取到的数据是否与预期一致
                for (size_t i = 0; i < popped; ++i) {
                    ASSERT_EQ(read_buffer[i], expected_value++);
                }
                bytes_read += popped;
            } else {
                // 如果生产者已经完成，但我们仍然读不到数据，就退出循环
                if (producer_finished) {
                    break;
                }
                std::this_thread::yield();
            }
        }
        // 最终要确保所有数据都被读取了
        ASSERT_EQ(bytes_read, total_bytes);
    });

    // 同时启动
    start_signal = true;

    producer.join();
    consumer.join();

    delete cb;
}