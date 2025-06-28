#include "HandlerThread.h" // 包含对应的头文件
#include <iostream>         // 为了 std::cerr
#include <stdexcept>        // 为了 std::runtime_error

namespace core {

    HandlerThread::HandlerThread(const std::string& name)
        // `mLooperPromise` 和 `mLooperFuture` 是实现线程安全初始化的关键。
        // `mLooperPromise` 会被新启动的线程 `run()` 用来设置 Looper 的值（或异常）。
        // `mLooperFuture` 则被 `getLooper()` 方法用来等待并获取这个值。
        // 使用 `share()` 将 future 转换为 `shared_future`，允许多次调用 `get()`。
        : mName(name), mLooper(nullptr), mLooperFuture(mLooperPromise.get_future().share()) {
    }

    HandlerThread::~HandlerThread() {
        if (mThread.joinable()) {
            quit();
            join();
        }
    }

    void HandlerThread::start() {
        if (mThread.joinable()) {
            return;
        }
        mThread = std::thread(&HandlerThread::run, this);
    }

    void HandlerThread::run() {
        // 1. 调用 `Looper::prepare()` 为当前线程创建一个 Looper。
        // 2. 通过 `mLooperPromise.set_value()` 将创建好的 Looper “发布”出去，
        //    这样等待在 `mLooperFuture` 上的 `getLooper()` 调用就可以收到了。
        // 3. 调用 `Looper::loop()` 进入消息循环，阻塞直到 `quit()` 被调用。
        // 使用 try-catch 块是为了在 Looper 准备失败时，能将异常传递出去。
        try {
            Looper::prepare();

            auto myLooper = Looper::myLooper();
            if (!myLooper) {
                throw std::runtime_error("Looper::myLooper() returned null after prepare()");
            }

            mLooperPromise.set_value(myLooper);

            Looper::loop();

        }
        catch (...) {
            // 如果 `prepare` 或 `loop` 的早期阶段失败，捕获异常并通过 promise 传递。
            // 这样，在主线程调用 `getLooper()` 时就会重新抛出这个异常，调用者就能知道失败了。
            try {
                mLooperPromise.set_exception(std::current_exception());
            }
            catch (...) {
                // Failsafe in case set_exception itself throws.
            }
        }
    }

    std::shared_ptr<Looper> HandlerThread::getLooper() {
        if (!mThread.joinable()) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(mMutex);
        if (!mLooper) {            
            // 第一次调用 `getLooper()` 时，`mLooper` 是 nullptr。
            // 此时，它会调用 `mLooperFuture.get()`。这个调用会阻塞，直到 `run()` 方法在新线程中
            // 调用了 `mLooperPromise.set_value()` 或 `set_exception()`。
            // 这就完美地解决了时序问题：保证了 `getLooper()` 返回时，Looper 必定已经被安全地创建好了。
            // 后续再次调用 `getLooper()` 时，`mLooper` 已经有值，会直接返回缓存的指针，不会再阻塞。
            try {
                mLooper = mLooperFuture.get();
            }
            catch (const std::exception& e) {
                std::cerr << "HandlerThread (" << mName << ") failed to create Looper: " << e.what() << std::endl;
                mLooper = nullptr;
            }
        }
        return mLooper;
    }

    bool HandlerThread::quit() {
        std::shared_ptr<Looper> looper = getLooper();
        if (looper) {
            looper->quit();
            return true;
        }
        return false;
    }

    void HandlerThread::join() {
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    std::thread::id HandlerThread::getThreadId() const {
        return mThread.get_id();
    }

} // namespace core


#if 0
#include "handler_thread.h" // 包含我们刚创建的 HandlerThread
#include <iostream>
#include <chrono>

using namespace std::chrono_literals;
using namespace core;

// 定义消息类型
const int MSG_WORK = 1;
const int MSG_FINISH = 2;

// 一个简单的 Handler 实现，用于在 HandlerThread 上工作
class MyWorkerHandler : public Handler {
public:
    // 构造函数接收一个 Looper，这是 HandlerThread 提供的
    explicit MyWorkerHandler(std::shared_ptr<Looper> looper) : Handler(looper) {}

    void handleMessage(const Message& msg) override {
        // 确认消息是在 HandlerThread 的线程上处理的
        std::cout << "[Worker Thread " << std::this_thread::get_id() << "] "
            << "Handling message: " << msg.what << std::endl;

        switch (msg.what) {
        case MSG_WORK: {
            // 模拟一些工作
            std::cout << "  -> Doing some work for 2 seconds..." << std::endl;
            std::this_thread::sleep_for(2s);
            std::cout << "  -> Work finished." << std::endl;
            break;
        }
        case MSG_FINISH: {
            // 收到结束消息后，退出 Looper
            std::cout << "  -> Finish message received. Quitting Looper." << std::endl;
            getLooper()->quit();
            break;
        }
        }
    }
};

int main() {
    std::cout << "[Main Thread " << std::this_thread::get_id() << "] Starting..." << std::endl;

    // 1. 创建一个 HandlerThread 实例
    HandlerThread workerThread("MyWorkerThread");

    // 2. 启动线程。这将在后台准备 Looper。
    workerThread.start();
    std::cout << "[Main Thread] Worker thread started with ID: " << workerThread.getThreadId() << std::endl;

    // 3. 获取与 HandlerThread 关联的 Looper。
    //    这一步会阻塞，直到后台线程中的 Looper 准备就绪。
    std::shared_ptr<Looper> looper = workerThread.getLooper();
    if (!looper) {
        std::cerr << "[Main Thread] Failed to get looper from worker thread." << std::endl;
        workerThread.join(); // 清理
        return 1;
    }
    std::cout << "[Main Thread] Successfully got Looper for thread " << looper->getThreadId() << std::endl;

    // 4. 使用获取到的 Looper 创建一个 Handler。
    //    这个 Handler 发送的所有消息都将在 workerThread 中执行。
    auto handler = std::make_shared<MyWorkerHandler>(looper);

    // 5. 使用 Handler 发送/发布任务到工作线程
    std::cout << "[Main Thread] Posting a runnable to the worker thread." << std::endl;
    handler->post([]() {
        std::cout << "[Worker Thread " << std::this_thread::get_id() << "] "
            << "Executing a posted runnable." << std::endl;
        });

    std::cout << "[Main Thread] Sending a work message to the worker thread." << std::endl;
    handler->sendMessage(handler->obtainMessage(MSG_WORK));

    // 发送一个延迟的退出消息
    std::cout << "[Main Thread] Sending a delayed finish message (3 seconds)." << std::endl;
    handler->sendMessageDelayed(handler->obtainMessage(MSG_FINISH), 3000);

    std::cout << "[Main Thread] All messages sent. Waiting for worker thread to finish." << std::endl;

    // 6. 等待工作线程结束。
    //    析构函数会自动调用 quit() 和 join()，但显式调用是更好的实践。
    workerThread.join();

    std::cout << "[Main Thread " << std::this_thread::get_id() << "] Worker thread has finished. Exiting." << std::endl;

    return 0;
}
#endif
