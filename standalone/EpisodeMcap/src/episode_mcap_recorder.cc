#include "episode_mcap/episode_mcap_protocol.h"
#include "episode_mcap/ros2_cdr_writer.h"

#include <mcap/writer.hpp>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_stop{false};

void SignalHandler(int) {
    g_stop.store(true, std::memory_order_release);
}

bool ReadAll(int fd, void* data, size_t size) {
    auto* bytes = static_cast<uint8_t*>(data);
    size_t read_size = 0;
    while (read_size < size) {
        const ssize_t rc = ::recv(fd, bytes + read_size, size - read_size, 0);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (rc == 0) {
            return false;
        }
        read_size += static_cast<size_t>(rc);
    }
    return true;
}

std::vector<uint8_t> ReadBytes(int fd, size_t size) {
    std::vector<uint8_t> data(size);
    if (size > 0 && !ReadAll(fd, data.data(), data.size())) {
        data.clear();
    }
    return data;
}

std::string BytesToString(std::vector<uint8_t>&& data) {
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

std::string JsonEscape(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    return out.str();
}

class EpisodeWriter {
public:
    bool Open(const fs::path& output_file) {
        output_file_ = output_file;
        mcap::McapWriterOptions options("ugripper-episode-mcap/1.0");
        options.noChunking = false;
        options.chunkSize = 1024 * 1024;
        options.compression = mcap::Compression::None;
        options.compressionLevel = mcap::CompressionLevel::Default;
        options.forceCompression = false;
        options.noRepeatedSchemas = false;
        options.noRepeatedChannels = false;
        options.noMessageIndex = false;
        const auto status = writer_.open(output_file.string(), options);
        if (!status.ok()) {
            std::cerr << "[EpisodeMcapRecorder] failed to open " << output_file
                      << ": " << status.message << std::endl;
            return false;
        }
        return true;
    }

    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        writer_.close();
    }

    bool Write(const ugripper::episode_mcap::FrameHeader& header,
               const std::string& topic,
               const std::string& frame_id,
               const std::vector<uint8_t>& payload) {
        if (topic == "/__ugripper/control/export_stats") {
            std::lock_guard<std::mutex> lock(mutex_);
            return ExportStatsLocked();
        }

        using ugripper::episode_mcap::FrameKind;
        std::vector<std::byte> data;
        std::string schema_key;
        std::string schema_name;
        std::string schema_encoding = "ros2msg";
        std::string channel_encoding = "cdr";
        std::unordered_map<std::string, std::string> metadata;

        const auto kind = static_cast<FrameKind>(header.kind);
        switch (kind) {
        case FrameKind::CompressedImageH264:
            schema_key = "sensor_msgs/msg/CompressedImage";
            schema_name = schema_key;
            metadata["video_format"] = "h264";
            data = ugripper::episode_mcap::EncodeCompressedImage(
                header.log_time_ns,
                frame_id,
                "h264",
                payload.data(),
                payload.size());
            break;
        case FrameKind::CompressedImageH265:
            schema_key = "sensor_msgs/msg/CompressedImage";
            schema_name = schema_key;
            metadata["video_format"] = "h265";
            data = ugripper::episode_mcap::EncodeCompressedImage(
                header.log_time_ns,
                frame_id,
                "h265",
                payload.data(),
                payload.size());
            break;
        case FrameKind::CameraInfo:
            schema_key = "sensor_msgs/msg/CameraInfo";
            schema_name = schema_key;
            if (!payload.empty()) {
                data.resize(payload.size());
                std::memcpy(data.data(), payload.data(), payload.size());
            } else {
                data = ugripper::episode_mcap::EncodeCameraInfo(
                    header.log_time_ns,
                    frame_id,
                    header.width,
                    header.height);
            }
            break;
        case FrameKind::FaysImu:
            if (payload.size() < sizeof(double) * 6) {
                return false;
            } else {
                const auto* values = reinterpret_cast<const double*>(payload.data());
                schema_key = "sensor_msgs/msg/Imu";
                schema_name = schema_key;
                data = ugripper::episode_mcap::EncodeImu(
                    header.log_time_ns,
                    frame_id,
                    values[0],
                    values[1],
                    values[2],
                    values[3],
                    values[4],
                    values[5]);
            }
            break;
        case FrameKind::Json:
            if (topic == "/metadata") {
                schema_key = "ugripper/metadata";
                schema_name = "ugripper.MetadataJson";
            } else if (topic == "/calibration") {
                schema_key = "ugripper/calibration";
                schema_name = "ugripper.CalibrationJson";
            } else {
                schema_key = "ugripper/json";
                schema_name = "ugripper.Json";
            }
            schema_encoding = "jsonschema";
            channel_encoding = "json";
            data.resize(payload.size());
            std::memcpy(data.data(), payload.data(), payload.size());
            break;
        case FrameKind::EncoderBinary:
            schema_key = "ugripper/encoder";
            schema_name = "ugripper.Encoder";
            schema_encoding = "jsonschema";
            channel_encoding = "binary";
            data.resize(payload.size());
            std::memcpy(data.data(), payload.data(), payload.size());
            break;
        default:
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const auto channel_id = EnsureChannel(
            topic,
            schema_key,
            schema_name,
            schema_encoding,
            channel_encoding,
            metadata);
        mcap::Message message;
        message.channelId = channel_id;
        message.sequence = header.sequence;
        message.logTime = header.log_time_ns;
        message.publishTime = header.publish_time_ns != 0 ? header.publish_time_ns : header.log_time_ns;
        message.data = data.data();
        message.dataSize = data.size();
        const auto status = writer_.write(message);
        if (!status.ok()) {
            std::cerr << "[EpisodeMcapRecorder] write failed topic=" << topic
                      << ": " << status.message << std::endl;
            return false;
        }
        UpdateStatsLocked(topic, message.logTime);
        return true;
    }

private:
    struct TopicStats {
        uint64_t count = 0;
        uint64_t first_log_time_ns = 0;
        uint64_t last_log_time_ns = 0;
        uint64_t timestamp_regressions = 0;
    };

    void UpdateStatsLocked(const std::string& topic, uint64_t log_time_ns) {
        auto& stats = topic_stats_[topic];
        if (stats.count == 0) {
            stats.first_log_time_ns = log_time_ns;
        } else if (log_time_ns < stats.last_log_time_ns) {
            ++stats.timestamp_regressions;
        }
        stats.last_log_time_ns = log_time_ns;
        ++stats.count;
    }

    bool ExportStatsLocked() {
        if (output_file_.empty()) {
            return false;
        }
        const fs::path stats_file(output_file_.string() + ".stats.json");
        const fs::path tmp_file(stats_file.string() + ".tmp");
        std::ofstream output(tmp_file, std::ios::trunc);
        if (!output.is_open()) {
            std::cerr << "[EpisodeMcapRecorder] failed to open stats sidecar: " << tmp_file << std::endl;
            return false;
        }
        output << "{\n  \"topics\": {\n";
        bool first = true;
        for (const auto& [topic, stats] : topic_stats_) {
            if (!first) {
                output << ",\n";
            }
            first = false;
            output << "    \"" << JsonEscape(topic) << "\": {"
                   << "\"count\": " << stats.count
                   << ", \"first_log_time_ns\": " << stats.first_log_time_ns
                   << ", \"last_log_time_ns\": " << stats.last_log_time_ns
                   << ", \"timestamp_regressions\": " << stats.timestamp_regressions
                   << "}";
        }
        output << "\n  }\n}\n";
        output.flush();
        if (!output.good()) {
            std::cerr << "[EpisodeMcapRecorder] failed to write stats sidecar: " << tmp_file << std::endl;
            return false;
        }
        output.close();
        std::error_code error;
        fs::rename(tmp_file, stats_file, error);
        if (error) {
            std::cerr << "[EpisodeMcapRecorder] failed to publish stats sidecar: "
                      << stats_file << " error=" << error.message() << std::endl;
            return false;
        }
        return true;
    }

    mcap::ChannelId EnsureChannel(const std::string& topic,
                                  const std::string& schema_key,
                                  const std::string& schema_name,
                                  const std::string& schema_encoding,
                                  const std::string& channel_encoding,
                                  const std::unordered_map<std::string, std::string>& metadata) {
        const std::string channel_key = topic + "\n" + channel_encoding + "\n" + schema_key;
        const auto existing = channel_ids_.find(channel_key);
        if (existing != channel_ids_.end()) {
            return existing->second;
        }

        mcap::SchemaId schema_id = 0;
        const auto schema_it = schema_ids_.find(schema_key);
        if (schema_it != schema_ids_.end()) {
            schema_id = schema_it->second;
        } else {
            std::string schema_data = "{}";
            if (schema_key == "sensor_msgs/msg/CompressedImage") {
                schema_data = ugripper::episode_mcap::CompressedImageSchema();
            } else if (schema_key == "sensor_msgs/msg/CameraInfo") {
                schema_data = ugripper::episode_mcap::CameraInfoSchema();
            } else if (schema_key == "sensor_msgs/msg/Imu") {
                schema_data = ugripper::episode_mcap::ImuSchema();
            } else if (schema_key == "ugripper/encoder") {
                schema_data = R"({"title":"UGripperEncoderBinary","description":"little-endian int32 raw + float32 rad"})";
            } else if (schema_key == "ugripper/metadata") {
                schema_data = R"({"title":"UGripperEpisodeMetadata","type":"object"})";
            } else if (schema_key == "ugripper/calibration") {
                schema_data = R"({"title":"UGripperEpisodeCalibration","type":"object"})";
            }

            mcap::Schema schema(schema_name, schema_encoding, schema_data);
            writer_.addSchema(schema);
            schema_id = schema.id;
            schema_ids_[schema_key] = schema_id;
        }

        mcap::Channel channel(topic, channel_encoding, schema_id);
        channel.metadata = metadata;
        writer_.addChannel(channel);
        channel_ids_[channel_key] = channel.id;
        return channel.id;
    }

    fs::path output_file_;
    mcap::McapWriter writer_;
    std::mutex mutex_;
    std::map<std::string, mcap::SchemaId> schema_ids_;
    std::map<std::string, mcap::ChannelId> channel_ids_;
    std::map<std::string, TopicStats> topic_stats_;
};

void ClientThread(int fd, EpisodeWriter* writer) {
    while (!g_stop.load(std::memory_order_acquire)) {
        ugripper::episode_mcap::FrameHeader header;
        if (!ReadAll(fd, &header, sizeof(header))) {
            break;
        }
        if (header.magic != ugripper::episode_mcap::kFrameMagic ||
            header.version != ugripper::episode_mcap::kFrameVersion) {
            break;
        }
        std::string topic = BytesToString(ReadBytes(fd, header.topic_size));
        std::string frame_id = BytesToString(ReadBytes(fd, header.frame_id_size));
        std::vector<uint8_t> payload = ReadBytes(fd, header.payload_size);
        if (topic.size() != header.topic_size ||
            frame_id.size() != header.frame_id_size ||
            payload.size() != header.payload_size) {
            break;
        }
        if (!writer->Write(header, topic, frame_id, payload)) {
            break;
        }
    }
    ::close(fd);
}

int ListenSocket(const fs::path& socket_path) {
    ::unlink(socket_path.c_str());
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::string path = socket_path.string();
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return -1;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(fd, 32) != 0) {
        ::close(fd);
        return -1;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return fd;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path output_file;
    fs::path socket_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: EpisodeMcapRecorder --output episode.mcap --socket episode_mcap.sock\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << std::endl;
            return 2;
        }
    }
    if (output_file.empty() || socket_path.empty()) {
        std::cerr << "EpisodeMcapRecorder requires --output and --socket" << std::endl;
        return 2;
    }

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    if (!output_file.parent_path().empty()) {
        fs::create_directories(output_file.parent_path());
    }
    if (!socket_path.parent_path().empty()) {
        fs::create_directories(socket_path.parent_path());
    }

    EpisodeWriter writer;
    if (!writer.Open(output_file)) {
        return 1;
    }
    const int listen_fd = ListenSocket(socket_path);
    if (listen_fd < 0) {
        std::cerr << "failed to listen on socket " << socket_path
                  << ": " << std::strerror(errno) << std::endl;
        return 1;
    }

    std::vector<std::thread> clients;
    while (!g_stop.load(std::memory_order_acquire)) {
        sockaddr_un peer{};
        socklen_t peer_len = sizeof(peer);
        const int fd = ::accept4(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (g_stop.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        clients.emplace_back(ClientThread, fd, &writer);
    }

    ::close(listen_fd);
    for (auto& thread : clients) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    writer.Close();
    ::unlink(socket_path.c_str());
    return 0;
}
