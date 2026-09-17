#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fays_stereo_recorder {

struct VideoFrame {
    std::vector<uint8_t> bytes;
    int width = 0, height = 0, channels = 0;
    uint64_t faysTsNs = 0, publishTimeNs = 0;
    // Capture-side values: never derive SDK gaps from the consumer's queue history.
    uint64_t previousFaysTsNs = 0, capturedSteadyNs = 0, sessionId = 0;
};

// One producer and one consumer. Slot/buffer swaps hold the lock briefly; SDK,
// memcpy, encoding and logging must all happen outside this queue's lock.
// Full queues retain the latest frames and count each discarded oldest frame.
class VideoFrameQueue {
public:
    static constexpr size_t kDefaultCapacity = 64;
    explicit VideoFrameQueue(size_t capacity = kDefaultCapacity) : slots_(capacity) {
        if (!capacity) throw std::invalid_argument("video frame capacity must be positive");
    }

    bool Push(VideoFrame&& frame) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (stopped_) return false;
            if (size_ == slots_.size()) {
                head_ = (head_ + 1) % slots_.size();
                --size_;
                dropCount_.fetch_add(1, std::memory_order_relaxed);
            }
            // Return the slot's old allocation to the producer for the next copy.
            std::swap(frame, slots_[(head_ + size_) % slots_.size()]);
            ++size_;
            if (size_ > highWater_.load(std::memory_order_relaxed))
                highWater_.store(size_, std::memory_order_relaxed);
        }
        cv_.notify_one();
        return true;
    }

    bool Pop(VideoFrame& out) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return size_ || stopped_; });
        if (!size_) return false;
        // Return the previously consumed allocation to the ring, without freeing it.
        std::swap(out, slots_[head_]);
        head_ = (head_ + 1) % slots_.size();
        --size_;
        return true;
    }

    void Stop() {
        { std::lock_guard<std::mutex> lock(mtx_); stopped_ = true; }
        cv_.notify_all();
    }
    uint64_t GetDropCount() const { return dropCount_.load(std::memory_order_relaxed); }
    size_t HighWater() const { return highWater_.load(std::memory_order_relaxed); }

private:
    std::vector<VideoFrame> slots_;
    size_t head_ = 0, size_ = 0;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stopped_ = false;
    std::atomic<uint64_t> dropCount_{0};
    std::atomic<size_t> highWater_{0};
};

} // namespace fays_stereo_recorder
