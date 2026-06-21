#pragma once

#include "Tribe.h"
#include "devices/net/ethgig/ethgig_dma.h"
#include "devices/net/ethgig/ethgig_mac.h"
#include "devices/net/ethgig/ethgig_pcs.h"
#include "devices/net/ethgig/ethgig_phy.h"
#include "verif/RGMIIVerif.h"
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <vector>
#include <deque>
#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <filesystem>
#include <string>
#include <sstream>
#include "../examples/tools.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#if defined(__linux__)
#include <sys/socket.h>
#include <sys/un.h>
#endif
#if defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif

#include "Ram.h"
#include "Axi4Ram.h"

#include <tuple>
#include <utility>


static volatile sig_atomic_t tribe_uart_stdin_sigint_pending = 0;
static volatile sig_atomic_t tribe_uart_stdin_sigtstp_pending = 0;

static void tribe_uart_stdin_sigint_handler(int)
{
    tribe_uart_stdin_sigint_pending = 1;
}

static void tribe_uart_stdin_sigtstp_handler(int)
{
    tribe_uart_stdin_sigtstp_pending = 1;
}

static bool normalize_interactive_uart_byte(unsigned char in, bool& previous_cr, unsigned char& out)
{
    if (in == '\r') {
        previous_cr = true;
        out = '\n';
        return true;
    }
    if (in == '\n' && previous_cr) {
        previous_cr = false;
        return false;
    }
    previous_cr = false;
    out = in;
    return true;
}

static inline uint64_t tribe_runtime_tick()
{
#if defined(__i386__) || defined(__x86_64__)
    unsigned aux;
    return __rdtscp(&aux);
#else
    return (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
}

class EthGigTapSocket
{
#if defined(__linux__)
    int fd = -1;
    std::string local_path;
    bool send_warning_printed = false;
    bool recv_warning_printed = false;
    bool trace_enabled = false;
    std::ofstream trace_file;
    uint64_t rx_backlog_drop_count = 0;
    uint64_t rx_backlog_last_report_cycle = 0;
    uint64_t host_control_drop_count = 0;
    uint64_t host_control_last_report_cycle = 0;
    uint64_t last_host_arp_request_cycle = 0;
    uint64_t last_host_icmp_echo_cycle = 0;

    static constexpr uint8_t MSG_HELLO = 1;
    static constexpr uint8_t MSG_FRAME = 2;
    static constexpr size_t ETHERNET_MIN_FRAME_BYTES = 60;
    static constexpr size_t MAX_RX_BACKLOG_PACKETS = 2;
    static constexpr size_t MAX_RX_DGRAMS_PER_PUMP = 16;
    static constexpr uint64_t RX_BACKLOG_REPORT_PERIOD = 1000000;
    static constexpr uint64_t HOST_CONTROL_MIN_CYCLES = 195312;

    static bool sockaddr_from_path(const std::string& path, sockaddr_un& addr)
    {
        if (path.empty() || path.size() >= sizeof(addr.sun_path)) {
            return false;
        }
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        return true;
    }

    static uint32_t crc32_next(uint32_t crc, uint8_t data)
    {
        uint32_t value = crc ^ data;
        for (uint32_t i = 0; i < 8; ++i) {
            value = (value & 1u) ? ((value >> 1) ^ 0xedb88320u) : (value >> 1);
        }
        return value;
    }

    static uint32_t fcs_for_frame(const std::vector<uint8_t>& frame)
    {
        uint32_t crc = 0xffffffffu;
        for (uint8_t value : frame) {
            crc = crc32_next(crc, value);
        }
        return ~crc;
    }

    static std::vector<uint8_t> tap_frame_to_wire(std::vector<uint8_t> frame)
    {
        while (frame.size() < ETHERNET_MIN_FRAME_BYTES) {
            frame.push_back(0);
        }

        uint32_t fcs = fcs_for_frame(frame);
        std::vector<uint8_t> wire;
        wire.reserve(frame.size() + 12);
        for (uint32_t i = 0; i < 7; ++i) {
            wire.push_back(0x55);
        }
        wire.push_back(0xd5);
        wire.insert(wire.end(), frame.begin(), frame.end());
        for (uint32_t i = 0; i < 4; ++i) {
            wire.push_back((uint8_t)((fcs >> (i * 8u)) & 0xffu));
        }
        return wire;
    }

    static bool wire_frame_to_tap(const std::vector<uint8_t>& wire, std::vector<uint8_t>& frame)
    {
        if (wire.size() < 12) {
            return false;
        }
        for (uint32_t i = 0; i < 7; ++i) {
            if (wire[i] != 0x55u) {
                return false;
            }
        }
        if (wire[7] != 0xd5u) {
            return false;
        }

        frame.assign(wire.begin() + 8, wire.end() - 4);
        return frame.size() >= 14;
    }

    static std::string frame_prefix(const std::vector<uint8_t>& frame)
    {
        std::ostringstream out;
        size_t limit = std::min<size_t>(frame.size(), 32);
        for (size_t i = 0; i < limit; ++i) {
            if (i != 0) {
                out << ' ';
            }
            out << std::hex << std::setw(2) << std::setfill('0') << (uint32_t)frame[i];
        }
        if (frame.size() > limit) {
            out << " ...";
        }
        return out.str();
    }

    static bool is_host_arp_request(const std::vector<uint8_t>& frame)
    {
        return frame.size() >= 42 &&
            frame[12] == 0x08u && frame[13] == 0x06u &&
            frame[20] == 0x00u && frame[21] == 0x01u;
    }

    static bool is_host_icmp_echo_request(const std::vector<uint8_t>& frame)
    {
        if (frame.size() < 34 || frame[12] != 0x08u || frame[13] != 0x00u) {
            return false;
        }
        uint8_t ihl = (uint8_t)((frame[14] & 0x0fu) * 4u);
        size_t icmp = 14u + ihl;
        return ihl >= 20u && frame.size() > icmp &&
            frame[23] == 0x01u && frame[icmp] == 0x08u;
    }

    bool should_drop_host_control_frame(const std::vector<uint8_t>& frame)
    {
        uint64_t now = _system_clock;
        uint64_t* last_cycle = nullptr;
        if (is_host_arp_request(frame)) {
            last_cycle = &last_host_arp_request_cycle;
        }
        else if (is_host_icmp_echo_request(frame)) {
            last_cycle = &last_host_icmp_echo_cycle;
        }
        else {
            return false;
        }

        if (*last_cycle != 0 && now - *last_cycle < HOST_CONTROL_MIN_CYCLES) {
            ++host_control_drop_count;
            return true;
        }
        *last_cycle = now;
        return false;
    }

    void trace(const char* direction, const std::vector<uint8_t>& frame)
    {
        if (!trace_enabled) {
            return;
        }
        std::ostream& out = trace_file.is_open() ? trace_file : std::cerr;
        out << "ethtap cycle=" << _system_clock << ' ' << direction
            << " len=" << frame.size() << " data=" << frame_prefix(frame) << '\n';
        out.flush();
    }

    void trace_rx_backlog_drops(size_t pending_rx_packets)
    {
        if (!trace_enabled || rx_backlog_drop_count == 0) {
            return;
        }
        uint64_t now = _system_clock;
        if (now - rx_backlog_last_report_cycle < RX_BACKLOG_REPORT_PERIOD) {
            return;
        }
        rx_backlog_last_report_cycle = now;
        std::ostream& out = trace_file.is_open() ? trace_file : std::cerr;
        out << "ethtap cycle=" << now << " drop-rx-backlog count="
            << rx_backlog_drop_count << " pending=" << pending_rx_packets << '\n';
        out.flush();
        rx_backlog_drop_count = 0;
    }

    void trace_host_control_drops()
    {
        if (!trace_enabled || host_control_drop_count == 0) {
            return;
        }
        uint64_t now = _system_clock;
        if (now - host_control_last_report_cycle < RX_BACKLOG_REPORT_PERIOD) {
            return;
        }
        host_control_last_report_cycle = now;
        std::ostream& out = trace_file.is_open() ? trace_file : std::cerr;
        out << "ethtap cycle=" << now << " drop-host-control-rate count="
            << host_control_drop_count << '\n';
        out.flush();
        host_control_drop_count = 0;
    }

    bool send_frame(const std::vector<uint8_t>& frame)
    {
        if (fd < 0 || frame.size() > 2048) {
            return false;
        }
        std::array<uint8_t, 2051> msg{};
        msg[0] = MSG_FRAME;
        msg[1] = (uint8_t)(frame.size() >> 8);
        msg[2] = (uint8_t)frame.size();
        std::memcpy(msg.data() + 3, frame.data(), frame.size());
        ssize_t wrote = ::send(fd, msg.data(), frame.size() + 3, MSG_DONTWAIT);
        if (wrote < 0 && errno != EAGAIN && errno != EWOULDBLOCK && !send_warning_printed) {
            std::print("eth tap socket send failed: {}\n", std::strerror(errno));
            send_warning_printed = true;
        }
        return wrote == (ssize_t)(frame.size() + 3);
    }

#endif

public:
    ~EthGigTapSocket()
    {
        close();
    }

    bool open(const std::string& server_path)
    {
#if defined(__linux__)
        close();
        sockaddr_un local_addr{};
        sockaddr_un server_addr{};
        local_path = "/tmp/tribe-ethgig-sim-" + std::to_string((long long)::getpid()) + ".sock";
        if (!sockaddr_from_path(local_path, local_addr) || !sockaddr_from_path(server_path, server_addr)) {
            std::print("invalid eth tap socket path\n");
            return false;
        }

        trace_enabled = std::getenv("TRIBE_TRACE_ETH") != nullptr;
        if (const char* path = std::getenv("TRIBE_TRACE_ETH_FILE")) {
            trace_file.open(path, std::ios::out | std::ios::app);
            if (!trace_file.is_open()) {
                std::print("can't open TRIBE_TRACE_ETH_FILE '{}'\n", path);
            }
        }

        fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            std::print("can't create eth tap socket: {}\n", std::strerror(errno));
            return false;
        }
        ::unlink(local_path.c_str());
        if (::bind(fd, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) != 0) {
            std::print("can't bind eth tap socket '{}': {}\n", local_path, std::strerror(errno));
            close();
            return false;
        }
        if (::connect(fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
            std::print("can't connect eth tap socket '{}': {}\n", server_path, std::strerror(errno));
            close();
            return false;
        }
        uint8_t hello = MSG_HELLO;
        (void)::send(fd, &hello, sizeof(hello), MSG_DONTWAIT);
        std::print("Connected ethgig media to TAP socket '{}'\n", server_path);
        return true;
#else
        (void)server_path;
        std::print("eth tap socket is supported only on Linux hosts\n");
        return false;
#endif
    }

    void close()
    {
#if defined(__linux__)
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
        if (!local_path.empty()) {
            ::unlink(local_path.c_str());
            local_path.clear();
        }
        if (trace_file.is_open()) {
            trace_file.close();
        }
#endif
    }

    bool active() const
    {
#if defined(__linux__)
        return fd >= 0;
#else
        return false;
#endif
    }

    void pump(RGMIIVerifFrontend& rgmii)
    {
#if defined(__linux__)
        if (fd < 0) {
            return;
        }

        size_t rx_dgrams = 0;
        for (; rx_dgrams < MAX_RX_DGRAMS_PER_PUMP; ++rx_dgrams) {
            std::array<uint8_t, 4096> msg{};
            ssize_t got = ::recv(fd, msg.data(), msg.size(), MSG_DONTWAIT);
            if (got < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && !recv_warning_printed) {
                    std::print("eth tap socket recv failed: {}\n", std::strerror(errno));
                    recv_warning_printed = true;
                }
                break;
            }
            if (got == 0) {
                break;
            }
            if (got >= 3 && msg[0] == MSG_FRAME) {
                size_t len = (size_t(msg[1]) << 8) | msg[2];
                if (len <= (size_t)got - 3) {
                    std::vector<uint8_t> tap_frame(msg.begin() + 3, msg.begin() + 3 + len);
                    // Host ping/ARP runs against real wall clock, but the
                    // simulated Linux clock advances much more slowly. Drop
                    // repeated control frames in simulated time so old echo
                    // requests do not become delayed replies with growing RTT.
                    if (should_drop_host_control_frame(tap_frame)) {
                        continue;
                    }
                    // The host TAP runs in real time while Tribe simulation is
                    // much slower. Bound ingress buffering so stale host frames
                    // are drained and dropped instead of turning into seconds
                    // of latency. Keep the drop trace summarized because ARP
                    // storms can otherwise dominate simulator runtime.
                    if (rgmii.pending_rx_packets() >= MAX_RX_BACKLOG_PACKETS) {
                        ++rx_backlog_drop_count;
                        continue;
                    }
                    std::vector<uint8_t> wire_frame = tap_frame_to_wire(tap_frame);
                    if (rgmii.push_rx_packet_limited(wire_frame, MAX_RX_BACKLOG_PACKETS)) {
                        trace("tap->wire", tap_frame);
                    }
                    else {
                        ++rx_backlog_drop_count;
                    }
                }
            }
        }
        trace_rx_backlog_drops(rgmii.pending_rx_packets());
        trace_host_control_drops();

        while (rgmii.has_tx_packet()) {
            std::vector<uint8_t> wire_frame = rgmii.pop_tx_packet();
            std::vector<uint8_t> tap_frame;
            if (!wire_frame_to_tap(wire_frame, tap_frame)) {
                trace("drop-bad-wire", wire_frame);
                continue;
            }
            trace("wire->tap", tap_frame);
            if (!send_frame(tap_frame)) {
                break;
            }
        }
#else
        (void)rgmii;
#endif
    }
};

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
static TribePerf verilator_tribe_perf(uint64_t bits)
{
    uint64_t storage = bits;
    return *reinterpret_cast<TribePerf*>(&storage);
}

template<size_t WORDS>
static logic<WORDS * 32> verilator_wide_to_logic(const VlWide<WORDS>& bits)
{
    logic<WORDS * 32> out = 0;
    memcpy(out.bytes, bits.m_storage, sizeof(out.bytes));
    return out;
}

template<size_t WORDS>
static logic<WORDS * 32> verilator_wide_to_logic(const WData (&bits)[WORDS])
{
    logic<WORDS * 32> out = 0;
    memcpy(out.bytes, bits, sizeof(out.bytes));
    return out;
}

static logic<64> verilator_wide_to_logic(const QData& bits)
{
    return (uint64_t)bits;
}

template<size_t WIDTH, size_t WORDS>
static void verilator_logic_to_wide(VlWide<WORDS>& out, const logic<WIDTH>& bits)
{
    static_assert(WIDTH <= WORDS * 32);
    memset(out.m_storage, 0, sizeof(out.m_storage));
    memcpy(out.m_storage, bits.bytes, sizeof(bits.bytes));
}

template<size_t WIDTH, size_t WORDS>
static void verilator_logic_to_wide(WData (&out)[WORDS], const logic<WIDTH>& bits)
{
    static_assert(WIDTH <= WORDS * 32);
    memset(out, 0, sizeof(out));
    memcpy(out, bits.bytes, sizeof(bits.bytes));
}

static void verilator_logic_to_wide(QData& out, const logic<64>& bits)
{
    out = (uint64_t)bits;
}

#define PORT_VALUE(port) (port)
#define PERF_VALUE(port) verilator_tribe_perf((uint64_t)(port))
#else
#define PORT_VALUE(port) (port())
#define PERF_VALUE(port) (port())
#endif

class TestTribe : public Module
{
    static constexpr size_t AXI_RAM0_DEPTH = TRIBE_MEM_REGION0_SIZE / (TRIBE_L2_AXI_WIDTH/8);
    static constexpr size_t AXI_RAM1_DEPTH = TRIBE_MEM_REGION1_SIZE / (TRIBE_L2_AXI_WIDTH/8);
    static constexpr size_t AXI_RAM2_DEPTH = TRIBE_MEM_REGION2_SIZE / (TRIBE_L2_AXI_WIDTH/8);
    Axi4Ram<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH,AXI_RAM0_DEPTH> mem0;
    Axi4Ram<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH,AXI_RAM1_DEPTH> mem1;
    Axi4Ram<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH,AXI_RAM2_DEPTH> mem2;
    Axi4RegionMux<6,clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> iospace;
    NS16550A<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> uart;
    CLINT<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> clint;
    PLIC<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> plic;
    Accelerator<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> accelerator;
    SDController<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> sdcard;
    SDCardVerifFrontend sdcard_verif;
    EthGigDMA<clog2(MAX_RAM_SIZE),4,TRIBE_L2_AXI_WIDTH> ethgig_dma;
    EthGigMAC<256> ethgig_mac;
    EthGigPCS<256> ethgig_pcs;
    EthGigPHY ethgig_phy;
    RGMIIVerifFrontend ethgig_verif;

#ifdef VERILATOR
    VERILATOR_MODEL tribe;
#else
    Tribe tribe;
#endif

    bool error;

    uint64_t perf_clocks = 0;
    uint64_t perf_stall = 0;
    uint64_t perf_hazard = 0;
    uint64_t perf_dcache_wait = 0;
    uint64_t perf_icache_wait = 0;
    uint64_t perf_branch = 0;
    uint64_t perf_icache_issue_wait_cycles = 0;
    uint64_t perf_icache_lookup_wait_cycles = 0;
    uint64_t perf_icache_refill_wait_cycles = 0;
    uint64_t perf_icache_init_wait_cycles = 0;
    uint64_t perf_icache_hit_lookup_cycles = 0;
    uint64_t runtime_strobe_ticks = 0;
    uint64_t runtime_tribe_strobe_ticks = 0;
    uint64_t runtime_checkpoint_ticks = 0;
    uint64_t runtime_perf_ticks = 0;
    uint64_t runtime_work_ticks = 0;
    uint64_t runtime_tribe_work_ticks = 0;
    uint64_t runtime_uart_ticks = 0;
    uint64_t runtime_trace_ticks = 0;
    uint64_t runtime_negedge_ticks = 0;
    uint64_t runtime_total_ticks = 0;
    uint32_t tohost_addr = 0;
    uint32_t tohost_value = 0;
    uint32_t reset_pc = 0;
    uint32_t boot_hartid = 0;
    uint32_t boot_dtb_addr = 0;
    uint32_t boot_priv = 3;
    uint32_t start_mem_addr = 0;
    uint32_t ram_size = DEFAULT_RAM_SIZE;
    // Testbench-driven UART RX is part of the simulated machine state.
    // Keep it in CppHDL registers so checkpoints capture the pin level
    // instead of hiding it in ordinary C++ variables.
    reg<u1> uart_rx_valid_reg;
    reg<u8> uart_rx_data_reg;
    reg<u32> uart_script_pos_reg;
    reg<u32> uart_script_delay_reg;
    reg<u1> uart_script_enabled_reg;
    reg<u1> uart_script_reported_reg;
    reg<u1> sd_dma_cache_invalidate_reg;
    reg<u1> eth_dma_cache_invalidate_reg;
    bool eth_loopback_enabled = false;
    EthGigTapSocket eth_tap_socket;
    bool tohost_done = false;

    class StdinRawMode
    {
        bool active = false;
        bool flags_active = false;
        bool sigint_active = false;
        bool sigtstp_active = false;
        int old_flags = 0;
        termios old_term = {};
        struct sigaction old_sigint = {};
        struct sigaction old_sigtstp = {};

        void restore_terminal()
        {
            if (active) {
                tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
            }
            if (flags_active) {
                fcntl(STDIN_FILENO, F_SETFL, old_flags);
            }
        }

        void apply_raw_terminal()
        {
            if (flags_active) {
                fcntl(STDIN_FILENO, F_SETFL, old_flags | O_NONBLOCK);
            }
            if (active) {
                termios raw = old_term;
                cfmakeraw(&raw);
                tcsetattr(STDIN_FILENO, TCSANOW, &raw);
            }
        }

        void install_sigtstp_handler()
        {
            struct sigaction next_sigtstp = {};
            next_sigtstp.sa_handler = tribe_uart_stdin_sigtstp_handler;
            sigemptyset(&next_sigtstp.sa_mask);
            sigaction(SIGTSTP, &next_sigtstp, nullptr);
        }

    public:
        explicit StdinRawMode(bool enable)
        {
            if (!enable) {
                return;
            }
            tribe_uart_stdin_sigint_pending = 0;
            tribe_uart_stdin_sigtstp_pending = 0;
            struct sigaction next_sigint = {};
            next_sigint.sa_handler = tribe_uart_stdin_sigint_handler;
            sigemptyset(&next_sigint.sa_mask);
            if (sigaction(SIGINT, &next_sigint, &old_sigint) == 0) {
                sigint_active = true;
            }
            struct sigaction next_sigtstp = {};
            next_sigtstp.sa_handler = tribe_uart_stdin_sigtstp_handler;
            sigemptyset(&next_sigtstp.sa_mask);
            if (sigaction(SIGTSTP, &next_sigtstp, &old_sigtstp) == 0) {
                sigtstp_active = true;
            }
            old_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
            if (old_flags >= 0 && fcntl(STDIN_FILENO, F_SETFL, old_flags | O_NONBLOCK) == 0) {
                flags_active = true;
            }
            if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old_term) == 0) {
                termios raw = old_term;
                cfmakeraw(&raw);
                if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                    active = true;
                }
            }
        }

        void suspend_to_shell()
        {
            restore_terminal();

            struct sigaction dfl = {};
            dfl.sa_handler = SIG_DFL;
            sigemptyset(&dfl.sa_mask);
            sigaction(SIGTSTP, &dfl, nullptr);
            raise(SIGTSTP);

            if (sigtstp_active) {
                install_sigtstp_handler();
            }
            apply_raw_terminal();
            tribe_uart_stdin_sigtstp_pending = 0;
        }

        ~StdinRawMode()
        {
            restore_terminal();
            if (sigint_active) {
                sigaction(SIGINT, &old_sigint, nullptr);
            }
            if (sigtstp_active) {
                sigaction(SIGTSTP, &old_sigtstp, nullptr);
            }
        }
    };

    struct Elf32Header
    {
        unsigned char ident[16];
        uint16_t type;
        uint16_t machine;
        uint32_t version;
        uint32_t entry;
        uint32_t phoff;
        uint32_t shoff;
        uint32_t flags;
        uint16_t ehsize;
        uint16_t phentsize;
        uint16_t phnum;
        uint16_t shentsize;
        uint16_t shnum;
        uint16_t shstrndx;
    } __PACKED;

    struct Elf32ProgramHeader
    {
        uint32_t type;
        uint32_t offset;
        uint32_t vaddr;
        uint32_t paddr;
        uint32_t filesz;
        uint32_t memsz;
        uint32_t flags;
        uint32_t align;
    } __PACKED;

    bool load_elf(FILE* fbin, std::vector<uint32_t>& ram, size_t& read_bytes, uint32_t mem_base, uint32_t mem_size_bytes, uint32_t& entry, bool elf_phys_override, uint32_t elf_phys_offset)
    {
        static constexpr uint32_t PT_LOAD = 1;
        Elf32Header ehdr = {};
        fseek(fbin, 0, SEEK_SET);
        if (fread(&ehdr, 1, sizeof(ehdr), fbin) != sizeof(ehdr)) {
            return false;
        }
        if (ehdr.ident[0] != 0x7f || ehdr.ident[1] != 'E' || ehdr.ident[2] != 'L' || ehdr.ident[3] != 'F' ||
            ehdr.ident[4] != 1 || ehdr.ident[5] != 1 || ehdr.phentsize != sizeof(Elf32ProgramHeader)) {
            return false;
        }
        entry = elf_phys_override ? ehdr.entry + elf_phys_offset : ehdr.entry;

        for (uint16_t i = 0; i < ehdr.phnum; ++i) {
            Elf32ProgramHeader phdr = {};
            fseek(fbin, ehdr.phoff + i * sizeof(phdr), SEEK_SET);
            if (fread(&phdr, 1, sizeof(phdr), fbin) != sizeof(phdr)) {
                return false;
            }
            const uint32_t type = phdr.type;
            const uint32_t offset = phdr.offset;
            const uint32_t vaddr = phdr.vaddr;
            const uint32_t paddr = phdr.paddr;
            const uint32_t filesz = phdr.filesz;
            if (type != PT_LOAD || filesz == 0) {
                continue;
            }

            const uint32_t phys = elf_phys_override ? (vaddr + elf_phys_offset) : (paddr ? paddr : vaddr);
            if (phys < mem_base || phys - mem_base + filesz > mem_size_bytes) {
                std::print("ELF segment outside test RAM window: paddr={:08x}, mem_base={:08x}, size={}\n", phys, mem_base, filesz);
                return false;
            }
            const uint32_t base = phys - mem_base;

            fseek(fbin, offset, SEEK_SET);
            for (uint32_t byte = 0; byte < filesz; ++byte) {
                int c = fgetc(fbin);
                if (c == EOF) {
                    return false;
                }
                const uint32_t addr = base + byte;
                const uint32_t shift = (addr & 3u) * 8u;
                ram[addr / 4] = (ram[addr / 4] & ~(0xffu << shift)) | (uint32_t(uint8_t(c)) << shift);
            }
            read_bytes += filesz;
        }
        return read_bytes != 0;
    }

    bool load_blob(const std::string& filename, std::vector<uint32_t>& ram, uint32_t addr, uint32_t mem_base, uint32_t mem_size_bytes, size_t& read_bytes)
    {
        FILE* f = fopen(filename.c_str(), "rb");
        if (!f) {
            std::print("can't open blob '{}'\n", filename);
            return false;
        }
        if (addr < mem_base) {
            std::print("blob outside test RAM window: addr={:08x}, mem_base={:08x}\n", addr, mem_base);
            fclose(f);
            return false;
        }
        uint32_t offset = addr - mem_base;
        if (offset >= mem_size_bytes) {
            std::print("blob outside test RAM window: addr={:08x}, mem_base={:08x}, mem_size={}\n", addr, mem_base, mem_size_bytes);
            fclose(f);
            return false;
        }
        uint32_t byte = offset;
        int c;
        while ((c = fgetc(f)) != EOF) {
            if (byte >= mem_size_bytes) {
                std::print("blob '{}' does not fit RAM window\n", filename);
                fclose(f);
                return false;
            }
            const uint32_t shift = (byte & 3u) * 8u;
            ram[byte / 4] = (ram[byte / 4] & ~(0xffu << shift)) | (uint32_t(uint8_t(c)) << shift);
            ++byte;
            ++read_bytes;
        }
        fclose(f);
        return true;
    }

    uint8_t ram_byte(const std::vector<uint32_t>& ram, uint32_t byte_addr)
    {
        return (uint8_t)((ram[byte_addr / 4] >> ((byte_addr & 3u) * 8u)) & 0xffu);
    }

    void set_ram_byte(std::vector<uint32_t>& ram, uint32_t byte_addr, uint8_t value)
    {
        const uint32_t shift = (byte_addr & 3u) * 8u;
        ram[byte_addr / 4] = (ram[byte_addr / 4] & ~(0xffu << shift)) | ((uint32_t)value << shift);
    }

    bool patch_dtb_bootargs(std::vector<uint32_t>& ram, uint32_t dtb_addr, uint32_t dtb_bytes,
                            uint32_t mem_base, uint32_t mem_size_bytes, const std::string& bootargs)
    {
        if (dtb_addr < mem_base || dtb_addr - mem_base + dtb_bytes > mem_size_bytes) {
            std::print("DTB bootargs patch outside test RAM window\n");
            return false;
        }

        static constexpr std::string_view prefix = "console=";
        const uint32_t base = dtb_addr - mem_base;
        for (uint32_t off = 0; off + prefix.size() < dtb_bytes; ++off) {
            bool match = true;
            for (uint32_t i = 0; i < prefix.size(); ++i) {
                if (ram_byte(ram, base + off + i) != (uint8_t)prefix[i]) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                continue;
            }

            uint32_t old_len = 0;
            while (off + old_len < dtb_bytes && ram_byte(ram, base + off + old_len) != 0) {
                ++old_len;
            }
            if (bootargs.size() > old_len) {
                std::print("new --bootargs is longer than DTB bootargs slot ({} > {})\n", bootargs.size(), old_len);
                return false;
            }
            for (uint32_t i = 0; i < old_len; ++i) {
                set_ram_byte(ram, base + off + i, i < bootargs.size() ? (uint8_t)bootargs[i] : 0);
            }
            std::print("Patched DTB bootargs: {}\n", bootargs);
            return true;
        }

        std::print("can't find DTB bootargs string to patch\n");
        return false;
    }

//    size_t i;

public:
    struct PerfSnapshot
    {
        uint64_t clocks = 0;
        uint64_t stall = 0;
        uint64_t hazard = 0;
        uint64_t dcache_wait = 0;
        uint64_t icache_wait = 0;
        uint64_t branch = 0;
        uint64_t icache_issue_wait = 0;
        uint64_t icache_lookup_wait = 0;
        uint64_t icache_refill_wait = 0;
        uint64_t icache_init_wait = 0;
        uint64_t icache_hit_lookup = 0;
        uint64_t runtime_total_ticks = 0;
    };

    bool      debugen_in;

    TestTribe(bool debug)
    {
        debugen_in = debug;
        drive_uart_rx(false);
        init_uart_script_state(true);
        sdcard_verif.fill_prbs();
    }

    ~TestTribe()
    {
    }

    PerfSnapshot perf_snapshot() const
    {
        return {
            perf_clocks,
            perf_stall,
            perf_hazard,
            perf_dcache_wait,
            perf_icache_wait,
            perf_branch,
            perf_icache_issue_wait_cycles,
            perf_icache_lookup_wait_cycles,
            perf_icache_refill_wait_cycles,
            perf_icache_init_wait_cycles,
            perf_icache_hit_lookup_cycles,
            runtime_total_ticks
        };
    }

    void drive_uart_rx(bool valid, uint8_t data = 0)
    {
        uart_rx_valid_reg = (u1)valid;
        uart_rx_valid_reg._next = (u1)false;
        if (valid) {
            uart_rx_data_reg = (u8)data;
            uart_rx_data_reg._next = (u8)data;
        }
    }

    void init_uart_script_state(bool enabled, uint32_t delay = 0)
    {
        uart_script_pos_reg.set((u32)0);
        uart_script_delay_reg.set((u32)delay);
        uart_script_enabled_reg.set((u1)enabled);
        uart_script_reported_reg.set((u1)enabled);
    }

    void enable_uart_script(uint32_t delay = 0)
    {
        uart_script_delay_reg.set((u32)delay);
        uart_script_enabled_reg.set((u1)true);
    }

    void mark_uart_script_reported()
    {
        uart_script_reported_reg.set((u1)true);
    }

    void advance_uart_script()
    {
        uart_script_pos_reg.set((u32)((uint32_t)uart_script_pos_reg + 1u));
    }

    static constexpr bool eth_dma_needs_cache_invalidate(bool tx_irq, bool rx_irq)
    {
        (void)tx_irq;
        return rx_irq;
    }

    void set_uart_script_delay(uint32_t delay)
    {
        uart_script_delay_reg.set((u32)delay);
    }

    void _assign()
    {
        size_t i = 0;
	#ifndef VERILATOR
        tribe.debugen_in = debugen_in;
        tribe.reset_pc_in = _ASSIGN(reset_pc);
        tribe.boot_hartid_in = _ASSIGN(boot_hartid);
        tribe.boot_dtb_addr_in = _ASSIGN(boot_dtb_addr);
        tribe.boot_priv_in = _ASSIGN((u<2>)boot_priv);
        tribe.external_cache_invalidate_in =
#ifdef ENABLE_MMU_TLB
            _ASSIGN(((bool)sd_dma_cache_invalidate_reg || (bool)eth_dma_cache_invalidate_reg) &&
                !tribe.debug_memory_wait_out() && !tribe.dmem_read_out() && !tribe.dmem_write_out());
#else
            _ASSIGN((bool)sd_dma_cache_invalidate_reg || (bool)eth_dma_cache_invalidate_reg);
#endif
        tribe.memory_base_in = _ASSIGN(start_mem_addr);
        tribe.memory_size_in = _ASSIGN((uint32_t)MAX_RAM_SIZE);
        tribe.mem_region_size_in[0] = _ASSIGN((uint32_t)TRIBE_MEM_REGION0_SIZE);
        tribe.mem_region_size_in[1] = _ASSIGN((uint32_t)TRIBE_MEM_REGION1_SIZE);
        tribe.mem_region_size_in[2] = _ASSIGN((uint32_t)TRIBE_MEM_REGION2_SIZE);
        tribe.mem_region_size_in[3] = _ASSIGN((uint32_t)TRIBE_IO_REGION_SIZE);
        for (i = 0; i < L2_MEM_PORTS; ++i) {
            tribe.axi_in[i].awvalid_in = _ASSIGN(false);
            tribe.axi_in[i].awaddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)0);
            tribe.axi_in[i].awid_in = _ASSIGN((u<4>)0);
            tribe.axi_in[i].wvalid_in = _ASSIGN(false);
            tribe.axi_in[i].wdata_in = _ASSIGN((logic<TRIBE_L2_AXI_WIDTH>)0);
            tribe.axi_in[i].wstrb_in = _ASSIGN((logic<TRIBE_L2_AXI_WIDTH / 8>)0);
            tribe.axi_in[i].wlast_in = _ASSIGN(false);
            tribe.axi_in[i].bready_in = _ASSIGN(false);
            tribe.axi_in[i].arvalid_in = _ASSIGN(false);
            tribe.axi_in[i].araddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)0);
            tribe.axi_in[i].arid_in = _ASSIGN((u<4>)0);
            tribe.axi_in[i].rready_in = _ASSIGN(false);
        }
        tribe.axi_in[0].awvalid_in = _ASSIGN(accelerator.dma_out.awvalid_in());
        tribe.axi_in[0].awaddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)accelerator.dma_out.awaddr_in());
        tribe.axi_in[0].awid_in = _ASSIGN((u<4>)(uint32_t)accelerator.dma_out.awid_in());
        tribe.axi_in[0].wvalid_in = _ASSIGN(accelerator.dma_out.wvalid_in());
        tribe.axi_in[0].wdata_in = _ASSIGN(accelerator.dma_out.wdata_in());
        tribe.axi_in[0].wstrb_in = _ASSIGN(accelerator.dma_out.wstrb_in());
        tribe.axi_in[0].wlast_in = _ASSIGN(accelerator.dma_out.wlast_in());
        tribe.axi_in[0].bready_in = _ASSIGN(accelerator.dma_out.bready_in());
        tribe.axi_in[0].arvalid_in = _ASSIGN(accelerator.dma_out.arvalid_in());
        tribe.axi_in[0].araddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)accelerator.dma_out.araddr_in());
        tribe.axi_in[0].arid_in = _ASSIGN((u<4>)(uint32_t)accelerator.dma_out.arid_in());
        tribe.axi_in[0].rready_in = _ASSIGN(accelerator.dma_out.rready_in());
        tribe.axi_in[1].awvalid_in = _ASSIGN(sdcard.dma_out.awvalid_in());
        tribe.axi_in[1].awaddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)sdcard.dma_out.awaddr_in());
        tribe.axi_in[1].awid_in = _ASSIGN((u<4>)(uint32_t)sdcard.dma_out.awid_in());
        tribe.axi_in[1].wvalid_in = _ASSIGN(sdcard.dma_out.wvalid_in());
        tribe.axi_in[1].wdata_in = _ASSIGN(sdcard.dma_out.wdata_in());
        tribe.axi_in[1].wstrb_in = _ASSIGN(sdcard.dma_out.wstrb_in());
        tribe.axi_in[1].wlast_in = _ASSIGN(sdcard.dma_out.wlast_in());
        tribe.axi_in[1].bready_in = _ASSIGN(sdcard.dma_out.bready_in());
        tribe.axi_in[1].arvalid_in = _ASSIGN(sdcard.dma_out.arvalid_in());
        tribe.axi_in[1].araddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)sdcard.dma_out.araddr_in());
        tribe.axi_in[1].arid_in = _ASSIGN((u<4>)(uint32_t)sdcard.dma_out.arid_in());
        tribe.axi_in[1].rready_in = _ASSIGN(sdcard.dma_out.rready_in());
        tribe.axi_in[2].awvalid_in = _ASSIGN(ethgig_dma.dma_out.awvalid_in());
        tribe.axi_in[2].awaddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)ethgig_dma.dma_out.awaddr_in());
        tribe.axi_in[2].awid_in = _ASSIGN((u<4>)(uint32_t)ethgig_dma.dma_out.awid_in());
        tribe.axi_in[2].wvalid_in = _ASSIGN(ethgig_dma.dma_out.wvalid_in());
        tribe.axi_in[2].wdata_in = _ASSIGN(ethgig_dma.dma_out.wdata_in());
        tribe.axi_in[2].wstrb_in = _ASSIGN(ethgig_dma.dma_out.wstrb_in());
        tribe.axi_in[2].wlast_in = _ASSIGN(ethgig_dma.dma_out.wlast_in());
        tribe.axi_in[2].bready_in = _ASSIGN(ethgig_dma.dma_out.bready_in());
        tribe.axi_in[2].arvalid_in = _ASSIGN(ethgig_dma.dma_out.arvalid_in());
        tribe.axi_in[2].araddr_in = _ASSIGN((u<clog2(MAX_RAM_SIZE)>)(uint32_t)ethgig_dma.dma_out.araddr_in());
        tribe.axi_in[2].arid_in = _ASSIGN((u<4>)(uint32_t)ethgig_dma.dma_out.arid_in());
        tribe.axi_in[2].rready_in = _ASSIGN(ethgig_dma.dma_out.rready_in());
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
        tribe.clint_msip_in = clint.msip_out;
        tribe.clint_mtip_in = clint.mtip_out;
        tribe.time_lo_in = clint.debug_mtime_lo_out;
        tribe.time_hi_in = clint.debug_mtime_hi_out;
        tribe.external_irq_in = plic.external_irq_out;
#endif
        tribe.__inst_name = __inst_name + "/tribe";
        tribe._assign();

        AXI4_DRIVER_FROM(mem0.axi_in, tribe.axi_out[0]);
        AXI4_DRIVER_FROM(mem1.axi_in, tribe.axi_out[1]);
        AXI4_DRIVER_FROM(mem2.axi_in, tribe.axi_out[2]);
        AXI4_DRIVER_FROM(iospace.slave_in, tribe.axi_out[3]);
        mem0.debugen_in = debugen_in;
        mem1.debugen_in = debugen_in;
        mem2.debugen_in = debugen_in;
        mem0.__inst_name = __inst_name + "/mem0";
        mem1.__inst_name = __inst_name + "/mem1";
        mem2.__inst_name = __inst_name + "/mem2";
        iospace.region_base_in[0] = _ASSIGN((uint32_t)0);
        iospace.region_size_in[0] = _ASSIGN((uint32_t)0x100);
        iospace.region_base_in[1] = _ASSIGN((uint32_t)0x100);
        iospace.region_size_in[1] = _ASSIGN((uint32_t)0xC000);
        iospace.region_base_in[2] = _ASSIGN((uint32_t)0xC100);
        iospace.region_size_in[2] = _ASSIGN((uint32_t)0x1000);
        iospace.region_base_in[3] = _ASSIGN((uint32_t)0xD100);
        iospace.region_size_in[3] = _ASSIGN((uint32_t)0x100);
        iospace.region_base_in[4] = _ASSIGN((uint32_t)0xE000);
        iospace.region_size_in[4] = _ASSIGN((uint32_t)0x1000);
        iospace.region_base_in[5] = _ASSIGN((uint32_t)0x10000);
        iospace.region_size_in[5] = _ASSIGN((uint32_t)0x210000);
        iospace.__inst_name = __inst_name + "/iospace";
        iospace._assign();
        AXI4_DRIVER_FROM(uart.axi_in, iospace.masters_out[0]);
        AXI4_DRIVER_FROM(clint.axi_in, iospace.masters_out[1]);
        AXI4_DRIVER_FROM(accelerator.axi_in, iospace.masters_out[2]);
        AXI4_DRIVER_FROM(sdcard.axi_in, iospace.masters_out[3]);
        AXI4_DRIVER_FROM(ethgig_dma.axi_in, iospace.masters_out[4]);
        AXI4_DRIVER_FROM(plic.axi_in, iospace.masters_out[5]);
        AXI4_RESPONDER_FROM(accelerator.dma_out, tribe.axi_in[0]);
        AXI4_RESPONDER_FROM(sdcard.dma_out, tribe.axi_in[1]);
        AXI4_RESPONDER_FROM(ethgig_dma.dma_out, tribe.axi_in[2]);
        ethgig_mac.tx_valid_in = ethgig_dma.mac_tx_valid_out;
        ethgig_mac.tx_data_in = ethgig_dma.mac_tx_data_out;
        ethgig_mac.tx_last_in = ethgig_dma.mac_tx_last_out;
        ethgig_mac.local_mac_in = ethgig_dma.local_mac_out;
        ethgig_mac.local_ip_in = _ASSIGN((uint32_t)0);
        ethgig_mac.local_mask_in = _ASSIGN((uint32_t)0);
        ethgig_mac.promisc_in = ethgig_dma.promisc_out;
        ethgig_dma.mac_tx_ready_in = ethgig_mac.tx_ready_out;
        ethgig_dma.mac_rx_valid_in = ethgig_mac.rx_valid_out;
        ethgig_dma.mac_rx_data_in = ethgig_mac.rx_data_out;
        ethgig_dma.mac_rx_last_in = ethgig_mac.rx_last_out;
        ethgig_mac.rx_ready_in = ethgig_dma.mac_rx_ready_out;
        ethgig_pcs.tx_valid_in = ethgig_mac.pcs_tx_valid_out;
        ethgig_pcs.tx_data_in = ethgig_mac.pcs_tx_data_out;
        ethgig_pcs.tx_last_in = ethgig_mac.pcs_tx_last_out;
        ethgig_mac.pcs_tx_ready_in = ethgig_pcs.tx_ready_out;
        ethgig_phy.tx_valid_in = ethgig_pcs.tx_valid_out;
        ethgig_phy.tx_data_in = ethgig_pcs.tx_data_out;
        ethgig_phy.tx_last_in = ethgig_pcs.tx_last_out;
        ethgig_pcs.tx_ready_in = ethgig_phy.tx_ready_out;
        ethgig_pcs.rx_valid_in = ethgig_phy.rx_valid_out;
        ethgig_pcs.rx_data_in = ethgig_phy.rx_data_out;
        ethgig_pcs.rx_last_in = ethgig_phy.rx_last_out;
        ethgig_phy.rx_ready_in = ethgig_pcs.rx_ready_out;
        ethgig_mac.pcs_rx_valid_in = ethgig_pcs.rx_valid_out;
        ethgig_mac.pcs_rx_data_in = ethgig_pcs.rx_data_out;
        ethgig_mac.pcs_rx_last_in = ethgig_pcs.rx_last_out;
        ethgig_pcs.rx_ready_in = ethgig_mac.pcs_rx_ready_out;
        ethgig_verif.rgmii_tx_ctl_in = ethgig_phy.rgmii_tx_ctl_out;
        ethgig_verif.rgmii_txd_in = ethgig_phy.rgmii_txd_out;
        ethgig_verif.rgmii_tx_last_in = ethgig_phy.rgmii_tx_last_out;
        ethgig_phy.rgmii_rx_ctl_in = ethgig_verif.rgmii_rx_ctl_out;
        ethgig_phy.rgmii_rxd_in = ethgig_verif.rgmii_rxd_out;
        ethgig_phy.rgmii_rx_last_in = ethgig_verif.rgmii_rx_last_out;
        ethgig_phy.mdio_mdc_in = _ASSIGN(false);
        ethgig_phy.mdio_host_oe_in = _ASSIGN(false);
        ethgig_phy.mdio_host_data_in = _ASSIGN(true);
        sdcard.sd_cmd_ready_in = sdcard_verif.sd_cmd_ready_out;
        sdcard.sd_rsp_valid_in = sdcard_verif.sd_rsp_valid_out;
        sdcard.sd_rsp_data_in = sdcard_verif.sd_rsp_data_out;
        sdcard.sd_rsp_last_in = sdcard_verif.sd_rsp_last_out;
        sdcard_verif.sd_cmd_valid_in = sdcard.sd_cmd_valid_out;
        sdcard_verif.sd_cmd_data_in = sdcard.sd_cmd_data_out;
        sdcard_verif.sd_cmd_last_in = sdcard.sd_cmd_last_out;
        sdcard_verif.sd_rsp_ready_in = sdcard.sd_rsp_ready_out;
        uart.uart_rx_valid_in = _ASSIGN((bool)uart_rx_valid_reg);
        uart.uart_rx_data_in = _ASSIGN((uint8_t)uart_rx_data_reg);
        clint.set_mtimecmp_in = tribe.sbi_set_timer_out;
        clint.set_mtimecmp_lo_in = tribe.sbi_timer_lo_out;
        clint.set_mtimecmp_hi_in = tribe.sbi_timer_hi_out;
        for (i = 0; i < 32; ++i) {
            plic.source_irq_in[i] = _ASSIGN(false);
        }
        plic.source_irq_in[1] = uart.irq_out;
        plic.source_irq_in[2] = sdcard.irq_out;
        plic.source_irq_in[3] = _ASSIGN(ethgig_dma.tx_irq_out() || ethgig_dma.rx_irq_out());
        uart.__inst_name = __inst_name + "/uart";
        clint.__inst_name = __inst_name + "/clint";
        plic.__inst_name = __inst_name + "/plic";
        accelerator.__inst_name = __inst_name + "/accelerator";
        sdcard.__inst_name = __inst_name + "/sdcard";
        sdcard_verif.__inst_name = __inst_name + "/sdcard_verif";
        ethgig_dma.__inst_name = __inst_name + "/ethgig_dma";
        ethgig_mac.__inst_name = __inst_name + "/ethgig_mac";
        ethgig_pcs.__inst_name = __inst_name + "/ethgig_pcs";
        ethgig_phy.__inst_name = __inst_name + "/ethgig_phy";
        ethgig_verif.__inst_name = __inst_name + "/ethgig_verif";
        mem0._assign();
        mem1._assign();
        mem2._assign();
        uart._assign();
        clint._assign();
        plic._assign();
        accelerator._assign();
        sdcard._assign();
        sdcard_verif._assign();
        ethgig_dma._assign();
        ethgig_mac._assign();
        ethgig_pcs._assign();
        ethgig_phy._assign();
        ethgig_verif._assign();

        AXI4_RESPONDER_FROM(tribe.axi_out[0], mem0.axi_in);
        AXI4_RESPONDER_FROM(tribe.axi_out[1], mem1.axi_in);
        AXI4_RESPONDER_FROM(tribe.axi_out[2], mem2.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[0], uart.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[1], clint.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[2], accelerator.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[3], sdcard.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[4], ethgig_dma.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[5], plic.axi_in);
        AXI4_RESPONDER_FROM(tribe.axi_out[3], iospace.slave_in);
	#else  // connecting Verilator to CppHDL
        tribe.reset_pc_in = reset_pc;
        tribe.boot_hartid_in = boot_hartid;
        tribe.boot_dtb_addr_in = boot_dtb_addr;
        tribe.boot_priv_in = boot_priv;
        tribe.external_cache_invalidate_in =
#ifdef ENABLE_MMU_TLB
            ((bool)sd_dma_cache_invalidate_reg || (bool)eth_dma_cache_invalidate_reg) &&
                !((bool)tribe.debug_memory_wait_out) && !((bool)tribe.dmem_read_out) && !((bool)tribe.dmem_write_out);
#else
            (bool)sd_dma_cache_invalidate_reg || (bool)eth_dma_cache_invalidate_reg;
#endif
        tribe.memory_base_in = start_mem_addr;
        tribe.memory_size_in = MAX_RAM_SIZE;
        tribe.mem_region_size_in[0] = TRIBE_MEM_REGION0_SIZE;
        tribe.mem_region_size_in[1] = TRIBE_MEM_REGION1_SIZE;
        tribe.mem_region_size_in[2] = TRIBE_MEM_REGION2_SIZE;
        tribe.mem_region_size_in[3] = TRIBE_IO_REGION_SIZE;
        for (size_t i = 0; i < L2_MEM_PORTS; ++i) {
            tribe.axi_in___05Fawvalid_in[i] = false;
            tribe.axi_in___05Fawaddr_in[i] = 0;
            tribe.axi_in___05Fawid_in[i] = 0;
            tribe.axi_in___05Fwvalid_in[i] = false;
            verilator_logic_to_wide(tribe.axi_in___05Fwdata_in[i], (logic<TRIBE_L2_AXI_WIDTH>)0);
            tribe.axi_in___05Fwstrb_in[i] = 0;
            tribe.axi_in___05Fwlast_in[i] = false;
            tribe.axi_in___05Fbready_in[i] = false;
            tribe.axi_in___05Farvalid_in[i] = false;
            tribe.axi_in___05Faraddr_in[i] = 0;
            tribe.axi_in___05Farid_in[i] = 0;
            tribe.axi_in___05Frready_in[i] = false;
        }
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
        tribe.clint_msip_in = clint.msip_out();
        tribe.clint_mtip_in = clint.mtip_out();
        tribe.time_lo_in = clint.debug_mtime_lo_out();
        tribe.time_hi_in = clint.debug_mtime_hi_out();
        tribe.external_irq_in = plic.external_irq_out();
#endif
        AXI4_DRIVER_FROM_VERILATOR_CONST(mem0.axi_in, tribe, 0, u<clog2(MAX_RAM_SIZE)>, verilator_wide_to_logic);
        AXI4_DRIVER_FROM_VERILATOR_CONST(mem1.axi_in, tribe, 1, u<clog2(MAX_RAM_SIZE)>, verilator_wide_to_logic);
        AXI4_DRIVER_FROM_VERILATOR_CONST(mem2.axi_in, tribe, 2, u<clog2(MAX_RAM_SIZE)>, verilator_wide_to_logic);
        AXI4_DRIVER_FROM_VERILATOR_CONST(iospace.slave_in, tribe, 3, u<clog2(MAX_RAM_SIZE)>, verilator_wide_to_logic);
        mem0.debugen_in = debugen_in;
        mem1.debugen_in = debugen_in;
        mem2.debugen_in = debugen_in;
        mem0.__inst_name = __inst_name + "/mem0";
        mem1.__inst_name = __inst_name + "/mem1";
        mem2.__inst_name = __inst_name + "/mem2";
        iospace.region_base_in[0] = _ASSIGN((uint32_t)0);
        iospace.region_size_in[0] = _ASSIGN((uint32_t)0x100);
        iospace.region_base_in[1] = _ASSIGN((uint32_t)0x100);
        iospace.region_size_in[1] = _ASSIGN((uint32_t)0xC000);
        iospace.region_base_in[2] = _ASSIGN((uint32_t)0xC100);
        iospace.region_size_in[2] = _ASSIGN((uint32_t)0x1000);
        iospace.region_base_in[3] = _ASSIGN((uint32_t)0xD100);
        iospace.region_size_in[3] = _ASSIGN((uint32_t)0x100);
        iospace.region_base_in[4] = _ASSIGN((uint32_t)0xE000);
        iospace.region_size_in[4] = _ASSIGN((uint32_t)0x1000);
        iospace.region_base_in[5] = _ASSIGN((uint32_t)0x10000);
        iospace.region_size_in[5] = _ASSIGN((uint32_t)0x210000);
        iospace.__inst_name = __inst_name + "/iospace";
        iospace._assign();
        AXI4_DRIVER_FROM(uart.axi_in, iospace.masters_out[0]);
        AXI4_DRIVER_FROM(clint.axi_in, iospace.masters_out[1]);
        AXI4_DRIVER_FROM(accelerator.axi_in, iospace.masters_out[2]);
        AXI4_DRIVER_FROM(sdcard.axi_in, iospace.masters_out[3]);
        AXI4_DRIVER_FROM(ethgig_dma.axi_in, iospace.masters_out[4]);
        AXI4_DRIVER_FROM(plic.axi_in, iospace.masters_out[5]);
        uart.uart_rx_valid_in = _ASSIGN((bool)uart_rx_valid_reg);
        uart.uart_rx_data_in = _ASSIGN((uint8_t)uart_rx_data_reg);
        clint.set_mtimecmp_in = _ASSIGN((bool)tribe.sbi_set_timer_out);
        clint.set_mtimecmp_lo_in = _ASSIGN((uint32_t)tribe.sbi_timer_lo_out);
        clint.set_mtimecmp_hi_in = _ASSIGN((uint32_t)tribe.sbi_timer_hi_out);
        for (i = 0; i < 32; ++i) {
            plic.source_irq_in[i] = _ASSIGN(false);
        }
        plic.source_irq_in[1] = uart.irq_out;
        plic.source_irq_in[2] = sdcard.irq_out;
        plic.source_irq_in[3] = _ASSIGN(ethgig_dma.tx_irq_out() || ethgig_dma.rx_irq_out());
        sdcard.sd_cmd_ready_in = sdcard_verif.sd_cmd_ready_out;
        sdcard.sd_rsp_valid_in = sdcard_verif.sd_rsp_valid_out;
        sdcard.sd_rsp_data_in = sdcard_verif.sd_rsp_data_out;
        sdcard.sd_rsp_last_in = sdcard_verif.sd_rsp_last_out;
        sdcard_verif.sd_cmd_valid_in = sdcard.sd_cmd_valid_out;
        sdcard_verif.sd_cmd_data_in = sdcard.sd_cmd_data_out;
        sdcard_verif.sd_cmd_last_in = sdcard.sd_cmd_last_out;
        sdcard_verif.sd_rsp_ready_in = sdcard.sd_rsp_ready_out;
        accelerator.dma_out.awready_out = _ASSIGN((bool)tribe.axi_in___05Fawready_out[0]);
        accelerator.dma_out.wready_out = _ASSIGN((bool)tribe.axi_in___05Fwready_out[0]);
        accelerator.dma_out.bvalid_out = _ASSIGN((bool)tribe.axi_in___05Fbvalid_out[0]);
        accelerator.dma_out.bid_out = _ASSIGN((u<4>)(uint32_t)tribe.axi_in___05Fbid_out[0]);
        accelerator.dma_out.arready_out = _ASSIGN((bool)tribe.axi_in___05Farready_out[0]);
        accelerator.dma_out.rvalid_out = _ASSIGN((bool)tribe.axi_in___05Frvalid_out[0]);
        accelerator.dma_out.rdata_out = _ASSIGN(verilator_wide_to_logic(tribe.axi_in___05Frdata_out[0]));
        accelerator.dma_out.rlast_out = _ASSIGN((bool)tribe.axi_in___05Frlast_out[0]);
        accelerator.dma_out.rid_out = _ASSIGN((u<4>)(uint32_t)tribe.axi_in___05Frid_out[0]);
        sdcard.dma_out.awready_out = _ASSIGN((bool)tribe.axi_in___05Fawready_out[1]);
        sdcard.dma_out.wready_out = _ASSIGN((bool)tribe.axi_in___05Fwready_out[1]);
        sdcard.dma_out.bvalid_out = _ASSIGN((bool)tribe.axi_in___05Fbvalid_out[1]);
        sdcard.dma_out.bid_out = _ASSIGN((u<4>)(uint32_t)tribe.axi_in___05Fbid_out[1]);
        sdcard.dma_out.arready_out = _ASSIGN((bool)tribe.axi_in___05Farready_out[1]);
        sdcard.dma_out.rvalid_out = _ASSIGN((bool)tribe.axi_in___05Frvalid_out[1]);
        sdcard.dma_out.rdata_out = _ASSIGN(verilator_wide_to_logic(tribe.axi_in___05Frdata_out[1]));
        sdcard.dma_out.rlast_out = _ASSIGN((bool)tribe.axi_in___05Frlast_out[1]);
        sdcard.dma_out.rid_out = _ASSIGN((u<4>)(uint32_t)tribe.axi_in___05Frid_out[1]);
        ethgig_dma.dma_out.awready_out = _ASSIGN((bool)tribe.axi_in___05Fawready_out[2]);
        ethgig_dma.dma_out.wready_out = _ASSIGN((bool)tribe.axi_in___05Fwready_out[2]);
        ethgig_dma.dma_out.bvalid_out = _ASSIGN((bool)tribe.axi_in___05Fbvalid_out[2]);
        ethgig_dma.dma_out.bid_out = _ASSIGN((u<4>)(uint32_t)tribe.axi_in___05Fbid_out[2]);
        ethgig_dma.dma_out.arready_out = _ASSIGN((bool)tribe.axi_in___05Farready_out[2]);
        ethgig_dma.dma_out.rvalid_out = _ASSIGN((bool)tribe.axi_in___05Frvalid_out[2]);
        ethgig_dma.dma_out.rdata_out = _ASSIGN(verilator_wide_to_logic(tribe.axi_in___05Frdata_out[2]));
        ethgig_dma.dma_out.rlast_out = _ASSIGN((bool)tribe.axi_in___05Frlast_out[2]);
        ethgig_dma.dma_out.rid_out = _ASSIGN((u<4>)(uint32_t)tribe.axi_in___05Frid_out[2]);
        ethgig_mac.tx_valid_in = ethgig_dma.mac_tx_valid_out;
        ethgig_mac.tx_data_in = ethgig_dma.mac_tx_data_out;
        ethgig_mac.tx_last_in = ethgig_dma.mac_tx_last_out;
        ethgig_mac.local_mac_in = ethgig_dma.local_mac_out;
        ethgig_mac.local_ip_in = _ASSIGN((uint32_t)0);
        ethgig_mac.local_mask_in = _ASSIGN((uint32_t)0);
        ethgig_mac.promisc_in = ethgig_dma.promisc_out;
        ethgig_dma.mac_tx_ready_in = ethgig_mac.tx_ready_out;
        ethgig_dma.mac_rx_valid_in = ethgig_mac.rx_valid_out;
        ethgig_dma.mac_rx_data_in = ethgig_mac.rx_data_out;
        ethgig_dma.mac_rx_last_in = ethgig_mac.rx_last_out;
        ethgig_mac.rx_ready_in = ethgig_dma.mac_rx_ready_out;
        ethgig_pcs.tx_valid_in = ethgig_mac.pcs_tx_valid_out;
        ethgig_pcs.tx_data_in = ethgig_mac.pcs_tx_data_out;
        ethgig_pcs.tx_last_in = ethgig_mac.pcs_tx_last_out;
        ethgig_mac.pcs_tx_ready_in = ethgig_pcs.tx_ready_out;
        ethgig_phy.tx_valid_in = ethgig_pcs.tx_valid_out;
        ethgig_phy.tx_data_in = ethgig_pcs.tx_data_out;
        ethgig_phy.tx_last_in = ethgig_pcs.tx_last_out;
        ethgig_pcs.tx_ready_in = ethgig_phy.tx_ready_out;
        ethgig_pcs.rx_valid_in = ethgig_phy.rx_valid_out;
        ethgig_pcs.rx_data_in = ethgig_phy.rx_data_out;
        ethgig_pcs.rx_last_in = ethgig_phy.rx_last_out;
        ethgig_phy.rx_ready_in = ethgig_pcs.rx_ready_out;
        ethgig_mac.pcs_rx_valid_in = ethgig_pcs.rx_valid_out;
        ethgig_mac.pcs_rx_data_in = ethgig_pcs.rx_data_out;
        ethgig_mac.pcs_rx_last_in = ethgig_pcs.rx_last_out;
        ethgig_pcs.rx_ready_in = ethgig_mac.pcs_rx_ready_out;
        ethgig_verif.rgmii_tx_ctl_in = ethgig_phy.rgmii_tx_ctl_out;
        ethgig_verif.rgmii_txd_in = ethgig_phy.rgmii_txd_out;
        ethgig_verif.rgmii_tx_last_in = ethgig_phy.rgmii_tx_last_out;
        ethgig_phy.rgmii_rx_ctl_in = ethgig_verif.rgmii_rx_ctl_out;
        ethgig_phy.rgmii_rxd_in = ethgig_verif.rgmii_rxd_out;
        ethgig_phy.rgmii_rx_last_in = ethgig_verif.rgmii_rx_last_out;
        ethgig_phy.mdio_mdc_in = _ASSIGN(false);
        ethgig_phy.mdio_host_oe_in = _ASSIGN(false);
        ethgig_phy.mdio_host_data_in = _ASSIGN(true);
        uart.__inst_name = __inst_name + "/uart";
        clint.__inst_name = __inst_name + "/clint";
        plic.__inst_name = __inst_name + "/plic";
        accelerator.__inst_name = __inst_name + "/accelerator";
        sdcard.__inst_name = __inst_name + "/sdcard";
        sdcard_verif.__inst_name = __inst_name + "/sdcard_verif";
        ethgig_dma.__inst_name = __inst_name + "/ethgig_dma";
        ethgig_mac.__inst_name = __inst_name + "/ethgig_mac";
        ethgig_pcs.__inst_name = __inst_name + "/ethgig_pcs";
        ethgig_phy.__inst_name = __inst_name + "/ethgig_phy";
        ethgig_verif.__inst_name = __inst_name + "/ethgig_verif";
        mem0._assign();
        mem1._assign();
        mem2._assign();
        uart._assign();
        clint._assign();
        plic._assign();
        accelerator._assign();
        sdcard._assign();
        sdcard_verif._assign();
        ethgig_dma._assign();
        ethgig_mac._assign();
        ethgig_pcs._assign();
        ethgig_phy._assign();
        ethgig_verif._assign();
        AXI4_RESPONDER_FROM(iospace.masters_out[0], uart.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[1], clint.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[2], accelerator.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[3], sdcard.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[4], ethgig_dma.axi_in);
        AXI4_RESPONDER_FROM(iospace.masters_out[5], plic.axi_in);
#endif
    }

    void _work(bool reset)
    {
        bool sd_dma_cache_invalidate_ready;
        bool eth_dma_cache_invalidate_ready;
        sd_dma_cache_invalidate_ready = true;
        eth_dma_cache_invalidate_ready = true;
#ifndef VERILATOR
        uint64_t tribe_work_time_start = tribe_runtime_tick();
        tribe._work(reset);
        runtime_tribe_work_ticks += tribe_runtime_tick() - tribe_work_time_start;
#else
//        memcpy(&tribe.data_in.m_storage, data_out, sizeof(tribe.data_in.m_storage));
        tribe.debugen_in    = debugen_in;
        tribe.reset_pc_in = reset_pc;
        tribe.boot_hartid_in = boot_hartid;
        tribe.boot_dtb_addr_in = boot_dtb_addr;
        tribe.boot_priv_in = boot_priv;
        tribe.memory_base_in = start_mem_addr;
        tribe.memory_size_in = MAX_RAM_SIZE;
        tribe.external_cache_invalidate_in =
#ifdef ENABLE_MMU_TLB
            ((bool)sd_dma_cache_invalidate_reg || (bool)eth_dma_cache_invalidate_reg) &&
                !((bool)tribe.debug_memory_wait_out) && !((bool)tribe.dmem_read_out) && !((bool)tribe.dmem_write_out);
#else
            (bool)sd_dma_cache_invalidate_reg || (bool)eth_dma_cache_invalidate_reg;
#endif
        tribe.mem_region_size_in[0] = TRIBE_MEM_REGION0_SIZE;
        tribe.mem_region_size_in[1] = TRIBE_MEM_REGION1_SIZE;
        tribe.mem_region_size_in[2] = TRIBE_MEM_REGION2_SIZE;
        tribe.mem_region_size_in[3] = TRIBE_IO_REGION_SIZE;
        tribe.axi_in___05Fawvalid_in[0] = accelerator.dma_out.awvalid_in();
        tribe.axi_in___05Fawaddr_in[0] = (uint32_t)accelerator.dma_out.awaddr_in();
        tribe.axi_in___05Fawid_in[0] = (uint32_t)accelerator.dma_out.awid_in();
        tribe.axi_in___05Fwvalid_in[0] = accelerator.dma_out.wvalid_in();
        verilator_logic_to_wide(tribe.axi_in___05Fwdata_in[0], accelerator.dma_out.wdata_in());
        tribe.axi_in___05Fwstrb_in[0] = (uint32_t)accelerator.dma_out.wstrb_in();
        tribe.axi_in___05Fwlast_in[0] = accelerator.dma_out.wlast_in();
        tribe.axi_in___05Fbready_in[0] = accelerator.dma_out.bready_in();
        tribe.axi_in___05Farvalid_in[0] = accelerator.dma_out.arvalid_in();
        tribe.axi_in___05Faraddr_in[0] = (uint32_t)accelerator.dma_out.araddr_in();
        tribe.axi_in___05Farid_in[0] = (uint32_t)accelerator.dma_out.arid_in();
        tribe.axi_in___05Frready_in[0] = accelerator.dma_out.rready_in();
        tribe.axi_in___05Fawvalid_in[1] = sdcard.dma_out.awvalid_in();
        tribe.axi_in___05Fawaddr_in[1] = (uint32_t)sdcard.dma_out.awaddr_in();
        tribe.axi_in___05Fawid_in[1] = (uint32_t)sdcard.dma_out.awid_in();
        tribe.axi_in___05Fwvalid_in[1] = sdcard.dma_out.wvalid_in();
        verilator_logic_to_wide(tribe.axi_in___05Fwdata_in[1], sdcard.dma_out.wdata_in());
        tribe.axi_in___05Fwstrb_in[1] = (uint32_t)sdcard.dma_out.wstrb_in();
        tribe.axi_in___05Fwlast_in[1] = sdcard.dma_out.wlast_in();
        tribe.axi_in___05Fbready_in[1] = sdcard.dma_out.bready_in();
        tribe.axi_in___05Farvalid_in[1] = sdcard.dma_out.arvalid_in();
        tribe.axi_in___05Faraddr_in[1] = (uint32_t)sdcard.dma_out.araddr_in();
        tribe.axi_in___05Farid_in[1] = (uint32_t)sdcard.dma_out.arid_in();
        tribe.axi_in___05Frready_in[1] = sdcard.dma_out.rready_in();
        tribe.axi_in___05Fawvalid_in[2] = ethgig_dma.dma_out.awvalid_in();
        tribe.axi_in___05Fawaddr_in[2] = (uint32_t)ethgig_dma.dma_out.awaddr_in();
        tribe.axi_in___05Fawid_in[2] = (uint32_t)ethgig_dma.dma_out.awid_in();
        tribe.axi_in___05Fwvalid_in[2] = ethgig_dma.dma_out.wvalid_in();
        verilator_logic_to_wide(tribe.axi_in___05Fwdata_in[2], ethgig_dma.dma_out.wdata_in());
        tribe.axi_in___05Fwstrb_in[2] = (uint32_t)ethgig_dma.dma_out.wstrb_in();
        tribe.axi_in___05Fwlast_in[2] = ethgig_dma.dma_out.wlast_in();
        tribe.axi_in___05Fbready_in[2] = ethgig_dma.dma_out.bready_in();
        tribe.axi_in___05Farvalid_in[2] = ethgig_dma.dma_out.arvalid_in();
        tribe.axi_in___05Faraddr_in[2] = (uint32_t)ethgig_dma.dma_out.araddr_in();
        tribe.axi_in___05Farid_in[2] = (uint32_t)ethgig_dma.dma_out.arid_in();
        tribe.axi_in___05Frready_in[2] = ethgig_dma.dma_out.rready_in();
#if defined(ENABLE_ZICSR) && defined(ENABLE_ISR)
        tribe.clint_msip_in = clint.msip_out();
        tribe.clint_mtip_in = clint.mtip_out();
        tribe.time_lo_in = clint.debug_mtime_lo_out();
        tribe.time_hi_in = clint.debug_mtime_hi_out();
        tribe.external_irq_in = plic.external_irq_out();
#endif

        tribe.clk = 0;
        tribe.reset = reset;
        tribe.eval();
#endif
        mem0._work(reset);
        mem1._work(reset);
        mem2._work(reset);
        iospace._work(reset);
        uart._work(reset);
        clint._work(reset);
        plic._work(reset);
        accelerator._work(reset);
        sdcard._work(reset);
        ethgig_dma._work(reset);
        ethgig_mac._work(reset);
        ethgig_pcs._work(reset);
        ethgig_phy._work(reset);
        ethgig_verif._work(reset);
#ifdef ENABLE_MMU_TLB
#ifdef VERILATOR
        sd_dma_cache_invalidate_ready =
            !((bool)tribe.debug_memory_wait_out) && !((bool)tribe.dmem_read_out) && !((bool)tribe.dmem_write_out);
        eth_dma_cache_invalidate_ready = sd_dma_cache_invalidate_ready;
#else
        sd_dma_cache_invalidate_ready =
            !tribe.debug_memory_wait_out() && !tribe.dmem_read_out() && !tribe.dmem_write_out();
        eth_dma_cache_invalidate_ready = sd_dma_cache_invalidate_ready;
#endif
#endif
        if (sdcard.dma_write_complete_out()) {
            sd_dma_cache_invalidate_reg._next = true;
        }
        else if (sd_dma_cache_invalidate_reg && sd_dma_cache_invalidate_ready) {
            sd_dma_cache_invalidate_reg._next = false;
        }
        else {
            sd_dma_cache_invalidate_reg._next = sd_dma_cache_invalidate_reg;
        }
        if (eth_dma_needs_cache_invalidate(ethgig_dma.tx_irq_out(), ethgig_dma.rx_irq_out())) {
            eth_dma_cache_invalidate_reg._next = true;
        }
        else if (eth_dma_cache_invalidate_reg && eth_dma_cache_invalidate_ready) {
            eth_dma_cache_invalidate_reg._next = false;
        }
        else {
            eth_dma_cache_invalidate_reg._next = eth_dma_cache_invalidate_reg;
        }
#ifdef VERILATOR
        AXI4_RESPONDER_FROM_VERILATOR(tribe, mem0.axi_in, 0);
        AXI4_RESPONDER_FROM_VERILATOR(tribe, mem1.axi_in, 1);
        AXI4_RESPONDER_FROM_VERILATOR(tribe, mem2.axi_in, 2);
        AXI4_RESPONDER_FROM_VERILATOR(tribe, iospace.slave_in, 3);
        tribe.clk = 1;
        tribe.reset = reset;
        uint64_t tribe_work_time_start = tribe_runtime_tick();
        tribe.eval();  // eval of verilator should be in the end
        runtime_tribe_work_ticks += tribe_runtime_tick() - tribe_work_time_start;
#endif

        if (reset) {
            error = false;
            sd_dma_cache_invalidate_reg.clr();
            eth_dma_cache_invalidate_reg.clr();
            return;
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        checkpoint_value(checkpoint_fd, perf_clocks);
        checkpoint_value(checkpoint_fd, perf_stall);
        checkpoint_value(checkpoint_fd, perf_hazard);
        checkpoint_value(checkpoint_fd, perf_dcache_wait);
        checkpoint_value(checkpoint_fd, perf_icache_wait);
        checkpoint_value(checkpoint_fd, perf_branch);
        checkpoint_value(checkpoint_fd, perf_icache_issue_wait_cycles);
        checkpoint_value(checkpoint_fd, perf_icache_lookup_wait_cycles);
        checkpoint_value(checkpoint_fd, perf_icache_refill_wait_cycles);
        checkpoint_value(checkpoint_fd, perf_icache_init_wait_cycles);
        checkpoint_value(checkpoint_fd, perf_icache_hit_lookup_cycles);
        checkpoint_value(checkpoint_fd, tohost_addr);
        checkpoint_value(checkpoint_fd, tohost_value);
        checkpoint_value(checkpoint_fd, reset_pc);
        checkpoint_value(checkpoint_fd, boot_hartid);
        checkpoint_value(checkpoint_fd, boot_dtb_addr);
        checkpoint_value(checkpoint_fd, boot_priv);
        checkpoint_value(checkpoint_fd, start_mem_addr);
        checkpoint_value(checkpoint_fd, ram_size);
        checkpoint_value(checkpoint_fd, tohost_done);
        checkpoint_value(checkpoint_fd, _system_clock);
        uart_rx_valid_reg.strobe(checkpoint_fd);
        uart_rx_data_reg.strobe(checkpoint_fd);
        uart_script_pos_reg.strobe(checkpoint_fd);
        // Keep the default checkpoint format compatible with existing Linux
        // checkpoints. Tests that need exact scripted UART pacing across
        // save/restore opt in explicitly.
        if (std::getenv("TRIBE_CHECKPOINT_UART_SCRIPT_DELAY")) {
            uart_script_delay_reg.strobe(checkpoint_fd);
        }
        else {
            uart_script_delay_reg.strobe();
        }
        uart_script_enabled_reg.strobe(checkpoint_fd);
        uart_script_reported_reg.strobe(checkpoint_fd);
        sd_dma_cache_invalidate_reg.strobe(checkpoint_fd);
        eth_dma_cache_invalidate_reg.strobe();
#ifndef VERILATOR
        uint64_t tribe_strobe_time_start = tribe_runtime_tick();
        tribe._strobe(checkpoint_fd);
        runtime_tribe_strobe_ticks += tribe_runtime_tick() - tribe_strobe_time_start;
#endif
        mem0._strobe(checkpoint_fd);  // we use these modules in Verilator test
        mem1._strobe(checkpoint_fd);
        mem2._strobe(checkpoint_fd);
        iospace._strobe(checkpoint_fd);
        uart._strobe(checkpoint_fd);
        clint._strobe(checkpoint_fd);
        plic._strobe(checkpoint_fd);
        accelerator._strobe(checkpoint_fd);
        sdcard._strobe(checkpoint_fd);
        ethgig_dma._strobe();
        ethgig_mac._strobe();
        ethgig_pcs._strobe();
        ethgig_phy._strobe();
        ethgig_verif._strobe();
        if (eth_tap_socket.active()) {
            eth_tap_socket.pump(ethgig_verif);
        }
        if (!eth_tap_socket.active() && eth_loopback_enabled && ethgig_verif.has_tx_packet()) {
            auto packet = ethgig_verif.pop_tx_packet();
            ethgig_verif.push_rx_packet(packet);
        }
        if (checkpoint_fd && checkpoint_reading(checkpoint_fd)) {
            sdcard_verif._strobe(checkpoint_fd);
        }
        else {
            sdcard_verif._work(false);
            sdcard_verif._strobe(checkpoint_fd);
        }
    }

    void _work_neg(bool reset)
    {
#ifdef VERILATOR
        tribe.clk = 0;
        tribe.reset = reset;
        tribe.eval();  // eval of verilator should be in the end
#else
        tribe._work_neg(reset);
#endif

        if (debugen_in) {
            printf("----------- %ld\n", _system_clock);
        }
    }

    void _strobe_neg()
    {
    }

    void perf_reset()
    {
        perf_clocks = 0;
        perf_stall = 0;
        perf_hazard = 0;
        perf_dcache_wait = 0;
        perf_icache_wait = 0;
        perf_branch = 0;
        perf_icache_issue_wait_cycles = 0;
        perf_icache_lookup_wait_cycles = 0;
        perf_icache_refill_wait_cycles = 0;
        perf_icache_init_wait_cycles = 0;
        perf_icache_hit_lookup_cycles = 0;
        runtime_strobe_ticks = 0;
        runtime_tribe_strobe_ticks = 0;
        runtime_checkpoint_ticks = 0;
        runtime_perf_ticks = 0;
        runtime_work_ticks = 0;
        runtime_tribe_work_ticks = 0;
        runtime_uart_ticks = 0;
        runtime_trace_ticks = 0;
        runtime_negedge_ticks = 0;
        runtime_total_ticks = 0;
    }

    void perf_sample()
    {
        auto perf = PERF_VALUE(tribe.perf_out);
        bool hazard = perf.hazard_stall;
        bool branch = perf.branch_stall;
        bool dcache_wait = perf.dcache_wait;
        bool icache_wait = perf.icache_wait;

        ++perf_clocks;
        perf_hazard += hazard;
        perf_branch += branch;
        perf_dcache_wait += dcache_wait;
        perf_icache_wait += icache_wait;
        perf_icache_issue_wait_cycles += perf.icache.issue_wait;
        perf_icache_lookup_wait_cycles += perf.icache.lookup_wait;
        perf_icache_refill_wait_cycles += perf.icache.refill_wait;
        perf_icache_init_wait_cycles += perf.icache.init_wait;
        perf_icache_hit_lookup_cycles += perf.icache.hit && perf.icache.lookup_wait;
        perf_stall += hazard || branch || dcache_wait || icache_wait;
    }

    void debug_perf_counters_print()
    {
        std::print(" perf[c{:04x} s{:04x} h{:04x} b{:04x} dc{:04x} ic{:04x} ii{:04x} il{:04x} ih{:04x} ir{:04x} in{:04x}]\n",
            (uint16_t)perf_clocks,
            (uint16_t)perf_stall,
            (uint16_t)perf_hazard,
            (uint16_t)perf_branch,
            (uint16_t)perf_dcache_wait,
            (uint16_t)perf_icache_wait,
            (uint16_t)perf_icache_issue_wait_cycles,
            (uint16_t)perf_icache_lookup_wait_cycles,
            (uint16_t)perf_icache_hit_lookup_cycles,
            (uint16_t)perf_icache_refill_wait_cycles,
            (uint16_t)perf_icache_init_wait_cycles);
    }

    void perf_print()
    {
        auto percent = [&](uint64_t value) {
            return perf_clocks ? (100.0 * (double)value / (double)perf_clocks) : 0.0;
        };
        auto runtime_part_percent = [&](uint64_t ticks) {
            return runtime_total_ticks ? (100.0 * (double)ticks / (double)runtime_total_ticks) : 0.0;
        };

        std::print("Performance: clocks={}, stalled={:.2f}% ({})"
                   ", hazards={:.2f}% ({})"
                   ", dcache_wait={:.2f}% ({})"
                   ", icache_wait={:.2f}% ({})"
                   ", branching={:.2f}% ({})\n",
            perf_clocks,
            percent(perf_stall), perf_stall,
            percent(perf_hazard), perf_hazard,
            percent(perf_dcache_wait), perf_dcache_wait,
            percent(perf_icache_wait), perf_icache_wait,
            percent(perf_branch), perf_branch);
        if (std::getenv("TRIBE_PERF_DETAIL")) {
            std::print("I-cache wait detail: issue={:.2f}% ({})"
                       ", lookup={:.2f}% ({})"
                       ", lookup_hit={:.2f}% ({})"
                       ", refill={:.2f}% ({})"
                       ", init={:.2f}% ({})\n",
                percent(perf_icache_issue_wait_cycles), perf_icache_issue_wait_cycles,
                percent(perf_icache_lookup_wait_cycles), perf_icache_lookup_wait_cycles,
                percent(perf_icache_hit_lookup_cycles), perf_icache_hit_lookup_cycles,
                percent(perf_icache_refill_wait_cycles), perf_icache_refill_wait_cycles,
                percent(perf_icache_init_wait_cycles), perf_icache_init_wait_cycles);
            std::print("Runtime detail: checkpoint={:.2f}% strobe={:.2f}% tribe_strobe={:.2f}% perf={:.2f}% work={:.2f}% tribe_work={:.2f}% uart={:.2f}% trace_probe={:.2f}% negedge={:.2f}%\n",
                runtime_part_percent(runtime_checkpoint_ticks),
                runtime_part_percent(runtime_strobe_ticks),
                runtime_part_percent(runtime_tribe_strobe_ticks),
                runtime_part_percent(runtime_perf_ticks),
                runtime_part_percent(runtime_work_ticks),
                runtime_part_percent(runtime_tribe_work_ticks),
                runtime_part_percent(runtime_uart_ticks),
                runtime_part_percent(runtime_trace_ticks),
                runtime_part_percent(runtime_negedge_ticks));
        }
    }

    bool run(std::string filename, size_t start_offset, std::string expected_log = "rv32i.log", uint64_t max_cycles = 2000000, uint32_t tohost = 0, uint32_t mem_base = 0, uint32_t ram_words = DEFAULT_RAM_SIZE, bool raw_program = false, uint32_t boot_hartid_arg = 0, uint32_t boot_dtb_addr_arg = 0, uint32_t boot_priv_arg = 3, bool elf_phys_override = false, uint32_t elf_phys_offset = 0, const std::string& dtb_file = "", bool linux_earlycon_mapbase = false, const std::string& initramfs_file = "", uint32_t initramfs_addr = 0, const std::string& checkpoint_load_file = "", const std::string& checkpoint_save_file = "", uint64_t checkpoint_save_cycle = 0, bool append_output = false, const std::string& bootargs = "", bool checkpoint_save_only_success = false, const std::string& expected_output_contains = "", const std::string& test_label = "", bool mirror_uart_output = false, bool interactive_uart_input = false, const std::string& checkpoint_save_after = "", const std::string& sd_image_file = "", const std::string& eth_tap_socket_path = "");
};
