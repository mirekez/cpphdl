#pragma once

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
