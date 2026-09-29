#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "Collector.hpp"

namespace collector {

class DoubleBufferCollector : public MetricsCollector {
public:
    DoubleBufferCollector();
    void record(uint64_t value) override;
    Snapshot snapshot() override;

private:
    struct Buf {
        std::array<uint64_t, 256> buckets{}; // обычные uint64_t
        uint64_t count = 0, sum = 0, min = UINT64_MAX, max = 0;
        void clear() {
            buckets.fill(0);
            count = 0;
            sum = 0;
            min = UINT64_MAX;
            max = 0;
        }
    };

    struct alignas(64) ThreadBuffers {
        std::atomic<int> inside{-1}; // -1 = нигде, 0 = буфер 0, 1 = буфер 1
        Buf buf[2];
    };

    ThreadBuffers* get_my_buffers();

    const uint64_t id_;
    std::atomic<int> active_;
    std::mutex snap_lock_;
    std::vector<std::unique_ptr<ThreadBuffers>> all_states_;
    Buf global_;
};

class BrokenDoubleBufferCollector : public MetricsCollector {
public:
    BrokenDoubleBufferCollector();
    void record(uint64_t value) override;
    Snapshot snapshot() override;

private:
    struct Buf {
        std::array<uint64_t, 256> buckets{}; // обычные uint64_t
        uint64_t count = 0, sum = 0, min = UINT64_MAX, max = 0;
        void clear() {
            buckets.fill(0);
            count = 0;
            sum = 0;
            min = UINT64_MAX;
            max = 0;
        }
    };

    struct alignas(64) ThreadBuffers {
        std::atomic<int> inside{-1}; // -1 = нигде, 0 = буфер 0, 1 = буфер 1
        Buf buf[2];
    };

    ThreadBuffers* get_my_buffers();

    const uint64_t id_;
    std::atomic<int> active_;
    std::mutex snap_lock_;
    std::vector<std::unique_ptr<ThreadBuffers>> all_states_;
    Buf global_;
};

} // namespace collector
