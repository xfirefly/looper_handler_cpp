#include "Preferences.h"
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm> // for std::find

// --- 辅助函数，用于打印 Preferences 的所有内容 ---
void print_all_prefs(const std::shared_ptr<core::Preferences>& prefs) {
    std::cout << "\n--- Current Preferences ---" << std::endl;
    auto all_data = prefs->getAll();
    if (all_data.empty()) {
        std::cout << "(Empty)" << std::endl;
    } else {
        for (const auto& pair : all_data) {
            std::cout << "'" << pair.first << "': ";
            const auto& type = pair.second.type();
            if (type == typeid(std::string)) {
                std::cout << "\"" << core::PreferencesManager::getInstance("sample_prefs")->getString(pair.first, "") << "\" (String)";
            } else if (type == typeid(int)) {
                std::cout << prefs->getInt(pair.first, 0) << " (int)";
            } else if (type == typeid(long long)) {
                std::cout << prefs->getLong(pair.first, 0LL) << " (long long)";
            } else if (type == typeid(double)) {
                std::cout << prefs->getFloat(pair.first, 0.0) << " (double)";
            } else if (type == typeid(bool)) {
                std::cout << (prefs->getBool(pair.first, false) ? "true" : "false") << " (bool)";
            } else if (type == typeid(std::vector<std::string>)) {
                std::cout << "[";
                auto string_set = prefs->getStringSet(pair.first, {});
                for (size_t i = 0; i < string_set.size(); ++i) {
                    std::cout << "\"" << string_set[i] << "\"";
                    if (i < string_set.size() - 1) std::cout << ", ";
                }
                std::cout << "] (StringSet)";
            }
            std::cout << std::endl;
        }
    }
    std::cout << "---------------------------\n" << std::endl;
}


// --- 自定义监听器，用于演示变更回调 ---
class MyPreferenceListener : public core::OnPreferenceChangeListener {
public:
    void onPreferenceChanged(core::Preferences* preferences, const std::string& key) override {
        std::cout << "[Listener]: Preference '" << key << "' has changed!" << std::endl;
        // 可以在这里根据 key 执行特定操作
    }
};


int main() {
    // 获取默认的 Preferences 实例
    auto defaultPrefs = core::PreferencesManager::getDefaultPreferences();
    print_all_prefs(defaultPrefs);
    // 使用它
    defaultPrefs->edit()->putString("welcome_message", "Hello Default Prefs!").commit(); 

    print_all_prefs(core::PreferencesManager::getDefaultPreferences());
    

    std::cout << "Default Welcome Message: " << defaultPrefs->getString("welcome_message", "") << std::endl;


    std::cout << "--- Preferences Sample ---" << std::endl;

    // 1. 获取一个名为 "sample_prefs" 的 Preferences 实例
    auto prefs = core::PreferencesManager::getInstance("sample_prefs");

    // 2. 注册一个变更监听器
    auto listener = std::make_shared<MyPreferenceListener>();
    prefs->registerOnPreferenceChangeListener(listener);
    std::cout << "Listener registered." << std::endl;

    // 3. 清理掉上次运行可能留下的数据
    std::cout << "\nClearing all previous data..." << std::endl;
    prefs->edit()->clear().commit(); // <--- 已更正
    print_all_prefs(prefs);

    // 4. 使用 Editor 写入各种数据类型
    std::cout << "Putting various data types using commit()..." << std::endl;
    auto editor = prefs->edit();
    editor->putString("user_name", "TestUser");
    editor->putInt("login_count", 5);
    editor->putLong("last_login_timestamp", 1672531200000LL); // long long
    editor->putFloat("user_score", 98.6); // double
    editor->putBool("is_premium_member", true);
    editor->putStringSet("user_tags", {"developer", "gamer", "C++"});
    
    // 使用 commit() 同步保存，它会立即写入文件并返回是否成功
    bool committed = editor->commit();
    std::cout << "commit() " << (committed ? "succeeded." : "failed.") << std::endl;
    
    // 检查监听器是否收到了通知
    print_all_prefs(prefs);

    // 5. 检查 contains() 和读取数据
    std::cout << "\nChecking contains() and getting values..." << std::endl;
    std::cout << "Contains 'user_name'? " << (prefs->contains("user_name") ? "Yes" : "No") << std::endl;
    std::cout << "Contains 'non_existent_key'? " << (prefs->contains("non_existent_key") ? "Yes" : "No") << std::endl;
    std::cout << "User score: " << prefs->getFloat("user_score", 0.0) << std::endl;
    std::cout << "Default value for 'non_existent_key': " << prefs->getString("non_existent_key", "default_val") << std::endl;

    // 6. 使用 remove() 和 apply()
    std::cout << "\nRemoving 'user_score' and changing 'is_premium_member' using apply()..." << std::endl;
    auto editor2 = prefs->edit();
    editor2->remove("user_score");
    editor2->putBool("is_premium_member", false);

    // apply() 是异步保存，它会立即返回，并在后台线程中写入文件
    editor2->apply();
    std::cout << "apply() returned immediately." << std::endl;
    
    // 等待一小会儿，让后台线程有时间完成写入
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    print_all_prefs(prefs);

    // 7. 注销监听器
    std::cout << "\nUnregistering the listener..." << std::endl;
    prefs->unregisterOnPreferenceChangeListener(listener);

    // 再次修改数据
    std::cout << "Changing 'login_count' again..." << std::endl;
    prefs->edit()->putInt("login_count", 10).commit(); // <--- 链式调用也需要注意
    std::cout << "No listener output should appear now." << std::endl;
    print_all_prefs(prefs);

    std::cout << "--- Sample Finished ---" << std::endl;

    return 0;
}