#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/quota.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    struct statvfs fs {};
    if (statvfs(argv[1], &fs)) return 1;
    uint64_t available = uint64_t(fs.f_bavail) * fs.f_frsize;
    const int fd = open(argv[1], O_RDONLY | O_DIRECTORY);
    if (fd < 0) return 1;
    struct dqblk quota {};
    // df reports filesystem space, which can exceed this user's tmpfs quota.
    if (syscall(SYS_quotactl_fd, fd, QCMD(Q_GETQUOTA, USRQUOTA), getuid(), &quota) == 0) {
        uint64_t limit = quota.dqb_bhardlimit;
        if (quota.dqb_bsoftlimit && (!limit || quota.dqb_bsoftlimit < limit))
            limit = quota.dqb_bsoftlimit;
        if (limit) {
            const uint64_t bytes = limit * 1024;
            available = std::min(available, bytes > quota.dqb_curspace ? bytes - quota.dqb_curspace : 0);
        }
    }
    close(fd);
    std::printf("%llu\n", static_cast<unsigned long long>(available / 1024));
}
