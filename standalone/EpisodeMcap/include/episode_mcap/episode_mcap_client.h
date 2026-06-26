#pragma once

#include "episode_mcap/episode_mcap_protocol.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ugripper::episode_mcap {

class EpisodeMcapClient {
public:
    EpisodeMcapClient() = default;
    ~EpisodeMcapClient();

    EpisodeMcapClient(const EpisodeMcapClient&) = delete;
    EpisodeMcapClient& operator=(const EpisodeMcapClient&) = delete;

    bool Connect(const std::string& socket_path, std::string* error_message);
    void Close();
    bool IsConnected() const;

    bool Send(FrameKind kind,
              const std::string& topic,
              const std::string& frame_id,
              uint64_t log_time_ns,
              uint64_t publish_time_ns,
              uint32_t sequence,
              uint32_t width,
              uint32_t height,
              const void* payload,
              size_t payload_size,
              std::string* error_message);

    bool Send(FrameKind kind,
              const std::string& topic,
              const std::string& frame_id,
              uint64_t log_time_ns,
              uint64_t publish_time_ns,
              uint32_t sequence,
              uint32_t width,
              uint32_t height,
              const std::vector<uint8_t>& payload,
              std::string* error_message);

private:
    int fd_ = -1;
};

bool SocketPathLooksEnabled(const std::string& socket_path);
std::string ResolveEpisodeSocketPath(const std::string& episode_dir);

}  // namespace ugripper::episode_mcap
