#include "gtest/gtest.h"
#include "looper_handler.h"
#include <thread>
#include <future>
#include <chrono>
#include <atomic>
#include <vector>

using namespace core;
using namespace std::chrono_literals;

// --- 测试用的 Handler ---
// 一个功能丰富的 Handler，用于记录事件、线程ID，并使用 promise/future 进行同步
class TestHandler : public Handler {
public:
    // 构造函数：可以与主线程 Looper 或后台线程 Looper 关联
    explicit TestHandler(std::shared_ptr<Looper> looper) : Handler(std::move(looper)) {}
    TestHandler() : Handler() {} // 使用当前线程的 Looper

    // 用于记录处理过的消息代码
    std::vector<int> handled_messages;
    // 用于记录 handleMessage 被调用时的线程 ID
    std::promise<std::thread::id> executed_thread_id_promise;
    // 用于测试 runnable 是否被执行
    std::promise<void> runnable_executed_promise;

    void handleMessage(const Message& msg) override {
        // 记录消息
        handled_messages.push_back(msg.what);

        // 如果是特殊测试消息，则设置 promise
        if (msg.what == MSG_EXECUTION_THREAD_CHECK) {
            executed_thread_id_promise.set_value(std::this_thread::get_id());
        }
    }

    // 一个特殊的 runnable，用于测试 post
    void test_runnable() {
        runnable_executed_promise.set_value();
    }

    // 消息代码常量
    static const int MSG_SIMPLE = 1;
    static const int MSG_DELAYED = 2;
    static const int MSG_EXECUTION_THREAD_CHECK = 3;
    static const int MSG_TO_BE_REMOVED = 4;
};

// --- Looper 和 Handler 测试套件 ---
class LooperHandlerTest : public ::testing::Test {
protected:
    std::unique_ptr<std::thread> looper_thread;
    std::shared_ptr<Looper> background_looper;
    std::promise<std::shared_ptr<Looper>> looper_promise;

    // 启动一个后台线程并为其准备 Looper
    void SetUp() override {
        auto future = looper_promise.get_future();
        looper_thread = std::make_unique<std::thread>([this]() {
            try {
                Looper::prepare();
                auto my_looper = Looper::myLooper();
                looper_promise.set_value(my_looper);
                Looper::loop();
            } catch(const std::exception& e) {
                // 在测试失败时能看到原因
                try {
                    looper_promise.set_exception(std::current_exception());
                } catch(...) {}
            }
        });
        // 等待后台 Looper 准备好
        background_looper = future.get();
        ASSERT_NE(background_looper, nullptr);
    }

    // 清理：退出 Looper 并 join 线程
    void TearDown() override {
        if (background_looper) {
            background_looper->quit();
        }
        if (looper_thread && looper_thread->joinable()) {
            looper_thread->join();
        }
    }
};

// --- Looper 测试用例 ---

TEST_F(LooperHandlerTest, PrepareAndMyLooper) {
    ASSERT_NE(background_looper, nullptr);
    EXPECT_EQ(background_looper->getThreadId(), looper_thread->get_id());
}

TEST_F(LooperHandlerTest, LoopAndQuit) {
    // SetUp 已经启动了 loop
    // TearDown 会调用 quit 并 join
    // 这个测试只要能成功运行（不卡死）就证明了 loop 和 quit 的基本功能
    SUCCEED();
}

TEST(LooperStaticTest, PrepareThrowsIfCalledTwice) {
    Looper::prepare();
    EXPECT_THROW(Looper::prepare(), std::runtime_error);
    // 清理，否则会影响其他测试
    Looper::myLooper()->quit();
    Looper::loop();
}

TEST(LooperStaticTest, MyLooperReturnsNullWithoutPrepare) {
    EXPECT_EQ(Looper::myLooper(), nullptr);
}

// --- Handler 测试用例 ---

TEST_F(LooperHandlerTest, HandlerCreation) {
    // 1. 在后台线程创建 Handler
    auto handler_on_background = std::make_shared<TestHandler>(background_looper);
    ASSERT_NE(handler_on_background, nullptr);
    EXPECT_EQ(handler_on_background->getLooper(), background_looper);

    // 2. 在主线程创建 Handler (需要先 prepare)
    Looper::prepare();
    auto main_thread_looper = Looper::myLooper();
    ASSERT_NE(main_thread_looper, nullptr);
    
    auto handler_on_main = std::make_shared<TestHandler>(); // 默认构造函数
    ASSERT_NE(handler_on_main, nullptr);
    EXPECT_EQ(handler_on_main->getLooper(), main_thread_looper);

    // 清理主线程 looper
    main_thread_looper->quit();
    // 由于我们不能阻塞主线程，就不调用 loop() 了。
}

TEST_F(LooperHandlerTest, SendMessageAndPost) {
    auto handler = std::make_shared<TestHandler>(background_looper);
    
    // 1. 测试 sendMessage
    auto thread_id_future = handler->executed_thread_id_promise.get_future();
    ASSERT_TRUE(handler->sendMessage(handler->obtainMessage(TestHandler::MSG_EXECUTION_THREAD_CHECK)));
    
    ASSERT_EQ(thread_id_future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(thread_id_future.get(), background_looper->getThreadId());

    // 2. 测试 post
    auto runnable_future = handler->runnable_executed_promise.get_future();
    ASSERT_TRUE(handler->post([handler]() {
        handler->test_runnable();
    }));

    ASSERT_EQ(runnable_future.wait_for(1s), std::future_status::ready);
}

TEST_F(LooperHandlerTest, SendMessageDelayedAndPostDelayed) {
    auto handler = std::make_shared<TestHandler>(background_looper);
    auto start_time = std::chrono::steady_clock::now();

    // 1. 延迟消息
    ASSERT_TRUE(handler->sendMessageDelayed(handler->obtainMessage(TestHandler::MSG_DELAYED), 100));

    // 2. 延迟 Runnable
    auto future = handler->runnable_executed_promise.get_future();
    ASSERT_TRUE(handler->postDelayed([handler]() {
        handler->test_runnable();
    }, 150));

    // 等待 runnable 完成
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // 验证延迟
    EXPECT_GE(duration, 150);

    // 验证消息顺序
    ASSERT_EQ(handler->handled_messages.size(), 2);
    EXPECT_EQ(handler->handled_messages[0], TestHandler::MSG_DELAYED);
}

TEST_F(LooperHandlerTest, RemoveMessages) {
    auto handler = std::make_shared<TestHandler>(background_looper);
    
    handler->sendMessageDelayed(handler->obtainMessage(TestHandler::MSG_SIMPLE), 200);
    handler->sendMessageDelayed(handler->obtainMessage(TestHandler::MSG_TO_BE_REMOVED), 200);
    handler->sendMessageDelayed(handler->obtainMessage(TestHandler::MSG_TO_BE_REMOVED), 300);

    // 在消息触发前移除它们
    handler->removeMessages(TestHandler::MSG_TO_BE_REMOVED);

    // 等待足够长的时间让未被移除的消息执行
    std::this_thread::sleep_for(400ms);

    // 验证结果
    ASSERT_EQ(handler->handled_messages.size(), 1);
    EXPECT_EQ(handler->handled_messages[0], TestHandler::MSG_SIMPLE);
}

TEST_F(LooperHandlerTest, RemoveCallbacks) {
    auto handler = std::make_shared<TestHandler>(background_looper);
    
    std::atomic<int> counter = 0;
    
    // 一个会执行的 runnable
    handler->postDelayed([&counter]() {
        counter++;
    }, 100);

    // 一个会被移除的 runnable
    auto callback_to_remove = [&counter]() {
        counter = -1; // 如果执行了，就设为错误值
    };
    handler->postDelayed(callback_to_remove, 150);

    // 移除
    handler->removeCallbacks(); 

    // 等待
    std::this_thread::sleep_for(200ms);
    
    // 验证 counter
    // 注意：removeCallbacks 移除了所有回调，所以第一个也应该被移除了
    EXPECT_EQ(counter, 0);
}


TEST_F(LooperHandlerTest, ObtainMessageVariants) {
    auto handler = std::make_shared<TestHandler>(background_looper);

    // 测试不同 obtainMessage 重载版本
    Message msg1 = handler->obtainMessage(101);
    EXPECT_EQ(msg1.what, 101);
    EXPECT_EQ(msg1.target.get(), handler.get());

    Message msg2 = handler->obtainMessage(102, std::string("test_obj"));
    ASSERT_TRUE(msg2.obj.has_value());
    EXPECT_EQ(std::any_cast<std::string>(msg2.obj), "test_obj");
    
    Message msg3 = handler->obtainMessage(103, 201, 202);
    EXPECT_EQ(msg3.arg1, 201);
    EXPECT_EQ(msg3.arg2, 202);

    Message msg4 = handler->obtainMessage(104, 301, 302, 404.0);
    EXPECT_EQ(msg4.arg1, 301);
    EXPECT_EQ(msg4.arg2, 302);
    ASSERT_TRUE(msg4.obj.has_value());
    EXPECT_DOUBLE_EQ(std::any_cast<double>(msg4.obj), 404.0);
}

TEST_F(LooperHandlerTest, MessageSendToTarget) {
    auto handler = std::make_shared<TestHandler>(background_looper);
    auto future = handler->executed_thread_id_promise.get_future();

    // 创建一个消息，并设置它的目标
    Message msg = handler->obtainMessage(TestHandler::MSG_EXECUTION_THREAD_CHECK);
    // msg.target 在 obtainMessage 时已经设置好了
    
    // 调用 sendToTarget()
    ASSERT_TRUE(msg.sendToTarget());
    
    // 验证它是否被正确处理
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(future.get(), background_looper->getThreadId());
}