#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace search {

/**
 * Класичний потокобезпечний пул потоків (Thread Pool)
 * Реалізовано відповідно до вимог курсової роботи без сторонніх бібліотек.
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t threads);
    ~ThreadPool();

    // Заборона копіювання
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Додавання завдання до черги виконання
    void enqueue(std::function<void()> task);

    // Блокуюче очікування виконання всіх завдань у черзі
    void wait_all();

    // Отримання кількості робочих потоків
    size_t get_thread_count() const { return threads_.size(); }

    // Отримання поточної довжини черги
    size_t get_queue_size() const;

private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;

    mutable std::mutex queue_mutex_;
    std::condition_variable cv_tasks_;
    std::condition_variable cv_done_;

    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
};

} // namespace search
