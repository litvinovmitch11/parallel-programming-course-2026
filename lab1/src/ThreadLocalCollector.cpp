#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "ThreadLocalCollector.hpp"

namespace collector {
namespace {

uint64_t next_collector_id() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
}

inline void relaxed_add(std::atomic<uint64_t>& c, uint64_t delta) {
    c.store(c.load(std::memory_order_relaxed) + delta, std::memory_order_relaxed);
}

} // namespace

ThreadLocalCollector::ThreadLocalCollector() : id_(next_collector_id()) {}

ThreadLocalCollector::ThreadState* ThreadLocalCollector::get_my_state() {
    struct TLSSlot {
        uint64_t id = 0;
        ThreadState* state = nullptr;
    };
    static thread_local TLSSlot slot;
    if (slot.id != id_) {
        auto s = std::make_unique<ThreadState>();
        ThreadState* raw = s.get();
        {
            std::lock_guard<std::mutex> g{list_lock_};
            all_states_.push_back(std::move(s));
        }
        slot.id = id_;
        slot.state = raw;
    }

    return slot.state;
}

void ThreadLocalCollector::record(uint64_t value) {
    ThreadState* s = get_my_state();
    uint64_t b = std::min(value / 4, (uint64_t)255);

    relaxed_add(s->buckets[b], 1);
    relaxed_add(s->count, 1);
    relaxed_add(s->sum, value);

    if (value < s->min.load(std::memory_order_relaxed))
        s->min.store(value, std::memory_order_relaxed);
    if (value > s->max.load(std::memory_order_relaxed))
        s->max.store(value, std::memory_order_relaxed);
}

Snapshot ThreadLocalCollector::snapshot() {
    std::vector<ThreadState*> states;
    {
        std::lock_guard<std::mutex> lock{list_lock_};
        states.reserve(all_states_.size());
        for (const auto& state : all_states_) {
            states.push_back(state.get());
        }
    }

    std::array<uint64_t, 256> snap_buckets{};
    uint64_t snap_count = 0;
    uint64_t snap_sum = 0;
    uint64_t snap_min = UINT64_MAX;
    uint64_t snap_max = 0;
    for (auto s : states) {
        for (size_t i = 0; i < snap_buckets.size(); ++i) {
            snap_buckets[i] += s->buckets[i].load(std::memory_order_relaxed);
        }
        snap_count += s->count.load(std::memory_order_relaxed);
        snap_sum += s->sum.load(std::memory_order_relaxed);
        snap_min = std::min(snap_min, s->min.load(std::memory_order_relaxed));
        snap_max = std::max(snap_max, s->max.load(std::memory_order_relaxed));
    }
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
