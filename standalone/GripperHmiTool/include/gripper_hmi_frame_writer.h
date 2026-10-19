#ifndef GRIPPER_HMI_FRAME_WRITER_H
#define GRIPPER_HMI_FRAME_WRITER_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace gripper_hmi {

enum class FrameWriteResult { Ok, WriteFailed, QueueQueryFailed, QueueTimeout };

// All writes on one port, including exclusive commands, share this gate.
// Queue empty is a host observation, not an MCU ACK. Add a full 8N1 frame's
// wire time before the quiet interval to cover the bridge's remaining bytes.
class FrameWriter
{
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto kQuietInterval = std::chrono::milliseconds(25);
    static constexpr auto kQueueTimeout = std::chrono::milliseconds(50);

    void Reset() { nextWriteAt_ = {}; }

    template <typename Write, typename Pending, typename Now, typename SleepUntil>
    FrameWriteResult Send(size_t size, uint32_t baudrate, Write write,
                          Pending pending, Now now, SleepUntil sleepUntil)
    {
        if (size == 0 || baudrate == 0) return FrameWriteResult::WriteFailed;
        if (now() < nextWriteAt_) sleepUntil(nextWriteAt_);
        if (!write()) return FrameWriteResult::WriteFailed;

        const auto deadline = now() + kQueueTimeout;
        while (true)
        {
            const int queued = pending();
            if (queued < 0) return FrameWriteResult::QueueQueryFailed;
            if (queued == 0) break;
            const auto current = now();
            if (current >= deadline) return FrameWriteResult::QueueTimeout;
            sleepUntil(std::min(deadline, current + std::chrono::milliseconds(1)));
        }
        const auto wireUs = (static_cast<uint64_t>(size) * 10 * 1000000 + baudrate - 1) / baudrate;
        nextWriteAt_ = now() + std::chrono::microseconds(wireUs) + kQuietInterval;
        return FrameWriteResult::Ok;
    }

private:
    Clock::time_point nextWriteAt_{};
};

} // namespace gripper_hmi
#endif
