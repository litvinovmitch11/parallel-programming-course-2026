#pragma once

#include "Collector.hpp"

namespace collector {

class BaselineCollector : public MetricsCollector {
public:
    BaselineCollector() : buckets(), count(0), sum(0), min(UINT64_MAX), max(0) {}
    void record(uint64_t value) override;
    Snapshot snapshot() override;

private:
    std::array<uint64_t, 256> buckets;
    uint64_t count;
    uint64_t sum;
    uint64_t min;
    uint64_t max;
};

} // namespace collector
