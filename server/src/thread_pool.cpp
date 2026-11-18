#include "thread_pool.hpp"

namespace search {

ThreadPool::ThreadPool(size_t threads) {
    if (threads == 0) threads = 1;
    threads_.reserve(threads);

    for (size_t i = 0; i < threads; ++i) {
        threads_.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    cv_tasks_.wait(lock, [this]() {
                        return stop_.load() || !tasks_.empty();
                    });

                    if (stop_.load() && tasks_.empty()) {
                        return;
                    }

                    task = std::move(tasks_.front());
                    tasks_.pop();
                }

                // Виконання завдання поза критичною секцією
                task();

                // Синхронізоване зменшення лічильника та сповіщення при завершенні всіх задач
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    if (--active_tasks_ == 0 && tasks_.empty()) {
                        cv_done_.notify_all();
                    }
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_.store(true);
    }
    cv_tasks_.notify_all();
    for (std::thread& worker : threads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_.load()) return;
        tasks_.push(std::move(task));
        ++active_tasks_;
    }
    cv_tasks_.notify_one();
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    cv_done_.wait(lock, [this]() {
        return tasks_.empty() && active_tasks_.load() == 0;
    });
}

size_t ThreadPool::get_queue_size() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

} // namespace search
