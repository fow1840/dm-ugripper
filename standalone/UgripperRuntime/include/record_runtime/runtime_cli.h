#ifndef RECORD_RUNTIME_RUNTIME_CLI_H
#define RECORD_RUNTIME_RUNTIME_CLI_H

#include "record_runtime.h"

#include <string>

namespace DA::Ugripper
{
enum class RuntimeCliAction
{
    Run,
    Exit,
};

struct RuntimeCliParseResult
{
    RecordRuntimeOptions options;
    RuntimeCliAction action = RuntimeCliAction::Run;
    int exitCode = 0;
};

[[nodiscard]] std::string GetRuntimeUsage(const char *program);
[[nodiscard]] RuntimeCliParseResult ParseRuntimeCli(int argc, char **argv);
}

#endif
