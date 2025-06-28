#ifndef LOOPER_HANDLER_H
#define LOOPER_HANDLER_H

#include <queue> // Included for completeness, though deque is used
#include <deque> 
#include <thread> // For std::thread::id
#include <mutex> // For std::mutex
#include <condition_variable> // For std::condition_variable
#include <functional> // For std::function
#include <chrono> // For std::chrono::steady_clock
#include <memory> // For std::shared_ptr, std::unique_ptr, std::enable_shared_from_this
#include <atomic> // For std::atomic
#include <optional> // For std::optional
#include <any> // C++17, or use void* with caution for older standards
#include <stdexcept> // For std::runtime_error, std::invalid_argument (used in Handler constructor declaration)
#include <cassert>   // for assertions (used in Handler constructor declaration)

namespace core {
    // Forward declarations
    class Handler;
    class MessageQueue;
    class Looper;

    // Represents a message or task to be processed
    struct Message {
        int what = 0;                         // User-defined message code
        int arg1 = 0;                         // Optional integer arguments
        int arg2 = 0;
        std::any obj;                         // Optional data payload (use std::any for type safety)
        std::shared_ptr<Handler> target;      // The handler that will process this message (Needs Handler fwd decl)
        std::function<void()> callback;       // Optional runnable task
        std::chrono::steady_clock::time_point when; // When the message should be processed

        // Default constructor
        Message() = default;

        // Convenience constructor for simple messages
        Message(int w, std::shared_ptr<Handler> t = nullptr);

        // Convenience constructor for messages with args
        Message(int w, int a1, int a2, std::shared_ptr<Handler> t = nullptr);

        // Convenience constructor for messages with data object
        Message(int w, std::any o, std::shared_ptr<Handler> t = nullptr);

        // Convenience constructor for runnables (callback)
        Message(std::function<void()> cb, std::shared_ptr<Handler> t = nullptr);

        // Sends this message to the Handler specified by Message.target.
        // Will assign a timestamp to the message before dispatching it.
        // Returns false if the target is null or the queue is quitting.
        bool sendToTarget(); // << NEW METHOD
    };

    // Thread-safe message queue
    class MessageQueue {
    public:
        MessageQueue(); // Default constructor declaration
        ~MessageQueue(); // Default destructor declaration

        // Non-copyable and non-movable
        MessageQueue(const MessageQueue&) = delete;
        MessageQueue& operator=(const MessageQueue&) = delete;

        // Enqueues a message. Messages are sorted by their 'when' time.
        bool enqueueMessage(Message&& msg, std::chrono::steady_clock::time_point when);

        // Retrieves the next message. Blocks if the queue is empty or
        // the next message is scheduled for the future.
        // Returns std::nullopt if the queue is quitting.
        std::optional<Message> next();

        // Signals the queue to stop processing messages.
        void quit();

        bool isQuitting() const;

        // Removes messages for a specific handler with a specific 'what' code
        void removeMessages(const std::shared_ptr<Handler>& h, int what);

        // Removes callback runnables for a specific handler
        void removeCallbacks(const std::shared_ptr<Handler>& h);


    private:
        std::deque<Message> mMessages;           // Use deque for efficient front removal/insertion
        mutable std::mutex mMutex;
        std::condition_variable mCondVar;
        std::atomic<bool> mQuitting{ false };
    };


    // Manages the message loop for a thread
    class Looper {
    private:
        // Private constructor: Use static methods prepare()/myLooper()
        Looper();

        // Thread-local storage for the Looper instance
        // Each thread gets its own pointer, initialized to nullptr
        static thread_local std::shared_ptr<Looper> tLooper;

        std::unique_ptr<MessageQueue> mQueue;
        std::thread::id mThreadId; // Store the owning thread ID (Initialized in constructor definition)

    public:
        // Non-copyable and non-movable
        Looper(const Looper&) = delete;
        Looper& operator=(const Looper&) = delete;

        // Prepares a Looper for the calling thread. Must be called before loop().
        static void prepare();

        // Returns the Looper associated with the calling thread.
        // Returns nullptr if Looper::prepare() hasn't been called.
        static std::shared_ptr<Looper> myLooper();

        // Starts the message loop for the calling thread's Looper.
        // This function will block until quit() is called.
        static void loop();

        // Stops the Looper safely. Can be called from any thread.
        void quit();

        // Gets the message queue associated with this Looper.
        // Be cautious when using the queue directly.
        MessageQueue* getQueue() const;

        std::thread::id getThreadId() const;
    };

    // Enables sending and processing Message objects associated with a Looper's MessageQueue

    class Handler : public std::enable_shared_from_this<Handler> {
    private:
        std::shared_ptr<Looper> mLooper;
        MessageQueue* mQueue; // Raw pointer for performance, lifetime managed by Looper

    public:
        // Creates a Handler associated with the Looper for the current thread.
        // Throws if Looper::prepare() wasn't called.
        Handler();

        // Creates a Handler associated with a specific Looper.
        explicit Handler(std::shared_ptr<Looper> looper);

        virtual ~Handler() = default; // Virtual destructor

        // Subclasses must implement this to receive messages.
        virtual void handleMessage(const Message& msg) = 0; // Pure virtual

        // Final method pattern: dispatches message to handleMessage
        // Marked virtual in case derived classes want to intercept dispatching (though unusual)
        virtual void dispatchMessage(const Message& msg);

        // --- Message Sending Methods ---

        // Sends a Message. It will be handled in handleMessage().
        bool sendMessage(Message&& msg);

        // Sends a Message with a delay.
        bool sendMessageDelayed(Message&& msg, long delayMillis);

        // Sends a Message to be processed at a specific time.
        bool sendMessageAtTime(Message&& msg, std::chrono::steady_clock::time_point uptimeMillis);

        // --- Runnable Posting Methods ---

        // Posts a task (std::function) to be run on the Handler's thread.
        bool post(std::function<void()> r);

        // Posts a task with a delay.
        bool postDelayed(std::function<void()> r, long delayMillis);

        // Posts a task to be run at a specific time.
        bool postAtTime(std::function<void()> r, std::chrono::steady_clock::time_point uptimeMillis);

        // --- Message Obtaining Methods ---  
    // Returns a new Message from the global message pool. More efficient than
    // creating and allocating new instances due to reuse. The obtained Message
    // has its target set to this Handler.
        Message obtainMessage();
        Message obtainMessage(int what);
        Message obtainMessage(int what, std::any obj);
        Message obtainMessage(int what, int arg1, int arg2);
        Message obtainMessage(int what, int arg1, int arg2, std::any obj);

        // --- Message Removal Methods ---

        // Removes any pending posts of messages with code 'what' that are targeted to this Handler.
        void removeMessages(int what);

        // Removes any pending posts of callbacks (runnables) targeted to this Handler.
        void removeCallbacks();

        // Gets the Looper associated with this Handler.
        std::shared_ptr<Looper> getLooper() const;
    };

}
#endif // LOOPER_HANDLER_H