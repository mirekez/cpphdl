#include <array>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <linux/if.h>
#include <linux/if_tun.h>
#include <net/if_arp.h>

static volatile sig_atomic_t stop_requested = 0;

static void signal_handler(int)
{
    stop_requested = 1;
}

static void usage(const char* argv0)
{
    std::cerr
        << "usage: " << argv0 << " [--tap tap0] [--socket /tmp/tribe-eth.sock]\n"
        << "\n"
        << "Creates or attaches a TAP interface and relays Ethernet frames to a\n"
        << "Tribe simulator started with --eth-tap-socket using the same path.\n";
}

static std::string frame_prefix(const uint8_t* data, size_t len)
{
    std::ostringstream out;
    size_t limit = std::min<size_t>(len, 32);
    for (size_t i = 0; i < limit; ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << std::hex << std::setw(2) << std::setfill('0') << (uint32_t)data[i];
    }
    if (len > limit) {
        out << " ...";
    }
    return out.str();
}

class TraceLog
{
    bool enabled = false;
    std::ofstream file;

public:
    TraceLog()
    {
        enabled = std::getenv("ETHGIG_TAP_TRACE") != nullptr;
        if (const char* path = std::getenv("ETHGIG_TAP_TRACE_FILE")) {
            file.open(path, std::ios::out | std::ios::app);
            if (!file.is_open()) {
                std::cerr << "can't open ETHGIG_TAP_TRACE_FILE '" << path << "'\n";
            }
            enabled = true;
        }
    }

    void frame(const char* direction, const uint8_t* data, size_t len)
    {
        if (!enabled) {
            return;
        }
        std::ostream& out = file.is_open() ? file : std::cerr;
        out << "ethgig-tap " << direction << " len=" << len
            << " data=" << frame_prefix(data, len) << "\n";
        out.flush();
    }
};

static std::string mac_text(const uint8_t* mac)
{
    std::ostringstream out;
    for (size_t i = 0; i < 6; ++i) {
        if (i != 0) {
            out << ':';
        }
        out << std::hex << std::setw(2) << std::setfill('0') << (uint32_t)mac[i];
    }
    return out.str();
}

static bool get_iface_mac(const std::string& ifname, uint8_t mac[6])
{
    int ctl_fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (ctl_fd < 0) {
        return false;
    }
    ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
    bool ok = ::ioctl(ctl_fd, SIOCGIFHWADDR, &ifr) == 0;
    if (ok) {
        std::memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
    }
    ::close(ctl_fd);
    return ok;
}

static bool set_iface_mac(const std::string& ifname, const uint8_t mac[6])
{
    int ctl_fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (ctl_fd < 0) {
        return false;
    }
    ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    std::memcpy(ifr.ifr_hwaddr.sa_data, mac, 6);
    bool ok = ::ioctl(ctl_fd, SIOCSIFHWADDR, &ifr) == 0;
    ::close(ctl_fd);
    return ok;
}

static bool is_broadcast_mac(const uint8_t* mac)
{
    for (size_t i = 0; i < 6; ++i) {
        if (mac[i] != 0xffu) {
            return false;
        }
    }
    return true;
}

static bool is_zero_mac(const uint8_t* mac)
{
    for (size_t i = 0; i < 6; ++i) {
        if (mac[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool mac_equal(const uint8_t* lhs, const uint8_t* rhs)
{
    return std::memcmp(lhs, rhs, 6) == 0;
}

static bool is_arp_request(const uint8_t* data, size_t len)
{
    return len >= 42 &&
        data[12] == 0x08u && data[13] == 0x06u &&
        data[20] == 0x00u && data[21] == 0x01u;
}

static int open_tap(const std::string& tap_name)
{
    int fd = ::open("/dev/net/tun", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        std::cerr << "can't open /dev/net/tun: " << std::strerror(errno) << "\n";
        return -1;
    }

    ifreq ifr{};
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (!tap_name.empty()) {
        std::strncpy(ifr.ifr_name, tap_name.c_str(), IFNAMSIZ - 1);
    }

    if (::ioctl(fd, TUNSETIFF, &ifr) != 0) {
        std::cerr << "TUNSETIFF failed for '" << tap_name << "': " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    int ctl_fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (ctl_fd >= 0) {
        ifreq up_ifr{};
        std::strncpy(up_ifr.ifr_name, ifr.ifr_name, IFNAMSIZ - 1);
        if (::ioctl(ctl_fd, SIOCGIFFLAGS, &up_ifr) == 0) {
            up_ifr.ifr_flags |= IFF_UP | IFF_RUNNING | IFF_PROMISC;
            if (::ioctl(ctl_fd, SIOCSIFFLAGS, &up_ifr) != 0) {
                std::cerr << "warning: can't bring TAP '" << ifr.ifr_name
                          << "' up: " << std::strerror(errno) << "\n";
            }
        }
        else {
            std::cerr << "warning: can't read TAP '" << ifr.ifr_name
                      << "' flags: " << std::strerror(errno) << "\n";
        }
        ::close(ctl_fd);
    }
    else {
        std::cerr << "warning: can't create TAP control socket: " << std::strerror(errno) << "\n";
    }

    std::cout << "TAP interface: " << ifr.ifr_name << "\n";
    return fd;
}

static bool sockaddr_from_path(const std::string& path, sockaddr_un& addr)
{
    if (path.empty() || path.size() >= sizeof(addr.sun_path)) {
        std::cerr << "invalid socket path: " << path << "\n";
        return false;
    }
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    return true;
}

static int open_socket(const std::string& socket_path)
{
    sockaddr_un addr{};
    if (!sockaddr_from_path(socket_path, addr)) {
        return -1;
    }

    int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    ::unlink(socket_path.c_str());
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "bind '" << socket_path << "' failed: " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }

    std::cout << "Bridge socket: " << socket_path << "\n";
    return fd;
}

int main(int argc, char** argv)
{
    std::string tap_name = "tap-tribe";
    std::string socket_path = "/tmp/tribe-ethgig.sock";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tap") == 0 && i + 1 < argc) {
            tap_name = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
            continue;
        }
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        usage(argv[0]);
        return 2;
    }

    int tap_fd = open_tap(tap_name);
    if (tap_fd < 0) {
        return 1;
    }

    int sock_fd = open_socket(socket_path);
    if (sock_fd < 0) {
        ::close(tap_fd);
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    static constexpr uint8_t MSG_HELLO = 1;
    static constexpr uint8_t MSG_FRAME = 2;
    sockaddr_un peer_addr{};
    socklen_t peer_len = 0;
    bool peer_valid = false;
    uint64_t tap_to_tribe = 0;
    uint64_t tribe_to_tap = 0;
    uint8_t tap_mac[6]{};
    (void)get_iface_mac(tap_name, tap_mac);
    TraceLog trace;

    while (!stop_requested) {
        pollfd pfds[2]{};
        pfds[0].fd = tap_fd;
        pfds[0].events = POLLIN;
        pfds[1].fd = sock_fd;
        pfds[1].events = POLLIN;

        int rc = ::poll(pfds, 2, 1000);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "poll failed: " << std::strerror(errno) << "\n";
            break;
        }

        if ((pfds[1].revents & POLLIN) != 0) {
            for (;;) {
                std::array<uint8_t, 4096> msg{};
                sockaddr_un from{};
                socklen_t from_len = sizeof(from);
                ssize_t got = ::recvfrom(sock_fd, msg.data(), msg.size(), MSG_DONTWAIT,
                    reinterpret_cast<sockaddr*>(&from), &from_len);
                if (got < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    std::cerr << "recvfrom socket failed: " << std::strerror(errno) << "\n";
                    break;
                }
                if (got == 0) {
                    break;
                }

                if (msg[0] == MSG_HELLO) {
                    peer_addr = from;
                    peer_len = from_len;
                    peer_valid = true;
                    std::cout << "Simulator connected\n";
                    continue;
                }

                if (got >= 3 && msg[0] == MSG_FRAME) {
                    size_t len = (size_t(msg[1]) << 8) | msg[2];
                    if (len <= (size_t)got - 3) {
                        const uint8_t* frame = msg.data() + 3;
                        if (is_arp_request(frame, len) &&
                            !is_broadcast_mac(frame) &&
                            !is_zero_mac(frame) &&
                            !mac_equal(frame, tap_mac)) {
                            uint8_t old_mac[6]{};
                            std::memcpy(old_mac, tap_mac, 6);
                            if (set_iface_mac(tap_name, frame)) {
                                std::memcpy(tap_mac, frame, 6);
                                std::cerr << "TAP MAC changed for guest ARP cache: "
                                          << mac_text(old_mac) << " -> " << mac_text(tap_mac) << "\n";
                            }
                            else {
                                std::cerr << "warning: can't set TAP MAC to " << mac_text(frame)
                                          << " for guest ARP cache\n";
                            }
                        }
                        ssize_t wrote = ::write(tap_fd, msg.data() + 3, len);
                        if (wrote < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                            trace.frame("tribe->tap-write-failed", msg.data() + 3, len);
                            std::cerr << "write TAP failed: " << std::strerror(errno)
                                      << " len=" << len
                                      << " data=" << frame_prefix(msg.data() + 3, len) << "\n";
                        }
                        else if (wrote == (ssize_t)len) {
                            ++tribe_to_tap;
                            trace.frame("tribe->tap", msg.data() + 3, len);
                        }
                    }
                }
            }
        }

        if ((pfds[0].revents & POLLIN) != 0) {
            for (;;) {
                std::array<uint8_t, 2051> msg{};
                ssize_t got = ::read(tap_fd, msg.data() + 3, msg.size() - 3);
                if (got < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    std::cerr << "read TAP failed: " << std::strerror(errno) << "\n";
                    break;
                }
                if (got == 0) {
                    break;
                }
                if (!peer_valid) {
                    continue;
                }
                msg[0] = MSG_FRAME;
                msg[1] = (uint8_t)(got >> 8);
                msg[2] = (uint8_t)got;
                ssize_t sent = ::sendto(sock_fd, msg.data(), (size_t)got + 3, MSG_DONTWAIT,
                    reinterpret_cast<sockaddr*>(&peer_addr), peer_len);
                if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "sendto simulator failed: " << std::strerror(errno) << "\n";
                }
                else if (sent == got + 3) {
                    ++tap_to_tribe;
                    trace.frame("tap->tribe", msg.data() + 3, (size_t)got);
                }
            }
        }
    }

    std::cout << "Frames TAP->Tribe: " << tap_to_tribe
              << ", Tribe->TAP: " << tribe_to_tap << "\n";
    ::close(sock_fd);
    ::close(tap_fd);
    ::unlink(socket_path.c_str());
    return 0;
}
