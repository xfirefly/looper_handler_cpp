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

class SharedPreferences; // 前向声明

/**
 * @class OnSharedPreferenceChangeListener
 * @brief 当共享偏好设置发生变化时接收回调的接口。
 */
class OnSharedPreferenceChangeListener {
public:
    virtual ~OnSharedPreferenceChangeListener() = default;
    /**
     * @brief 当共享偏好设置发生变化时调用。
     * @param sharedPreferences 发生变化的 SharedPreferences 实例。
     * @param key 发生变化的偏好设置的键。
     */
    virtual void onSharedPreferenceChanged(SharedPreferences* sharedPreferences, const std::string& key) = 0;
};

class SharedPreferences : public std::enable_shared_from_this<SharedPreferences> {
public:
    class Editor {
    public:
        Editor(const Editor&) = delete;
        Editor& operator=(const Editor&) = delete;
        Editor& putString(const std::string& key, const std::string& value);
        Editor& putInt(const std::string& key, int value);
        Editor& putLong(const std::string& key, long long value); // 新增
        Editor& putFloat(const std::string& key, double value);
        Editor& putBool(const std::string& key, bool value);
        Editor& putStringSet(const std::string& key, const std::vector<std::string>& value); // 新增
        Editor& remove(const std::string& key);
        Editor& clear();
        bool commit();
        void apply();

        friend class SharedPreferences;
        Editor(SharedPreferences& prefs);
    private:
        SharedPreferences& mPrefs;
        std::map<std::string, std::any> mModifications;
        bool mClearRequest = false;
    };

    SharedPreferences(const SharedPreferences&) = delete;
    SharedPreferences& operator=(const SharedPreferences&) = delete;

    std::string getString(const std::string& key, const std::string& defValue) const;
    int getInt(const std::string& key, int defValue) const;
    long long getLong(const std::string& key, long long defValue) const; // 新增
    double getFloat(const std::string& key, double defValue) const;
    bool getBool(const std::string& key, bool defValue) const;
    std::vector<std::string> getStringSet(const std::string& key, const std::vector<std::string>& defValue) const; // 新增
    std::map<std::string, std::any> getAll() const; // 新增
    bool contains(const std::string& key) const;
    std::unique_ptr<Editor> edit();

    // 新增监听器相关方法
    void registerOnSharedPreferenceChangeListener(std::shared_ptr<OnSharedPreferenceChangeListener> listener);
    void unregisterOnSharedPreferenceChangeListener(std::shared_ptr<OnSharedPreferenceChangeListener> listener);

private:
    friend class SharedPreferencesManager;
    explicit SharedPreferences(const std::string& name);
    void loadFromFile();
    bool saveToFile(const std::map<std::string, std::any>& data, const std::map<std::string, std::any>& modifications);
    
    mutable std::mutex mMutex;
    std::string mFilePath;
    std::map<std::string, std::any> mData;
    std::vector<std::shared_ptr<OnSharedPreferenceChangeListener>> mListeners; // 新增
    mutable std::mutex mListenersMutex; // 新增
};

class SharedPreferencesManager {
public:
    static std::shared_ptr<SharedPreferences> getInstance(const std::string& name);
private:
    static std::mutex sMutex;
    static std::map<std::string, std::shared_ptr<SharedPreferences>> sInstances;
};

} // namespace core

#endif // SHARED_PREFERENCES_H