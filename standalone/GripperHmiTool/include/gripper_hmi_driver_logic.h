#pragma once

#include "gripper_hmi_calibration_data.h"
#include "gripper_hmi_protocol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gripper_hmi::driver_logic {

static constexpr size_t kCalibrationHeaderChunkCount =
    (sizeof(GripperCalibrationHeader) + GripperHmiProtocol::kCalibrationChunkSize - 1) /
    GripperHmiProtocol::kCalibrationChunkSize;
static constexpr size_t kCalibrationChunkCount =
    kCalibrationPayloadSize / GripperHmiProtocol::kCalibrationChunkSize;

enum class CalibrationWriteChunkStatusAction
{
    Ack,
    Retry,
    AbortAndRecover,
    Fail,
};

enum class StatusFrameScanResult
{
    NeedMoreData,
    ContinueScanning,
    MatchedStatus,
};

enum class ExclusiveFrameScanResult
{
    NeedMoreData,
    ContinueScanning,
    MatchedStatus,
    MatchedData,
};

enum class SizedFrameScanResult
{
    NeedMoreData,
    ContinueScanning,
    MatchedFrame,
};

enum class ExclusiveDrainAction
{
    StopDrained,
    StopIoFailure,
    ContinueSleeping,
    ContinueAfterBytes,
};

enum class ReadBytesAction
{
    ReturnSuccessNoData,
    ReturnSuccessWithData,
    ReturnIoFailure,
    ContinueSleeping,
};

enum class CalibrationRangeCollectResult
{
    NeedMoreData,
    ContinueScanning,
    CollectionComplete,
};

enum class CalibrationChunkIndexBase
{
    Unknown,
    ZeroBased,
    OneBased,
};

enum class CalibrationReadRetryAction
{
    AbortRecoverRetry,
    RetryAfterDelay,
    StopFailure,
};

enum class CalibrationChunkReadAction
{
    AcceptData,
    AbortRecoverRetry,
    RetryAfterDelay,
    StopFailure,
};

enum class CalibrationFrameScanResult
{
    NeedMoreData,
    ContinueScanning,
    MatchedFrame,
};

struct ExclusiveDrainStepResult
{
    ExclusiveDrainAction action = ExclusiveDrainAction::StopDrained;
    int64_t next_idle_deadline_ms = 0;
};

struct CalibrationRangeCollectState
{
    std::array<uint8_t, kCalibrationPayloadSize>* raw = nullptr;
    std::array<bool, kCalibrationChunkCount>* received = nullptr;
    bool* required_chunk_count_known = nullptr;
    size_t* required_chunk_count = nullptr;
    CalibrationChunkIndexBase* index_base = nullptr;
    uint8_t* status_token = nullptr;
    uint8_t* status_code = nullptr;
};

inline bool ShouldWriteBeepCommand(const GripperBeepState& state,
                                   uint32_t retry_writes_remaining,
                                   uint64_t last_write_at_ms,
                                   uint64_t now_ms,
                                   uint64_t resend_interval_ms)
{
    if (retry_writes_remaining == 0 && state.duty == 0)
    {
        // Keep OFF alive at low rate after the initial burst. A lost burst
        // must not leave hardware buzzing indefinitely; no extra state machine.
        resend_interval_ms = 1000;
    }
    if (last_write_at_ms == 0 || now_ms < last_write_at_ms)
    {
        return true;
    }
    return now_ms - last_write_at_ms >= resend_interval_ms;
}

// A pending query is not runnable until the previous beep's quiet window ends.
inline uint64_t StateQueryWaitMs(uint64_t now_ms, bool pending,
                                 uint64_t last_query_ms, uint64_t last_beep_ms)
{
    const uint64_t query_due = pending || last_query_ms == 0 ? now_ms : last_query_ms + 1000;
    const uint64_t quiet_due = last_beep_ms == 0 ? now_ms : last_beep_ms + 25;
    const uint64_t due = std::max(query_due, quiet_due);
    return due > now_ms ? due - now_ms : 1;
}

// Compute from post-dispatch state: a query sent in this iteration must not
// suppress the next beep retry or the LED keepalive for an entire second.
inline uint64_t OwnerWaitMs(uint64_t now_ms, bool query_pending,
                            uint64_t last_query_ms, uint64_t last_beep_ms,
                            uint64_t beep_interval_ms, uint64_t last_led_ms,
                            uint64_t last_write_ms, bool render_enabled,
                            uint64_t last_render_ms)
{
    // Receive unsolicited key reports at least every 20ms even when no LED
    // effect is active. This does not permit writes before their quiet deadline.
    uint64_t wait = std::min<uint64_t>(
        20, StateQueryWaitMs(now_ms, query_pending, last_query_ms, last_beep_ms));
    const bool query_due = query_pending || last_query_ms == 0 || now_ms >= last_query_ms + 1000;
    // The due query owns its quiet window. New commands still wake the owner
    // through notification; overdue retries must not cause 1ms polling here.
    if (query_due) return wait;
    const uint64_t budget_due = last_write_ms == 0 ? now_ms : last_write_ms + 5;
    const uint64_t beep_due = std::max({last_beep_ms == 0 ? now_ms : last_beep_ms + beep_interval_ms,
                                        last_query_ms + 25, budget_due});
    const uint64_t led_due = std::max(last_led_ms == 0 ? now_ms : last_led_ms + 250, budget_due);
    const auto until = [now_ms](uint64_t due) { return due > now_ms ? due - now_ms : uint64_t{1}; };
    wait = std::min({wait, until(beep_due), until(led_due)});
    if (render_enabled)
        wait = std::min(wait, until(last_render_ms == 0 ? now_ms : last_render_ms + 20));
    return wait;
}

// Only redundant traffic uses this budget. Explicit changes and health probes
// must not wait behind keepalives; ties prefer the older LED refresh.
enum class KeepaliveCommand { None, Beep, Led };

inline KeepaliveCommand SelectKeepaliveCommand(bool beep_ready, bool led_ready,
                                               uint64_t beep_due_ms, uint64_t led_due_ms,
                                               uint64_t last_write_ms, uint64_t now_ms)
{
    if (last_write_ms != 0 && now_ms >= last_write_ms && now_ms - last_write_ms < 5)
        return KeepaliveCommand::None;
    if (led_ready && (!beep_ready || led_due_ms <= beep_due_ms))
        return KeepaliveCommand::Led;
    return beep_ready ? KeepaliveCommand::Beep : KeepaliveCommand::None;
}

inline size_t RequiredCalibrationChunkCountFromHeader(const GripperCalibrationHeader& header);

inline bool TryParseCalibrationHeader(const std::array<uint8_t, kCalibrationPayloadSize>& raw,
                                      const std::array<bool, kCalibrationChunkCount>& received,
                                      GripperCalibrationHeader* header,
                                      size_t* required_chunk_count);

inline bool HasAllCalibrationChunks(const std::array<bool, kCalibrationChunkCount>& received,
                                    size_t required_chunk_count);

inline bool IsRetryableCalibrationStatus(uint8_t status_code)
{
    return status_code == GripperHmiProtocol::kStatusMissingData ||
           status_code == GripperHmiProtocol::kStatusZeroDataChecksumError ||
           status_code == GripperHmiProtocol::kStatusChecksumError ||
           status_code == GripperHmiProtocol::kStatusAddressOutOfLimit;
}

inline bool IsCalibrationAbortRecoveryStatus(uint8_t status_code)
{
    return status_code == GripperHmiProtocol::kStatusMissingData ||
           status_code == GripperHmiProtocol::kStatusZeroDataChecksumError;
}

inline bool IsCalibrationReadResponseToken(size_t packet_index, uint8_t token)
{
    return token == static_cast<uint8_t>(packet_index) ||
           token == static_cast<uint8_t>(packet_index + 1);
}

inline bool IsCalibrationWriteAckToken(size_t packet_index, uint8_t token)
{
    return IsCalibrationReadResponseToken(packet_index, token);
}

inline bool IsValidEightByteRecvFrame(const uint8_t* frame)
{
    if (frame == nullptr)
    {
        return false;
    }

    return GripperHmiProtocol::validateXor(
        frame,
        GripperHmiProtocol::kStatusFrameLength - 1,
        frame[GripperHmiProtocol::kStatusFrameLength - 1]);
}

inline bool IsExclusiveStatusFrame(const uint8_t* frame)
{
    if (!IsValidEightByteRecvFrame(frame))
    {
        return false;
    }

    return frame[4] == 0x00 && frame[5] == 0x00 && frame[6] == 0xA5;
}

inline bool NormalizeBufferToRecvHeader(std::vector<uint8_t>* buffer)
{
    if (buffer == nullptr)
    {
        return false;
    }

    static constexpr std::array<uint8_t, 2> kHeader = {
        GripperHmiProtocol::kRecvHead1,
        GripperHmiProtocol::kRecvHead2,
    };

    if (buffer->size() < kHeader.size())
    {
        return false;
    }

    auto header_pos = std::search(buffer->begin(), buffer->end(), kHeader.begin(), kHeader.end());
    if (header_pos == buffer->end())
    {
        if (buffer->size() > 1)
        {
            buffer->erase(buffer->begin(), buffer->end() - 1);
        }
        return false;
    }

    if (header_pos != buffer->begin())
    {
        buffer->erase(buffer->begin(), header_pos);
    }
    return true;
}

inline StatusFrameScanResult ScanExpectedExclusiveStatusFrame(std::vector<uint8_t>* buffer,
                                                             uint8_t expected_token,
                                                             uint8_t* response_token,
                                                             uint8_t* status_code)
{
    if (buffer == nullptr)
    {
        return StatusFrameScanResult::NeedMoreData;
    }

    if (!NormalizeBufferToRecvHeader(buffer))
    {
        return StatusFrameScanResult::NeedMoreData;
    }

    if (buffer->size() < GripperHmiProtocol::kStatusFrameLength)
    {
        return StatusFrameScanResult::NeedMoreData;
    }

    if (!IsValidEightByteRecvFrame(buffer->data()))
    {
        buffer->erase(buffer->begin());
        return StatusFrameScanResult::ContinueScanning;
    }

    if (!IsExclusiveStatusFrame(buffer->data()))
    {
        buffer->erase(
            buffer->begin(),
            buffer->begin() + static_cast<std::ptrdiff_t>(GripperHmiProtocol::kStatusFrameLength));
        return StatusFrameScanResult::ContinueScanning;
    }

    const uint8_t parsed_token = (*buffer)[2];
    const uint8_t parsed_status_code = (*buffer)[3];
    buffer->erase(
        buffer->begin(),
        buffer->begin() + static_cast<std::ptrdiff_t>(GripperHmiProtocol::kStatusFrameLength));

    if (expected_token != 0xFF && parsed_token != expected_token)
    {
        return StatusFrameScanResult::ContinueScanning;
    }

    if (response_token != nullptr)
    {
        *response_token = parsed_token;
    }
    if (status_code != nullptr)
    {
        *status_code = parsed_status_code;
    }
    return StatusFrameScanResult::MatchedStatus;
}

inline bool ValidateCalibrationReadFrame(const uint8_t* frame)
{
    if (frame == nullptr)
    {
        return false;
    }

    return GripperHmiProtocol::calculateXor(
               frame + 3,
               GripperHmiProtocol::kCalibrationChunkSize) ==
           frame[GripperHmiProtocol::kCalibrationChunkFrameLength - 1];
}

inline ExclusiveFrameScanResult ScanCalibrationDataOrStatusFrame(std::vector<uint8_t>* buffer,
                                                                 uint8_t expected_token,
                                                                 size_t data_length,
                                                                 std::vector<uint8_t>* payload,
                                                                 uint8_t* token,
                                                                 uint8_t* status_code)
{
    if (buffer == nullptr)
    {
        return ExclusiveFrameScanResult::NeedMoreData;
    }

    if (!NormalizeBufferToRecvHeader(buffer))
    {
        return ExclusiveFrameScanResult::NeedMoreData;
    }

    const size_t data_frame_size = 2 + 1 + data_length + 1;
    if (buffer->size() >= data_frame_size &&
        IsCalibrationReadResponseToken(expected_token, (*buffer)[2]) &&
        ValidateCalibrationReadFrame(buffer->data()))
    {
        if (payload != nullptr)
        {
            payload->assign(
                buffer->begin() + 3,
                buffer->begin() + 3 + static_cast<std::ptrdiff_t>(data_length));
        }
        return ExclusiveFrameScanResult::MatchedData;
    }

    if (buffer->size() >= GripperHmiProtocol::kStatusFrameLength &&
        IsExclusiveStatusFrame(buffer->data()))
    {
        const uint8_t parsed_token = (*buffer)[2];
        const uint8_t parsed_status_code = (*buffer)[3];
        buffer->erase(
            buffer->begin(),
            buffer->begin() + static_cast<std::ptrdiff_t>(GripperHmiProtocol::kStatusFrameLength));

        if (!IsCalibrationReadResponseToken(expected_token, parsed_token))
        {
            return ExclusiveFrameScanResult::ContinueScanning;
        }

        if (token != nullptr)
        {
            *token = parsed_token;
        }
        if (status_code != nullptr)
        {
            *status_code = parsed_status_code;
        }
        return ExclusiveFrameScanResult::MatchedStatus;
    }

    if (buffer->size() > data_frame_size)
    {
        buffer->erase(buffer->begin());
        return ExclusiveFrameScanResult::ContinueScanning;
    }

    return ExclusiveFrameScanResult::NeedMoreData;
}

inline ExclusiveFrameScanResult ScanRawDataOrStatusFrame(std::vector<uint8_t>* buffer,
                                                         size_t data_length,
                                                         std::vector<uint8_t>* payload,
                                                         uint8_t* token,
                                                         uint8_t* status_code)
{
    if (buffer == nullptr)
    {
        return ExclusiveFrameScanResult::NeedMoreData;
    }

    if (!NormalizeBufferToRecvHeader(buffer))
    {
        return ExclusiveFrameScanResult::NeedMoreData;
    }

    const size_t data_frame_size = 2 + data_length + 1;
    if (buffer->size() >= data_frame_size &&
        GripperHmiProtocol::validateXor(buffer->data(), data_frame_size - 1, (*buffer)[data_frame_size - 1]))
    {
        if (payload != nullptr)
        {
            payload->assign(
                buffer->begin() + 2,
                buffer->begin() + 2 + static_cast<std::ptrdiff_t>(data_length));
        }
        buffer->erase(buffer->begin(), buffer->begin() + static_cast<std::ptrdiff_t>(data_frame_size));
        return ExclusiveFrameScanResult::MatchedData;
    }

    if (buffer->size() >= GripperHmiProtocol::kStatusFrameLength &&
        IsExclusiveStatusFrame(buffer->data()))
    {
        if (token != nullptr)
        {
            *token = (*buffer)[2];
        }
        if (status_code != nullptr)
        {
            *status_code = (*buffer)[3];
        }
        buffer->erase(
            buffer->begin(),
            buffer->begin() + static_cast<std::ptrdiff_t>(GripperHmiProtocol::kStatusFrameLength));
        return ExclusiveFrameScanResult::MatchedStatus;
    }

    if (buffer->size() > data_frame_size)
    {
        buffer->erase(buffer->begin());
        return ExclusiveFrameScanResult::ContinueScanning;
    }

    return ExclusiveFrameScanResult::NeedMoreData;
}

inline SizedFrameScanResult ScanSizedRecvFrame(std::vector<uint8_t>* buffer,
                                               size_t frame_size,
                                               std::vector<uint8_t>* frame)
{
    if (buffer == nullptr || frame_size < 4)
    {
        return SizedFrameScanResult::NeedMoreData;
    }

    if (!NormalizeBufferToRecvHeader(buffer))
    {
        return SizedFrameScanResult::NeedMoreData;
    }

    if (buffer->size() < frame_size)
    {
        return SizedFrameScanResult::NeedMoreData;
    }

    if (!GripperHmiProtocol::validateXor(buffer->data(), frame_size - 1, (*buffer)[frame_size - 1]))
    {
        buffer->erase(buffer->begin());
        return SizedFrameScanResult::ContinueScanning;
    }

    if (frame != nullptr)
    {
        frame->assign(buffer->begin(), buffer->begin() + static_cast<std::ptrdiff_t>(frame_size));
    }
    return SizedFrameScanResult::MatchedFrame;
}

inline ExclusiveDrainStepResult EvaluateExclusiveDrainStep(int64_t now_ms,
                                                           int64_t idle_deadline_ms,
                                                           int64_t max_deadline_ms,
                                                           int bytes_read,
                                                           int idle_window_ms)
{
    ExclusiveDrainStepResult result;
    result.next_idle_deadline_ms = idle_deadline_ms;

    if (now_ms >= idle_deadline_ms || now_ms >= max_deadline_ms)
    {
        result.action = ExclusiveDrainAction::StopDrained;
        return result;
    }

    if (bytes_read < 0)
    {
        result.action = ExclusiveDrainAction::StopIoFailure;
        return result;
    }

    if (bytes_read > 0)
    {
        result.action = ExclusiveDrainAction::ContinueAfterBytes;
        result.next_idle_deadline_ms = now_ms + idle_window_ms;
        return result;
    }

    result.action = ExclusiveDrainAction::ContinueSleeping;
    return result;
}

inline ReadBytesAction EvaluateReadBytesStep(bool deadline_reached, int bytes_read)
{
    if (bytes_read < 0)
    {
        return ReadBytesAction::ReturnIoFailure;
    }

    if (bytes_read > 0)
    {
        return ReadBytesAction::ReturnSuccessWithData;
    }

    if (deadline_reached)
    {
        return ReadBytesAction::ReturnSuccessNoData;
    }

    return ReadBytesAction::ContinueSleeping;
}

inline CalibrationRangeCollectResult CollectCalibrationRangeFrame(std::vector<uint8_t>* buffer,
                                                                  CalibrationRangeCollectState state)
{
    if (buffer == nullptr ||
        state.raw == nullptr ||
        state.received == nullptr ||
        state.required_chunk_count_known == nullptr ||
        state.required_chunk_count == nullptr)
    {
        return CalibrationRangeCollectResult::NeedMoreData;
    }

    if (!NormalizeBufferToRecvHeader(buffer))
    {
        return CalibrationRangeCollectResult::NeedMoreData;
    }

    constexpr size_t kDataFrameSize = 2 + 1 + GripperHmiProtocol::kCalibrationChunkSize + 1;
    if (buffer->size() >= kDataFrameSize && ValidateCalibrationReadFrame(buffer->data()))
    {
        const uint8_t token = (*buffer)[2];
        CalibrationChunkIndexBase resolved_base = CalibrationChunkIndexBase::ZeroBased;
        if (state.index_base != nullptr)
        {
            if (*state.index_base == CalibrationChunkIndexBase::Unknown && token == 0)
            {
                *state.index_base = CalibrationChunkIndexBase::ZeroBased;
            }
            else if (*state.index_base == CalibrationChunkIndexBase::Unknown &&
                     static_cast<size_t>(token) == state.received->size())
            {
                // Tokens 1..63 are ambiguous until token 0 or token 64 is observed.
                // While ambiguous they are stored provisionally at their token index.
                // Seeing token 64 proves the old 1-based convention, so shift the
                // provisional chunks down by one before storing the final chunk.
                for (size_t index = 0; index + 1 < state.received->size(); ++index)
                {
                    (*state.received)[index] = (*state.received)[index + 1];
                    if ((*state.received)[index])
                    {
                        std::memcpy(
                            state.raw->data() + index * GripperHmiProtocol::kCalibrationChunkSize,
                            state.raw->data() + (index + 1) * GripperHmiProtocol::kCalibrationChunkSize,
                            GripperHmiProtocol::kCalibrationChunkSize);
                    }
                }
                state.received->back() = false;
                *state.index_base = CalibrationChunkIndexBase::OneBased;
            }
            resolved_base = *state.index_base;
        }

        size_t packet_index = state.received->size();
        if ((resolved_base == CalibrationChunkIndexBase::ZeroBased ||
             resolved_base == CalibrationChunkIndexBase::Unknown) &&
            token < state.received->size())
        {
            packet_index = static_cast<size_t>(token);
        }
        else if (resolved_base == CalibrationChunkIndexBase::OneBased &&
                 token >= 1 && static_cast<size_t>(token) <= state.received->size())
        {
            packet_index = static_cast<size_t>(token - 1);
        }

        if (packet_index < state.received->size())
        {
            std::memcpy(state.raw->data() + packet_index * GripperHmiProtocol::kCalibrationChunkSize,
                        buffer->data() + 3,
                        GripperHmiProtocol::kCalibrationChunkSize);
            (*state.received)[packet_index] = true;

            GripperCalibrationHeader header{};
            size_t parsed_required_chunk_count = kCalibrationChunkCount;
            if (!(*state.required_chunk_count_known) &&
                TryParseCalibrationHeader(*state.raw, *state.received, &header, &parsed_required_chunk_count))
            {
                *state.required_chunk_count = parsed_required_chunk_count;
                *state.required_chunk_count_known = true;
            }
        }

        buffer->erase(buffer->begin(), buffer->begin() + static_cast<std::ptrdiff_t>(kDataFrameSize));
        if (*state.required_chunk_count_known &&
            HasAllCalibrationChunks(*state.received, *state.required_chunk_count))
        {
            return CalibrationRangeCollectResult::CollectionComplete;
        }
        return CalibrationRangeCollectResult::ContinueScanning;
    }

    if (buffer->size() >= GripperHmiProtocol::kStatusFrameLength &&
        IsExclusiveStatusFrame(buffer->data()))
    {
        if (state.status_token != nullptr)
        {
            *state.status_token = (*buffer)[2];
        }
        if (state.status_code != nullptr)
        {
            *state.status_code = (*buffer)[3];
        }
        buffer->erase(
            buffer->begin(),
            buffer->begin() + static_cast<std::ptrdiff_t>(GripperHmiProtocol::kStatusFrameLength));
        return CalibrationRangeCollectResult::ContinueScanning;
    }

    if (buffer->size() < kDataFrameSize)
    {
        return CalibrationRangeCollectResult::NeedMoreData;
    }

    buffer->erase(buffer->begin());
    return CalibrationRangeCollectResult::ContinueScanning;
}

inline CalibrationReadRetryAction DecideCalibrationRangeRetryAction(uint8_t status_code,
                                                                    int attempt,
                                                                    int retry_limit)
{
    if (IsCalibrationAbortRecoveryStatus(status_code) && attempt + 1 < retry_limit)
    {
        return CalibrationReadRetryAction::AbortRecoverRetry;
    }

    if (IsRetryableCalibrationStatus(status_code))
    {
        return CalibrationReadRetryAction::RetryAfterDelay;
    }

    return CalibrationReadRetryAction::StopFailure;
}

inline CalibrationChunkReadAction DecideCalibrationChunkReadAction(ExclusiveFrameScanResult result,
                                                                   size_t payload_size,
                                                                   size_t expected_payload_size,
                                                                   uint8_t status_code,
                                                                   int attempt,
                                                                   int retry_limit)
{
    if (result == ExclusiveFrameScanResult::MatchedData && payload_size == expected_payload_size)
    {
        return CalibrationChunkReadAction::AcceptData;
    }

    if (result == ExclusiveFrameScanResult::MatchedStatus &&
        IsCalibrationAbortRecoveryStatus(status_code) &&
        attempt + 1 < retry_limit)
    {
        return CalibrationChunkReadAction::AbortRecoverRetry;
    }

    if (result == ExclusiveFrameScanResult::MatchedStatus &&
        IsRetryableCalibrationStatus(status_code))
    {
        return CalibrationChunkReadAction::RetryAfterDelay;
    }

    return CalibrationChunkReadAction::StopFailure;
}

inline CalibrationFrameScanResult ScanExpectedCalibrationFrame(std::vector<uint8_t>* buffer,
                                                               uint8_t expected_token,
                                                               std::vector<uint8_t>* payload)
{
    if (buffer == nullptr)
    {
        return CalibrationFrameScanResult::NeedMoreData;
    }

    if (!NormalizeBufferToRecvHeader(buffer))
    {
        return CalibrationFrameScanResult::NeedMoreData;
    }

    constexpr size_t kFrameSize = 2 + 1 + GripperHmiProtocol::kCalibrationChunkSize + 1;
    if (buffer->size() < kFrameSize)
    {
        return CalibrationFrameScanResult::NeedMoreData;
    }

    if (ValidateCalibrationReadFrame(buffer->data()))
    {
        const uint8_t token = (*buffer)[2];
        if (token == expected_token)
        {
            if (payload != nullptr)
            {
                payload->assign(
                    buffer->begin() + 3,
                    buffer->begin() + 3 + static_cast<std::ptrdiff_t>(GripperHmiProtocol::kCalibrationChunkSize));
            }
            return CalibrationFrameScanResult::MatchedFrame;
        }

        buffer->erase(buffer->begin(), buffer->begin() + static_cast<std::ptrdiff_t>(kFrameSize));
        return CalibrationFrameScanResult::ContinueScanning;
    }

    buffer->erase(buffer->begin());
    return CalibrationFrameScanResult::ContinueScanning;
}

inline size_t RequiredCalibrationChunkCountFromHeader(const GripperCalibrationHeader& header)
{
    const size_t required_bytes = std::max<size_t>(header.payloadSize, header.headerSize);
    const size_t clamped_bytes = std::min(required_bytes, kCalibrationPayloadSize);
    return std::max<size_t>(
        kCalibrationHeaderChunkCount,
        (clamped_bytes + GripperHmiProtocol::kCalibrationChunkSize - 1) / GripperHmiProtocol::kCalibrationChunkSize);
}

inline bool TryParseCalibrationHeader(const std::array<uint8_t, kCalibrationPayloadSize>& raw,
                                      const std::array<bool, kCalibrationChunkCount>& received,
                                      GripperCalibrationHeader* header,
                                      size_t* required_chunk_count)
{
    if (header == nullptr || required_chunk_count == nullptr)
    {
        return false;
    }

    for (size_t packet_index = 0; packet_index < kCalibrationHeaderChunkCount; ++packet_index)
    {
        if (!received[packet_index])
        {
            return false;
        }
    }

    GripperCalibrationHeader parsed{};
    std::memcpy(&parsed, raw.data(), sizeof(parsed));
    if (std::memcmp(parsed.magic, "UCAL", 4) != 0)
    {
        return false;
    }
    if (parsed.headerSize != sizeof(GripperCalibrationHeader))
    {
        return false;
    }
    if (parsed.payloadSize == 0 || parsed.payloadSize > kCalibrationPayloadSize)
    {
        return false;
    }

    *header = parsed;
    *required_chunk_count = RequiredCalibrationChunkCountFromHeader(parsed);
    return true;
}

inline bool HasAllCalibrationChunks(const std::array<bool, kCalibrationChunkCount>& received, size_t required_chunk_count)
{
    const size_t verify_count = std::min(required_chunk_count, received.size());
    return std::all_of(
        received.begin(),
        received.begin() + static_cast<std::ptrdiff_t>(verify_count),
        [](bool value) { return value; });
}

inline bool ShouldAttemptSequentialCalibrationFallback(bool range_read_collected,
                                                       bool required_chunk_count_known,
                                                       size_t required_chunk_count,
                                                       const std::array<bool, kCalibrationChunkCount>& received,
                                                       uint8_t status_code)
{
    return !range_read_collected &&
           (!required_chunk_count_known || !HasAllCalibrationChunks(received, required_chunk_count)) &&
           IsRetryableCalibrationStatus(status_code);
}

inline CalibrationWriteChunkStatusAction DecideCalibrationWriteChunkStatus(uint8_t status_code, int attempt)
{
    if (status_code == GripperHmiProtocol::kStatusOk)
    {
        return CalibrationWriteChunkStatusAction::Ack;
    }

    if (status_code == GripperHmiProtocol::kStatusMissingData)
    {
        return CalibrationWriteChunkStatusAction::AbortAndRecover;
    }

    if (status_code == GripperHmiProtocol::kStatusZeroDataChecksumError)
    {
        return attempt > 0 ? CalibrationWriteChunkStatusAction::AbortAndRecover
                           : CalibrationWriteChunkStatusAction::Retry;
    }

    if (IsRetryableCalibrationStatus(status_code))
    {
        return CalibrationWriteChunkStatusAction::Retry;
    }

    return CalibrationWriteChunkStatusAction::Fail;
}

}  // namespace gripper_hmi::driver_logic
