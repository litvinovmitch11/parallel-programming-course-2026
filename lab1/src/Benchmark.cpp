#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <latch>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "BaselineCollector.hpp"
#include "DoubleBufferCollector.hpp"
#include "LockStripingCollector.hpp"
#include "MutexCollector.hpp"
#include "ThreadLocalCollector.hpp"

constexpr int collector_name_width = 30;

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

void write_data_histogram(const std::vector<unsigned>& values, std::ostream& out) {
    std::array<uint64_t, 1024> frequencies{};
    for (unsigned value : values) {
        frequencies[value]++;
    }

    out << "value,count\n";
    for (size_t value = 1; value < frequencies.size(); ++value) {
        out << value << ',' << frequencies[value] << '\n';
    }
}

template <class Collector>
void run_with_data(const std::vector<unsigned>& values, const std::vector<unsigned>& T,
                   const std::string& collector_name, std::ostream& results) {
    for (auto t : T) {
        auto median = measurePoint<Collector>(values, t);
        std::cout << std::left << std::setw(collector_name_width) << collector_name << std::right
                  << "T = " << std::setw(2) << t << "  Median = " << std::fixed
                  << std::setprecision(3) << std::setw(10) << median / 1'000'000.0L << " Mops/s\n";
        results << collector_name << ',' << t << ',' << std::fixed << std::setprecision(6)
                << median / 1'000'000.0L << '\n';
        results.flush();
    }
}

template <class Collector, unsigned N = 10'000>
void run_inconsistency_test(const std::vector<unsigned>& values, unsigned T,
                            const std::string& collector_name, std::ostream& results) {
    Collector collector;
    // ожидание готовности потоков
    std::latch ready{T};
    // общий стартовый выстрел
    std::latch start{1};
    // флаг остановки
    std::atomic<bool> stop = false;
    // результаты потоков
    std::vector<uint64_t> count(T);
    std::vector<std::thread> threads;
    threads.reserve(T);
    for (size_t k = 0; k < T; ++k) {
        threads.emplace_back([&, k] {
            uint64_t local_count = 0;
            size_t i = k * 1000;
            // локальное состояние потока подготовлено
            ready.count_down();
            // ждем команды "старт"
            start.wait();
            while (!stop) {
                collector.record(values[i]);
                local_count++;
                i++;
                if (i == values.size())
                    i = 0;
            }
            count[k] = local_count;
        });
    }
    // ожидаем подготовку потоков
    ready.wait();
    // погнали!
    start.count_down();
    uint64_t sum_greater_count = 0;
    uint64_t sum_equal_count = 0;
    uint64_t sum_less_count = 0;
    for (size_t i = 0; i < N; ++i) {
        auto snapshot = collector.snapshot();
        uint64_t sum_bucket = 0;
        for (auto v : snapshot.buckets)
            sum_bucket += v;
        if (sum_bucket > snapshot.count)
            sum_greater_count++;
        else if (sum_bucket == snapshot.count)
            sum_equal_count++;
        else
            sum_less_count++;
    }
    stop = true;
    for (auto& t : threads) {
        t.join();
    }
    const uint64_t broken_count = sum_less_count + sum_greater_count;
    const long double broken_percent = 100.0L * static_cast<long double>(broken_count) / N;
    std::cout << std::left << std::setw(collector_name_width) << collector_name << std::right
              << "T = " << std::setw(2) << T << "  Broken = " << std::fixed << std::setprecision(2)
              << std::setw(6) << broken_percent << "% (" << std::setw(5) << broken_count << "/" << N
              << ")"
              << "  Less = " << std::setw(5) << sum_less_count << "  Equal = " << std::setw(5)
              << sum_equal_count << "  Greater = " << std::setw(5) << sum_greater_count << "\n";

    uint64_t count_from_threads = 0;
    for (size_t i = 0; i < T; ++i) {
        count_from_threads += count[i];
    }
    const uint64_t final_count = collector.snapshot().count;
    const int64_t final_difference =
        static_cast<int64_t>(final_count) - static_cast<int64_t>(count_from_threads);
    std::cout << std::left << std::setw(collector_name_width) << collector_name << std::right
              << "T = " << std::setw(2) << T
              << "  Final count = " << (count_from_threads == final_count ? "OK" : "MISMATCH")
              << "  Threads = " << count_from_threads << "  Snapshot = " << final_count << "\n";
    results << collector_name << ',' << T << ',' << N << ',' << std::fixed << std::setprecision(6)
            << broken_percent << ',' << sum_less_count << ',' << sum_equal_count << ','
            << sum_greater_count << ',' << count_from_threads << ',' << final_count << ','
            << final_difference << '\n';
    results.flush();
}

int main() {
    const std::filesystem::path results_directory = "results";
    std::filesystem::create_directories(results_directory);
    std::ofstream benchmark_results{results_directory / "benchmark.csv"};
    std::ofstream stress_results{results_directory / "stress.csv"};
    std::ofstream histogram_results{results_directory / "input_histogram.csv"};
    if (!benchmark_results || !stress_results || !histogram_results) {
        std::cerr << "Cannot open result files in " << results_directory << '\n';
        return 1;
    }

    benchmark_results << "collector,threads,median_mops\n";
    stress_results << "collector,threads,snapshots,broken_percent,less,equal,greater,"
                      "thread_count,snapshot_count,difference\n";

    const size_t N = (1 << 20);
    std::vector<unsigned> values = gen_data(N);
    write_data_histogram(values, histogram_results);

    std::vector<unsigned> T_one = {1};
    run_with_data<collector::BaselineCollector>(values, T_one, "BaselineCollector",
                                                benchmark_results);

    std::vector<unsigned> T_full = {1, 2, 4, 8, 16};
    run_with_data<collector::MutexCollector>(values, T_full, "MutexCollector", benchmark_results);
    run_with_data<collector::DryRunMutexCollector>(values, T_full, "DryRunMutexCollector",
                                                   benchmark_results);
    run_with_data<collector::LockStripingCollector>(values, T_full, "LockStripingCollector",
                                                    benchmark_results);
    run_with_data<collector::ThreadLocalCollector>(values, T_full, "ThreadLocalCollector",
                                                   benchmark_results);
    run_with_data<collector::DoubleBufferCollector>(values, T_full, "DoubleBufferCollector",
                                                    benchmark_results);
    run_with_data<collector::BrokenDoubleBufferCollector>(
        values, T_full, "BrokenDoubleBufferCollector", benchmark_results);

    run_inconsistency_test<collector::LockStripingCollector>(values, 4, "LockStripingCollector",
                                                             stress_results);
    run_inconsistency_test<collector::ThreadLocalCollector>(values, 4, "ThreadLocalCollector",
                                                            stress_results);
    run_inconsistency_test<collector::DoubleBufferCollector>(values, 4, "DoubleBufferCollector",
                                                             stress_results);
    run_inconsistency_test<collector::BrokenDoubleBufferCollector>(
        values, 4, "BrokenDoubleBufferCollector", stress_results);
}
