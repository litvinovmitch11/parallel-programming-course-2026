#include <algorithm>
#include <mutex>

#include "MutexCollector.hpp"

namespace collector {
void MutexCollector::record(uint64_t value) {
    std::lock_guard<std::mutex> lock{m};
    const auto bucket = std::min<uint64_t>(value / 4, 255);
    buckets[bucket] += 1;
    count += 1;
    sum += value;
    min = min > value ? value : min;
    max = max < value ? value : max;
}

Snapshot MutexCollector::snapshot() {
    std::lock_guard<std::mutex> lock{m};
    uint64_t curr = 0;
    long double threshold = static_cast<long double>(count) * 0.50;
    uint64_t p50 = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        curr += buckets[i];
        if (curr >= threshold) {
            p50 = i * 4;
            break;
        }
    }
    curr = 0;
    threshold = static_cast<long double>(count) * 0.99;
    uint64_t p99 = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        curr += buckets[i];
        if (curr >= threshold) {
            p99 = i * 4;
            break;
        }
    }
    return Snapshot{
        buckets, count, sum, min, max, p50, p99,
    };
}

void DryRunMutexCollector::record(uint64_t) {
    std::lock_guard<std::mutex> lock{m};
}

Snapshot DryRunMutexCollector::snapshot() {
    std::lock_guard<std::mutex> lock{m};
    uint64_t curr = 0;
    long double threshold = static_cast<long double>(count) * 0.50;
    uint64_t p50 = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        curr += buckets[i];
        if (curr >= threshold) {
            p50 = i * 4;
            break;
        }
    }
    curr = 0;
    threshold = static_cast<long double>(count) * 0.99;
    uint64_t p99 = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        curr += buckets[i];
        if (curr >= threshold) {
            p99 = i * 4;
            break;
        }
    }
    return Snapshot{
        buckets, count, sum, min, max, p50, p99,
    };
}

} // namespace collector
