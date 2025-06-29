#include "gtest/gtest.h"
#include "WorkerThread.h"
#include <future>
#include <chrono>
#include <atomic>


// --- 测试套件 ---
class WorkerThreadTest : public ::testing::Test {
protected:
    // 每个测试开始前执行
    void SetUp() override {
        // 创建一个新的 WorkerThread 实例
        workerThread = std::make_unique<core::WorkerThread>("TestWorker");
    }

    // 每个测试结束后执行
    void TearDown() override {
        // 确保线程被优雅地停止和清理
        if (workerThread) {
            //workerThread->finish(); // 发送退出消息
           // workerThread->join();   // 等待线程结束
           workerThread.reset(); // 重置智能指针，自动调用析构函数
        }
    }

    std::unique_ptr<core::WorkerThread> workerThread;
};

// --- 测试用例 ---

// 测试1: 验证构造函数和启动功能
// 目标: 覆盖 WorkerThread() 和 start()
TEST_F(WorkerThreadTest, ConstructionAndStart) {
    // 启动前，getLooper() 应该返回 nullptr
    ASSERT_EQ(workerThread->getLooper(), nullptr);

    // 启动线程
    workerThread->start();

    // 启动后，getLooper() 应该能返回一个有效的 Looper 实例
    // 这里我们给它一点时间来完成初始化
    std::shared_ptr<core::Looper> looper = nullptr;
    for (int i = 0; i < 100 && looper == nullptr; ++i) {
        looper = workerThread->getLooper();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_NE(looper, nullptr);

    // 验证线程ID是否与主线程不同
    EXPECT_NE(workerThread->getThreadId(), std::this_thread::get_id());
}

// 测试2: 验证 post() 功能
// 目标: 覆盖 post()
TEST_F(WorkerThreadTest, PostTask) {
    std::promise<std::thread::id> executedThreadIdPromise;
    auto executedThreadIdFuture = executedThreadIdPromise.get_future();

    workerThread->start();

    // 提交一个任务，该任务将设置 promise 的值为其执行线程的ID
    bool posted = workerThread->post([&]() {
        executedThreadIdPromise.set_value(std::this_thread::get_id());
    });

    // 验证任务是否成功提交
    ASSERT_TRUE(posted);

    // 等待任务执行完成（最多等待2秒）
    auto status = executedThreadIdFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    // 验证任务是在 WorkerThread 的线程上执行的
    EXPECT_EQ(executedThreadIdFuture.get(), workerThread->getThreadId());
}

// 测试3: 验证 postDelayed() 功能
// 目标: 覆盖 postDelayed()
TEST_F(WorkerThreadTest, PostDelayedTask) {
    std::promise<void> taskExecutedPromise;
    auto taskExecutedFuture = taskExecutedPromise.get_future();
    auto startTime = std::chrono::steady_clock::now();
    long delayMillis = 200;

    workerThread->start();
    
    // 提交一个延迟任务
    bool posted = workerThread->postDelayed([&]() {
        taskExecutedPromise.set_value();
    }, delayMillis);

    // 验证任务是否成功提交
    ASSERT_TRUE(posted);

    // 等待任务完成
    auto status = taskExecutedFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready);

    // 验证延迟是否生效
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    // 实际执行时间应该大于或约等于延迟时间
    EXPECT_GE(duration, delayMillis);
}

// 测试4: 验证 finish() 和析构函数
// 目标: 覆盖 finish() 和 ~WorkerThread()
TEST_F(WorkerThreadTest, FinishAndDestruction) {
    std::atomic<int> taskCount = 0;

    workerThread->start();
    auto looper = workerThread->getLooper();
    ASSERT_NE(looper, nullptr);

    // 提交几个任务
    workerThread->post([&]() { taskCount++; std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
    workerThread->post([&]() { taskCount++; std::this_thread::sleep_for(std::chrono::milliseconds(50)); });
    
    // 调用 finish()，它会把一个退出任务放到队列末尾
    bool finish_posted = workerThread->finish();
    ASSERT_TRUE(finish_posted);

    // 提交一个在 finish() 之后的新任务，理论上它不应该被执行，因为 Looper 会在处理完 finish 任务后退出
    // (注意: 这是一个竞争条件，但在多数情况下，finish 的退出任务会先被处理)
    workerThread->post([&]() { taskCount = -1; }); // 如果这个任务执行了，测试会失败

    // 等待线程自然结束
    workerThread->join();

    // 验证前两个任务被执行了
    EXPECT_EQ(taskCount, 2);
}


// 测试 finishNow() 功能，它应该跳过排队的任务
// 目标: 覆盖 finishNow()
TEST_F(WorkerThreadTest, FinishNowSkipsQueuedTasks) {
    std::atomic<int> task_execution_count = 0;
    std::promise<void> first_task_started_promise;
    auto first_task_started_future = first_task_started_promise.get_future();
    std::promise<void> first_task_finished_promise;
    auto first_task_finished_future = first_task_finished_promise.get_future();

    workerThread->start();
    auto looper = workerThread->getLooper();
    ASSERT_NE(looper, nullptr);

    // 1. 提交一个需要一些时间的任务（任务1）
    workerThread->post([&]() {
        first_task_started_promise.set_value();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        task_execution_count++; // 只有这个任务应该被计数
        first_task_finished_promise.set_value();
    });

    // 2. 提交一个普通任务，它不应该被执行（任务2）
    workerThread->post([&]() {
        task_execution_count = -1; // 如果这个任务执行了，测试会失败
    });

    // 3. 确保任务1已经开始执行
    ASSERT_EQ(first_task_started_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    
    // 4. 在任务1执行期间，调用 finishNow()
    bool finish_posted = workerThread->finishNow();
    ASSERT_TRUE(finish_posted);

    // 5. 等待线程结束
    workerThread->join();

    // 6. 验证只有第一个任务被执行了
    EXPECT_EQ(task_execution_count, 1);
}