#ifndef HANDLER_THREAD_H
#define HANDLER_THREAD_H

#include "looper_handler.h" // 依赖 Looper
#include <thread>
#include <string>
#include <future>   // For std::promise and std::shared_future
#include <memory>   // For std::shared_ptr
#include <mutex>    // For std::mutex

namespace core {

    /**
     * @class HandlerThread
     * @brief 一个封装了 Looper 的线程类。
     *
     * HandlerThread 是一个便利的类，用于启动一个带有 Looper 的新线程。
     * 这个 Looper 可以用来创建 Handler，以便在新线程上处理消息和可运行对象。
     * 调用 start() 来启动线程，然后调用 getLooper() 来获取该线程的 Looper。
     * 当不再需要时，调用 quit() 或 quitSafely() 来停止线程。
     */
    class HandlerThread {
    public:
        /**
         * @brief 构造函数。
         * @param name 线程的描述性名称。
         */
        explicit HandlerThread(const std::string& name = "HandlerThread");

        /**
         * @brief 析构函数。
         *
         * 会自动尝试停止并加入线程，以防止资源泄漏。
         * 强烈建议在销毁对象前手动调用 quit() 和 join()。
         */
        virtual ~HandlerThread();

        // 禁止拷贝和移动，以避免对底层线程和 Looper 的所有权混淆
        HandlerThread(const HandlerThread&) = delete;
        HandlerThread& operator=(const HandlerThread&) = delete;
        HandlerThread(HandlerThread&&) = delete;
        HandlerThread& operator=(HandlerThread&&) = delete;

        /**
         * @brief 启动线程。
         */
        void start();

        /**
         * @brief 获取与此 HandlerThread 关联的 Looper。
         * @return 指向此线程 Looper 的共享指针。
         */
        std::shared_ptr<Looper> getLooper();

        /**
         * @brief 请求 Looper 退出。
         * @return 如果 Looper 存在则返回 true，否则返回 false。
         */
        bool quit();

        /**
         * @brief 等待线程终止。
         */
        void join();

        /**
         * @brief 获取线程的 ID。
         * @return 线程的 ID。
         */
        std::thread::id getThreadId() const;

    private:
        /**
         * @brief 线程的主执行函数。
         */
        void run();

        std::string mName;
        std::thread mThread;
        std::shared_ptr<Looper> mLooper;

        std::promise<std::shared_ptr<Looper>> mLooperPromise;
        std::shared_future<std::shared_ptr<Looper>> mLooperFuture;

        std::mutex mMutex;
    };

} // namespace core

#endif // HANDLER_THREAD_H