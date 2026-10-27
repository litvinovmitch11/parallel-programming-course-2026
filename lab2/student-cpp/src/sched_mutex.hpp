#pragma once
// Этап 2: лок, о котором знает планировщик. Поток, ждущий занятый лок, не попадает в enabled;
// обычный std::mutex здесь нельзя — поток уснёт с ходом в руках и всё зависнет.
// Лог: "lock <имя>", "unlock <имя>". Планировщик должен знать все локи, чтобы печатать held={a:A, b:B}.
#include <stdexcept>
#include <string>

#include "scheduler.hpp"

namespace sched {

class sched_mutex {
public:
    // Создаётся в setup() тестовой программы; зарегистрируйте лок в планировщике s.
    sched_mutex(Scheduler& s, std::string name) : name(std::move(name)), scheduler_(&s) { s.addLock(this); }
    sched_mutex(const sched_mutex&) = delete;

    // Пауза; если лок занят — ждать, не участвуя в выборе; затем стать владельцем.
    void lock() { scheduler_->lock(*this); }
    // Пауза; освободить лок, ждавшие потоки снова могут быть выбраны.
    void unlock() { scheduler_->unlock(*this); }

    const std::string name;

private:
    Scheduler* scheduler_;
    int owner_ = -1;

    friend class Scheduler;
};

// RAII: lock сейчас, unlock на закрывающей скобке. Выдан готовым.
class sched_guard {
public:
    explicit sched_guard(sched_mutex& m) : m_(m) { m_.lock(); }
    ~sched_guard() noexcept(false) { m_.unlock(); }
    sched_guard(const sched_guard&) = delete;

private:
    sched_mutex& m_;
};

}  // namespace sched
