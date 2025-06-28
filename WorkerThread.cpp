#include "WorkerThread.h"
#include <iostream>

namespace core {
 
// 这个 Handler 的唯一职责是执行 Message 中携带的 callback
WorkerThread::WorkerHandler::WorkerHandler(std::shared_ptr<Looper> looper)
    : Handler(std::move(looper)) {}

// handleMessage 的实现。
// 由于 WorkerThread 的 post 方法总是创建带有 callback 的 Message，
// 这些 Message 会被 Looper 直接处理，永远不会分发到此 handleMessage。
// 因此，我们只需要提供一个空的实现来满足基类的纯虚函数要求即可。
void WorkerThread::WorkerHandler::handleMessage(const Message& msg) {
    std::cerr << "Error in WorkerHandler::handleMessage." << std::endl;
}

// --- WorkerThread 实现 ---
WorkerThread::WorkerThread(const std::string& name)
    : HandlerThread(name), mWorkerHandler(nullptr) {
}

WorkerThread::~WorkerThread() {
    // 确保线程被正确清理
    finish();
    join();
}

void WorkerThread::start() {
    HandlerThread::start(); // 启动基类的线程和 Looper
    // Looper 准备好后，创建我们的 WorkerHandler
    std::shared_ptr<Looper> looper = getLooper();
    if (looper) {
        mWorkerHandler = std::make_shared<WorkerHandler>(looper);
    } else {
        std::cerr << "Failed to start WorkerThread: Looper is null." << std::endl;
    }
}

bool WorkerThread::post(std::function<void()> task) {
    if (!mWorkerHandler) {
        return false;
    }
    // 使用 Handler 的 post 方法提交一个 runnable
    return mWorkerHandler->post(std::move(task));
}

bool WorkerThread::postDelayed(std::function<void()> task, long delayMillis) {
    if (!mWorkerHandler) {
        return false;
    }
    // 使用 Handler 的 postDelayed 方法提交一个带延迟的 runnable
    return mWorkerHandler->postDelayed(std::move(task), delayMillis);
}

bool WorkerThread::finish() {
    if (!mWorkerHandler) {
        return false;
    }
    // 提交一个特殊的任务来停止 Looper
    // 这是最优雅的关闭方式，因为它能确保所有在它之前的任务都执行完毕
    return mWorkerHandler->post([this]() {
        this->quit();
    });
}

} // namespace core