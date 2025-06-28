#include "gtest/gtest.h"
#include "HandlerThread.h"
 
#include <future>
#include <chrono>

// --- 测试用的 Handler ---
// 一个简单的 Handler，用于验证消息是否在正确的线程上被处理
class TestHandler : public core::Handler {
public:
    // 记录 handleMessage 被调用时的线程 ID
    std::promise<std::thread::id> executed_thread_id_promise;

    // 构造函数，需要一个 Looper
    explicit TestHandler(std::shared_ptr<core::Looper> looper) : core::Handler(looper) {}

    // 重写 handleMessage 来处理消息
    void handleMessage(const core::Message& msg) override {
        // 当消息被处理时，设置 promise 的值为当前线程的 ID
        executed_thread_id_promise.set_value(std::this_thread::get_id());
    }
};

// --- HandlerThread 测试套件 ---
class HandlerThreadTest : public ::testing::Test {
protected:
    // 在每个测试开始前，创建一个新的 HandlerThread 实例
    void SetUp() override {
        handlerThread = std::make_unique<core::HandlerThread>("MyTestHandlerThread");
    }

    // 在每个测试结束后，确保线程被正确清理
    void TearDown() override {
        if (handlerThread) {
            // 请求退出并等待线程结束
            handlerThread->quit();
            handlerThread->join();
        }
    }

    std::unique_ptr<core::HandlerThread> handlerThread;
};

// --- 测试用例 ---

// 测试1: 验证构造函数和 start() 方法
// 目标: 覆盖 HandlerThread() 和 start()
TEST_F(HandlerThreadTest, ConstructionAndStart) {
    // 启动线程
    handlerThread->start();

    // 验证 getThreadId() 返回一个有效的、与主线程不同的线程ID
    auto worker_thread_id = handlerThread->getThreadId();
    ASSERT_NE(worker_thread_id, std::this_thread::get_id());
    ASSERT_NE(worker_thread_id, std::thread::id{}); // 确保不是默认构造的无效ID
}

// 测试2: 验证 getLooper() 功能
// 目标: 覆盖 getLooper()
TEST_F(HandlerThreadTest, GetLooper) {
    handlerThread->start();

    // 第一次调用 getLooper() 应该会阻塞直到 Looper 被创建，并返回一个有效的 Looper
    auto looper = handlerThread->getLooper();
    ASSERT_NE(looper, nullptr);

    // 验证 Looper 所在的线程就是 HandlerThread 的线程
    ASSERT_EQ(looper->getThreadId(), handlerThread->getThreadId());

    // 第二次调用 getLooper() 应该立即返回同一个 Looper 实例
    auto another_looper_handle = handlerThread->getLooper();
    ASSERT_EQ(looper.get(), another_looper_handle.get());
}

// 测试3: 在 HandlerThread 上派发和处理消息
// 目标: 综合验证 start(), getLooper(), 和线程的消息循环功能
TEST_F(HandlerThreadTest, PostAndProcessMessage) {
    handlerThread->start();

    // 获取 Looper 并创建一个与该 Looper 关联的 Handler
    auto looper = handlerThread->getLooper();
    ASSERT_NE(looper, nullptr);
    auto handler = std::make_shared<TestHandler>(looper);

    // 获取 future 以便在主线程中等待结果
    auto future = handler->executed_thread_id_promise.get_future();

    // 发送一个空消息到工作线程
    handler->sendMessage(handler->obtainMessage(123));

    // 等待消息被处理（最多2秒）
    auto status = future.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    // 验证消息是在 HandlerThread 的线程上被处理的
    std::thread::id executed_id = future.get();
    EXPECT_EQ(executed_id, handlerThread->getThreadId());
}


// 测试4: 验证 quit() 和 join() 功能
// 目标: 覆盖 quit() 和 join()
TEST_F(HandlerThreadTest, QuitAndJoin) {
    handlerThread->start();
    ASSERT_NE(handlerThread->getThreadId(), std::thread::id{});

    // 请求退出
    bool quit_result = handlerThread->quit();
    ASSERT_TRUE(quit_result);

    // 等待线程终止。如果 quit() 工作正常，join() 应该不会永久阻塞。
    // 我们在这里不设置超时，因为 join 本身就是等待。如果它卡住了，测试会超时失败。
    handlerThread->join();

    // join() 成功返回后，线程应该已经结束。
    // 注意：我们无法直接检查线程是否“存活”，但 join 的成功返回就是最好的证明。
}