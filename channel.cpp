#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <string>

// To use coroutines with Asio, we need to define this token.
namespace asio = boost::asio;
using boost::system::error_code;

// For brevity, create a type alias for our channel
// This channel will transport std::string objects
using StringChannel = asio::experimental::channel<void(error_code, std::string)>;


// The 'producer' function acts like a Goroutine.
// It produces messages and sends them to the channel.
// asio::awaitable<void> is the return type for an Asio coroutine.
asio::awaitable<void> producer(StringChannel& ch) {
    try {
        for (int i = 0; i < 5; ++i) {
            std::string msg = "Message " + std::to_string(i);
            std::cout << "Producer: sending '" << msg << "'\n";
            
            // co_await pauses this coroutine until the send operation completes.
            // If the channel is full (for a buffered channel) or no one is
            // ready to receive (for an unbuffered channel), this will suspend
            // execution without blocking the OS thread.
            co_await ch.async_send(error_code{}, msg, asio::use_awaitable);
            
            // Simulate some work
            co_await asio::steady_timer(co_await asio::this_coro::executor, std::chrono::milliseconds(100)).async_wait(asio::use_awaitable);
        }
        
        // Close the channel to signal that no more messages will be sent.
        ch.close();
        std::cout << "Producer: finished and closed channel.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Producer error: " << e.what() << std::endl;
    }
}

// The 'consumer' function also acts like a Goroutine.
// It receives messages from the channel and processes them.
asio::awaitable<void> consumer(StringChannel& ch) {
    try {
        for (;;) {
            // co_await pauses this coroutine until a message is received.
            // If the producer is done and the channel is closed, it will
            // throw an exception, which we use to break the loop.
            std::string msg = co_await ch.async_receive(asio::use_awaitable);
            std::cout << "Consumer: received '" << msg << "'\n";
        }
    } catch (const std::exception& e) {
        // This is expected when the channel is closed by the producer.
        std::cout << "Consumer: finished, channel is closed.\n";
    }
}

int main() {
    // The io_context is the heart of Asio. It acts as the scheduler
    // for our coroutines, running them on a thread pool.
    asio::io_context ctx;
    
    // Create the channel. The first argument is the executor (scheduler).
    // The '0' means it is an unbuffered channel, just like Go's `make(chan string)`.
    // For a buffered channel of size 10, you would use `StringChannel(ctx, 10)`.
    StringChannel ch(ctx, 0);
    
    // Start the coroutines. co_spawn is like `go my_func()` in Go.
    // It launches the coroutine on the io_context's scheduler.
    asio::co_spawn(ctx, producer(ch), asio::detached);
    asio::co_spawn(ctx, consumer(ch), asio::detached);
    
    // Run the scheduler. This will execute the coroutines until they
    // are all finished.
    ctx.run();

    return 0;
}