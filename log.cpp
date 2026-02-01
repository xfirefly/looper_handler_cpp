
#include "log.h"
#include "platform.h"
#include "main/constants.h"
#include <iostream>
#include <filesystem>

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/rotating_file_sink.h"


#include "spdlog/sinks/base_sink.h"
#include <mutex>



// Custom sink that forwards logs to the Logger's callback
template<typename Mutex>
class CallbackSink : public spdlog::sinks::base_sink<Mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        auto& logger = LogInstance();
        auto cb = logger.GetCallback();
        if (cb) {
            // Use raw payload to avoid double formatting (timestamp, etc.) 
            // as DebugWindow adds its own prefix/timestamp.
            std::string logMsg(msg.payload.data(), msg.payload.size());
            cb(msg.level, logMsg);
        }
    }

    void flush_() override {}
};

Logger& LogInstance() {
	static Logger m_instance;
	return m_instance;
}
   
Logger::Logger() {
	//设置为异步日志
	//spdlog::set_async_mode(32768);  // 必须为 2 的幂	 

	auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	stdout_sink->set_level(spdlog::level::debug);

	auto log_path = GetLogPath();
	auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_path, 1024 * 100, 2);

    // Add Callback Sink
    auto cb_sink = std::make_shared<CallbackSink<std::mutex>>();
    cb_sink->set_level(spdlog::level::trace); // Capture all

	const spdlog::sinks_init_list sinks = { stdout_sink, rotating_sink, cb_sink };
  	nml_logger = std::make_shared<spdlog::logger>("both", sinks.begin(), sinks.end() );
	spdlog::register_logger(nml_logger);
	
	// 设置日志记录级别
#ifdef _DEBUG
	nml_logger->set_level(spdlog::level::trace);
#else
	nml_logger->set_level(spdlog::level::debug);
#endif

	//设置当出发 err 或更严重的错误时立刻刷新日志到  disk .
	nml_logger->flush_on(spdlog::level::warn);
	//spdlog::set_pattern("%Y-%m-%d %H:%M:%S [%l] [%t] - <%s>|<%#>|<%!>,%v");
	spdlog::set_pattern("%Y-%m-%d %H:%M:%S [%l] [%t] [%! :%#] %v");
    
    // Set pattern for sink too, or default
    // We rely on default pattern set by logger which propagates? 
    // Actually spdlog sets formatter on sinks when set_pattern is called on logger? 
    // Yes, sinking it triggers formatter.
    
	spdlog::flush_every(std::chrono::seconds(5));

	std::cout << " log path :" << log_path << std::endl;
}



std::string Logger::GetLogPath() {
	return (core::Platform::getAppDataPath() / APP_LOG_PATH).string();
}

Logger::~Logger() {
	spdlog::drop_all();
	spdlog::shutdown();
}

