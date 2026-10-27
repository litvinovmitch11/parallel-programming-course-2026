#pragma once
// Стратегии выбора следующего потока.
// FirstAlive — этап 1, Replay — этап 2, RandomPick — этап 3, Dfs — этап 4 (ограничение bound — этап 5).
// enabled — номера потоков, которые могут сделать шаг, по возрастанию; last — прошлый выбранный (-1 в начале).
#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "strategy.hpp"

namespace sched {

// Всегда первый поток из enabled.
struct FirstAlive : Strategy {
    int pick(const std::vector<int>& enabled, int) override { return enabled.front(); }
};

// Проигрывает трассу: на i-м решении выбирает поток trace[i] ('A' -> 0, 'B' -> 1).
struct Replay : Strategy {
    explicit Replay(std::string trace) : trace_(std::move(trace)) {}
    // Сбросить позицию в трассе перед прогоном.
    void begin_run() override { pos_ = 0; }
    int pick(const std::vector<int>& enabled, int) override {
        if (pos_ == trace_.size()) throw std::logic_error("replay trace ended early");
        int id = trace_[pos_++] - 'A';
        if (std::find(enabled.begin(), enabled.end(), id) == enabled.end())
            throw std::logic_error("replay picked disabled worker");
        return id;
    }

private:
    std::string trace_;
    std::size_t pos_ = 0;
};

// Случайный поток: enabled[rnd() % enabled.size()], rnd — std::mt19937(seed).
struct RandomPick : Strategy {
    explicit RandomPick(uint32_t seed) : rnd_(seed) {}
    int pick(const std::vector<int>& enabled, int) override { return enabled[rnd_() % enabled.size()]; }

private:
    std::mt19937 rnd_;
};

// Stateless DFS по дереву выборов: каждый прогон с нуля, повторяет префикс прошлого и берёт следующий вариант.
// bound — максимум вытеснений (этап 5); для dfs runner передаёт INT_MAX.
struct Dfs : Strategy {
    explicit Dfs(int bound) : bound_(bound) {}
    // Подготовиться к новому прогону (позиция в пути, счётчик вытеснений).
    void begin_run() override {
        pos_ = 0;
        preemptions_ = 0;
    }
    int pick(const std::vector<int>& enabled, int last) override {
        bool canContinue = std::find(enabled.begin(), enabled.end(), last) != enabled.end();

        if (pos_ == path_.size()) {
            std::vector<int> options;
            for (int id : enabled) {
                int next = preemptions_ + (canContinue && id != last);
                if (next <= bound_) options.push_back(id);
            }
            path_.push_back({std::move(options), 0});
        }

        Node& node = path_[pos_++];
        int id = node.enabled[node.pick];
        if (canContinue && id != last) preemptions_++;
        return id;
    }
    // Выбрать следующий путь; false — дерево обойдено.
    bool end_run() override {
        path_.resize(pos_);
        while (!path_.empty()) {
            Node& node = path_.back();
            if (++node.pick < node.enabled.size()) return true;
            path_.pop_back();
        }
        return false;
    }

private:
    struct Node {
        std::vector<int> enabled;
        std::size_t pick;
    };

    std::vector<Node> path_;
    std::size_t pos_ = 0;
    int bound_;
    int preemptions_ = 0;
};

}  // namespace sched
