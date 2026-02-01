#pragma once

#include "looper_handler.h"
 
#include <thread>
#include <future>
#include <iostream>
#include <string>
#include <chrono>

namespace core {

    enum MsgType {
        MSG_TASK_A = 0,
        MSG_TASK_B,
        MSG_SHUTDOWN
    };



    class WorkerHandler : public Handler {
    public:
        explicit WorkerHandler(std::shared_ptr<Looper> looper) : Handler(looper) {}

        void handleMessage(const Message& msg) override;

        void destroy() { sendMessage(Message(MSG_SHUTDOWN)); }

        static void worker_thread(std::promise<std::shared_ptr<Looper>> looperPromise);

        static std::shared_ptr<WorkerHandler> createWorker();

    };

}