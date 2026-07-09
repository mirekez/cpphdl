#pragma once

#include "cpphdl.h"

#if !defined(SYNTHESIS)
#include "verif/RGMIIVerif.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#endif

struct TribeCoreDebug
{
    uint32_t pc;
    bool fetch_valid;
    bool memory_wait;
} __PACKED;

struct TribeMmuDebug
{
    bool immu_ptw_read;
    uint32_t immu_ptw_addr;
    bool immu_busy;
    bool immu_fault;
    uint32_t immu_paddr;
    uint32_t immu_last_addr;
    uint32_t immu_last_pte;
    bool dmmu_ptw_read;
    uint32_t dmmu_ptw_addr;
    bool dmmu_busy;
    bool dmmu_fault;
    uint32_t ptw_word;
} __PACKED;

struct TribeCacheDebug
{
    bool icache_read_valid;
    uint32_t icache_read_addr;
    bool icache_read_in;
    bool icache_stall_in;
    bool dcache_read_valid;
    uint32_t dcache_read_addr;
    uint32_t dcache_read_data;
    bool dcache_cpu_read;
    bool dcache_cpu_write;
    uint32_t dcache_cpu_addr;
    uint32_t dcache_cpu_wdata;
    uint8_t dcache_cpu_wmask;
} __PACKED;

struct TribeWritebackDebug
{
    bool load_ready;
    bool mem_wait;
    bool load_data_valid;
    uint32_t load_addr;
    bool split_low_valid;
    bool split_high_valid;
    bool held_load_valid;
    bool split_load_in;
    uint32_t alu_addr;
    uint32_t state_pc;
    uint8_t state_wb_op;
    uint8_t state_mem_op;
    uint8_t state_rd;
    uint8_t state_funct3;
} __PACKED;

struct TribeCsrDebug
{
    uint32_t satp;
    uint32_t mstatus;
    uint32_t mtvec;
    uint32_t mepc;
    uint32_t mcause;
    uint32_t mtval;
    uint32_t sepc;
    uint32_t stvec;
    uint32_t scause;
    uint32_t stval;
    u<2> priv;
} __PACKED;

struct TribeIrqDebug
{
    bool valid;
    uint32_t cause;
    bool to_supervisor;
    uint32_t mip;
    uint32_t mie;
    uint32_t mideleg;
} __PACKED;

struct TribeRegsDebug
{
    uint32_t ra;
    bool write;
    bool write_actual;
    uint8_t wr_id;
    uint32_t data;
} __PACKED;

struct TribeBranchDebug
{
    bool taken_now;
    uint32_t target_now;
} __PACKED;

struct TribeDecodeDebug
{
    uint32_t instr;
    uint32_t pc;
    uint8_t br;
    uint32_t imm;
} __PACKED;

struct TribeSbiDebug
{
    bool ecall;
    uint32_t a7;
    uint32_t a6;
    uint32_t a0;
    bool base;
    bool noop;
    bool handled;
} __PACKED;

#if !defined(SYNTHESIS)
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
#endif
