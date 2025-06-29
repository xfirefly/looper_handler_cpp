#include "gtest/gtest.h"
#include "Preferences.h"
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream> // 用于文件操作
#include <filesystem>
#include <cstdlib> // 用于 std::getenv
#include <algorithm> // for std::find
#include <climits> // for INT_MIN, INT_MAX

using namespace core;

// 测试用的 Preferences 文件名
const std::string TEST_PREFS_NAME = "test_prefs";

class PreferencesTest : public ::testing::Test {
protected:
    std::shared_ptr<Preferences> prefs;

    // 获取测试文件的完整路径
    std::string getTestFilePath() {
        std::filesystem::path dir_path;
#ifdef _WIN32
        char* buffer = nullptr;
        size_t size = 0;
        if (_dupenv_s(&buffer, &size, "USERPROFILE") == 0 && buffer != nullptr) {
            dir_path = buffer;
            free(buffer);
        } else {
            dir_path = ".";
        }
#else
        const char* home_dir = std::getenv("HOME");
        dir_path = (home_dir ? home_dir : ".");
#endif
        dir_path /= ".cpp_prefs/";
        return (dir_path / (TEST_PREFS_NAME + ".toml")).string();
    }


    void SetUp() override {
        // 每个测试开始前，获取一个新的实例并清空它，确保测试环境独立
        prefs = PreferencesManager::getInstance(TEST_PREFS_NAME);
        auto editor = prefs->edit();
        editor->clear();
        editor->commit();
    }

    void TearDown() override {
        // 测试结束后，再次清空
        auto editor = prefs->edit();
        editor->clear();
        editor->commit();
    }
};

// 1. 测试基本数据类型: String
TEST_F(PreferencesTest, StringValue) {
    auto editor = prefs->edit();
    editor->putString("username", "coder");
    editor->commit();

    EXPECT_EQ(prefs->getString("username", "default"), "coder");
    EXPECT_EQ(prefs->getString("non_existent_key", "default"), "default");
}

// 2. 测试基本数据类型: Int
TEST_F(PreferencesTest, IntValue) {
    auto editor = prefs->edit();
    editor->putInt("user_age", 30);
    editor->commit();

    EXPECT_EQ(prefs->getInt("user_age", 0), 30);
    EXPECT_EQ(prefs->getInt("non_existent_key", -1), -1);
}

 

// 4. 测试基本数据类型: Float
TEST_F(PreferencesTest, FloatValue) {
    auto editor = prefs->edit();
    editor->putFloat("user_score", 99.5f);
    editor->commit();

    EXPECT_FLOAT_EQ(prefs->getFloat("user_score", 0.0f), 99.5f);
    EXPECT_FLOAT_EQ(prefs->getFloat("non_existent_key", -1.0f), -1.0f);
}

// 5. 测试基本数据类型: Bool
TEST_F(PreferencesTest, BoolValue) {
    auto editor = prefs->edit();
    editor->putBool("is_active", true);
    editor->commit();

    EXPECT_TRUE(prefs->getBool("is_active", false));
    EXPECT_FALSE(prefs->getBool("non_existent_key", false));
}

// 6. 测试新增功能: StringSet
TEST_F(PreferencesTest, StringSetValue) {
    std::vector<std::string> tags = {"C++", "Android", "Testing"};
    auto editor = prefs->edit();
    editor->putStringSet("tags", tags);
    editor->commit();

    std::vector<std::string> retrieved_tags = prefs->getStringSet("tags", {});
    ASSERT_EQ(retrieved_tags.size(), 3);
    EXPECT_EQ(retrieved_tags[0], "C++");
    EXPECT_EQ(retrieved_tags[1], "Android");
    EXPECT_EQ(retrieved_tags[2], "Testing");

    EXPECT_TRUE(prefs->getStringSet("non_existent_key", {}).empty());
}


// 7. 测试 remove() 和 contains()
TEST_F(PreferencesTest, RemoveAndContains) {
    auto editor = prefs->edit();
    editor->putString("temp_data", "to be removed");
    editor->commit();

    EXPECT_TRUE(prefs->contains("temp_data"));

    auto editor2 = prefs->edit();
    editor2->remove("temp_data");
    editor2->commit();

    EXPECT_FALSE(prefs->contains("temp_data"));
}

// 8. 测试 clear()
TEST_F(PreferencesTest, Clear) {
    auto editor = prefs->edit();
    editor->putString("key1", "value1");
    editor->putInt("key2", 123);
    editor->commit();

    EXPECT_TRUE(prefs->contains("key1"));
    EXPECT_TRUE(prefs->contains("key2"));

    auto editor2 = prefs->edit();
    editor2->clear();
    editor2->commit();

    EXPECT_FALSE(prefs->contains("key1"));
    EXPECT_FALSE(prefs->contains("key2"));
}

// 9. 测试 apply() 异步提交
TEST_F(PreferencesTest, Apply) {
    auto editor = prefs->edit();
    editor->putString("async_key", "async_value");
    editor->commit();

    // 等待一小段时间让异步写入完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 创建一个新的实例来确保从文件中重新加载
    auto new_prefs = PreferencesManager::getInstance(TEST_PREFS_NAME);
    EXPECT_EQ(new_prefs->getString("async_key", ""), "async_value");
}

// 10. 测试新增功能: getAll()
TEST_F(PreferencesTest, GetAll) {
    auto editor = prefs->edit();
    editor->putString("name", "test");
    editor->putInt("version", 1);
    editor->putBool("enabled", true);
    editor->commit();

    auto all_prefs = prefs->getAll();
    ASSERT_EQ(all_prefs.size(), 3);
    EXPECT_EQ(std::any_cast<std::string>(all_prefs["name"]), "test");
    EXPECT_EQ(std::any_cast<int64_t>(all_prefs["version"]), 1);
    EXPECT_EQ(std::any_cast<bool>(all_prefs["enabled"]), true);
}


// 11. 测试新增功能: 变更监听器
class MockListener : public OnPreferenceChangeListener {
public:
    void onPreferenceChanged(Preferences* preferences, const std::string& key) override {
        std::lock_guard<std::mutex> lock(mtx);
        changed_keys.push_back(key);
    }

    std::vector<std::string> getChangedKeys() {
        std::lock_guard<std::mutex> lock(mtx);
        return changed_keys;
    }
    
    void clearChangedKeys() {
        std::lock_guard<std::mutex> lock(mtx);
        changed_keys.clear();
    }
private:
    std::mutex mtx;
    std::vector<std::string> changed_keys;
};

TEST_F(PreferencesTest, ListenerTest) {
    auto listener = std::make_shared<MockListener>();
    prefs->registerOnPreferenceChangeListener(listener);

    // 测试 put
    auto editor = prefs->edit();
    editor->putString("listen_key", "listen_value");
    editor->putInt("listen_int", 100);
    editor->commit();
    
    auto keys = listener->getChangedKeys();
    ASSERT_EQ(keys.size(), 2);
    // 注意：由于 map 是无序的，我们不能假设顺序
    EXPECT_NE(std::find(keys.begin(), keys.end(), "listen_key"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "listen_int"), keys.end());

    listener->clearChangedKeys();
    
    // 测试 remove
    auto editor2 = prefs->edit();
    editor2->remove("listen_key");
    editor2->commit();
    keys = listener->getChangedKeys();
    ASSERT_EQ(keys.size(), 1);
    EXPECT_EQ(keys[0], "listen_key");

    listener->clearChangedKeys();

    // 测试 unregister
    prefs->unregisterOnPreferenceChangeListener(listener);
    auto editor3 = prefs->edit();
    editor3->putBool("another_key", true);
    editor3->commit();
    keys = listener->getChangedKeys();
    EXPECT_TRUE(keys.empty());
}

// =======================================================================================
// 新增的 Bug 复现和验证测试
// 这个测试专门用来验证“修改一个值导致其他值被清除”的 bug
// =======================================================================================
TEST_F(PreferencesTest, FullPersistenceTest) {
    const std::string TEST_PREFS_NAME = "xx";
    // 1. 手动创建一个包含多种数据类型的 toml 文件
    std::string test_file_path = getTestFilePath();
    std::ofstream ofs(test_file_path, std::ios::trunc);
    ASSERT_TRUE(ofs.is_open());
    ofs << R"(
string_val = "original_string"
int_val = 123
long_val = 9876543210
float_val = 45.6
bool_val = true
to_be_modified = "change_me"
)";
    ofs.close();

    // 2. 加载这个文件。
    // 使用一个新的 Preferences 实例来确保它从文件加载
    auto prefs1 = core::PreferencesManager::getInstance(TEST_PREFS_NAME);

    // 3. 只修改其中一个值并提交
   prefs1->edit()->putString("to_be_modified", "i_was_changed").commit();
   prefs1->edit()->putStringSet("new_string_set", {"item1", "item2"}).commit();
    
    // 4. 再次创建一个全新的实例，强制从磁盘重新加载，以验证持久化结果
    auto prefs2 = core::PreferencesManager::getInstance(TEST_PREFS_NAME);

    EXPECT_EQ(prefs1, prefs2); // 确保两个实例是同一个
    
    // 5. 验证
    // a) 验证被修改的值是否正确
    EXPECT_EQ(prefs2->getString("to_be_modified", ""), "i_was_changed");

    // b) (关键) 验证所有未被修改的值是否仍然存在且正确
    EXPECT_EQ(prefs2->getString("string_val", ""), "original_string");
    EXPECT_EQ(prefs2->getInt("int_val", 0), 123);
    EXPECT_EQ(prefs2->getInt("long_val", 0), 9876543210);
    EXPECT_DOUBLE_EQ(prefs2->getFloat("float_val", 0.0), 45.6);
    EXPECT_TRUE(prefs2->getBool("bool_val", false));

    // c) 检查总数是否正确，确保没有多余或缺少键
    EXPECT_EQ(prefs2->getAll().size(), 7);
}