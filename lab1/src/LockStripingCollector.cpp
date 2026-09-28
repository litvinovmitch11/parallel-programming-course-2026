#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>

#include "LockStripingCollector.hpp"

namespace collector {
void LockStripingCollector::record(uint64_t value) {
    const auto bucket = std::min<uint64_t>(value / 4, 255);
    {
        std::lock_guard<std::mutex> lock{ms[bucket % 16]};
        buckets[bucket] += 1;
    }
    count += 1;
    sum += value;
    uint64_t current_min = min;
    while (current_min > value && !min.compare_exchange_weak(current_min, value)) {
    }
    uint64_t current_max = max;
    while (current_max < value && !max.compare_exchange_weak(current_max, value)) {
    }
}

Snapshot LockStripingCollector::snapshot() {
    std::array<uint64_t, 256> snap_buckets;
    for (size_t i = 0; i < 16; ++i) {
        std::lock_guard<std::mutex> lock{ms[i]};
        for (size_t j = i; j < 256; j += 16) {
            snap_buckets[j] = buckets[j];
        }
    }
    uint64_t snap_count = count;
    uint64_t snap_sum = sum;
    uint64_t snap_min = min;
    uint64_t snap_max = max;
    uint64_t curr = 0;
    long double threshold = static_cast<long double>(snap_count) * 0.50;
    uint64_t p50 = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        curr += snap_buckets[i];
        if (curr >= threshold) {
            p50 = i * 4;
            break;
        }
    }
    curr = 0;
    threshold = static_cast<long double>(snap_count) * 0.99;
    uint64_t p99 = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        curr += snap_buckets[i];
        if (curr >= threshold) {
            p99 = i * 4;
            break;
        }
    }
    return Snapshot{
        snap_buckets, snap_count, snap_sum, snap_min, snap_max, p50, p99,
    };
}

} // namespace collector
