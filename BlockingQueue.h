 
#pragma once

#include <cassert>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>

 
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

	void push(const T & item)
	{
		std::lock_guard lock(mutex);

		queue.push_back(item);
		cv.notify_one();
	}

	template <typename Pred>
	std::optional<T> pop_if(Pred && pred)
	{
		std::unique_lock lock(mutex);

		cv.wait(lock, [&]() { return !queue.empty() || closed; });

		if (closed)
			throw BlockingQueueClosed{};

		assert(!queue.empty());

		if (pred(queue.front()))
		{
			T item = std::move(queue.front());
			queue.pop_front();

			return item;
		}

		return {};
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

	T & peek()
	{
		std::unique_lock lock(mutex);

		cv.wait(lock, [&]() { return !queue.empty() || closed; });

		if (closed)
			throw BlockingQueueClosed{};

		assert(!queue.empty());
		return queue.front();
	}

	void close()
	{
		std::lock_guard lock(mutex);
		closed = true;
		cv.notify_all();
	}
};
 