#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace kukri {
struct ProcessResult {
    int code;  // Child exit status; POSIX signals are reported as 128 + signal.
    std::string out, err;
};

// Runs args directly, without a shell, in the inherited working directory and
// environment. args must be nonempty and contain no NUL bytes. Supplies input on
// stdin and captures stdout/stderr as bytes. Throws on process setup or I/O errors;
// a nonzero child status is returned, not thrown. No timeout is imposed.
ProcessResult run(const std::vector<std::string>& args, const std::string& input = {});

// Returns the running executable's absolute path, which may resolve symlinks.
// Throws if platform discovery fails; do not use this to preserve an install link.
std::filesystem::path executable_path();

// Quotes one argument for Git's POSIX-style filter shell, including on Windows.
// This is not Windows CreateProcess argument quoting. value must contain no NUL.
std::string shell_quote(const std::string& value);

// Returns UTF-8 path bytes with forward slashes, without resolving symlinks.
std::string path_text(const std::filesystem::path& path);
}  // namespace kukri
