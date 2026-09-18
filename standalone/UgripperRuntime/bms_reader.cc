#include "record_runtime/bms_reader.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <climits>
#include <map>
#include <sstream>

namespace DA::Ugripper {
uint64_t BatterySteadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::optional<BatterySample> ParseBmsSnapshot(const std::string& text, int64_t unix_ms) {
    if (text.size() > 4096) return {};
    std::map<std::string, int64_t> values;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto key = line.substr(0, colon);
        if (key != "timestamp" && key != "soc_percent" && key != "totalCapacity_mAh" &&
            key != "remainCapacity_mAh" && key != "totalVoltage_mV" &&
            key != "current_mA" && key != "isCharging" && key != "isDischarging") continue;
        std::istringstream number(line.substr(colon + 1));
        int64_t value;
        if (!(number >> value) || !(number >> std::ws).eof() || values.count(key)) return {};
        values[key] = value;
    }
    for (const auto* key : {"timestamp", "soc_percent", "totalCapacity_mAh", "remainCapacity_mAh",
                            "totalVoltage_mV", "current_mA", "isCharging", "isDischarging"}) {
        if (!values.count(key)) return {};
    }
    // Source timestamp has second resolution. Never refresh old data merely by rereading it.
    const auto stamp = values["timestamp"];
    if (stamp <= 0 || stamp > unix_ms / 1000 || unix_ms - stamp * 1000 > 20000) return {};
    for (const auto* key : {"soc_percent", "totalCapacity_mAh", "remainCapacity_mAh", "totalVoltage_mV"}) {
        if (values[key] < 0 || values[key] > INT_MAX) return {};
    }
    if (values["soc_percent"] > 100 || values["current_mA"] < INT_MIN || values["current_mA"] > INT_MAX ||
        values["isCharging"] < 0 || values["isCharging"] > 1 ||
        values["isDischarging"] < 0 || values["isDischarging"] > 1) return {};
    BatterySample sample;
    sample.percent = values["soc_percent"];
    sample.capacity_mah = values["totalCapacity_mAh"];
    sample.remaining_mah = values["remainCapacity_mAh"];
    sample.voltage_mv = values["totalVoltage_mV"];
    sample.current_ma = values["current_mA"];
    sample.charging = values["isCharging"] != 0;
    sample.discharging = values["isDischarging"] != 0;
    const auto age = static_cast<uint64_t>(unix_ms - stamp * 1000);
    const auto steady = BatterySteadyMs();
    sample.received_ms = steady - std::min(steady, age);
    return sample;
}
BmsReader::BmsReader(std::string source, std::string snapshot)
    : source_(std::move(source)), snapshot_(std::move(snapshot)), thread_(&BmsReader::Run, this) {}
BmsReader::~BmsReader() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    { std::lock_guard<std::mutex> lock(mutex_); sample_.reset(); }
    Publish();
}
std::optional<BatterySample> BmsReader::Sample() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sample_ || BatterySteadyMs() - sample_->received_ms > 20000) return {};
    return sample_;
}
void BmsReader::Publish() {
    try {
        const auto sample = Sample();
        const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        nlohmann::json data{{"connected", sample.has_value()}, {"updated_at_ms", epoch}, {"source", source_}};
        data["status"] = sample ? "ready" : "service_unavailable";
        if (sample) {
            data["percent"] = sample->percent;
            data["charging"] = sample->charging;
            data["discharging"] = sample->discharging;
            data["voltage_mv"] = sample->voltage_mv;
            data["current_ma"] = sample->current_ma;
            data["capacity_mah"] = sample->capacity_mah;
            data["remaining_mah"] = sample->remaining_mah;
            data["sample_age_ms"] = BatterySteadyMs() - sample->received_ms;
        }
        std::filesystem::create_directories(std::filesystem::path(snapshot_).parent_path());
        const std::string temporary = snapshot_ + ".tmp";
        std::ofstream file(temporary);
        file << data.dump() << '\n';
        file.close();
        if (file) std::filesystem::rename(temporary, snapshot_);
    } catch (const std::exception&) { /* Battery telemetry must not stop collection. */ }
}
void BmsReader::Run() {
    while (!stop_) {
        std::optional<BatterySample> sample;
        // Bounded read also tolerates a producer truncating/replacing the snapshot.
        std::ifstream file(source_);
        if (file) {
            char buffer[4097];
            file.read(buffer, sizeof(buffer));
            if (!file.bad() && file.gcount() <= 4096) {
                const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                sample = ParseBmsSnapshot(std::string(buffer, file.gcount()), epoch);
            }
        }
        { std::lock_guard<std::mutex> lock(mutex_); sample_ = sample; }
        Publish();
        for (int i = 0; i < 10 && !stop_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
}
