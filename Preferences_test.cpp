#include "gtest/gtest.h"
#include "Preferences.h"
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <climits>
#include <atomic>

using namespace core;

// 为测试定义一个唯一的 Preferences 文件名
const std::string TEST_PREFS_NAME = "gtest_prefs";

// 测试专用的监听器，用于记录变更
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


// --- 测试夹具 (Test Fixture) ---
class PreferencesTest : public ::testing::Test {
protected:
    std::shared_ptr<Preferences> prefs;

    // 在每个测试开始前执行
    void SetUp() override {
        // 获取一个干净的 Preferences 实例，确保测试之间互相独立
        prefs = PreferencesManager::getInstance(TEST_PREFS_NAME);
        prefs->edit()->clear().commit();
    }

    // 在每个测试结束后执行
    void TearDown() override {
        // 清理测试数据
        if (prefs) {
            prefs->edit()->clear().commit();
        }
    }
};

// --- 基本数据类型测试 ---

TEST_F(PreferencesTest, StringValue) {
    prefs->edit()->putString("username", "coder").commit();
    EXPECT_EQ(prefs->getString("username", "default"), "coder");
    EXPECT_EQ(prefs->getString("non_existent", "default"), "default");
}

TEST_F(PreferencesTest, EmptyStringValue) {
    prefs->edit()->putString("empty_string", "").commit();
    EXPECT_EQ(prefs->getString("empty_string", "default"), "");
}

TEST_F(PreferencesTest, IntValue) {
    prefs->edit()->putInt("user_age", 30).commit();
    EXPECT_EQ(prefs->getInt("user_age", 0), 30);
    EXPECT_EQ(prefs->getInt("non_existent", -1), -1);
}

TEST_F(PreferencesTest, IntBoundaryValues) {
    auto editor = prefs->edit();
    editor->putInt("max_int", INT64_MAX);
    editor->putInt("min_int", INT64_MIN);
    editor->putInt("zero_int", 0);
    editor->commit();

    EXPECT_EQ(prefs->getInt("max_int", 0), INT64_MAX);
    EXPECT_EQ(prefs->getInt("min_int", 0), INT64_MIN);
    EXPECT_EQ(prefs->getInt("zero_int", 1), 0);
}

TEST_F(PreferencesTest, FloatValue) {
    prefs->edit()->putFloat("user_score", 99.5).commit();
    EXPECT_FLOAT_EQ(prefs->getFloat("user_score", 0.0), 99.5);
    EXPECT_FLOAT_EQ(prefs->getFloat("non_existent", -1.0), -1.0);
}

TEST_F(PreferencesTest, BoolValue) {
    auto editor = prefs->edit();
    editor->putBool("is_active", true);
    editor->putBool("is_guest", false);
    editor->commit();

    EXPECT_TRUE(prefs->getBool("is_active", false));
    EXPECT_FALSE(prefs->getBool("is_guest", true));
    EXPECT_FALSE(prefs->getBool("non_existent", false));
}

TEST_F(PreferencesTest, StringSetValue) {
    std::vector<std::string> tags = {"C++", "Android", "Testing"};
    prefs->edit()->putStringSet("tags", tags).commit();

    std::vector<std::string> retrieved_tags = prefs->getStringSet("tags", {});
    ASSERT_EQ(retrieved_tags.size(), 3);
    EXPECT_EQ(retrieved_tags[0], "C++");
    EXPECT_EQ(retrieved_tags[1], "Android");
    EXPECT_EQ(retrieved_tags[2], "Testing");
}

TEST_F(PreferencesTest, EmptyStringSetValue) {
    prefs->edit()->putStringSet("empty_set", {}).commit();
    EXPECT_TRUE(prefs->getStringSet("empty_set", {"a"}).empty());
}

// --- 功能性测试 ---

TEST_F(PreferencesTest, RemoveAndContains) {
    prefs->edit()->putString("temp_data", "to be removed").commit();
    EXPECT_TRUE(prefs->contains("temp_data"));

    prefs->edit()->remove("temp_data").commit();
    EXPECT_FALSE(prefs->contains("temp_data"));
    
    // 移除一个不存在的键不应产生错误
    EXPECT_NO_THROW(prefs->edit()->remove("non_existent_key").commit());
}

TEST_F(PreferencesTest, Clear) {
    auto editor = prefs->edit();
    editor->putString("key1", "value1");
    editor->putInt("key2", 123);
    editor->commit();

    ASSERT_TRUE(prefs->contains("key1"));
    ASSERT_TRUE(prefs->contains("key2"));

    prefs->edit()->clear().commit();

    EXPECT_FALSE(prefs->contains("key1"));
    EXPECT_FALSE(prefs->contains("key2"));
    EXPECT_TRUE(prefs->getAll().empty());
}

TEST_F(PreferencesTest, GetAll) {
    auto editor = prefs->edit();
    editor->putString("name", "test");
    editor->putInt("version", 1);
    editor->commit();

    auto all_prefs = prefs->getAll();
    ASSERT_EQ(all_prefs.size(), 2);
    EXPECT_EQ(std::any_cast<std::string>(all_prefs["name"]), "test");
    EXPECT_EQ(std::any_cast<int64_t>(all_prefs["version"]), 1);
}

TEST_F(PreferencesTest, EditorComplexOperations) {
    // 1. 放入初始值
    prefs->edit()->putString("persistent_key", "initial").putInt("counter", 1).commit();

    // 2. 在一个 Editor 会话中执行多种操作
    auto editor = prefs->edit();
    editor->putInt("counter", 2);             // 修改
    editor->putString("new_key", "new_value"); // 新增
    editor->remove("persistent_key");         // 删除
    editor->commit();

    // 3. 验证结果
    EXPECT_EQ(prefs->getInt("counter", 0), 2);
    EXPECT_EQ(prefs->getString("new_key", ""), "new_value");
    EXPECT_FALSE(prefs->contains("persistent_key"));
}

TEST_F(PreferencesTest, EditorClearAndPut) {
    // 1. 放入初始值
    prefs->edit()->putString("old_key", "old_value").commit();

    // 2. 在同一个 Editor 中先 clear 再 put
    auto editor = prefs->edit();
    editor->clear();
    editor->putString("new_key", "fresh_value");
    editor->commit();

    // 3. 验证
    EXPECT_FALSE(prefs->contains("old_key"));
    EXPECT_TRUE(prefs->contains("new_key"));
    EXPECT_EQ(prefs->getString("new_key", ""), "fresh_value");
    EXPECT_EQ(prefs->getAll().size(), 1);
}


// --- 监听器测试 ---

TEST_F(PreferencesTest, ListenerNotification) {
    auto listener = std::make_shared<MockListener>();
    prefs->registerOnPreferenceChangeListener(listener);

    // 1. 测试 put
    prefs->edit()->putString("listen_key", "value1").putInt("other_key", 10).commit();
    auto keys = listener->getChangedKeys();
    ASSERT_EQ(keys.size(), 2);
    // 使用 std::find 是因为 map 的迭代顺序不确定
    EXPECT_NE(std::find(keys.begin(), keys.end(), "listen_key"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "other_key"), keys.end());

    listener->clearChangedKeys();
    
    // 2. 测试 remove
    prefs->edit()->remove("listen_key").commit();
    keys = listener->getChangedKeys();
    ASSERT_EQ(keys.size(), 1);
    EXPECT_EQ(keys[0], "listen_key");

    listener->clearChangedKeys();

    // 3. 测试 clear
    prefs->edit()->putInt("one_more", 1).commit(); // 先放一个值
    listener->clearChangedKeys();
    //prefs->edit()->clear().commit();
    keys = listener->getChangedKeys();
    // clear 应该为每个被移除的键都触发一次通知
    ASSERT_EQ(keys.size(), 2); 
    EXPECT_NE(std::find(keys.begin(), keys.end(), "other_key"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "one_more"), keys.end());
    
    listener->clearChangedKeys();

    // 4. 测试注销
    prefs->unregisterOnPreferenceChangeListener(listener);
    prefs->edit()->putBool("final_key", true).commit();
    keys = listener->getChangedKeys();
    EXPECT_TRUE(keys.empty());
}

// --- 多线程和持久性测试 ---

TEST_F(PreferencesTest, DataPersistence) {
    // 1. 写入数据
    prefs->edit()->putString("session_token", "abc-123").commit();

    // 2. 创建一个新的实例，模拟应用重启
    auto new_prefs_instance = PreferencesManager::getInstance(TEST_PREFS_NAME);
    
    // 3. 从新实例中读取数据，验证是否已持久化
    EXPECT_EQ(new_prefs_instance->getString("session_token", ""), "abc-123");
}

TEST_F(PreferencesTest, MultiThreadedReadWrite) {
    std::atomic<bool> stop_flag = false;
    std::atomic<int> write_count = 0;
    
    // 写入线程
    std::thread writer([&]() {
        int i = 0;
        while (!stop_flag) {
            prefs->edit()->putInt("counter", i++).commit();
            write_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // 读取线程
    std::thread reader([&]() {
        while (!stop_flag) {
            int val = prefs->getInt("counter", -1);
            // 我们只关心它是否能无错运行
            ASSERT_GE(val, -1);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    });

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop_flag = true;

    writer.join();
    reader.join();

    // 最后的断言只是为了确保测试确实执行了写入操作
    EXPECT_GT(write_count, 10);
    std::cout << "Multi-threaded test completed with " << write_count << " writes." << std::endl;
}