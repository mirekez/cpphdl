#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    std::ifstream manifest(argv[1]);
    if (!manifest) return 2;
    std::vector<std::string> arguments{argv[2]};
    std::vector<int> descriptors;
    std::string path;
    while (std::getline(manifest, path)) {
        const int descriptor = memfd_create("cva6-object", 0);
        if (descriptor < 0) return 2;
        descriptors.push_back(descriptor);
        const pid_t decompress = fork();
        if (decompress < 0) return 2;
        if (decompress == 0) {
            if (dup2(descriptor, STDOUT_FILENO) < 0) _exit(2);
            execlp("gzip", "gzip", "-dc", "--", path.c_str(), nullptr);
            _exit(127);
        }
        int status;
        while (waitpid(decompress, &status, 0) < 0) if (errno != EINTR) return 2;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 2;
        arguments.push_back("/proc/" + std::to_string(getpid()) + "/fd/" + std::to_string(descriptor));
    }
    if (descriptors.empty()) return 2;
    for (int index = 3; index < argc; ++index) arguments.emplace_back(argv[index]);
    std::vector<char*> command;
    for (auto& argument : arguments) command.push_back(argument.data());
    command.push_back(nullptr);
    const pid_t child = fork();
    if (child < 0) return 2;
    if (child == 0) { execvp(command[0], command.data()); _exit(127); }
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return 2;
    for (int descriptor : descriptors) close(descriptor);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
