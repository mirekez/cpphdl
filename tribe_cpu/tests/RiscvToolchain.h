#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace tribe_test {

inline std::filesystem::path find_riscv_tool(const char* name)
{
    auto executable = [](const std::filesystem::path& path) {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) && access(path.c_str(), X_OK) == 0;
    };
    for (const auto* variable : {"RISCV_HOME", "RISCV"}) {
        if (const auto* root = std::getenv(variable); root && *root) {
            const auto candidate = std::filesystem::path(root) / "bin" / name;
            if (executable(candidate)) return candidate;
        }
    }
    if (const auto* path = std::getenv("PATH")) {
        std::string remaining(path);
        size_t begin = 0;
        do {
            const auto end = remaining.find(':', begin);
            const auto directory = remaining.substr(begin, end - begin);
            const auto candidate = std::filesystem::path(directory.empty() ? "." : directory) / name;
            if (executable(candidate)) return candidate;
            if (end == std::string::npos) break;
            begin = end + 1;
        } while (begin <= remaining.size());
    }
    if (const auto* home = std::getenv("HOME")) {
        const auto candidate = std::filesystem::path(home) / "riscv" / "bin" / name;
        if (executable(candidate)) return candidate;
    }
    return {};
}

}
