#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace DA::utils {
// Stable across processes; normalize the recorder's trailing slash. Runtime-only
// state must never extend the episode file contract. A new session overwrites it
// on its first frame, and /dev/shm is cleared on reboot.
inline std::filesystem::path StereoFrameLossStatePath(std::filesystem::path episode,
                                                     const std::string& camera) {
    episode = episode.lexically_normal();
    if (episode.filename().empty()) episode = episode.parent_path();
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char ch : episode.string() + "/" + camera) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return std::filesystem::path("/dev/shm") /
        ("ugripper_stereo_loss_" + std::to_string(hash) + ".json");
}
inline std::filesystem::path StreamLossResultPath(const std::filesystem::path& episode,
                                                 const std::string& stream) {
    return StereoFrameLossStatePath(episode, "final_loss_" + stream);
}

}
