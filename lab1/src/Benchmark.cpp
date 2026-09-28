#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <latch>
#include <random>
#include <thread>
#include <vector>

#include "BaselineCollector.hpp"

// Один забег: T потоков крутят цикл ровно seconds секунд
template <class Collector>
long double run(Collector& collector, const std::vector<unsigned>& values, unsigned T,
                unsigned seconds) {
    // ожидание готовности потоков
    std::latch ready{T};
    // общий стартовый выстрел
    std::latch start{1};
    // флаг остановки
    std::atomic<bool> stop = false;
    // результаты потоков
    std::vector<uint64_t> ops(T);

    std::vector<std::thread> threads;
    threads.reserve(T);
    for (size_t k = 0; k < T; ++k) {
        threads.emplace_back([&, k] {
            // строго локальная переменная!
            uint64_t local_count = 0;
            // разносим точки старта по массиву значений
            size_t i = k * 1000;
            // локальное состояние потока подготовлено
            ready.count_down();
            // ждем команды "старт"
            start.wait();
            while (!stop) {
                // <-- ВЕСЬ ЗАМЕР ЗДЕСЬ
                collector.record(values[i]);
                local_count++;
                i++;
                if (i == values.size())
                    i = 0;
            }
            // сохраняем итог только при выходе
            ops[k] = local_count;
        });
    }
    // ожидаем подготовку потоков
    ready.wait();
    auto t0 = std::chrono::steady_clock::now();
    // погнали!
    start.count_down();
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    // стоп!
    stop = true;
    auto t1 = std::chrono::steady_clock::now();
    for (auto& t : threads) {
        t.join();
    }
    uint64_t total = 0;
    for (size_t i = 0; i < T; ++i) {
        total += ops[i];
    }
    auto duration = std::chrono::duration<long double>(t1 - t0).count();
    // реальное число оп/сек
    return static_cast<long double>(total) / duration;
}

// Замер одной точки графика (например, для T=4 потоков)
template <class Collector, unsigned N = 5, unsigned S = 5>
long double measurePoint(const std::vector<unsigned>& values, unsigned T) {
    Collector collector;
    // ПРОГРЕВ: 5 сек крутим вхолостую, результат выбрасываем
    run(collector, values, T, S);
    std::array<long double, N> results;
    // 5 честных забегов по 5 секунд
    for (size_t i = 0; i < N; ++i) {
        results[i] = run(collector, values, T, S);
    }
    std::cout << "snapshot.count = " << collector.snapshot().count << "\n";
    // возвращаем медиану
    long double median = 0;
    std::sort(results.begin(), results.end());
    if constexpr (N % 2 == 1) {
        median = results[N / 2];
    } else {
        median = (results[N / 2 - 1] + results[N / 2]) / 2;
    }
    return median;
}

std::vector<unsigned> gen_data(size_t N, uint32_t seed = 42) {
    const unsigned k_max = 1023;
    std::vector<double> weights(k_max);
    for (size_t k = 1; k <= k_max; ++k) {
        weights[k - 1] = 1.0 / std::pow(static_cast<double>(k), 1.15);
    }
    std::discrete_distribution<unsigned> dist(weights.begin(), weights.end());

    std::mt19937_64 rng(seed);
    std::vector<unsigned> values(N);
    for (size_t i = 0; i < N; ++i) {
        values[i] = dist(rng) + 1;
    }
    return values;
}

int main() {
    const size_t N = (1 << 20);
    std::vector<unsigned> values = gen_data(N);
    // unsigned T[] = {1, 2, 4, 8, 16};
    unsigned T[] = {1};
    for (auto t : T) {
        auto median = measurePoint<collector::BaselineCollector>(values, t);
        std::cout << "BaselineCollector\tT = " << t << "\tMedian = " << median / 1'000'000.0L
                  << " Mops/s\n";
    }
}
