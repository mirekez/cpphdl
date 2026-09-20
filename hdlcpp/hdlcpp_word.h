#pragma once

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

inline int runWordFrontend(int argc, char** argv)
{
#if defined(_WIN32)
    std::cerr << "word-model frontend currently requires a POSIX host\n";
    return 1;
#else
#ifdef HDLCPP_SOURCE_DIR
    const auto root = std::filesystem::path(HDLCPP_SOURCE_DIR);
#else
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
#endif
    const auto script = root / "tools" / "hdlcpp-word.py";
    if (!std::filesystem::is_regular_file(script)) {
        std::cerr << "cannot locate hdlcpp-word.py at " << script << '\n';
        return 1;
    }
    std::vector<std::string> arguments{"python3", script.string()};
    for (int index = 2; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    std::vector<char*> pointers;
    for (auto& argument : arguments) {
        pointers.push_back(argument.data());
    }
    pointers.push_back(nullptr);
    execvp(pointers[0], pointers.data());
    std::cerr << "cannot execute the Python word-model frontend\n";
    return 1;
#endif
}
