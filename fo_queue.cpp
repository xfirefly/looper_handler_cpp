#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include <folly/ProducerConsumerQueue.h>

int main() {
    const size_t queueSize = 1024;
    folly::ProducerConsumerQueue<int> queue(queueSize);

    const int total_items = 1'000'000;
    std::atomic<int> items_consumed_count = 0;
    std::atomic<bool> producer_is_done = false;

    std::cout << "Queue created with size " << queueSize << ", ready to process " << total_items << " items.\n";
    auto start_time = std::chrono::high_resolution_clock::now();

    // Producer thread (remains the same)
    std::thread producer([&]() {
        for (int i = 0; i < total_items; ++i) {
            while (!queue.write(i)) {
                std::this_thread::yield();
            }
        }
        std::cout << "[Producer] All data has been written.\n";
        producer_is_done.store(true, std::memory_order_release); 
    });

    // Consumer threads
    std::vector<std::thread> consumers;
    int num_consumers = 2; 
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&, consumer_id = i + 1]() {
            int value;
            
            // FIX: Implement robust, race-free termination logic.
            while (true) {
                // First, try to read from the queue.
                if (queue.read(value)) {
                    items_consumed_count++;
                } else {
                    // If the queue is empty, check if the producer is finished.
                    if (producer_is_done.load(std::memory_order_acquire)) {
                        // The producer is done, but there might be a final item in the queue
                        // that we missed. We must check one last time before exiting.
                        if (!queue.read(value)) {
                            // The producer is done AND the queue is now confirmed to be empty.
                            // It is now safe to exit.
                            break;
                        } else {
                            // We found a lingering item! Process it.
                            items_consumed_count++;
                        }
                    } else {
                        // The producer is still running; the queue is just temporarily empty.
                        std::this_thread::yield();
                    }
                }
            }
            std::cout << "[Consumer " << consumer_id << "] Work completed.\n";
        });
    }

    // Wait for all threads to finish
    producer.join();
    for (auto& consumer_thread : consumers) {
        consumer_thread.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end_time - start_time;

    std::cout << "\nAll work completed.\n";
    std::cout << "Total items consumed: " << items_consumed_count.load() << "\n";
    std::cout << "Total time taken: " << duration.count() << " milliseconds.\n";

    return 0;
}