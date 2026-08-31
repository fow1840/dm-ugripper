#include "record_runtime/runtime_cli.h"

#include "utils/ugripper_logger.h"

#include <exception>
#include <iostream>
#include <sstream>
#include <string>

namespace DA::Ugripper
{
std::string GetRuntimeUsage(const char *program)
{
    std::ostringstream usage;
    usage
        << "Usage: " << program << " [--port PATH]... [--camera-bin PATH] [--sensor-bin PATH] [--disk-root PATH] [--poll-ms N]\n"
        << "Defaults:\n"
        << "  --port /dev/right_gripper\n"
        << "  --port /dev/left_gripper\n";
    return usage.str();
}

RuntimeCliParseResult ParseRuntimeCli(int argc, char **argv)
{
    RuntimeCliParseResult result;

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h")
        {
            std::cout << GetRuntimeUsage(argv[0]);
            result.action = RuntimeCliAction::Exit;
            return result;
        }
        if (argument == "--port" && index + 1 < argc)
        {
            result.options.gripperPorts.emplace_back(argv[++index]);
            continue;
        }
        if (argument == "--camera-bin" && index + 1 < argc)
        {
            result.options.cameraRecorderBin = argv[++index];
            continue;
        }
        if (argument == "--sensor-bin" && index + 1 < argc)
        {
            result.options.sensorRecorderBin = argv[++index];
            continue;
        }
        if (argument == "--disk-root" && index + 1 < argc)
        {
            result.options.diskRoot = argv[++index];
            continue;
        }
        if (argument == "--poll-ms" && index + 1 < argc)
        {
            try
            {
                result.options.pollMs = std::stoi(argv[++index]);
            }
            catch (const std::exception &error)
            {
                DM_LOG_ERROR_CODE("UGR-70-01",
                                  "Invalid --poll-ms value: {}",
                                  error.what());
                std::cout << GetRuntimeUsage(argv[0]);
                result.action = RuntimeCliAction::Exit;
                result.exitCode = 1;
                return result;
            }
            continue;
        }

        DM_LOG_ERROR_CODE("UGR-70-01", "{}", (::DA::utils::LogString() << "Unknown argument: " << argument).str());
        std::cout << GetRuntimeUsage(argv[0]);
        result.action = RuntimeCliAction::Exit;
        result.exitCode = 1;
        return result;
    }

    return result;
}
}
