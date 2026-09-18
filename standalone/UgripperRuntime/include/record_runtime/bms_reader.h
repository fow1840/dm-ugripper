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
// Read the Bluetooth service's text snapshot; never open its serial device.
std::optional<BatterySample> ParseBmsSnapshot(const std::string& text, int64_t unix_ms);
class BmsReader {
public:
    explicit BmsReader(std::string source = "/opt/bluetooth_server/bms_latest.txt",
                       std::string snapshot = "/dev/shm/ugripper/battery_status.json");
    ~BmsReader();
    std::optional<BatterySample> Sample() const;
private:
    void Run();
    void Publish();
    std::string source_, snapshot_;
    mutable std::mutex mutex_;
    std::optional<BatterySample> sample_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
uint64_t BatterySteadyMs();
}
