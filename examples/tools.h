#pragma once

#include <algorithm>
#include <fstream>
#include <string_view>

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

inline void ToolReplaceAll(std::string& text, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

inline unsigned TribeVerilatorL2PortBits()
{
#if defined(TRIBE_L2_AXI_WIDTH)
    return TRIBE_L2_AXI_WIDTH;
#else
    return 64;
#endif
}

inline unsigned TribeVerilatorL2MemAddrBits()
{
#if defined(TRIBE_RAM_BYTES_CONFIG) && defined(TRIBE_IO_REGION_SIZE_CONFIG)
    uint64_t span = (uint64_t)TRIBE_RAM_BYTES_CONFIG + (uint64_t)TRIBE_IO_REGION_SIZE_CONFIG;
    unsigned bits = 0;
    uint64_t size = 1;
    while (size < span) {
        size <<= 1;
        ++bits;
    }
    return bits ? bits : 1;
#else
    return 32;
#endif
}

inline std::string StripL2CachePackageImports(const std::string& text)
{
    std::string filtered;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string_view line(text.data() + pos, end - pos);
        if (line.find("import L2Cache") != 0 || line.find("_pkg::*;") == std::string_view::npos) {
            filtered.append(line);
            filtered.push_back('\n');
        }
        pos = end + 1;
    }
    return filtered;
}

inline void NormalizeCopiedTribeSv(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    text = StripL2CachePackageImports(text);
    ToolReplaceAll(text, "Base_pkg::PORT_BITWIDTH", std::to_string(TribeVerilatorL2PortBits()));
    ToolReplaceAll(text, "Base_pkg::MEM_ADDR_BITS", std::to_string(TribeVerilatorL2MemAddrBits()));
    ToolReplaceAll(text, "MEM_PORTS", "4");
    const std::string mem_addr_bits = std::to_string(TribeVerilatorL2MemAddrBits());
    // OOP L2 currently emits inherited AXI address intermediates as 32-bit
    // wires. Match the copied child module port width so Verilator does not
    // reject unpacked array connections with implicit packed-width casts.
    ToolReplaceAll(text, "wire[32-1:0] l2cache__axi_in__awaddr_in[4];",
        "wire[" + mem_addr_bits + "-1:0] l2cache__axi_in__awaddr_in[4];");
    ToolReplaceAll(text, "wire[32-1:0] l2cache__axi_in__araddr_in[4];",
        "wire[" + mem_addr_bits + "-1:0] l2cache__axi_in__araddr_in[4];");
    ToolReplaceAll(text, "wire[32-1:0] l2cache__axi_out__awaddr_out[4];",
        "wire[" + mem_addr_bits + "-1:0] l2cache__axi_out__awaddr_out[4];");
    ToolReplaceAll(text, "wire[32-1:0] l2cache__axi_out__araddr_out[4];",
        "wire[" + mem_addr_bits + "-1:0] l2cache__axi_out__araddr_out[4];");

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

inline void NormalizeCopiedL2CacheOOSv(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    text = StripL2CachePackageImports(text);

    std::string filtered;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string_view line(text.data() + pos, end - pos);
        const bool inherited_alias = line.find("Base_pkg::") != std::string_view::npos;
        const bool inherited_typedef = line.find("typedef L2Cache") != std::string_view::npos;
        if (!inherited_alias && !inherited_typedef) {
            filtered.append(line);
            filtered.push_back('\n');
        }
        pos = end + 1;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << filtered;
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
    if (top_name == "Tribe") {
        NormalizeCopiedTribeSv(folder_name + "/" + top_name + ".sv");
    }
    std::string modules_list;
    for (const auto& module : modules) {
        std::filesystem::copy_file(generated_dir / (module + ".sv"), folder_name + "/" + module + ".sv", std::filesystem::copy_options::overwrite_existing);
        if (module == "Tribe") {
            NormalizeCopiedTribeSv(folder_name + "/" + module + ".sv");
        }
        if (module == "L2CacheOO") {
            NormalizeCopiedL2CacheOOSv(folder_name + "/" + module + ".sv");
        }
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
    std::vector<std::string> modules = {"Predef_pkg",
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
              "L2CacheOO",
              "BranchPredictor",
              "InterruptController",
              "Decode",
              "Execute",
              "ExecuteMem",
              "CSR",
              "MMU_TLB",
              "Writeback",
              "WritebackMem"};
    return VerilatorCompileInFolder(cpp_name, folder_base, "Tribe", modules, {
                  (source_root / "include").string(),
                  (source_root / "tribe").string(),
                  (source_root / "tribe" / "common").string(),
                  (source_root / "tribe" / "spec").string(),
                  (source_root / "tribe" / "cache").string(),
                  (source_root / "tribe" / "devices").string()});
}

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)
