#include "thread_pool.hpp"

namespace search {

ThreadPool::ThreadPool(size_t threads) {
    // Захист від передачі 0 потоків: мінімум 1 робочий потік має працювати
    if (threads == 0) threads = 1;
    threads_.reserve(threads);

    // Запускаємо N постійних потоків-робітників
    for (size_t i = 0; i < threads; ++i) {
        threads_.emplace_back([this]() {
            // Безкінечний цикл обробки задач (поки не спрацює stop_)
            while (true) {
                std::function<void()> task;

                // --- КРИТИЧНА СЕКЦІЯ №1: забираємо задачу з черги ---
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);

                    // Потік засинає на cv_tasks_ і не витрачає CPU,
                    // поки або пул не зупинять, або в черзі не з'явиться задача.
                    // Лямбда-предикат захищає від помилкових пробуджень (spurious wakeups).
                    cv_tasks_.wait(lock, [this]() {
                        return stop_.load() || !tasks_.empty();
                    });

                    // Якщо пул зупиняється і всі задачі з черги вже розібрані — виходимо з потоку
                    if (stop_.load() && tasks_.empty()) {
                        return;
                    }

                    // Витягуємо задачу через std::move без зайвого копіювання
                    task = std::move(tasks_.front());
                    tasks_.pop();
                } // <-- Тут lock виходить з області видимості і м'ютекс ВІДРАЗУ звільняється!

                // --- ВИКОНАННЯ ЗАДАЧІ ПОЗА КРИТИЧНОЮ СЕКЦІЄЮ ---
                // Дуже важливо: інші потоки не чекають, поки поточний потік читає файл і рахує токени!
                task();

                // --- КРИТИЧНА СЕКЦІЯ №2: позначаємо задачу виконаною ---
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    // Зменшуємо active_tasks_. Якщо це була остання активна задача і черга порожня —
                    // будимо потік, який викликав wait_all()
                    if (--active_tasks_ == 0 && tasks_.empty()) {
                        cv_done_.notify_all();
                    }
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    // Безпечне завершення: ставимо прапор stop_ під м'ютексом, щоб не втратити пробудження
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_.store(true);
    }
    // Будимо всі сплячі потоки, щоб вони побачили stop_ == true і завершилися
    cv_tasks_.notify_all();

    // Обов'язково робимо join() для кожного потоку (RAII гарантує відсутність витоків потоків)
    for (std::thread& worker : threads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_.load()) return; // Якщо пул уже зупиняється, нові задачі не приймаємо
        tasks_.push(std::move(task));
        // Збільшуємо лічильник активних задач ДО звільнення м'ютекса,
        // щоб wait_all() не прокинувся передчасно
        ++active_tasks_;
    }
    // Будимо один вільний потік-робітник
    cv_tasks_.notify_one();
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    // Чекаємо, поки одночасно спорожніє черга tasks_ і всі робітники закінчать виконання (active_tasks_ == 0)
    cv_done_.wait(lock, [this]() {
        return tasks_.empty() && active_tasks_.load() == 0;
    });
}

size_t ThreadPool::get_queue_size() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

} // namespace search
