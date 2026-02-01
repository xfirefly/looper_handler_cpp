#ifndef _LOG_0B0512CC_B1CC_4483_AF05_1914E7F7D4DA_
#define _LOG_0B0512CC_B1CC_4483_AF05_1914E7F7D4DA_

#include <string>

#ifndef SPDLOG_TRACE_ON
#define SPDLOG_TRACE_ON
#endif

#ifndef SPDLOG_DEBUG_ON
#define SPDLOG_DEBUG_ON
#endif

#ifdef _WIN32
#define __FILENAME__                                                           \
  (strrchr(__FILE__, '\\') ? (strrchr(__FILE__, '\\') + 1) : __FILE__)
#else
#define __FILENAME__                                                           \
  (strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/') + 1) : __FILE__)
#endif

#include "spdlog/spdlog.h"
#include <memory>
#include <mutex>

/**
 * @class Logger
 * @brief 基于 spdlog 的日志封装类。
 *
 * 通常不需要直接使用此类，而是使用提供的宏：
 * LogTrace, LogDebug, LogInfo, LogWarn, LogErr, LogCritical.
 *
 * <h2>使用示例</h2>
 * @code
 * LogInfo("Application started");
 * LogDebug("Value is: {}", 42);
 * LogErr("An error occurred: {}", "Connection failed");
 * @endcode
 */
class Logger {
public:
  auto GetLogger() { return nml_logger; }

  Logger();
  ~Logger();
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  // Callback function type: (level, msg)
  using LogCallback = std::function<void(int, const std::string &)>;
  
  void SetSinkCallback(LogCallback cb) { 
      std::lock_guard<std::mutex> lock(m_mutex);
      m_callback = cb; 
  }
  
  LogCallback GetCallback() { 
      std::lock_guard<std::mutex> lock(m_mutex);
      return m_callback; 
  }

  std::string GetLogPath();

private:
  std::shared_ptr<spdlog::logger> nml_logger;
  LogCallback m_callback;
  std::mutex m_mutex;
};

Logger &LogInstance();

#define SPDLOG_LOGGER_CALL_(level, ...)                                        \
  LogInstance().GetLogger()->log(                                              \
      spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, level,          \
      __VA_ARGS__)
#define LogTrace(...) SPDLOG_LOGGER_CALL_(spdlog::level::trace, __VA_ARGS__)
#define LogDebug(...) SPDLOG_LOGGER_CALL_(spdlog::level::debug, __VA_ARGS__)
#define LogInfo(...) SPDLOG_LOGGER_CALL_(spdlog::level::info, __VA_ARGS__)
#define LogWarn(...) SPDLOG_LOGGER_CALL_(spdlog::level::warn, __VA_ARGS__)
#define LogErr(...) SPDLOG_LOGGER_CALL_(spdlog::level::err, __VA_ARGS__)
#define LogCritical(...)                                                       \
  SPDLOG_LOGGER_CALL_(spdlog::level::critical, __VA_ARGS__)
#define LogCriticalIf(b, ...)                                                  \
  do {                                                                         \
    if ((b)) {                                                                 \
      SPDLOG_LOGGER_CALL_(spdlog::level::critical, __VA_ARGS__);               \
    }                                                                          \
  } while (0)

#ifdef WIN32
#define errcode WSAGetLastError()
#endif

#endif //_LOG_0B0512CC_B1CC_4483_AF05_1914E7F7D4DA_
