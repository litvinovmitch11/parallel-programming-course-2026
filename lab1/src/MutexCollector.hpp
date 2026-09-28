#pragma once

#include <mutex>

#include "Collector.hpp"

namespace collector {

class MutexCollector : public MetricsCollector {
public:
    MutexCollector() : buckets(), count(0), sum(0), min(UINT64_MAX), max(0) {}
    void record(uint64_t value) override;
    Snapshot snapshot() override;

private:
    std::mutex m;
    std::array<uint64_t, 256> buckets;
    uint64_t count;
    uint64_t sum;
    uint64_t min;
    uint64_t max;
};

class DryRunMutexCollector : public MetricsCollector {
public:
    DryRunMutexCollector() : buckets(), count(0), sum(0), min(UINT64_MAX), max(0) {}
    void record(uint64_t) override;
    Snapshot snapshot() override;

private:
    std::mutex m;
    std::array<uint64_t, 256> buckets;
    uint64_t count;
    uint64_t sum;
    uint64_t min;
    uint64_t max;
};

} // namespace collector
