#include <iostream>
#include <memory> // For std::shared_ptr and std::enable_shared_from_this
#include <string>
#include <vector>
#include <boost/asio.hpp>

namespace asio = boost::asio;
using asio::ip::tcp;

// Forward declaration
class Server;

// Session class to handle a single client connection.
// It inherits from std::enable_shared_from_this to allow us to safely
// create a shared_ptr to itself inside its own member functions.
// This is the key to managing the object's lifetime.
class Session : public std::enable_shared_from_this<Session> {
public:
    // The constructor takes the socket that was created by the acceptor.
    // We move the socket into the class, as the session now owns it.
    Session(tcp::socket socket) : socket_(std::move(socket)) {}

    // Public entry point to start the session logic.
    void start() {
        // Start the first async read operation.
        do_read();
    }

private:
    void do_read() {
        // Create a shared_ptr to `this` to keep the session object alive
        // until the async operation completes.
        auto self = shared_from_this();

        socket_.async_read_some(asio::buffer(data_, max_length),
            [this, self](const boost::system::error_code& ec, std::size_t length) {
                // This is the handler for the read operation.
                if (!ec) {
                    // If the read was successful, we immediately start an async write
                    // to echo the data back to the client.
                    do_write(length);
                }
                // If an error occurred (e.g., client disconnected), the lambda
                // finishes, `self` (the shared_ptr) goes out of scope. If there are
                // no other shared_ptrs to this Session, it will be destroyed automatically.
            });
    }

    void do_write(std::size_t length) {
        // Create another shared_ptr to keep the session alive.
        auto self = shared_from_this();

        // asio::async_write guarantees that the entire buffer is sent.
        asio::async_write(socket_, asio::buffer(data_, length),
            [this, self](const boost::system::error_code& ec, std::size_t /*bytes_transferred*/) {
                // This is the handler for the write operation.
                if (!ec) {
                    // If the write was successful, we go back to reading more data
                    // from the client, completing the echo loop.
                    do_read();
                }
                // If an error occurred, the session is over.
            });
    }

    tcp::socket socket_;
    enum { max_length = 1024 };
    char data_[max_length];
};

// Server class to listen for and accept incoming connections.
class Server {
public:
    Server(asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        // Start the first accept operation.
        do_accept();
    }

private:
    void do_accept() {
        // Start an asynchronous accept operation.
        acceptor_.async_accept(
            [this](const boost::system::error_code& ec, tcp::socket socket) {
                // This is the handler for the accept operation.
                if (!ec) {
                    // If the accept was successful, create a new Session object
                    // to handle the connection, and then start it.
                    // We use std::make_shared to create the session on the heap.
                    std::make_shared<Session>(std::move(socket))->start();
                }

                // CRITICAL: Immediately start another accept operation
                // to be ready for the next client. This creates the "accept loop".
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main0(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: echo_server <port>\n";
            return 1;
        }

        asio::io_context io_context;

        // Create the server object, which will automatically start accepting.
        Server s(io_context, std::atoi(argv[1]));
        
        std::cout << "Echo server is running on port " << argv[1] << "...\n";

        // Run the io_context. This will block until the server is stopped.
        // Since our server's accept loop never ends, this will run forever.
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}


//////////////////////////////////////////////////////////////////////////////////////////
// #include <iostream>
// #include <string>
// #include <vector>
// #include <boost/asio.hpp>

// namespace asio = boost/asio;
// using asio::ip::tcp;

// Session 协程：处理单个客户端的读写循环
asio::awaitable<void> session(tcp::socket socket) {
    try {
        char data[1024];

        //在一个无限循环中处理客户端的读写
        for (;;) {
            // 1. 异步读取数据
            // co_await 会暂停协程，直到数据被读取或发生错误。
            // 协程的状态（包括局部变量 data）会被自动保存。
            std::size_t n = co_await socket.async_read_some(asio::buffer(data), asio::use_awaitable);

            // 2. 异步写回数据
            // 使用 async_write 确保所有数据都被发送。
            co_await asio::async_write(socket, asio::buffer(data, n), asio::use_awaitable);
        }
    } catch (const std::exception& e) {
        // 当客户端断开连接时，async_read_some 会抛出异常（通常是 eof）。
        // 我们捕获这个异常，打印一条信息，然后让协程自然结束。
        // 当协程函数返回时，它所占用的资源（包括 socket）会被自动清理。
        // 我们不再需要手动管理任何东西！
        std::cout << "Session finished: " << e.what() << std::endl;
    }
}

// Listener 协程：监听并接受新的连接
asio::awaitable<void> listener(short port) {
    // 获取当前协程的执行器
    auto executor = co_await asio::this_coro::executor;
    
    // 创建一个 acceptor 来监听指定的端口
    tcp::acceptor acceptor(executor, {tcp::v4(), static_cast<boost::asio::ip::port_type>(port)});
    
    std::cout << "Echo server is running on port " << port << "..." << std::endl;

    // 无限循环，持续接受新的连接
    for (;;) {
        // 1. 异步等待一个新连接
        // co_await 会暂停 listener 协程，直到有客户端连接进来。
        tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);

        // 2. 为新连接启动一个 session 协程
        // co_spawn 会立即开始执行 session 协程，而 listener 协程本身不会等待它完成。
        // asio::detached 表示我们不关心 session 协程何时结束，它会自我管理。
        asio::co_spawn(executor, session(std::move(socket)), asio::detached);
    }
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: echo_server_coroutines <port>\n";
            return 1;
        }

        asio::io_context io_context;

        short port = std::atoi(argv[1]);

        // 启动 listener 协程。
        // co_spawn 会将协程与 io_context 关联起来并开始执行。
        asio::co_spawn(io_context, listener(port), asio::detached);

        // 运行 io_context。为了充分利用多核 CPU，可以创建多个线程来调用 run()。
        // 例如，创建一个线程池。
        std::vector<std::thread> threads;
        unsigned int thread_count = std::thread::hardware_concurrency();
        for (unsigned int i = 0; i < thread_count; ++i) {
            threads.emplace_back([&io_context] { io_context.run(); });
        }

        // 等待所有线程完成（在这个服务器例子中，它们会永远运行）。
        for (auto& t : threads) {
            t.join();
        }

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}