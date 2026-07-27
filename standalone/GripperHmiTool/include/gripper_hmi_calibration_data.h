#ifndef GRIPPER_HMI_CALIBRATION_DATA_H
#define GRIPPER_HMI_CALIBRATION_DATA_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace gripper_hmi
{

constexpr size_t kSerialNumberLength = 16;
constexpr size_t kSerialNumberFieldLength = 32;
constexpr size_t kCalibrationPayloadSize = 1024;
constexpr uint32_t kCalibrationDataFormatVersionLegacy = 0x00010000u;
constexpr uint32_t kCalibrationDataFormatVersionForceV1 = 0x00020000u;

enum GripperCalibrationValidField : uint32_t
{
    kCalibrationValidForceSensor = 1u << 0,
};

enum GripperForceCalibrationFlag : uint32_t
{
    kForceCalibrationValid = 1u << 0,
};

#pragma pack(push, 1)

// Keep this header byte-for-byte compatible with the original UCAL header.
struct GripperCalibrationHeader
{
    char magic[4] = {'U', 'C', 'A', 'L'};
    uint32_t dataFormatVersion = kCalibrationDataFormatVersionForceV1;
    uint16_t payloadSize = static_cast<uint16_t>(sizeof(GripperCalibrationHeader));
    uint16_t headerSize = static_cast<uint16_t>(sizeof(GripperCalibrationHeader));
    uint32_t validFields = 0;
    uint8_t reserved[16] = {};
};

// Compact force calibration block. The embedded SN uses exactly the same
// 32-byte printable-ASCII, NUL-padded representation as the existing HMI SN.
struct GripperForceCalibrationBlock
{
    char magic[4] = {'F', 'C', 'A', 'L'};
    uint16_t blockVersion = 1;
    uint16_t blockSize = static_cast<uint16_t>(sizeof(GripperForceCalibrationBlock));
    std::array<char, kSerialNumberFieldLength> serialNumber{};
    int32_t zeroRaw = 0;
    int32_t referenceRaw = 0;
    uint32_t referenceMassMg = 0;
    uint32_t flags = 0;
    uint32_t calibratedAtUnixSeconds = 0;
    uint32_t crc32 = 0;
};

struct GripperCalibrationData
{
    GripperCalibrationHeader header{};
    GripperForceCalibrationBlock force{};
    uint8_t reserved[kCalibrationPayloadSize - sizeof(GripperCalibrationHeader) -
                     sizeof(GripperForceCalibrationBlock)] = {};
};

#pragma pack(pop)

using GripperSerialNumber = std::array<char, kSerialNumberFieldLength>;

static_assert(sizeof(GripperCalibrationHeader) == 32, "GripperCalibrationHeader must be 32 bytes");
static_assert(sizeof(GripperForceCalibrationBlock) == 64,
              "GripperForceCalibrationBlock must be 64 bytes");
static_assert(sizeof(GripperCalibrationData) == kCalibrationPayloadSize,
              "GripperCalibrationData must be exactly 1024 bytes");
static_assert(offsetof(GripperCalibrationData, header) == 0x000, "header offset mismatch");
static_assert(offsetof(GripperCalibrationData, force) == 0x020, "force offset mismatch");
static_assert(offsetof(GripperCalibrationData, reserved) == 0x060, "reserved offset mismatch");

inline bool isPrintableSerialChar(char value)
{
    return value >= 0x20 && value <= 0x7E;
}

inline bool encodeSerialNumber(const std::string &text, GripperSerialNumber *serial)
{
    if (serial == nullptr || text.empty() || text.size() > kSerialNumberFieldLength)
    {
        return false;
    }

    GripperSerialNumber encoded{};
    for (size_t index = 0; index < text.size(); ++index)
    {
        if (!isPrintableSerialChar(text[index]))
        {
            return false;
        }
        encoded[index] = text[index];
    }

    *serial = encoded;
    return true;
}

inline std::string decodeSerialNumber(const GripperSerialNumber &serial)
{
    size_t length = serial.size();
    while (length > 0 && serial[length - 1] == '\0')
    {
        --length;
    }
    return std::string(serial.data(), length);
}

inline uint32_t calculateCalibrationCrc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t index = 0; index < size; ++index)
    {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

inline uint32_t calculateForceCalibrationCrc32(const GripperForceCalibrationBlock &force)
{
    return calculateCalibrationCrc32(
        reinterpret_cast<const uint8_t *>(&force), offsetof(GripperForceCalibrationBlock, crc32));
}

inline bool isForceCalibrationPayload(const GripperCalibrationData &data)
{
    return std::memcmp(data.header.magic, "UCAL", 4) == 0 &&
           data.header.dataFormatVersion == kCalibrationDataFormatVersionForceV1 &&
           data.header.headerSize == sizeof(GripperCalibrationHeader) &&
           data.header.payloadSize >= sizeof(GripperCalibrationHeader) + sizeof(GripperForceCalibrationBlock) &&
           (data.header.validFields & kCalibrationValidForceSensor) != 0 &&
           std::memcmp(data.force.magic, "FCAL", 4) == 0 &&
           data.force.blockVersion == 1 &&
           data.force.blockSize == sizeof(GripperForceCalibrationBlock) &&
           (data.force.flags & kForceCalibrationValid) != 0 &&
           data.force.referenceMassMg > 0 &&
           data.force.referenceRaw != data.force.zeroRaw &&
           data.force.crc32 == calculateForceCalibrationCrc32(data.force);
}

inline bool forceCalibrationMatchesSerialNumber(const GripperCalibrationData &data,
                                                const std::string &serialNumber)
{
    GripperSerialNumber encoded{};
    return encodeSerialNumber(serialNumber, &encoded) && data.force.serialNumber == encoded;
}

inline bool makeForceCalibrationData(const std::string &serialNumber,
                                     int32_t zeroRaw,
                                     int32_t referenceRaw,
                                     uint32_t referenceMassMg,
                                     uint32_t calibratedAtUnixSeconds,
                                     GripperCalibrationData *data)
{
    if (data == nullptr || referenceRaw == zeroRaw || referenceMassMg == 0)
    {
        return false;
    }

    GripperSerialNumber encoded{};
    if (!encodeSerialNumber(serialNumber, &encoded))
    {
        return false;
    }

    GripperCalibrationData value{};
    value.header.dataFormatVersion = kCalibrationDataFormatVersionForceV1;
    value.header.payloadSize = static_cast<uint16_t>(sizeof(GripperCalibrationHeader) +
                                                     sizeof(GripperForceCalibrationBlock));
    value.header.headerSize = static_cast<uint16_t>(sizeof(GripperCalibrationHeader));
    value.header.validFields = kCalibrationValidForceSensor;
    value.force.serialNumber = encoded;
    value.force.zeroRaw = zeroRaw;
    value.force.referenceRaw = referenceRaw;
    value.force.referenceMassMg = referenceMassMg;
    value.force.flags = kForceCalibrationValid;
    value.force.calibratedAtUnixSeconds = calibratedAtUnixSeconds;
    value.force.crc32 = calculateForceCalibrationCrc32(value.force);
    *data = value;
    return true;
}

inline bool forceMassMg(const GripperCalibrationData &data, int32_t raw, int64_t *massMg)
{
    if (massMg == nullptr || !isForceCalibrationPayload(data))
    {
        return false;
    }

    const int64_t numerator = (static_cast<int64_t>(raw) - static_cast<int64_t>(data.force.zeroRaw)) *
                              static_cast<int64_t>(data.force.referenceMassMg);
    const int64_t denominator = static_cast<int64_t>(data.force.referenceRaw) -
                                static_cast<int64_t>(data.force.zeroRaw);
    *massMg = numerator / denominator;
    return true;
}

inline bool forceNewtons(const GripperCalibrationData &data, int32_t raw, double *newtons)
{
    if (newtons == nullptr || !isForceCalibrationPayload(data))
    {
        return false;
    }

    constexpr double kStandardGravityMps2 = 9.80665;
    constexpr double kMilligramsPerKilogram = 1'000'000.0;
    const double rawDelta = static_cast<double>(raw) - static_cast<double>(data.force.zeroRaw);
    const double calibrationDelta = static_cast<double>(data.force.referenceRaw) -
                                    static_cast<double>(data.force.zeroRaw);
    const double massMg = rawDelta * static_cast<double>(data.force.referenceMassMg) / calibrationDelta;
    *newtons = (massMg / kMilligramsPerKilogram) * kStandardGravityMps2;
    return true;
}

}  // namespace gripper_hmi

#endif
