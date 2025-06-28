#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <any>
#include <mutex>
#include <memory> // 为了 std::shared_ptr 和 std::weak_ptr

namespace core {

    // --- 前向声明 ---
    class Intent;
    class BroadcastReceiver;
    class IntentFilter;

    // =================================================================================
    // Intent, BroadcastReceiver, IntentFilter (这几个类保持不变)
    // =================================================================================
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
    // LocalBroadcastManager - 核心管理类 (健壮版本)
    // =================================================================================
    class LocalBroadcastManager final {
    public:
        static LocalBroadcastManager& getInstance();
        LocalBroadcastManager(const LocalBroadcastManager&) = delete;
        LocalBroadcastManager& operator=(const LocalBroadcastManager&) = delete;

        // [FIX 1] API 变更: 接收 shared_ptr
        void registerReceiver(std::shared_ptr<BroadcastReceiver> receiver, const IntentFilter& filter);

        // [FIX 1] API 变更: 接收 shared_ptr
        void unregisterReceiver(const std::shared_ptr<BroadcastReceiver>& receiver);

        void sendBroadcast(const Intent& intent);

    private:
        LocalBroadcastManager() = default;

        std::mutex m_mutex;
        // [FIX 1] 内部存储变更为 weak_ptr
        std::map<std::string, std::vector<std::weak_ptr<BroadcastReceiver>>> m_actions;
        std::map<BroadcastReceiver*, std::vector<std::string>> m_receivers; // key 仍然是裸指针，仅作为唯一标识符
    };

}