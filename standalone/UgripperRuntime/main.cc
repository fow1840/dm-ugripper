#include "utils/cpu_affinity.h"
#include "record_runtime.h"
#include "record_runtime/runtime_cli.h"
#include "utils/ugripper_logger.h"

#include <utils/single_process_instance.h>

#include <csignal>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
DA::Ugripper::RecordRuntime *g_runtime = nullptr;

void handleSignal(int signal)
{
    if ((signal == SIGINT || signal == SIGTERM) && g_runtime != nullptr)
    {
        g_runtime->Stop();
    }
}

}

int main(int argc, char **argv)
{
    const std::string app_name = "UgripperRuntime";
    DM_LOG_INIT(app_name,
                "info",
                "./logs/UgripperRuntime/UgripperRuntime.log",
                1024 * 1024 * 10,
                3,
                false);
    DA::utils::ConfigureCpuAffinity(DA::utils::CpuRole::Auxiliary);

    std::unique_ptr<DA::utils::SingleProcessInstance> single_process_instance;
    try
    {
        single_process_instance = std::make_unique<DA::utils::SingleProcessInstance>(app_name);
    }
    catch (const std::runtime_error &error)
    {
        DM_LOG_WARN("{} is already running or cannot acquire the single-process lock: {}",
                    app_name,
                    error.what());
        return 0;
    }

    auto cliResult = DA::Ugripper::ParseRuntimeCli(argc, argv);
    if (cliResult.action == DA::Ugripper::RuntimeCliAction::Exit)
    {
        return cliResult.exitCode;
    }

    DA::Ugripper::RecordRuntime runtime(std::move(cliResult.options));
    g_runtime = &runtime;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    int exitCode = 1;
    if (runtime.Initialize())
    {
        exitCode = runtime.Run();
    }

    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    g_runtime = nullptr;
    return exitCode;
}
