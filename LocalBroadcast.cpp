#include "LocalBroadcast.h"
#include <iostream>
#include <algorithm>
#include <stdexcept> // For std::exception

namespace core {

    // --- Intent, IntentFilter 实现 (保持不变) ---
    Intent::Intent(std::string_view action) : m_action(action) {}
    std::string Intent::getAction() const { return m_action; }
    IntentFilter::IntentFilter(std::string_view action) { addAction(action); }
    void IntentFilter::addAction(std::string_view action) { m_actions.emplace_back(action); }
    const std::vector<std::string>& IntentFilter::getActions() const { return m_actions; }


    // =================================================================================
    // LocalBroadcastManager 实现 (健壮版本)
    // =================================================================================
    LocalBroadcastManager& LocalBroadcastManager::getInstance() {
        static LocalBroadcastManager instance;
        return instance;
    }

    LocalBroadcastManager::LocalBroadcastManager() {
        mWorkerThread = std::make_unique<core::WorkerThread>("LocalBroadcastThread");
        mWorkerThread->start();
    }
 
    LocalBroadcastManager::~LocalBroadcastManager() {
        if (mWorkerThread) {
            mWorkerThread->finish();
            mWorkerThread->join();
        }
    }

    // [FIX 1] registerReceiver 实现更新
    void LocalBroadcastManager::registerReceiver(std::shared_ptr<BroadcastReceiver> receiver, const IntentFilter& filter) {
        if (!receiver) return;

        std::lock_guard<std::mutex> lock(m_mutex);

        const auto& actions = filter.getActions();
        BroadcastReceiver* raw_ptr = receiver.get();

        // 存储反向映射，用于快速注销
        m_receivers[raw_ptr] = actions;

        // 将 weak_ptr 添加到每个 action 的监听列表中
        // `weak_ptr` 不增加引用计数，只观察对象 
        for (const auto& action : actions) {
            m_actions[action].push_back(receiver); // shared_ptr 隐式转换为 weak_ptr
        }
    }

    // [FIX 1] unregisterReceiver 实现更新
    void LocalBroadcastManager::unregisterReceiver(const std::shared_ptr<BroadcastReceiver>& receiver) {
        if (!receiver) return;

        std::lock_guard<std::mutex> lock(m_mutex);

        BroadcastReceiver* raw_ptr = receiver.get();
        auto it = m_receivers.find(raw_ptr);

        if (it != m_receivers.end()) {
            // 从每个 action 的监听列表中移除该接收器
            for (const auto& action : it->second) {
                auto& actionReceivers = m_actions[action];
                // 这里使用了经典的 "erase-remove idiom"，是 C++ 中最高效、最安全的删除容器元素的方式。
                // 1. `std::remove_if`：它会遍历 `actionReceivers` 列表，将所有“不该被删除”的元素
                //    移动到容器的前部。它返回一个指向“逻辑上”新结尾的迭代器。它并不会真的缩减容器大小。
                //    我们的判断条件是：
                //    a) `weak_receiver.expired()`: 如果接收器对象已经被销毁（比如 `shared_ptr` 计数归零），
                //       它的 `weak_ptr` 就会过期。我们顺便清理掉这些无效的指针。
                //    b) `weak_receiver.lock().get() == raw_ptr`: 如果 `weak_ptr` 仍然有效，我们
                //       就把它提升为 `shared_ptr` 并获取裸指针，与我们要注销的接收器进行比较。
                // 2. `actionReceivers.erase()`：然后，我们用 `erase` 方法，将从这个新的“逻辑结尾”到
                //    实际物理结尾之间的所有元素（也就是所有待删除的元素）一次性地从容器中物理删除。
                // 这种做法避免了在循环中直接删除元素可能导致的迭代器失效问题。
                 
                actionReceivers.erase(
                    std::remove_if(actionReceivers.begin(), actionReceivers.end(),
                        [raw_ptr](const std::weak_ptr<BroadcastReceiver>& weak_receiver) {
                            return weak_receiver.expired() || weak_receiver.lock().get() == raw_ptr;
                        }),
                    actionReceivers.end()
                );
            }
            // 从总表中删除
            m_receivers.erase(it);
        }
    }

    void LocalBroadcastManager::sendBroadcast(const Intent& intent) {
        std::vector<std::weak_ptr<BroadcastReceiver>> receiversToNotify;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const std::string action = intent.getAction();

            auto it = m_actions.find(action);
            if (it != m_actions.end()) {
                receiversToNotify = it->second;
            }
        }

        if (!mWorkerThread) {
            std::cerr << "ERROR: WorkerThread is not available in LocalBroadcastManager." << std::endl;
            return;
        }

        for (const auto& weak_receiver : receiversToNotify) {
            // 这是使用 `weak_ptr` 的核心所在。在调用 `onReceive` 之前，必须先尝试将 `weak_ptr`
            // “锁” (lock) 为一个 `shared_ptr`。
            // `weak_receiver.lock()` 会返回一个 `shared_ptr`：
            // - 如果原始对象仍然存在，它会返回一个有效的、指向该对象的 `shared_ptr`。
            // - 如果原始对象已经被销毁，它会返回一个空的 `shared_ptr`。
            // 通过 `if (auto shared_receiver = ...)`，我们能原子地、安全地检查对象的存活状态
            // 并获取其所有权。这可以防止在多线程环境中，刚刚检查完对象还存在，但在调用其方法前，
            // 对象就被另一个线程销毁了（即“野指针”问题）。            
            if (auto shared_receiver = weak_receiver.lock()) {                
                // 将 onReceive 调用 post到工作线程
                mWorkerThread->post([shared_receiver, intent]() {
                    try {
                        // 这个 lambda 表达式中的代码将在 WorkerThread 中执行
                        shared_receiver->onReceive(intent);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "ERROR: An exception was thrown in a BroadcastReceiver (executed on worker thread): "
                            << e.what() << std::endl;
                    }
                    catch (...) {
                        std::cerr << "ERROR: An unknown exception was thrown in a BroadcastReceiver (executed on worker thread)."
                            << std::endl;
                    }
                });
            }
        }
    }    
}
 

#if 0
#include "LocalBroadcast.h"
#include <iostream>

// 一个行为正常的接收器
class GoodReceiver : public BroadcastReceiver {
public:
    void onReceive(const Intent& intent) override {
        std::cout << "GoodReceiver: Received broadcast! Data: "
            << *intent.getExtra<std::string>("data") << std::endl;
    }
};

// 一个会抛出异常的接收器
class BadReceiver : public BroadcastReceiver {
public:
    void onReceive(const Intent& intent) override {
        std::cout << "BadReceiver: I'm about to throw an exception..." << std::endl;
        throw std::runtime_error("Something went wrong inside BadReceiver!");
    }
};

// 一个生命周期很短的接收器
class ShortLivedReceiver : public BroadcastReceiver {
public:
    ShortLivedReceiver() { std::cout << "ShortLivedReceiver: I have been created." << std::endl; }
    ~ShortLivedReceiver() { std::cout << "ShortLivedReceiver: I have been destroyed." << std::endl; }

    void onReceive(const Intent& intent) override {
        // 这个方法永远不会被调用，因为对象在广播前就被销毁了
        std::cout << "ShortLivedReceiver: If you see this, something is wrong." << std::endl;
    }
};


int main() {
    auto& lbm = LocalBroadcastManager::getInstance();
    IntentFilter filter("my_action");

    // 1. 注册两个长期存活的接收器
    auto good_receiver = std::make_shared<GoodReceiver>();
    auto bad_receiver = std::make_shared<BadReceiver>();
    lbm.registerReceiver(good_receiver, filter);
    lbm.registerReceiver(bad_receiver, filter);

    // 2. 在一个独立作用域内创建并注册一个短命的接收器
    {
        auto short_lived_receiver = std::make_shared<ShortLivedReceiver>();
        lbm.registerReceiver(short_lived_receiver, filter);
        std::cout << "\n--- ShortLivedReceiver is about to go out of scope ---\n" << std::endl;
    } // <-- short_lived_receiver 的 shared_ptr 在这里被销毁, 对象被析构

    // 3. 发送广播
    std::cout << "--- Sending broadcast... ---" << std::endl;
    Intent intent("my_action");
    intent.putExtra("data", std::string("Hello Robust World!"));
    lbm.sendBroadcast(intent);
    std::cout << "--- Broadcast sent. ---" << std::endl;

    // 4. 清理
    lbm.unregisterReceiver(good_receiver);
    lbm.unregisterReceiver(bad_receiver);

    std::cout << "\nProgram finished successfully." << std::endl;
    return 0;
}
#endif
