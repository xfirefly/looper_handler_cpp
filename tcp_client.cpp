#include <iostream>
#include <string>
#include <array> // For our read buffer
#include <boost/asio.hpp>

namespace asio = boost::asio;
using asio::ip::tcp;

// A class to encapsulate the client logic
class Client {
public:
    // The constructor needs an io_context to work with.
    Client(asio::io_context& io_context)
        : resolver_(io_context), socket_(io_context) {}

    // The main entry point to start the client operations.
    void connect(const std::string& host, const std::string& service) {
        // Step 1: Resolve the host and service names into a list of endpoints.
        // This is the first asynchronous operation.
        resolver_.async_resolve(host, service, 
            [this](const boost::system::error_code& ec, tcp::resolver::results_type results) {
                // This is the completion handler for the resolve operation.
                handle_resolve(ec, results);
            });
    }

private:
    // Handler for the completion of a resolve operation.
    void handle_resolve(const boost::system::error_code& ec, tcp::resolver::results_type results) {
        if (ec) {
            std::cerr << "Resolve error: " << ec.message() << std::endl;
            return;
        }

        // Step 2: Attempt to connect to one of the resolved endpoints.
        // asio::async_connect will try each endpoint in the list until one succeeds.
        asio::async_connect(socket_, results,
            [this](const boost::system::error_code& ec, const tcp::endpoint& endpoint) {
                // This is the completion handler for the connect operation.
                handle_connect(ec, endpoint);
            });
    }

    // Handler for the completion of a connect operation.
    void handle_connect(const boost::system::error_code& ec, const tcp::endpoint& endpoint) {
        if (ec) {
            std::cerr << "Connect error: " << ec.message() << std::endl;
            return;
        }
        
        std::cout << "Connected to " << endpoint << std::endl;

        // Step 3: Now that we are connected, start reading data from the server.
        // We will read into our buffer.
        socket_.async_read_some(asio::buffer(buffer_),
            [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                // This is the completion handler for the read operation.
                handle_read(ec, bytes_transferred);
            });
    }

    // Handler for the completion of a read operation.
    void handle_read(const boost::system::error_code& ec, std::size_t bytes_transferred) {
        // The Daytime protocol server sends the data and then closes the connection.
        // This means our read operation will likely complete with an "End of File" (EOF) error,
        // which is expected in this case. We should treat EOF not as a fatal error,
        // but as a signal that the server has finished sending data.

        if (!ec) {
            // No error, we received data. Process it and wait for more.
            // (For Daytime this path is less likely, but good practice for other protocols)
            std::cout.write(buffer_.data(), bytes_transferred);
            // In a more complex protocol, you would start another async_read_some here.
        } else if (ec == asio::error::eof) {
            // End of file. The server closed the connection cleanly.
            // This is the expected success case for the Daytime protocol.
            std::cout << "Connection closed by server." << std::endl;
            // We can now print the data we received just before the EOF.
            std::cout.write(buffer_.data(), bytes_transferred);
        } else {
            // A real error occurred.
            std::cerr << "Read error: " << ec.message() << std::endl;
        }

        // At this point, the operation is complete. The socket will be closed automatically
        // when the Client object is destroyed.
    }

private:
    tcp::resolver resolver_;
    tcp::socket socket_;
    std::array<char, 128> buffer_; // A small buffer for the received data.
};

int main0(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: tcp_client_example <host>" << std::endl;
            std::cerr << "Example: tcp_client_example time.nist.gov" << std::endl;
            return 1;
        }

        // As always, we need an io_context.
        asio::io_context io_context;

        // Create our client object.
        Client client(io_context);

        // Start the chain of asynchronous operations.
        // The service "daytime" corresponds to port 13.
        client.connect(argv[1], "daytime");

        // Run the io_context to start processing events.
        io_context.run();

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////
// #include <iostream>
// #include <string>
// #include <array>
// #include <boost/asio.hpp>

// namespace asio = boost::asio;
// using asio::ip::tcp;

// 协程的返回类型是 asio::awaitable<T>。对于没有返回值的协程，使用 asio::awaitable<void>。
asio::awaitable<void> daytime_client(const std::string& host) {
    try {
        // co_await asio::this_coro::executor 用于获取当前协程正在其上运行的执行器。
        // 这是创建 I/O 对象（如 socket 和 resolver）的推荐方式。
        auto executor = co_await asio::this_coro::executor;

        // 1. 创建 Resolver 和 Socket
        // 不再需要把它们作为类的成员变量，直接作为局部变量即可。
        tcp::resolver resolver(executor);
        tcp::socket socket(executor);

        // 2. 异步解析域名
        // co_await 会暂停协程，直到异步操作完成。
        // 操作的结果会直接作为表达式的返回值，而不是通过回调参数传递。
        // 我们必须使用 asio::use_awaitable 作为“完成令牌”。
        std::cout << "Resolving " << host << "..." << std::endl;
        auto endpoints = co_await resolver.async_resolve(host, "daytime", asio::use_awaitable);

        // 3. 异步连接
        std::cout << "Connecting..." << std::endl;
        auto endpoint = co_await asio::async_connect(socket, endpoints, asio::use_awaitable);
        std::cout << "Connected to " << endpoint << std::endl;

        // 4. 异步读取
        std::array<char, 128> buffer;
        std::cout << "Reading response..." << std::endl;
        
        // 在协程中，如果发生错误（包括 EOF），Asio 会抛出 boost::system::system_error 异常。
        // 我们可以用一个 try-catch 块来处理所有步骤的错误。
        auto bytes_transferred = co_await socket.async_read_some(asio::buffer(buffer), asio::use_awaitable);
        
        std::cout << "Response received:" << std::endl;
        std::cout.write(buffer.data(), bytes_transferred);

    } catch (const boost::system::system_error& e) {
        // 统一的错误处理
        // 对于 Daytime 协议，服务器发送完数据就关闭连接，这会产生 EOF 错误。
        // 在协程模型里，EOF 也会被当作异常抛出。
        if (e.code() == asio::error::eof) {
            std::cout << "Connection closed by server (EOF)." << std::endl;
        } else {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: tcp_client_coroutines <host>\n";
            std::cerr << "Example: tcp_client_coroutines time.nist.gov\n";
            return 1;
        }

        asio::io_context io_context;

        // asio::co_spawn 用于启动一个协程。
        // 它需要一个执行器（我们从 io_context 获取）、要执行的协程函数、
        // 以及一个可选的完成处理器（用于处理协程本身的完成或未捕获的异常）。
        asio::co_spawn(io_context, 
                       daytime_client(argv[1]), 
                       // 这个 lambda 在协程完成时被调用。
                       // 如果协程内部有未捕获的异常，它会通过参数 p 传递进来。
                       [](std::exception_ptr p) {
                           if (p) {
                               try {
                                   std::rethrow_exception(p);
                               } catch (const std::exception& e) {
                                   std::cerr << "Coroutine failed: " << e.what() << std::endl;
                               }
                           }
                       });

        // 启动事件循环
        io_context.run();

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}