#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H

#include "HandlerThread.h"
#include <functional>
#include <memory>

namespace core {

/**
 * @class WorkerThread
 * @brief 一个专门用于在后台串行执行耗时任务的线程类。
 *
 * WorkerThread 继承自 HandlerThread，并提供了一个简单的接口
 * 来提交（post）任务到后台线程执行。所有提交的任务将按照
 * 提交的顺序在一个单独的线程中串行执行。
 */
class WorkerThread final : public HandlerThread {
public:
    /**
     * @brief 构造函数。
     * @param name 线程的描述性名称。
     */
    explicit WorkerThread(const std::string& name = "WorkerThread");

    /**
     * @brief 析构函数。
     */
    ~WorkerThread() override;

    /**
     * @brief 启动工作线程。
     *
     * 在调用任何 post 方法之前必须先调用此方法。
     */
    void start();

    /**
     * @brief 提交一个任务到工作线程立即执行。
     * @param task 要执行的任务，一个不带参数也无返回值的可调用对象 (std::function<void()>).
     * @return 如果任务成功提交，返回 true；否则返回 false。
     */
    bool post(std::function<void()> task);

    /**
     * @brief 提交一个任务到工作线程，在指定的延迟后执行。
     * @param task 要执行的任务。
     * @param delayMillis 延迟时间（毫秒）。
     * @return 如果任务成功提交，返回 true；否则返回 false。
     */
    bool postDelayed(std::function<void()> task, long delayMillis);

    /**
     * @brief 完成并优雅地停止工作线程。
     *
     * 会向任务队列发送一个停止消息。队列中所有待处理的任务
     * 执行完毕后，线程将退出。
     * 调用此方法后，不应再提交新任务。
     * @return 如果停止消息成功发送，返回 true。
     */
    bool finish();

    /**
     * @brief 立即停止工作线程（在当前任务完成后）。
     *
     * 提交一个高优先级的退出消息到队列头部，跳过所有其他排队的任务。
     * 当前正在执行的任务仍会完成。
     * @return 如果停止消息成功发送，返回 true。
     */
    bool finishNow();
        
    
private:
    // 一个简单的内部 Handler，仅用于处理 std::function<void()> 任务
    class WorkerHandler : public Handler {
    public:
        explicit WorkerHandler(std::shared_ptr<Looper> looper);
        void handleMessage(const Message& msg) override;
    };

    std::shared_ptr<WorkerHandler> mWorkerHandler;

public:
    std::shared_ptr<WorkerHandler> getHandler() { return mWorkerHandler;};    
};

} // namespace core

#endif // WORKER_THREAD_H