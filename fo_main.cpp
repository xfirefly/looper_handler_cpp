#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <stdexcept>

// 核心 Folly 组件
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/futures/Future.h>
 

// =================================================================================
// 第一部分：建立执行器 (Executors)
// 这些执行器是我们的工作中心，负责调度任务到合适的线程。
// =================================================================================

// CPU 执行器：用于需要大量计算的任务。
// 线程数量通常设定为 CPU 核心数。
folly::CPUThreadPoolExecutor g_cpuExecutor(std::thread::hardware_concurrency());

// I/O 执行器：用于会发生阻塞的 I/O 任务 (如文件读写、网络请求)。
// 它通常拥有更多的线程来应对等待。
folly::IOThreadPoolExecutor g_ioExecutor(10);


// =================================================================================
// 第二部分：异步任务函数
// 每个函数都模拟一个工作单元，并返回一个 Future 来代表未来的结果。
// =================================================================================

// 模拟用户数据结构
struct UserProfile {
    int userId;
    std::string name;
    std::string email;
};

// 任务 1: 从数据库获取用户 ID (模拟 I/O)
folly::Future<int> fetchUserIdFromDB(const std::string& username) {
    std::cout << "[DB Task] 在线程 " << std::this_thread::get_id() << " 上准备查询用户 " << username << "\n";
    
    // folly::via 会将后续的 .then() 派发到指定的执行器上
    return folly::via(&g_ioExecutor).then([username]() {
        std::cout << "  -> [DB Task] 线程 " << std::this_thread::get_id() << " 正在执行数据库查询...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 模拟 I/O 延迟
        if (username == "Alice") {
            return 101;
        }
        throw std::runtime_error("用户不存在");
    });
}

// 任务 2: 调用 API 获取用户详细资料 (模拟网络 I/O)
folly::Future<UserProfile> fetchUserDetails(int userId) {
    std::cout << "[API Task] 在线程 " << std::this_thread::get_id() << " 上准备调用 API, userId=" << userId << "\n";
    
    return folly::via(&g_ioExecutor).then([userId]() {
        std::cout << "  -> [API Task] 线程 " << std::this_thread::get_id() << " 正在进行网络调用...\n";
        std::this_thread::sleep_for(std::chrono::seconds(1)); // 模拟网络延迟
        return UserProfile{userId, "Alice", "alice@example.com"};
    });
}

// 任务 3: 生成欢迎信息 (模拟 CPU 密集)
folly::Future<std::string> generateWelcomeMessage(UserProfile profile) {
    std::cout << "[CPU Task] 在线程 " << std::this_thread::get_id() << " 上准备处理数据, user=" << profile.name << "\n";

    return folly::via(&g_cpuExecutor).then([profile]() {
        std::cout << "  -> [CPU Task] 线程 " << std::this_thread::get_id() << " 正在进行复杂计算...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(800)); // 模拟计算耗时
        return "Hello, " + profile.name + "! Welcome back. Your email is " + profile.email;
    });
}

// 任务 4: 写入日志 (模拟 I/O)
folly::Future<folly::Unit> logMessage(const std::string& message) {
    std::cout << "[Log Task] 在线程 " << std::this_thread::get_id() << " 上准备写入日志\n";
    
    return folly::via(&g_ioExecutor).then([message]() {
        std::cout << "  -> [Log Task] 线程 " << std::this_thread::get_id() << " 正在写入文件...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // folly::Unit 用于表示 Future 没有有意义的返回值 (类似 void)
        return folly::unit;
    });
}

// =================================================================================
// 第三部分：主逻辑 - 串联 Future
// =================================================================================

int main() {
    std::cout << "主线程 ID: " << std::this_thread::get_id() << "\n\n";

    try {
        // 通过 .then() 将所有异步任务优雅地串联起来
        // 这是一个数据依赖链：上一步的输出是下一步的输入
        folly::Future<std::string> finalResultFuture = fetchUserIdFromDB("Alice")
            .thenValue([](int userId) {
                // userId 获取成功后，调用下一个异步函数
                return fetchUserDetails(userId);
            })
            .thenValue([](UserProfile profile) {
                // profile 获取成功后，调用下一个异步函数
                return generateWelcomeMessage(profile);
            })
            .thenValue([](std::string message) {
                // message 产生成功后，调用日志任务
                // logMessage 返回 Future<Unit>，但我们想把 message 继续传递下去
                return logMessage(message).thenValue([message] {
                    // 在日志任务完成后，将原始信息返回
                    return message;
                });
            })
            .onError([](const std::runtime_error& e) {
                // 链路中任何地方发生 std::runtime_error 都会被这里捕获
                std::cerr << "\n!!! 错误捕获: " << e.what() << " !!!\n";
                return std::string("流程因错误而终止"); // 提供一个默认值
            });

        std::cout << "主线程：异步工作流已启动，等待最终结果...\n\n";

        // folly::coro::blockingWait 会阻塞当前线程，直到 Future 完成并返回结果
        // 这通常用在程序的顶层 (如 main)
        //std::string result = folly::coro::blockingWait(std::move(finalResultFuture));

        std::cout << "\n========================================\n";
        std::cout << "主线程收到最终结果:\n>> " << result << "\n";
        std::cout << "========================================\n";

    } catch (const std::exception& e) {
        std::cerr << "在 main 中捕获到未处理的异常: " << e.what() << std::endl;
    }

    return 0;
}