#include "SharedPreferences.h"
#include "WorkerThread.h"
#define TOML_ENABLE_FORMATTERS 1
#include "toml.hpp" // 包含 tomlplusplus 库
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <algorithm> // for std::find
#include <climits>   // for INT_MIN, INT_MAX

namespace core {

// --- SharedPreferencesManager (无需改动) ---
std::mutex SharedPreferencesManager::sMutex;
std::map<std::string, std::shared_ptr<SharedPreferences>> SharedPreferencesManager::sInstances;

std::shared_ptr<SharedPreferences> SharedPreferencesManager::getInstance(const std::string& name) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sInstances.find(name) == sInstances.end()) {
        struct PrefsMaker : public SharedPreferences {
            PrefsMaker(const std::string& name) : SharedPreferences(name) {}
        };
        sInstances[name] = std::make_shared<PrefsMaker>(name);
    }
    return sInstances[name];
}

// --- SharedPreferences 实现 ---
SharedPreferences::SharedPreferences(const std::string& name) {
    std::filesystem::path dir_path;

#ifdef _WIN32
    char* buffer = nullptr;
    size_t size = 0;
    if (_dupenv_s(&buffer, &size, "USERPROFILE") == 0 && buffer != nullptr) {
        dir_path = buffer;
        free(buffer);
    }
    else {
        dir_path = ".";
    }
#else
    const char* home_dir = std::getenv("HOME");
    dir_path = (home_dir ? home_dir : ".");
#endif

    dir_path /= ".cpp_prefs/";

    std::filesystem::create_directories(dir_path);
    mFilePath = (dir_path / (name + ".toml")).string();

    loadFromFile();
}

void SharedPreferences::loadFromFile() {
    std::lock_guard<std::mutex> lock(mMutex);

    toml::table tbl;
    try {
        tbl = toml::parse_file(mFilePath);
    } catch (const toml::parse_error& err) {
        if (std::filesystem::exists(mFilePath) && std::filesystem::file_size(mFilePath) > 0) {
            std::cerr << "Failed to load preferences from " << mFilePath << ":\n" << err << std::endl;
        }
        return;
    }

    for (auto const& [key, val] : tbl) {
        std::string key_str(key.str());
        if (val.is_string()) {
            mData[key_str] = *val.as_string();
        } else if (val.is_integer()) {
            mData[key_str] = static_cast<long long>(*val.as_integer());
        } else if (val.is_floating_point()) {
            mData[key_str] = static_cast<double>(*val.as_floating_point());
        } else if (val.is_boolean()) {
            mData[key_str] = *val.as_boolean();
        } else if (val.is_array()) {
            auto& arr = *val.as_array();
            if (!arr.empty() && arr[0].is_string()) {
                std::vector<std::string> string_set;
                for (const auto& elem : arr) {
                    string_set.push_back(std::string(*elem.as_string()));
                }
                mData[key_str] = string_set;
            }
        }
    }
}

 
bool SharedPreferences::saveToFile(const std::map<std::string, std::any>& data, const std::map<std::string, std::any>& modifications) {
    toml::table tbl;

    for (const auto& pair : data) {
        const auto& type = pair.second.type();
        if (type == typeid(std::string)) {
            tbl.insert(pair.first, std::any_cast<std::string>(pair.second));
        }
        else if (type == typeid(int)) {
            tbl.insert(pair.first, std::any_cast<int>(pair.second));
        }
        else if (type == typeid(long long)) {
            tbl.insert(pair.first, std::any_cast<long long>(pair.second));
        }
        else if (type == typeid(double)) {
            tbl.insert(pair.first, static_cast<double>(std::any_cast<double>(pair.second)));
        }
        else if (type == typeid(bool)) {
            tbl.insert(pair.first, std::any_cast<bool>(pair.second));
        }
        else if (type == typeid(std::vector<std::string>)) {
            toml::array arr;
            for (const auto& s : std::any_cast<std::vector<std::string>>(pair.second)) {
                arr.push_back(s);
            }
            tbl.insert(pair.first, arr);
        }
    }

    try {
        std::ofstream file(mFilePath, std::ios::out | std::ios::trunc);
        file << tbl;
        std::cout << "Preferences saved to " << mFilePath << std::endl;
        
        std::vector<std::shared_ptr<OnSharedPreferenceChangeListener>> listeners_copy;
        {
            std::lock_guard<std::mutex> lock(mListenersMutex);
            listeners_copy = mListeners;
        }

        auto self = shared_from_this();
        for(const auto& mod : modifications) {
            for(const auto& listener : listeners_copy) {
                if(listener) {
                    listener->onSharedPreferenceChanged(self.get(), mod.first);
                }
            }
        }

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to save preferences to " << mFilePath << ": " << e.what() << std::endl;
        return false;
    }
}

 
std::string SharedPreferences::getString(const std::string& key, const std::string& defValue) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mData.find(key);
    if (it != mData.end() && it->second.type() == typeid(std::string)) {
        return std::any_cast<std::string>(it->second);
    }
    return defValue;
}

int SharedPreferences::getInt(const std::string& key, int defValue) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mData.find(key);
    if (it != mData.end()) {
        // 首先检查它是否直接就是 int 类型
        if (it->second.type() == typeid(int)) {
            return std::any_cast<int>(it->second);
        }
        // 由于 loadFromFile 会将所有整数存为 long long, 我们需要处理这种情况
        if (it->second.type() == typeid(long long)) {
            long long val = std::any_cast<long long>(it->second);
            // 在转换前，检查数值是否在 int 的范围内，防止溢出
            if (val >= INT_MIN && val <= INT_MAX) {
                return static_cast<int>(val);
            }
        }
    }
    return defValue;
}

long long SharedPreferences::getLong(const std::string& key, long long defValue) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mData.find(key);
    if (it != mData.end() && it->second.type() == typeid(long long)) {
        return std::any_cast<long long>(it->second);
    }
    return defValue;
}

double SharedPreferences::getFloat(const std::string& key, double defValue) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mData.find(key);
    if (it != mData.end() && it->second.type() == typeid(double)) {
        return std::any_cast<double>(it->second);
    }
    return defValue;
}

bool SharedPreferences::getBool(const std::string& key, bool defValue) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mData.find(key);
    if (it != mData.end() && it->second.type() == typeid(bool)) {
        return std::any_cast<bool>(it->second);
    }
    return defValue;
}

std::vector<std::string> SharedPreferences::getStringSet(const std::string& key, const std::vector<std::string>& defValue) const {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mData.find(key);
    if (it != mData.end() && it->second.type() == typeid(std::vector<std::string>)) {
        return std::any_cast<std::vector<std::string>>(it->second);
    }
    return defValue;
}

 
std::map<std::string, std::any> SharedPreferences::getAll() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mData;
}

bool SharedPreferences::contains(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mData.find(key) != mData.end();
}

std::unique_ptr<SharedPreferences::Editor> SharedPreferences::edit() {
    return std::make_unique<Editor>(*this);
}

void SharedPreferences::registerOnSharedPreferenceChangeListener(std::shared_ptr<OnSharedPreferenceChangeListener> listener) {
    if(!listener) return;
    std::lock_guard<std::mutex> lock(mListenersMutex);
    mListeners.push_back(listener);
}

void SharedPreferences::unregisterOnSharedPreferenceChangeListener(std::shared_ptr<OnSharedPreferenceChangeListener> listener) {
    if(!listener) return;
    std::lock_guard<std::mutex> lock(mListenersMutex);
    mListeners.erase(std::remove(mListeners.begin(), mListeners.end(), listener), mListeners.end());
}

SharedPreferences::Editor::Editor(SharedPreferences& prefs) : mPrefs(prefs) {}

SharedPreferences::Editor& SharedPreferences::Editor::putString(const std::string& key, const std::string& value) {
    mModifications[key] = value;
    return *this;
}

SharedPreferences::Editor& SharedPreferences::Editor::putInt(const std::string& key, int value) {
    mModifications[key] = value;
    return *this;
}

SharedPreferences::Editor& SharedPreferences::Editor::putLong(const std::string& key, long long value) {
    mModifications[key] = value;
    return *this;
}

SharedPreferences::Editor& SharedPreferences::Editor::putFloat(const std::string& key, double value) {
    mModifications[key] = value;
    return *this;
}

SharedPreferences::Editor& SharedPreferences::Editor::putBool(const std::string& key, bool value) {
    mModifications[key] = value;
    return *this;
}

SharedPreferences::Editor& SharedPreferences::Editor::putStringSet(const std::string& key, const std::vector<std::string>& value) {
    mModifications[key] = value;
    return *this;
}

SharedPreferences::Editor& SharedPreferences::Editor::remove(const std::string& key) {
    mModifications[key] = std::any(); // 使用空的 any 来标记移除
    return *this;
}

SharedPreferences::Editor& SharedPreferences::Editor::clear() {
    mClearRequest = true;
    return *this;
}

bool SharedPreferences::Editor::commit() {
    std::map<std::string, std::any> dataToWrite;
    std::map<std::string, std::any> modifications_copy = mModifications;
    {
        std::lock_guard<std::mutex> lock(mPrefs.mMutex);
        if (mClearRequest) {
            mPrefs.mData.clear();
        }
        for (const auto& mod : mModifications) {
            if (mod.second.has_value()) {
                mPrefs.mData[mod.first] = mod.second;
            }
            else {
                mPrefs.mData.erase(mod.first);
            }
        }
        dataToWrite = mPrefs.mData;
    }
    return mPrefs.saveToFile(dataToWrite, modifications_copy);
}

// 将 writerThread 和 once_flag 作为静态变量，保证它们在整个程序中是唯一的。
static std::unique_ptr<WorkerThread> g_writerThread;
static std::once_flag g_writerThreadOnceFlag;

void SharedPreferences::Editor::apply() {
    std::map<std::string, std::any> dataToWrite;
    std::map<std::string, std::any> modifications_copy = mModifications;
    {
        std::lock_guard<std::mutex> lock(mPrefs.mMutex);
        if (mClearRequest) {
            mPrefs.mData.clear();
        }
        for (const auto& mod : mModifications) {
            if (mod.second.has_value()) {
                mPrefs.mData[mod.first] = mod.second;
            }
            else {
                mPrefs.mData.erase(mod.first);
            }
        }
        dataToWrite = mPrefs.mData;
    }

    // 使用 std::call_once 来确保后台线程只被创建和启动一次
    std::call_once(g_writerThreadOnceFlag, []() {
        g_writerThread = std::make_unique<WorkerThread>("SharedPreferencesWriter");
        g_writerThread->start();
    });

    // 现在可以安全地使用 g_writerThread
    if (g_writerThread) {
        g_writerThread->post([prefs = &mPrefs, data = std::move(dataToWrite), mods = std::move(modifications_copy)]() {
            prefs->saveToFile(data, mods);
        });
    }
}


} // namespace core