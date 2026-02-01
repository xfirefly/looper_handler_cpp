#pragma once
#include "WorkerThread.h"
#include <functional>
#include <memory>
#include <chrono>
#include <mutex>

namespace core {

    /**
     * @class Debouncer
     * @brief 基于 WorkerThread 的防抖动执行器。
     *
     * Debouncer 确保在给定的延迟时间内，如果连续多次调用，只有最后一次调用会被执行。
     * 与之前的版本不同，此版本不创建新线程，而是利用 WorkerThread 将任务调度到目标线程执行。
     */
    template<typename... Args>
    class Debouncer {
    public:
        /**
         * @brief 构造函数
         * @param worker 用于调度延迟任务的 WorkerThread (例如 WorkerThread 的 worker)。
         * @param func 要执行的目标函数。
         * @param delay 防抖延迟时间。
         */
        Debouncer(std::shared_ptr<WorkerThread> worker, std::function<void(Args...)> func, std::chrono::milliseconds delay)
            : worker_(std::move(worker)), func_(std::move(func)), delay_(delay) {
            if (!worker_) {
                throw std::invalid_argument("Debouncer: WorkerThread cannot be null.");
            }
        }

        /**
         * @brief 析构函数
         * 确保对象销毁时，所有待执行的任务都被逻辑取消，防止回调执行。
         */
        ~Debouncer() {
            std::lock_guard<std::mutex> lock(mutex_);
            if (active_token_) {
                *active_token_ = false; // 逻辑取消当前等待的任务
            }
        }

        /**
         * @brief 触发调用
         * 每次调用都会重置计时器。只有在延迟时间内没有新的调用，函数才会执行。
         */
        void operator()(Args... args) {
            std::lock_guard<std::mutex> lock(mutex_);

            // 1. 取消上一次的调用（如果存在）
            // 我们通过将旧的 token 标记为 false 来实现逻辑取消。
            // 之前的任务在 WorkerThread 中醒来时会检查这个值。
            if (active_token_) {
                *active_token_ = false;
            }

            // 2. 创建新的 Token
            active_token_ = std::make_shared<bool>(true);
            std::weak_ptr<bool> weak_token = active_token_;

            // 3. 复制函数对象
            // 关键：我们按值捕获 func_ 的副本。这确保了即使 Debouncer 实例在任务执行前被销毁，
            // Lambda 内部也不会因为访问 `this->func_` 而导致悬空指针崩溃。
            auto func_copy = func_;

            // 4. 提交延迟任务到 WorkerThread
            // 注意：args... 也会被按值捕获（复制）进 Lambda
            worker_->postDelayed([weak_token, func_copy, args...]() {
                // 在 WorkerThread 线程中执行：
                // 尝试锁定 weak_ptr。如果 Debouncer 已经析构且 Token 引用计数归零，lock 会返回空。
                if (auto token = weak_token.lock()) {
                    // 检查 Token 值。如果为 true，说明这是最新的任务且未被取消。
                    if (*token) {
                        func_copy(args...);
                    }
                }
            }, delay_.count());
        }

    private:
        std::shared_ptr<WorkerThread> worker_;
        std::function<void(Args...)> func_;
        std::chrono::milliseconds delay_;

        std::shared_ptr<bool> active_token_; // 当前有效的执行令牌
        std::mutex mutex_;                   // 保护 active_token_ 的线程安全
    };

} // namespace core