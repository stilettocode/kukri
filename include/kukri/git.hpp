#pragma once
#include <string>
namespace kukri {
// Executes enable, disable, status, verify, or doctor for the current repository.
// Changes the process working directory to the repository root and writes command
// diagnostics to stdout/stderr. Only enable/disable modify repository integration.
// executable is an optional absolute install path for enable; it must refer to
// the running binary. Returns 1 for a failed audit and 0 for success; operational
// errors throw std::runtime_error. Call from the single-threaded CLI only.
int repository_command(const std::string& command, const std::string& executable = {});
}  // namespace kukri
