#pragma once

#include <cassert>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>

namespace core {
 
class BlockingQueueClosed : public std::exception
{
public:
	const char * what() const noexcept override
	{
		return "BlockingQueueClosed";
	}
};

template <typename T>
class BlockingQueue
{
	std::deque<T> queue;
	std::condition_variable cv;
	std::mutex mutex;
	bool closed = false;

public:
	void push(T && item)
	{
		std::lock_guard lock(mutex);

		queue.push_back(std::move(item));
		cv.notify_one();
	}

	void push(const T& item)
	{
		T item_copy = item; // 在锁外创建拷贝
		std::lock_guard lock(mutex);
		if (closed) {
			// 可以在此抛出异常，防止向已关闭的队列添加元素
			throw BlockingQueueClosed{};
		}
		queue.push_back(std::move(item_copy)); // 将拷贝移入队列
		cv.notify_one();
	}

	template <typename Pred>
	std::optional<T> pop_if(Pred&& pred)
	{
		std::unique_lock lock(mutex);

		while (true) // 使用循环来处理假唤醒和条件不满足的情况
		{
			cv.wait(lock, [&]() { return !queue.empty() || closed; });

			if (closed && queue.empty())
				return {}; // 返回空的 optional 而不是抛出异常，让调用者决定如何处理

			assert(!queue.empty());

			if (pred(queue.front()))
			{
				T item = std::move(queue.front());
				queue.pop_front();
				return item;
			}

			// 如果条件不满足，但队列已关闭且没有更多元素了，也应该退出
			if (closed) {
				return {};
			}
			// 如果条件不满足，循环会继续，线程会重新进入等待状态
		}
	}

	T pop()
	{
		std::unique_lock lock(mutex);

		cv.wait(lock, [&]() { return !queue.empty() || closed; });

		if (closed)
			throw BlockingQueueClosed{};

		assert(!queue.empty());
		T item = std::move(queue.front());
		queue.pop_front();

		return item;
	}

	template <typename Pred>
	void drop_until(Pred && pred)
	{
		std::unique_lock lock(mutex);

		while (!queue.empty() && !pred(queue.front()))
			queue.pop_front();
	}

	T peek()
	{
		std::unique_lock lock(mutex);
		cv.wait(lock, [&]() { return !queue.empty() || closed; });

		if (closed && queue.empty()) // 在返回前再次检查，以防 close() 后队列被清空
			throw BlockingQueueClosed{};

		assert(!queue.empty());
		return queue.front(); // 返回一个拷贝
	}

	void close()
	{
		std::lock_guard lock(mutex);
		closed = true;
		cv.notify_all();
	}
};
 
}