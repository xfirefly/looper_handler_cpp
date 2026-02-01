#include "gtest/gtest.h"
#include "LocalBroadcast.h"
#include <future>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <string>

using namespace core;
using namespace std::chrono_literals;

// --- 测试用的 BroadcastReceiver ---
// 一个功能丰富的接收器，用于测试各种场景
class TestReceiver : public BroadcastReceiver {
public:
    // 用于在接收到广播时通知主测试线程
    std::promise<Intent> received_intent_promise;
    // 用于在接收到广播时记录线程ID
    std::promise<std::thread::id> executed_thread_id_promise;
    // 用于统计接收到广播的次数
    std::atomic<int> received_count = 0;
    // 用于在析构时发出信号
    std::promise<void> destruction_promise;
    // 接收器的唯一标识符
    std::string id;

    explicit TestReceiver(std::string id = "") : id(std::move(id)) {}

    ~TestReceiver() override {
        // 在对象销毁时，设置 promise，以便测试可以验证其生命周期
        destruction_promise.set_value();
    }

    void onReceive(const Intent& intent) override {
        received_count++;
        
        // 尝试设置 promise 的值。如果 promise 已经被移动或已经设置过值，会抛出异常，
        // 但在我们的测试设计中，每个 promise 只会被设置一次。
        try {
            // 创建一个 Intent 的拷贝来设置 promise，因为原始 intent 是 const 引用
            received_intent_promise.set_value(intent);
        } catch (const std::future_error&) {
            // 忽略错误，因为一个 promise 可能被用于多个测试场景，而我们只关心第一次的接收
        }
        
        try {
            executed_thread_id_promise.set_value(std::this_thread::get_id());
        } catch (const std::future_error&) {
            // 同上
        }
    }

    // 重置 promise 以便在新的测试中断言
    void reset_promises() {
        received_intent_promise = std::promise<Intent>();
        executed_thread_id_promise = std::promise<std::thread::id>();
    }
};

// --- LocalBroadcastManager 测试套件 ---
class LocalBroadcastManagerTest : public ::testing::Test {
protected:
    BroadcastManager* lbm;

    // 在每个测试开始前执行
    void SetUp() override {
        // 获取 BroadcastManager 的单例
        lbm = &BroadcastManager::getInstance();
    }

    // 每个测试结束后不执行特殊操作
    void TearDown() override {}
};


// --- 测试用例 ---

// 测试1: 基本的注册、广播和接收流程
TEST_F(LocalBroadcastManagerTest, RegisterAndReceiveBroadcast) {
    const std::string ACTION_TEST = "ACTION_TEST";
    auto receiver = std::make_shared<TestReceiver>();
    IntentFilter filter(ACTION_TEST);

    lbm->registerReceiver(receiver, filter);

    Intent intent(ACTION_TEST);
    lbm->sendBroadcast(intent);

    // 等待广播被接收
    auto future = receiver->received_intent_promise.get_future();
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);

    // 验证接收到的 action 是否正确
    Intent received_intent = future.get();
    EXPECT_EQ(received_intent.getAction(), ACTION_TEST);
    EXPECT_EQ(receiver->received_count, 1);
    
    lbm->unregisterReceiver(receiver);
}

// 测试2: 验证广播是在工作线程中异步执行的
TEST_F(LocalBroadcastManagerTest, BroadcastIsAsynchronous) {
    auto receiver = std::make_shared<TestReceiver>();
    lbm->registerReceiver(receiver, IntentFilter("ACTION_ASYNC"));

    lbm->sendBroadcast(Intent("ACTION_ASYNC"));
    
    // 验证 onReceive 是在非主线程执行的
    auto future = receiver->executed_thread_id_promise.get_future();
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_NE(future.get(), std::this_thread::get_id());

    lbm->unregisterReceiver(receiver);
}

// 测试3: 验证注销后不再接收广播
TEST_F(LocalBroadcastManagerTest, UnregisterReceiver) {
    const std::string ACTION_UNREGISTER = "ACTION_UNREGISTER";
    auto receiver = std::make_shared<TestReceiver>();
    IntentFilter filter(ACTION_UNREGISTER);

    lbm->registerReceiver(receiver, filter);
    lbm->unregisterReceiver(receiver);

    lbm->sendBroadcast(Intent(ACTION_UNREGISTER));

    auto future = receiver->received_intent_promise.get_future();
    // 我们期望 future 超时，因为广播不应该被接收
    ASSERT_EQ(future.wait_for(100ms), std::future_status::timeout);
    EXPECT_EQ(receiver->received_count, 0);
}

// 测试4: 多个接收器监听同一个 Action
TEST_F(LocalBroadcastManagerTest, MultipleReceiversForSameAction) {
    const std::string ACTION_MULTIPLE = "ACTION_MULTIPLE";
    auto receiver1 = std::make_shared<TestReceiver>("R1");
    auto receiver2 = std::make_shared<TestReceiver>("R2");
    IntentFilter filter(ACTION_MULTIPLE);

    lbm->registerReceiver(receiver1, filter);
    lbm->registerReceiver(receiver2, filter);

    lbm->sendBroadcast(Intent(ACTION_MULTIPLE));

    // 验证两个接收器都收到了广播
    auto future1 = receiver1->received_intent_promise.get_future();
    auto future2 = receiver2->received_intent_promise.get_future();
    ASSERT_EQ(future1.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(future2.wait_for(1s), std::future_status::ready);

    EXPECT_EQ(receiver1->received_count, 1);
    EXPECT_EQ(receiver2->received_count, 1);

    lbm->unregisterReceiver(receiver1);
    lbm->unregisterReceiver(receiver2);
}

// 测试5: 一个接收器监听多个 Action
TEST_F(LocalBroadcastManagerTest, SingleReceiverForMultipleActions) {
    const std::string ACTION_A = "ACTION_A";
    const std::string ACTION_B = "ACTION_B";
    auto receiver = std::make_shared<TestReceiver>();
    IntentFilter filter(ACTION_A);
    filter.addAction(ACTION_B);

    lbm->registerReceiver(receiver, filter);

    // 发送第一个广播
    lbm->sendBroadcast(Intent(ACTION_A));
    auto future_a = receiver->received_intent_promise.get_future();
    ASSERT_EQ(future_a.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(future_a.get().getAction(), ACTION_A);

    // 重置 promise 并发送第二个广播
    receiver->reset_promises();
    lbm->sendBroadcast(Intent(ACTION_B));
    auto future_b = receiver->received_intent_promise.get_future();
    ASSERT_EQ(future_b.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(future_b.get().getAction(), ACTION_B);
    
    EXPECT_EQ(receiver->received_count, 2);

    lbm->unregisterReceiver(receiver);
}

// 测试6 (核心健壮性测试): 接收器生命周期结束后的安全性
// 验证当一个 receiver 被销毁后，LocalBroadcastManager 不会崩溃
TEST_F(LocalBroadcastManagerTest, ReceiverLifecycleSafety) {
    const std::string ACTION_LIFECYCLE = "ACTION_LIFECYCLE";
    auto destruction_future = std::make_shared<std::promise<void>>()->get_future();

    // 在一个独立作用域内创建和注册 receiver
    {
        auto short_lived_receiver = std::make_shared<TestReceiver>();
        destruction_future = short_lived_receiver->destruction_promise.get_future();
        
        lbm->registerReceiver(short_lived_receiver, IntentFilter(ACTION_LIFECYCLE));
        
        // 当离开这个作用域时，short_lived_receiver 的 shared_ptr 将被销毁
    }
    
    // 确认接收器已经被析构 (weak_ptr 应该过期)
    ASSERT_EQ(destruction_future.wait_for(1s), std::future_status::ready);
    
    // 发送广播。如果代码不健壮，这里可能会因为试图调用悬挂指针而崩溃。
    // 我们期望这里不发生任何事情，并且能顺利执行完毕。
    EXPECT_NO_THROW(lbm->sendBroadcast(Intent(ACTION_LIFECYCLE)));
    
    // 等待一小会儿，确保广播循环有时间执行（尽管它应该什么都不做）
    std::this_thread::sleep_for(100ms);
}

// 测试7: 带有 Extra 数据的 Intent
TEST_F(LocalBroadcastManagerTest, BroadcastWithExtras) {
    auto receiver = std::make_shared<TestReceiver>();
    lbm->registerReceiver(receiver, IntentFilter("ACTION_EXTRAS"));

    Intent intent("ACTION_EXTRAS");
    intent.putExtra("string_data", std::string("hello world"));
    intent.putExtra("int_data", 42);

    lbm->sendBroadcast(intent);

    auto future = receiver->received_intent_promise.get_future();
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    Intent received = future.get();

    // 验证接收到的 extra 数据
    const std::string* str_data = received.getExtra<std::string>("string_data");
    const int* int_data = received.getExtra<int>("int_data");

    ASSERT_NE(str_data, nullptr);
    EXPECT_EQ(*str_data, "hello world");
    
    ASSERT_NE(int_data, nullptr);
    EXPECT_EQ(*int_data, 42);

    lbm->unregisterReceiver(receiver);
}

// 测试8: 接收器在 onReceive 中抛出异常
TEST_F(LocalBroadcastManagerTest, ReceiverThrowsException) {
    class ThrowingReceiver : public BroadcastReceiver {
    public:
        std::promise<void> has_run_promise;
        void onReceive(const Intent& intent) override {
            has_run_promise.set_value();
            throw std::runtime_error("Test exception from receiver");
        }
    };

    auto throwing_receiver = std::make_shared<ThrowingReceiver>();
    auto normal_receiver = std::make_shared<TestReceiver>();
    IntentFilter filter("ACTION_THROW");

    lbm->registerReceiver(throwing_receiver, filter);
    lbm->registerReceiver(normal_receiver, filter);

    // 我们期望即使一个接收器抛出异常，整个广播系统也不会崩溃，
    // 并且其他接收器应该能正常接收到广播。
    EXPECT_NO_THROW(lbm->sendBroadcast(Intent("ACTION_THROW")));

    // 验证两个接收器都运行了
    auto future_throw = throwing_receiver->has_run_promise.get_future();
    auto future_normal = normal_receiver->received_intent_promise.get_future();

    ASSERT_EQ(future_throw.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(future_normal.wait_for(1s), std::future_status::ready);

    lbm->unregisterReceiver(throwing_receiver);
    lbm->unregisterReceiver(normal_receiver);
}


// 测试9: 边界情况 - 注册和注销空指针
TEST_F(LocalBroadcastManagerTest, RegisterAndUnregisterNull) {
    // 注册和注销空指针不应该导致任何崩溃或异常
    EXPECT_NO_THROW(lbm->registerReceiver(nullptr, IntentFilter("ACTION_NULL")));
    EXPECT_NO_THROW(lbm->unregisterReceiver(nullptr));
}

// 测试10: 压力测试 (并发注册、注销和广播)
TEST_F(LocalBroadcastManagerTest, ConcurrencyStressTest) {
    std::atomic<bool> stop_flag = false;
    const int NUM_THREADS = 4;
    std::vector<std::thread> threads;

    // 广播线程
    threads.emplace_back([this, &stop_flag]() {
        while (!stop_flag) {
            lbm->sendBroadcast(Intent("STRESS_ACTION"));
            std::this_thread::sleep_for(1ms);
        }
    });

    // 注册/注销线程
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([this, &stop_flag, i]() {
            while (!stop_flag) {
                auto receiver = std::make_shared<TestReceiver>(std::to_string(i));
                IntentFilter filter("STRESS_ACTION");
                
                lbm->registerReceiver(receiver, filter);
                std::this_thread::sleep_for(std::chrono::microseconds(500));
                lbm->unregisterReceiver(receiver);
            }
        });
    }

    // 运行测试一小段时间
    std::this_thread::sleep_for(200ms);
    stop_flag = true;

    // 等待所有线程结束
    for (auto& t : threads) {
        t.join();
    }
    // 如果没有崩溃或死锁，测试就通过了
    SUCCEED();
}