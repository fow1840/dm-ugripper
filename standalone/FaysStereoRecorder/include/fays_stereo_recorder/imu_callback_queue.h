#pragma once
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <semaphore.h>
#include <stdexcept>
#include <time.h>

namespace fays_stereo_recorder {
// Exactly one SDK producer and one consumer. No allocation or business lock
// in Push. A semaphore retains wakeups across the empty-to-wait race.
template<class T, size_t Capacity>
class ImuCallbackQueue {
public:
    ImuCallbackQueue() { if (sem_init(&available_, 0, 0)) throw std::runtime_error("IMU semaphore init failed"); }
    ~ImuCallbackQueue() { sem_destroy(&available_); }
    ImuCallbackQueue(const ImuCallbackQueue&) = delete;
    bool Push(const T& value) {
        const auto w = written_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= Capacity) return false;
        values_[w % Capacity] = value;
        written_.store(w + 1, std::memory_order_release);
        sem_post(&available_);
        return true;
    }
    bool Pop(T& value) {
        // System time may be adjusted during clock synchronization. Timeout
        // must remain bounded for shutdown even after a backward wall step.
        timespec deadline{};
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_nsec += 10'000'000;
        if (deadline.tv_nsec >= 1'000'000'000) { ++deadline.tv_sec; deadline.tv_nsec -= 1'000'000'000; }
        int result;
        do { result = sem_clockwait(&available_, CLOCK_MONOTONIC, &deadline); } while (result && errno == EINTR);
        if (result) return false;
        const auto r = read_.load(std::memory_order_relaxed);
        value = values_[r % Capacity];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    uint64_t Written() const { return written_.load(std::memory_order_acquire); }
    uint64_t Size() const { const auto r = read_.load(std::memory_order_acquire); return Written() - r; }
private:
    std::array<T, Capacity> values_{};
    alignas(64) std::atomic<uint64_t> written_{0};
    alignas(64) std::atomic<uint64_t> read_{0};
    sem_t available_{};
};
}
