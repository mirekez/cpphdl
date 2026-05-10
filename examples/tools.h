#pragma once

const std::string compilerParams = " -mavx2 -g -O2 -std=c++26 -fno-strict-aliasing -Wno-unknown-warning-option -Wno-deprecated-missing-comma-variadic-parameter";

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

template<typename... Args>
inline bool VerilatorCompileInFolder(std::string cpp_name, std::string folder_base, std::string top_name, const std::vector<std::string>& modules, const std::vector<std::string>& includes, Args&&... args);

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
    std::string folder_name = folder_base + "_" + oss.str();
    folder_name.pop_back();
    std::filesystem::remove_all(folder_name);
    std::filesystem::create_directory(folder_name);
    std::filesystem::copy_file(std::string("generated/") + top_name + ".sv", folder_name + "/" + top_name + ".sv", std::filesystem::copy_options::overwrite_existing);
    std::string modules_list;
    for (const auto& module : modules) {
        std::filesystem::copy_file(std::string("generated/") + module + ".sv", folder_name + "/" + module + ".sv", std::filesystem::copy_options::overwrite_existing);
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
    return SystemEcho((std::string("cd ") + folder_name + "/obj_dir" +
        "; make -j4 -f V" + top_name + ".mk CXX=clang++ LINK=\"clang++ -L$CONDA_PREFIX/lib -static-libstdc++ -static-libgcc\"").c_str()) == 0;
};

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)
