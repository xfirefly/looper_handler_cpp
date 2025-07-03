#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>

// --- 平台特定的头文件和函数 ---
#ifdef _MSC_VER
#include <windows.h> // For Sleep()
#else
#include <time.h> // For nanosleep()
#endif

// 包含被测试的头文件
#include "audiobuf.h"

// --- 简单的断言宏，用于提供更清晰的输出 ---
#define ASSERT(condition)                                           \
    do {                                                            \
        if (!(condition)) {                                         \
            fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", \
                    #condition, __FILE__, __LINE__);                \
            exit(1);                                                \
        }                                                           \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_TRUE(a) ASSERT((a))
#define ASSERT_NOT_NULL(a) ASSERT((a) != NULL)
#define ASSERT_LT(a, b) ASSERT((a) < (b)) // 添加缺失的宏

// --- 跨平台睡眠函数 ---
void cross_platform_sleep_ms(int ms) {
#ifdef _MSC_VER
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
#endif
}


// --- 测试函数声明 ---
void test_initialization();
void test_initial_state();
void test_basic_write_read();
void test_write_to_full();
void test_read_from_empty();
void test_wrap_around();
void test_write_silence();
void test_spsc_concurrency();

// --- 测试实现 ---

void test_initialization() {
    printf("Running: test_initialization\n");
    struct audiobuf buf;
    const size_t sample_size = sizeof(int16_t);
    const uint32_t capacity = 1024;

    bool success = audiobuf_init(&buf, sample_size, capacity);
    ASSERT_TRUE(success);
    ASSERT_NOT_NULL(buf.data);
    ASSERT_EQ(buf.alloc_size, capacity + 1);
    ASSERT_EQ(buf.sample_size, sample_size);
    ASSERT_EQ(atomic_load(&buf.head), 0);
    ASSERT_EQ(atomic_load(&buf.tail), 0);
    ASSERT_EQ(audiobuf_capacity(&buf), capacity);

    audiobuf_destroy(&buf);
}

void test_initial_state() {
    printf("Running: test_initial_state\n");
    struct audiobuf buf;
    audiobuf_init(&buf, sizeof(int16_t), 1024);

    ASSERT_EQ(audiobuf_can_read(&buf), 0);
    ASSERT_EQ(audiobuf_capacity(&buf), 1024);

    audiobuf_destroy(&buf);
}

void test_basic_write_read() {
    printf("Running: test_basic_write_read\n");
    struct audiobuf buf;
    audiobuf_init(&buf, sizeof(int16_t), 1024);

    const uint32_t write_count = 100;
    int16_t* write_data = malloc(write_count * sizeof(int16_t));
    ASSERT_NOT_NULL(write_data);
    for (uint32_t i = 0; i < write_count; ++i) {
        write_data[i] = (int16_t)i;
    }

    uint32_t written = audiobuf_write(&buf, write_data, write_count);
    ASSERT_EQ(written, write_count);
    ASSERT_EQ(audiobuf_can_read(&buf), write_count);

    int16_t* read_data = malloc(write_count * sizeof(int16_t));
    ASSERT_NOT_NULL(read_data);
    uint32_t read_bytes = audiobuf_read(&buf, read_data, write_count);
    ASSERT_EQ(read_bytes, write_count);

    ASSERT_EQ(memcmp(write_data, read_data, write_count * sizeof(int16_t)), 0);
    ASSERT_EQ(audiobuf_can_read(&buf), 0);

    free(write_data);
    free(read_data);
    audiobuf_destroy(&buf);
}

void test_write_to_full() {
    printf("Running: test_write_to_full\n");
    struct audiobuf buf;
    const uint32_t capacity = 128;
    audiobuf_init(&buf, sizeof(int16_t), capacity);

    int16_t* write_data = malloc(capacity * sizeof(int16_t));
    ASSERT_NOT_NULL(write_data);
    memset(write_data, 'A', capacity * sizeof(int16_t));

    uint32_t written = audiobuf_write(&buf, write_data, capacity);
    ASSERT_EQ(written, capacity);
    ASSERT_EQ(audiobuf_can_read(&buf), capacity);

    // 再次写入应该失败
    written = audiobuf_write(&buf, write_data, 1);
    ASSERT_EQ(written, 0);

    free(write_data);
    audiobuf_destroy(&buf);
}

void test_read_from_empty() {
    printf("Running: test_read_from_empty\n");
    struct audiobuf buf;
    audiobuf_init(&buf, sizeof(int16_t), 128);

    int16_t read_data[10]; // Small enough for stack
    uint32_t read_bytes = audiobuf_read(&buf, read_data, 10);
    ASSERT_EQ(read_bytes, 0);

    audiobuf_destroy(&buf);
}

void test_wrap_around() {
    printf("Running: test_wrap_around\n");
    struct audiobuf buf;
    const uint32_t capacity = 1024;
    audiobuf_init(&buf, sizeof(int16_t), capacity);

    uint32_t initial_write_size = capacity - 10;
    int16_t* initial_data = malloc(initial_write_size * sizeof(int16_t));
    ASSERT_NOT_NULL(initial_data);
    audiobuf_write(&buf, initial_data, initial_write_size);

    int16_t* temp_read_buf = malloc(100 * sizeof(int16_t));
    ASSERT_NOT_NULL(temp_read_buf);
    audiobuf_read(&buf, temp_read_buf, 100);
    ASSERT_EQ(atomic_load(&buf.tail), 100);

    uint32_t remaining_space = capacity - audiobuf_can_read(&buf);
    int16_t* wrap_data = malloc(remaining_space * sizeof(int16_t));
    ASSERT_NOT_NULL(wrap_data);
    for(uint32_t i=0; i<remaining_space; ++i) wrap_data[i] = (int16_t)i;

    uint32_t written = audiobuf_write(&buf, wrap_data, remaining_space);
    ASSERT_EQ(written, remaining_space);
    ASSERT_LT(atomic_load(&buf.head), atomic_load(&buf.tail));

    ASSERT_EQ(audiobuf_write(&buf, wrap_data, 1), 0);

    free(initial_data);
    free(temp_read_buf);
    free(wrap_data);
    audiobuf_destroy(&buf);
}

void test_write_silence() {
    printf("Running: test_write_silence\n");
    struct audiobuf buf;
    audiobuf_init(&buf, sizeof(int16_t), 128);

    const uint32_t silence_count = 50;
    uint32_t written = audiobuf_write_silence(&buf, silence_count);
    ASSERT_EQ(written, silence_count);
    ASSERT_EQ(audiobuf_can_read(&buf), silence_count);

    int16_t* read_data = malloc(silence_count * sizeof(int16_t));
    ASSERT_NOT_NULL(read_data);
    uint32_t read_bytes = audiobuf_read(&buf, read_data, silence_count);
    ASSERT_EQ(read_bytes, silence_count);

    for (uint32_t i = 0; i < silence_count; ++i) {
        ASSERT_EQ(read_data[i], 0);
    }

    free(read_data);
    audiobuf_destroy(&buf);
}


// --- SPSC 并发测试 ---
typedef struct {
    struct audiobuf* buf;
    uint32_t total_samples;
    atomic_bool* producer_finished;
} thread_args_t;

void* producer_thread_func(void* args_ptr) {
    thread_args_t* args = (thread_args_t*)args_ptr;
    uint32_t samples_written = 0;
    int16_t write_chunk[256];

    while (samples_written < args->total_samples) {
        for (size_t i = 0; i < 256; ++i) {
            write_chunk[i] = (int16_t)(samples_written + i);
        }

        uint32_t remaining = args->total_samples - samples_written;
        uint32_t to_write = (256 < remaining) ? 256 : remaining;

        uint32_t written_now = 0;
        while (written_now < to_write) {
            uint32_t written = audiobuf_write(args->buf, write_chunk + written_now, to_write - written_now);
            written_now += written;
            if (written == 0) {
                cross_platform_sleep_ms(1); // 缓冲区满，让出CPU
            }
        }
        samples_written += written_now;
    }
    atomic_store(args->producer_finished, true);
    return NULL;
}

void* consumer_thread_func(void* args_ptr) {
    thread_args_t* args = (thread_args_t*)args_ptr;
    uint32_t samples_read = 0;
    int16_t read_chunk[128];

    while (samples_read < args->total_samples) {
        uint32_t can_read_now = audiobuf_can_read(args->buf);
        if (can_read_now > 0) {
            uint32_t to_read = (128 < can_read_now) ? 128 : can_read_now;
            uint32_t read_now = audiobuf_read(args->buf, read_chunk, to_read);

            for (uint32_t i = 0; i < read_now; ++i) {
                ASSERT_EQ(read_chunk[i], (int16_t)(samples_read + i));
            }
            samples_read += read_now;
        } else if (atomic_load(args->producer_finished)) {
            break;
        } else {
            cross_platform_sleep_ms(1); // 缓冲区空，让出CPU
        }
    }
    ASSERT_EQ(samples_read, args->total_samples);
    return NULL;
}

void test_spsc_concurrency() {
    printf("Running: test_spsc_concurrency\n");
    struct audiobuf buf;
    audiobuf_init(&buf, sizeof(int16_t), 8192);

    atomic_bool producer_finished;
    atomic_init(&producer_finished, false);

    thread_args_t args = {
        .buf = &buf,
        .total_samples = 500000,
        .producer_finished = &producer_finished
    };

    pthread_t producer_tid, consumer_tid;
    pthread_create(&producer_tid, NULL, producer_thread_func, &args);
    pthread_create(&consumer_tid, NULL, consumer_thread_func, &args);

    pthread_join(producer_tid, NULL);
    pthread_join(consumer_tid, NULL);

    audiobuf_destroy(&buf);
}


// --- 主函数 ---
int main() {
    printf("--- Starting audiobuf C11 tests ---\n");

    test_initialization();
    test_initial_state();
    test_basic_write_read();
    test_write_to_full();
    test_read_from_empty();
    test_wrap_around();
    test_write_silence();
    test_spsc_concurrency();

    printf("--- All audiobuf C11 tests passed! ---\n");
    return 0;
}