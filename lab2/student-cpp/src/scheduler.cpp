// Этап 1–2: реализация Scheduler, point() и await().
// Сейчас все методы бросают исключение «TODO»: заполните их по заданию этапов 1 и 2.
// Деструктор должен дождаться (join) всех запущенных потоков, в том числе после deadlock.
#include "scheduler.hpp"
#include "sched_mutex.hpp"

#include <algorithm>
#include <stdexcept>

namespace sched {

thread_local Scheduler* Scheduler::current_ = nullptr;
thread_local int Scheduler::currentId_ = -1;

Scheduler::Scheduler() {}

Scheduler::~Scheduler() {
    stop();
    join();
}

void Scheduler::stop() {
    std::lock_guard lock(m_);
    stopped_ = true;
    for (auto& [id, w] : workers_) { w.cv_.notify_one(); }
}

void Scheduler::join() {
    for (auto& [id, w] : workers_) {
        if (w.thread_.joinable()) w.thread_.join();
    }
}

void Scheduler::spawn(std::function<void()> fn) {
    workers_.try_emplace(nextId_++, std::move(fn));
}

void Scheduler::run(Strategy& s) {
    try {
        for (auto& [id, w] : workers_) {
            std::unique_lock lock(m_);
            w.thread_ = std::thread([this, id] {
                current_ = this;
                currentId_ = id;

                try {
                    workers_.at(id).body_();
                } catch (const Stop&) {
                } catch (...) {
                    std::lock_guard lock(m_);
                    workers_.at(id).error_ = std::current_exception();
                }

                {
                    std::lock_guard lock(m_);
                    workers_.at(id).state_ = 2;
                }
                cv_.notify_one();
            });

            cv_.wait(lock, [&] { return w.state_ != -1; });
        }

        int last = -1;
        while (true) {
            std::unique_lock lock(m_);
            std::vector<int> enabled;
            bool alive = false;

            for (auto& [idx, w] : workers_) {
                if (w.state_ != 2) alive = true;
                if (w.state_ == 3 && w.cond_ && w.cond_()) {
                    w.state_ = 0;
                    w.cond_ = {};
                }
                if (w.state_ == 0 || (w.state_ == 4 && w.lock_->owner_ == -1)) enabled.push_back(idx);
            }

            if (enabled.empty()) {
                if (alive) {
                    deadlock = "held={";
                    bool first = true;
                    for (sched_mutex* lock : locks_) {
                        if (lock->owner_ == -1) continue;
                        if (!first) deadlock += ", ";
                        deadlock += lock->name + ":" + char('A' + lock->owner_);
                        first = false;
                    }
                    deadlock += "}";
                    stopped_ = true;
                    for (auto& [id, w] : workers_) w.cv_.notify_one();
                }
                break;
            }

            int pick = s.pick(enabled, last);
            if (std::find(enabled.begin(), enabled.end(), pick) == enabled.end())
                throw std::logic_error("strategy picked disabled worker");

            Worker& w = workers_.at(pick);
            trace_.push_back(char('A' + pick));
            w.state_ = 1;
            w.cv_.notify_one();
            cv_.wait(lock, [&] { return w.state_ != 1; });
            last = pick;
        }
    } catch (...) {
        stop();
        join();
        throw;
    }

    join();
    for (auto& [id, w] : workers_)
        if (w.error_) std::rethrow_exception(w.error_);
}

void Scheduler::step() {
    std::unique_lock lock(m_);
    Worker& w = workers_.at(currentId_);

    if (w.touched_) {
        w.state_ = 0;
        cv_.notify_one();
        w.cv_.wait(lock, [&] { return w.state_ == 1 || stopped_; });
        if (stopped_) throw Stop{};
        w.touched_ = false;
    }

    w.touched_ = true;
}

void Scheduler::mark(const std::string& text) {
    std::lock_guard lock(m_);
    Worker& w = workers_.at(currentId_);
    if (logging) log.push_back(std::string(1, char('A' + currentId_)) + "  " + text);
    w.touched_ = true;
}

void Scheduler::wait(std::function<bool()> cond) {
    std::unique_lock lock(m_);
    Worker& w = workers_.at(currentId_);
    bool done = cond();

    if (w.touched_ || !done) {
        if (done) {
            w.state_ = 0;
        } else {
            w.state_ = 3;
            w.cond_ = std::move(cond);
        }

        cv_.notify_one();
        w.cv_.wait(lock, [&] { return w.state_ == 1 || stopped_; });
        if (stopped_) throw Stop{};
        w.touched_ = false;
    }

    if (logging) log.push_back(std::string(1, char('A' + currentId_)) + "  await done");
    w.touched_ = true;
}

void Scheduler::addLock(sched_mutex* lock) {
    std::lock_guard guard(m_);
    locks_.push_back(lock);
}

void Scheduler::lock(sched_mutex& lock) {
    std::unique_lock guard(m_);
    Worker& w = workers_.at(currentId_);
    w.lock_ = &lock;

    if (w.touched_ || lock.owner_ != -1) {
        w.state_ = 4;
        cv_.notify_one();
        w.cv_.wait(guard, [&] { return w.state_ == 1 || stopped_; });
        if (stopped_) throw Stop{};
        w.touched_ = false;
    }

    if (lock.owner_ != -1) throw std::logic_error("lock is not free");
    lock.owner_ = currentId_;
    w.lock_ = nullptr;
    if (logging) log.push_back(std::string(1, char('A' + currentId_)) + "  lock " + lock.name);
    w.touched_ = true;
}

void Scheduler::unlock(sched_mutex& lock) {
    std::unique_lock guard(m_);
    if (stopped_) return;

    Worker& w = workers_.at(currentId_);
    if (lock.owner_ != currentId_) throw std::logic_error("unlock by non-owner");

    if (w.touched_) {
        w.state_ = 0;
        cv_.notify_one();
        w.cv_.wait(guard, [&] { return w.state_ == 1 || stopped_; });
        if (stopped_) return;
        w.touched_ = false;
    }

    lock.owner_ = -1;
    if (logging) log.push_back(std::string(1, char('A' + currentId_)) + "  unlock " + lock.name);
    w.touched_ = true;
}

std::string Scheduler::trace_string() const {
    return trace_;
}

void point() {
    if (!Scheduler::current_) throw std::logic_error("point outside scheduler thread");
    Scheduler::current_->mark("window");
}

void await(std::function<bool()> cond) {
    if (!Scheduler::current_) throw std::logic_error("await outside scheduler thread");
    Scheduler::current_->wait(std::move(cond));
}

}  // namespace sched
