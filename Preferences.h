#ifndef SHARED_PREFERENCES_H
#define SHARED_PREFERENCES_H

#include <string>
#include <map>
#include <any>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <functional> // 为了 std::function

namespace core {

class Preferences; // 前向声明

/**
 * @class OnPreferenceChangeListener
 * @brief 当共享偏好设置发生变化时接收回调的接口。
 */
class OnPreferenceChangeListener {
public:
    virtual ~OnPreferenceChangeListener() = default;
    /**
     * @brief 当共享偏好设置发生变化时调用。
     * @param preferences 发生变化的 Preferences 实例。
     * @param key 发生变化的偏好设置的键。
     */
    virtual void onPreferenceChanged(Preferences* preferences, const std::string& key) = 0;
};

/**
 * @class Preferences
 * @brief 提供轻量级的键值对存储，用于保存应用配置。
 * 
 * 类似于 Android 的 SharedPreferences。支持多种数据类型（String, Int, Float, Bool, StringSet）。
 * 数据持久化到本地文件。
 *
 * <h2>使用示例</h2>
 * @code
 * auto prefs = core::PreferencesManager::getInstance("user_settings");
 * 
 * // 写配置
 * prefs->edit()
 *      ->putString("username", "alice")
 *      ->putInt("login_count", 42)
 *      ->commit();
 * 
 * // 读配置
 * std::string username = prefs->getString("username", "guest");
 * int count = prefs->getInt("login_count", 0);
 * @endcode
 */
class Preferences : public std::enable_shared_from_this<Preferences> {
public:
    class Editor {
    public:
        Editor(const Editor&) = delete;
        Editor& operator=(const Editor&) = delete;
        Editor& putString(const std::string& key, const std::string& value);
        Editor& putInt(const std::string& key, int64_t value);
        Editor& putFloat(const std::string& key, double value);
        Editor& putBool(const std::string& key, bool value);
        Editor& putStringSet(const std::string& key, const std::vector<std::string>& value); // 新增
        Editor& remove(const std::string& key);
        Editor& clear();
        bool commit();
     

        friend class Preferences;
        Editor(Preferences& prefs);
    private:
        Preferences& mPrefs;
        std::map<std::string, std::any> mModifications;
        bool mClearRequest = false;
    };

    Preferences(const Preferences&) = delete;
    Preferences& operator=(const Preferences&) = delete;

    std::string getString(const std::string& key, const std::string& defValue) const;
    int64_t getInt(const std::string& key, int64_t defValue) const;
    double getFloat(const std::string& key, double defValue) const;
    bool getBool(const std::string& key, bool defValue) const;
    std::vector<std::string> getStringSet(const std::string& key, const std::vector<std::string>& defValue) const; // 新增
    std::map<std::string, std::any> getAll() const; // 新增
    bool contains(const std::string& key) const;
    std::unique_ptr<Editor> edit();

    // 新增监听器相关方法
    void registerOnPreferenceChangeListener(std::shared_ptr<OnPreferenceChangeListener> listener);
    void unregisterOnPreferenceChangeListener(std::shared_ptr<OnPreferenceChangeListener> listener);

private:
    friend class PreferencesManager;
    explicit Preferences(const std::string& name);
    void loadFromFile();
    bool saveToFile(const std::map<std::string, std::any>& data, const std::map<std::string, std::any>& modifications);
    
    mutable std::mutex mMutex;
    std::string mFilePath;
    std::map<std::string, std::any> mData;
    std::vector<std::shared_ptr<OnPreferenceChangeListener>> mListeners; // 新增
    mutable std::mutex mListenersMutex; // 新增
    mutable std::mutex mFileWriteMutex; // 新增：用于序列化文件写操作防止竞争
};

class PreferencesManager {
public:
    static std::shared_ptr<Preferences> getInstance(const std::string& name);
    static std::shared_ptr<Preferences> getDefaultPreferences();
private:
    static std::mutex sMutex;
    static std::map<std::string, std::shared_ptr<Preferences>> sInstances;
};

} // namespace core

#endif // SHARED_PREFERENCES_H