#include "WorkerThread.h"
#include "Debouncer.h"
#include <iostream>
#include <memory>

int main() {
    // 1. 准备后台线程
    auto worker = std::make_shared<core::WorkerThread>( );
    worker->start();
 

    // 3. 创建 Debouncer
    // 任务将在 worker 线程中执行，防抖时间 500ms
    core::Debouncer<std::string> searchDebouncer(
        worker, 
        [](std::string text) {
            std::cout << "[Worker] Searching for: " << text << std::endl;
        }, 
        std::chrono::milliseconds(500)
    );

    // 4. 模拟快速输入 (在主线程)
    std::cout << "[Main] User typing 'H'..." << std::endl;
    searchDebouncer("H");

    std::cout << "[Main] User typing 'He'..." << std::endl;
    searchDebouncer("He");

    std::cout << "[Main] User typing 'Hel'..." << std::endl;
    searchDebouncer("Hel");

    // 休息一下，让最后一次生效
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 5. 清理
    worker.reset();
}