#include "looper_handler.h" // Include the header first

#include <iostream> // For std::cout, std::cerr (used in implementations and main)
#include <algorithm> // for std::find_if, std::sort, std::remove_if, std::upper_bound (used in MessageQueue impl)
#include <utility>   // For std::move

namespace core {
    // --- Message Implementation ---

    // Convenience constructor for simple messages 
    Message::Message(int w, std::shared_ptr<Handler> t) : what(w), target(std::move(t)) {}

    // Convenience constructor for messages with args 
    Message::Message(int w, int a1, int a2, std::shared_ptr<Handler> t)
        : what(w), arg1(a1), arg2(a2), target(std::move(t)) {
    }

    // Convenience constructor for messages with data object 
    Message::Message(int w, std::any o, std::shared_ptr<Handler> t)
        : what(w), obj(std::move(o)), target(std::move(t)) {
    }

    // Convenience constructor for runnables (callback) 
    Message::Message(std::function<void()> cb, std::shared_ptr<Handler> t)
        : callback(std::move(cb)), target(std::move(t)) {
    }

    // Sends this message to the Handler specified by Message.target.
    // Will assign a timestamp to the message before dispatching it.
    // Returns false if the target is null or the queue is quitting.
    bool Message::sendToTarget() { // << NEW METHOD IMPLEMENTATION
        if (!target) {
            // Or throw an exception, or log an error
            std::cerr << "Error: Message::sendToTarget() called with no target Handler." << std::endl;
            return false;
        }
        // Message struct itself doesn't know about mQueue or time directly.
        // It relies on its target Handler to do the enqueuing.
        // The Handler's sendMessageAtTime will set the 'when'.
        // We pass `std::move(*this)` because sendMessageAtTime expects Message&&
        return target->sendMessageAtTime(std::move(*this), std::chrono::steady_clock::now());
    }

    // --- MessageQueue Implementation ---
    MessageQueue::MessageQueue() = default; // Definition
    MessageQueue::~MessageQueue() = default; // Definition

    // Enqueues a message. Messages are sorted by their 'when' time. 
    bool MessageQueue::enqueueMessage(Message&& msg, std::chrono::steady_clock::time_point when) {
        std::unique_lock<std::mutex> lock(mMutex); // Use the mutable mutex
        if (mQuitting) {
            std::cerr << "Warning: Enqueuing message on a quitting queue." << std::endl;
            return false; // Don't enqueue if quitting
        }

        msg.when = when;
        // Insert message sorted by 'when'
        // 使用 std::upper_bound 而不是简单的 push_back，是为了维持消息队列的时间顺序。
        // 这样，`next()` 方法总是可以只看队列的头部，来确定下一条需要处理的消息以及需要等待多久。
        // 这是一种空间换时间的优化，避免了在 `next()` 中遍历整个队列来寻找最早的消息。
        auto it = std::upper_bound(mMessages.begin(), mMessages.end(), msg,
            [](const Message& a, const Message& b) {
                return a.when < b.when;
            });
        mMessages.insert(it, std::move(msg));

        // 理论上，只有当插入的消息成为新的队首时（即它是最早需要执行的任务），或者队列之前为空时，
        // 才“必须”唤醒线程。但是，为了逻辑简化和健壮性，这里选择总是通知。
        // 这样可以避免复杂的条件判断，性能开销在绝大多数情况下可以忽略不计。
        // Notify the looper thread only if the new message is at the front
        // or if the queue was previously potentially blocked waiting.
        // Simpler: always notify. More robust, performance impact usually negligible.
        // if (it == mMessages.begin()) { // Check if inserted at beginning
        mCondVar.notify_one();
        // }
        return true;
    }

    // Retrieves the next message. Blocks if the queue is empty or
    // the next message is scheduled for the future.
    // Returns std::nullopt if the queue is quitting. 
    std::optional<Message> MessageQueue::next() {
        std::unique_lock<std::mutex> lock(mMutex); // Use the mutable mutex
        auto now = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point nextPollTimeout = now; // Initialize just in case

        while (true) {
            if (mQuitting) {
                return std::nullopt; // Return nullopt if quitting
            }

            if (!mMessages.empty()) {
                const auto& frontMsg = mMessages.front();
                if (frontMsg.when <= now) {
                    // Message is ready to be processed
                    Message msg = std::move(mMessages.front());
                    mMessages.pop_front();
                    return std::move(msg);
                }
                else {
                    // Next message is scheduled for the future, calculate wait time
                    nextPollTimeout = frontMsg.when;
                }
            }
            else {
                // Queue is empty, wait indefinitely until notified
                nextPollTimeout = std::chrono::steady_clock::time_point::max();
            }

            // `mCondVar.wait_until` 或 `mCondVar.wait` 会原子地解锁互斥锁 `mMutex` 并让线程进入休眠。
            // 这样做是为了避免 CPU 空转，节省资源。
            // 线程会被以下两种情况之一唤醒：
            // 1. `enqueueMessage` 调用了 `mCondVar.notify_one()`，表示有新消息。
            // 2. 等待时间达到了 `nextPollTimeout`，表示队首的延迟消息可能到期了。
            // 唤醒后，线程会重新获取锁，并从 `while(true)` 的顶部开始下一次循环，重新判断状态。
            // Wait until the next message's time or until notified
            if (nextPollTimeout == std::chrono::steady_clock::time_point::max()) {
                mCondVar.wait(lock); // Wait indefinitely
            }
            else {
                mCondVar.wait_until(lock, nextPollTimeout);
            }

            // After waking up, re-evaluate the time and queue state
            now = std::chrono::steady_clock::now();
        }
    }

    // Signals the queue to stop processing messages. 
    void MessageQueue::quit() {
        std::unique_lock<std::mutex> lock(mMutex); // Use the mutable mutex
        if (!mQuitting) {
            mQuitting = true;
            mMessages.clear(); // Optionally clear pending messages on quit
            mCondVar.notify_all(); // Wake up the looper thread if it's waiting
        }
    }

    // 
    bool MessageQueue::isQuitting() const {
        std::lock_guard<std::mutex> lock(mMutex); // Use the mutable mutex
        return mQuitting;
    }

    // Removes messages for a specific handler with a specific 'what' code 
    void MessageQueue::removeMessages(const std::shared_ptr<Handler>& h, int what) {
        std::unique_lock<std::mutex> lock(mMutex); // Use the mutable mutex
        if (mQuitting) return;

        mMessages.erase(std::remove_if(mMessages.begin(), mMessages.end(),
            [&](const Message& msg) {
                return msg.target == h && msg.what == what && !msg.callback; // Only remove non-callback messages
            }),
            mMessages.end());
    }

    // Removes callback runnables for a specific handler 
    void MessageQueue::removeCallbacks(const std::shared_ptr<Handler>& h) {
        std::unique_lock<std::mutex> lock(mMutex); // Use the mutable mutex
        if (mQuitting) return;

        mMessages.erase(std::remove_if(mMessages.begin(), mMessages.end(),
            [&](const Message& msg) {
                return msg.target == h && msg.callback; // Only remove callback messages
            }),
            mMessages.end());
    }


    // --- Looper Implementation ---

    // Initialize the static thread_local variable 
    thread_local std::shared_ptr<Looper> Looper::tLooper = nullptr;

    // Private constructor: Use static methods prepare()/myLooper() 
    Looper::Looper()
        : mQueue(std::make_unique<MessageQueue>()), mThreadId(std::this_thread::get_id()) // Initializer list sets members
    {
    }

    // Prepares a Looper for the calling thread. Must be called before loop(). 
    void Looper::prepare() {
        if (tLooper) {
            throw std::runtime_error("Looper already prepared for this thread.");
        }
        // Create Looper and store it in thread_local variable
        struct LooperMaker : public Looper { LooperMaker() : Looper() {} }; // Helper to access private constructor
        tLooper = std::make_shared<LooperMaker>();
    }

    // Returns the Looper associated with the calling thread.
    // Returns nullptr if Looper::prepare() hasn't been called. 
    std::shared_ptr<Looper> Looper::myLooper() {
        return tLooper;
    }


    // Starts the message loop for the calling thread's Looper.
    // This function will block until quit() is called. 
    void Looper::loop() {
        auto me = myLooper();
        if (!me) {
            throw std::runtime_error("No Looper; Looper::prepare() wasn't called on this thread.");
        }
        if (me->mThreadId != std::this_thread::get_id()) {
            throw std::runtime_error("Looper::loop() must be called from the Looper's thread.");
        }

        std::cout << "Looper starting loop on thread: " << std::this_thread::get_id() << std::endl;

        while (true) {
            // Check if queue exists before calling next()
            if (!me->mQueue) {
                std::cerr << "Looper thread " << std::this_thread::get_id() << " exiting: message queue is null." << std::endl;
                break; // Should not happen normally
            }

            std::optional<Message> msgOpt = me->mQueue->next();

            if (!msgOpt) {
                // Queue is quitting or returned nullopt for other reasons
                std::cout << "Looper exiting loop on thread " << std::this_thread::get_id() << std::endl;
                break;
            }

            Message msg = std::move(*msgOpt);

            // Dispatch the message
            if (msg.target) {
                // If it's a callback message, run the callback
                if (msg.callback) {
                    try {
                        msg.callback();
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Exception in Handler callback: " << e.what() << std::endl;
                    }
                    catch (...) {
                        std::cerr << "Unknown exception in Handler callback." << std::endl;
                    }
                }
                else {
                    // Otherwise, call the handler's dispatchMessage (which calls handleMessage)
                    // Now Handler definition is complete, so this call is valid. 
                    try {
                        msg.target->dispatchMessage(msg);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Exception in handleMessage or dispatchMessage: " << e.what() << std::endl;
                    }
                    catch (...) {
                        std::cerr << "Unknown exception in handleMessage or dispatchMessage." << std::endl;
                    }
                }
            }
            else {
                std::cerr << "Warning: Message received without target handler." << std::endl;
                // Optionally, just discard the message or log more details
                if (msg.callback) {
                    std::cerr << "Warning: Callback message received without target handler. Running callback anyway." << std::endl;
                    try { msg.callback(); }
                    catch (...) { /* Ignore exceptions? Log? */ }
                }
            }
        }
        // Clean up thread-local storage when loop exits
        tLooper = nullptr; // Reset the thread-local pointer for this thread
    }


    // Stops the Looper safely. Can be called from any thread. 
    void Looper::quit() {
        if (mQueue) {
            mQueue->quit();
        }
    }

    // Gets the message queue associated with this Looper.
    // Be cautious when using the queue directly. 
    MessageQueue* Looper::getQueue() const {
        return mQueue.get();
    }

    // 
    std::thread::id Looper::getThreadId() const {
        return mThreadId;
    }


    // --- Handler Implementation ---

    // Creates a Handler associated with the Looper for the current thread.
    // Throws if Looper::prepare() wasn't called. 
    Handler::Handler() : mLooper(Looper::myLooper()) {
        if (!mLooper) {
            throw std::runtime_error("Can't create handler inside thread that has not called Looper::prepare()");
        }
        mQueue = mLooper->getQueue();
        assert(mQueue != nullptr); // Ensure queue exists
    }

    // Creates a Handler associated with a specific Looper. 
    Handler::Handler(std::shared_ptr<Looper> looper) : mLooper(std::move(looper)) {
        if (!mLooper) {
            throw std::invalid_argument("Looper cannot be null");
        }
        mQueue = mLooper->getQueue();
        assert(mQueue != nullptr); // Ensure queue exists
    }

    // Subclasses must implement this to receive messages. (Pure virtual - no definition here)
    // virtual void handleMessage(const Message& msg) = 0;

    // Final method pattern: dispatches message to handleMessage
    // Marked virtual in case derived classes want to intercept dispatching (though unusual) 
    void Handler::dispatchMessage(const Message& msg) {
        handleMessage(msg); // Calls the pure virtual function (must be overridden)
    }

    // --- Message Sending Methods ---

    // Sends a Message. It will be handled in handleMessage(). 
    bool Handler::sendMessage(Message&& msg) {
        auto now = std::chrono::steady_clock::now();
        return sendMessageAtTime(std::move(msg), now);
    }

    // Sends a Message with a delay. 
    bool Handler::sendMessageDelayed(Message&& msg, long delayMillis) {
        if (delayMillis < 0) delayMillis = 0;
        auto when = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMillis);
        return sendMessageAtTime(std::move(msg), when);
    }

    // Sends a Message to be processed at a specific time. 
    bool Handler::sendMessageAtTime(Message&& msg, std::chrono::steady_clock::time_point uptimeMillis) {
        if (!mQueue) return false; // Should not happen if constructor succeeded
        msg.target = shared_from_this(); // Set the target handler
        return mQueue->enqueueMessage(std::move(msg), uptimeMillis);
    }

    // --- Runnable Posting Methods ---

    // Posts a task (std::function) to be run on the Handler's thread. 
    bool Handler::post(std::function<void()> r) {
        auto now = std::chrono::steady_clock::now();
        return postAtTime(std::move(r), now);
    }

    // Posts a task with a delay. 
    bool Handler::postDelayed(std::function<void()> r, long delayMillis) {
        if (delayMillis < 0) delayMillis = 0;
        auto when = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMillis);
        return postAtTime(std::move(r), when);
    }

    // Posts a task to be run at a specific time. 
    bool Handler::postAtTime(std::function<void()> r, std::chrono::steady_clock::time_point uptimeMillis) {
        if (!mQueue) return false;
        Message msg(std::move(r), shared_from_this()); // Create message with callback
        return mQueue->enqueueMessage(std::move(msg), uptimeMillis);
    }

    // --- Message Obtaining Methods --- (NEW IMPLEMENTATIONS)
    Message Handler::obtainMessage() {
        // In a real system, this might come from a pool. Here, we just construct.
        Message msg;
        msg.target = shared_from_this();
        return msg; // Rely on RVO/move
    }

    Message Handler::obtainMessage(int what) {
        Message msg;
        msg.what = what;
        msg.target = shared_from_this();
        return msg;
    }

    Message Handler::obtainMessage(int what, std::any obj) {
        Message msg;
        msg.what = what;
        msg.obj = std::move(obj);
        msg.target = shared_from_this();
        return msg;
    }

    Message Handler::obtainMessage(int what, int arg1, int arg2) {
        Message msg;
        msg.what = what;
        msg.arg1 = arg1;
        msg.arg2 = arg2;
        msg.target = shared_from_this();
        return msg;
    }

    Message Handler::obtainMessage(int what, int arg1, int arg2, std::any obj) {
        Message msg;
        msg.what = what;
        msg.arg1 = arg1;
        msg.arg2 = arg2;
        msg.obj = std::move(obj);
        msg.target = shared_from_this();
        return msg;
    }

    // --- Message Removal Methods ---

    // Removes any pending posts of messages with code 'what' that are targeted to this Handler. 
    void Handler::removeMessages(int what) {
        if (mQueue) {
            mQueue->removeMessages(shared_from_this(), what);
        }
    }

    // Removes any pending posts of callbacks (runnables) targeted to this Handler. 
    void Handler::removeCallbacks() {
        if (mQueue) {
            mQueue->removeCallbacks(shared_from_this());
        }
    }

    // Gets the Looper associated with this Handler. 
    std::shared_ptr<Looper> Handler::getLooper() const {
        return mLooper;
    }

}

#if 0

#include "looper_handler.h"
#include <thread>
#include <future>
#include <iostream>
#include <string>
#include <chrono>

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
            }
            catch (const std::bad_any_cast& e) {
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
                }
                catch (const std::bad_any_cast& e) {
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
 
};

  

void worker_thread_with_promise(std::promise<std::shared_ptr<Looper>> looperPromise) {
    std::cout << "Worker thread (" << std::this_thread::get_id() << ") started." << std::endl;
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
 
#endif
