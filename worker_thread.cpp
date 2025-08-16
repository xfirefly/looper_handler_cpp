#include <iostream>
#include <thread>
#include <chrono>
#include <boost/asio.hpp>

namespace asio = boost::asio;

int main() {
    // 1. Create the executor's task queue.
    asio::io_context io_context;

    // 2. Prevent the io_context from running out of work and stopping prematurely.
    // This is like telling the executor service "stay alive until we say stop".
    auto work_guard = asio::make_work_guard(io_context);
 
    // 3. Create and run the single worker thread.
    // The thread's only job is to run tasks from the io_context queue.
    std::thread worker_thread([&io_context]() {
        std::cout << "Worker thread started. Waiting for tasks...\n";
        io_context.run(); // This blocks until the context is stopped.
        std::cout << "Worker thread finished.\n";
    });

    std::cout << "Main thread: Posting tasks to the executor.\n";

    // 4. Submit tasks (like Java's executor.submit(Runnable)).
    // These will be executed sequentially on the worker_thread.
    asio::post(io_context, []() {
        std::cout << "[Task 1] Running on worker thread.\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    });

    asio::post(io_context, []() {
        std::cout << "[Task 2] Running on worker thread. This runs after Task 1 completes.\n";
    });
    
    asio::post(io_context, []() {
        std::cout << "[Task 3] The final task.\n";
    });


    // Allow some time for initial tasks to post
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 5. To shut down, release the work guard and join the thread.
    std::cout << "Main thread: Preparing to shut down the executor.\n";
    work_guard.reset(); // Allow io_context.run() to exit when work is done.
    worker_thread.join(); // Wait for the worker thread to finish.

    std::cout << "Main thread: Executor has been shut down.\n";

    return 0;
}