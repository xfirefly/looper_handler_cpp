#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

// A coroutine to perform a complete HTTPS GET request.
// It returns the response body as a string.
// Corrected https_get_beast function
asio::awaitable<std::string> https_get_beast(
    std::string host, 
    std::string port, 
    std::string target) 
{
    // The SSL context is required for HTTPS connections.
    ssl::context ssl_ctx(ssl::context::tlsv13_client);
    ssl_ctx.set_default_verify_paths();
    //ssl_ctx.set_verify_mode(ssl::verify_peer);
    ssl_ctx.set_verify_mode(ssl::verify_none);
    ssl_ctx.set_verify_callback(ssl::host_name_verification(host));

    auto executor = co_await asio::this_coro::executor;

    // I/O objects.
    tcp::resolver resolver(executor);

    // [FIX] The type ssl::stream comes from the `boost::asio::ssl` namespace (aliased as `ssl`).
    // It is a template that wraps a lower-level stream. In our case, it wraps a `beast::tcp_stream`.
    ssl::stream<beast::tcp_stream> stream(executor, ssl_ctx);

    try {
        // 1. Resolve the domain name.
        std::cout << "[1/5] Resolving " << host << "..." << std::endl;
        auto const results = co_await resolver.async_resolve(host, port, asio::use_awaitable);

        // 2. Make the TCP connection.
        // beast::get_lowest_layer gets the underlying beast::tcp_stream from the ssl::stream.
        std::cout << "[2/5] Connecting..." << std::endl;
        co_await beast::get_lowest_layer(stream).async_connect(results, asio::use_awaitable);

        // 3. Perform the SSL/TLS Handshake.
        std::cout << "[3/5] Performing SSL Handshake..." << std::endl;
        co_await stream.async_handshake(ssl::stream_base::client, asio::use_awaitable);

        // ... the rest of the function remains the same ...
        
        // 4. Send the HTTP GET request.
        std::cout << "[4/5] Sending HTTP GET request..." << std::endl;
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        co_await http::async_write(stream, req, asio::use_awaitable);

        // 5. Receive the HTTP response.
        std::cout << "[5/5] Receiving HTTP response..." << std::endl;
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        co_await http::async_read(stream, buffer, res, asio::use_awaitable);

        // Gracefully close the SSL stream.
        boost::system::error_code ec;
        co_await stream.async_shutdown(asio::redirect_error(asio::use_awaitable, ec));
        if (ec && ec != ssl::error::stream_truncated) {
            throw boost::system::system_error{ec};
        }

        co_return res.body();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        throw;
    }
}


int main() {
    try {
        asio::io_context io_context;

        std::string host = "www.zhihu.com";
        std::string port = "443";
        std::string target = "/";

        // Launch the coroutine.
        // [FIX 1] The call no longer needs to pass io_context.
        asio::co_spawn(io_context, 
                       https_get_beast(host, port, target),
                       // Completion handler for the coroutine itself.
                       [](std::exception_ptr p, std::string body) {
                           if (p) {
                               // The exception was re-thrown from the coroutine and caught here.
                           } else {
                               std::cout << "\n--- Download Complete ---\n";
                               std::cout << "Response body size: " << body.length() << " bytes\n";
                               std::cout << "First 80 chars: " << body.substr(0, 80) << "...\n";
                           }
                       });

        io_context.run();

    } catch (std::exception& e) {
        std::cerr << "Main exception: " << e.what() << std::endl;
    }

    return 0;
}