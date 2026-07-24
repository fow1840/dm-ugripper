#include "gripper_hmi_driver.h"
#include "gripper_hmi_led_effects.h"
#include "utils/logger.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static constexpr size_t kBtnUpKeyIndex = 0;
static constexpr size_t kBtnDownKeyIndex = 1;

namespace {
std::atomic<bool> g_stop_requested{false};
constexpr int kBeepStateProbeDelayMs = 80;
constexpr int kBeepStateProbeIntervalMs = 100;
constexpr int kBeepSilenceRetryLimit = 5;
constexpr int kBeepSilenceConfirmMs = 150;
constexpr int kLuxMonitorRequestIntervalMs = 200;

void handleSignal(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        g_stop_requested.store(true);
    }
}

uint64_t currentSteadyMs()
{
    const auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}

uint64_t currentEpochMs()
{
    const auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}
}

struct TestOptions
{
    std::vector<std::string> ports;
    uint32_t baudrate = 115200;
    int durationSec = 10;
    int pollMs = 20;
    bool setLed = false;
    bool enableBeep = false;
    bool customBeep = false;
    bool ledOnly = false;
    bool stateMode = false;
    bool showHelp = false;
    bool readSerialNumber = false;
    bool readCalibration = false;
    bool hasWriteSerialNumber = false;
    bool writeForceCalibration = false;
    bool hasForceZeroRaw = false;
    bool hasForceReferenceRaw = false;
    bool hasForceReferenceMassMg = false;
    bool hasFillLightPwm = false;
    bool monitorLux = false;
    bool durationSpecified = false;
    int beepDurationMs = 0;
    int beepDuty = static_cast<int>(GripperHmiDriver::kDefaultBeepDuty);
    int beepFreq = static_cast<int>(GripperHmiDriver::kDefaultBeepFrequency);
    int fillLightPwm = 0;
    int32_t forceZeroRaw = 0;
    int32_t forceReferenceRaw = 0;
    uint32_t forceReferenceMassMg = 0;
    std::string serialNumberToWrite;
    std::string writeCalibrationBinPath;
    std::string dumpCalibrationBinPath;
    GripperLedColor color{32, 0, 0};
    GripperLedEffect effect{};
};

static void printUsage(const char *program)
{
    std::cout
        << "Usage: " << program << " [--port PATH]... [--baud N] [--duration SEC] [--poll-ms N] [--monitor-lux] [--rgb R G B] [--fill-light-pwm N] [--beep [--beep-ms N]] [--beep-duty N [--beep-freq N] [--beep-ms N]] [--state NAME] [--led-only]\n"
        << "State examples:\n"
        << "  --state READY\n"
        << "  --state RECORDING\n"
        << "  --state ERROR_1\n"
        << "  --state CALIB_RUN:0.5\n"
        << "Beep example:\n"
        << "  --beep --beep-ms 300\n"
        << "  --beep-duty 50 --beep-freq 1000 --beep-ms 300\n"
        << "  beep uses fixed tested defaults for active buzzer: duty=30 freq=1000\n"
        << "Fill light example:\n"
        << "  --fill-light-pwm 500  # valid range: 0-1000\n"
        << "Sensor monitor example:\n"
        << "  --monitor-lux --duration 0  # query lux at 5 Hz and display lux + pressure\n"
        << "Protocol commands:\n"
        << "  --read-sn\n"
        << "  --write-sn DAG91126320001D6  # encoded into 32-byte field with trailing zeros\n"
        << "  --read-calib [--dump-calib-bin /tmp/umi_calib.bin]\n"
        << "  --write-calib-bin /tmp/umi_calib.bin\n"
        << "  --write-force-calib --zero-raw N --reference-raw N --reference-mass-mg N\n"
        << "Defaults:\n"
        << "  --port /dev/right_gripper\n"
        << "  --port /dev/left_gripper\n"
        << "  --duration 0 means run until Ctrl+C\n";
}

static std::string keyName(int keyIndex)
{
    switch (keyIndex)
    {
    case 0:
        return "BTN_UP";
    case 1:
        return "BTN_DOWN";
    case -1:
        return "BTN_UNKNOWN";
    default:
        return "KEY" + std::to_string(keyIndex);
    }
}

static bool silenceBeepWithRetries(GripperHmiDriver *device)
{
    if (device == nullptr)
    {
        return false;
    }

    for (int attempt = 1; attempt <= kBeepSilenceRetryLimit; ++attempt)
    {
        const uint64_t previousStateCount = device->getSnapshot().beepStateCount;
        const bool commandOk = device->silenceBeep();
        device->requestState();

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(kBeepSilenceConfirmMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            device->pollOnce(10);
            const auto snapshot = device->getSnapshot();
            if (snapshot.beepStateCount > previousStateCount)
            {
                if (snapshot.beepState.duty == 0 && snapshot.beepState.frequency == 0)
                {
                    return true;
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cerr << "Failed to confirm beep silence on: " << device->getPort()
                  << " attempt=" << attempt
                  << " command_ok=" << (commandOk ? "true" : "false")
                  << std::endl;
    }
    return false;
}

static std::string toHexString(const std::string &text)
{
    std::ostringstream stream;
    for (size_t index = 0; index < text.size(); ++index)
    {
        if (index != 0)
        {
            stream << ' ';
        }
        stream << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
               << static_cast<int>(static_cast<unsigned char>(text[index]));
    }
    return stream.str();
}

static bool parseInt(const char *text, int *value)
{
    if (text == nullptr || value == nullptr)
    {
        return false;
    }
    char *end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0')
    {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

static bool parseInt32(const char *text, int32_t *value)
{
    if (text == nullptr || value == nullptr)
    {
        return false;
    }
    char *end = nullptr;
    const long long parsed = std::strtoll(text, &end, 10);
    if (end == text || *end != '\0' ||
        parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max())
    {
        return false;
    }
    *value = static_cast<int32_t>(parsed);
    return true;
}

static bool parseUint32(const char *text, uint32_t *value)
{
    if (text == nullptr || value == nullptr || text[0] == '-')
    {
        return false;
    }
    char *end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0' || parsed > std::numeric_limits<uint32_t>::max())
    {
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

static bool parseOptions(int argc, char **argv, TestOptions *options)
{
    if (options == nullptr)
    {
        return false;
    }

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h")
        {
            options->showHelp = true;
            return true;
        }

        if (argument == "--port")
        {
            if (index + 1 >= argc)
            {
                std::cerr << "Missing value for --port" << std::endl;
                return false;
            }
            options->ports.emplace_back(argv[++index]);
            continue;
        }

        if (argument == "--baud")
        {
            int value = 0;
            if (index + 1 >= argc || !parseInt(argv[++index], &value) || value <= 0)
            {
                std::cerr << "Invalid value for --baud" << std::endl;
                return false;
            }
            options->baudrate = static_cast<uint32_t>(value);
            continue;
        }

        if (argument == "--duration")
        {
            int value = 0;
            if (index + 1 >= argc || !parseInt(argv[++index], &value) || value < 0)
            {
                std::cerr << "Invalid value for --duration" << std::endl;
                return false;
            }
            options->durationSec = value;
            options->durationSpecified = true;
            continue;
        }

        if (argument == "--monitor-lux")
        {
            options->monitorLux = true;
            continue;
        }

        if (argument == "--poll-ms")
        {
            int value = 0;
            if (index + 1 >= argc || !parseInt(argv[++index], &value) || value < 0)
            {
                std::cerr << "Invalid value for --poll-ms" << std::endl;
                return false;
            }
            options->pollMs = value;
            continue;
        }

        if (argument == "--rgb")
        {
            int red = 0;
            int green = 0;
            int blue = 0;
            if (index + 3 >= argc ||
                !parseInt(argv[++index], &red) ||
                !parseInt(argv[++index], &green) ||
                !parseInt(argv[++index], &blue) ||
                red < 0 || red > 255 ||
                green < 0 || green > 255 ||
                blue < 0 || blue > 255)
            {
                std::cerr << "Invalid value for --rgb" << std::endl;
                return false;
            }
            options->setLed = true;
            options->color = GripperLedColor{
                static_cast<uint8_t>(red),
                static_cast<uint8_t>(green),
                static_cast<uint8_t>(blue),
            };
            continue;
        }

        if (argument == "--beep")
        {
            options->enableBeep = true;
            continue;
        }

        if (argument == "--fill-light-pwm")
        {
            int value = 0;
            if (index + 1 >= argc || !parseInt(argv[++index], &value) ||
                value < 0 || value > static_cast<int>(GripperHmiProtocol::kMaxFillLightPwm))
            {
                std::cerr << "Invalid value for --fill-light-pwm (expected 0-"
                          << GripperHmiProtocol::kMaxFillLightPwm << ")" << std::endl;
                return false;
            }
            options->hasFillLightPwm = true;
            options->fillLightPwm = value;
            continue;
        }

        if (argument == "--beep-duty")
        {
            int value = 0;
            if (index + 1 >= argc || !parseInt(argv[++index], &value) || value < 0 || value > 255)
            {
                std::cerr << "Invalid value for --beep-duty" << std::endl;
                return false;
            }
            options->enableBeep = true;
            options->customBeep = true;
            options->beepDuty = value;
            continue;
        }

        if (argument == "--beep-freq")
        {
            int value = 0;
            if (index + 1 >= argc || !parseInt(argv[++index], &value) || value < 0 || value > 65535)
            {
                std::cerr << "Invalid value for --beep-freq" << std::endl;
                return false;
            }
            options->enableBeep = true;
            options->customBeep = true;
            options->beepFreq = value;
            continue;
        }

        if (argument == "--beep-ms")
        {
            int value = 0;
            if (index + 1 >= argc || !parseInt(argv[++index], &value) || value < 0)
            {
                std::cerr << "Invalid value for --beep-ms" << std::endl;
                return false;
            }
            options->beepDurationMs = value;
            continue;
        }

        if (argument == "--state")
        {
            if (index + 1 >= argc)
            {
                std::cerr << "Missing value for --state" << std::endl;
                return false;
            }
            if (!GripperLedEffectRenderer::parseStateText(argv[++index], &options->effect))
            {
                std::cerr << "Invalid value for --state" << std::endl;
                return false;
            }
            options->stateMode = true;
            continue;
        }

        if (argument == "--led-only")
        {
            options->ledOnly = true;
            continue;
        }

        if (argument == "--read-sn")
        {
            options->readSerialNumber = true;
            continue;
        }

        if (argument == "--write-sn")
        {
            if (index + 1 >= argc)
            {
                std::cerr << "Missing value for --write-sn" << std::endl;
                return false;
            }
            options->serialNumberToWrite = argv[++index];
            options->hasWriteSerialNumber = true;
            continue;
        }

        if (argument == "--read-calib")
        {
            options->readCalibration = true;
            continue;
        }

        if (argument == "--write-calib-bin")
        {
            if (index + 1 >= argc)
            {
                std::cerr << "Missing value for --write-calib-bin" << std::endl;
                return false;
            }
            options->writeCalibrationBinPath = argv[++index];
            continue;
        }

        if (argument == "--write-force-calib")
        {
            options->writeForceCalibration = true;
            continue;
        }

        if (argument == "--zero-raw")
        {
            if (index + 1 >= argc || !parseInt32(argv[++index], &options->forceZeroRaw))
            {
                std::cerr << "Invalid value for --zero-raw" << std::endl;
                return false;
            }
            options->hasForceZeroRaw = true;
            continue;
        }

        if (argument == "--reference-raw")
        {
            if (index + 1 >= argc || !parseInt32(argv[++index], &options->forceReferenceRaw))
            {
                std::cerr << "Invalid value for --reference-raw" << std::endl;
                return false;
            }
            options->hasForceReferenceRaw = true;
            continue;
        }

        if (argument == "--reference-mass-mg")
        {
            if (index + 1 >= argc || !parseUint32(argv[++index], &options->forceReferenceMassMg) ||
                options->forceReferenceMassMg == 0)
            {
                std::cerr << "Invalid value for --reference-mass-mg" << std::endl;
                return false;
            }
            options->hasForceReferenceMassMg = true;
            continue;
        }

        if (argument == "--dump-calib-bin")
        {
            if (index + 1 >= argc)
            {
                std::cerr << "Missing value for --dump-calib-bin" << std::endl;
                return false;
            }
            options->dumpCalibrationBinPath = argv[++index];
            continue;
        }

        std::cerr << "Unknown argument: " << argument << std::endl;
        return false;
    }

    if (options->ports.empty())
    {
        options->ports.emplace_back("/dev/right_gripper");
        options->ports.emplace_back("/dev/left_gripper");
    }
    if (options->monitorLux && !options->durationSpecified)
    {
        options->durationSec = 0;
    }

    if (options->writeForceCalibration &&
        (!options->hasForceZeroRaw || !options->hasForceReferenceRaw ||
         !options->hasForceReferenceMassMg || options->forceZeroRaw == options->forceReferenceRaw))
    {
        std::cerr << "--write-force-calib requires distinct --zero-raw/--reference-raw and positive "
                     "--reference-mass-mg"
                  << std::endl;
        return false;
    }

    return true;
}

static bool readBinaryFile(const std::string &path, std::vector<uint8_t> *buffer)
{
    if (buffer == nullptr)
    {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        return false;
    }

    buffer->assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

static bool writeBinaryFile(const std::string &path, const uint8_t *data, size_t size)
{
    if (data == nullptr)
    {
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    if (!output.is_open())
    {
        return false;
    }

    output.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    return output.good();
}

static void printCalibrationSummary(const gripper_hmi::GripperCalibrationData &data)
{
    const auto &header = data.header;
    std::cout
        << "calibration.magic=" << std::string(header.magic, header.magic + 4) << '\n'
        << "calibration.data_format_version=0x" << std::hex << header.dataFormatVersion << std::dec << '\n'
        << "calibration.payload_size=" << header.payloadSize << '\n'
        << "calibration.header_size=" << header.headerSize << '\n'
        << "calibration.valid_fields=0x" << std::hex << header.validFields << std::dec << '\n';

    if (header.dataFormatVersion == gripper_hmi::kCalibrationDataFormatVersionLegacy)
    {
        std::cout << "calibration.kind=legacy_camera_imu_ignored" << std::endl;
        return;
    }

    const bool valid = gripper_hmi::isForceCalibrationPayload(data);
    std::cout
        << "calibration.kind=force_sensor" << '\n'
        << "force.valid=" << (valid ? "yes" : "no") << '\n'
        << "force.serial_number=" << gripper_hmi::decodeSerialNumber(data.force.serialNumber) << '\n'
        << "force.zero_raw=" << data.force.zeroRaw << '\n'
        << "force.reference_raw=" << data.force.referenceRaw << '\n'
        << "force.reference_mass_mg=" << data.force.referenceMassMg << '\n'
        << "force.flags=0x" << std::hex << data.force.flags << std::dec << '\n'
        << "force.calibrated_at_unix_s=" << data.force.calibratedAtUnixSeconds << '\n'
        << "force.crc32=0x" << std::hex << data.force.crc32 << std::dec << '\n'
        << "force.crc32_calculated=0x" << std::hex
        << gripper_hmi::calculateForceCalibrationCrc32(data.force) << std::dec << std::endl;
}

int main(int argc, char **argv)
{
    DM_LOG_INIT("GripperHmiTool",
                "info",
                "./logs/GripperHmiTool/GripperHmiTool.log",
                1024 * 1024 * 10,
                3,
                false);

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    TestOptions options;
    if (!parseOptions(argc, argv, &options))
    {
        return 1;
    }

    if (options.showHelp)
    {
        printUsage(argv[0]);
        return 0;
    }

    std::vector<std::unique_ptr<GripperHmiDriver>> devices;
    for (size_t index = 0; index < options.ports.size(); ++index)
    {
        auto driver = std::make_unique<GripperHmiDriver>(
            options.ports[index],
            options.baudrate,
            "GripperHmiTool" + std::to_string(index));

        if (!driver->connect())
        {
            std::cerr << "Failed to connect: " << options.ports[index] << std::endl;
            return 1;
        }

        devices.push_back(std::move(driver));
    }

    std::cout << "Connected " << devices.size() << " gripper HMI device(s)" << std::endl;

    if (options.hasFillLightPwm)
    {
        bool allOk = true;
        for (auto &device : devices)
        {
            if (!device->setFillLightPwm(static_cast<uint16_t>(options.fillLightPwm)))
            {
                std::cerr << "Failed to set fill light PWM on: " << device->getPort() << std::endl;
                allOk = false;
            }
            else
            {
                std::cout << '[' << device->getPort() << "] fill-light-pwm="
                          << options.fillLightPwm << std::endl;
            }
        }
        if (!allOk)
        {
            return 1;
        }
    }

    if (options.readSerialNumber || options.hasWriteSerialNumber ||
        options.readCalibration || options.writeForceCalibration ||
        !options.writeCalibrationBinPath.empty())
    {
        bool allOk = true;

        if (options.hasWriteSerialNumber)
        {
            for (auto &device : devices)
            {
                if (!device->writeSerialNumber(options.serialNumberToWrite))
                {
                    std::cerr << "Failed to write SN on: " << device->getPort();
                    if (!device->getLastCommandError().empty())
                    {
                        std::cerr << " reason=" << device->getLastCommandError();
                    }
                    std::cerr << std::endl;
                    allOk = false;
                }
                else
                {
                    std::cout << '[' << device->getPort() << "] write-sn ok: " << options.serialNumberToWrite << std::endl;
                }
            }
        }

        if (options.readSerialNumber)
        {
            for (auto &device : devices)
            {
                std::string serialNumber;
                if (!device->readSerialNumber(&serialNumber))
                {
                    std::cerr << "Failed to read SN on: " << device->getPort();
                    if (!device->getLastCommandError().empty())
                    {
                        std::cerr << " reason=" << device->getLastCommandError();
                    }
                    std::cerr << std::endl;
                    allOk = false;
                }
                else
                {
                    std::cout << '[' << device->getPort() << "] sn=" << serialNumber;
                    if (serialNumber.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
                    {
                        std::cout << " hex=" << toHexString(serialNumber);
                    }
                    std::cout << std::endl;
                }
            }
        }

        if (options.writeForceCalibration)
        {
            for (auto &device : devices)
            {
                std::string serialNumber;
                if (!device->readSerialNumber(&serialNumber))
                {
                    std::cerr << "Failed to read SN before force calibration on: " << device->getPort();
                    if (!device->getLastCommandError().empty())
                    {
                        std::cerr << " reason=" << device->getLastCommandError();
                    }
                    std::cerr << std::endl;
                    allOk = false;
                    continue;
                }

                gripper_hmi::GripperCalibrationData calibrationData{};
                if (!gripper_hmi::makeForceCalibrationData(
                        serialNumber,
                        options.forceZeroRaw,
                        options.forceReferenceRaw,
                        options.forceReferenceMassMg,
                        static_cast<uint32_t>(currentEpochMs() / 1000),
                        &calibrationData))
                {
                    std::cerr << "Invalid force calibration values for: " << device->getPort() << std::endl;
                    allOk = false;
                    continue;
                }

                if (!device->writeCalibrationData(calibrationData))
                {
                    std::cerr << "Failed to write force calibration on: " << device->getPort();
                    if (!device->getLastCommandError().empty())
                    {
                        std::cerr << " reason=" << device->getLastCommandError();
                    }
                    std::cerr << std::endl;
                    allOk = false;
                    continue;
                }

                gripper_hmi::GripperCalibrationData readback{};
                if (!device->readCalibrationData(&readback) ||
                    !gripper_hmi::isForceCalibrationPayload(readback) ||
                    !gripper_hmi::forceCalibrationMatchesSerialNumber(readback, serialNumber) ||
                    std::memcmp(&readback.force, &calibrationData.force, sizeof(readback.force)) != 0)
                {
                    std::cerr << "Force calibration readback verification failed on: "
                              << device->getPort() << std::endl;
                    allOk = false;
                    continue;
                }

                std::cout << '[' << device->getPort() << "] write-force-calib ok"
                          << " sn=" << serialNumber
                          << " zero_raw=" << calibrationData.force.zeroRaw
                          << " reference_raw=" << calibrationData.force.referenceRaw
                          << " reference_mass_mg=" << calibrationData.force.referenceMassMg
                          << std::endl;
            }
        }

        if (!options.writeCalibrationBinPath.empty())
        {
            std::vector<uint8_t> raw;
            if (!readBinaryFile(options.writeCalibrationBinPath, &raw))
            {
                std::cerr << "Failed to read calibration binary: " << options.writeCalibrationBinPath << std::endl;
                return 1;
            }
            if (raw.size() != gripper_hmi::kCalibrationPayloadSize)
            {
                std::cerr << "Invalid calibration binary size: " << raw.size()
                          << " expected=" << gripper_hmi::kCalibrationPayloadSize << std::endl;
                return 1;
            }

            gripper_hmi::GripperCalibrationData calibrationData{};
            std::memcpy(&calibrationData, raw.data(), sizeof(calibrationData));
            if (!gripper_hmi::isForceCalibrationPayload(calibrationData))
            {
                std::cerr << "Calibration binary is not a valid force-sensor UCAL payload" << std::endl;
                return 1;
            }
            for (auto &device : devices)
            {
                std::string serialNumber;
                if (!device->readSerialNumber(&serialNumber) ||
                    !gripper_hmi::forceCalibrationMatchesSerialNumber(calibrationData, serialNumber))
                {
                    std::cerr << "Calibration SN does not match gripper on: " << device->getPort()
                              << std::endl;
                    allOk = false;
                    continue;
                }
                if (!device->writeCalibrationData(calibrationData))
                {
                    std::cerr << "Failed to write calibration on: " << device->getPort();
                    if (!device->getLastCommandError().empty())
                    {
                        std::cerr << " reason=" << device->getLastCommandError();
                    }
                    std::cerr << std::endl;
                    allOk = false;
                }
                else
                {
                    std::cout << '[' << device->getPort() << "] write-calibration ok" << std::endl;
                }
            }
        }

        if (options.readCalibration)
        {
            for (auto &device : devices)
            {
                gripper_hmi::GripperCalibrationData calibrationData{};
                if (!device->readCalibrationData(&calibrationData))
                {
                    std::cerr << "Failed to read calibration on: " << device->getPort();
                    if (!device->getLastCommandError().empty())
                    {
                        std::cerr << " reason=" << device->getLastCommandError();
                    }
                    std::cerr << std::endl;
                    allOk = false;
                    continue;
                }

                std::cout << '[' << device->getPort() << "] calibration summary" << std::endl;
                printCalibrationSummary(calibrationData);
                if (calibrationData.header.dataFormatVersion ==
                    gripper_hmi::kCalibrationDataFormatVersionForceV1)
                {
                    std::string serialNumber;
                    const bool snRead = device->readSerialNumber(&serialNumber);
                    std::cout << "force.sn_match="
                              << (snRead && gripper_hmi::forceCalibrationMatchesSerialNumber(
                                                calibrationData, serialNumber)
                                      ? "yes"
                                      : "no")
                              << std::endl;
                }

                if (!options.dumpCalibrationBinPath.empty())
                {
                    const std::string dumpPath =
                        devices.size() == 1
                            ? options.dumpCalibrationBinPath
                            : options.dumpCalibrationBinPath + "." + device->getName() + ".bin";
                    if (!writeBinaryFile(dumpPath,
                                         reinterpret_cast<const uint8_t *>(&calibrationData),
                                         sizeof(calibrationData)))
                    {
                        std::cerr << "Failed to dump calibration binary: " << dumpPath << std::endl;
                        allOk = false;
                    }
                    else
                    {
                        std::cout << '[' << device->getPort() << "] dumped calibration: " << dumpPath << std::endl;
                    }
                }
            }
        }

        return allOk ? 0 : 1;
    }

    if (options.stateMode)
    {
        std::cout << "LED state mode: " << GripperLedEffectRenderer::stateText(options.effect) << std::endl;
    }
    if (options.enableBeep)
    {
        std::cout << "Beep request: enabled=true"
                  << " mode=" << (options.customBeep ? "custom" : "default")
                  << " duty=" << options.beepDuty
                  << " freq=" << options.beepFreq
                  << " duration_ms=" << options.beepDurationMs
                  << std::endl;
    }

    const auto startedAt = std::chrono::steady_clock::now();
    auto lastStatePrint = startedAt;
    std::vector<uint64_t> lastLuxCounts(devices.size(), 0);
    std::vector<uint64_t> lastLuxEventAtMs(devices.size(), 0);
    std::array<uint8_t, 3> lastColor{255, 255, 255};
    GripperLedEffectRenderer renderer;
    bool beepApplied = false;
    bool beepSilenced = false;
    auto beepStartedAt = startedAt;
    auto lastBeepStateRequestAt = startedAt - std::chrono::milliseconds(kBeepStateProbeIntervalMs);
    auto lastLuxRequestAt = startedAt - std::chrono::milliseconds(kLuxMonitorRequestIntervalMs);
    if (options.monitorLux)
    {
        std::cout << "Polling lux at 5 Hz and displaying pressure; press Ctrl+C to stop" << std::endl;
    }

    while (!g_stop_requested.load())
    {
        const auto now = std::chrono::steady_clock::now();
        if (options.durationSec > 0)
        {
            const auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(now - startedAt).count();
            if (elapsedSec >= options.durationSec)
            {
                break;
            }
        }

        if (options.stateMode)
        {
            const auto rendered = renderer.render(options.effect, currentSteadyMs(), currentEpochMs());
            const std::array<uint8_t, 3> color{rendered.red, rendered.green, rendered.blue};
            if (color != lastColor)
            {
                for (auto &device : devices)
                {
                    if (!device->setLedColor(rendered))
                    {
                        std::cerr << "Failed to set LED on: " << device->getPort() << std::endl;
                    }
                }
                lastColor = color;
            }
        }
        else if (options.setLed)
        {
            for (auto &device : devices)
            {
                if (!device->setLedColor(options.color))
                {
                    std::cerr << "Failed to set LED on: " << device->getPort() << std::endl;
                }
            }
            options.setLed = false;
        }

        if (options.enableBeep && !beepApplied)
        {
            const GripperBeepState beepState{
                static_cast<uint16_t>(options.beepDuty),
                static_cast<uint16_t>(options.beepFreq),
            };
            for (auto &device : devices)
            {
                const bool ok = options.customBeep
                                    ? device->setBeepState(beepState)
                                    : device->setBeepEnabled(true);
                if (!ok)
                {
                    std::cerr << "Failed to enable beep on: " << device->getPort() << std::endl;
                }
            }
            beepApplied = true;
            beepStartedAt = now;
        }

        if (options.enableBeep && !beepSilenced && options.beepDurationMs > 0)
        {
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - beepStartedAt).count();
            if (elapsedMs >= options.beepDurationMs)
            {
                for (auto &device : devices)
                {
                    if (!silenceBeepWithRetries(device.get()))
                    {
                        std::cerr << "Failed to silence beep on: " << device->getPort() << std::endl;
                    }
                }
                beepSilenced = true;
            }
        }

        if (!options.ledOnly)
        {
            if (options.monitorLux &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLuxRequestAt).count() >=
                    kLuxMonitorRequestIntervalMs)
            {
                for (auto &device : devices)
                {
                    device->requestAmbientLight();
                }
                lastLuxRequestAt = now;
            }

            bool requestedBeepStateThisLoop = false;
            size_t deviceIndex = 0;
            for (auto &device : devices)
            {
                bool shouldRequestState = false;
                if (options.enableBeep)
                {
                    const auto elapsedSinceBeepMs =
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - beepStartedAt).count();
                    const auto elapsedSinceLastProbeMs =
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBeepStateRequestAt).count();
                    shouldRequestState = beepApplied &&
                                         elapsedSinceBeepMs >= kBeepStateProbeDelayMs &&
                                         elapsedSinceLastProbeMs >= kBeepStateProbeIntervalMs;
                }

                if (shouldRequestState)
                {
                    device->requestState();
                    if (options.enableBeep)
                    {
                        requestedBeepStateThisLoop = true;
                    }
                }
                GripperKeyReport report;
                if (!options.monitorLux && device->waitForKeyChange(options.pollMs, &report))
                {
                    std::cout
                        << '[' << device->getPort() << "] key=" << keyName(report.keyIndex) << "(" << report.keyIndex << ")"
                        << " raw=0x" << std::hex << static_cast<int>(report.rawCode) << std::dec
                        << (report.pressed ? " pressed" : " released")
                        << std::endl;
                }
                else
                {
                    device->pollOnce(options.monitorLux ? options.pollMs : 0);
                }

                if (options.monitorLux)
                {
                    const auto snapshot = device->getSnapshot();
                    if (snapshot.lastAmbientLightReport.has_value() &&
                        snapshot.ambientLightReportCount > lastLuxCounts[deviceIndex])
                    {
                        const uint64_t nowMs = currentSteadyMs();
                        const uint64_t newFrames = snapshot.ambientLightReportCount - lastLuxCounts[deviceIndex];
                        const uint64_t intervalMs = lastLuxEventAtMs[deviceIndex] == 0
                                                        ? 0
                                                        : nowMs - lastLuxEventAtMs[deviceIndex];
                        const double eventHz = intervalMs == 0
                                                   ? 0.0
                                                   : static_cast<double>(newFrames) * 1000.0 /
                                                         static_cast<double>(intervalMs);
                        std::cout << '[' << device->getPort() << "] lux="
                                  << snapshot.lastAmbientLightReport->lux
                                  << "(age_ms=" << snapshot.ambientLightReportAgeMs
                                  << ",count=" << snapshot.ambientLightReportCount << ')'
                                  << " pressure=";
                        if (snapshot.lastPressureReport.has_value())
                        {
                            const auto &pressure = snapshot.lastPressureReport.value();
                            std::cout << pressure.value << "(raw=0x"
                                      << std::hex << std::uppercase << std::setw(8)
                                      << std::setfill('0') << static_cast<uint32_t>(pressure.value) << std::dec
                                      << std::setfill(' ') << ",age_ms=" << snapshot.pressureReportAgeMs
                                      << ",count=" << snapshot.pressureReportCount << ')';
                        }
                        else
                        {
                            std::cout << "none";
                        }
                        std::cout << " new_lux_frames=" << newFrames;
                        if (intervalMs != 0)
                        {
                            std::cout << " lux_interval_ms=" << intervalMs
                                      << " lux_hz=" << std::fixed << std::setprecision(2) << eventHz
                                      << std::defaultfloat;
                        }
                        std::cout << std::endl;
                        lastLuxCounts[deviceIndex] = snapshot.ambientLightReportCount;
                        lastLuxEventAtMs[deviceIndex] = nowMs;
                    }
                }
                ++deviceIndex;
            }

            if (options.enableBeep && requestedBeepStateThisLoop)
            {
                lastBeepStateRequestAt = now;
            }

            const int statePrintIntervalMs = options.enableBeep ? 100 : 1000;
            if (!options.monitorLux &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatePrint).count() >= statePrintIntervalMs)
            {
                lastStatePrint = now;
                for (auto &device : devices)
                {
                    const auto snapshot = device->getSnapshot();
                    std::cout
                        << '[' << device->getPort() << "] active=" << (snapshot.active ? "yes" : "no")
                        << " age_ms=" << snapshot.lastRxAgeMs
                        << " beep_duty=" << snapshot.beepState.duty
                        << " beep_freq=" << snapshot.beepState.frequency
                        << " pressure=";
                    if (snapshot.lastPressureReport.has_value())
                    {
                        const auto &pressure = snapshot.lastPressureReport.value();
                        std::cout << pressure.value << "(raw=0x"
                                  << std::hex << std::uppercase << std::setw(8)
                                  << std::setfill('0') << static_cast<uint32_t>(pressure.value) << std::dec
                                  << std::setfill(' ') << ",age_ms=" << snapshot.pressureReportAgeMs << ')';
                    }
                    else
                    {
                        std::cout << "none";
                    }
                    std::cout << " lux=";
                    if (snapshot.lastAmbientLightReport.has_value())
                    {
                        std::cout << snapshot.lastAmbientLightReport->lux
                                  << "(age_ms=" << snapshot.ambientLightReportAgeMs << ')';
                    }
                    else
                    {
                        std::cout << "none";
                    }
                    std::cout
                        << " btn_up=" << (snapshot.keyPressed[kBtnUpKeyIndex] ? 1 : 0)
                        << " btn_down=" << (snapshot.keyPressed[kBtnDownKeyIndex] ? 1 : 0)
                        << " extra=";
                    for (size_t keyIndex = 0; keyIndex < snapshot.keyPressed.size(); ++keyIndex)
                    {
                        if (keyIndex == kBtnUpKeyIndex || keyIndex == kBtnDownKeyIndex)
                        {
                            continue;
                        }
                        std::cout << keyIndex << ':' << (snapshot.keyPressed[keyIndex] ? '1' : '0') << ' ';
                    }
                    std::cout << std::endl;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(
            options.stateMode ? static_cast<int>(GripperLedEffectRenderer::recommendedRenderIntervalMs())
                              : std::max(1, options.pollMs)));
    }

    return 0;
}
