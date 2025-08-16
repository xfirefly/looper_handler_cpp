//          Copyright Jean Pierre Cimalando 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// g++ -O2 -o test_case test_case.cc -ljack -lpthread

#include "ringbuffer.h"
#include <memory>
#include <thread>
#include <stdio.h>
#include <stdlib.h>

ringbuffer_t *ring_buffer;
size_t capacity = 1024;
size_t message_count = 100;
size_t message_size = 100;

void thread1()
{
  ringbuffer_t *rb = ::ring_buffer;
  const size_t message_size = ::message_size;
  const size_t message_count = ::message_count;
  std::unique_ptr<uint8_t[]> message_buffer(new uint8_t[message_size]);

  for (size_t i = 0; i < message_count;)
  {
    *(size_t *)message_buffer.get() = i;

    size_t can_write = ringbuffer_write_space(rb);
    size_t count = 0;
    if (can_write >= message_size)
    {
      count = ringbuffer_put(rb, (const char *)message_buffer.get(), message_size);
    }
    if (count == message_size)
      ++i;
    else
      std::this_thread::sleep_for(std::chrono::milliseconds(1)); // sched_yield();
  }
}

void thread2()
{
  ringbuffer_t *rb = ::ring_buffer;
  const size_t message_size = ::message_size;
  const size_t message_count = ::message_count;
  std::unique_ptr<uint8_t[]> message_buffer(new uint8_t[message_size]);

  for (size_t i = 0; i < message_count;)
  { 
    size_t can_read = ringbuffer_read_space(rb);
    size_t count = 0;
    if (can_read >= message_size)
    {
      count = ringbuffer_get(rb, (char *)message_buffer.get(), message_size);
    }

    if (count == message_size)
    {
      size_t msg = *(size_t *)message_buffer.get();
      if (msg != i)
      {
        printf("message (%zu) != expected (%zu)\n", msg, i);
        exit(1);
      }
      ++i;
    }
  }
}

int main()
{
  size_t ntimes = 1000000;
  for (size_t i = 0; i < ntimes; ++i)
  {
    printf("---------------- %4zu/%4zu ----------------\n", i + 1, ntimes);
    ring_buffer = ringbuffer_create(::capacity);
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
    ringbuffer_destroy(ring_buffer);
    ring_buffer = nullptr;
     
  }
  printf("-------------------------------------------\n");
  printf("success!\n");

  return 0;
}