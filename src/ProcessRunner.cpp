#include "ProcessRunner.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <ctime>
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

namespace codex_lan_agent {

std::string StatusTimeStampText() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t current_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &current_time);
#else
    localtime_r(&current_time, &local_tm);
#endif
    char buffer[64];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04d-%02d-%02d %02d:%02d:%02d",
        local_tm.tm_year + 1900,
        local_tm.tm_mon + 1,
        local_tm.tm_mday,
        local_tm.tm_hour,
        local_tm.tm_min,
        local_tm.tm_sec);
    return buffer;
}

void AppendLogHeader(
    const std::string & log_path,
    const std::string & command_line,
    const std::string & working_directory) {
    std::ofstream output(log_path, std::ios::out | std::ios::app);
    output << "[task_start]\n";
    output << "timestamp=" << StatusTimeStampText() << "\n";
    output << "working_directory=" << working_directory << "\n";
    output << "command=" << command_line << "\n";
    output << "\n";
}

void AppendLogHeartbeat(
    const std::string & log_path,
    unsigned long process_id,
    long long quiet_sec) {
    std::ofstream output(log_path, std::ios::out | std::ios::app);
    output << "[task_heartbeat]\n";
    output << "timestamp=" << StatusTimeStampText() << "\n";
    output << "process_id=" << process_id << "\n";
    output << "quiet_sec=" << quiet_sec << "\n";
    output << "\n";
}

std::uintmax_t SafeFileSize(const std::string & path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : size;
}

#ifdef _WIN32
bool KillProcessTreeWindows(unsigned long pid) {
    const std::string command =
        "cmd.exe /c taskkill /PID " + std::to_string(pid) + " /T /F >nul 2>&1";
    return std::system(command.c_str()) == 0;
}
#endif

#ifndef _WIN32
std::string GetShellProgram() {
    const char * shell_program = std::getenv("CODEX_LAN_AGENT_SHELL");
    if (shell_program != nullptr && shell_program[0] != '\0') {
        return shell_program;
    }
    return "/bin/sh";
}

std::string GetShellFlag() {
    const char * shell_flag = std::getenv("CODEX_LAN_AGENT_SHELL_FLAG");
    if (shell_flag != nullptr && shell_flag[0] != '\0') {
        return shell_flag;
    }
    return "-lc";
}
#endif

std::string CompletionReasonFromResult(const ProcessRunResult & result) {
    if (result.stalled) {
        return "stall_timeout";
    }
    if (result.timed_out) {
        return "total_timeout";
    }
    return result.exit_code == 0 ? "process_exit_0" : "process_exit_nonzero";
}

bool RunCommandWithLog(
    const std::string & command_line,
    const std::string & working_directory,
    const std::string & log_path,
    int timeout_sec,
    int stall_timeout_sec,
    ProcessRunResult * result,
    std::string * error_message) {
    if (!result) {
        if (error_message) {
            *error_message = "process result pointer is null";
        }
        return false;
    }

    std::filesystem::create_directories(std::filesystem::path(log_path).parent_path());
    AppendLogHeader(log_path, command_line, working_directory);
    result->log_path = log_path;
    result->started_at = StatusTimeStampText();
    result->last_output_at = result->started_at;
    result->process_id = 0;
    result->heartbeat_count = 0;
    result->runtime_sec = 0;
    result->quiet_sec_at_finish = 0;
    result->process_output_observed = false;
    result->stalled = false;
    result->completion_reason.clear();
    result->timed_out = false;

#ifdef _WIN32
    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE log_file = CreateFileA(
        log_path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security_attributes,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (log_file == INVALID_HANDLE_VALUE) {
        if (error_message) {
            *error_message = "failed to open log file: " + log_path;
        }
        return false;
    }

    SetFilePointer(log_file, 0, nullptr, FILE_END);

    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdOutput = log_file;
    startup_info.hStdError = log_file;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process_info{};
    std::vector<char> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back('\0');

    BOOL created = CreateProcessA(
        nullptr,
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup_info,
        &process_info);

    CloseHandle(log_file);

    if (!created) {
        if (error_message) {
            *error_message = "failed to start process: " + command_line;
        }
        return false;
    }
    result->process_id = static_cast<unsigned long>(process_info.dwProcessId);

    const auto started = std::chrono::steady_clock::now();
    auto last_output_change = started;
    auto last_heartbeat = started;
    std::uintmax_t last_size = SafeFileSize(log_path);

    DWORD wait_result = WAIT_TIMEOUT;
    while (true) {
        wait_result = WaitForSingleObject(process_info.hProcess, 500);
        if (wait_result == WAIT_OBJECT_0) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const std::uintmax_t current_size = SafeFileSize(log_path);
        if (current_size != last_size) {
            last_size = current_size;
            last_output_change = now;
            result->process_output_observed = true;
            result->last_output_at = StatusTimeStampText();
        }

        if (timeout_sec > 0) {
            const auto elapsed_sec =
                std::chrono::duration_cast<std::chrono::seconds>(now - started).count();
            if (elapsed_sec >= timeout_sec) {
                result->timed_out = true;
                KillProcessTreeWindows(result->process_id);
                break;
            }
        }

        if (stall_timeout_sec > 0) {
            const auto quiet_sec =
                std::chrono::duration_cast<std::chrono::seconds>(now - last_output_change).count();
            if (quiet_sec >= stall_timeout_sec) {
                result->stalled = true;
                KillProcessTreeWindows(result->process_id);
                break;
            }
        } else {
            const auto heartbeat_sec =
                std::chrono::duration_cast<std::chrono::seconds>(now - last_heartbeat).count();
            if (heartbeat_sec >= 30) {
                const auto quiet_sec =
                    std::chrono::duration_cast<std::chrono::seconds>(now - last_output_change).count();
                AppendLogHeartbeat(log_path, result->process_id, quiet_sec);
                ++result->heartbeat_count;
                last_heartbeat = now;
                last_size = SafeFileSize(log_path);
            }
        }
    }

    DWORD exit_code = 0;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    if (result->stalled) {
        result->exit_code = 125;
    } else if (result->timed_out) {
        result->exit_code = 124;
    } else {
        result->exit_code = static_cast<int>(exit_code);
    }
    const auto finished = std::chrono::steady_clock::now();
    result->runtime_sec = std::chrono::duration_cast<std::chrono::seconds>(finished - started).count();
    result->quiet_sec_at_finish =
        std::chrono::duration_cast<std::chrono::seconds>(finished - last_output_change).count();
    result->finished_at = StatusTimeStampText();
    result->completion_reason = CompletionReasonFromResult(*result);

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return true;
#else
    const int log_fd = open(
        log_path.c_str(),
        O_CREAT | O_WRONLY | O_APPEND,
        0644);
    if (log_fd < 0) {
        if (error_message) {
            *error_message = "failed to open log file: " + log_path;
        }
        return false;
    }

    const pid_t child_pid = fork();
    if (child_pid < 0) {
        close(log_fd);
        if (error_message) {
            *error_message = "failed to fork process";
        }
        return false;
    }

    if (child_pid == 0) {
        if (!working_directory.empty()) {
            if (chdir(working_directory.c_str()) != 0) {
                _exit(126);
            }
        }
        dup2(log_fd, STDOUT_FILENO);
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);
        const std::string shell_program = GetShellProgram();
        const std::string shell_argv0 = std::filesystem::path(shell_program).filename().string();
        const std::string shell_flag = GetShellFlag();
        if (shell_flag.empty()) {
            execl(
                shell_program.c_str(),
                shell_argv0.c_str(),
                command_line.c_str(),
                static_cast<char *>(nullptr));
        } else {
            execl(
                shell_program.c_str(),
                shell_argv0.c_str(),
                shell_flag.c_str(),
                command_line.c_str(),
                static_cast<char *>(nullptr));
        }
        _exit(127);
    }

    close(log_fd);

    int status = 0;
    result->timed_out = false;
    result->process_id = static_cast<unsigned long>(child_pid);
    const auto started = std::chrono::steady_clock::now();
    auto last_output_change = started;
    std::uintmax_t last_size = SafeFileSize(log_path);
    const auto deadline =
        started + std::chrono::seconds(timeout_sec > 0 ? timeout_sec : 31536000);
    while (true) {
        const pid_t wait_result = waitpid(child_pid, &status, WNOHANG);
        if (wait_result == child_pid) {
            break;
        }
        if (wait_result < 0) {
            if (error_message) {
                *error_message = "waitpid failed";
            }
            return false;
        }
        const std::uintmax_t current_size = SafeFileSize(log_path);
        if (current_size != last_size) {
            last_size = current_size;
            last_output_change = std::chrono::steady_clock::now();
            result->process_output_observed = true;
            result->last_output_at = StatusTimeStampText();
        }
        if (timeout_sec > 0 && std::chrono::steady_clock::now() >= deadline) {
            result->timed_out = true;
            kill(child_pid, SIGKILL);
            waitpid(child_pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (WIFEXITED(status)) {
        result->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result->exit_code = 128 + WTERMSIG(status);
    } else {
        result->exit_code = -1;
    }
    const auto finished = std::chrono::steady_clock::now();
    result->runtime_sec = std::chrono::duration_cast<std::chrono::seconds>(finished - started).count();
    result->quiet_sec_at_finish =
        std::chrono::duration_cast<std::chrono::seconds>(finished - last_output_change).count();
    result->finished_at = StatusTimeStampText();
    result->completion_reason = CompletionReasonFromResult(*result);
    return true;
#endif
}

}  // namespace codex_lan_agent
