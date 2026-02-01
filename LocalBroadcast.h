#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <any>
#include <mutex>
#include <memory>

#include "WorkerThread.h" // 包含 WorkerThread

namespace core {

    // --- 前向声明 ---
    class Intent;
    class BroadcastReceiver;
    class IntentFilter;

 
    class Intent {
    public:
        Intent(std::string_view action);
        std::string getAction() const;

        template<typename T>
        void putExtra(std::string_view key, T value) {
            m_extras[std::string(key)] = std::move(value);
        }

        template<typename T>
        const T* getExtra(std::string_view key) const {
            auto it = m_extras.find(std::string(key));
            if (it != m_extras.end()) {
                return std::any_cast<T>(&(it->second));
            }
            return nullptr;
        }

        int what;
    private:
        std::string m_action;
        std::map<std::string, std::any> m_extras;
    };

    class BroadcastReceiver {
    public:
        virtual ~BroadcastReceiver() = default;
        virtual void onReceive(const Intent& intent) = 0;
    };

    class IntentFilter {
    public:
        IntentFilter(std::string_view action);
        void addAction(std::string_view action);
        const std::vector<std::string>& getActions() const;
    private:
        std::vector<std::string> m_actions;
    };


    // =================================================================================
    // BroadcastManager - 核心管理类 (已集成 WorkerThread)
    // =================================================================================
    /**
     * @class BroadcastManager
     * @brief 进程内本地广播管理器。
     *
     * 用于在应用内的不同组件之间发送和接收广播 Intent。
     * 所有的广播发送和接收都在同一个进程中，且是线程安全的。
     * 
     * <h2>使用示例</h2>
     * @code
     * // 1. 定义接收器
     * class MyReceiver : public core::BroadcastReceiver {
     *     void onReceive(const core::Intent& intent) override {
     *         if (intent.getAction() == "com.example.UPDATE") {
     *             std::cout << "Received update broadcast!" << std::endl;
     *         }
     *     }
     * };
     * 
     * // 2. 注册接收器
     * auto receiver = std::make_shared<MyReceiver>();
     * core::IntentFilter filter("com.example.UPDATE");
     * core::BroadcastManager::getInstance().registerReceiver(receiver, filter);
     * 
     * // 3. 发送广播
     * core::Intent intent("com.example.UPDATE");
     * core::BroadcastManager::getInstance().sendBroadcast(intent);
     * 
     * // 4. 注销 (BroadcastManager 析构也会自动清理，但显式注销更好)
     * core::BroadcastManager::getInstance().unregisterReceiver(receiver);
     * @endcode
     */
    class BroadcastManager final {
    public:
        static BroadcastManager& getInstance();
        ~BroadcastManager(); // 添加析构函数以管理线程
        BroadcastManager(const BroadcastManager&) = delete;
        BroadcastManager& operator=(const BroadcastManager&) = delete;

        void registerReceiver(std::shared_ptr<BroadcastReceiver> receiver, const IntentFilter& filter);
        void unregisterReceiver(const std::shared_ptr<BroadcastReceiver>& receiver);
        void sendBroadcast(const std::string_view action, int what);
        void sendBroadcast(const Intent& intent);

    private:
        BroadcastManager(); // 构造函数设为私有

        std::mutex m_mutex;
        std::map<std::string, std::vector<std::weak_ptr<BroadcastReceiver>>> m_actions;
        std::map<BroadcastReceiver*, std::vector<std::string>> m_receivers;
        
        // 用于执行 onReceive 回调的工作线程
        std::unique_ptr<core::WorkerThread> mWorkerThread; 
    };

}