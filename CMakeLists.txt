# CMake 最低版本要求 (建议 3.14 或更高，以更好地支持 vcpkg)
cmake_minimum_required(VERSION 3.14)

# 定义项目名称和语言
project(LooperHandlerProject LANGUAGES CXX)
 
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # 推荐关闭 CXX 扩展

# ------------------------------------------------------------------
# 寻找依赖包
# ------------------------------------------------------------------
# 寻找 Google Test。vcpkg 工具链会帮助 CMake 找到它。
# REQUIRED 确保如果找不到 GTest，配置会失败。
find_package(GTest REQUIRED)

# 寻找线程库 (代码中使用了 std::thread)
find_package(Threads REQUIRED)

# ------------------------------------------------------------------
# 定义您的 Looper/Handler 库
# ------------------------------------------------------------------
# 将您的核心代码编译成一个库 (这是一个好习惯)
add_library(looper_handler 
    HandlerThread.cpp
    HandlerThread.h
    LocalBroadcast.cpp
    LocalBroadcast.h
    looper_handler.cpp
    looper_handler.h
    WorkerThread.cpp
    WorkerThread.h    
    SharedPreferences.cpp
    SharedPreferences.h
)

# 链接线程库到您的核心库
# PUBLIC 表示任何链接 looper_handler 的目标也会自动链接 Threads
target_link_libraries(looper_handler PUBLIC Threads::Threads)


# --- 测试设置 ---
enable_testing()

# 包含 GoogleTest 以使用 gtest_add_tests
include(GoogleTest)

# --- 可执行文件和测试定义 ---


# 1. SharedPreferences 单元测试
add_executable(SharedPreferences_test SharedPreferences_test.cpp)
target_link_libraries(SharedPreferences_test PRIVATE looper_handler GTest::Main)
gtest_add_tests(TARGET SharedPreferences_test)

# 2. WorkerThread 单元测试
add_executable(WorkerThread_test WorkerThread_test.cpp)
target_link_libraries(WorkerThread_test PRIVATE looper_handler GTest::Main)
gtest_add_tests(TARGET WorkerThread_test)

# 3. HandlerThread 单元测试 (新增)
add_executable(HandlerThread_test HandlerThread_test.cpp)
target_link_libraries(HandlerThread_test PRIVATE looper_handler GTest::Main)
gtest_add_tests(TARGET HandlerThread_test)

# 4. SharedPreferences 示例程序
add_executable(SharedPreferences_sample SharedPreferences_sample.cpp)
target_link_libraries(SharedPreferences_sample PRIVATE looper_handler)

# 5. 主测试程序
add_executable(main_test main_test.cpp)
target_link_libraries(main_test PRIVATE looper_handler)

# 
# ctest -C Debug -V 

# 这些代码是模仿 Android sdk  里面的 java class   的 c++ 版本, 
# 检查其功能是否和 Android sdk 里面的版本一致, 缺少的功能请说明
# ; 以及是否有多余的代码, 如果有多余的代码请说明
# ; 以及是否有错误, 如果有错误请说明      
# ; 以及是否有不必要的代码, 如果有不必要的代码请说明
# ; 以及是否有不必要的注释, 如果有不必要的注释请说明
# ;检查 c++ 版本是否足够健壮, 可用于生产环境, 如果不够健壮请说明