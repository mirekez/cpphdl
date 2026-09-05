#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "cpphdl.h"

#ifndef CPPHDL_USE_GENERATED_PCH
#include "all_generated.h"
#include "generated/corev_apu/tb/ariane_axi_pkg.h"
#include "generated/corev_apu/src/ariane.h"
#endif

long _system_clock = 0;

namespace {

struct Elf32Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

constexpr uint64_t DRAM_BASE = 0x80000000ull;
constexpr uint64_t UART_BASE = 0x10000000ull;

std::vector<unsigned char> read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::fprintf(stderr, "failed to open %s\n", path);
        std::exit(2);
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

uint64_t parse_u64(const char* text)
{
    return std::strtoull(text, nullptr, 0);
}

struct Memory {
    std::map<uint64_t, uint8_t> bytes;

    uint8_t read8(uint64_t address) const
    {
        const auto found = bytes.find(address);
        return found == bytes.end() ? 0 : found->second;
    }

    uint32_t read32(uint64_t address) const
    {
        uint32_t value = 0;
        for (unsigned byte = 0; byte < 4; ++byte) {
            value |= uint32_t(read8(address + byte)) << (8 * byte);
        }
        return value;
    }

    uint64_t read64(uint64_t address) const
    {
        const uint64_t base = address & ~7ull;
        uint64_t value = 0;
        for (unsigned byte = 0; byte < 8; ++byte) {
            value |= uint64_t(read8(base + byte)) << (8 * byte);
        }
        return value;
    }

    void write64(uint64_t address, uint64_t value, uint64_t strobes)
    {
        const uint64_t base = address & ~7ull;
        for (unsigned byte = 0; byte < 8; ++byte) {
            if ((strobes >> byte) & 1u) {
                bytes[base + byte] = uint8_t(value >> (8 * byte));
            }
        }
    }
};

uint32_t load_elf32(Memory& memory, const char* path)
{
    const auto file = read_file(path);
    if (file.size() < sizeof(Elf32Ehdr)) {
        std::fprintf(stderr, "ELF too small: %s\n", path);
        std::exit(2);
    }
    Elf32Ehdr header{};
    std::memcpy(&header, file.data(), sizeof(header));
    if (header.e_ident[0] != 0x7f || header.e_ident[1] != 'E' ||
        header.e_ident[2] != 'L' || header.e_ident[3] != 'F' ||
        header.e_ident[4] != 1) {
        std::fprintf(stderr, "not an ELF32 file: %s\n", path);
        std::exit(2);
    }
    for (uint16_t index = 0; index < header.e_phnum; ++index) {
        const uint64_t offset = header.e_phoff + uint64_t(index) * header.e_phentsize;
        if (offset + sizeof(Elf32Phdr) > file.size()) {
            std::fprintf(stderr, "bad program header in %s\n", path);
            std::exit(2);
        }
        Elf32Phdr segment{};
        std::memcpy(&segment, file.data() + offset, sizeof(segment));
        if (segment.p_type != 1) {
            continue;
        }
        const uint64_t base = segment.p_paddr;
        const bool alias = base >= DRAM_BASE;
        for (uint32_t byte = 0; byte < segment.p_memsz; ++byte) {
            const uint8_t value = byte < segment.p_filesz
                ? file.at(uint64_t(segment.p_offset) + byte) : 0;
            memory.bytes[base + byte] = value;
            if (alias) {
                memory.bytes[base - DRAM_BASE + byte] = value;
            }
        }
    }
    return header.e_entry;
}

} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s program.riscv [tohost_addr] [max_cycles]\n", argv[0]);
        return 2;
    }

    Memory memory;
    const uint32_t entry = load_elf32(memory, argv[1]);
    const uint64_t tohost = argc > 2 ? parse_u64(argv[2]) : 0x80001000ull;
    const uint64_t fromhost = 0x80001040ull;
    const uint64_t max_cycles = argc > 3 ? parse_u64(argv[3]) : 3000000ull;

    static constexpr auto config = build_config_pkg::build_config(cva6_config_pkg::cva6_cfg);
    using RvfiInstr = cpphdl_rvfi::probes_instr_t<config>;
    using RvfiCsr = cpphdl_rvfi::probes_csr_t<config>;
    using RvfiProbes = ariane_rvfi_probes_t_default_t<config, RvfiInstr, RvfiCsr>;
    ariane<config, RvfiInstr, RvfiCsr, RvfiProbes> dut;

    logic<1> reset_n = 0;
    logic<config.VLEN> boot_address = entry;
    logic<config.XLEN> hart_id = 0;
    logic<2> irq = 0;
    logic<1> ipi = 0;
    logic<1> timer_irq = 0;
    logic<1> debug_request = 0;
    ariane_axi::resp_t response{};

    dut.rst_ni_in = [&]() { return &reset_n; };
    dut.boot_addr_i_in = [&]() { return &boot_address; };
    dut.hart_id_i_in = [&]() { return &hart_id; };
    dut.irq_i_in = [&]() { return &irq; };
    dut.ipi_i_in = [&]() { return &ipi; };
    dut.time_irq_i_in = [&]() { return &timer_irq; };
    dut.debug_req_i_in = [&]() { return &debug_request; };
    dut.noc_resp_i_in = [&]() { return &response; };
    // hdlcpp materializes references to packed-struct members as projected
    // ports.  A top-level aggregate input has no parent module from which the
    // generated projections can be wired, so bind each AXI projection here.
    dut.noc_resp_i_in__field_ar_ready = [&]() { return &response.ar_ready; };
    dut.noc_resp_i_in__field_aw_ready = [&]() { return &response.aw_ready; };
    dut.noc_resp_i_in__field_b = [&]() { return &response.b; };
    dut.noc_resp_i_in__field_b_id = [&]() { return &response.b.id; };
    dut.noc_resp_i_in__field_b_resp = [&]() { return &response.b.resp; };
    dut.noc_resp_i_in__field_b_valid = [&]() { return &response.b_valid; };
    dut.noc_resp_i_in__field_r = [&]() { return &response.r; };
    dut.noc_resp_i_in__field_r_data = [&]() { return &response.r.data; };
    dut.noc_resp_i_in__field_r_id = [&]() { return &response.r.id; };
    dut.noc_resp_i_in__field_r_last = [&]() { return &response.r.last; };
    dut.noc_resp_i_in__field_r_resp = [&]() { return &response.r.resp; };
    dut.noc_resp_i_in__field_r_valid = [&]() { return &response.r_valid; };
    dut.noc_resp_i_in__field_w_ready = [&]() { return &response.w_ready; };

    bool read_active = false;
    uint64_t read_address = 0;
    uint64_t read_beats = 0;
    uint64_t read_id = 0;
    bool address_pending = false;
    uint64_t write_address = 0;
    uint64_t write_beats = 0;
    uint64_t write_id = 0;
    bool data_pending = false;
    uint64_t write_data = 0;
    uint64_t write_strobes = 0;
    bool write_last = false;
    bool response_pending = false;
    uint64_t reads = 0;
    uint64_t writes = 0;
    std::string output_line;
    bool saw_passed = false;
    uint64_t pass_cycle = max_cycles;

    auto emit_character = [&](char character) {
        if (character == '\n' || character == '\r') {
            if (!output_line.empty()) {
                std::printf("UART: %s\n", output_line.c_str());
                saw_passed |= output_line == "PASSED";
                output_line.clear();
            }
        } else {
            output_line.push_back(character);
        }
    };

    dut._assign();
    uint64_t cycle = 0;
    try {
        for (; cycle < max_cycles; ++cycle) {
            dut._strobe();
            ++_system_clock;
            reset_n = cycle >= 8;

            response = {};
            response.aw_ready = !address_pending && !response_pending;
            response.w_ready = !data_pending;
            response.ar_ready = !read_active;
            const bool offered_read = read_active;
            if (read_active) {
                response.r_valid = 1;
                response.r.id = read_id;
                response.r.data = memory.read64(read_address);
                response.r.resp = 0;
                response.r.last = read_beats == 1;
            }
            if (response_pending) {
                response.b_valid = 1;
                response.b.id = write_id;
                response.b.resp = 0;
            }

            dut._work(!bool(reset_n));
            const auto& request = dut.noc_req_o_out();

            if (!read_active && bool(request.ar_valid)) {
                read_active = true;
                read_address = uint64_t(request.ar.addr);
                read_beats = uint64_t(request.ar.len) + 1;
                read_id = uint64_t(request.ar.id);
            }
            if (offered_read && bool(request.r_ready)) {
                ++reads;
                read_address += 8;
                if (--read_beats == 0) {
                    read_active = false;
                }
            }

            if (!address_pending && !response_pending && bool(request.aw_valid)) {
                address_pending = true;
                write_address = uint64_t(request.aw.addr);
                write_beats = uint64_t(request.aw.len) + 1;
                write_id = uint64_t(request.aw.id);
            }
            if (!data_pending && bool(request.w_valid)) {
                data_pending = true;
                write_data = uint64_t(request.w.data);
                write_strobes = uint64_t(request.w.strb);
                write_last = bool(request.w.last);
            }
            if (address_pending && data_pending) {
                memory.write64(write_address, write_data, write_strobes);
                ++writes;
                for (unsigned byte = 0; byte < 8; ++byte) {
                    if (((write_strobes >> byte) & 1u) && write_address + byte == UART_BASE) {
                        emit_character(char((write_data >> (8 * byte)) & 0xffu));
                    }
                }

                const bool wrote_tohost = write_address == tohost ||
                    write_address + DRAM_BASE == tohost;
                if (wrote_tohost && write_data != 0 && (write_data & 1u) == 0) {
                    const uint64_t payload = uint32_t(write_data);
                    uint64_t syscall = memory.read64(payload);
                    uint64_t fd = memory.read64(payload + 8);
                    uint64_t buffer = memory.read64(payload + 16);
                    uint64_t length = memory.read64(payload + 24);
                    if (syscall > 0xffffffffull) {
                        syscall = memory.read32(payload);
                        fd = memory.read32(payload + 4);
                        buffer = memory.read32(payload + 8);
                        length = memory.read32(payload + 12);
                    }
                    if (syscall == 64 && (fd == 1 || fd == 2)) {
                        for (uint64_t byte = 0; byte < length; ++byte) {
                            emit_character(char(memory.read8(buffer + byte)));
                        }
                    }
                    memory.write64(tohost, 0, 0xff);
                    memory.write64(tohost - DRAM_BASE, 0, 0xff);
                    memory.write64(fromhost, 1, 0xff);
                    memory.write64(fromhost - DRAM_BASE, 1, 0xff);
                    if (saw_passed && pass_cycle == max_cycles)
                        pass_cycle = cycle;
                } else if (wrote_tohost && (write_data & 1u)) {
                    if (uint32_t(write_data) == 1 && saw_passed) {
                        if (pass_cycle == max_cycles)
                            pass_cycle = cycle;
                        // Keep evaluating through the fixed benchmark budget.
                        // RV32 emits the 64-bit exit word as two stores.
                        memory.write64(tohost, 0, 0xff);
                        memory.write64(tohost - DRAM_BASE, 0, 0xff);
                    }
                    else {
                        std::fprintf(stderr, "cpphdl FAIL tohost=0x%llx data=0x%llx passed=%u\n",
                                     (unsigned long long)tohost,
                                     (unsigned long long)write_data,
                                     unsigned(saw_passed));
                        return 1;
                    }
                }

                data_pending = false;
                write_address += 8;
                if (--write_beats == 0 || write_last) {
                    address_pending = false;
                    response_pending = true;
                }
            }
            if (response_pending && bool(request.b_ready)) {
                response_pending = false;
            }
        }
    } catch (const cpphdl_exception& exception) {
        std::fprintf(stderr, "CPPHDL_EXCEPTION cycle=%llu: %s\n",
                     (unsigned long long)cycle, exception.text.c_str());
        return 3;
    }

    if (saw_passed) {
        std::printf("cpphdl BENCHMARK PASS cycles=%llu pass_cycle=%llu reads=%llu writes=%llu\n",
                    (unsigned long long)max_cycles,
                    (unsigned long long)pass_cycle,
                    (unsigned long long)reads,
                    (unsigned long long)writes);
        return 0;
    }
    std::fprintf(stderr,
                 "cpphdl TIMEOUT cycles=%llu entry=0x%x tohost=0x%llx reads=%llu writes=%llu\n",
                 (unsigned long long)max_cycles, entry,
                 (unsigned long long)tohost,
                 (unsigned long long)reads,
                 (unsigned long long)writes);
    return 1;
}
