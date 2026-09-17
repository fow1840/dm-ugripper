#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace DA::Ugripper {
struct BatterySample {
    int percent = 0;
    int voltage_mv = 0;
    int current_ma = 0;
    int capacity_mah = 0;
    int remaining_mah = 0;
    bool charging = false;
    bool discharging = false;
    uint64_t received_ms = 0;
};
// Wire layout from tmp/bms; integers are little-endian, independent of host ABI.
class BmsParser {
public:
    std::optional<BatterySample> Feed(uint8_t byte);
private:
    std::vector<uint8_t> bytes_;
};
class BmsReader {
public:
    explicit BmsReader(std::string port = "/dev/ttyS0",
                       std::string snapshot = "/dev/shm/ugripper/battery_status.json");
    ~BmsReader();
    std::optional<BatterySample> Sample() const;
private:
    void Run();
    void Publish();
    std::string port_, snapshot_;
    mutable std::mutex mutex_;
    std::optional<BatterySample> sample_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
uint64_t BatterySteadyMs();
}
