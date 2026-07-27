#pragma once

#include <cstdint>

namespace ugripper::sensor {

enum class ForceSide : uint8_t
{
    Left = 0,
    Right = 1,
};

#pragma pack(push, 1)
struct ForceIpcSample
{
    uint8_t side = 0;
    uint8_t reserved[7] = {};
    uint64_t timestamp_ns = 0;
    double force_newtons = 0.0;
};
#pragma pack(pop)

static_assert(sizeof(ForceIpcSample) == 24, "ForceIpcSample layout changed");

struct ForceFrame
{
    double force_newtons = 0.0;
};

static_assert(sizeof(ForceFrame) == sizeof(double), "ForceFrame layout changed");

}  // namespace ugripper::sensor
