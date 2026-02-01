#include "StdAfx.h"
#include "looper_handler.h"
#include "worker.h"
#include <thread>
#include <future>
#include <iostream>
#include <string>
#include <chrono>
 
namespace core {

    void WorkerHandler::handleMessage(const Message& msg) {
        switch (msg.what) {
        case MSG_SHUTDOWN:
            getLooper()->quit();
            break;
        default:
            std::cout << "  Unknown message type." << std::endl;
            break;
        }
    }

    void WorkerHandler::worker_thread(std::promise<std::shared_ptr<Looper>> looperPromise) {
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
    }

    std::shared_ptr<WorkerHandler> WorkerHandler::createWorker() {
        std::promise<std::shared_ptr<Looper>> looperPromise;
        auto looperFuture = looperPromise.get_future();

        std::thread worker(WorkerHandler::worker_thread, std::move(looperPromise));
        worker.detach(); //!!!!!!!! fixme
        
        std::shared_ptr<Looper> workerLooper = looperFuture.get();
        if (!workerLooper) {
            std::cerr << "Failed to get worker Looper!" << std::endl;
            if (worker.joinable()) worker.join();
        }
         
        return std::move(std::make_shared<WorkerHandler>(workerLooper));
    }

}