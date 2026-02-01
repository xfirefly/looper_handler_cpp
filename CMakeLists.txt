# CMake 最低版本要求 (建议 3.20 或更高，以更好地支持 vcpkg)
cmake_minimum_required(VERSION 3.20)

# 定义项目名称和语言
project(LooperHandlerProject LANGUAGES CXX C)
 
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF) # 推荐关闭 CXX 扩展

# 为所有 C 目标设置 C11 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# ------------------------------------------------------------------
# 寻找依赖包
# ------------------------------------------------------------------
# 寻找 Google Test。vcpkg 工具链会帮助 CMake 找到它。
# REQUIRED 确保如果找不到 GTest，配置会失败。
find_package(GTest REQUIRED)

# 寻找线程库 (代码中使用了 std::thread)
find_package(Threads REQUIRED)

find_package(folly REQUIRED)

# ------------------------------------------------------------------
# 定义您的 Looper/Handler 库
# ------------------------------------------------------------------
# 将您的核心代码编译成一个库 (这是一个好习惯)
add_library(looper_handler 
    ringbuffer.c
    audiobuf.c
    HandlerThread.cpp
    HandlerThread.h
    LocalBroadcast.cpp
    LocalBroadcast.h
    looper_handler.cpp
    looper_handler.h
    WorkerThread.cpp
    WorkerThread.h    
    Preferences.cpp
    Preferences.h

)

# Set C standard to C11 for the target
# set_target_properties(looper_handler
#     PROPERTIES
#     C_STANDARD 11
#     C_STANDARD_REQUIRED ON
#     C_EXTENSIONS OFF
# )


# 链接线程库到您的核心库
# PUBLIC 表示任何链接 looper_handler 的目标也会自动链接 Threads
target_link_libraries(looper_handler PUBLIC Threads::Threads)

find_package(PThreads4W REQUIRED)
find_package(asio CONFIG REQUIRED) 
find_package(Boost REQUIRED COMPONENTS asio) 
find_package(Boost REQUIRED COMPONENTS beast)
find_package(OpenSSL REQUIRED)

# --- 测试设置 ---
enable_testing()

# 包含 GoogleTest 以使用 gtest_add_tests
include(GoogleTest)

# --- 可执行文件和测试定义 ---

add_executable(beast_coro beast_coro.cpp)
target_link_libraries(beast_coro PRIVATE 
    Boost::system         # Asio often needs this
    OpenSSL::SSL          # The SSL library
    OpenSSL::Crypto       # The Crypto library
    Boost::beast 
    Boost::asio
)
target_compile_options(beast_coro PRIVATE "/bigobj")


add_executable(LocalBroadcastManager_test LocalBroadcastManager_test.cpp) 
target_link_libraries(LocalBroadcastManager_test PRIVATE looper_handler GTest::Main)
gtest_add_tests(TARGET LocalBroadcastManager_test)

# 1. Preferences 单元测试
# add_executable(Preferences_test Preferences_test.cpp)
# target_link_libraries(Preferences_test PRIVATE looper_handler GTest::Main)
# gtest_add_tests(TARGET Preferences_test)

# 2. WorkerThread 单元测试
add_executable(WorkerThread_test WorkerThread_test.cpp)
target_link_libraries(WorkerThread_test PRIVATE looper_handler GTest::Main)
gtest_add_tests(TARGET WorkerThread_test)

# 3. HandlerThread 单元测试 (新增)
add_executable(HandlerThread_test HandlerThread_test.cpp)
target_link_libraries(HandlerThread_test PRIVATE looper_handler GTest::Main)
gtest_add_tests(TARGET HandlerThread_test)

# 4. Preferences 示例程序
add_executable(Preferences_sample Preferences_sample.cpp)
target_link_libraries(Preferences_sample PRIVATE looper_handler)

# 5. 主测试程序
add_executable(main_test main_test.cpp)
target_link_libraries(main_test PRIVATE looper_handler)

# 6. BlockingQueue 单元测试 (新增)
add_executable(BlockingQueue_test BlockingQueue_test.cpp)
target_link_libraries(BlockingQueue_test PRIVATE looper_handler GTest::Main)
gtest_add_tests(TARGET BlockingQueue_test)


# 7. Handler 和 Looper 单元测试
add_executable(looper_handler_test looper_handler_test.cpp)
target_link_libraries(looper_handler_test PRIVATE looper_handler GTest::Main)
gtest_add_tests(TARGET looper_handler_test)

add_executable(ringbuffer_demo ringbuffer_demo.cpp)
target_link_libraries(ringbuffer_demo PRIVATE looper_handler)
 
# 8. RingBuffer 单元测试
add_executable(ringbuffer_test ringbuffer_test.cpp)
target_link_libraries(ringbuffer_test PRIVATE looper_handler GTest::Main)
gtest_add_tests(TARGET ringbuffer_test)

 
# 9. CircularBuffer 单元测试
add_executable(circular_buffer_test circular_buffer_test.cpp)
target_link_libraries(circular_buffer_test PRIVATE GTest::Main)
gtest_add_tests(TARGET circular_buffer_test)

add_executable(channel channel.cpp)
target_link_libraries(channel PRIVATE Boost::asio)
target_compile_definitions(channel PRIVATE -D_WIN32_WINNT=0x0A00)


 
add_executable(worker_thread worker_thread.cpp)
target_link_libraries(worker_thread PRIVATE Boost::asio)
target_compile_definitions(worker_thread PRIVATE -D_WIN32_WINNT=0x0A00)

add_executable(tcp_client tcp_client.cpp)
target_link_libraries(tcp_client PRIVATE Boost::asio)
target_compile_definitions(tcp_client PRIVATE -D_WIN32_WINNT=0x0A00)


add_executable(echo_server echo_server.cpp)
target_link_libraries(echo_server PRIVATE Boost::asio)
target_compile_definitions(echo_server PRIVATE -D_WIN32_WINNT=0x0A00)
 

# Debouncer_test.cpp
add_executable(Debouncer_test Debouncer_test.cpp)
target_link_libraries(Debouncer_test PRIVATE looper_handler)
target_compile_definitions(Debouncer_test PRIVATE -D_WIN32_WINNT=0x0A00) 


# 11. audiobuf 单元测试 (C11版本)
add_executable(audiobuf_test_c11 audiobuf_test_c11.c)
target_link_libraries(audiobuf_test_c11 PRIVATE looper_handler PThreads4W::PThreads4W)
# 由于这不是gtest，我们不使用 gtest_add_tests
# 我们可以通过 `add_test` 命令将其添加到 ctest 测试集中
add_test(NAME AudioBufC11Test COMMAND audiobuf_test_c11)
 
# add_executable(fo_coro fo_coro.cpp)
# target_link_libraries(fo_coro PRIVATE Folly::folly Folly::folly_deps Folly::follybenchmark Folly::folly_test_util)
# target_compile_features(fo_coro PRIVATE cxx_std_20)

# add_executable(fo_main fo_main.cpp)
# target_link_libraries(fo_main PRIVATE Folly::folly Folly::folly_deps Folly::follybenchmark Folly::folly_test_util)
# target_compile_features(fo_main PRIVATE cxx_std_20)
 
# add_executable(fo_queue fo_queue.cpp)
# target_link_libraries(fo_queue PRIVATE Folly::folly Folly::folly_deps Folly::follybenchmark Folly::folly_test_util)
# target_compile_features(fo_queue PRIVATE cxx_std_20)


# For MSVC, enable C11 atomics support needed by ringbuffer.c
if(MSVC)
  target_compile_options(looper_handler PUBLIC "/experimental:c11atomics")
  target_compile_options(audiobuf_test_c11 PUBLIC "/experimental:c11atomics")

  #target_compile_options(fo_coro PUBLIC "/await")
  #target_compile_options(fo_main PUBLIC "/std:c++20")
endif()

 
#[=[

# 运行测试
ctest -C Debug -V 

这些代码是模仿 Android sdk  里面的 java class   的 c++ 版本, 
仔细检查其功能是否和 Android sdk 里面的版本一致, 缺少的功能请说明
; 以及是否有多余的代码, 如果有多余的代码请说明
; 以及是否有错误, 如果有错误请说明      
; 以及是否有不必要的代码, 如果有不必要的代码请说明
; 以及是否有不必要的注释, 如果有不必要的注释请说明
;检查 c++ 版本是否足够健壮, 可用于生产环境, 如果不够健壮请说明

 
为 CircularBuffer 类编写一套全面、健壮的单元测试代码。 覆盖所有公开函数和各种边界情况，确保其在生产环境中的稳定性和可靠性
 
为  Preferences 类生成一份详尽的 示例代码。这份代码将作为一份清晰的开发文档，覆盖所有公开函数和各种可能的使用场景, 供程序员参考

]=]

