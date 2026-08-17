#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr unsigned char kSecretMask = 0xa7;
constexpr std::array<unsigned char, 13> kTargetHost = {
    0x96, 0x9e, 0x95, 0x89, 0x96, 0x91, 0x9f,
    0x89, 0x95, 0x89, 0x95, 0x93, 0x97,
};
constexpr std::array<unsigned char, 6> kTargetUser = {
    0xd2, 0xc5, 0xd2, 0xc9, 0xd3, 0xd2,
};
constexpr std::array<unsigned char, 6> kTargetPassword = {
    0xd2, 0xc5, 0xd2, 0xc9, 0xd3, 0xd2,
};
constexpr std::array<unsigned char, 16> kAskpassPepper = {
    0x35, 0x8c, 0x71, 0xd2, 0x19, 0xa6, 0x4f, 0xe3,
    0x92, 0x0b, 0xc5, 0x67, 0xfa, 0x44, 0x1d, 0xb8,
};

constexpr std::array<std::string_view, 5> kPackageOrder = {
    "ugripper",
    "das-usb-updater",
    "bluetooth-gatt-server",
    "databot-device-joint",
    "device-ota-mender",
};

struct CommandResult {
    int exit_code = 127;
    std::string output;
};

struct PackageInfo {
    fs::path path;
    std::string name;
    std::string version;
    std::string architecture;
};

template <std::size_t N>
std::string decodeSecret(const std::array<unsigned char, N>& encoded) {
    std::string value;
    value.reserve(N);
    for (unsigned char byte : encoded) {
        value.push_back(static_cast<char>(byte ^ kSecretMask));
    }
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

void replaceAll(std::string& value, const std::string& needle,
                const std::string& replacement) {
    if (needle.empty()) {
        return;
    }
    std::size_t position = 0;
    while ((position = value.find(needle, position)) != std::string::npos) {
        value.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
}

std::string sanitizeOutput(std::string value) {
    replaceAll(value, decodeSecret(kTargetHost), "[target]");
    replaceAll(value, decodeSecret(kTargetUser), "[user]");
    replaceAll(value, decodeSecret(kTargetPassword), "[secret]");
    return value;
}

std::string executablePath() {
    std::array<char, 4096> buffer{};
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size <= 0) {
        throw std::runtime_error("无法确定当前可执行文件路径");
    }
    buffer[static_cast<std::size_t>(size)] = '\0';
    return std::string(buffer.data());
}

uint64_t askpassProof(std::string_view nonce) {
    uint64_t hash = 1469598103934665603ULL;
    const auto update = [&hash](unsigned char value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (unsigned char value : nonce) {
        update(value);
    }
    for (unsigned char value : kAskpassPepper) {
        update(value);
    }
    return hash;
}

std::string hexValue(uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}

std::string randomToken() {
    std::array<unsigned char, 16> bytes{};
    std::ifstream random("/dev/urandom", std::ios::binary);
    if (random.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
        std::ostringstream output;
        for (unsigned char byte : bytes) {
            output << std::hex << std::setfill('0') << std::setw(2)
                   << static_cast<unsigned int>(byte);
        }
        return output.str();
    }

    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return hexValue(static_cast<uint64_t>(now) ^ static_cast<uint64_t>(getpid()));
}

bool handleAskpassMode() {
    const char* nonce = std::getenv("UGRIPPER_FLASH_ASKPASS_NONCE");
    const char* proof = std::getenv("UGRIPPER_FLASH_ASKPASS_PROOF");
    if (nonce == nullptr || proof == nullptr) {
        return false;
    }

    if (hexValue(askpassProof(nonce)) != proof) {
        return false;
    }

    std::cout << decodeSecret(kTargetPassword) << std::endl;
    return true;
}

CommandResult runCommand(const std::vector<std::string>& arguments,
                         const std::string& standard_input = {},
                         bool enable_askpass = false,
                         const std::string& askpass_nonce = {}) {
    if (arguments.empty()) {
        throw std::runtime_error("内部错误：空命令");
    }

    int output_pipe[2] = {-1, -1};
    int input_pipe[2] = {-1, -1};
    if (pipe(output_pipe) != 0 || pipe(input_pipe) != 0) {
        throw std::runtime_error("无法创建子进程管道");
    }

    const pid_t child = fork();
    if (child < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        close(input_pipe[0]);
        close(input_pipe[1]);
        throw std::runtime_error("无法启动子进程");
    }

    if (child == 0) {
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);

        if (enable_askpass) {
            setsid();
            const std::string path = executablePath();
            const std::string proof = hexValue(askpassProof(askpass_nonce));
            setenv("SSH_ASKPASS", path.c_str(), 1);
            setenv("SSH_ASKPASS_REQUIRE", "force", 1);
            setenv("DISPLAY", ":0", 1);
            setenv("UGRIPPER_FLASH_ASKPASS_NONCE", askpass_nonce.c_str(), 1);
            setenv("UGRIPPER_FLASH_ASKPASS_PROOF", proof.c_str(), 1);
        }

        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const std::string& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(input_pipe[0]);
    close(output_pipe[1]);

    std::size_t written = 0;
    while (written < standard_input.size()) {
        const ssize_t count = write(input_pipe[1], standard_input.data() + written,
                                    standard_input.size() - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    close(input_pipe[1]);

    std::string output;
    std::array<char, 8192> buffer{};
    while (true) {
        const ssize_t count = read(output_pipe[0], buffer.data(), buffer.size());
        if (count > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    close(output_pipe[0]);

    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }

    CommandResult result;
    result.output = std::move(output);
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

bool executableExists(const std::string& name) {
    const char* raw_path = std::getenv("PATH");
    if (raw_path == nullptr) {
        return false;
    }
    std::istringstream paths(raw_path);
    std::string directory;
    while (std::getline(paths, directory, ':')) {
        const fs::path candidate = fs::path(directory.empty() ? "." : directory) / name;
        if (access(candidate.c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
}

bool isWhitelisted(std::string_view package) {
    return std::find(kPackageOrder.begin(), kPackageOrder.end(), package) !=
           kPackageOrder.end();
}

std::string readDebField(const fs::path& path, const std::string& field) {
    const CommandResult result = runCommand({"dpkg-deb", "-f", path.string(), field});
    if (result.exit_code != 0) {
        throw std::runtime_error("无法读取 deb 元数据：" + path.filename().string());
    }
    const std::string value = trim(result.output);
    if (value.empty() || value.find_first_of("\r\n\t") != std::string::npos) {
        throw std::runtime_error("deb 元数据非法：" + path.filename().string());
    }
    return value;
}

bool isSafeFileName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '.' || character == '_' ||
               character == '+' || character == '-' || character == '~';
    });
}

bool versionGreater(const std::string& candidate, const std::string& current) {
    return runCommand({"dpkg", "--compare-versions", candidate, "gt", current}).exit_code == 0;
}

std::vector<PackageInfo> discoverPackages(const fs::path& directory) {
    std::map<std::string, PackageInfo> selected;
    std::vector<std::string> ignored;

    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".deb") {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        if (!isSafeFileName(filename)) {
            throw std::runtime_error("deb 文件名包含不支持的字符：" + filename);
        }

        PackageInfo info;
        info.path = fs::absolute(entry.path());
        info.name = readDebField(info.path, "Package");
        info.version = readDebField(info.path, "Version");
        info.architecture = readDebField(info.path, "Architecture");

        if (!isWhitelisted(info.name)) {
            ignored.push_back(filename + " (" + info.name + ")");
            continue;
        }
        if (info.architecture != "arm64" && info.architecture != "all") {
            throw std::runtime_error("白名单包架构不是 arm64/all：" + filename +
                                     " (" + info.architecture + ")");
        }

        const auto current = selected.find(info.name);
        if (current == selected.end() || versionGreater(info.version, current->second.version)) {
            selected[info.name] = std::move(info);
        }
    }

    for (const std::string& item : ignored) {
        std::cout << "[SKIP] 非白名单 deb：" << item << '\n';
    }

    std::vector<PackageInfo> packages;
    for (const auto package_name : kPackageOrder) {
        const auto iterator = selected.find(std::string(package_name));
        if (iterator != selected.end()) {
            packages.push_back(iterator->second);
        }
    }
    return packages;
}

std::string shellQuote(const std::string& value) {
    std::string quoted = "'";
    for (char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

class SshContext {
public:
    SshContext() : nonce_(randomToken()), alias_("device_" + nonce_.substr(0, 12)) {
        const std::string host = decodeSecret(kTargetHost);
        const std::string user = decodeSecret(kTargetUser);
        std::ostringstream config;
        config << "Host " << alias_ << "\n"
               << "  HostName " << host << "\n"
               << "  User " << user << "\n"
               << "  Port 22\n"
               << "  ConnectTimeout 15\n"
               << "  ConnectionAttempts 1\n"
               << "  ServerAliveInterval 10\n"
               << "  ServerAliveCountMax 2\n"
               << "  PreferredAuthentications password,keyboard-interactive\n"
               << "  PasswordAuthentication yes\n"
               << "  KbdInteractiveAuthentication yes\n"
               << "  PubkeyAuthentication no\n"
               << "  IdentitiesOnly yes\n"
               << "  StrictHostKeyChecking no\n"
               << "  UserKnownHostsFile /dev/null\n"
               << "  LogLevel ERROR\n";

        char pattern[] = "/dev/shm/.ugripper-flash-XXXXXX";
        config_fd_ = mkstemp(pattern);
        if (config_fd_ < 0) {
            throw std::runtime_error("无法创建临时 SSH 配置");
        }
        config_path_ = pattern;
        fchmod(config_fd_, S_IRUSR | S_IWUSR);
        const std::string text = config.str();
        config_size_ = text.size();
        std::size_t offset = 0;
        while (offset < text.size()) {
            const ssize_t count = write(config_fd_, text.data() + offset, text.size() - offset);
            if (count > 0) {
                offset += static_cast<std::size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR) {
                continue;
            }
            close(config_fd_);
            config_fd_ = -1;
            unlink(config_path_.c_str());
            throw std::runtime_error("无法写入临时 SSH 配置");
        }
    }

    ~SshContext() {
        if (config_fd_ >= 0) {
            lseek(config_fd_, 0, SEEK_SET);
            const std::string zeros(config_size_, '\0');
            std::size_t offset = 0;
            while (offset < zeros.size()) {
                const ssize_t count =
                    write(config_fd_, zeros.data() + offset, zeros.size() - offset);
                if (count > 0) {
                    offset += static_cast<std::size_t>(count);
                    continue;
                }
                if (count < 0 && errno == EINTR) {
                    continue;
                }
                break;
            }
            fsync(config_fd_);
            close(config_fd_);
        }
        if (!config_path_.empty()) {
            unlink(config_path_.c_str());
        }
    }

    SshContext(const SshContext&) = delete;
    SshContext& operator=(const SshContext&) = delete;

    CommandResult ssh(const std::string& remote_command,
                      const std::string& input = {}) const {
        lseek(config_fd_, 0, SEEK_SET);
        return runCommand({"ssh", "-F", configPath(), alias_, remote_command}, input,
                          true, nonce_);
    }

    CommandResult scp(const std::vector<PackageInfo>& packages,
                      const std::string& remote_directory) const {
        lseek(config_fd_, 0, SEEK_SET);
        std::vector<std::string> arguments = {"scp", "-F", configPath(), "--"};
        for (const PackageInfo& package : packages) {
            arguments.push_back(package.path.string());
        }
        arguments.push_back(alias_ + ":" + remote_directory + "/");
        return runCommand(arguments, {}, true, nonce_);
    }

private:
    std::string configPath() const {
        return config_path_;
    }

    int config_fd_ = -1;
    std::size_t config_size_ = 0;
    std::string config_path_;
    std::string nonce_;
    std::string alias_;
};

void requireSuccess(const CommandResult& result, const std::string& context) {
    if (result.exit_code == 0) {
        return;
    }
    const std::string detail = trim(sanitizeOutput(result.output));
    throw std::runtime_error(context + "失败" +
                             (detail.empty() ? std::string{} : "：\n" + detail));
}

std::map<std::string, std::string> queryInstalledVersions(const SshContext& ssh) {
    std::ostringstream command;
    command << "for pkg in";
    for (const std::string_view package : kPackageOrder) {
        command << " " << shellQuote(std::string(package));
    }
    command << R"SH(; do
  status="$(dpkg-query -W -f='${Status}' -- "$pkg" 2>/dev/null || true)"
  version=""
  if [ "$status" = "install ok installed" ]; then
    version="$(dpkg-query -W -f='${Version}' -- "$pkg")"
  fi
  printf '__UGRIPPER_PKG__\t%s\t%s\n' "$pkg" "$version"
done
)SH";

    const CommandResult result = ssh.ssh(command.str());
    requireSuccess(result, "读取目标机软件版本");

    std::map<std::string, std::string> versions;
    std::istringstream lines(result.output);
    std::string line;
    constexpr std::string_view marker = "__UGRIPPER_PKG__\t";
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.compare(0, marker.size(), marker) != 0) {
            continue;
        }
        const std::size_t separator = line.find('\t', marker.size());
        if (separator == std::string::npos) {
            throw std::runtime_error("目标机软件版本返回格式错误");
        }
        const std::string package = line.substr(marker.size(), separator - marker.size());
        const std::string version = line.substr(separator + 1);
        if (!isWhitelisted(package) || version.find_first_of("\r\n\t") != std::string::npos ||
            !versions.emplace(package, version).second) {
            throw std::runtime_error("目标机软件版本返回内容非法");
        }
    }

    if (versions.size() != kPackageOrder.size()) {
        throw std::runtime_error("目标机未返回完整的白名单软件版本");
    }
    return versions;
}

int compareVersions(const std::string& local, const std::string& installed) {
    if (runCommand({"dpkg", "--compare-versions", local, "gt", installed}).exit_code == 0) {
        return 1;
    }
    if (runCommand({"dpkg", "--compare-versions", local, "lt", installed}).exit_code == 0) {
        return -1;
    }
    if (runCommand({"dpkg", "--compare-versions", local, "eq", installed}).exit_code == 0) {
        return 0;
    }
    throw std::runtime_error("无法比较软件版本：" + local + " / " + installed);
}

std::string upgradeStatus(const PackageInfo* local, const std::string& installed) {
    if (local == nullptr) {
        return "无本地包";
    }
    if (installed.empty()) {
        return "新安装";
    }
    const int comparison = compareVersions(local->version, installed);
    if (comparison > 0) {
        return "升级";
    }
    if (comparison < 0) {
        return "降级";
    }
    return "重装";
}

std::string buildRemoteInstallScript(const std::vector<PackageInfo>& packages,
                                     const std::string& remote_directory) {
    std::ostringstream script;
    script << "set -euo pipefail\n"
           << "REMOTE_DIR=" << shellQuote(remote_directory) << "\n"
           << "cleanup() { rm -rf -- \"$REMOTE_DIR\"; }\n"
           << "trap cleanup EXIT\n"
           << "target_arch=\"$(dpkg --print-architecture)\"\n"
           << "target_machine=\"$(uname -m)\"\n"
           << "[ \"$target_arch\" = arm64 ] || { echo '[INSTALL] fail: target is not arm64'; exit 30; }\n"
           << "case \"$target_machine\" in aarch64|arm64) ;; *) echo '[INSTALL] fail: target machine is not ARM64'; exit 31 ;; esac\n"
           << "APT_DPKG_LOCK_TIMEOUT_SECONDS=300\n"
           << R"SH(wait_for_apt_dpkg_locks() {
  local reason="${1:-apt/dpkg}"
  local start now elapsed holders lock pids
  local locks=(
    /var/lib/dpkg/lock-frontend
    /var/lib/dpkg/lock
    /var/cache/apt/archives/lock
    /var/lib/apt/lists/lock
  )
  if ! command -v fuser >/dev/null 2>&1; then
    echo "[INSTALL] WARN fuser not found; skip apt/dpkg lock pre-wait for $reason"
    return 0
  fi
  start="$(date +%s)"
  while true; do
    holders=""
    for lock in "${locks[@]}"; do
      if [ -e "$lock" ] && fuser -s "$lock" 2>/dev/null; then
        pids="$(fuser "$lock" 2>/dev/null | tr -s ' ' ',' | sed 's/^,//;s/,$//')"
        holders="${holders} $lock:${pids:-unknown}"
      fi
    done
    if [ -z "$holders" ]; then
      return 0
    fi
    now="$(date +%s)"
    elapsed=$((now - start))
    if [ "$elapsed" -ge "$APT_DPKG_LOCK_TIMEOUT_SECONDS" ]; then
      echo "[INSTALL] fail: apt/dpkg lock still held after ${APT_DPKG_LOCK_TIMEOUT_SECONDS}s for $reason:$holders"
      ps -eo pid,ppid,stat,cmd | grep -E 'apt|apt-get|dpkg|packagekit|unattended' | grep -v grep || true
      return 32
    fi
    echo "[INSTALL] WAIT apt/dpkg lock for $reason (${elapsed}s):$holders"
    sleep 2
  done
}
installed_package_version() {
  local pkg="$1"
  local status version
  IFS="$(printf '\t')" read -r status version < <(dpkg-query -W -f='${Status}\t${Version}' "$pkg" 2>/dev/null || true)
  if [ "$status" = "install ok installed" ] && [ -n "$version" ]; then
    printf '%s' "$version"
  fi
}
)SH";

    script << "install_args=(\n";
    for (const PackageInfo& package : packages) {
        script << "  " << shellQuote(remote_directory + "/" + package.path.filename().string())
               << "\n";
    }
    script << ")\nexpected_names=(\n";
    for (const PackageInfo& package : packages) {
        script << "  " << shellQuote(package.name) << "\n";
    }
    script << ")\nexpected_versions=(\n";
    for (const PackageInfo& package : packages) {
        script << "  " << shellQuote(package.version) << "\n";
    }
    script << ")\nexpected_arches=(\n";
    for (const PackageInfo& package : packages) {
        script << "  " << shellQuote(package.architecture) << "\n";
    }
    script << R"SH()
for i in "${!install_args[@]}"; do
  deb="${install_args[$i]}"
  actual_name="$(dpkg-deb -f "$deb" Package)"
  actual_version="$(dpkg-deb -f "$deb" Version)"
  actual_arch="$(dpkg-deb -f "$deb" Architecture)"
  if [ "$actual_name" != "${expected_names[$i]}" ] || [ "$actual_version" != "${expected_versions[$i]}" ]; then
    echo "[INSTALL] fail: uploaded deb metadata mismatch: $(basename "$deb")"
    exit 33
  fi
  if [ "$actual_arch" != "${expected_arches[$i]}" ]; then
    echo "[INSTALL] fail: uploaded deb architecture changed: $(basename "$deb")"
    exit 34
  fi
  case "$actual_arch" in arm64|all) ;; *) echo "[INSTALL] fail: unsupported deb architecture: $actual_arch"; exit 35 ;; esac
done

has_das=0
for pkg in "${expected_names[@]}"; do
  [ "$pkg" = das-usb-updater ] && has_das=1
done
if [ "$has_das" -eq 1 ]; then
  legacy_status="$(dpkg-query -W -f='${Status}' ugripper-usb-updater 2>/dev/null || true)"
  if [ "$legacy_status" = "install ok installed" ]; then
    echo "[INSTALL] remove legacy updater before das-usb-updater"
    wait_for_apt_dpkg_locks "remove legacy updater"
    dpkg -r ugripper-usb-updater
  fi
fi

echo "[INSTALL] installing ${#install_args[@]} package(s)"
wait_for_apt_dpkg_locks "dpkg install application packages"
dpkg --auto-deconfigure -i --force-overwrite "${install_args[@]}"
wait_for_apt_dpkg_locks "apt-get fix dependencies"
apt-get -o DPkg::Lock::Timeout=300 -f install --no-download -y

for i in "${!install_args[@]}"; do
  pkg="${expected_names[$i]}"
  expected="${expected_versions[$i]}"
  installed="$(installed_package_version "$pkg")"
  if [ "$installed" != "$expected" ]; then
    echo "[INSTALL] fail: $pkg expected=$expected installed=${installed:-not-installed}"
    exit 21
  fi
  echo "[INSTALL] verified $pkg=$installed"
  if [ "$pkg" = device-ota-mender ]; then
    echo "[MENDER] running mender-offline-init --force-install-module..."
    if command -v mender-offline-init >/dev/null 2>&1; then
      echo "[MENDER] wait 2s before mender-offline-init --force-install-module..."
      sleep 2
      wait_for_apt_dpkg_locks "mender-offline-init"
      set +e
      mender-offline-init --force-install-module 2>&1 | sed 's/^/[MENDER] /'
      mender_rc="${PIPESTATUS[0]}"
      set -e
      if [ "$mender_rc" -ne 0 ]; then
        echo "[MENDER] fail: mender-offline-init --force-install-module exit=$mender_rc"
        exit "$mender_rc"
      fi
      echo "[MENDER] mender-offline-init --force-install-module completed"
    else
      echo "[MENDER] fail: mender-offline-init not found"
      exit 22
    fi
  fi
done

echo "[RESULT] installation completed"
for pkg in "${expected_names[@]}"; do
  echo "[RESULT] $pkg=$(installed_package_version "$pkg")"
done
)SH";
    return script.str();
}

void cleanupRemoteDirectory(const SshContext& ssh, const std::string& remote_directory) {
    const CommandResult cleanup = ssh.ssh("rm -rf -- " + shellQuote(remote_directory));
    (void)cleanup;
}

int runInstaller() {
    for (const char* tool : {"dpkg-deb", "dpkg", "ssh", "scp"}) {
        if (!executableExists(tool)) {
            throw std::runtime_error(std::string("缺少必要命令：") + tool);
        }
    }

    const fs::path scan_directory = fs::current_path();
    std::vector<PackageInfo> packages = discoverPackages(scan_directory);
    if (packages.empty()) {
        throw std::runtime_error("当前目录没有可安装的白名单 deb");
    }

    std::cout << "[1/4] 正在检查目标设备架构...\n";
    SshContext ssh;
    const CommandResult architecture = ssh.ssh(
        "a=$(dpkg --print-architecture 2>/dev/null || true); m=$(uname -m 2>/dev/null || true); "
        "if [ \"$a\" = arm64 ] && { [ \"$m\" = aarch64 ] || [ \"$m\" = arm64 ]; }; "
        "then printf 'ARM64_OK\\n'; else printf 'ARM64_FAIL arch=%s machine=%s\\n' \"$a\" \"$m\"; exit 40; fi");
    requireSuccess(architecture, "目标设备架构检查");
    if (architecture.output.find("ARM64_OK") == std::string::npos) {
        throw std::runtime_error("目标设备未返回 ARM64 确认");
    }

    std::cout << "[1/4] 正在读取目标机白名单软件版本...\n";
    const std::map<std::string, std::string> installed_versions =
        queryInstalledVersions(ssh);

    std::map<std::string, const PackageInfo*> local_packages;
    for (const PackageInfo& package : packages) {
        local_packages.emplace(package.name, &package);
    }

    std::cout << "\n目标机白名单软件及升级计划：\n";
    std::cout << "序号  软件包                         目标机版本               本地版本(架构)              状态\n";
    for (std::size_t index = 0; index < kPackageOrder.size(); ++index) {
        const std::string name(kPackageOrder[index]);
        const auto local_iterator = local_packages.find(name);
        const PackageInfo* local =
            local_iterator == local_packages.end() ? nullptr : local_iterator->second;
        const std::string& installed = installed_versions.at(name);
        const std::string local_display =
            local == nullptr ? "-" : local->version + " (" + local->architecture + ")";
        std::cout << std::left << std::setw(6) << (index + 1)
                  << std::setw(31) << name
                  << std::setw(25) << (installed.empty() ? "未安装" : installed)
                  << std::setw(29) << local_display
                  << upgradeStatus(local, installed) << '\n';
    }

#ifdef UGRIPPER_FLASH_PREFLIGHT_TEST
    std::cout << "[TEST] SSH 登录、ARM64 预检和版本查询通过，未上传或安装软件包。\n";
    return 0;
#endif

    std::cout << "\n输入 INSTALL 确认升级/安装，其他输入取消：" << std::flush;
    std::string confirmation;
    std::getline(std::cin, confirmation);
    if (confirmation != "INSTALL") {
        std::cout << "已取消，未上传或安装软件包。\n";
        return 0;
    }

    const std::string remote_directory = "/tmp/ugripper-customer-install-" + randomToken().substr(0, 12);
    std::cout << "[2/4] 正在上传 " << packages.size() << " 个软件包...\n";
    requireSuccess(ssh.ssh("mkdir -p -- " + shellQuote(remote_directory) +
                           " && chmod 700 -- " + shellQuote(remote_directory)),
                   "创建远端临时目录");
    const CommandResult upload = ssh.scp(packages, remote_directory);
    if (upload.exit_code != 0) {
        cleanupRemoteDirectory(ssh, remote_directory);
        requireSuccess(upload, "上传软件包");
    }

#ifdef UGRIPPER_FLASH_TRANSFER_TEST
    cleanupRemoteDirectory(ssh, remote_directory);
    std::cout << "[TEST] SCP 上传与远端临时目录清理通过，未安装软件包。\n";
    return 0;
#endif

    std::cout << "[3/4] 正在安装并校验软件包...\n";
    const std::string remote_script = buildRemoteInstallScript(packages, remote_directory);
    const std::string privileged_input = decodeSecret(kTargetPassword) + "\n" + remote_script;
    const CommandResult installation =
        ssh.ssh("sudo -S -k -p '' bash -s", privileged_input);
    std::cout << sanitizeOutput(installation.output);
    requireSuccess(installation, "远端安装");

    std::cout << "[4/4] 烧录完成。\n";
    return 0;
}

}  // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    if (handleAskpassMode()) {
        return 0;
    }
    if (std::getenv("UGRIPPER_FLASH_ASKPASS_NONCE") != nullptr) {
        return 126;
    }

    try {
        return runInstaller();
    } catch (const std::exception& error) {
        std::cerr << "[ERROR] " << sanitizeOutput(error.what()) << '\n';
        return 1;
    }
}
