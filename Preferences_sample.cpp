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


int main0() {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////
 
// 自定义监听器实现
class XPreferenceListener : public core::OnPreferenceChangeListener {
public:
    void onPreferenceChanged(core::Preferences* preferences, const std::string& key) override {
        std::cout << "Listener: Preference '" << key << "' changed!" << std::endl;
        // 你可以在这里根据 key 获取更新后的值
        // 例如：
        if (key == "appName") {
            std::cout << "New appName: " << preferences->getString("appName", "default") << std::endl;
        }
    }
};

// 辅助函数，用于打印 std::any 的内容
void printAny(const std::string& key, const std::any& value) {
    std::cout << "  " << key << ": ";
    if (value.type() == typeid(std::string)) {
        std::cout << "String = " << std::any_cast<std::string>(value) << std::endl;
    } else if (value.type() == typeid(int64_t)) {
        std::cout << "Int64 = " << std::any_cast<int64_t>(value) << std::endl;
    } else if (value.type() == typeid(double)) {
        std::cout << "Double = " << std::any_cast<double>(value) << std::endl;
    } else if (value.type() == typeid(bool)) {
        std::cout << "Bool = " << std::any_cast<bool>(value) << std::endl;
    } else if (value.type() == typeid(std::vector<std::string>)) {
        std::cout << "StringSet = [";
        const auto& vec = std::any_cast<std::vector<std::string>>(value);
        for (size_t i = 0; i < vec.size(); ++i) {
            std::cout << "\"" << vec[i] << "\"";
            if (i < vec.size() - 1) {
                std::cout << ", ";
            }
        }
        std::cout << "]" << std::endl;
    } else {
        std::cout << "Unknown type" << std::endl;
    }
}

int main() {
    std::cout << "--- Preferences Demo ---" << std::endl;

    // 1. 获取一个 Preferences 实例
    std::shared_ptr<core::Preferences> prefs = core::PreferencesManager::getInstance("my_app_settings");
    std::shared_ptr<core::Preferences> defaultPrefs = core::PreferencesManager::getDefaultPreferences();
        for (const auto& pair : prefs->getAll()) {
            printAny(pair.first, pair.second);
        }
        
    // 注册监听器
    std::shared_ptr<XPreferenceListener> listener = std::make_shared<XPreferenceListener>();
    prefs->registerOnPreferenceChangeListener(listener);

    std::cout << "\n--- Step 1: Initializing and Writing Complex Preferences ---" << std::endl;
    // 2. 使用 Editor 写入复杂的偏好设置，覆盖所有 put 函数
    if (auto editor = prefs->edit()) {
        editor->putString("appName", "MyAwesomeApp");
        editor->putInt("versionCode", 1024);
        editor->putFloat("floatSetting", 3.14159);
        editor->putBool("darkModeEnabled", true);
        editor->putStringSet("featureFlags", {"alpha_feature", "beta_test", "experimental_ui"});
        editor->putString("developerInfo.name", "John Doe"); // 模拟嵌套结构
        editor->putInt("developerInfo.id", 12345);
        editor->putString("api.baseURL", "https://api.example.com/v1");
        editor->putBool("api.debugMode", false);
        editor->putInt("user.id", 98765);
        editor->putString("user.username", "testuser");
        editor->putStringSet("user.roles", {"admin", "editor"});
        editor->putFloat("display.brightness", 0.85);
        editor->putInt("network.timeoutMs", 5000);

        std::cout << "Committing initial preferences..." << std::endl;
        if (editor->commit()) {
            std::cout << "Initial preferences committed successfully." << std::endl;
        } else {
            std::cerr << "Failed to commit initial preferences." << std::endl;
        }
    }

    std::cout << "\n--- Step 2: Reading All Preferences ---" << std::endl;
    // 3. 读取所有偏好设置并打印
    std::map<std::string, std::any> allPrefs = prefs->getAll();
    if (allPrefs.empty()) {
        std::cout << "No preferences found or failed to load." << std::endl;
    } else {
       // std::cout << "All current preferences (" << prefs->mFilePath << "):" << std::endl;
        for (const auto& pair : allPrefs) {
            printAny(pair.first, pair.second);
        }
    }

    std::cout << "\n--- Step 3: Demonstrating get functions with default values ---" << std::endl;
    // 演示 get 函数
    std::cout << "appName: " << prefs->getString("appName", "DefaultApp") << std::endl;
    std::cout << "versionCode: " << prefs->getInt("versionCode", 0) << std::endl;
    std::cout << "floatSetting: " << prefs->getFloat("floatSetting", 0.0) << std::endl;
    std::cout << "darkModeEnabled: " << (prefs->getBool("darkModeEnabled", false) ? "true" : "false") << std::endl;
    
    std::vector<std::string> defaultFlags = {"default_flag"};
    std::vector<std::string> featureFlags = prefs->getStringSet("featureFlags", defaultFlags);
    std::cout << "featureFlags: [";
    for (size_t i = 0; i < featureFlags.size(); ++i) {
        std::cout << "\"" << featureFlags[i] << "\"";
        if (i < featureFlags.size() - 1) std::cout << ", ";
    }
    std::cout << "]" << std::endl;

    // 尝试获取不存在的键
    std::cout << "nonExistentKey (default 'nope'): " << prefs->getString("nonExistentKey", "nope") << std::endl;
    std::cout << "nonExistentInt (default 999): " << prefs->getInt("nonExistentInt", 999) << std::endl;


    std::cout << "\n--- Step 4: Demonstrating 'contains' ---" << std::endl;
    std::cout << "Does 'appName' exist? " << (prefs->contains("appName") ? "Yes" : "No") << std::endl;
    std::cout << "Does 'unknownKey' exist? " << (prefs->contains("unknownKey") ? "Yes" : "No") << std::endl;

    std::cout << "\n--- Step 5: Demonstrating update and remove ---" << std::endl;
    if (auto editor = prefs->edit()) {
        editor->putInt("versionCode", 1025); // 更新
        editor->putString("appName", "MyUpdatedApp"); // 更新
        editor->remove("api.debugMode"); // 移除一个键
        editor->putFloat("newSetting", 99.99); // 添加一个新键

        std::cout << "Committing update and removal..." << std::endl;
        if (editor->commit()) {
            std::cout << "Update and removal committed successfully." << std::endl;
        } else {
            std::cerr << "Failed to commit update and removal." << std::endl;
        }
    }

    std::cout << "\n--- Step 6: Reading Preferences after update ---" << std::endl;
    allPrefs = prefs->getAll(); // 重新加载以确保从文件读取最新状态 (Preferences 内部已经更新，但为了模拟真实场景可以重新获取)
    std::cout << "All preferences after update/removal:" << std::endl;
    for (const auto& pair : allPrefs) {
        printAny(pair.first, pair.second);
    }
    std::cout << "Does 'api.debugMode' exist now? " << (prefs->contains("api.debugMode") ? "Yes" : "No") << std::endl;


    std::cout << "\n--- Step 7: Demonstrating 'clear' ---" << std::endl;
    if (auto editor = prefs->edit()) {
        //editor->clear();
        std::cout << "Committing clear request..." << std::endl;
        if (editor->commit()) {
            std::cout << "Preferences cleared successfully." << std::endl;
        } else {
            std::cerr << "Failed to clear preferences." << std::endl;
        }
    }

    std::cout << "\n--- Step 8: Reading Preferences after clear ---" << std::endl;
    allPrefs = prefs->getAll();
    if (allPrefs.empty()) {
        std::cout << "Preferences are empty after clear operation." << std::endl;
    } else {
        std::cout << "Preferences still contain data after clear (unexpected):" << std::endl;
        for (const auto& pair : allPrefs) {
            printAny(pair.first, pair.second);
        }
    }

    // 撤销注册监听器
    prefs->unregisterOnPreferenceChangeListener(listener);
    std::cout << "\nUnregistered listener. Changes will no longer trigger callbacks." << std::endl;

    // 再次修改，验证监听器是否不再触发
    if (auto editor = prefs->edit()) {
        editor->putInt("someOtherSetting", 100);
        editor->commit();
        std::cout << "Modified 'someOtherSetting' after unregistering listener. No listener callback expected." << std::endl;
    }

    std::cout << "\n--- Preferences Demo Finished ---" << std::endl;

    return 0;
}