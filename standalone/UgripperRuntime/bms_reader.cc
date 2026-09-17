#include "record_runtime/bms_reader.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <termios.h>
#include <unistd.h>

namespace DA::Ugripper {
uint64_t BatterySteadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::optional<BatterySample> BmsParser::Feed(uint8_t byte) {
    bytes_.push_back(byte);
    while (!bytes_.empty()) {
        if (bytes_[0] != 0x68 || (bytes_.size() > 1 && bytes_[1] != 0x5a) ||
            (bytes_.size() > 2 && bytes_[2] != 0xa5)) {
            bytes_.erase(bytes_.begin());
            continue;
        }
        if (bytes_.size() < 90) return {};
        uint8_t checksum = 0;
        for (size_t i = 0; i < 88; ++i) checksum ^= bytes_[i];
        const auto u16 = [&](size_t i) { return int(bytes_[i]) | (int(bytes_[i+1]) << 8); };
        if (bytes_[3] != 0 || bytes_[89] != 0x16 || checksum != bytes_[88] ||
            u16(45) > 100 || bytes_[71] == 0 || bytes_[71] > 16) {
            bytes_.erase(bytes_.begin());
            continue;
        }
        BatterySample sample;
        sample.percent = u16(45);
        sample.capacity_mah = u16(41) * 10;
        sample.remaining_mah = u16(43) * 10;
        uint32_t current = 0;
        for (int i = 0; i < 4; ++i) current |= uint32_t(bytes_[47+i]) << (8*i);
        sample.current_ma = static_cast<int32_t>(current);
        sample.charging = (u16(65) & (1 << 12)) != 0;
        sample.discharging = (u16(65) & (1 << 11)) != 0;
        for (int i = 0; i < bytes_[71]; ++i) sample.voltage_mv += u16(4+2*i);
        bytes_.erase(bytes_.begin(), bytes_.begin()+90);
        return sample;
    }
    return {};
}
BmsReader::BmsReader(std::string port, std::string snapshot)
    : port_(std::move(port)), snapshot_(std::move(snapshot)), thread_(&BmsReader::Run, this) {}
BmsReader::~BmsReader() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    { std::lock_guard<std::mutex> lock(mutex_); sample_.reset(); }
    Publish();
}
std::optional<BatterySample> BmsReader::Sample() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sample_ || BatterySteadyMs() - sample_->received_ms > 5000) return {};
    return sample_;
}
void BmsReader::Publish() {
    try {
        const auto sample = Sample();
        const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        nlohmann::json data{{"connected", sample.has_value()}, {"updated_at_ms", epoch}, {"port", port_}};
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
    int fd = -1;
    termios original{};
    bool restore = false;
    BmsParser parser;
    uint64_t retry = 0, request = 0, publish = 0, opened = 0, received = 0;
    const auto closePort = [&] {
        if (fd >= 0) {
            if (restore) tcsetattr(fd, TCSANOW, &original);
            close(fd);
        }
        fd = -1;
        restore = false;
        parser = BmsParser{};
        std::lock_guard<std::mutex> lock(mutex_);
        sample_.reset();
    };
    while (!stop_) {
        const auto now = BatterySteadyMs();
        if (fd < 0 && now >= retry) {
            retry = now + 3000;
            fd = open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0) {
                if (flock(fd, LOCK_EX | LOCK_NB) != 0 || tcgetattr(fd, &original) != 0) closePort();
                else {
                    auto config = original;
                    cfmakeraw(&config);
                    config.c_cflag = CLOCAL | CREAD | CS8;
                    config.c_cc[VMIN] = 0; config.c_cc[VTIME] = 0;
                    cfsetispeed(&config, B9600); cfsetospeed(&config, B9600);
                    restore = true;
                    if (tcsetattr(fd, TCSANOW, &config) != 0) closePort();
                    else { opened = now; received = 0; request = 0; }
                }
            }
        }
        if (fd >= 0) {
            if (now >= request) {
                constexpr uint8_t command[]{0x68,0x0c,0xf3,0,0,0,0,0,0,0,0x97,0x16};
                if (write(fd, command, sizeof(command)) != sizeof(command)) closePort();
                request = now + 1000;
            }
            if (fd >= 0) {
                pollfd poller{fd, POLLIN, 0};
                if (poll(&poller, 1, 100) > 0) {
                    if (poller.revents & (POLLERR | POLLHUP | POLLNVAL)) closePort();
                    else if (poller.revents & POLLIN) {
                        uint8_t buffer[512];
                        const auto count = read(fd, buffer, sizeof(buffer));
                        for (ssize_t i = 0; i < count; ++i) {
                            if (auto sample = parser.Feed(buffer[i])) {
                                received = sample->received_ms = BatterySteadyMs();
                                std::lock_guard<std::mutex> lock(mutex_);
                                sample_ = *sample;
                            }
                        }
                    }
                }
                if (now > (received ? received : opened) + 5000) closePort();
            }
        } else std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (now >= publish) { Publish(); publish = now + 1000; }
    }
    closePort();
}
}
