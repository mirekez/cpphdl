#define MAIN_FILE_INCLUDED
#include "../main.cpp"

#if !defined(SYNTHESIS)

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <vector>

static constexpr uint32_t LINUX_MEM_BASE = 0x80000000u;
static constexpr uint32_t LINUX_RAM_BYTES = 32u * 1024u * 1024u;
static constexpr uint32_t LINUX_INITRAMFS_ADDR = 0x81c00000u;
static constexpr uint32_t LINUX_DTB_ADDR = 0x81f00000u;

static std::filesystem::path source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

static std::filesystem::path linux_source_dir()
{
    return source_root_dir() / "tribe" / "linux";
}

static std::filesystem::path linux_work_dir()
{
    return std::filesystem::current_path() / "linux-work";
}

static std::string shell_quote(const std::filesystem::path& path)
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

static bool run_command(const std::string& command)
{
    int rc = std::system(command.c_str());
    if (rc != 0) {
        std::print("command failed: {}\n", command);
        return false;
    }
    return true;
}

static bool newer_than(const std::filesystem::path& a, const std::filesystem::path& b)
{
    return std::filesystem::exists(a) && (!std::filesystem::exists(b) ||
        std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b));
}

static std::vector<uint8_t> read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static bool write_file_bytes(const std::filesystem::path& path, const std::vector<uint8_t>& data)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::print("can't write {}\n", path.string());
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
    return true;
}

static bool has_newc_cpio_magic(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    char magic[6] = {};
    file.read(magic, sizeof(magic));
    return file.gcount() == (std::streamsize)sizeof(magic) && std::memcmp(magic, "070701", 6) == 0;
}

static uint32_t parse_cpio_hex(const std::vector<uint8_t>& data, size_t pos)
{
    uint32_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        char ch = (char)data[pos + i];
        value <<= 4;
        if (ch >= '0' && ch <= '9') {
            value |= (uint32_t)(ch - '0');
        }
        else {
            value |= (uint32_t)(std::tolower((unsigned char)ch) - 'a' + 10);
        }
    }
    return value;
}

static void append_cpio_hex(std::vector<uint8_t>& out, uint32_t value)
{
    char text[9];
    std::snprintf(text, sizeof(text), "%08x", value);
    out.insert(out.end(), text, text + 8);
}

static size_t align4(size_t value)
{
    return (value + 3u) & ~size_t(3u);
}

static bool build_probe_initramfs(const std::filesystem::path& base_cpio, const std::filesystem::path& output_cpio)
{
    const std::string init_script =
        "#!/bin/sh\n"
        "\n"
        "mount -t proc proc /proc\n"
        "mount -t sysfs sysfs /sys\n"
        "mount -t devtmpfs devtmpfs /dev 2>/dev/null || true\n"
        "\n"
        "echo\n"
        "echo \"RISC-V initramfs started\"\n"
        "echo \"BusyBox probe follows\"\n"
        "/bin/busybox\n"
        "echo \"BusyBox shell ready\"\n"
        "\n"
        "exec /bin/sh -i </dev/console >/dev/console 2>&1\n";

    std::vector<uint8_t> data = read_file_bytes(base_cpio);
    if (data.empty()) {
        std::print("can't read initramfs base {}\n", base_cpio.string());
        return false;
    }

    std::vector<uint8_t> out;
    bool replaced = false;
    size_t pos = 0;
    while (pos + 110 <= data.size()) {
        if (std::memcmp(data.data() + pos, "070701", 6) != 0) {
            std::print("bad newc initramfs header at offset {}\n", pos);
            return false;
        }

        uint32_t fields[13] = {};
        for (size_t i = 0; i < 13; ++i) {
            fields[i] = parse_cpio_hex(data, pos + 6 + i * 8);
        }

        size_t file_size = fields[6];
        size_t name_size = fields[11];
        size_t name_start = pos + 110;
        size_t name_end = name_start + name_size;
        size_t body_start = align4(name_end);
        size_t body_end = body_start + file_size;
        if (name_end > data.size() || body_end > data.size()) {
            std::print("truncated newc initramfs entry at offset {}\n", pos);
            return false;
        }

        std::string name(reinterpret_cast<const char*>(data.data() + name_start), name_size ? name_size - 1 : 0);
        const uint8_t* body = data.data() + body_start;
        size_t body_size = file_size;
        if (name == "init" || name == "./init") {
            fields[6] = (uint32_t)init_script.size();
            body = reinterpret_cast<const uint8_t*>(init_script.data());
            body_size = init_script.size();
            replaced = true;
        }

        out.insert(out.end(), {'0', '7', '0', '7', '0', '1'});
        for (uint32_t field : fields) {
            append_cpio_hex(out, field);
        }
        out.insert(out.end(), data.begin() + (std::ptrdiff_t)name_start, data.begin() + (std::ptrdiff_t)name_end);
        out.resize(align4(out.size()), 0);
        out.insert(out.end(), body, body + body_size);
        out.resize(align4(out.size()), 0);

        pos = align4(body_end);
        if (name == "TRAILER!!!") {
            break;
        }
    }

    if (!replaced) {
        std::print("failed to replace /init in initramfs\n");
        return false;
    }
    return write_file_bytes(output_cpio, out);
}

static bool write_dts_with_initrd(const std::filesystem::path& input_dts,
                                  const std::filesystem::path& output_dts,
                                  uint32_t initrd_start,
                                  uint32_t initrd_end)
{
    std::ifstream in(input_dts);
    std::ofstream out(output_dts);
    if (!in || !out) {
        std::print("can't open DTS input/output\n");
        return false;
    }

    std::string line;
    bool in_chosen = false;
    bool inserted = false;
    while (std::getline(in, line)) {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
        if (trimmed == "chosen {") {
            in_chosen = true;
        }
        else if (in_chosen && trimmed == "};" && !inserted) {
            out << "\t\tlinux,initrd-start = <0x" << std::hex << initrd_start << ">;\n";
            out << "\t\tlinux,initrd-end = <0x" << std::hex << initrd_end << ">;\n";
            inserted = true;
            in_chosen = false;
        }
        else if (in_chosen && trimmed.rfind("linux,initrd-", 0) == 0) {
            continue;
        }
        out << line << "\n";
    }

    if (!inserted) {
        std::print("failed to find /chosen in DTS {}\n", input_dts.string());
        return false;
    }
    return true;
}

static std::filesystem::path find_dtc()
{
    if (const char* env = std::getenv("DTC")) {
        if (std::filesystem::exists(env)) {
            return env;
        }
    }
    if (std::system("command -v dtc >/dev/null 2>&1") == 0) {
        return "dtc";
    }
    auto linux_dtc = source_root_dir().parent_path() / "3" / "riscv32_linux_from_scratch" /
        "build" / "linux-rv32" / "scripts" / "dtc" / "dtc";
    if (std::filesystem::exists(linux_dtc)) {
        return linux_dtc;
    }
    return {};
}

static bool prepare_linux_inputs(std::filesystem::path& vmlinux,
                                 std::filesystem::path& initramfs,
                                 std::filesystem::path& dtb)
{
    const auto src = linux_source_dir();
    const auto work = linux_work_dir();
    std::filesystem::create_directories(work);

    const auto archive = src / "vmlinux.tgz";
    vmlinux = work / "vmlinux";
    if (newer_than(archive, vmlinux)) {
        if (!std::filesystem::exists(archive)) {
            std::print("missing {}\n", archive.string());
            return false;
        }
        if (!run_command("tar -xzf " + shell_quote(archive) + " -C " + shell_quote(work))) {
            return false;
        }
    }

    const auto initramfs_gz = src / "initramfs.cpio.gz";
    const auto raw_initramfs = work / "initramfs.base.raw";
    const auto base_cpio = work / "initramfs.base.cpio";
    initramfs = work / "initramfs.cpio";
    if (newer_than(initramfs_gz, raw_initramfs)) {
        if (!run_command("gzip -dc " + shell_quote(initramfs_gz) + " > " + shell_quote(raw_initramfs))) {
            return false;
        }
    }
    if (newer_than(raw_initramfs, base_cpio) || !has_newc_cpio_magic(base_cpio)) {
        if (has_newc_cpio_magic(raw_initramfs)) {
            std::filesystem::copy_file(raw_initramfs, base_cpio, std::filesystem::copy_options::overwrite_existing);
        }
        else {
            // Some checked-in initramfs artifacts are gzip-compressed tar files
            // containing initramfs.cpio; extract that member before rewriting /init.
            if (!run_command("tar -xOf " + shell_quote(raw_initramfs) + " initramfs.cpio > " + shell_quote(base_cpio))) {
                return false;
            }
        }
    }
    if (newer_than(base_cpio, initramfs)) {
        if (!build_probe_initramfs(base_cpio, initramfs)) {
            return false;
        }
    }

    const auto dts = src / "config32.dts";
    const auto initrd_dts = work / "config32.initramfs.dts";
    dtb = work / "config32.initramfs.dtb";
    if (newer_than(dts, initrd_dts) || newer_than(initramfs, initrd_dts)) {
        uint32_t initrd_end = LINUX_INITRAMFS_ADDR + (uint32_t)std::filesystem::file_size(initramfs);
        if (!write_dts_with_initrd(dts, initrd_dts, LINUX_INITRAMFS_ADDR, initrd_end)) {
            return false;
        }
    }
    if (newer_than(initrd_dts, dtb)) {
        const auto dtc = find_dtc();
        if (dtc.empty()) {
            std::print("missing dtc; set DTC or install device-tree-compiler\n");
            return false;
        }
        if (!run_command(shell_quote(dtc) + " -I dts -O dtb -o " + shell_quote(dtb) + " " + shell_quote(initrd_dts))) {
            return false;
        }
    }

    return std::filesystem::exists(vmlinux) && std::filesystem::exists(initramfs) && std::filesystem::exists(dtb);
}

static bool run_linux(bool debug,
                      const std::string& marker,
                      int cycles,
                      const std::string& checkpoint_load,
                      const std::string& checkpoint_save,
                      uint64_t checkpoint_save_cycle,
                      bool append_output,
                      bool checkpoint_save_only)
{
    std::filesystem::path vmlinux;
    std::filesystem::path initramfs;
    std::filesystem::path dtb;
    if (!prepare_linux_inputs(vmlinux, initramfs, dtb)) {
        return false;
    }

    const char* bootargs_env = std::getenv("TRIBE_LINUX_BOOTARGS");
    std::string bootargs = bootargs_env ? bootargs_env : "";
    return TestTribe(debug).run(vmlinux.string(),
        0, "", cycles, 0, LINUX_MEM_BASE, LINUX_RAM_BYTES / 4, false,
        0, LINUX_DTB_ADDR, 1, true, LINUX_MEM_BASE - 0xc0000000u, dtb.string(), true,
        initramfs.string(), LINUX_INITRAMFS_ADDR,
        checkpoint_load, checkpoint_save, checkpoint_save_cycle, append_output,
        bootargs, checkpoint_save_only, marker);
}

int main(int argc, char** argv)
{
    bool noveril = false;
    bool debug = false;
    bool append_output = false;
    bool checkpoint_save_only = false;
    bool prepare_only = false;
    int cycles = []() {
        const char* env = std::getenv("TRIBE_LINUX_CYCLES");
        return env ? std::stoi(env) : 300000000;
    }();
    std::string marker = []() {
        const char* env = std::getenv("TRIBE_LINUX_MARKER");
        return env ? std::string(env) : std::string("BusyBox v");
    }();
    std::string checkpoint_load;
    std::string checkpoint_save;
    uint64_t checkpoint_save_cycle = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        else if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        else if (strcmp(argv[i], "--first-uart") == 0) {
            marker = "Linux version";
        }
        else if (strcmp(argv[i], "--busybox") == 0) {
            marker = "BusyBox v";
        }
        else if (strcmp(argv[i], "--append-output") == 0) {
            append_output = true;
        }
        else if (strcmp(argv[i], "--checkpoint-save-only") == 0) {
            checkpoint_save_only = true;
        }
        else if (strcmp(argv[i], "--prepare-only") == 0) {
            prepare_only = true;
        }
        else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            cycles = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--marker") == 0 && i + 1 < argc) {
            marker = argv[++i];
        }
        else if (strcmp(argv[i], "--checkpoint-load") == 0 && i + 1 < argc) {
            checkpoint_load = argv[++i];
        }
        else if (strcmp(argv[i], "--checkpoint-save") == 0 && i + 1 < argc) {
            checkpoint_save = argv[++i];
        }
        else if (strcmp(argv[i], "--checkpoint-save-cycle") == 0 && i + 1 < argc) {
            checkpoint_save_cycle = std::stoull(argv[++i], nullptr, 0);
        }
        else {
            std::print("unknown option: {}\n", argv[i]);
            return 2;
        }
    }

    bool ok = true;
    if (prepare_only) {
        std::filesystem::path vmlinux;
        std::filesystem::path initramfs;
        std::filesystem::path dtb;
        ok = prepare_linux_inputs(vmlinux, initramfs, dtb);
        if (ok) {
            std::print("Prepared Linux inputs:\n  {}\n  {}\n  {}\n", vmlinux.string(), initramfs.string(), dtb.string());
        }
    }
    else {
        ok = run_linux(debug, marker, cycles, checkpoint_load, checkpoint_save,
            checkpoint_save_cycle, append_output, checkpoint_save_only);
    }

#ifndef VERILATOR
    if (ok && !noveril) {
        const auto source_root = source_root_dir();
        std::print("Building Linux Tribe Verilator simulation...\n");
        std::string verilator_defines =
            "-DL2_AXI_WIDTH=64 -DTRIBE_RAM_BYTES_CONFIG=" + std::to_string(LINUX_RAM_BYTES) +
            " -DTRIBE_IO_REGION_SIZE_CONFIG=1048576";
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_defines.c_str(), 1);
        ok &= VerilatorCompileTribeInFolder(__FILE__, "Linux", source_root);
        std::string command = "Linux/obj_dir/VTribe --cycles " + std::to_string(cycles) +
            " --marker " + shell_quote(marker);
        if (debug) {
            command += " --debug";
        }
        if (append_output) {
            command += " --append-output";
        }
        if (checkpoint_save_only) {
            command += " --checkpoint-save-only";
        }
        if (!checkpoint_load.empty()) {
            command += " --checkpoint-load " + shell_quote(checkpoint_load);
        }
        if (!checkpoint_save.empty()) {
            command += " --checkpoint-save " + shell_quote(checkpoint_save);
        }
        if (checkpoint_save_cycle) {
            command += " --checkpoint-save-cycle " + std::to_string(checkpoint_save_cycle);
        }
        ok &= std::system(command.c_str()) == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return ok ? 0 : 1;
}

#endif
