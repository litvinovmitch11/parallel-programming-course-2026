#pragma once

#include <atomic>
#include <mutex>

#include "Collector.hpp"

namespace collector {

class LockStripingCollector : public MetricsCollector {
public:
    LockStripingCollector() : buckets(), count(0), sum(0), min(UINT64_MAX), max(0) {}
    void record(uint64_t value) override;
    Snapshot snapshot() override;

private:
    std::array<std::mutex, 16> ms;
    std::array<uint64_t, 256> buckets;
    std::atomic<uint64_t> count;
    std::atomic<uint64_t> sum;
    std::atomic<uint64_t> min;
    std::atomic<uint64_t> max;
};

} // namespace collector
