#include "ProcessRunner.h"
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>

// Run in a disposable directory. No production listener or lock is touched.
int main() {
    const int lock = open("regression.lock", O_CREAT | O_RDWR, 0600);
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (lock < 0 || sock < 0 || flock(lock, LOCK_EX | LOCK_NB)) return 2;
    codex_lan_agent::ProcessRunResult result;
    std::string error;
    const std::string command =
        "test ! -e /proc/self/fd/" + std::to_string(lock) +
        " && test ! -e /proc/self/fd/" + std::to_string(sock) +
        " && echo FD_ISOLATION_PASS";
    const bool ok = codex_lan_agent::RunCommandWithLog(
        command, ".", "./regression.log", 15, 10, &result, &error);
    const bool parent_ok = fcntl(lock, F_GETFD) >= 0 && fcntl(sock, F_GETFD) >= 0;
    close(sock);
    close(lock);
    std::cout << "exit=" << result.exit_code << " parent=" << parent_ok << " " << error << '\n';
    return ok && result.exit_code == 0 && parent_ok ? 0 : 1;
}
