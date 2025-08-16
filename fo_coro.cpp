#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/futures/Future.h>

// 修正 1: 使用新的、正式的 Folly 協程標頭檔路徑
//#include <folly/coro/Future.h>
#include <folly/coro/BlockingWait.h>

// 全局的執行緒池，用於執行後台任務
folly::CPUThreadPoolExecutor g_executor(4);

// 1. 模擬非同步獲取資料，返回一個 Future
folly::Future<std::string> fetchData() {
    std::cout << "[Thread " << std::this_thread::get_id() << "] 開始獲取資料...\n";
    
    return folly::makeFuture()
        .via(&g_executor)
        .then([](auto&&) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "[Thread " << std::this_thread::get_id() << "] 資料獲取完畢！\n";
            return std::string("42");
        });
}

// 2. 模擬非同步處理資料，返回一個 Future
folly::Future<int> processData(std::string data) {
    std::cout << "[Thread " << std::this_thread::get_id() << "] 開始處理資料: " << data << "\n";
    
    return folly::makeFuture()
        .via(&g_executor)
        .then([data](auto&&) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            int result = std::stoi(data);
            std::cout << "[Thread " << std::this_thread::get_id() << "] 資料處理完畢！\n";
            return result;
        });
}

// 3. 使用 co_await 編寫的非同步主邏輯
folly::Future<std::string> runLogic() {
    std::cout << "[Thread " << std::this_thread::get_id() << "] 協程邏輯開始...\n";

    std::string data = co_await fetchData();

    std::cout << "[Thread " << std::this_thread::get_id() << "] 協程拿到資料，準備下一步處理。\n";

    int number = co_await processData(data);

    std::string finalResult = "最終計算結果: " + std::to_string(number * 10);
    
    co_return finalResult;
}


int main() {
    std::cout << "[Thread " << std::this_thread::get_id() << "] Main 函數開始。\n";

    folly::Future<std::string> futureResult = runLogic();

    std::cout << "[Thread " << std::this_thread::get_id() << "] 協程已啟動，Main 函數可以做其他事...\n";

    // 修正 2: blockingWait 現在位於 folly::coro 命名空間
    std::string result = folly::coro::blockingWait(std::move(futureResult));

    std::cout << "\n----------------------------------------\n";
    std::cout << "[Thread " << std::this_thread::get_id() << "] Main 函數收到協程的最終結果: \n";
    std::cout << ">> " << result << std::endl;
    std::cout << "----------------------------------------\n";

    return 0;
}