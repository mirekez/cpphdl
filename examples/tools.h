#pragma once

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string_view>

inline std::string HostOptCflags()
{
    const char* extra = std::getenv("CPPHDL_HOST_OPT_FLAGS");
    return extra ? std::string(" ") + extra : std::string();
}

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

inline std::filesystem::path CpphdlBuildRootFrom(const std::filesystem::path& source_root)
{
    namespace fs = std::filesystem;

    if (const char* build_dir = std::getenv("CPPHDL_BUILD_DIR")) {
        fs::path path(build_dir);
        if (fs::is_regular_file(path / "cpphdl")) {
            return path;
        }
    }

    fs::path current = fs::current_path();
    if (fs::is_regular_file(current / "cpphdl")) {
        return current;
    }
    if (fs::is_regular_file(current / ".." / "cpphdl")) {
        return current / "..";
    }

    return source_root / "build";
}

inline std::filesystem::path CpphdlToolFrom(const std::filesystem::path& source_root)
{
    return CpphdlBuildRootFrom(source_root) / "cpphdl";
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
    const fs::path build_root = CpphdlBuildRootFrom(source_root);

    const fs::path build_generated = build_root / rel_source_dir / "generated";
    if (fs::exists(build_generated / (top_name + ".sv"))) {
        return build_generated;
    }

    if (!rel_source_dir.empty()) {
        const fs::path build_group_generated = build_root / *rel_source_dir.begin() / "generated";
        if (fs::exists(build_group_generated / (top_name + ".sv"))) {
            return build_group_generated;
        }
    }

    const fs::path build_root_generated = build_root / "generated";
    if (fs::exists(build_root_generated / (top_name + ".sv"))) {
        return build_root_generated;
    }

    return current_generated;
}

inline void AppendGeneratedModulesByPrefix(std::vector<std::string>& modules,
    const std::filesystem::path& generated_dir,
    const std::vector<std::string>& prefixes)
{
    namespace fs = std::filesystem;

    if (!fs::exists(generated_dir)) {
        return;
    }

    for (const auto& prefix : prefixes) {
        std::vector<std::string> matches;
        for (const auto& entry : fs::directory_iterator(generated_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".sv") {
                continue;
            }
            const std::string stem = entry.path().stem().string();
            if (stem.rfind(prefix, 0) == 0) {
                matches.push_back(stem);
            }
        }
        std::sort(matches.begin(), matches.end());
        for (const auto& module : matches) {
            if (std::find(modules.begin(), modules.end(), module) == modules.end()) {
                modules.push_back(module);
            }
        }
    }
}

inline std::vector<std::string> VerilatorSvImports(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::vector<std::string> imports;
    std::string line;

    while (std::getline(in, line)) {
        const std::string marker = "import ";
        const size_t import_pos = line.find(marker);
        if (import_pos == std::string::npos) {
            continue;
        }
        const size_t name_begin = import_pos + marker.size();
        const size_t name_end = line.find("::*;", name_begin);
        if (name_end == std::string::npos || name_end <= name_begin) {
            continue;
        }
        imports.push_back(line.substr(name_begin, name_end - name_begin));
    }
    return imports;
}

inline void ToolReplaceAll(std::string& text, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

inline void NormalizeCopiedL2CacheSv(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // cpphdl currently lowers Axi4If<MEM_ADDR_BITS,...> fields inside the
    // template Interface as 32-bit ports. Tribe instantiates L2 with narrowed
    // memory-side address arrays, and Verilator rejects whole unpacked-array
    // connections when the child port keeps the wrong 32-bit width.
    ToolReplaceAll(text,
        "output wire[32-1:0] axi_out__awaddr_out[MEM_PORTS]",
        "output wire[MEM_ADDR_BITS-1:0] axi_out__awaddr_out[MEM_PORTS]");
    ToolReplaceAll(text,
        "output wire[32-1:0] axi_out__araddr_out[MEM_PORTS]",
        "output wire[MEM_ADDR_BITS-1:0] axi_out__araddr_out[MEM_PORTS]");

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

inline void VerilatorCopyModuleWithImports(const std::filesystem::path& generated_dir,
    const std::string& folder_name,
    const std::string& module,
    std::vector<std::string>& ordered_modules,
    std::set<std::string>& copied,
    bool add_to_command)
{
    namespace fs = std::filesystem;

    if (copied.find(module) != copied.end()) {
        return;
    }

    const fs::path source = generated_dir / (module + ".sv");
    if (!fs::exists(source)) {
        return;
    }

    for (const auto& imported : VerilatorSvImports(source)) {
        VerilatorCopyModuleWithImports(generated_dir, folder_name, imported, ordered_modules, copied, true);
    }

    const fs::path destination = fs::path(folder_name) / (module + ".sv");
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
    copied.insert(module);

    if (module == "L2Cache") {
        NormalizeCopiedL2CacheSv(destination);
    }

    if (add_to_command) {
        ordered_modules.push_back(module);
    }
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

inline std::string VerilatorTool()
{
    if (const char* verilator = std::getenv("CPPHDL_VERILATOR")) {
        if (ToolExecutableWorks(verilator)) {
            return verilator;
        }
    }
    if (const char* verilator = std::getenv("VERILATOR")) {
        if (ToolExecutableWorks(verilator)) {
            return verilator;
        }
    }
    if (const char* conda_prefix = std::getenv("CONDA_PREFIX")) {
        std::filesystem::path conda_verilator = std::filesystem::path(conda_prefix) / "bin" / "verilator";
        if (ToolExecutableWorks(conda_verilator.string())) {
            return conda_verilator.string();
        }
    }
    return "verilator";
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

inline std::string VerilatorCxxStandardFlag(const std::string& cxx)
{
    // The Verilator make runs outside CMake, so probe the compiler selected for
    // that run instead of assuming it accepts the host build's newest spelling.
    for (const char* flag : {"-std=c++26", "-std=c++2c", "-std=c++23", "-std=c++20", "-std=c++17"}) {
        const std::string command = ToolShellQuoteString(cxx) + " " + flag
            + " -x c++ -fsyntax-only /dev/null >/dev/null 2>&1";
        if (std::system(command.c_str()) == 0) {
            return flag;
        }
    }
    return "-std=c++17";
}

inline std::string VerilatorCompilerParams(const std::string& cxx)
{
    return HostOptCflags() + " -g -O2 " + VerilatorCxxStandardFlag(cxx)
        + " -fno-strict-aliasing -Wno-unknown-warning-option"
          " -Wno-deprecated-missing-comma-variadic-parameter";
}

#if defined(TRIBE_L2_AXI_WIDTH) && defined(TRIBE_RAM_BYTES_CONFIG) && defined(TRIBE_IO_REGION_SIZE_CONFIG)
inline bool RegenerateTribeSvForVerilator(const std::filesystem::path& source_root, const std::filesystem::path& generated_dir)
{
    namespace fs = std::filesystem;

    fs::path cpphdl = CpphdlToolFrom(source_root);
    if (!fs::exists(cpphdl)) {
        std::cout << "can't find cpphdl generator in CPPHDL_BUILD_DIR, near build directory, or source root\n";
        return false;
    }

    std::string command;
    command += ToolShellQuote(cpphdl);
    command += " --generated-dir " + ToolShellQuote(generated_dir);
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
inline bool VerilatorCompileInExactFolderFromGenerated(std::string cpp_name, std::string folder_name, std::string top_name, const std::filesystem::path& generated_dir, const std::vector<std::string>& modules, const std::vector<std::string>& includes, Args&&... args);

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
    return VerilatorCompileInExactFolderFromGenerated(cpp_name, folder_name, top_name, generated_dir, modules, includes, std::forward<Args>(args)...);
};

template<typename... Args>
inline bool VerilatorCompileInExactFolderFromGenerated(std::string cpp_name, std::string folder_name, std::string top_name, const std::filesystem::path& generated_dir, const std::vector<std::string>& modules, const std::vector<std::string>& includes, Args&&... args)
{
    std::filesystem::remove_all(folder_name);
    std::filesystem::create_directory(folder_name);
    std::vector<std::string> ordered_modules;
    std::set<std::string> copied_modules;
    for (const auto& imported : VerilatorSvImports(generated_dir / (top_name + ".sv"))) {
        VerilatorCopyModuleWithImports(generated_dir, folder_name, imported, ordered_modules, copied_modules, true);
    }
    VerilatorCopyModuleWithImports(generated_dir, folder_name, top_name, ordered_modules, copied_modules, false);
    std::string modules_list;
    for (const auto& module : modules) {
        VerilatorCopyModuleWithImports(generated_dir, folder_name, module, ordered_modules, copied_modules, true);
    }
    for (const auto& module : ordered_modules) {
        modules_list += module + ".sv ";
    }
    std::string includes_list;
    for (const auto& include : includes) {
        includes_list += " -I" + include;
    }
    size_t n = 0;
    // SV parameters substitution
    ((std::ignore = std::system((std::string("gawk -i inplace '{ if ($0 ~ /parameter/) count++; if (count == ") + std::to_string(++n) +
        " ) $0 = gensub(/(parameter +[^ =]+)([ \\t]*=[^,)]*)?/, \"\\\\1 = " + std::to_string(args) +
        "\", 1); print }' " + folder_name + "/" + top_name + ".sv").c_str())), ...);
    // running Verilator
    const std::string verilator = ToolShellQuoteString(VerilatorTool());
    const std::string verilator_cxx_raw = VerilatorCxx();
    const std::string compiler_params = VerilatorCompilerParams(verilator_cxx_raw);
    if (SystemEcho((std::string("cd ") + folder_name +
            "; " + verilator + " -cc " + modules_list + " " + top_name + ".sv --exe " + cpp_name + " --top-module " + top_name +
            " --Wno-fatal --CFLAGS \"-DVERILATOR " + includes_list + " -DVERILATOR_MODEL=V" + top_name + " " + compiler_params + VerilatorExtraCflags() + "\"").c_str()) != 0) {
        return false;
    }
    const std::string verilator_cxx = ToolShellQuoteString(verilator_cxx_raw);
    return SystemEcho((std::string("cd ") + folder_name + "/obj_dir" +
        "; make -j4 -f V" + top_name + ".mk CXX=" + verilator_cxx + " LINK=\"" + verilator_cxx + " -L$CONDA_PREFIX/lib -static-libstdc++ -static-libgcc\"").c_str()) == 0;
};

inline bool VerilatorCompileTribeInFolder(std::string cpp_name, std::string folder_base, const std::filesystem::path& source_root)
{
    namespace fs = std::filesystem;

#if defined(TRIBE_L2_AXI_WIDTH) && defined(TRIBE_RAM_BYTES_CONFIG) && defined(TRIBE_IO_REGION_SIZE_CONFIG)
    // Full or parallel CTest runs may execute several Tribe Verilator tests at
    // once. Keep regenerated SV per test so one test cannot overwrite another
    // test's generated/Tribe.sv while Verilator is copying imports.
    const fs::path generated_dir = fs::current_path() / (folder_base + "_generated");
    fs::remove_all(generated_dir);
    fs::create_directories(generated_dir);
    if (!RegenerateTribeSvForVerilator(source_root, generated_dir)) {
        return false;
    }
#endif
    std::vector<std::string> modules = {"File",
              "RAM",
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
              "WritebackMem"};
    std::string folder_name = folder_base;
#if defined(TRIBE_L2_AXI_WIDTH) && defined(TRIBE_RAM_BYTES_CONFIG) && defined(TRIBE_IO_REGION_SIZE_CONFIG)
    return VerilatorCompileInExactFolderFromGenerated(cpp_name, folder_name, "Tribe", generated_dir, modules, {
#else
    return VerilatorCompileInFolder(cpp_name, folder_name, "Tribe", modules, {
#endif
                  (source_root / "include").string(),
                  (source_root / "tribe").string(),
                  (source_root / "tribe" / "common").string(),
                  (source_root / "tribe" / "spec").string(),
                  (source_root / "tribe" / "cache").string(),
                  (source_root / "tribe" / "devices").string()});
}

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)
