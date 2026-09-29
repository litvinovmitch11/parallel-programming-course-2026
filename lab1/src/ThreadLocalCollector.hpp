#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "Collector.hpp"

namespace collector {

class ThreadLocalCollector : public MetricsCollector {
public:
    ThreadLocalCollector();
    void record(uint64_t value) override;
    Snapshot snapshot() override;

private:
    struct alignas(64) ThreadState {
        std::array<std::atomic<uint64_t>, 256> buckets{};
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> sum{0};
        std::atomic<uint64_t> min{UINT64_MAX};
        std::atomic<uint64_t> max{0};
    };

    ThreadState* get_my_state();

    const uint64_t id_;
    std::mutex list_lock_;
    std::vector<std::unique_ptr<ThreadState>> all_states_;
};

} // namespace collector
