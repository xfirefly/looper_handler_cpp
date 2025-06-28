#include "looper_handler.h"
#include <thread>
#include <future>
#include <iostream>
#include <string>
#include <chrono>
#include "Preferences.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace core;
using namespace std::chrono_literals;

const int MSG_TASK_A = 1;
const int MSG_TASK_B = 2;
const int MSG_TASK_C_OBTAINED = 3; // New message type for testing
const int MSG_SHUTDOWN = 99;

class WorkerHandler : public Handler {
public:
    explicit WorkerHandler(std::shared_ptr<Looper> looper) : Handler(looper) {}

    void handleMessage(const Message& msg) override {
        std::cout << "Worker Handler (" << getLooper()->getThreadId() << " vs " << std::this_thread::get_id() 
                  << ") received message: " << msg.what << std::endl;

        switch (msg.what) {
            case MSG_TASK_A:
                std::cout << "  Processing Task A... Arg1=" << msg.arg1 << std::endl;
                std::this_thread::sleep_for(50ms);
                std::cout << "  Task A finished." << std::endl;
                break;
            case MSG_TASK_B:
                std::cout << "  Processing Task B... Arg2=" << msg.arg2 << std::endl;
                 try {
                     std::string data = std::any_cast<std::string>(msg.obj);
                     std::cout << "  Data: " << data << std::endl;
                 } catch (const std::bad_any_cast& e) {
                     std::cerr << "  Failed to cast obj: " << e.what() << std::endl;
                 }
                std::cout << "  Task B finished." << std::endl;
                break;
            case MSG_TASK_C_OBTAINED: // Handle new message type
                std::cout << "  Processing Task C (obtained)... Arg1=" << msg.arg1 << ", Arg2=" << msg.arg2 << std::endl;
                if (msg.obj.has_value()) {
                    try {
                        std::string data = std::any_cast<std::string>(msg.obj);
                        std::cout << "  Data for C: " << data << std::endl;
                    } catch (const std::bad_any_cast& e) {
                        std::cerr << "  Failed to cast obj for C: " << e.what() << std::endl;
                    }
                }
                std::cout << "  Task C finished." << std::endl;
                break;
            case MSG_SHUTDOWN:
                 std::cout << "  Shutdown message received. Quitting Looper." << std::endl;
                 getLooper()->quit();
                 break;
            default:
                std::cout << "  Unknown message type." << std::endl;
                break;
        }
    }


    static void worker_thread(std::promise<std::shared_ptr<Looper>> looperPromise);
	static std::shared_ptr<WorkerHandler> createWorker();
};

void WorkerHandler::worker_thread(std::promise<std::shared_ptr<Looper>> looperPromise) {    
    try {
        Looper::prepare();
        auto myLooper = Looper::myLooper();
        looperPromise.set_value(myLooper);

        Looper::loop(); // This blocks until quit

    }
    catch (const std::exception& e) {
        std::cerr << "Worker thread exception: " << e.what() << std::endl;
        try { looperPromise.set_exception(std::current_exception()); }
        catch (...) {}
    }
     
}

// not use
std::shared_ptr<WorkerHandler> WorkerHandler::createWorker() {
    std::promise<std::shared_ptr<Looper>> looperPromise;
    auto looperFuture = looperPromise.get_future();

    std::thread worker(WorkerHandler::worker_thread, std::move(looperPromise));
    worker.detach();

    std::shared_ptr<Looper> workerLooper = looperFuture.get();
    if (!workerLooper) {
        std::cerr << "Failed to get worker Looper!" << std::endl;
        if (worker.joinable()) worker.join();

    }

    return std::move(std::make_shared<WorkerHandler>(workerLooper));
}


void worker_thread_with_promise(std::promise<std::shared_ptr<Looper>> looperPromise) {
    std::cout << "Worker thread (" << std::this_thread::get_id() << ") started." << std::endl;
    try {
        Looper::prepare();
        auto myLooper = Looper::myLooper();
        looperPromise.set_value(myLooper);

        Looper::loop(); // This blocks until quit

    } catch (const std::exception& e) {
        std::cerr << "Worker thread exception: " << e.what() << std::endl;
        try { looperPromise.set_exception(std::current_exception()); } catch(...) {}
    }
    std::cout << "Worker thread (" << std::this_thread::get_id() << ") finished Looper::loop." << std::endl;
}


int main() { // Or main01, main02 etc. if you want to keep old mains
    std::cout << "Main thread (" << std::this_thread::get_id() << ") started." << std::endl;

    std::promise<std::shared_ptr<Looper>> looperPromise;
    auto looperFuture = looperPromise.get_future();

    std::thread worker(worker_thread_with_promise, std::move(looperPromise));

    std::shared_ptr<Looper> workerLooper = looperFuture.get();
    if (!workerLooper) {
        std::cerr << "Failed to get worker Looper!" << std::endl;
        if(worker.joinable()) worker.join();
        return 1;
    }
    std::cout << "Main thread obtained worker Looper for thread " << workerLooper->getThreadId() << std::endl;

    auto workerHandler = std::make_shared<WorkerHandler>(workerLooper);
     

    // 1. Send a simple message
    workerHandler->sendMessage(Message(MSG_TASK_A, 123, 0)); // Target explicitly set here for demo

    // 2. Send a message with data
    workerHandler->sendMessage(Message(MSG_TASK_B, std::string("Hello from Main! (direct send)")));

    // 3. Post a delayed callback
    workerHandler->postDelayed([workerHandler]() { // Capture workerHandler
        std::cout << "Worker Callback (" << std::this_thread::get_id() << ") executed after delay!" << std::endl;
        workerHandler->sendMessage(Message(MSG_TASK_A, 456, 0)); // Send another message
    }, 100); // Shortened delay for quicker testing

    // 4. Use obtainMessage().sendToTarget() <<<<<< NEW TEST CASE
    std::cout << "Main thread: obtaining and sending MSG_TASK_C_OBTAINED." << std::endl;
    Message obtainedMsg1 = workerHandler->obtainMessage(MSG_TASK_C_OBTAINED, 100, 200);
    obtainedMsg1.sendToTarget(); // Message is moved from here

    Message obtainedMsg2 = workerHandler->obtainMessage(MSG_TASK_C_OBTAINED, 777, 888, std::string("Data via obtainMessage"));
    // obtainedMsg2 will be sent delayed
    // To send it delayed, we can't use its own sendToTarget() directly with delay.
    // We'd use handler's sendMessageDelayed
    // Let's send it immediately for this test:
    // obtainedMsg2.sendToTarget(); // This would work
    // Or, more flexibly, if we wanted to send it delayed:
    workerHandler->sendMessageDelayed(std::move(obtainedMsg2), 200);


    // 5. Send a shutdown message, slightly delayed to allow other messages to process
    workerHandler->sendMessageDelayed(Message(MSG_SHUTDOWN, std::shared_ptr<Handler>(workerHandler)), 500);

    std::cout << "Main thread finished sending messages." << std::endl;

    worker.join();
    std::this_thread::sleep_for(5000ms);

    std::cout << "Main thread exiting." << std::endl;
    return 0;
}



// use  std::packaged_task
int main00() {
    std::cout << "Main thread (" << std::this_thread::get_id() << ") started." << std::endl;

    // 1. 定义一个函数（或 Lambda）来执行 Looper 的准备工作并返回 Looper 实例
    auto prepareAndGetLooper = []() -> std::shared_ptr<Looper> {
        Looper::prepare();
        return Looper::myLooper();
    };

    // 2. 创建 packaged_task，包装我们的准备函数
    std::packaged_task<std::shared_ptr<Looper>()> setupTask(prepareAndGetLooper);

    // 3. 获取 future
    std::future<std::shared_ptr<Looper>> looperFuture = setupTask.get_future();

    // 4. 启动线程，**关键点**：线程需要先执行 task，然后再执行 Looper::loop()
    std::thread worker([task = std::move(setupTask)]() mutable {
        try {
            task(); // **执行 task，这会自动设置 future 的值或异常**
            Looper::loop(); // **启动循环**
            std::cout << "Looper thread exiting normally." << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Looper thread caught exception (during setup or loop): " << e.what() << std::endl;
            // 注意：如果异常发生在 loop() 中，packaged_task 的 future 不会捕捉到它。
            // packaged_task 只捕捉包装函数内的异常。
            // 如果 setupTask() 抛出异常，future 会捕捉到。
        }
    });

  
    // 等待工作线程准备好 Looper 并获取它
    std::shared_ptr<Looper> workerLooper = looperFuture.get();
    if (!workerLooper) {
        std::cerr << "Failed to get worker Looper!" << std::endl;
        if(worker.joinable()) worker.join();
        return 1;
    }
    std::cout << "Main thread obtained worker Looper." << std::endl;

    // 创建一个与工作线程 Looper 关联的 Handler
    auto workerHandler = std::make_shared<WorkerHandler>(workerLooper);

    // --- 开始发送消息 ---

    // 1. 发送一个简单的消息 (使用 (int, int, int, shared_ptr) 构造函数)
    workerHandler->sendMessage(Message(MSG_TASK_A, 123, 0, workerHandler));

    // 2. 发送一个带数据的消息 (使用 (int, any, shared_ptr) 构造函数)
    workerHandler->sendMessage(Message(MSG_TASK_B, std::string("Hello from Main!"), workerHandler));

    // 3. 发布一个延迟执行的回调 (Runnable)
    workerHandler->postDelayed([=]() { // 使用 [=] 捕获 workerHandler
        // !!! 这段代码将在工作线程执行 !!!
        std::cout << "Worker Callback (" << std::this_thread::get_id() << ") executed after delay!" << std::endl;
        
        // 甚至可以在回调中发送更多消息
        workerHandler->sendMessage(Message(MSG_TASK_A, 456, 0, workerHandler));

    }, 2000); // 延迟 200 毫秒

    // 4. 发送一个延迟消息
    workerHandler->sendMessageDelayed(Message(MSG_SHUTDOWN), 5000); // 500ms 后关闭

    std::cout << "Main thread finished sending messages." << std::endl;

    // 等待工作线程结束 (Looper::loop() 返回后)
    worker.join();

    std::cout << "Main thread exiting." << std::endl;
    return 0;
}


 
 