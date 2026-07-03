#ifdef MAIN_FILE_INCLUDED
#define NO_MAINFILE
#endif
#define MAIN_FILE_INCLUDED

#include "Tribe.h"

// CppHDL INLINE TEST ///////////////////////////////////////////////////

#if !defined(SYNTHESIS)

#include "TribeTest.h"

#include <cstdlib>

class PcSymbolTable
{
    std::vector<std::pair<uint32_t, std::string>> symbols;

public:
    bool load(const char* filename)
    {
        if (!filename || !*filename) {
            return false;
        }

        std::ifstream in(filename);
        if (!in) {
            std::print("can't open PC symbol file '{}'\n", filename);
            return false;
        }

        std::string line;
        while (std::getline(in, line)) {
            std::istringstream iss(line);
            std::string addr_text;
            std::string type_text;
            std::string name;
            if (!(iss >> addr_text >> type_text >> name) || type_text.empty() ||
                type_text[0] == 'U' || type_text[0] == 'A' || type_text[0] == 'a') {
                continue;
            }

            try {
                uint32_t addr = (uint32_t)std::stoull(addr_text, nullptr, 16);
                symbols.emplace_back(addr, name);
            }
            catch (...) {
                continue;
            }
        }

        std::sort(symbols.begin(), symbols.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
        return !symbols.empty();
    }

    std::string lookup(uint32_t pc) const
    {
        if (symbols.empty()) {
            return "-";
        }

        auto it = std::upper_bound(symbols.begin(), symbols.end(), pc,
            [](uint32_t value, const auto& symbol) { return value < symbol.first; });
        if (it == symbols.begin()) {
            return "-";
        }

        --it;
        uint32_t offset = pc - it->first;
        if (offset > 0x100000u) {
            return "-";
        }
        if (offset == 0) {
            return it->second;
        }
        std::ostringstream out;
        out << it->second << "+0x" << std::hex << offset;
        return out.str();
    }
};

bool TestTribe::run(std::string filename, size_t start_offset, std::string expected_log, uint64_t max_cycles, uint32_t tohost, uint32_t mem_base, uint32_t ram_words, bool raw_program, uint32_t boot_hartid_arg, uint32_t boot_dtb_addr_arg, uint32_t boot_priv_arg, bool elf_phys_override, uint32_t elf_phys_offset, const std::string& dtb_file, bool linux_earlycon_mapbase, const std::string& initramfs_file, uint32_t initramfs_addr, const std::string& checkpoint_load_file, const std::string& checkpoint_save_file, uint64_t checkpoint_save_cycle, bool append_output, const std::string& bootargs, bool checkpoint_save_only_success, const std::string& expected_output_contains, const std::string& test_label, bool mirror_uart_output, bool interactive_uart_input, const std::string& checkpoint_save_after, const std::string& sd_image_file, const std::string& eth_tap_socket_path)
    {
        std::string label = test_label.empty() ? std::filesystem::path(filename).filename().string() : test_label;
        if (label.empty()) {
            label = filename;
        }
        const bool quiet_harness_output = std::getenv("TRIBE_TEST_QUIET");
#ifdef VERILATOR
        if (!quiet_harness_output) {
            std::print("VERILATOR TestTribe[{}]...", label);
        }
#else
        if (!quiet_harness_output) {
            std::print("CppHDL TestTribe[{}]...", label);
        }
#endif
        if (debugen_in) {
            std::print("\n");
        }

        FILE* out = fopen("out.txt", append_output ? "ab" : "wb");
        fclose(out);
        tohost_addr = tohost;
        tohost_value = 0;
        tohost_done = false;
        start_mem_addr = mem_base;
        reset_pc = mem_base;
        boot_hartid = boot_hartid_arg;
        boot_dtb_addr = boot_dtb_addr_arg;
        boot_priv = boot_priv_arg;
        ram_size = ram_words;
        (void)linux_earlycon_mapbase;
        if (ram_size == 0 || ram_size > TRIBE_RAM_BYTES/4) {
            std::print("invalid --ram-size {}; supported range is 1..{} words\n", ram_size, TRIBE_RAM_BYTES/4);
            return false;
        }

        /////////////// read program to memory
        std::vector<uint32_t> ram(MAX_RAM_SIZE / 4);
        FILE* fbin = fopen(filename.c_str(), "r");
        if (!fbin) {
            std::print("can't open file '{}'\n", filename);
            return false;
        }
        size_t read_bytes = 0;
        uint32_t elf_entry = 0;
        if (!raw_program && load_elf(fbin, ram, read_bytes, start_mem_addr, ram_size * 4, elf_entry, elf_phys_override, elf_phys_offset)) {
            reset_pc = elf_entry;
            if (!quiet_harness_output) {
                std::print("Reading ELF program into memory (size: {})\n", read_bytes);
            }
        }
        else {
            fseek(fbin, start_offset, SEEK_SET);
            read_bytes = fread(ram.data(), 1, 4 * ram_size, fbin);
            if (!quiet_harness_output) {
                std::print("Reading raw program into memory (size: {}, offset: {})\n", read_bytes, start_offset);
            }
        }
        if (!dtb_file.empty()) {
            if (!boot_dtb_addr) {
                std::print("--dtb requires --boot-dtb-addr\n");
                fclose(fbin);
                return false;
            }
            size_t dtb_bytes = 0;
            if (!load_blob(dtb_file, ram, boot_dtb_addr, start_mem_addr, ram_size * 4, dtb_bytes)) {
                fclose(fbin);
                return false;
            }
            if (!bootargs.empty() && !patch_dtb_bootargs(ram, boot_dtb_addr, (uint32_t)dtb_bytes, start_mem_addr, ram_size * 4, bootargs)) {
                fclose(fbin);
                return false;
            }
            if (!quiet_harness_output) {
                std::print("Reading DTB into memory (size: {}, addr: {:08x})\n", dtb_bytes, boot_dtb_addr);
            }
        }
        if (!initramfs_file.empty()) {
            if (!initramfs_addr) {
                std::print("--initramfs requires --initramfs-addr\n");
                fclose(fbin);
                return false;
            }
            size_t initramfs_bytes = 0;
            if (!load_blob(initramfs_file, ram, initramfs_addr, start_mem_addr, ram_size * 4, initramfs_bytes)) {
                fclose(fbin);
                return false;
            }
            if (!quiet_harness_output) {
                std::print("Reading initramfs into memory (size: {}, addr: {:08x})\n", initramfs_bytes, initramfs_addr);
            }
        }

        const size_t active_lines = (ram_size * 4 + (TRIBE_L2_AXI_WIDTH/8) - 1) / (TRIBE_L2_AXI_WIDTH/8);
        for (size_t line_idx = 0; line_idx < active_lines; ++line_idx) {
            logic<TRIBE_L2_AXI_WIDTH> line = 0;
            for (size_t word = 0; word < (TRIBE_L2_AXI_WIDTH/8) / 4; ++word) {
                size_t addr = line_idx * ((TRIBE_L2_AXI_WIDTH/8) / 4) + word;
                line.bits(word * 32 + 31, word * 32) = ram[addr];
                if (debugen_in) {
                    std::print("{:04x}: {:08x}\n", addr, ram[addr]);
                }
            }
            if (line_idx < AXI_RAM0_DEPTH) {
                mem0.ram.buffer[line_idx] = line;
            }
            else if (line_idx < AXI_RAM0_DEPTH + AXI_RAM1_DEPTH) {
                mem1.ram.buffer[line_idx - AXI_RAM0_DEPTH] = line;
            }
            else {
                mem2.ram.buffer[line_idx - AXI_RAM0_DEPTH - AXI_RAM1_DEPTH] = line;
            }
        }
        fclose(fbin);
        ///////////////////////////////////////

        if (!sd_image_file.empty()) {
            if (!sdcard_verif.load_image(sd_image_file)) {
                std::print("can't open SD image '{}'\n", sd_image_file);
                return false;
            }
            if (!quiet_harness_output) {
                std::print("Reading SD image into card model (size: {}, file: {})\n",
                    sdcard_verif.image_size(), sd_image_file);
            }
        }

        auto save_sd_image = [&]() {
            if (sd_image_file.empty()) {
                return true;
            }
            if (!sdcard_verif.save_image(sd_image_file)) {
                std::print("*** can't write SD image '{}' ***\n", sd_image_file);
                return false;
            }
            return true;
        };

        __inst_name = "tribe_test";
        _assign();
        _strobe();
        ++_system_clock;
        _work(1);
        _strobe_neg();
        _work_neg(1);

        auto start = std::chrono::high_resolution_clock::now();
        perf_reset();
        eth_loopback_enabled = std::getenv("TRIBE_ETH_LOOPBACK") != nullptr;
        if (!eth_tap_socket_path.empty() && !eth_tap_socket.open(eth_tap_socket_path)) {
            return false;
        }
        const char* uart_input_env = std::getenv("TRIBE_UART_INPUT");
        const char* uart_input_after_env = std::getenv("TRIBE_UART_INPUT_AFTER");
        std::string scripted_uart_input = uart_input_env ? uart_input_env : "";
        std::string scripted_uart_after = uart_input_after_env ? uart_input_after_env : "";
        const char* uart_input_char_delay_env = std::getenv("TRIBE_UART_INPUT_CHAR_DELAY");
        uint32_t scripted_uart_char_delay = uart_input_char_delay_env ? std::stoul(uart_input_char_delay_env, nullptr, 0) : 0;
        const char* uart_input_start_delay_env = std::getenv("TRIBE_UART_INPUT_START_DELAY");
        uint32_t scripted_uart_start_delay = uart_input_start_delay_env ? std::stoul(uart_input_start_delay_env, nullptr, 0) : 0;
        bool checkpoint_loaded_pending_work = false;
        if (!checkpoint_load_file.empty()) {
            FILE* checkpoint_in = fopen(checkpoint_load_file.c_str(), "rb");
            if (!checkpoint_in) {
                std::print("can't open checkpoint input '{}'\n", checkpoint_load_file);
                return false;
            }
            _strobe(checkpoint_read_fd(checkpoint_in));
            fclose(checkpoint_in);
            // Checkpoint loading restores _system_clock together with registers and
            // memories. Move to a fresh comb epoch before reading restored
            // UART/PLIC/MMU outputs; otherwise lazy comb caches can still carry
            // pre-load host-process values.
            ++_system_clock;
            checkpoint_loaded_pending_work = true;
            if (!scripted_uart_input.empty() && std::getenv("TRIBE_UART_INPUT_RESTART_ON_LOAD")) {
                // Most checkpoint users need the serialized feeder registers
                // restored exactly. This opt-in restart is only for loading an
                // unrelated checkpoint and injecting a fresh host script.
                init_uart_script_state(scripted_uart_after.empty(),
                    scripted_uart_after.empty() ? scripted_uart_start_delay : 0);
            }
            if (!quiet_harness_output) {
                std::print("Loaded checkpoint '{}'\n", checkpoint_load_file);
            }
            if (!sd_image_file.empty() && std::getenv("TRIBE_SD_IMAGE_OVERRIDE_CHECKPOINT")) {
                if (!sdcard_verif.load_image(sd_image_file)) {
                    std::print("can't reload SD image '{}' after checkpoint\n", sd_image_file);
                    return false;
                }
                if (!quiet_harness_output) {
                    std::print("Reloaded SD image after checkpoint (size: {}, file: {})\n",
                        sdcard_verif.image_size(), sd_image_file);
                }
            }
        }
        const char* trace_period_env = std::getenv("TRIBE_TRACE_PC_PERIOD");
        uint32_t trace_period = trace_period_env ? std::stoul(trace_period_env, nullptr, 0) : 0;
        const char* trace_pc_file_env = std::getenv("TRIBE_TRACE_PC_FILE");
        FILE* trace_pc_file = nullptr;
        if (trace_pc_file_env && trace_pc_file_env[0]) {
            trace_pc_file = fopen(trace_pc_file_env, "wb");
            if (!trace_pc_file) {
                std::print("can't open PC trace file '{}'\n", trace_pc_file_env);
                return false;
            }
            setvbuf(trace_pc_file, nullptr, _IOLBF, 0);
        }
        PcSymbolTable pc_symbols;
        pc_symbols.load(std::getenv("TRIBE_TRACE_PC_SYMBOLS_FILE"));
        const char* trace_addr_env = std::getenv("TRIBE_TRACE_ADDR");
        uint32_t trace_addr = trace_addr_env ? std::stoul(trace_addr_env, nullptr, 0) : 0;
        const char* trace_addr_from_env = std::getenv("TRIBE_TRACE_ADDR_FROM");
        const char* trace_addr_to_env = std::getenv("TRIBE_TRACE_ADDR_TO");
        uint32_t trace_addr_from = trace_addr_from_env ? std::stoul(trace_addr_from_env, nullptr, 0) : 0;
        uint32_t trace_addr_to = trace_addr_to_env ? std::stoul(trace_addr_to_env, nullptr, 0) : 0;
        const char* trace_addr_file_env = std::getenv("TRIBE_TRACE_ADDR_FILE");
        FILE* trace_addr_file = nullptr;
        if (trace_addr_file_env && trace_addr_file_env[0]) {
            trace_addr_file = fopen(trace_addr_file_env, "wb");
            if (!trace_addr_file) {
                std::print("can't open address trace file '{}'\n", trace_addr_file_env);
                return false;
            }
            setvbuf(trace_addr_file, nullptr, _IOLBF, 0);
        }
        const char* trace_pc_from_env = std::getenv("TRIBE_TRACE_PC_FROM");
        const char* trace_pc_to_env = std::getenv("TRIBE_TRACE_PC_TO");
        uint32_t trace_pc_from = trace_pc_from_env ? std::stoul(trace_pc_from_env, nullptr, 0) : 0;
        uint32_t trace_pc_to = trace_pc_to_env ? std::stoul(trace_pc_to_env, nullptr, 0) : 0;
        const char* debug_start_env = std::getenv("TRIBE_DEBUG_START");
        uint32_t debug_start = debug_start_env ? std::stoul(debug_start_env, nullptr, 0) : 0;
        const char* debug_pc_ge_env = std::getenv("TRIBE_DEBUG_PC_GE");
        uint32_t debug_pc_ge = debug_pc_ge_env ? std::stoul(debug_pc_ge_env, nullptr, 0) : 0;
        const bool trace_mmu = std::getenv("TRIBE_TRACE_MMU") != nullptr;
        const bool trace_csr = std::getenv("TRIBE_TRACE_CSR") != nullptr;
        const char* trace_csr_file_env = std::getenv("TRIBE_TRACE_CSR_FILE");
        FILE* trace_csr_file = nullptr;
        if (trace_csr_file_env && trace_csr_file_env[0]) {
            trace_csr_file = fopen(trace_csr_file_env, "ab");
            if (!trace_csr_file) {
                std::print("can't open CSR trace file '{}'\n", trace_csr_file_env);
                return false;
            }
            setvbuf(trace_csr_file, nullptr, _IOLBF, 0);
        }
        const bool trace_wb = std::getenv("TRIBE_TRACE_WB") != nullptr;
        const bool trace_io = std::getenv("TRIBE_TRACE_IO") != nullptr;
        const bool trace_sd = std::getenv("TRIBE_TRACE_SD") != nullptr;
        const bool trace_clint = std::getenv("TRIBE_TRACE_CLINT") != nullptr;
        const char* trace_clint_file_env = std::getenv("TRIBE_TRACE_CLINT_FILE");
        FILE* trace_clint_file = nullptr;
        if (trace_clint_file_env && trace_clint_file_env[0]) {
            trace_clint_file = fopen(trace_clint_file_env, "wb");
            if (!trace_clint_file) {
                std::print("can't open CLINT trace file '{}'\n", trace_clint_file_env);
                return false;
            }
            setvbuf(trace_clint_file, nullptr, _IOLBF, 0);
        }
        const char* trace_sd_file_env = std::getenv("TRIBE_TRACE_SD_FILE");
        FILE* trace_sd_file = nullptr;
        if (trace_sd_file_env && trace_sd_file_env[0]) {
            trace_sd_file = fopen(trace_sd_file_env, "wb");
            if (!trace_sd_file) {
                std::print("can't open SD trace file '{}'\n", trace_sd_file_env);
                return false;
            }
            setvbuf(trace_sd_file, nullptr, _IOLBF, 0);
        }
        const bool trace_sbi = std::getenv("TRIBE_TRACE_SBI") != nullptr;
        const bool trace_mmu_fault = std::getenv("TRIBE_TRACE_MMU_FAULT") != nullptr;
        const bool trace_ra = std::getenv("TRIBE_TRACE_RA") != nullptr;
        const char* trace_reg_id_env = std::getenv("TRIBE_TRACE_REG_ID");
        const int trace_reg_id = trace_reg_id_env ? std::stoi(trace_reg_id_env, nullptr, 0) : -1;
        const char* trace_reg_file_env = std::getenv("TRIBE_TRACE_REG_FILE");
        FILE* trace_reg_file = nullptr;
        if (trace_reg_file_env && trace_reg_file_env[0]) {
            trace_reg_file = fopen(trace_reg_file_env, "wb");
            if (!trace_reg_file) {
                std::print("can't open register trace file '{}'\n", trace_reg_file_env);
                return false;
            }
            setvbuf(trace_reg_file, nullptr, _IOLBF, 0);
        }
        const bool trace_bad_branch = std::getenv("TRIBE_TRACE_BAD_BRANCH") != nullptr;
        const char* trace_uart_rx_file_env = std::getenv("TRIBE_TRACE_UART_RX_FILE");
        FILE* trace_uart_rx_file = nullptr;
        if (trace_uart_rx_file_env && trace_uart_rx_file_env[0]) {
            trace_uart_rx_file = fopen(trace_uart_rx_file_env, "wb");
            if (!trace_uart_rx_file) {
                std::print("can't open UART RX trace file '{}'\n", trace_uart_rx_file_env);
                return false;
            }
            setvbuf(trace_uart_rx_file, nullptr, _IOLBF, 0);
        }
        const bool trace_uart_rx = std::getenv("TRIBE_TRACE_UART_RX") != nullptr || trace_uart_rx_file != nullptr;
        auto uart_rx_trace_out = [&]() {
            return trace_uart_rx_file ? trace_uart_rx_file : stdout;
        };
        const bool uart_ctrl_c_to_guest = std::getenv("TRIBE_UART_CTRL_C_TO_GUEST") != nullptr;
        const bool uart_ctrl_z_to_guest = std::getenv("TRIBE_UART_CTRL_Z_TO_GUEST") != nullptr;
        const char* trace_after_env = std::getenv("TRIBE_TRACE_AFTER");
        std::string trace_after = trace_after_env ? trace_after_env : "";
        bool trace_after_seen = trace_after.empty();
        bool last_immu_fault = false;
        std::string expected_output;
        std::string captured_output;
        bool expected_marker_seen = false;
        bool checkpoint_save_after_seen = false;
        bool checkpoint_save_after_completed = false;
        bool mirrored_uart_needs_newline = false;
        bool host_interrupt = false;
        std::deque<unsigned char> interactive_uart_queue;
        bool interactive_uart_previous_cr = false;
        uint64_t interactive_uart_last_block_report = 0;
        bool trace_sd_prev_active = false;
        uint32_t trace_sd_prev_addr = 0;
        bool trace_sd_prev_read = false;
        bool trace_sd_prev_write = false;
        uint32_t trace_sd_prev_wdata = 0;
        uint32_t trace_sd_prev_mask = 0;
        uint32_t trace_sd_prev_status = 0xffffffffu;
        uint32_t trace_sd_prev_state = 0xffffffffu;
        uint32_t trace_sd_prev_count_bucket = 0xffffffffu;
        uint32_t trace_sd_prev_len = 0xffffffffu;
        bool trace_sd_prev_cmd_valid = false;
        bool trace_sd_prev_cmd_ready = false;
        uint32_t trace_sd_prev_cmd_data = 0xffffffffu;
        bool trace_sd_prev_rsp_valid = false;
        bool trace_sd_prev_rsp_ready = false;
        uint32_t trace_sd_prev_rsp_data = 0xffffffffu;
        bool trace_sd_prev_dma_awvalid = false;
        bool trace_sd_prev_dma_awready = false;
        bool trace_sd_prev_dma_wvalid = false;
        bool trace_sd_prev_dma_wready = false;
        bool trace_sd_prev_dma_bvalid = false;
        bool trace_sd_prev_dma_bready = false;
        bool trace_sd_prev_dma_arvalid = false;
        bool trace_sd_prev_dma_arready = false;
        bool trace_sd_prev_dma_rvalid = false;
        bool trace_sd_prev_dma_rready = false;
        uint64_t trace_sd_last_poll_report = 0;
        const bool trace_sd_data = std::getenv("TRIBE_TRACE_SD_DATA") != nullptr;
        if (checkpoint_load_file.empty()) {
            init_uart_script_state(scripted_uart_after.empty(), scripted_uart_after.empty() ? scripted_uart_start_delay : 0);
        }
        StdinRawMode stdin_raw(interactive_uart_input);
        if (!tohost_addr && expected_output_contains.empty()) {
            std::ifstream expected_file(expected_log, std::ios::binary);
            if (!expected_file) {
                std::print("can't open expected output '{}'\n", expected_log);
                error = true;
            }
            else {
                expected_output.assign(std::istreambuf_iterator<char>(expected_file), std::istreambuf_iterator<char>());
            }
        }
        auto output_file_reached_expected = [&]() {
            if (tohost_addr || expected_output.empty()) {
                return false;
            }
            std::ifstream out_file("out.txt", std::ios::binary);
            if (!out_file) {
                return false;
            }
            std::string current_output((std::istreambuf_iterator<char>(out_file)), std::istreambuf_iterator<char>());
            if (current_output.size() < expected_output.size()) {
                return false;
            }
            error = current_output != expected_output;
            return true;
        };
        auto capture_uart_output = [&]() {
            if (!uart.uart_valid_out()) {
                return false;
            }

            FILE* uart_out = fopen("out.txt", "ab");
            if (!uart_out) {
                return false;
            }

            char ch = (char)uart.uart_data_out();
            fputc(ch, uart_out);
            fclose(uart_out);
            if (mirror_uart_output) {
                (void)write(STDOUT_FILENO, &ch, 1);
                fflush(stdout);
                mirrored_uart_needs_newline = ch != '\n';
            }
            captured_output.push_back(ch);
            if (!trace_after_seen && !trace_after.empty() &&
                captured_output.find(trace_after) != std::string::npos) {
                trace_after_seen = true;
                std::print("\n*** trace enabled after '{}' at cycle {} ***\n", trace_after, perf_clocks);
            }
            if (!checkpoint_save_after.empty() && !checkpoint_save_file.empty() &&
                !checkpoint_save_after_seen &&
                captured_output.find(checkpoint_save_after) != std::string::npos) {
                checkpoint_save_after_seen = true;
            }
            if (!(bool)uart_script_enabled_reg && !scripted_uart_input.empty() &&
                captured_output.find(scripted_uart_after) != std::string::npos) {
                enable_uart_script(scripted_uart_start_delay);
                if (trace_uart_rx && !(bool)uart_script_reported_reg) {
                    mark_uart_script_reported();
                    std::print(uart_rx_trace_out(), "*** uart-rx-script-enabled after '{}' ***\n", scripted_uart_after);
                }
            }
            if (!tohost_addr) {
                if (!expected_output_contains.empty() && captured_output.find(expected_output_contains) != std::string::npos) {
                    expected_marker_seen = true;
                    return true;
                }
                if (!expected_output.empty() && captured_output.size() >= expected_output.size()) {
                    error = captured_output != expected_output;
                    return true;
                }
            }
            return false;
        };
        const bool unlimited_cycles = max_cycles == 0;
        uint64_t cycles_remaining = max_cycles;
        bool cycle_limit_reached = false;
        bool checkpoint_saved = false;
        while (!error && !tohost_done && !host_interrupt) {
            if (!unlimited_cycles && cycles_remaining-- == 0) {
                cycle_limit_reached = true;
                break;
            }
            uint64_t cycle_time_start = tribe_runtime_tick();
            uint64_t section_time_start = cycle_time_start;
            uint64_t strobe_ticks_before = runtime_strobe_ticks;
            if (checkpoint_loaded_pending_work) {
                checkpoint_loaded_pending_work = false;
            }
            else {
                FILE* checkpoint_out = nullptr;
                bool checkpoint_due = !checkpoint_saved && !checkpoint_save_file.empty() &&
                    ((checkpoint_save_cycle && perf_clocks + 1 == checkpoint_save_cycle) || checkpoint_save_after_seen);
                if (checkpoint_due) {
                    checkpoint_out = fopen(checkpoint_save_file.c_str(), "wb");
                    if (!checkpoint_out) {
                        std::print("*** can't open checkpoint output '{}' ***\n", checkpoint_save_file);
                        error = true;
                        break;
                    }
                }
                uint64_t strobe_time_start = tribe_runtime_tick();
                _strobe(checkpoint_out);
                runtime_strobe_ticks += tribe_runtime_tick() - strobe_time_start;
                if (checkpoint_out) {
                    fclose(checkpoint_out);
                    checkpoint_saved = true;
                    std::print("*** Saved checkpoint '{}' at cycle {} ***\n", checkpoint_save_file, perf_clocks + 1);
                    if (checkpoint_save_only_success) {
                        // The checkpoint stores UART output-valid state. If a
                        // byte is visible on this save cycle, a later restore
                        // must be the single place that captures it.
                        break;
                    }
                    if (checkpoint_save_after_seen && !checkpoint_save_after.empty()) {
                        checkpoint_save_after_completed = true;
                        tohost_value = 1;
                        tohost_done = true;
                    }
                }
            }
            runtime_checkpoint_ticks += tribe_runtime_tick() - section_time_start - (runtime_strobe_ticks - strobe_ticks_before);
            section_time_start = tribe_runtime_tick();
            ++_system_clock;
            perf_sample();
            if (capture_uart_output()) {
                break;
            }
            drive_uart_rx(false);
            if (interactive_uart_input) {
                if (tribe_uart_stdin_sigint_pending) {
                    tribe_uart_stdin_sigint_pending = 0;
                    if (uart_ctrl_c_to_guest) {
                        interactive_uart_queue.push_back(0x03);
                    }
                    else {
                        host_interrupt = true;
                    }
                }
                if (tribe_uart_stdin_sigtstp_pending) {
                    tribe_uart_stdin_sigtstp_pending = 0;
                    if (uart_ctrl_z_to_guest) {
                        interactive_uart_queue.push_back(0x1a);
                    }
                    else {
                        std::print("\n*** Suspended by Ctrl+Z; use 'fg' to resume ***\n");
                        stdin_raw.suspend_to_shell();
                    }
                }
                for (size_t stdin_reads = 0; stdin_reads < 64 && !host_interrupt; ++stdin_reads) {
                    unsigned char ch = 0;
                    ssize_t got = read(STDIN_FILENO, &ch, 1);
                    if (got == 1) {
                        if (ch == 0x03 && !uart_ctrl_c_to_guest) {
                            host_interrupt = true;
                            std::print("\n*** Interrupted by Ctrl+C ***\n");
                            break;
                        }
                        if (ch == 0x1a && !uart_ctrl_z_to_guest) {
                            std::print("\n*** Suspended by Ctrl+Z; use 'fg' to resume ***\n");
                            stdin_raw.suspend_to_shell();
                            break;
                        }
                        if (!normalize_interactive_uart_byte(ch, interactive_uart_previous_cr, ch)) {
                            continue;
                        }
                        if (interactive_uart_queue.size() < 4096) {
                            interactive_uart_queue.push_back(ch);
                            if (trace_uart_rx) {
                                std::print(uart_rx_trace_out(), "uart-rx-stdin-queue data={:02x} queued={}\n",
                                    (uint32_t)ch, interactive_uart_queue.size());
                            }
                        }
                        else if (trace_uart_rx) {
                            std::print(uart_rx_trace_out(), "uart-rx-stdin-drop data={:02x} queued={}\n",
                                (uint32_t)ch, interactive_uart_queue.size());
                        }
                        continue;
                    }
                    if (got == 0 || (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
                        break;
                    }
                    std::print("\n***stdin read failed while feeding UART***\n");
                    error = true;
                    break;
                }
                if (host_interrupt) {
                    break;
                }
                if (trace_uart_rx && !interactive_uart_queue.empty() && !uart.uart_rx_ready_out() &&
                    perf_clocks - interactive_uart_last_block_report >= 1000000ull) {
                    interactive_uart_last_block_report = perf_clocks;
                    std::print(uart_rx_trace_out(), "uart-rx-stdin-blocked queued={} ready={} irq={}\n",
                        interactive_uart_queue.size(), (bool)uart.uart_rx_ready_out(), (bool)uart.irq_out());
                }
            }
            uint32_t scripted_uart_pos = (uint32_t)uart_script_pos_reg;
            uint32_t scripted_uart_delay = (uint32_t)uart_script_delay_reg;
            if ((bool)uart_script_enabled_reg && scripted_uart_pos < scripted_uart_input.size() && scripted_uart_delay) {
                set_uart_script_delay(scripted_uart_delay - 1u);
            }
            else if (uart.uart_rx_ready_out() && (bool)uart_script_enabled_reg && scripted_uart_pos < scripted_uart_input.size()) {
                uint8_t uart_rx_data = (uint8_t)scripted_uart_input[scripted_uart_pos];
                advance_uart_script();
                set_uart_script_delay(scripted_uart_char_delay);
                drive_uart_rx(true, uart_rx_data);
                if (trace_uart_rx) {
                    std::print(uart_rx_trace_out(), "uart-rx-script-send pos={} data={:02x}\n",
                        scripted_uart_pos, (uint32_t)uart_rx_data);
                }
            }
            else if (interactive_uart_input && uart.uart_rx_ready_out() && !interactive_uart_queue.empty()) {
                unsigned char ch = interactive_uart_queue.front();
                interactive_uart_queue.pop_front();
                if (ch == 0x03 && !uart_ctrl_c_to_guest) {
                    host_interrupt = true;
                    std::print("\n*** Interrupted by Ctrl+C ***\n");
                }
                else if (ch == 0x1a && !uart_ctrl_z_to_guest) {
                    std::print("\n*** Suspended by Ctrl+Z; use 'fg' to resume ***\n");
                    stdin_raw.suspend_to_shell();
                }
                else {
                    drive_uart_rx(true, ch);
                    if (trace_uart_rx) {
                        std::print(uart_rx_trace_out(), "uart-rx-stdin-send data={:02x} queued={}\n",
                            (uint32_t)ch, interactive_uart_queue.size());
                    }
                }
            }
            runtime_uart_ticks += tribe_runtime_tick() - section_time_start;
            section_time_start = tribe_runtime_tick();
            if (debug_start && perf_clocks >= debug_start) {
                debugen_in = true;
#ifndef VERILATOR
                tribe.debugen_in = true;
#endif
            }
            if (debug_pc_ge && (uint32_t)PORT_VALUE(tribe.debug_pc_out) >= debug_pc_ge) {
                debugen_in = true;
#ifndef VERILATOR
                tribe.debugen_in = true;
#endif
            }
            runtime_perf_ticks += tribe_runtime_tick() - section_time_start;
            section_time_start = tribe_runtime_tick();
            _work(0);
            runtime_work_ticks += tribe_runtime_tick() - section_time_start;
            section_time_start = tribe_runtime_tick();
            if (trace_after_seen && trace_period && (perf_clocks % trace_period) == 0) {
                auto perf_now = PERF_VALUE(tribe.perf_out);
                uint32_t trace_pc = 0;
#ifdef ENABLE_MMU_TLB
                trace_pc = (uint32_t)PORT_VALUE(tribe.debug_pc_out);
#endif
                FILE* trace_pc_out = trace_pc_file ? trace_pc_file : stdout;
                std::print(trace_pc_out, "trace cycle={} pc={:08x} sym={} imem={:08x} dmem={:08x} rd={} wr={} data={:08x} hst={} bst={} dcw={} icw={} is={} ih={} ds={} dh={} fv={} irv={} ira={:08x} ipa={:08x} ibusy={} ifault={} mw={} wblr={} wbmemw={} iread={} istall={}\n",
                    perf_clocks,
                    trace_pc,
                    pc_symbols.lookup(trace_pc),
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                    (bool)PORT_VALUE(tribe.dmem_read_out),
                    (bool)PORT_VALUE(tribe.dmem_write_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_data_out),
                    (bool)perf_now.hazard_stall,
                    (bool)perf_now.branch_stall,
                    (bool)perf_now.dcache_wait,
                    (bool)perf_now.icache_wait,
                    (uint32_t)perf_now.icache.state,
                    (bool)perf_now.icache.hit,
                    (uint32_t)perf_now.dcache.state,
                    (bool)perf_now.dcache.hit,
#ifdef ENABLE_MMU_TLB
                    (bool)PORT_VALUE(tribe.debug_fetch_valid_out),
                    (bool)PORT_VALUE(tribe.debug_icache_read_valid_out),
                    (uint32_t)PORT_VALUE(tribe.debug_icache_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_immu_paddr_out),
                    (bool)PORT_VALUE(tribe.debug_immu_busy_out),
                    (bool)PORT_VALUE(tribe.debug_immu_fault_out),
                    (bool)PORT_VALUE(tribe.debug_memory_wait_out),
                    (bool)PORT_VALUE(tribe.debug_wb_load_ready_out),
                    (bool)PORT_VALUE(tribe.debug_wb_mem_wait_out),
                    (bool)PORT_VALUE(tribe.debug_icache_read_in_out),
                    (bool)PORT_VALUE(tribe.debug_icache_stall_in_out)
#else
                    false, false, 0u, 0u, false, false, false, false, false, false, false
#endif
                    );
                if (trace_pc_file) {
                    fflush(trace_pc_file);
                }
            }
            if (trace_after_seen && trace_wb && trace_period && (perf_clocks % trace_period) == 0) {
#ifdef ENABLE_MMU_TLB
                FILE* trace_pc_out = trace_pc_file ? trace_pc_file : stdout;
                std::print(trace_pc_out,
                    "trace-wb cycle={} pc={:08x} wbpc={:08x} wbop={} memop={} rd={} f3={} ready={} wait={} hold={} ldv={} held={} ldaddr={:08x} split_in={} split_lo_v={} split_hi_v={} alu={:08x} dcv={} dcaddr={:08x} dcd={:08x} cpu_rd={} cpu_wr={} cpu_addr={:08x} cpu_wdata={:08x} cpu_wmask={:x}\n",
                    perf_clocks,
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
                    (uint32_t)PORT_VALUE(tribe.debug_wb_state_pc_out),
                    (uint32_t)PORT_VALUE(tribe.debug_wb_state_wb_op_out),
                    (uint32_t)PORT_VALUE(tribe.debug_wb_state_mem_op_out),
                    (uint32_t)PORT_VALUE(tribe.debug_wb_state_rd_out),
                    (uint32_t)PORT_VALUE(tribe.debug_wb_state_funct3_out),
                    (bool)PORT_VALUE(tribe.debug_wb_load_ready_out),
                    (bool)PORT_VALUE(tribe.debug_wb_mem_wait_out),
                    (bool)PORT_VALUE(tribe.debug_memory_wait_out),
                    (bool)PORT_VALUE(tribe.debug_wb_load_data_valid_out),
                    (bool)PORT_VALUE(tribe.debug_wb_held_load_valid_out),
                    (uint32_t)PORT_VALUE(tribe.debug_wb_load_addr_out),
                    (bool)PORT_VALUE(tribe.debug_wb_split_load_in_out),
                    (bool)PORT_VALUE(tribe.debug_wb_split_low_valid_out),
                    (bool)PORT_VALUE(tribe.debug_wb_split_high_valid_out),
                    (uint32_t)PORT_VALUE(tribe.debug_wb_alu_addr_out),
                    (bool)PORT_VALUE(tribe.debug_dcache_read_valid_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dcache_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dcache_read_data_out),
                    (bool)PORT_VALUE(tribe.debug_dcache_cpu_read_out),
                    (bool)PORT_VALUE(tribe.debug_dcache_cpu_write_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dcache_cpu_addr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dcache_cpu_wdata_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dcache_cpu_wmask_out));
                if (trace_pc_file) {
                    fflush(trace_pc_file);
                }
#endif
            }
            if (trace_after_seen && trace_csr && trace_period && (perf_clocks % trace_period) == 0) {
                FILE* trace_csr_out = trace_csr_file ? trace_csr_file : stdout;
                std::print(trace_csr_out, "trace-csr cycle={} priv={} ra={:08x} satp={:08x} mstatus={:08x} mtvec={:08x} mepc={:08x} mcause={:08x} mtval={:08x} stvec={:08x} sepc={:08x} scause={:08x} stval={:08x}",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_priv_out),
                    (uint32_t)PORT_VALUE(tribe.debug_ra_out),
                    (uint32_t)PORT_VALUE(tribe.debug_satp_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mstatus_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mtvec_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mepc_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mcause_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mtval_out),
                    (uint32_t)PORT_VALUE(tribe.debug_stvec_out),
                    (uint32_t)PORT_VALUE(tribe.debug_sepc_out),
                    (uint32_t)PORT_VALUE(tribe.debug_scause_out),
                    (uint32_t)PORT_VALUE(tribe.debug_stval_out)
#else
                    3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
#endif
                    );
#ifdef ENABLE_ISR
                std::print(trace_csr_out, " irq_valid={} irq_cause={} irq_to_s={} irq_mip={:08x} irq_mie={:08x} irq_mideleg={:08x} external_irq={}",
                    (bool)PORT_VALUE(tribe.debug_irq_valid_out),
                    (uint32_t)PORT_VALUE(tribe.debug_irq_cause_out),
                    (bool)PORT_VALUE(tribe.debug_irq_to_supervisor_out),
			    (uint32_t)PORT_VALUE(tribe.debug_irq_mip_out),
			    (uint32_t)PORT_VALUE(tribe.debug_irq_mie_out),
			    (uint32_t)PORT_VALUE(tribe.debug_irq_mideleg_out),
#ifdef VERILATOR
			    (bool)tribe.external_irq_in);
#else
			    (bool)tribe.external_irq_in());
#endif
#endif
                std::print(trace_csr_out, "\n");
            }
            if (trace_after_seen && trace_clint && trace_period && (perf_clocks % trace_period) == 0) {
                uint64_t trace_mtime = ((uint64_t)clint.debug_mtime_hi_out() << 32) |
                    (uint32_t)clint.debug_mtime_lo_out();
                uint64_t trace_mtimecmp = ((uint64_t)clint.debug_mtimecmp_hi_out() << 32) |
                    (uint32_t)clint.debug_mtimecmp_lo_out();
                FILE* trace_clint_out = trace_clint_file ? trace_clint_file : stdout;
                std::print(trace_clint_out, "trace-clint cycle={} mtime={} mtimecmp={} delta={} mtip={} set_timer={} timer={:08x}{:08x}\n",
                    perf_clocks,
                    trace_mtime,
                    trace_mtimecmp,
                    trace_mtimecmp - trace_mtime,
                    (bool)clint.mtip_out(),
                    (bool)PORT_VALUE(tribe.sbi_set_timer_out),
                    (uint32_t)PORT_VALUE(tribe.sbi_timer_hi_out),
                    (uint32_t)PORT_VALUE(tribe.sbi_timer_lo_out));
                if (trace_clint_file) {
                    fflush(trace_clint_file);
                }
            }
#ifdef ENABLE_MMU_TLB
            if (trace_mmu_fault) {
                bool immu_fault_now = (bool)PORT_VALUE(tribe.debug_immu_fault_out);
                if (immu_fault_now && !last_immu_fault) {
                    std::print("trace-immu-fault cycle={} pc={:08x} ra={:08x} imem={:08x} satp={:08x} priv={} scause={:08x} sepc={:08x} stval={:08x} stvec={:08x} last_pte_addr={:08x} last_pte={:08x}\n",
                        perf_clocks,
                        (uint32_t)PORT_VALUE(tribe.debug_pc_out),
                        (uint32_t)PORT_VALUE(tribe.debug_ra_out),
                        (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                        (uint32_t)PORT_VALUE(tribe.debug_satp_out),
                        (uint32_t)PORT_VALUE(tribe.debug_priv_out),
                        (uint32_t)PORT_VALUE(tribe.debug_scause_out),
                        (uint32_t)PORT_VALUE(tribe.debug_sepc_out),
                        (uint32_t)PORT_VALUE(tribe.debug_stval_out),
                        (uint32_t)PORT_VALUE(tribe.debug_stvec_out),
                        (uint32_t)PORT_VALUE(tribe.debug_immu_last_addr_out),
                        (uint32_t)PORT_VALUE(tribe.debug_immu_last_pte_out));
                }
                last_immu_fault = immu_fault_now;
            }
#endif
            if (trace_sbi && (bool)PORT_VALUE(tribe.sbi_set_timer_out)) {
                std::print("trace-sbi cycle={} pc={:08x} set_timer={:08x}{:08x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.sbi_timer_hi_out),
                    (uint32_t)PORT_VALUE(tribe.sbi_timer_lo_out));
            }
            if (trace_sbi && (bool)PORT_VALUE(tribe.debug_sbi_ecall_out)) {
                std::print("trace-sbi-ecall cycle={} pc={:08x} priv={} a7={:08x} a6={:08x} a0={:08x} base={} timer={} noop={} handled={}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.debug_priv_out),
                    (uint32_t)PORT_VALUE(tribe.debug_sbi_a7_out),
                    (uint32_t)PORT_VALUE(tribe.debug_sbi_a6_out),
                    (uint32_t)PORT_VALUE(tribe.debug_sbi_a0_out),
                    (bool)PORT_VALUE(tribe.debug_sbi_base_out),
                    (bool)PORT_VALUE(tribe.sbi_set_timer_out),
                    (bool)PORT_VALUE(tribe.debug_sbi_noop_out),
                    (bool)PORT_VALUE(tribe.debug_sbi_handled_out));
            }
            if (trace_ra && (bool)PORT_VALUE(tribe.debug_regs_write_out) && (uint8_t)PORT_VALUE(tribe.debug_regs_wr_id_out) == 1) {
                std::print("trace-ra cycle={} pc={:08x} ra={:08x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.debug_regs_data_out));
            }
            if (trace_reg_id >= 0 && (bool)PORT_VALUE(tribe.debug_regs_write_out) &&
                (uint8_t)PORT_VALUE(tribe.debug_regs_wr_id_out) == (uint8_t)trace_reg_id) {
                FILE* trace_reg_out = trace_reg_file ? trace_reg_file : stdout;
                std::print(trace_reg_out, "trace-reg cycle={} pc={:08x} x{}={:08x} actual={} priv={} satp={:08x} scause={:08x} sepc={:08x} stval={:08x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    0u,
#endif
                    trace_reg_id,
                    (uint32_t)PORT_VALUE(tribe.debug_regs_data_out),
                    (bool)PORT_VALUE(tribe.debug_regs_write_actual_out),
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_priv_out),
                    (uint32_t)PORT_VALUE(tribe.debug_satp_out),
                    (uint32_t)PORT_VALUE(tribe.debug_scause_out),
                    (uint32_t)PORT_VALUE(tribe.debug_sepc_out),
                    (uint32_t)PORT_VALUE(tribe.debug_stval_out)
#else
                    3u, 0u, 0u, 0u, 0u
#endif
                    );
            }
            if (trace_bad_branch && (bool)PORT_VALUE(tribe.debug_branch_taken_now_out)) {
                uint32_t target = (uint32_t)PORT_VALUE(tribe.debug_branch_target_now_out);
                if (target < 0x10000u || (target >= 0x80000000u && target < 0x80001000u)) {
                    std::print("trace-bad-branch cycle={} pc={:08x} target={:08x} imem={:08x} dmem={:08x} rd={} wr={} wbwr={} wbid={} wbdata={:08x}\n",
                        perf_clocks,
#ifdef ENABLE_MMU_TLB
                        (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                        (uint32_t)0,
#endif
                        target,
                        (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                        (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                        (bool)PORT_VALUE(tribe.dmem_read_out),
                        (bool)PORT_VALUE(tribe.dmem_write_out),
                        (bool)PORT_VALUE(tribe.debug_regs_write_actual_out),
                        (uint8_t)PORT_VALUE(tribe.debug_regs_wr_id_out),
                        (uint32_t)PORT_VALUE(tribe.debug_regs_data_out));
                }
            }
            const uint32_t trace_daddr = (uint32_t)PORT_VALUE(tribe.dmem_addr_out);
            const bool trace_addr_hit =
                (trace_addr && trace_daddr == trace_addr) ||
                (trace_addr_from < trace_addr_to && trace_daddr >= trace_addr_from && trace_daddr < trace_addr_to);
            if (trace_addr_hit && ((bool)PORT_VALUE(tribe.dmem_read_out) || (bool)PORT_VALUE(tribe.dmem_write_out))) {
                FILE* out = trace_addr_file ? trace_addr_file : stdout;
                std::print(out, "trace-addr cycle={} pc={:08x} imem={:08x} addr={:08x} rd={} wr={} wdata={:08x} mask={:02x} source=l2\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                    trace_daddr,
                    (bool)PORT_VALUE(tribe.dmem_read_out),
                    (bool)PORT_VALUE(tribe.dmem_write_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_data_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_mask_out));
            }
#ifdef ENABLE_MMU_TLB
            const uint32_t trace_cpu_daddr = (uint32_t)PORT_VALUE(tribe.debug_dcache_cpu_addr_out);
            const bool trace_cpu_addr_hit =
                (trace_addr && trace_cpu_daddr == trace_addr) ||
                (trace_addr_from < trace_addr_to && trace_cpu_daddr >= trace_addr_from && trace_cpu_daddr < trace_addr_to);
            if (trace_cpu_addr_hit && ((bool)PORT_VALUE(tribe.debug_dcache_cpu_read_out) || (bool)PORT_VALUE(tribe.debug_dcache_cpu_write_out))) {
                FILE* out = trace_addr_file ? trace_addr_file : stdout;
                std::print(out, "trace-addr cycle={} pc={:08x} imem={:08x} addr={:08x} rd={} wr={} wdata={:08x} mask={:02x} source=dcache-cpu rvalid={} raddr={:08x} rdata={:08x}\n",
                    perf_clocks,
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                    trace_cpu_daddr,
                    (bool)PORT_VALUE(tribe.debug_dcache_cpu_read_out),
                    (bool)PORT_VALUE(tribe.debug_dcache_cpu_write_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dcache_cpu_wdata_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dcache_cpu_wmask_out),
                    (bool)PORT_VALUE(tribe.debug_dcache_read_valid_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dcache_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dcache_read_data_out));
            }
#endif
            if (trace_pc_from && (uint32_t)PORT_VALUE(tribe.debug_pc_out) >= trace_pc_from &&
                (uint32_t)PORT_VALUE(tribe.debug_pc_out) < trace_pc_to) {
                std::print("trace-pc-range cycle={} pc={:08x} imem={:08x} dinstr={:08x} dpc={:08x} dbr={} dimm={:08x} dmem={:08x} rd={} wr={} wdata={:08x} mask={:02x} wbwr={} wbact={} wbid={} wbdata={:08x} loadready={} memwait={} brtake={} brtarget={:08x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_decode_instr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_decode_pc_out),
                    (uint8_t)PORT_VALUE(tribe.debug_decode_br_out),
                    (uint32_t)PORT_VALUE(tribe.debug_decode_imm_out),
#else
                    0u, 0u, 0u, 0u,
#endif
                    (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                    (bool)PORT_VALUE(tribe.dmem_read_out),
                    (bool)PORT_VALUE(tribe.dmem_write_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_data_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_mask_out),
                    (bool)PORT_VALUE(tribe.debug_regs_write_out),
                    (bool)PORT_VALUE(tribe.debug_regs_write_actual_out),
                    (uint8_t)PORT_VALUE(tribe.debug_regs_wr_id_out),
                    (uint32_t)PORT_VALUE(tribe.debug_regs_data_out),
                    (bool)PORT_VALUE(tribe.debug_wb_load_ready_out),
                    (bool)PORT_VALUE(tribe.debug_memory_wait_out),
                    (bool)PORT_VALUE(tribe.debug_branch_taken_now_out),
                    (uint32_t)PORT_VALUE(tribe.debug_branch_target_now_out));
            }
            if (trace_io &&
                (uint32_t)PORT_VALUE(tribe.dmem_addr_out) >= start_mem_addr + TRIBE_RAM_BYTES &&
                (uint32_t)PORT_VALUE(tribe.dmem_addr_out) < start_mem_addr + MAX_RAM_SIZE &&
                ((bool)PORT_VALUE(tribe.dmem_read_out) || (bool)PORT_VALUE(tribe.dmem_write_out))) {
                std::print("trace-io cycle={} pc={:08x} imem={:08x} addr={:08x} rd={} wr={} wdata={:08x} mask={:02x}\n",
                    perf_clocks,
#ifdef ENABLE_MMU_TLB
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                    (uint32_t)0,
#endif
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                    (bool)PORT_VALUE(tribe.dmem_read_out),
                    (bool)PORT_VALUE(tribe.dmem_write_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_data_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_write_mask_out));
            }
            if (trace_sd) {
                uint32_t trace_sd_addr_now = (uint32_t)PORT_VALUE(tribe.dmem_addr_out);
                bool trace_sd_read_now = (bool)PORT_VALUE(tribe.dmem_read_out);
                bool trace_sd_write_now = (bool)PORT_VALUE(tribe.dmem_write_out);
                uint32_t trace_sd_wdata_now = (uint32_t)PORT_VALUE(tribe.dmem_write_data_out);
                uint32_t trace_sd_mask_now = (uint32_t)PORT_VALUE(tribe.dmem_write_mask_out);
                uint32_t trace_sd_status_now = (uint32_t)sdcard.debug_status_out();
                uint32_t trace_sd_state_now = (uint32_t)sdcard.debug_state_out();
                uint32_t trace_sd_count_now = (uint32_t)sdcard.debug_count_out();
                uint32_t trace_sd_len_now = (uint32_t)sdcard.debug_len_out();
                bool trace_sd_cmd_valid_now = (bool)sdcard.sd_cmd_valid_out();
                bool trace_sd_cmd_ready_now = (bool)sdcard.sd_cmd_ready_in();
                uint32_t trace_sd_cmd_data_now = (uint32_t)sdcard.sd_cmd_data_out();
                bool trace_sd_rsp_valid_now = (bool)sdcard.sd_rsp_valid_in();
                bool trace_sd_rsp_ready_now = (bool)sdcard.sd_rsp_ready_out();
                uint32_t trace_sd_rsp_data_now = (uint32_t)sdcard.sd_rsp_data_in();
                bool trace_sd_dma_awvalid_now = (bool)sdcard.dma_out.awvalid_in();
                bool trace_sd_dma_awready_now = (bool)sdcard.dma_out.awready_out();
                bool trace_sd_dma_wvalid_now = (bool)sdcard.dma_out.wvalid_in();
                bool trace_sd_dma_wready_now = (bool)sdcard.dma_out.wready_out();
                bool trace_sd_dma_bvalid_now = (bool)sdcard.dma_out.bvalid_out();
                bool trace_sd_dma_bready_now = (bool)sdcard.dma_out.bready_in();
                bool trace_sd_dma_arvalid_now = (bool)sdcard.dma_out.arvalid_in();
                bool trace_sd_dma_arready_now = (bool)sdcard.dma_out.arready_out();
                bool trace_sd_dma_rvalid_now = (bool)sdcard.dma_out.rvalid_out();
                bool trace_sd_dma_rready_now = (bool)sdcard.dma_out.rready_in();
                uint32_t trace_sd_count_bucket_now = trace_sd_count_now / 4096u;
                if (trace_sd_count_now == 0 || trace_sd_count_now == trace_sd_len_now) {
                    trace_sd_count_bucket_now = trace_sd_count_now;
                }
                bool trace_sd_active_now =
                    trace_sd_addr_now >= 0x8200d100u &&
                    trace_sd_addr_now < 0x8200d140u &&
                    (trace_sd_read_now || trace_sd_write_now);
                uint32_t trace_sd_reg_now = trace_sd_addr_now - 0x8200d100u;
                bool trace_sd_tuple_changed =
                    !trace_sd_prev_active ||
                    trace_sd_prev_addr != trace_sd_addr_now ||
                    trace_sd_prev_read != trace_sd_read_now ||
                    trace_sd_prev_write != trace_sd_write_now ||
                    trace_sd_prev_wdata != trace_sd_wdata_now ||
                    trace_sd_prev_mask != trace_sd_mask_now;
                bool trace_sd_state_changed =
                    trace_sd_prev_status != trace_sd_status_now ||
                    trace_sd_prev_state != trace_sd_state_now ||
                    trace_sd_prev_count_bucket != trace_sd_count_bucket_now ||
                    trace_sd_prev_len != trace_sd_len_now;
                bool trace_sd_periodic_poll =
                    trace_sd_active_now && trace_sd_read_now && trace_sd_reg_now == 0x04u &&
                    (perf_clocks - trace_sd_last_poll_report >= 1000000ull);
                bool trace_sd_log_now =
                    trace_sd_active_now &&
                    ((trace_sd_write_now && trace_sd_tuple_changed) ||
                     trace_sd_state_changed ||
                     (trace_sd_data && trace_sd_tuple_changed && trace_sd_read_now && trace_sd_reg_now == 0x1cu) ||
                     trace_sd_periodic_poll);
                bool trace_sd_cmd_log_now =
                    trace_sd_cmd_valid_now &&
                    (trace_sd_cmd_valid_now != trace_sd_prev_cmd_valid ||
                     trace_sd_cmd_ready_now != trace_sd_prev_cmd_ready ||
                     trace_sd_cmd_data_now != trace_sd_prev_cmd_data);
                bool trace_sd_rsp_log_now =
                    trace_sd_rsp_valid_now &&
                    (trace_sd_rsp_valid_now != trace_sd_prev_rsp_valid ||
                     trace_sd_rsp_ready_now != trace_sd_prev_rsp_ready ||
                     trace_sd_rsp_data_now != trace_sd_prev_rsp_data);
                bool trace_sd_dma_log_now =
                    trace_sd_state_changed ||
                    trace_sd_dma_awvalid_now != trace_sd_prev_dma_awvalid ||
                    trace_sd_dma_awready_now != trace_sd_prev_dma_awready ||
                    trace_sd_dma_wvalid_now != trace_sd_prev_dma_wvalid ||
                    trace_sd_dma_wready_now != trace_sd_prev_dma_wready ||
                    trace_sd_dma_bvalid_now != trace_sd_prev_dma_bvalid ||
                    trace_sd_dma_bready_now != trace_sd_prev_dma_bready ||
                    trace_sd_dma_arvalid_now != trace_sd_prev_dma_arvalid ||
                    trace_sd_dma_arready_now != trace_sd_prev_dma_arready ||
                    trace_sd_dma_rvalid_now != trace_sd_prev_dma_rvalid ||
                    trace_sd_dma_rready_now != trace_sd_prev_dma_rready;
                if (trace_sd_log_now) {
                    FILE* trace_sd_out = trace_sd_file ? trace_sd_file : stdout;
                    std::print(trace_sd_out, "trace-sd cycle={} pc={:08x} reg={:02x} rd={} wr={} wdata={:08x} mask={:02x} status={:02x} state={} count={} len={}\n",
                        perf_clocks,
#ifdef ENABLE_MMU_TLB
                        (uint32_t)PORT_VALUE(tribe.debug_pc_out),
#else
                        (uint32_t)0,
#endif
                        trace_sd_reg_now,
                        trace_sd_read_now,
                        trace_sd_write_now,
                        trace_sd_wdata_now,
                        trace_sd_mask_now,
                        trace_sd_status_now,
                        trace_sd_state_now,
                        trace_sd_count_now,
                        trace_sd_len_now);
                    if (trace_sd_file) {
                        fflush(trace_sd_file);
                    }
                    if (trace_sd_read_now && trace_sd_reg_now == 0x04u) {
                        trace_sd_last_poll_report = perf_clocks;
                    }
                }
                if (trace_sd_cmd_log_now || trace_sd_rsp_log_now) {
                    FILE* trace_sd_out = trace_sd_file ? trace_sd_file : stdout;
                    if (trace_sd_cmd_log_now) {
                        std::print(trace_sd_out, "trace-sd-cmd cycle={} valid={} ready={} data={:02x} last={} state={} count={} len={}\n",
                            perf_clocks,
                            trace_sd_cmd_valid_now,
                            trace_sd_cmd_ready_now,
                            trace_sd_cmd_data_now,
                            (bool)sdcard.sd_cmd_last_out(),
                            trace_sd_state_now,
                            trace_sd_count_now,
                            trace_sd_len_now);
                    }
                    if (trace_sd_rsp_log_now) {
                        std::print(trace_sd_out, "trace-sd-rsp cycle={} valid={} ready={} data={:02x} last={} state={} count={} len={}\n",
                            perf_clocks,
                            trace_sd_rsp_valid_now,
                            trace_sd_rsp_ready_now,
                            trace_sd_rsp_data_now,
                            (bool)sdcard.sd_rsp_last_in(),
                            trace_sd_state_now,
                            trace_sd_count_now,
                            trace_sd_len_now);
                    }
                    if (trace_sd_file) {
                        fflush(trace_sd_file);
                    }
                }
                if (trace_sd_dma_log_now && trace_sd_state_now != 0) {
                    FILE* trace_sd_out = trace_sd_file ? trace_sd_file : stdout;
                    std::print(trace_sd_out,
                        "trace-sd-dma cycle={} state={} count={} len={} status={:02x} aw={}/{} w={}/{} b={}/{} ar={}/{} r={}/{}\n",
                        perf_clocks,
                        trace_sd_state_now,
                        trace_sd_count_now,
                        trace_sd_len_now,
                        trace_sd_status_now,
                        trace_sd_dma_awvalid_now,
                        trace_sd_dma_awready_now,
                        trace_sd_dma_wvalid_now,
                        trace_sd_dma_wready_now,
                        trace_sd_dma_bvalid_now,
                        trace_sd_dma_bready_now,
                        trace_sd_dma_arvalid_now,
                        trace_sd_dma_arready_now,
                        trace_sd_dma_rvalid_now,
                        trace_sd_dma_rready_now);
                    if (trace_sd_file) {
                        fflush(trace_sd_file);
                    }
                }
                trace_sd_prev_active = trace_sd_active_now;
                trace_sd_prev_addr = trace_sd_addr_now;
                trace_sd_prev_read = trace_sd_read_now;
                trace_sd_prev_write = trace_sd_write_now;
                trace_sd_prev_wdata = trace_sd_wdata_now;
                trace_sd_prev_mask = trace_sd_mask_now;
                trace_sd_prev_status = trace_sd_status_now;
                trace_sd_prev_state = trace_sd_state_now;
                trace_sd_prev_count_bucket = trace_sd_count_bucket_now;
                trace_sd_prev_len = trace_sd_len_now;
                trace_sd_prev_cmd_valid = trace_sd_cmd_valid_now;
                trace_sd_prev_cmd_ready = trace_sd_cmd_ready_now;
                trace_sd_prev_cmd_data = trace_sd_cmd_data_now;
                trace_sd_prev_rsp_valid = trace_sd_rsp_valid_now;
                trace_sd_prev_rsp_ready = trace_sd_rsp_ready_now;
                trace_sd_prev_rsp_data = trace_sd_rsp_data_now;
                trace_sd_prev_dma_awvalid = trace_sd_dma_awvalid_now;
                trace_sd_prev_dma_awready = trace_sd_dma_awready_now;
                trace_sd_prev_dma_wvalid = trace_sd_dma_wvalid_now;
                trace_sd_prev_dma_wready = trace_sd_dma_wready_now;
                trace_sd_prev_dma_bvalid = trace_sd_dma_bvalid_now;
                trace_sd_prev_dma_bready = trace_sd_dma_bready_now;
                trace_sd_prev_dma_arvalid = trace_sd_dma_arvalid_now;
                trace_sd_prev_dma_arready = trace_sd_dma_arready_now;
                trace_sd_prev_dma_rvalid = trace_sd_dma_rvalid_now;
                trace_sd_prev_dma_rready = trace_sd_dma_rready_now;
            }
#ifdef ENABLE_MMU_TLB
            if (trace_mmu && (PORT_VALUE(tribe.debug_immu_ptw_read_out) || PORT_VALUE(tribe.debug_dmmu_ptw_read_out) ||
                              PORT_VALUE(tribe.debug_immu_busy_out) || PORT_VALUE(tribe.debug_immu_fault_out) ||
                              PORT_VALUE(tribe.debug_dmmu_busy_out) || PORT_VALUE(tribe.debug_dmmu_fault_out))) {
                std::print("mmu cycle={} pc={:08x} imem={:08x} dmem={:08x} d_rd={} d_wr={} i_ptw={} i_addr={:08x} i_busy={} i_fault={} i_last_addr={:08x} i_last_pte={:08x} d_ptw={} d_addr={:08x} word={:08x} d_busy={} d_fault={}\n",
                    perf_clocks,
                    (uint32_t)PORT_VALUE(tribe.debug_pc_out),
                    (uint32_t)PORT_VALUE(tribe.imem_read_addr_out),
                    (uint32_t)PORT_VALUE(tribe.dmem_addr_out),
                    (bool)PORT_VALUE(tribe.dmem_read_out),
                    (bool)PORT_VALUE(tribe.dmem_write_out),
                    (bool)PORT_VALUE(tribe.debug_immu_ptw_read_out),
                    (uint32_t)PORT_VALUE(tribe.debug_immu_ptw_addr_out),
                    (bool)PORT_VALUE(tribe.debug_immu_busy_out),
                    (bool)PORT_VALUE(tribe.debug_immu_fault_out),
                    (uint32_t)PORT_VALUE(tribe.debug_immu_last_addr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_immu_last_pte_out),
                    (bool)PORT_VALUE(tribe.debug_dmmu_ptw_read_out),
                    (uint32_t)PORT_VALUE(tribe.debug_dmmu_ptw_addr_out),
                    (uint32_t)PORT_VALUE(tribe.debug_mmu_ptw_word_out),
                    (bool)PORT_VALUE(tribe.debug_dmmu_busy_out),
                    (bool)PORT_VALUE(tribe.debug_dmmu_fault_out));
            }
#endif
            if (!tohost_addr && (perf_clocks & 0xffu) == 0 && output_file_reached_expected()) {
                break;
            }
            if (tohost_addr && PORT_VALUE(tribe.dmem_write_out) && PORT_VALUE(tribe.dmem_addr_out) == tohost_addr &&
                PORT_VALUE(tribe.dmem_write_mask_out) && PORT_VALUE(tribe.dmem_write_data_out)) {
                tohost_value = PORT_VALUE(tribe.dmem_write_data_out);
                tohost_done = true;
            }
            if (debugen_in) {
                debug_perf_counters_print();
            }
            runtime_trace_ticks += tribe_runtime_tick() - section_time_start;
            section_time_start = tribe_runtime_tick();
            _strobe_neg();
            _work_neg(0);
            runtime_negedge_ticks += tribe_runtime_tick() - section_time_start;
            runtime_total_ticks += tribe_runtime_tick() - cycle_time_start;
        }

        if (mirrored_uart_needs_newline) {
            std::print("\n");
        }

        if (host_interrupt) {
            error |= !save_sd_image();
            if (trace_uart_rx_file) {
                fclose(trace_uart_rx_file);
            }
            return !error;
        }

        if (checkpoint_save_only_success) {
            if (!checkpoint_saved) {
                std::print("*** checkpoint was not saved{} ***\n", cycle_limit_reached ? " before cycle limit" : "");
                error = true;
            }
        }
        else if (!checkpoint_save_after.empty()) {
            if (!checkpoint_save_after_completed) {
                std::print("*** checkpoint marker '{}' was not seen{} ***\n", checkpoint_save_after, cycle_limit_reached ? " before cycle limit" : "");
                error = true;
            }
        }
        else if (!expected_output_contains.empty()) {
            if (!expected_marker_seen) {
                std::print("*** UART output marker '{}' was not seen{} ***\n", expected_output_contains, cycle_limit_reached ? " before cycle limit" : "");
                error = true;
            }
        }
        else if (tohost_addr) {
            if (!tohost_done) {
                std::print("*** tohost was not written{} ***\n", cycle_limit_reached ? " before cycle limit" : "");
                error = true;
            }
            else if (tohost_value != 1) {
                std::print("*** tohost reported failure value {:08x} ***\n", tohost_value);
                error = true;
            }
        }
        else {
            std::ifstream a(expected_log, std::ios::binary), b("out.txt", std::ios::binary);
            error |= !std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(), std::istreambuf_iterator<char>(b));
        }

        error |= !save_sd_image();
        if (trace_uart_rx_file) {
            fclose(trace_uart_rx_file);
        }
        perf_print();
        if (!quiet_harness_output) {
            std::print(" {} ({} microseconds)\n", !error ? (checkpoint_save_only_success ? "CHECKPOINT SAVED" : "PASSED") : "FAILED",
                (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        }
        return !error;
    }

[[maybe_unused]] static std::filesystem::path absolute_from(const std::filesystem::path& base, const std::string& path)
{
    std::filesystem::path p(path);
    return p.is_absolute() ? p : std::filesystem::absolute(base / p);
}

[[maybe_unused]] static std::filesystem::path tribe_source_root_dir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path();
}

[[maybe_unused]] static std::string shell_quote_path(const std::filesystem::path& path)
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

[[maybe_unused]] static bool regenerate_tribe_sv(const std::filesystem::path& source_root)
{
    namespace fs = std::filesystem;

    fs::path cpphdl;
    if (const char* build_dir = std::getenv("CPPHDL_BUILD_DIR")) {
        cpphdl = fs::path(build_dir) / "cpphdl";
    }
    if (cpphdl.empty() || !fs::exists(cpphdl)) {
        cpphdl = fs::current_path() / ".." / "cpphdl";
    }
    if (!fs::exists(cpphdl)) {
        cpphdl = source_root / "build" / "cpphdl";
    }
    if (!fs::exists(cpphdl)) {
        std::print("can't find cpphdl generator in CPPHDL_BUILD_DIR, near build directory, or source root\n");
        return false;
    }

    std::string command;
    command += shell_quote_path(cpphdl);
    command += " " + shell_quote_path(source_root / "tribe" / "main.cpp");
    command += " -DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
    command += " -DTRIBE_RAM_BYTES_CONFIG=" + std::to_string(TRIBE_RAM_BYTES);
    command += " -DTRIBE_IO_REGION_SIZE_CONFIG=" + std::to_string(TRIBE_IO_REGION_SIZE);
    command += " -I " + shell_quote_path(source_root / "include");
    command += " -I " + shell_quote_path(source_root / "tribe" / "common");
    command += " -I " + shell_quote_path(source_root / "tribe" / "spec");
    command += " -I " + shell_quote_path(source_root / "tribe" / "devices");
    if (const char* toolchain_args = std::getenv("CPPHDL_TOOLCHAIN_ARGS")) {
        command += " ";
        command += toolchain_args;
    }
    return std::system(command.c_str()) == 0;
}

[[maybe_unused]] static void use_executable_workdir_if_needed(const char* argv0)
{
    namespace fs = std::filesystem;

    // CTest gives every executable a private work directory. Do not chdir back
    // to build/tribe/tests there, or parallel tests overwrite each other's ELF,
    // checkpoint, log, and Verilator generated artifacts.
    if (std::getenv("TRIBE_KEEP_WORKDIR")) {
        return;
    }

    if (fs::exists("generated/Tribe.sv") || fs::exists("rv32i.bin") || fs::exists("uart.elf") || fs::exists("rv32i.bin")) {
        return;
    }

    fs::path exe = fs::absolute(argv0);
    fs::path exe_dir = exe.parent_path();
    if (!exe_dir.empty() && (fs::exists(exe_dir / "generated" / "Tribe.sv") || fs::exists(exe_dir / "rv32i.bin") || fs::exists(exe_dir / "uart.elf") || fs::exists(exe_dir / "rv32i.bin"))) {
        fs::current_path(exe_dir);
    }
}

#if !defined(NO_MAINFILE)

int main (int argc, char** argv)
{
    const std::filesystem::path original_cwd = std::filesystem::current_path();
    bool debug = false;
    bool noveril = false;
    std::string program = "rv32i.bin";
    std::string expected_log = "rv32i.log";
    bool program_arg = false;
    bool log_arg = false;
    bool raw_program = false;
    size_t start_offset = 0x37c;
    uint64_t max_cycles = 2000000;
    uint32_t tohost = 0;
    uint32_t start_mem_addr = 0;
    uint32_t ram_size = DEFAULT_RAM_SIZE;
    uint32_t boot_hartid = 0;
    uint32_t boot_dtb_addr = 0;
    uint32_t boot_priv = 3;
    bool elf_phys_override = false;
    uint32_t elf_phys_offset = 0;
    std::string dtb_file;
    std::string initramfs_file;
    std::string sd_image_file;
    std::string eth_tap_socket_path;
    uint32_t initramfs_addr = 0;
    std::string checkpoint_load_file;
    std::string checkpoint_save_file;
    uint64_t checkpoint_save_cycle = 0;
    std::string checkpoint_save_after;
    bool append_output = false;
    bool mirror_uart_output = false;
    bool interactive_uart_input = false;
    std::string expected_output_contains;
    std::string bootargs;
    bool linux_earlycon_mapbase = false;
    int only = -1;
    for (int i=1; i < argc; ++i) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
        if (strcmp(argv[i], "--program") == 0 && i + 1 < argc) {
            program = argv[++i];
            program_arg = true;
            raw_program = false;
            continue;
        }
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            expected_log = argv[++i];
            log_arg = true;
            continue;
        }
        if (strcmp(argv[i], "--raw") == 0) {
            raw_program = true;
            continue;
        }
        if (strcmp(argv[i], "--elf") == 0) {
            raw_program = false;
            continue;
        }
        if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
            start_offset = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            max_cycles = std::stoull(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--tohost") == 0 && i + 1 < argc) {
            tohost = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--start-mem-addr") == 0 && i + 1 < argc) {
            start_mem_addr = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--ram-size") == 0 && i + 1 < argc) {
            ram_size = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--boot-hartid") == 0 && i + 1 < argc) {
            boot_hartid = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--boot-dtb-addr") == 0 && i + 1 < argc) {
            boot_dtb_addr = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--boot-priv") == 0 && i + 1 < argc) {
            std::string value = argv[++i];
            if (value == "m" || value == "M") {
                boot_priv = 3;
            }
            else if (value == "s" || value == "S") {
                boot_priv = 1;
            }
            else if (value == "u" || value == "U") {
                boot_priv = 0;
            }
            else {
                boot_priv = std::stoul(value, nullptr, 0);
            }
            continue;
        }
        if (strcmp(argv[i], "--dtb") == 0 && i + 1 < argc) {
            dtb_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--initramfs") == 0 && i + 1 < argc) {
            initramfs_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--initramfs-addr") == 0 && i + 1 < argc) {
            initramfs_addr = std::stoul(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--sd-image") == 0 && i + 1 < argc) {
            sd_image_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--eth-tap-socket") == 0 && i + 1 < argc) {
            eth_tap_socket_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--checkpoint-load") == 0 && i + 1 < argc) {
            checkpoint_load_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--checkpoint-save") == 0 && i + 1 < argc) {
            checkpoint_save_file = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--checkpoint-save-cycle") == 0 && i + 1 < argc) {
            checkpoint_save_cycle = std::stoull(argv[++i], nullptr, 0);
            continue;
        }
        if (strcmp(argv[i], "--checkpoint-save-after") == 0 && i + 1 < argc) {
            checkpoint_save_after = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--append-output") == 0) {
            append_output = true;
            continue;
        }
        if (strcmp(argv[i], "--mirror-uart") == 0) {
            mirror_uart_output = true;
            continue;
        }
        if (strcmp(argv[i], "--uart-stdin") == 0) {
            interactive_uart_input = true;
            mirror_uart_output = true;
            continue;
        }
        if (strcmp(argv[i], "--expected-output-contains") == 0 && i + 1 < argc) {
            expected_output_contains = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--uart-input") == 0) {
            interactive_uart_input = true;
            continue;
        }
        if (strcmp(argv[i], "--linux-earlycon-mapbase") == 0) {
            linux_earlycon_mapbase = true;
            continue;
        }
        if (strcmp(argv[i], "--bootargs") == 0 && i + 1 < argc) {
            bootargs = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--elf-phys-offset") == 0 && i + 1 < argc) {
            elf_phys_offset = std::stoul(argv[++i], nullptr, 0);
            elf_phys_override = true;
            continue;
        }
        if (strcmp(argv[i], "--elf-phys-base") == 0 && i + 1 < argc) {
            uint32_t phys_base = std::stoul(argv[++i], nullptr, 0);
            elf_phys_offset = phys_base - 0xc0000000u;
            elf_phys_override = true;
            continue;
        }
        if (argv[i][0] != '-') {
            only = atoi(argv[argc-1]);
        }
    }

    if (program_arg) {
        program = absolute_from(original_cwd, program).string();
    }
    if (log_arg) {
        expected_log = absolute_from(original_cwd, expected_log).string();
    }
    if (!dtb_file.empty()) {
        dtb_file = absolute_from(original_cwd, dtb_file).string();
    }
    if (!initramfs_file.empty()) {
        initramfs_file = absolute_from(original_cwd, initramfs_file).string();
    }
    if (!sd_image_file.empty()) {
        sd_image_file = absolute_from(original_cwd, sd_image_file).string();
    }
    if (!eth_tap_socket_path.empty() && eth_tap_socket_path[0] != '/') {
        eth_tap_socket_path = absolute_from(original_cwd, eth_tap_socket_path).string();
    }
    if (!checkpoint_load_file.empty()) {
        checkpoint_load_file = absolute_from(original_cwd, checkpoint_load_file).string();
    }
    if (!checkpoint_save_file.empty()) {
        checkpoint_save_file = absolute_from(original_cwd, checkpoint_save_file).string();
    }
    use_executable_workdir_if_needed(argv[0]);

    bool ok = true;
#ifndef VERILATOR  // this cpphdl test runs verilator tests recursively using same file
    if (!noveril) {
        std::cout << "Building verilator simulation... =============================================================\n";
        std::string verilator_l2_width_define = "-DL2_AXI_WIDTH=" + std::to_string(TRIBE_L2_AXI_WIDTH);
        setenv("CPPHDL_VERILATOR_CFLAGS", verilator_l2_width_define.c_str(), 1);
        const auto source_root = tribe_source_root_dir();
        auto start = std::chrono::high_resolution_clock::now();
        if (!regenerate_tribe_sv(source_root)) {
            ok = false;
        }
        else {
            ok &= VerilatorCompile(__FILE__, "Tribe", {"Predef_pkg",
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
                  "L2CacheFsmState_pkg",
                  "L2MemDriver_pkg",
                  "L2AxiResponderComb_pkg",
                  "L2AxiDriverComb_pkg",
                  "Axi4WriteAddressReady_pkg",
                  "Axi4WriteDataReady_pkg",
                  "Axi4WriteResponse4_pkg",
                  "Axi4ReadAddressReady_pkg",
                  "Axi4ReadData4_64_pkg",
                  "Axi4Responder4_64_pkg",
                  "Axi4WriteAddress23_4_pkg",
                  "Axi4WriteAddress32_4_pkg",
                  "Axi4WriteData64_pkg",
                  "Axi4WriteResponseReady_pkg",
                  "Axi4ReadAddress32_4_pkg",
                  "Axi4ReadDataReady_pkg",
                  "Axi4Driver32_4_64_pkg",
                  "Axi4ReadData4_256_pkg",
                  "Axi4Responder4_256_pkg",
                  "Axi4WriteData256_pkg",
                  "Axi4Driver32_4_256_pkg",
                  "File",
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
	                  "WritebackMem"}, {
                          (source_root / "include").string(),
                          (source_root / "tribe").string(),
                          (source_root / "tribe" / "common").string(),
                          (source_root / "tribe" / "spec").string(),
                          (source_root / "tribe" / "cache").string(),
                          (source_root / "tribe" / "devices").string()});
        }
        std::cout << "Executing tests... ===========================================================================\n";
        auto compile_us = ((std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start)).count());
        ok = ( ok
            && ((only != -1 && only != 0) || std::system((std::string("Tribe/obj_dir/VTribe") + (debug?" --debug":"") + " 0").c_str()) == 0)
        );
        std::cout << "Verilator compilation time: " << compile_us/2 << " microseconds\n";
    }
#else
    Verilated::commandArgs(argc, argv);
#endif

    return !( ok
        && ((only != -1 && only != 0) || TestTribe(debug).run(program, start_offset, expected_log, max_cycles, tohost, start_mem_addr, ram_size, raw_program, boot_hartid, boot_dtb_addr, boot_priv, elf_phys_override, elf_phys_offset, dtb_file, linux_earlycon_mapbase, initramfs_file, initramfs_addr, checkpoint_load_file, checkpoint_save_file, checkpoint_save_cycle, append_output, bootargs, false, expected_output_contains, "", mirror_uart_output, interactive_uart_input, checkpoint_save_after, sd_image_file, eth_tap_socket_path))
    );
}

#endif  // !NO_MAINFILE

/////////////////////////////////////////////////////////////////////////

#endif  // !SYNTHESIS

#ifdef MAIN_FILE_INCLUDED
#undef NO_MAINFILE
#endif
