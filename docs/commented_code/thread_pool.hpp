#pragma once

// Підключаємо стандартні заголовки C++20 для багатопотоковості без сторонніх бібліотек:
#include <vector>              // Контейнер для зберігання робочих потоків (std::thread)
#include <queue>               // Черга задач FIFO (перший прийшов — перший виконався)
#include <thread>              // Системні потоки ОС
#include <mutex>               // М'ютекс для взаємного виключення доступу до черги задач
#include <condition_variable>  // Умовні змінні для сну та пробудження потоків без busy-waiting
#include <functional>          // Обгортка std::function<void()> для передачі будь-якої лямбди/задачі
#include <atomic>              // Атомарні змінні (stop_, active_tasks_) без блокування на читання

namespace search {

/**
 * Власна реалізація потокобезпечного пулу потоків (Thread Pool).
 * Головна ідея для захисту: ми створюємо фіксовану кількість потоків один раз,
 * щоб не витрачати час ОС на створення/знищення потоку для кожного з 10 410 файлів.
 */
class ThreadPool {
public:
    // Конструктор: запускає `threads` фонових потоків-робітників (workers)
    explicit ThreadPool(size_t threads);

    // Деструктор: коректно зупиняє всі потоки і чекає їх завершення через join()
    ~ThreadPool();

    // Явно забороняємо копіювання, бо std::thread і std::mutex не можна копіювати
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Поставити нову задачу (лямбду) у чергу на виконання
    void enqueue(std::function<void()> task);

    // Заблокувати поточний потік (наприклад, HTTP-обробник /api/index),
    // поки не виконаються абсолютно всі поставлені задачі
    void wait_all();

    // Повертає кількість робочих потоків у пулі
    size_t get_thread_count() const { return threads_.size(); }

    // Повертає поточну довжину черги очікуючих задач (потокобезпечно)
    size_t get_queue_size() const;

private:
    std::vector<std::thread> threads_;          // Вектор робочих потоків
    std::queue<std::function<void()>> tasks_;   // Черга задач, захищена queue_mutex_

    mutable std::mutex queue_mutex_;            // Основний м'ютекс захисту черги tasks_
    std::condition_variable cv_tasks_;          // Сигналізує робітникам: «з'явилася нова задача або стоп»
    std::condition_variable cv_done_;           // Сигналізує очікуючому в wait_all(): «усі задачі виконано»

    std::atomic<bool> stop_{false};             // Прапор завершення роботи пулу (виставляється в деструкторі)
    std::atomic<size_t> active_tasks_{0};       // Лічильник задач: ті, що в черзі + ті, що виконуються зараз
};

} // namespace search
