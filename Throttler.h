#pragma once

#include <thread>
#include <future>
#include <iostream>
#include <string>
#include <chrono>
#include <mutex>
#include <functional>
 

namespace core {


	// ===================================================================
	// 1. 通用的 Throttler 类
	// ===================================================================
	template<typename... Args>
	class Throttler {
	public:
		// 构造函数：接受一个可调用对象和节流的时间间隔
		Throttler(std::function<void(Args...)> func, std::chrono::milliseconds interval)
			: func_(func), interval_(interval) {
			// 为了让第一次调用能立即执行，我们将 last_execution_ 初始化为一个“足够早”的时间点
			last_execution_ = std::chrono::steady_clock::now() - interval_;
		}

		// 调用函数：这是事件触发时需要调用的接口
		void operator()(Args... args) {
			// 使用互斥锁确保线程安全
			std::lock_guard<std::mutex> lock(mtx_);
			auto now = std::chrono::steady_clock::now();

			// 检查当前时间距离上次成功执行的时间是否已经超过了设定的间隔
			if (now - last_execution_ >= interval_) {
				// 如果超过了间隔，就更新执行时间点...
				last_execution_ = now;
				// ...并立即执行函数
				// 注意：这个简单版本的节流器是在调用者的线程上同步执行 func_ 的。
				func_(std::forward<Args>(args)...);
			}
			// 如果时间间隔未到，则本次调用被“节流”——即被忽略。
		}

	private:
		std::function<void(Args...)> func_;                       // 要执行的函数
		const std::chrono::milliseconds interval_;                // 节流间隔
		std::chrono::steady_clock::time_point last_execution_;    // 上次执行的时间点
		std::mutex mtx_;                                          // 用于保护 last_execution_
	};

}