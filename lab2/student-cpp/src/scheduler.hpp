#pragma once
// Этап 1: планировщик, который ведёт настоящие потоки строго по одному (передача хода через семафоры).
// Этап 2: обнаружение deadlock в главном цикле и корректное завершение застрявших потоков.
// Внутреннее устройство (поля, семафоры, вспомогательные функции для shared и sched_mutex) придумываете сами.
#include <condition_variable>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "strategy.hpp"

namespace sched {

template <class T>
class shared;
class sched_mutex;

class Scheduler {
public:
    Scheduler();
    ~Scheduler();
    // Зарегистрировать поток. Потоки получают буквы A, B, C… в порядке вызова spawn.
    void spawn(std::function<void()> body);
    // Запустить потоки и вести их по одному: на каждой паузе спросить strategy.pick(enabled, last).
    void run(Strategy& strategy);
    // Трасса прогона: буквы потоков, выбранных во всех решениях подряд, например "AAAABB".
    std::string trace_string() const;

    // Если true, каждая операция с общими данными добавляет строку в log. Выставляет runner.
    bool logging = false;
    // Лог прогона: строки "<поток>  <операция>", например "A  lock mu", "A  balance -> 100".
    std::vector<std::string> log;
    // Пустая строка, если deadlock не было; иначе "held={a:A, b:B}" (этап 2).
    std::string deadlock;

private:
    struct Worker {
        Worker(std::function<void()> body) : body_(std::move(body)) {}

        std::function<void()> body_;
        std::thread thread_;
        std::condition_variable cv_;
        std::function<bool()> cond_;
        std::exception_ptr error_;
        sched_mutex* lock_ = nullptr;
        int state_ = -1;
        bool touched_ = true;
    };

    struct Stop {};

    void step();
    void mark(const std::string& text);
    void wait(std::function<bool()> cond);
    void addLock(sched_mutex* lock);
    void lock(sched_mutex& lock);
    void unlock(sched_mutex& lock);
    void stop();
    void join();

    static thread_local Scheduler* current_;
    static thread_local int currentId_;

    std::mutex m_;
    std::condition_variable cv_;

    std::map<int, Worker> workers_;
    std::vector<sched_mutex*> locks_;
    int nextId_ = 0;
    std::string trace_;
    bool stopped_ = false;

    friend void point();
    friend void await(std::function<bool()> cond);
    friend class sched_mutex;
    template <class T>
    friend class shared;
};

// window() из тестовых программ: пауза перед следующей операцией с общими данными обязательна.
void point();
// Ждать, пока cond не станет true. До этого поток не попадает в enabled.
void await(std::function<bool()> cond);

}  // namespace sched
