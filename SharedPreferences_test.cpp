#include "gtest/gtest.h"
#include "SharedPreferences.h"
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>

using namespace core;

// 测试用的 SharedPreferences 文件名
const std::string TEST_PREFS_NAME = "test_prefs";

class SharedPreferencesTest : public ::testing::Test {
protected:
    std::shared_ptr<SharedPreferences> prefs;

    void SetUp() override {
        // 每个测试开始前，获取一个新的实例并清空它，确保测试环境独立
        prefs = SharedPreferencesManager::getInstance(TEST_PREFS_NAME);
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
TEST_F(SharedPreferencesTest, StringValue) {
    auto editor = prefs->edit();
    editor->putString("username", "coder");
    editor->commit();

    EXPECT_EQ(prefs->getString("username", "default"), "coder");
    EXPECT_EQ(prefs->getString("non_existent_key", "default"), "default");
}

// 2. 测试基本数据类型: Int
TEST_F(SharedPreferencesTest, IntValue) {
    auto editor = prefs->edit();
    editor->putInt("user_age", 30);
    editor->commit();

    EXPECT_EQ(prefs->getInt("user_age", 0), 30);
    EXPECT_EQ(prefs->getInt("non_existent_key", -1), -1);
}

// 3. 测试新增功能: Long
TEST_F(SharedPreferencesTest, LongValue) {
    long long large_number = 9876543210LL;
    auto editor = prefs->edit();
    editor->putLong("large_number", large_number);
    editor->commit();

    EXPECT_EQ(prefs->getLong("large_number", 0LL), large_number);
    EXPECT_EQ(prefs->getLong("non_existent_key", -1LL), -1LL);
}


// 4. 测试基本数据类型: Float
TEST_F(SharedPreferencesTest, FloatValue) {
    auto editor = prefs->edit();
    editor->putFloat("user_score", 99.5f);
    editor->commit();

    EXPECT_FLOAT_EQ(prefs->getFloat("user_score", 0.0f), 99.5f);
    EXPECT_FLOAT_EQ(prefs->getFloat("non_existent_key", -1.0f), -1.0f);
}

// 5. 测试基本数据类型: Bool
TEST_F(SharedPreferencesTest, BoolValue) {
    auto editor = prefs->edit();
    editor->putBool("is_active", true);
    editor->commit();

    EXPECT_TRUE(prefs->getBool("is_active", false));
    EXPECT_FALSE(prefs->getBool("non_existent_key", false));
}

// 6. 测试新增功能: StringSet
TEST_F(SharedPreferencesTest, StringSetValue) {
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
TEST_F(SharedPreferencesTest, RemoveAndContains) {
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
TEST_F(SharedPreferencesTest, Clear) {
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
TEST_F(SharedPreferencesTest, Apply) {
    auto editor = prefs->edit();
    editor->putString("async_key", "async_value");
    editor->apply();

    // 等待一小段时间让异步写入完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 创建一个新的实例来确保从文件中重新加载
    auto new_prefs = SharedPreferencesManager::getInstance(TEST_PREFS_NAME);
    EXPECT_EQ(new_prefs->getString("async_key", ""), "async_value");
}

// 10. 测试新增功能: getAll()
TEST_F(SharedPreferencesTest, GetAll) {
    auto editor = prefs->edit();
    editor->putString("name", "test");
    editor->putInt("version", 1);
    editor->putBool("enabled", true);
    editor->commit();

    auto all_prefs = prefs->getAll();
    ASSERT_EQ(all_prefs.size(), 3);
    EXPECT_EQ(std::any_cast<std::string>(all_prefs["name"]), "test");
    EXPECT_EQ(std::any_cast<int>(all_prefs["version"]), 1);
    EXPECT_EQ(std::any_cast<bool>(all_prefs["enabled"]), true);
}


// 11. 测试新增功能: 变更监听器
class MockListener : public OnSharedPreferenceChangeListener {
public:
    void onSharedPreferenceChanged(SharedPreferences* sharedPreferences, const std::string& key) override {
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

TEST_F(SharedPreferencesTest, ListenerTest) {
    auto listener = std::make_shared<MockListener>();
    prefs->registerOnSharedPreferenceChangeListener(listener);

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
    prefs->unregisterOnSharedPreferenceChangeListener(listener);
    auto editor3 = prefs->edit();
    editor3->putBool("another_key", true);
    editor3->commit();
    keys = listener->getChangedKeys();
    EXPECT_TRUE(keys.empty());
}