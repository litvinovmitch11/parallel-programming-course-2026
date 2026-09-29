#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include "DoubleBufferCollector.hpp"

namespace collector {
namespace {

uint64_t next_collector_id() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
}

} // namespace

DoubleBufferCollector::DoubleBufferCollector() : id_(next_collector_id()), active_(0) {}

DoubleBufferCollector::ThreadBuffers* DoubleBufferCollector::get_my_buffers() {
    struct TLSSlot {
        uint64_t id = 0;
        ThreadBuffers* state = nullptr;
    };
    static thread_local TLSSlot slot;
    if (slot.id != id_) {
        auto s = std::make_unique<ThreadBuffers>();
        ThreadBuffers* raw = s.get();
        {
            std::lock_guard<std::mutex> g{snap_lock_};
            all_states_.push_back(std::move(s));
        }
        slot.id = id_;
        slot.state = raw;
    }

    return slot.state;
}

void DoubleBufferCollector::record(uint64_t value) {
    ThreadBuffers* my = get_my_buffers();
    int b;
    while (true) {
        b = active_.load();
        my->inside.store(b);
        if (active_.load() == b)
            break;
        my->inside.store(-1, std::memory_order_release);
    }

    uint64_t bucket = std::min(value / 4, (uint64_t)255);
    my->buf[b].buckets[bucket]++;
    my->buf[b].count++;
    my->buf[b].sum += value;
    my->buf[b].min = std::min(my->buf[b].min, value);
    my->buf[b].max = std::max(my->buf[b].max, value);

    my->inside.store(-1, std::memory_order_release);
}

Snapshot DoubleBufferCollector::snapshot() {
    std::lock_guard<std::mutex> g{snap_lock_};

    int old = active_.load();
    active_.store(1 - old);

    for (auto& state : all_states_) {
        ThreadBuffers* s = state.get();
        while (s->inside.load() == old) {
            std::this_thread::yield();
        }

        for (size_t i = 0; i < global_.buckets.size(); ++i) {
            global_.buckets[i] += s->buf[old].buckets[i];
        }
        global_.count += s->buf[old].count;
        global_.sum += s->buf[old].sum;
        global_.min = std::min(global_.min, s->buf[old].min);
        global_.max = std::max(global_.max, s->buf[old].max);
        s->buf[old].clear();
    }

    uint64_t curr = 0;
    long double threshold = static_cast<long double>(global_.count) * 0.50;
    uint64_t p50 = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        curr += global_.buckets[i];
        if (curr >= threshold) {
            p50 = i * 4;
            break;
        }
    }
    curr = 0;
    threshold = static_cast<long double>(global_.count) * 0.99;
    uint64_t p99 = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        curr += global_.buckets[i];
        if (curr >= threshold) {
            p99 = i * 4;
            break;
        }
    }
    return Snapshot{
        global_.buckets, global_.count, global_.sum, global_.min, global_.max, p50, p99,
    };
}

BrokenDoubleBufferCollector::BrokenDoubleBufferCollector() : id_(next_collector_id()), active_(0) {}

BrokenDoubleBufferCollector::ThreadBuffers* BrokenDoubleBufferCollector::get_my_buffers() {
    struct TLSSlot {
        uint64_t id = 0;
        ThreadBuffers* state = nullptr;
    };
    static thread_local TLSSlot slot;
    if (slot.id != id_) {
        auto s = std::make_unique<ThreadBuffers>();
        ThreadBuffers* raw = s.get();
        {
            std::lock_guard<std::mutex> g{snap_lock_};
            all_states_.push_back(std::move(s));
        }
        slot.id = id_;
        slot.state = raw;
    }

    return slot.state;
}

void BrokenDoubleBufferCollector::record(uint64_t value) {
    ThreadBuffers* my = get_my_buffers();
    int b = active_.load();
    my->inside.store(b);

    uint64_t bucket = std::min(value / 4, (uint64_t)255);
    my->buf[b].buckets[bucket]++;
    my->buf[b].count++;
    my->buf[b].sum += value;
    my->buf[b].min = std::min(my->buf[b].min, value);
    my->buf[b].max = std::max(my->buf[b].max, value);

    my->inside.store(-1, std::memory_order_release);
}

Snapshot BrokenDoubleBufferCollector::snapshot() {
    std::lock_guard<std::mutex> g{snap_lock_};

    int old = active_.load();
    active_.store(1 - old);

    for (auto& state : all_states_) {
        ThreadBuffers* s = state.get();
        while (s->inside.load() == old) {
            std::this_thread::yield();
        }

        for (size_t i = 0; i < global_.buckets.size(); ++i) {
            global_.buckets[i] += s->buf[old].buckets[i];
        }
        global_.count += s->buf[old].count;
        global_.sum += s->buf[old].sum;
        global_.min = std::min(global_.min, s->buf[old].min);
        global_.max = std::max(global_.max, s->buf[old].max);
        s->buf[old].clear();
    }

    uint64_t curr = 0;
    long double threshold = static_cast<long double>(global_.count) * 0.50;
    uint64_t p50 = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        curr += global_.buckets[i];
        if (curr >= threshold) {
            p50 = i * 4;
            break;
        }
    }
    curr = 0;
    threshold = static_cast<long double>(global_.count) * 0.99;
    uint64_t p99 = 0;
    for (uint64_t i = 0; i < 256; ++i) {
        curr += global_.buckets[i];
        if (curr >= threshold) {
            p99 = i * 4;
            break;
        }
    }
    return Snapshot{
        global_.buckets, global_.count, global_.sum, global_.min, global_.max, p50, p99,
    };
}

} // namespace collector
