#pragma once

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

inline std::string HostOptCflags()
{
    const char* extra = std::getenv("CPPHDL_HOST_OPT_FLAGS");
    return extra ? std::string(" ") + extra : std::string();
}

const std::string compilerParams = HostOptCflags() + " -g -O2 -std=c++26 -fno-strict-aliasing -Wno-unknown-warning-option -Wno-deprecated-missing-comma-variadic-parameter";

inline std::string VerilatorExtraCflags()
{
    const char* extra = std::getenv("CPPHDL_VERILATOR_CFLAGS");
    return extra ? std::string(" ") + extra : std::string();
}

inline int SystemEcho(const char* cmd)
{
    std::cout << cmd << "\n";
    return std::system(cmd);
}

inline std::filesystem::path CpphdlSourceRootFrom(std::string cpp_name)
{
    std::filesystem::path source = std::filesystem::absolute(cpp_name);
    for (std::filesystem::path dir = source.parent_path(); !dir.empty(); dir = dir.parent_path()) {
        if (std::filesystem::exists(dir / "include" / "cpphdl.h")
            && std::filesystem::exists(dir / "CMakeLists.txt")) {
            return dir;
        }
        if (dir == dir.root_path()) {
            break;
        }
    }
    return std::filesystem::current_path();
}

inline std::filesystem::path VerilatorGeneratedDir(std::string cpp_name, const std::string& top_name)
{
    namespace fs = std::filesystem;

    const fs::path current_generated = fs::current_path() / "generated";
    if (fs::exists(current_generated / (top_name + ".sv"))) {
        return current_generated;
    }

    const fs::path source = fs::absolute(cpp_name);
    const fs::path source_dir = source.parent_path();
    const fs::path source_root = CpphdlSourceRootFrom(cpp_name);
    const fs::path rel_source_dir = fs::relative(source_dir, source_root);

    const fs::path build_generated = source_root / "build" / rel_source_dir / "generated";
    if (fs::exists(build_generated / (top_name + ".sv"))) {
        return build_generated;
    }

    const fs::path build_root_generated = source_root / "build" / "generated";
    if (fs::exists(build_root_generated / (top_name + ".sv"))) {
        return build_root_generated;
    }

    return current_generated;
}

inline std::string ToolShellQuote(const std::filesystem::path& path)
{
    std::string text = path.string();
    std::string quoted = "'";
    for (char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        }
        else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

inline std::filesystem::path CpphdlWorkspaceRootFrom(std::string cpp_name)
{
    namespace fs = std::filesystem;

    fs::path source = fs::absolute(cpp_name);
    for (fs::path dir = source.parent_path(); !dir.empty(); dir = dir.parent_path()) {
        if (fs::exists(dir / "cpphdl" / ".conda")
            && fs::exists(dir / "cpphdl" / "include" / "cpphdl.h")) {
            return dir;
        }
        if (fs::exists(dir / ".conda")
            && fs::exists(dir / "include" / "cpphdl.h")) {
            return dir.parent_path();
        }
        if (dir == dir.root_path()) {
            break;
        }
    }
    return fs::current_path();
}

inline std::filesystem::path CpphdlCondaPrefix(std::string cpp_name)
{
    namespace fs = std::filesystem;

    const fs::path workspace_root = CpphdlWorkspaceRootFrom(cpp_name);
    const fs::path workspace_cpphdl_conda = workspace_root / "cpphdl" / ".conda";
    if (fs::exists(workspace_cpphdl_conda / "bin")) {
        return workspace_cpphdl_conda;
    }

    const fs::path cpphdl_root = CpphdlSourceRootFrom(cpp_name);
    const fs::path cpphdl_conda = cpphdl_root / ".conda";
    if (fs::exists(cpphdl_conda / "bin")) {
        return cpphdl_conda;
    }

    return {};
}

inline std::filesystem::path CpphdlCondaTool(std::string cpp_name, const std::string& tool)
{
    namespace fs = std::filesystem;

    const fs::path prefix = CpphdlCondaPrefix(cpp_name);
    if (!prefix.empty()) {
        const fs::path path = prefix / "bin" / tool;
        if (fs::exists(path)) {
            return path;
        }
    }
    return tool;
}

inline std::string VerilatorCondaEnvPrefix(std::string cpp_name)
{
    namespace fs = std::filesystem;

    const fs::path prefix = CpphdlCondaPrefix(cpp_name);
    if (prefix.empty()) {
        return {};
    }

    std::string env;
    env += "CONDA_PREFIX=" + ToolShellQuote(prefix);
    env += " PATH=" + ToolShellQuote(prefix / "bin") + ":$PATH";
    env += " LD_LIBRARY_PATH=" + ToolShellQuote(prefix / "lib") + ":${LD_LIBRARY_PATH:-} ";
    return env;
}

inline std::string CpphdlCondaToolchainArgs(std::string cpp_name)
{
    namespace fs = std::filesystem;

    const fs::path prefix = CpphdlCondaPrefix(cpp_name);
    if (prefix.empty()) {
        return {};
    }

    std::string args;
    args += " --target=x86_64-conda-linux-gnu";

    const fs::path sysroot = prefix / "x86_64-conda-linux-gnu" / "sysroot";
    if (fs::exists(sysroot)) {
        args += " --sysroot=" + ToolShellQuote(sysroot);
    }

    const fs::path clang_root = prefix / "lib" / "clang";
    if (fs::exists(clang_root)) {
        fs::path clang_include;
        for (const fs::directory_entry& entry: fs::directory_iterator(clang_root)) {
            if (entry.is_directory() && fs::exists(entry.path() / "include")) {
                clang_include = entry.path() / "include";
            }
        }
        if (!clang_include.empty()) {
            args += " -isystem " + ToolShellQuote(clang_include);
        }
    }

    const fs::path gcc_root = prefix / "lib" / "gcc" / "x86_64-conda-linux-gnu";
    if (fs::exists(gcc_root)) {
        fs::path gcc_include;
        for (const fs::directory_entry& entry: fs::directory_iterator(gcc_root)) {
            if (entry.is_directory() && fs::exists(entry.path() / "include" / "c++")) {
                gcc_include = entry.path() / "include" / "c++";
            }
        }
        if (!gcc_include.empty()) {
            args += " -isystem " + ToolShellQuote(gcc_include);
            args += " -isystem " + ToolShellQuote(gcc_include / "x86_64-conda-linux-gnu");
            args += " -isystem " + ToolShellQuote(gcc_include / "backward");
        }
    }

    const fs::path sysroot_include = sysroot / "usr" / "include";
    if (fs::exists(sysroot_include)) {
        args += " -isystem " + ToolShellQuote(sysroot_include);
    }

    return args;
}

inline bool VerilatorBuildInObjDir(std::string cpp_name, std::string folder_name, std::string top_name)
{
    namespace fs = std::filesystem;

    const fs::path prefix = CpphdlCondaPrefix(cpp_name);
    const fs::path make = CpphdlCondaTool(cpp_name, "make");
    const fs::path cxx = CpphdlCondaTool(cpp_name, "clang++");
    std::string link = cxx.string();
    if (!prefix.empty()) {
        link += " -L" + (prefix / "lib").string();
    }
    link += " -static-libstdc++ -static-libgcc";

    std::string command = "cd " + ToolShellQuote(fs::path(folder_name) / "obj_dir") + "; ";
    command += VerilatorCondaEnvPrefix(cpp_name);
    command += ToolShellQuote(make);
    command += " -j4 -f V" + top_name + ".mk";
    command += " CXX=" + ToolShellQuote(cxx);
    command += " LINK=" + ToolShellQuote(link);
    if (SystemEcho(command.c_str()) != 0) {
        return false;
    }

    const fs::path objcopy = CpphdlCondaTool(cpp_name, "llvm-objcopy");
    const fs::path bin = fs::path(folder_name) / "obj_dir" / ("V" + top_name);
    if (fs::exists(objcopy) && fs::exists(bin)) {
        std::string strip_command = ToolShellQuote(objcopy);
        strip_command += " --remove-section=.note.gnu.property " + ToolShellQuote(bin);
        (void)SystemEcho(strip_command.c_str());
    }
    return true;
}

inline std::string ToolShellQuoteString(const std::string& text)
{
    std::string quoted = "'";
    for (char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        }
        else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

inline bool ToolExecutableWorks(const std::string& executable)
{
    return std::system((ToolShellQuoteString(executable) + " --version >/dev/null 2>&1").c_str()) == 0;
}

inline std::string VerilatorCxx()
{
    if (const char* cxx = std::getenv("CPPHDL_VERILATOR_CXX")) {
        if (ToolExecutableWorks(cxx)) {
            return cxx;
        }
    }
#ifdef CPPHDL_CMAKE_CXX_COMPILER
    if (ToolExecutableWorks(CPPHDL_CMAKE_CXX_COMPILER)) {
        return CPPHDL_CMAKE_CXX_COMPILER;
    }
#endif
    if (const char* cxx = std::getenv("CXX")) {
        if (ToolExecutableWorks(cxx)) {
            return cxx;
        }
    }
    if (ToolExecutableWorks("clang++")) {
        return "clang++";
    }
    return "c++";
}

#if defined(TRIBE_L2_AXI_WIDTH) && defined(TRIBE_RAM_BYTES) && defined(TRIBE_IO_REGION_SIZE)
inline bool RegenerateTribeSvForVerilator(const std::filesystem::path& source_root)
{
    namespace fs = std::filesystem;

    fs::path cpphdl = fs::current_path() / ".." / "cpphdl";
    if (!fs::exists(cpphdl)) {
        cpphdl = source_root / "build" / "cpphdl";
    }
    if (!fs::exists(cpphdl)) {
        std::cout << "can't find cpphdl generator near build directory or source root\n";
        return false;
    }

    std::string command;
    command += ToolShellQuote(cpphdl);
    command += " " + ToolShellQuote(source_root / "tribe" / "main.cpp");
    command += " -DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
    command += " -DTRIBE_RAM_BYTES_CONFIG=" + std::to_string(TRIBE_RAM_BYTES);
    command += " -DTRIBE_IO_REGION_SIZE_CONFIG=" + std::to_string(TRIBE_IO_REGION_SIZE);
    command += " -I " + ToolShellQuote(source_root / "include");
    command += " -I " + ToolShellQuote(source_root / "tribe" / "common");
    command += " -I " + ToolShellQuote(source_root / "tribe" / "spec");
    command += " -I " + ToolShellQuote(source_root / "tribe" / "devices");
    return SystemEcho(command.c_str()) == 0;
}
#endif

template<typename... Args>
inline bool VerilatorCompileInFolder(std::string cpp_name, std::string folder_base, std::string top_name, const std::vector<std::string>& modules, const std::vector<std::string>& includes, Args&&... args);

template<typename... Args>
inline bool VerilatorCompileInExactFolder(std::string cpp_name, std::string folder_name, std::string top_name, const std::vector<std::string>& modules, const std::vector<std::string>& includes, Args&&... args);

template<typename... Args>
inline bool VerilatorCompile(std::string cpp_name, std::string name, const std::vector<std::string>& modules, const std::vector<std::string>& includes, Args&&... args)
{
    return VerilatorCompileInFolder(cpp_name, name, name, modules, includes, std::forward<Args>(args)...);
};

template<typename... Args>
inline bool VerilatorCompileInFolder(std::string cpp_name, std::string folder_base, std::string top_name, const std::vector<std::string>& modules, const std::vector<std::string>& includes, Args&&... args)
{
    std::ostringstream oss;
    ((oss << args << "_"), ...);
    std::string suffix = oss.str();
    std::string folder_name = folder_base;
    if (!suffix.empty()) {
        suffix.pop_back();
        folder_name += "_" + suffix;
    }
    return VerilatorCompileInExactFolder(cpp_name, folder_name, top_name, modules, includes, std::forward<Args>(args)...);
};

template<typename... Args>
inline bool VerilatorCompileInExactFolder(std::string cpp_name, std::string folder_name, std::string top_name, const std::vector<std::string>& modules, const std::vector<std::string>& includes, Args&&... args)
{
    const auto generated_dir = VerilatorGeneratedDir(cpp_name, top_name);
    std::filesystem::remove_all(folder_name);
    std::filesystem::create_directory(folder_name);
    std::filesystem::copy_file(generated_dir / (top_name + ".sv"), folder_name + "/" + top_name + ".sv", std::filesystem::copy_options::overwrite_existing);
    std::string modules_list;
    for (const auto& module : modules) {
        std::filesystem::copy_file(generated_dir / (module + ".sv"), folder_name + "/" + module + ".sv", std::filesystem::copy_options::overwrite_existing);
        modules_list += module + ".sv ";
    }
    std::string includes_list;
    for (const auto& include : includes) {
        includes_list += " -I" + include;
    }
    size_t n = 0;
    // SV parameters substitution
    ((std::ignore = std::system((std::string("gawk -i inplace '{ if ($0 ~ /parameter/) count++; if (count == ") + std::to_string(++n) +
        " ) sub(/^.*parameter +[^ ]+/, \"& = " + std::to_string(args) + "\"); print }' " + folder_name + "/" + top_name + ".sv").c_str())), ...);
    // running Verilator
    SystemEcho((std::string("cd ") + folder_name +
        "; verilator -cc " + modules_list + " " + top_name + ".sv --exe " + cpp_name + " --top-module " + top_name +
        " --Wno-fatal --CFLAGS \"-DVERILATOR " + includes_list + " -DVERILATOR_MODEL=V" + top_name + " " + compilerParams + VerilatorExtraCflags() + "\"").c_str());
    const std::string verilator_cxx = ToolShellQuoteString(VerilatorCxx());
    return SystemEcho((std::string("cd ") + folder_name + "/obj_dir" +
        "; make -j4 -f V" + top_name + ".mk CXX=" + verilator_cxx + " LINK=\"" + verilator_cxx + " -L$CONDA_PREFIX/lib -static-libstdc++ -static-libgcc\"").c_str()) == 0;
};

inline bool VerilatorCompileTribeInFolder(std::string cpp_name, std::string folder_base, const std::filesystem::path& source_root)
{
#if defined(TRIBE_L2_AXI_WIDTH) && defined(TRIBE_RAM_BYTES) && defined(TRIBE_IO_REGION_SIZE)
    if (!RegenerateTribeSvForVerilator(source_root)) {
        return false;
    }
#endif
    return VerilatorCompileInFolder(cpp_name, folder_base, "Tribe", {"Predef_pkg",
              "Amo_pkg",
              "Trap_pkg",
              "State_pkg",
              "Rv32i_pkg",
              "Rv32ic_pkg",
              "Rv32im_pkg",
              "Rv32ia_pkg",
              "Zicsr_pkg",
              "Alu_pkg",
              "Br_pkg",
              "Sys_pkg",
              "Csr_pkg",
              "Mem_pkg",
              "Wb_pkg",
              "L1CachePerf_pkg",
              "TribePerf_pkg",
              "File",
              "RAM1PORT",
              "L1Cache",
              "L2Cache",
              "BranchPredictor",
              "InterruptController",
              "Decode",
              "Execute",
              "ExecuteMem",
              "CSR",
              "MMU_TLB",
              "Writeback",
              "WritebackMem"}, {
                  (source_root / "include").string(),
                  (source_root / "tribe").string(),
                  (source_root / "tribe" / "common").string(),
                  (source_root / "tribe" / "spec").string(),
                  (source_root / "tribe" / "cache").string(),
                  (source_root / "tribe" / "devices").string()});
}

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)
