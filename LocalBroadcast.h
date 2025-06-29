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
    // LocalBroadcastManager - 核心管理类 (已集成 WorkerThread)
    // =================================================================================
    class LocalBroadcastManager final {
    public:
        static LocalBroadcastManager& getInstance();
        ~LocalBroadcastManager(); // 添加析构函数以管理线程
        LocalBroadcastManager(const LocalBroadcastManager&) = delete;
        LocalBroadcastManager& operator=(const LocalBroadcastManager&) = delete;

        void registerReceiver(std::shared_ptr<BroadcastReceiver> receiver, const IntentFilter& filter);
        void unregisterReceiver(const std::shared_ptr<BroadcastReceiver>& receiver);
        void sendBroadcast(const Intent& intent);

    private:
        LocalBroadcastManager(); // 构造函数设为私有

        std::mutex m_mutex;
        std::map<std::string, std::vector<std::weak_ptr<BroadcastReceiver>>> m_actions;
        std::map<BroadcastReceiver*, std::vector<std::string>> m_receivers;
        
        // 用于执行 onReceive 回调的工作线程
        std::unique_ptr<core::WorkerThread> mWorkerThread; 
    };

}