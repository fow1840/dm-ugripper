#include "episode_mcap/episode_mcap_client.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ugripper::episode_mcap {
namespace {

bool WriteAll(int fd, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t written = 0;
    while (written < size) {
        const ssize_t rc = ::send(fd, bytes + written, size - written, MSG_NOSIGNAL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (rc == 0) {
            return false;
        }
        written += static_cast<size_t>(rc);
    }
    return true;
}

}  // namespace

EpisodeMcapClient::~EpisodeMcapClient() {
    Close();
}

bool SocketPathLooksEnabled(const std::string& socket_path) {
    std::error_code ec;
    return !socket_path.empty() && std::filesystem::exists(socket_path, ec);
}

std::string ResolveEpisodeSocketPath(const std::string& episode_dir) {
    if (episode_dir.empty()) {
        return "";
    }
    const std::filesystem::path dir(episode_dir);
    const std::filesystem::path marker_path = dir / "episode_mcap_socket_path.txt";
    std::ifstream marker(marker_path);
    std::string socket_path;
    if (marker.good() && std::getline(marker, socket_path)) {
        const auto first = socket_path.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) {
            const auto last = socket_path.find_last_not_of(" \t\r\n");
            return socket_path.substr(first, last - first + 1);
        }
    }
    return (dir / "episode_mcap.sock").string();
}

bool EpisodeMcapClient::Connect(const std::string& socket_path, std::string* error_message) {
    Close();
    if (socket_path.empty()) {
        if (error_message != nullptr) {
            *error_message = "empty episode MCAP socket path";
        }
        return false;
    }
    if (socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        if (error_message != nullptr) {
            *error_message = "episode MCAP socket path too long: " + socket_path;
        }
        return false;
    }

    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        if (error_message != nullptr) {
            *error_message = std::string("socket failed: ") + std::strerror(errno);
        }
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (error_message != nullptr) {
            *error_message = std::string("connect failed: ") + std::strerror(errno);
        }
        Close();
        return false;
    }
    return true;
}

void EpisodeMcapClient::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool EpisodeMcapClient::IsConnected() const {
    return fd_ >= 0;
}

bool EpisodeMcapClient::Send(FrameKind kind,
                             const std::string& topic,
                             const std::string& frame_id,
                             uint64_t log_time_ns,
                             uint64_t publish_time_ns,
                             uint32_t sequence,
                             uint32_t width,
                             uint32_t height,
                             const void* payload,
                             size_t payload_size,
                             std::string* error_message) {
    if (fd_ < 0) {
        if (error_message != nullptr) {
            *error_message = "episode MCAP socket is not connected";
        }
        return false;
    }
    if (topic.size() > std::numeric_limits<uint32_t>::max() ||
        frame_id.size() > std::numeric_limits<uint32_t>::max() ||
        payload_size > std::numeric_limits<uint32_t>::max()) {
        if (error_message != nullptr) {
            *error_message = "episode MCAP frame is too large";
        }
        return false;
    }

    FrameHeader header;
    header.kind = static_cast<uint16_t>(kind);
    header.topic_size = static_cast<uint32_t>(topic.size());
    header.frame_id_size = static_cast<uint32_t>(frame_id.size());
    header.log_time_ns = log_time_ns;
    header.publish_time_ns = publish_time_ns;
    header.sequence = sequence;
    header.width = width;
    header.height = height;
    header.payload_size = static_cast<uint32_t>(payload_size);

    if (!WriteAll(fd_, &header, sizeof(header)) ||
        !WriteAll(fd_, topic.data(), topic.size()) ||
        !WriteAll(fd_, frame_id.data(), frame_id.size()) ||
        (payload_size > 0 && !WriteAll(fd_, payload, payload_size))) {
        if (error_message != nullptr) {
            *error_message = std::string("send failed: ") + std::strerror(errno);
        }
        Close();
        return false;
    }
    return true;
}

bool EpisodeMcapClient::Send(FrameKind kind,
                             const std::string& topic,
                             const std::string& frame_id,
                             uint64_t log_time_ns,
                             uint64_t publish_time_ns,
                             uint32_t sequence,
                             uint32_t width,
                             uint32_t height,
                             const std::vector<uint8_t>& payload,
                             std::string* error_message) {
    return Send(kind,
                topic,
                frame_id,
                log_time_ns,
                publish_time_ns,
                sequence,
                width,
                height,
                payload.data(),
                payload.size(),
                error_message);
}

}  // namespace ugripper::episode_mcap
