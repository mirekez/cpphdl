#include "../../VerilogA.h"
#include <cerrno>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef CPPHDL_ANALOG_PACKAGE_ROOT
#define CPPHDL_ANALOG_PACKAGE_ROOT "."
#endif
#ifndef CPPHDL_ANALOG_CXX
#define CPPHDL_ANALOG_CXX "c++"
#endif

namespace cpphdl::verilog_a {
namespace {
void help() {
    std::cout << R"(Usage:
  cpphdl --verilog-a SOURCE --top TYPE --ports Vdd,Vss,IN,OUT [options] -- [C++ compiler arguments]
  cpphdl-veriloga SOURCE --top TYPE --ports Vdd,Vss,IN,OUT [options] -- [C++ compiler arguments]

  --top TYPE           C++ class derived from analog::IC; templates are supported.
  --ports MEMBERS      Explicit comma-separated electrical member ports, including inherited rails.
  --module NAME        Verilog-A module name (default: AnalogIC).
  --output FILE        Output .va file (default: AnalogIC.va).
  --configure FUNCTION Call FUNCTION(component) before elaboration to set model parameters.
  --cxx EXECUTABLE     Native C++20 compiler (default: configured CMake compiler).
  --help               Show this help.

The converter compiles and executes a native elaboration harness. The source and
compiler arguments are ordinary trusted C++ code. Nested ICs lower to one flat
Verilog-A module with component/device provenance. Simulation-only primitives
and digital operations are rejected for reusable IC export.
)";
}
bool member(std::string_view value) {
    if (value.empty() || !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
        return false;
    for (char c : value)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            return false;
    return true;
}
bool type_expression(std::string_view value) {
    if (value.empty())
        return false;
    for (char c : value)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '<' || c == '>' || c == ',' ||
              c == ' '))
            return false;
    return true;
}
#if !defined(_WIN32)
int execute(const std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    for (const auto& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    auto process = fork();
    if (process < 0)
        throw std::runtime_error("cannot start compiler: " + std::string(std::strerror(errno)));
    if (process == 0) {
        execvp(argv[0], argv.data());
        std::cerr << "cannot execute " << arguments.front() << ": " << std::strerror(errno) << '\n';
        _exit(127);
    }
    int status;
    while (waitpid(process, &status, 0) < 0)
        if (errno != EINTR)
            throw std::runtime_error("cannot wait for compiler");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
struct temporary_directory {
    std::filesystem::path path;
    temporary_directory() {
        auto pattern = (std::filesystem::temp_directory_path() / "cpphdl-veriloga-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        auto* result = mkdtemp(buffer.data());
        if (!result)
            throw std::runtime_error("cannot create elaboration directory");
        path = result;
    }
    ~temporary_directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
#endif
} // namespace

int run(int argc, const char* const* argv) {
    try {
        std::string source, top, ports, module = "AnalogIC", output, configure;
        std::string compiler = CPPHDL_ANALOG_CXX;
        std::vector<std::string> compiler_arguments;
        int first = argc > 1 && std::string_view(argv[1]) == "--verilog-a" ? 2 : 1;
        for (int i = first; i < argc; ++i) {
            std::string argument = argv[i];
            if (argument == "--") {
                for (++i; i < argc; ++i)
                    compiler_arguments.emplace_back(argv[i]);
                break;
            }
            std::string inline_value;
            bool has_inline_value = false;
            if (auto equals = argument.find('='); argument.starts_with("--") && equals != std::string::npos) {
                inline_value = argument.substr(equals + 1);
                argument.resize(equals);
                has_inline_value = true;
            }
            if (!has_inline_value && (argument == "--help" || argument == "-h")) {
                help();
                return 0;
            }
            auto value = [&](const std::string& option) {
                if (has_inline_value)
                    return inline_value;
                if (i + 1 >= argc)
                    throw std::runtime_error(option + " needs a value");
                return std::string(argv[++i]);
            };
            if (argument == "--top")
                top = value(argument);
            else if (argument == "--ports")
                ports = value(argument);
            else if (argument == "--module")
                module = value(argument);
            else if (argument == "--output" || argument == "-o")
                output = value(argument);
            else if (argument == "--configure")
                configure = value(argument);
            else if (argument == "--cxx")
                compiler = value(argument);
            else if (argument.starts_with('-'))
                throw std::runtime_error("unknown option: " + argument);
            else if (source.empty())
                source = argument;
            else
                throw std::runtime_error("only one source is accepted; pass compiler arguments after --");
        }
        if (source.empty() || !type_expression(top) || ports.empty() || !member(module) ||
            (!configure.empty() && !type_expression(configure)))
            throw std::runtime_error("provide SOURCE, --top TYPE and --ports MEMBERS; see --verilog-a --help");
        std::vector<std::string> members;
        for (std::size_t start = 0; start <= ports.size();) {
            auto end = ports.find(',', start);
            auto name = ports.substr(start, end == std::string::npos ? end : end - start);
            if (!member(name))
                throw std::runtime_error("invalid electrical member name: " + name);
            members.push_back(name);
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        auto input = std::filesystem::absolute(source);
        if (!std::filesystem::is_regular_file(input))
            throw std::runtime_error("source is not a regular file: " + input.string());
        auto destination = std::filesystem::absolute(output.empty() ? module + ".va" : output);
#if defined(_WIN32)
        throw std::runtime_error("native elaboration driver currently supports POSIX hosts; use the C++ export API");
#else
        temporary_directory temporary;
        auto harness = temporary.path / "elaborate.cpp";
        auto exported = temporary.path / "module.va";
        std::ofstream file(harness);
        file << "#include <cpphdl_veriloga.h>\n#include <iostream>\n#include <type_traits>\n"
             << "#define main cpphdl_veriloga_source_main\n#include " << std::quoted(input.string()) << '\n'
             << "#undef main\nint main() {\ntry {\nanalog::simulation sim;\n"
             << "static_assert(std::is_base_of_v<analog::IC, " << top << ">, \"top must derive from analog::IC\");\n"
             << top << " component;\n";
        if (!configure.empty())
            file << configure << "(component);\n";
        file << "component.elaborate();\nanalog::verilog_a::write_module(component, " << std::quoted(exported.string())
             << ", " << std::quoted(module) << ", {\n";
        for (const auto& name : members)
            file << '{' << std::quoted(name) << ", component." << name << "},\n";
        file << "});\nreturn 0;\n} catch (const std::exception& e) {std::cerr << e.what() << '\\n'; return 2;}\n}\n";
        file.close();
        if (!file)
            throw std::runtime_error("cannot write elaboration harness");
        auto root = std::filesystem::path(CPPHDL_ANALOG_PACKAGE_ROOT);
        auto executable = temporary.path / "elaborate";
        std::vector<std::string> command{compiler,
                                         "-std=c++20",
                                         "-O2",
                                         "-I" + (root / "include").string(),
                                         "-I" + (root / "analog").string(),
                                         "-I" + (root / "analog/include").string()};
        command.insert(command.end(), compiler_arguments.begin(), compiler_arguments.end());
        command.insert(command.end(), {harness.string(), "-o", executable.string()});
        int status = execute(command);
        if (status != 0)
            return status;
        status = execute({executable.string()});
        if (status != 0)
            return status;
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::copy_file(exported, destination, std::filesystem::copy_options::overwrite_existing);
        std::cout << destination.string() << '\n';
        return 0;
#endif
    } catch (const std::exception& error) {
        std::cerr << "Verilog-A export: " << error.what() << '\n';
        return 2;
    }
}
} // namespace cpphdl::verilog_a
