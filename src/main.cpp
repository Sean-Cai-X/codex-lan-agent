#include "AgentConfig.h"
#include "HttpClient.h"
#include "ProcessRunner.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <condition_variable>
#include <cerrno>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <cstring>
#include <thread>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winreg.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace {

using codex_lan_agent::AgentConfig;

std::string Trim(const std::string & value);
std::string ToLowerAscii(std::string value);

// Manual compile gate: change to true only on the intended remote machine
// after review, then rebuild on that machine.
constexpr bool kRemoteCompileGateApproved = false;
static_assert(
    kRemoteCompileGateApproved,
    "CODEX_REMOTE_COMPILE_STEP: change kRemoteCompileGateApproved to true after manual review on the target remote machine, then rebuild codex_lan_agent.exe");

struct CommandResult {
    int exit_code = 0;
    bool ok = true;
    std::unordered_map<std::string, std::string> fields;
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponseSpec {
    int status_code = 200;
    std::string status_text = "OK";
    std::string body = "{}";
    std::string content_type = "application/json";
    std::unordered_map<std::string, std::string> headers;
};

enum class TaskKind {
    kCliProfile,
    kCase,
    kRagFlow,
    kLocalChat
};

struct TaskRecord {
    std::string task_id;
    TaskKind kind = TaskKind::kCliProfile;
    std::string arg1;
    std::string arg2;
    std::string pending_log_path;
    std::string resource_key;
    std::string status = "queued";
    std::string submitted_at;
    std::string started_at;
    std::string completed_at;
    CommandResult result;
};

class TaskManager {
public:
    explicit TaskManager(const AgentConfig & config);
    ~TaskManager();

    std::string EnqueueCliProfile(const std::string & profile, const std::string & args);
    std::string EnqueueCase(const std::string & case_path);
    std::string EnqueueRagFlow(const std::string & query, const std::string & mode);
    std::string EnqueueLocalChat(const std::string & scope, const std::string & question, const std::string & mode);
    CommandResult GetTaskResult(const std::string & task_id) const;
    CommandResult GetLatestTaskResult() const;
    int QueueDepth() const;

private:
    std::string EnqueueTask(TaskKind kind, const std::string & arg1, const std::string & arg2);
    void PruneCompletedTasksLocked();
    void WorkerLoop();
    static std::string StatusTimeStamp();
    static std::string TaskKindName(TaskKind kind);

    const AgentConfig & config_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::unordered_map<std::string, TaskRecord> tasks_;
    std::deque<std::string> pending_ids_;
    std::deque<std::string> completed_ids_;
    std::thread worker_;
    bool stop_ = false;
    unsigned long long next_id_ = 1;
    std::size_t max_completed_history_ = 100;
};

class WsaSession {
public:
    WsaSession() {
#ifdef _WIN32
        valid_ = WSAStartup(MAKEWORD(2, 2), &data_) == 0;
#else
        valid_ = true;
#endif
    }

    ~WsaSession() {
#ifdef _WIN32
        if (valid_) {
            WSACleanup();
        }
#endif
    }

    bool valid() const {
        return valid_;
    }

private:
#ifdef _WIN32
    WSADATA data_{};
#endif
    bool valid_ = false;
};

class ServerInstanceGuard {
public:
    ServerInstanceGuard() {
#ifdef _WIN32
        handle_ = CreateMutexA(nullptr, FALSE, "Global\\codex_lan_agent_server_instance");
        if (handle_ == nullptr) {
            acquired_ = false;
            error_message_ = "failed to create server mutex";
            return;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            acquired_ = false;
            error_message_ = "another codex_lan_agent server instance is already running";
            return;
        }
#else
        const std::string lock_path = GetServerLockFilePath();
        lock_fd_ = open(lock_path.c_str(), O_CREAT | O_RDWR, 0644);
        if (lock_fd_ < 0) {
            acquired_ = false;
            error_message_ = "failed to open server lock file";
            return;
        }
        if (flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
            acquired_ = false;
            error_message_ = "another codex_lan_agent server instance is already running";
            return;
        }
#endif
        acquired_ = true;
    }

    ~ServerInstanceGuard() {
#ifdef _WIN32
        if (handle_ != nullptr) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
#else
        if (lock_fd_ >= 0) {
            flock(lock_fd_, LOCK_UN);
            close(lock_fd_);
            lock_fd_ = -1;
        }
#endif
    }

    bool acquired() const {
        return acquired_;
    }

    const std::string & error_message() const {
        return error_message_;
    }

private:
#ifdef _WIN32
    HANDLE handle_ = nullptr;
#else
    int lock_fd_ = -1;
#endif
    bool acquired_ = false;
    std::string error_message_;
};

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
constexpr int kSocketErrorResult = SOCKET_ERROR;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
constexpr int kSocketErrorResult = -1;
#endif

int CloseSocketPortable(SocketHandle socket_handle) {
#ifdef _WIN32
    return closesocket(socket_handle);
#else
    return close(socket_handle);
#endif
}

std::string GetServerLockFilePath() {
#ifdef _WIN32
    return std::string();
#else
    const char * runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir != nullptr && runtime_dir[0] != '\0') {
        return std::string(runtime_dir) + "/codex_lan_agent_server_instance.lock";
    }
    const char * tmp_dir = std::getenv("TMPDIR");
    if (tmp_dir != nullptr && tmp_dir[0] != '\0') {
        return std::string(tmp_dir) + "/codex_lan_agent_server_instance.lock";
    }
    return "/tmp/codex_lan_agent_server_instance.lock";
#endif
}

std::string CurrentPlatformName() {
#ifdef _WIN32
    return "windows";
#else
    return "linux";
#endif
}

void IgnoreBrokenPipePortable() {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
}

std::string GetHostNamePortable() {
#ifdef _WIN32
    char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = static_cast<DWORD>(sizeof(buffer));
    if (GetComputerNameA(buffer, &size) != 0 && buffer[0] != '\0') {
        return std::string(buffer, size);
    }
    const char * env_name = std::getenv("COMPUTERNAME");
    if (env_name != nullptr && env_name[0] != '\0') {
        return env_name;
    }
#else
    char buffer[256] = {};
    if (gethostname(buffer, sizeof(buffer) - 1) == 0 && buffer[0] != '\0') {
        return buffer;
    }
    const char * env_name = std::getenv("HOSTNAME");
    if (env_name != nullptr && env_name[0] != '\0') {
        return env_name;
    }
#endif
    return "unknown-host";
}

std::string ReadTextFileTrimmed(const std::string & path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::string();
    }
    std::ostringstream content;
    content << input.rdbuf();
    return Trim(content.str());
}

std::string GetStableMachineId() {
#ifdef _WIN32
    char buffer[256] = {};
    DWORD buffer_size = static_cast<DWORD>(sizeof(buffer));
    const LONG result = RegGetValueA(
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Cryptography",
        "MachineGuid",
        RRF_RT_REG_SZ,
        nullptr,
        buffer,
        &buffer_size);
    if (result == ERROR_SUCCESS && buffer[0] != '\0') {
        return Trim(buffer);
    }
    const char * fallback = std::getenv("COMPUTERNAME");
    if (fallback != nullptr && fallback[0] != '\0') {
        return fallback;
    }
#else
    std::string machine_id = ReadTextFileTrimmed("/etc/machine-id");
    if (!machine_id.empty()) {
        return machine_id;
    }
    machine_id = ReadTextFileTrimmed("/var/lib/dbus/machine-id");
    if (!machine_id.empty()) {
        return machine_id;
    }
#endif
    return "unknown-machine-id";
}

std::string BuildRemoteMachineCodeSource() {
    return CurrentPlatformName()
        + "|" + GetHostNamePortable()
        + "|" + GetStableMachineId();
}

std::uint64_t Fnv1a64(const std::string & text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string FormatMachineCode(std::uint64_t value) {
    std::ostringstream hex_stream;
    hex_stream << std::uppercase << std::hex << std::setfill('0') << std::setw(16) << value;
    const std::string hex_value = hex_stream.str();
    return hex_value.substr(0, 4) + "-"
        + hex_value.substr(4, 4) + "-"
        + hex_value.substr(8, 4) + "-"
        + hex_value.substr(12, 4);
}

std::string BuildRemoteMachineCode() {
    return FormatMachineCode(Fnv1a64(BuildRemoteMachineCodeSource()));
}

bool ValidateRemoteMachineCode(
    const std::string & expected_code,
    std::string * error_message) {
    const std::string actual_code = BuildRemoteMachineCode();

    if (expected_code.empty()) {
        if (error_message != nullptr) {
            *error_message = "remote machine code is not provided; actual_code=" + actual_code;
        }
        return false;
    }

    if (actual_code != expected_code) {
        if (error_message != nullptr) {
            *error_message = "remote machine code mismatch; expected_code="
                + expected_code + " actual_code=" + actual_code;
        }
        return false;
    }

    return true;
}

int SetReuseAddrPortable(SocketHandle socket_handle, int reuse_value) {
#ifdef _WIN32
    return setsockopt(
        socket_handle,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char *>(&reuse_value),
        static_cast<int>(sizeof(reuse_value)));
#else
    return setsockopt(
        socket_handle,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse_value,
        static_cast<socklen_t>(sizeof(reuse_value)));
#endif
}

void PrintUsage() {
    std::cout
        << "codex_lan_agent --config <path> <command> [args]\n"
        << "commands:\n"
        << "  health\n"
        << "  list-profiles\n"
        << "  run-cli-profile <profile> [args]\n"
        << "  run-case <case-path>\n"
        << "  run-rag-flow <query> [mode]\n"
        << "  serve --machine-code <code>  (exposes async enqueue/task endpoints over HTTP/MCP)\n"
        ;
}

std::string TimeStampForFileName() {
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
        "%04d%02d%02d_%02d%02d%02d",
        local_tm.tm_year + 1900,
        local_tm.tm_mon + 1,
        local_tm.tm_mday,
        local_tm.tm_hour,
        local_tm.tm_min,
        local_tm.tm_sec);
    return buffer;
}

std::string IsoTimestampNow() {
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
        "%04d-%02d-%02dT%02d:%02d:%02d+08:00",
        local_tm.tm_year + 1900,
        local_tm.tm_mon + 1,
        local_tm.tm_mday,
        local_tm.tm_hour,
        local_tm.tm_min,
        local_tm.tm_sec);
    return buffer;
}

std::string Trim(const std::string & value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

bool FindArgument(
    const std::vector<std::string> & arguments,
    const std::string & name,
    std::string * value) {
    for (std::size_t index = 0; index + 1 < arguments.size(); ++index) {
        if (arguments[index] == name) {
            *value = arguments[index + 1];
            return true;
        }
    }
    return false;
}

std::string JoinRemainingArguments(
    const std::vector<std::string> & arguments,
    std::size_t start_index) {
    std::string joined;
    for (std::size_t index = start_index; index < arguments.size(); ++index) {
        if (!joined.empty()) {
            joined += " ";
        }
        const std::string & argument = arguments[index];
        const bool needs_quotes = argument.find(' ') != std::string::npos;
        if (needs_quotes) {
            joined += "\"";
            joined += argument;
            joined += "\"";
        } else {
            joined += argument;
        }
    }
    return joined;
}

std::string BuildLogPath(
    const AgentConfig & config,
    const std::string & prefix) {
    return codex_lan_agent::JoinPath(
        config.log_root,
        prefix + "_" + TimeStampForFileName() + ".log");
}

std::string BuildServerStatePath(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "agent_server_state.json");
}

void WriteServerStateFile(
    const AgentConfig & config,
    const std::string & status,
    const std::string & detail = "") {
    std::filesystem::create_directories(config.log_root);
    std::ofstream output(BuildServerStatePath(config), std::ios::out | std::ios::trunc);
    output << "{"
           << "\"status\":\"" << codex_lan_agent::JsonEscape(status) << "\","
           << "\"listen_host\":\"" << codex_lan_agent::JsonEscape(config.listen_host) << "\","
           << "\"listen_port\":\"" << config.listen_port << "\","
           << "\"workspace_root\":\"" << codex_lan_agent::JsonEscape(config.workspace_root) << "\","
           << "\"detail\":\"" << codex_lan_agent::JsonEscape(detail) << "\""
           << "}";
}

void LogServerEvent(
    const AgentConfig & config,
    const std::string & event,
    const std::string & detail = "") {
    std::ostringstream output;
    output << "[" << TimeStampForFileName() << "] server_event=" << event;
    if (!detail.empty()) {
        output << " detail=\"" << detail << "\"";
    }
    std::cerr << output.str() << std::endl;
    (void)config;
}

int GetLastSocketErrorCode() {
#ifdef _WIN32
    return static_cast<int>(WSAGetLastError());
#else
    return errno;
#endif
}

std::string DescribeSocketErrorCode(int error_code) {
#ifdef _WIN32
    std::ostringstream output;
    output << "winsock_error=" << error_code;
    return output.str();
#else
    std::ostringstream output;
    output << "errno=" << error_code;
    const char * text = std::strerror(error_code);
    if (text != nullptr && text[0] != '\0') {
        output << " (" << text << ")";
    }
    return output.str();
#endif
}

bool StartsWithPath(
    const std::filesystem::path & path,
    const std::filesystem::path & prefix) {
    const std::string path_text = std::filesystem::weakly_canonical(path).string();
    const std::string prefix_text = std::filesystem::weakly_canonical(prefix).string();
#ifdef _WIN32
    if (path_text.size() < prefix_text.size()) {
        return false;
    }
    return _strnicmp(path_text.c_str(), prefix_text.c_str(), prefix_text.size()) == 0;
#else
    return path_text.rfind(prefix_text, 0) == 0;
#endif
}

bool TryResolveAllowedPath(
    const AgentConfig & config,
    const std::string & raw_path,
    std::filesystem::path * normalized_path,
    std::string * error_message) {
    if (raw_path.empty()) {
        if (error_message) {
            *error_message = "file_path is required";
        }
        return false;
    }

    std::filesystem::path requested(raw_path);
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(requested, ec);
    if (ec) {
        if (error_message) {
            *error_message = "failed to normalize path";
        }
        return false;
    }

    const std::filesystem::path logs_root = std::filesystem::path(config.log_root);
    const std::filesystem::path workspace_root = std::filesystem::path(config.workspace_root);
    if (!StartsWithPath(normalized, logs_root) && !StartsWithPath(normalized, workspace_root)) {
        if (error_message) {
            *error_message = "path is outside allowed roots";
        }
        return false;
    }

    *normalized_path = normalized;
    return true;
}

bool TryResolveWorkspaceFilePath(
    const AgentConfig & config,
    const std::string & raw_path,
    std::filesystem::path * normalized_path,
    std::string * error_message) {
    if (raw_path.empty()) {
        if (error_message) {
            *error_message = "file_path is required";
        }
        return false;
    }

    std::filesystem::path requested(raw_path);
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(requested, ec);
    if (ec) {
        if (error_message) {
            *error_message = "failed to normalize path";
        }
        return false;
    }
    const std::filesystem::path workspace_root = std::filesystem::path(config.workspace_root);
    if (!StartsWithPath(normalized, workspace_root)) {
        if (error_message) {
            *error_message = "path is outside workspace_root";
        }
        return false;
    }
    if (!std::filesystem::is_regular_file(normalized, ec) || ec) {
        if (error_message) {
            *error_message = "target is not a regular file";
        }
        return false;
    }
    const std::string filename = ToLowerAscii(normalized.filename().string());
    if (filename == "codex_lan_agent.exe") {
        if (error_message) {
            *error_message = "refusing to modify running agent executable";
        }
        return false;
    }
    *normalized_path = normalized;
    return true;
}

CommandResult RunCliProfile(
    const AgentConfig & config,
    const std::string & profile_name,
    const std::string & extra_arguments,
    const std::string & forced_log_path = std::string());

CommandResult RunCase(
    const AgentConfig & config,
    const std::string & case_path);

CommandResult RunRagFlow(
    const AgentConfig & config,
    const std::string & query,
    const std::string & mode);

CommandResult RunLocalChat(
    const AgentConfig & config,
    const std::string & scope,
    const std::string & question,
    const std::string & mode,
    int timeout_ms = 30000);

CommandResult TailTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    int max_lines);

CommandResult BuildHealthResult(const AgentConfig & config);

CommandResult BuildLivenessResult(const AgentConfig & config);

CommandResult BuildRuntimeOverviewResult(const AgentConfig & config);

std::string ComputeCommandOutcome(const CommandResult & result);

CommandResult DiscoverLogsResult(
    const AgentConfig & config,
    int max_entries,
    int tail_lines);

CommandResult SnapshotDiffResult(
    const AgentConfig & config,
    const std::string & repo_root = std::string(),
    int timeout_sec = 30);

std::string ResultToJson(const CommandResult & result);

CommandResult BuildQueuedTaskResult(const std::string & task_id);

CommandResult BuildTargetDryRunResult(
    const std::string & build_dir,
    const std::string & target,
    const std::string & config_name);

CommandResult BuildLlamaObserverSmokeResult(
    const AgentConfig & config,
    bool probe,
    const std::string & question);

CommandResult OptFileReadResult(
    const AgentConfig & config,
    const std::string & target_name,
    int max_bytes);

CommandResult OptFileWritePreviewResult(
    const AgentConfig & config,
    const std::string & target_name,
    const std::string & data,
    bool append);

CommandResult OptFileApplyWriteResult(
    const AgentConfig & config,
    const std::string & target_name,
    const std::string & data,
    bool append);

CommandResult RecordDialogSliceResult(
    const AgentConfig & config,
    const std::string & session_id,
    const std::string & turn_id,
    const std::string & user_text,
    const std::string & assistant_text,
    const std::string & tags);

CommandResult AnalyzeDialogSlicesResult(
    const AgentConfig & config,
    const std::string & session_id,
    int max_entries);

std::string BuildDialogSlicesDir(const AgentConfig & config);

std::filesystem::path BuildDialogSlicePath(
    const AgentConfig & config,
    const std::string & session_id);

CommandResult BuildDispatchContractMapResult(const std::string & table_name);

CommandResult BuildIntentDispatchPrepareResult(
    const AgentConfig & config,
    const std::string & task_state,
    const std::string & reasoning_level,
    const std::string & primary_intent,
    const std::string & secondary_intents,
    const std::string & intent_confidence_raw,
    const std::string & association_scope,
    const std::string & entity_refs,
    const std::string & evidence_refs,
    const std::string & risk_flags,
    const std::string & desired_next_action,
    const std::string & session_id,
    const std::string & turn_id,
    const std::string & slice_summary,
    const std::string & expression_keys,
    const std::string & summary,
    bool insufficient_context,
    const std::string & query,
    const std::string & arguments_text);

CommandResult BuildSemanticExecutionCardResult(
    const std::string & thread_name,
    const std::string & session_title,
    const std::string & task_id,
    const std::string & task_state,
    const std::string & reasoning_level,
    const std::string & primary_intent,
    const std::string & secondary_intents,
    const std::string & scope_modules,
    const std::string & expected_output,
    const std::string & evidence_required,
    const std::string & writeback_required,
    const std::string & next_action_if_blocked);

CommandResult AllocateRemoteChatSessionResult(
    const AgentConfig & config,
    const std::string & thread_name,
    const std::string & module_name,
    const std::string & reasoning_level,
    const std::string & task_state,
    const std::string & short_goal,
    const std::string & task_id,
    const std::string & requested_session_title,
    const std::string & parent_session_id,
    const std::string & dispatch_mode);

std::string ExpectedMarkerForProfile(const std::string & profile_name);

std::string AnalyzeSemanticOutcome(
    const std::string & profile_name,
    const CommandResult & result,
    const std::string & log_content);

std::string ExtractJsonString(
    const std::string & body,
    const std::string & key);

std::string ExtractJsonRawValue(
    const std::string & body,
    const std::string & key);

std::string GetQueryParamValue(
    const HttpRequest & request,
    const std::string & key);

std::string ExtractTaskIdFromPath(const std::string & path);

bool IsTaskBoardPath(const std::string & path);

std::string BuildTaskBoardEndpoint(const std::string & path);

std::string GetFieldOrDefault(
    const CommandResult & result,
    const std::string & key,
    const std::string & default_value);

TaskManager * g_task_manager = nullptr;
std::mutex g_resource_lock_mutex;
std::unordered_set<std::string> g_active_resource_keys;
std::mutex g_remote_control_event_mutex;
std::unordered_map<std::string, std::string> g_last_remote_control_event;

std::string BuildRemoteControlEventsPath(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "remote_control_events.jsonl");
}

std::string BuildExperienceCardsPath(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "experience_cards.jsonl");
}

std::unordered_map<std::string, std::string> SnapshotLastRemoteControlEvent() {
    std::lock_guard<std::mutex> lock(g_remote_control_event_mutex);
    return g_last_remote_control_event;
}

std::string ExtractCliNamedArgument(
    const std::string & arguments,
    const std::string & key) {
    const std::string marker = key + " ";
    const std::size_t start = arguments.find(marker);
    if (start == std::string::npos) {
        return std::string();
    }

    std::size_t value_pos = start + marker.size();
    while (value_pos < arguments.size() &&
           std::isspace(static_cast<unsigned char>(arguments[value_pos])) != 0) {
        ++value_pos;
    }

    if (value_pos >= arguments.size()) {
        return std::string();
    }

    if (arguments[value_pos] == '"') {
        ++value_pos;
        const std::size_t quote_end = arguments.find('"', value_pos);
        if (quote_end == std::string::npos) {
            return arguments.substr(value_pos);
        }
        return arguments.substr(value_pos, quote_end - value_pos);
    }

    std::size_t value_end = value_pos;
    while (value_end < arguments.size() &&
           std::isspace(static_cast<unsigned char>(arguments[value_end])) == 0) {
        ++value_end;
    }
    return arguments.substr(value_pos, value_end - value_pos);
}

std::string BuildTaskResourceKey(
    const std::string & profile_name,
    const std::string & extra_arguments) {
    if (profile_name == "prepare_build_dir" ||
        profile_name == "check_build_dir" ||
        profile_name == "configure_project" ||
        profile_name == "build_target" ||
        profile_name == "build_and_test" ||
        profile_name == "run_ctest_target") {
        const std::string build_dir = ExtractCliNamedArgument(extra_arguments, "-BuildDir");
        if (!build_dir.empty()) {
            return "builddir:" + build_dir;
        }
    }
    return std::string();
}

class ScopedResourceLock {
public:
    explicit ScopedResourceLock(const std::string & resource_key)
        : resource_key_(resource_key) {
        if (resource_key_.empty()) {
            acquired_ = true;
            return;
        }

        std::lock_guard<std::mutex> lock(g_resource_lock_mutex);
        if (g_active_resource_keys.find(resource_key_) != g_active_resource_keys.end()) {
            acquired_ = false;
            return;
        }
        g_active_resource_keys.insert(resource_key_);
        acquired_ = true;
    }

    ~ScopedResourceLock() {
        if (!acquired_ || resource_key_.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(g_resource_lock_mutex);
        g_active_resource_keys.erase(resource_key_);
    }

    bool acquired() const {
        return acquired_;
    }

private:
    std::string resource_key_;
    bool acquired_ = false;
};

std::vector<std::string> SnapshotActiveResourceKeys() {
    std::lock_guard<std::mutex> lock(g_resource_lock_mutex);
    return std::vector<std::string>(g_active_resource_keys.begin(), g_active_resource_keys.end());
}

CommandResult ReadTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    int max_lines = 200) {
    CommandResult result;
    result.fields["file_path"] = file_path;

    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        return result;
    }

    std::filesystem::path requested(file_path);
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, requested.string(), &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        return result;
    }

    std::ifstream input(normalized);
    if (!input.is_open()) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = "failed to open file";
        return result;
    }

    std::ostringstream content;
    std::string line;
    int line_count = 0;
    while (line_count < max_lines && std::getline(input, line)) {
        content << line << "\n";
        ++line_count;
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["line_count"] = std::to_string(line_count);
    result.fields["content"] = content.str();
    return result;
}

CommandResult TailTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    int max_lines = 120) {
    CommandResult result;
    result.fields["file_path"] = file_path;

    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 24;
        result.fields["error"] = "file_path is required";
        return result;
    }

    std::filesystem::path requested(file_path);
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, requested.string(), &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 25;
        result.fields["error"] = path_error;
        return result;
    }

    std::ifstream input(normalized);
    if (!input.is_open()) {
        result.ok = false;
        result.exit_code = 27;
        result.fields["error"] = "failed to open file";
        return result;
    }

    std::deque<std::string> tail_lines;
    std::string line;
    int total_lines = 0;
    const int bounded_max_lines = max_lines > 0 ? max_lines : 1;
    while (std::getline(input, line)) {
        if (static_cast<int>(tail_lines.size()) >= bounded_max_lines) {
            tail_lines.pop_front();
        }
        tail_lines.push_back(line);
        ++total_lines;
    }

    std::ostringstream content;
    for (const std::string & tail_line : tail_lines) {
        content << tail_line << "\n";
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["line_count"] = std::to_string(static_cast<int>(tail_lines.size()));
    result.fields["total_lines"] = std::to_string(total_lines);
    result.fields["content"] = content.str();
    return result;
}

CommandResult TaskLogTailResult(
    const AgentConfig & config,
    const std::string & task_id,
    int max_lines) {
    if (g_task_manager == nullptr) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "task manager is not active";
        return result;
    }

    const CommandResult task_result = g_task_manager->GetTaskResult(task_id);
    const auto result_log_it = task_result.fields.find("result_log_path");
    const auto live_log_it = task_result.fields.find("log_path");
    const std::string resolved_log_path =
        (result_log_it != task_result.fields.end() && !result_log_it->second.empty())
            ? result_log_it->second
            : ((live_log_it != task_result.fields.end() && !live_log_it->second.empty())
                ? live_log_it->second
                : std::string());
    if (resolved_log_path.empty()) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 42;
        result.fields["task_id"] = task_id;
        result.fields["error"] = "task has no log_path";
        return result;
    }

    CommandResult result = TailTextFileResult(config, resolved_log_path, max_lines);
    result.fields["task_id"] = task_id;
    result.fields["log_path"] = resolved_log_path;
    const std::string profile_name = GetFieldOrDefault(task_result, "arg1", "");
    result.fields["expected_marker"] = ExpectedMarkerForProfile(profile_name);
    const std::string semantic_outcome = AnalyzeSemanticOutcome(
        profile_name,
        task_result,
        GetFieldOrDefault(result, "content", ""));
    result.fields["semantic_outcome"] = semantic_outcome;
    result.fields["expected_marker_verified"] = GetFieldOrDefault(
        task_result,
        "result_expected_marker_verified",
        GetFieldOrDefault(task_result, "expected_marker_verified", ""));
    return result;
}

std::vector<std::string> SplitLinesPreserveText(const std::string & text) {
    std::vector<std::string> lines;
    std::string current;
    for (char ch : text) {
        if (ch == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty() || (!text.empty() && text.back() == '\n')) {
        lines.push_back(current);
    }
    return lines;
}

std::string BuildSimpleUnifiedDiff(
    const std::string & file_path,
    const std::string & old_content,
    const std::string & new_content) {
    const std::vector<std::string> old_lines = SplitLinesPreserveText(old_content);
    const std::vector<std::string> new_lines = SplitLinesPreserveText(new_content);
    std::ostringstream diff;
    diff << "--- " << file_path << "\n";
    diff << "+++ " << file_path << "\n";
    diff << "@@\n";
    const std::size_t max_lines = std::max(old_lines.size(), new_lines.size());
    for (std::size_t index = 0; index < max_lines; ++index) {
        const bool has_old = index < old_lines.size();
        const bool has_new = index < new_lines.size();
        if (has_old && has_new && old_lines[index] == new_lines[index]) {
            diff << " " << old_lines[index] << "\n";
            continue;
        }
        if (has_old) {
            diff << "-" << old_lines[index] << "\n";
        }
        if (has_new) {
            diff << "+" << new_lines[index] << "\n";
        }
    }
    return diff.str();
}

bool ReadWholeFile(
    const std::filesystem::path & path,
    std::string * content,
    std::string * error_message) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error_message) {
            *error_message = "failed to open file";
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    *content = buffer.str();
    return true;
}

bool TextContainsCaseInsensitive(const std::string & text, const std::string & needle) {
    return ToLowerAscii(text).find(ToLowerAscii(needle)) != std::string::npos;
}

std::string ExpectedMarkerForProfile(const std::string & profile_name) {
    if (profile_name == "configure_project") {
        return "cmake_configure_complete";
    }
    if (profile_name == "run_ctest_target") {
        return "ctest_tests_found_and_passed";
    }
    if (profile_name == "prepare_build_dir") {
        return "prepare_build_dir_done=true";
    }
    if (profile_name == "build_target") {
        return "build_exit_code_0";
    }
    if (profile_name == "check_build_dir") {
        return "check_build_dir_done";
    }
    return profile_name.empty() ? "exit_code_0" : (profile_name + "_exit_code_0");
}

std::string AnalyzeSemanticOutcome(
    const std::string & profile_name,
    const CommandResult & result,
    const std::string & log_content) {
    const bool has_nonzero_exit_code =
        TextContainsCaseInsensitive(log_content, "exit_code=") &&
        !TextContainsCaseInsensitive(log_content, "exit_code=0");
    if (TextContainsCaseInsensitive(log_content, "fatal:") ||
        TextContainsCaseInsensitive(log_content, "fatal error") ||
        TextContainsCaseInsensitive(log_content, "error:") ||
        TextContainsCaseInsensitive(log_content, "exception") ||
        TextContainsCaseInsensitive(log_content, "exit_code=1") ||
        TextContainsCaseInsensitive(log_content, "exit code 1") ||
        has_nonzero_exit_code ||
        TextContainsCaseInsensitive(log_content, "nonzero exit") ||
        TextContainsCaseInsensitive(log_content, "non-zero exit")) {
        return "failure";
    }
    if (TextContainsCaseInsensitive(log_content, "CMake Error")) {
        return "cmake_error";
    }
    if (TextContainsCaseInsensitive(log_content, "No tests were found")) {
        return "ctest_no_tests_found";
    }
    if (TextContainsCaseInsensitive(log_content, "The following tests FAILED")) {
        return "ctest_tests_failed";
    }
    if (profile_name == "run_ctest_target" &&
        TextContainsCaseInsensitive(log_content, "100% tests passed")) {
        return "ctest_tests_passed";
    }
    if (profile_name == "configure_project" &&
        TextContainsCaseInsensitive(log_content, "Configuring incomplete, errors occurred")) {
        return "cmake_configure_incomplete";
    }
    if (profile_name == "prepare_build_dir" &&
        TextContainsCaseInsensitive(log_content, "prepare_build_dir_done=true")) {
        return "prepare_build_dir_ready";
    }
    return result.ok && result.exit_code == 0 ? "succeeded" : "failed";
}

std::string ExtractOutputTextFallback(const CommandResult & result) {
    std::string output_text = GetFieldOrDefault(result, "output_text", "");
    if (!output_text.empty()) {
        return output_text;
    }
    output_text = ExtractJsonString(GetFieldOrDefault(result, "body", ""), "output_text");
    if (!output_text.empty()) {
        return output_text;
    }
    output_text = ExtractJsonString(GetFieldOrDefault(result, "body", ""), "response");
    if (!output_text.empty()) {
        return output_text;
    }
    output_text = ExtractJsonString(GetFieldOrDefault(result, "body", ""), "content");
    if (!output_text.empty()) {
        return output_text;
    }
    output_text = GetFieldOrDefault(result, "body", "");
    if (output_text.size() > 4000) {
        output_text = output_text.substr(0, 4000);
    }
    return output_text;
}

bool LooksLikeFileScope(const AgentConfig & config, const std::string & scope, std::filesystem::path * path) {
    if (scope.empty()) {
        return false;
    }
    std::filesystem::path candidate(scope);
    if (candidate.is_relative()) {
        candidate = std::filesystem::path(config.workspace_root) / candidate;
    }
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, ec);
    if (ec || !std::filesystem::is_regular_file(normalized, ec) || ec) {
        return false;
    }
    const std::filesystem::path workspace_root(config.workspace_root);
    if (!StartsWithPath(normalized, workspace_root)) {
        return false;
    }
    *path = normalized;
    return true;
}

void AddRagEvidenceFields(
    CommandResult * result,
    const std::string & source_ref,
    const std::string & evidence_lines,
    bool insufficient_context,
    const std::string & confidence) {
    result->fields["output_text"] = ExtractOutputTextFallback(*result);
    result->fields["source_refs"] = source_ref;
    result->fields["evidence_lines"] = evidence_lines;
    result->fields["confidence"] = confidence;
    result->fields["insufficient_context"] = insufficient_context ? "true" : "false";
    if (insufficient_context) {
        result->fields["semantic_outcome"] = "insufficient_context";
        result->fields["next_action"] = "provide file scope, log path, or diff text";
    }
}

CommandResult RagLogClassifyResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & task_id,
    const std::string & log_text) {
    CommandResult result;
    std::string content = log_text;
    std::string source_ref = "inline_log_text";
    if (content.empty() && !task_id.empty()) {
        CommandResult tail = TaskLogTailResult(config, task_id, 120);
        content = GetFieldOrDefault(tail, "content", "");
        source_ref = "task-log(" + task_id + ")";
        result.fields["task_id"] = task_id;
        result.fields["log_path"] = GetFieldOrDefault(tail, "log_path", "");
    }
    if (content.empty() && !file_path.empty()) {
        CommandResult tail = TailTextFileResult(config, file_path, 120);
        content = GetFieldOrDefault(tail, "content", "");
        source_ref = file_path;
        result.fields["log_path"] = file_path;
    }
    const bool insufficient = content.empty();
    result.ok = !insufficient;
    result.exit_code = insufficient ? 60 : 0;
    result.fields["semantic_outcome"] = insufficient
        ? "insufficient_context"
        : AnalyzeSemanticOutcome("", result, content);
    if (result.fields["semantic_outcome"] == "failure") {
        result.ok = false;
        result.exit_code = 62;
    }
    result.fields["output_text"] = result.fields["semantic_outcome"];
    result.fields["source_refs"] = source_ref;
    result.fields["evidence_lines"] = content.substr(0, std::min<std::size_t>(content.size(), 2000));
    result.fields["confidence"] = insufficient ? "low" : "high";
    result.fields["risk_level"] =
        result.fields["semantic_outcome"] == "failure" ? "high" : (insufficient ? "medium" : "low");
    result.fields["insufficient_context"] = insufficient ? "true" : "false";
    result.fields["summary"] = insufficient ? "no log content available" : result.fields["semantic_outcome"];
    result.fields["next_action"] = insufficient ? "provide file_path, task_id, or log_text" : "inspect evidence_lines";
    return result;
}

CommandResult RagDiffReviewResult(
    const AgentConfig & config,
    const std::string & diff_text) {
    CommandResult result;
    std::string content = diff_text;
    std::string source_ref = "inline_diff_text";
    (void)config;
    const bool insufficient = content.empty();
    result.ok = !insufficient;
    result.exit_code = insufficient ? 61 : 0;
    result.fields["semantic_outcome"] = insufficient ? "insufficient_context" : "diff_review_ready";
    std::string output_text = "insufficient context: provide diff_text or make workspace diff readable";
    std::string confidence = "low";
    if (!insufficient) {
        output_text =
            "diff evidence captured; use evidence_lines for basic review or call rag.query for deeper analysis";
        confidence = "medium";
        result.fields["review_status_code"] = "skipped";
        result.fields["review_log_path"] = "";
        result.fields["review_error"] = "";
    }
    result.fields["output_text"] = output_text;
    result.fields["source_refs"] = source_ref;
    result.fields["evidence_lines"] = content.substr(0, std::min<std::size_t>(content.size(), 4000));
    result.fields["confidence"] = confidence;
    if (result.fields.find("insufficient_context") == result.fields.end()) {
        result.fields["insufficient_context"] = insufficient ? "true" : "false";
    }
    if (result.fields.find("risk_level") == result.fields.end()) {
        result.fields["risk_level"] = insufficient ? "medium" : "low";
    }
    if (result.fields.find("fallback") == result.fields.end()) {
        result.fields["fallback"] = insufficient
            ? "{\"tool\":\"lan_agent_snapshot_diff\",\"reason\":\"diff_text unavailable\"}"
            : "{\"tool\":\"rag.query\",\"reason\":\"deeper AI review requested\"}";
    }
    result.fields["summary"] = result.fields["output_text"];
    result.fields["next_action"] = insufficient ? "provide diff_text or fix git root" : "review evidence_lines";
    return result;
}

struct SemanticActionSpec {
    const char * action_id;
    const char * description;
    const char * tool;
    const char * arguments_schema;
    const char * success_rule;
    const char * fallback;
    const char * result_fields;
    const char * risk_level;
    const char * dry_run_supported;
    const char * side_effect;
};

const std::vector<SemanticActionSpec> & GetSemanticActionSpecs() {
    static const std::vector<SemanticActionSpec> actions = {
        {
            "check_remote_online",
            "Check whether codex_lan_agent is reachable and can answer lightweight health requests.",
            "lan_agent_health",
            "{}",
            "{\"status\":\"ok\"}",
            "{\"tool\":\"lan_agent_runtime_overview\",\"reason\":\"health unavailable\"}",
            "[\"status\",\"remote_timestamp\",\"queue_depth\",\"active_resource_lock_count\",\"last_request_time\"]",
            "low",
            "true",
            "none"
        },
        {
            "check_local_chat",
            "Check whether the local-chat gateway used by rag.query is ready.",
            "lan_agent_health",
            "{}",
            "{\"local_chat_ready\":\"true\"}",
            "{\"tool\":\"rag.basic_comm_smoke\",\"reason\":\"local chat readiness needs provider context\"}",
            "[\"local_chat_ready\",\"local_chat_endpoint\",\"local_chat_detail\"]",
            "low",
            "true",
            "none"
        },
        {
            "read_latest_log",
            "Discover recent agent logs and read the latest log tail without using the task queue.",
            "lan_agent_discover_logs",
            "{\"max_entries\":\"optional integer\",\"tail_lines\":\"optional integer\"}",
            "{\"latest_log_path\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_tail_text_file\",\"reason\":\"known log path available\"}",
            "[\"latest_log_path\",\"latest_log_name\",\"latest_log_tail\",\"log_count\",\"returned_count\"]",
            "low",
            "true",
            "none"
        },
        {
            "get_task_status",
            "Read one queued/running/completed task status by task_id.",
            "lan_agent_get_task",
            "{\"task_id\":\"required string\"}",
            "{\"status\":\"queued|running|succeeded|failed\"}",
            "{\"tool\":\"lan_agent_task_log\",\"reason\":\"task status needs log evidence\"}",
            "[\"task_id\",\"status\",\"summary\",\"next_action\",\"semantic_outcome\",\"result_log_path\",\"expected_marker\"]",
            "low",
            "true",
            "none"
        },
        {
            "run_light_command",
            "Run a known lightweight CLI profile directly; avoid for build/test workloads.",
            "lan_agent_run_cli_profile",
            "{\"profile\":\"required string\",\"args\":\"optional string\",\"safe_profile_allowlist\":[\"check_build_dir\",\"run_script\",\"run_local_chat\"]}",
            "{\"exit_code\":\"0\",\"semantic_outcome\":\"not_failed\"}",
            "{\"tool\":\"lan_agent_enqueue_cli_profile\",\"reason\":\"command may be long-running\"}",
            "[\"profile\",\"exit_code\",\"log_path\",\"semantic_outcome\",\"expected_marker\"]",
            "medium",
            "false",
            "process"
        },
        {
            "build_target",
            "Queue a build_target profile for a build directory and target.",
            "lan_agent_build_target",
            "{\"build_dir\":\"required string\",\"target\":\"required string\",\"config\":\"optional string\",\"dry_run\":\"optional boolean\",\"validate_args\":\"optional boolean\"}",
            "{\"status\":\"queued\",\"final_status\":\"succeeded\",\"expected_marker_verified\":\"true\"}",
            "{\"tool\":\"lan_agent_task_log\",\"reason\":\"build failed or marker missing\"}",
            "[\"task_id\",\"status\",\"queue_depth\",\"summary\",\"next_action\",\"resource_key\"]",
            "high",
            "true",
            "queue_build_task"
        },
        {
            "get_git_diff",
            "Create a read-only snapshot diff from remote workspace_root.",
            "lan_agent_snapshot_diff",
            "{\"repo_root\":\"optional string\"}",
            "{\"semantic_outcome\":\"snapshot_diff_ready\"}",
            "{\"tool\":\"lan_agent_preview_patch\",\"reason\":\"git root unavailable\"}",
            "[\"semantic_outcome\",\"log_path\",\"content\",\"expected_marker\"]",
            "low",
            "true",
            "none"
        },
        {
            "basic_diff_review",
            "Review a small diff or fallback to remote snapshot diff through local-chat.",
            "rag.diff_review",
            "{\"diff_text\":\"optional string\"}",
            "{\"insufficient_context\":\"false\",\"output_text\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_snapshot_diff\",\"reason\":\"diff_text unavailable\"}",
            "[\"output_text\",\"source_refs\",\"evidence_lines\",\"confidence\",\"insufficient_context\",\"review_log_path\",\"risk_level\",\"fallback\"]",
            "medium",
            "true",
            "none"
        },
        {
            "read_test_result",
            "Read and classify a task log or test log into a semantic outcome.",
            "rag.log_classify",
            "{\"task_id\":\"optional string\",\"file_path\":\"optional string\",\"log_text\":\"optional string\"}",
            "{\"insufficient_context\":\"false\",\"semantic_outcome\":\"not_insufficient_context\"}",
            "{\"tool\":\"lan_agent_task_log\",\"reason\":\"task log tail needed\"}",
            "[\"semantic_outcome\",\"evidence_lines\",\"confidence\",\"insufficient_context\",\"log_path\",\"risk_level\"]",
            "low",
            "true",
            "none"
        },
        {
            "generate_thread_report",
            "Generate a compact standard thread report from current health and event state.",
            "lan_agent_runtime_overview",
            "{}",
            "{\"status\":\"ok\"}",
            "{\"tool\":\"lan_agent_tail_control_events\",\"reason\":\"recent event evidence needed\"}",
            "[\"module\",\"remote_entry\",\"action\",\"result\",\"next_action\",\"last_request_time\",\"last_request_entry\"]",
            "low",
            "true",
            "none"
        },
        {
            "rag.basic_comm.check",
            "Check RAG basic communication status.",
            "rag.basic_comm_smoke",
            "{}",
            "{\"result\":\"pass\",\"blocking_points\":\"none\"}",
            "{\"tool\":\"lan_agent_health\",\"reason\":\"basic smoke unavailable\"}",
            "[\"module\",\"remote_entry\",\"result\",\"blocking_points\",\"next_action\"]",
            "low",
            "true",
            "none"
        },
        {
            "read_document",
            "Read a remote document or text file under workspace/log roots.",
            "lan_agent_read_text_file",
            "{\"file_path\":\"required string\",\"max_lines\":\"optional integer\"}",
            "{\"file_path\":\"non_empty\",\"content\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_list_directory\",\"reason\":\"discover file path first\"}",
            "[\"file_path\",\"content\",\"line_count\",\"normalized_path\"]",
            "low",
            "true",
            "none"
        },
        {
            "write_document",
            "Write one remote document by replacing a single file under workspace_root.",
            "lan_agent_apply_single_file_patch",
            "{\"file_path\":\"required string\",\"new_content\":\"required string\"}",
            "{\"result\":\"applied\",\"log_path\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_preview_patch\",\"reason\":\"preview file replacement first\"}",
            "[\"file_path\",\"diff\",\"log_path\",\"backup_path\",\"result\"]",
            "medium",
            "false",
            "write_file"
        },
        {
            "configure_project",
            "Queue one remote CMake configure for a project root and build directory.",
            "lan_agent_configure_project",
            "{\"project_root\":\"required string\",\"build_dir\":\"required string\",\"generator_kind\":\"optional string\",\"cmake_args\":\"optional string\",\"env\":\"optional string\"}",
            "{\"status\":\"queued|succeeded\",\"expected_marker_verified\":\"true\"}",
            "{\"tool\":\"lan_agent_task_log\",\"reason\":\"configure evidence needed\"}",
            "[\"task_id\",\"status\",\"generator_kind\",\"cmake_args\",\"env\",\"next_action\"]",
            "high",
            "true",
            "queue_configure_task"
        },
        {
            "run_project_tests",
            "Queue one remote ctest regex run for a build directory.",
            "lan_agent_run_ctest_target",
            "{\"build_dir\":\"required string\",\"test_regex\":\"required string\",\"config\":\"optional string\"}",
            "{\"status\":\"queued|succeeded\",\"semantic_outcome\":\"ctest_tests_passed\"}",
            "{\"tool\":\"lan_agent_task_log\",\"reason\":\"ctest evidence needed\"}",
            "[\"task_id\",\"status\",\"semantic_outcome\",\"expected_marker\",\"next_action\"]",
            "high",
            "false",
            "queue_test_task"
        },
        {
            "record_dialog_slice",
            "Persist one local AI dialog turn as a JSONL slice for later review and retrieval.",
            "lan_agent_record_dialog_slice",
            "{\"session_id\":\"required string\",\"turn_id\":\"required string\",\"user_text\":\"required string\",\"assistant_text\":\"required string\",\"tags\":\"optional string\"}",
            "{\"result\":\"recorded\",\"slice_path\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_tail_control_events\",\"reason\":\"verify dialog slice write event\"}",
            "[\"slice_path\",\"session_id\",\"turn_id\",\"bytes\",\"checksum\",\"result\"]",
            "low",
            "false",
            "append_slice"
        },
        {
            "analyze_dialog_slices",
            "Analyze stored dialog slices for one session or the whole dialog_slices folder.",
            "lan_agent_analyze_dialog_slices",
            "{\"session_id\":\"optional string\",\"max_entries\":\"optional integer\"}",
            "{\"slice_file_count\":\"non_zero\",\"result\":\"analyzed\"}",
            "{\"tool\":\"lan_agent_list_directory\",\"reason\":\"dialog slice folder may be empty\"}",
            "[\"analysis_root\",\"slice_file_count\",\"session_id\",\"latest_slice_path\",\"result\",\"summary\"]",
            "low",
            "true",
            "none"
        }
    };
    return actions;
}

const SemanticActionSpec * FindSemanticActionById(const std::string & action_id) {
    const std::vector<SemanticActionSpec> & actions = GetSemanticActionSpecs();
    for (const SemanticActionSpec & action : actions) {
        if (action_id == action.action_id) {
            return &action;
        }
    }
    return nullptr;
}

CommandResult BuildSemanticActionMapResult(const std::string & action_id) {
    CommandResult result;
    result.fields["schema"] =
        "action_id,description,tool,arguments_schema,success_rule,fallback,result_fields,risk_level,dry_run_supported,side_effect";
    const std::vector<SemanticActionSpec> & actions = GetSemanticActionSpecs();
    int index = 0;
    for (const SemanticActionSpec & action : actions) {
        if (!action_id.empty() && action_id != action.action_id) {
            continue;
        }
        const std::string prefix = action_id.empty()
            ? ("action_" + std::to_string(index) + "_")
            : "";
        result.fields[prefix + "action_id"] = action.action_id;
        result.fields[prefix + "description"] = action.description;
        result.fields[prefix + "tool"] = action.tool;
        result.fields[prefix + "arguments_schema"] = action.arguments_schema;
        result.fields[prefix + "success_rule"] = action.success_rule;
        result.fields[prefix + "fallback"] = action.fallback;
        result.fields[prefix + "result_fields"] = action.result_fields;
        result.fields[prefix + "risk_level"] = action.risk_level;
        result.fields[prefix + "dry_run_supported"] = action.dry_run_supported;
        result.fields[prefix + "side_effect"] = action.side_effect;
        ++index;
    }
    result.fields["action_count"] = std::to_string(index);
    if (!action_id.empty() && index == 0) {
        result.ok = false;
        result.exit_code = 44;
        result.fields["error"] = "unknown semantic action";
        result.fields["action_id"] = action_id;
    }
    return result;
}

bool SemanticActionMatchesQuery(const SemanticActionSpec & action, const std::string & query) {
    if (query.empty()) {
        return false;
    }
    const std::string lower_query = ToLowerAscii(query);
    if (lower_query.find(ToLowerAscii(action.action_id)) != std::string::npos ||
        lower_query.find(ToLowerAscii(action.tool)) != std::string::npos) {
        return true;
    }
    if ((lower_query.find("online") != std::string::npos ||
         lower_query.find("health") != std::string::npos ||
         lower_query.find("reachable") != std::string::npos) &&
        std::string(action.action_id) == "check_remote_online") {
        return true;
    }
    if ((lower_query.find("local chat") != std::string::npos ||
         lower_query.find("chat") != std::string::npos) &&
        std::string(action.action_id) == "check_local_chat") {
        return true;
    }
    if ((lower_query.find("latest log") != std::string::npos ||
         lower_query.find("recent log") != std::string::npos) &&
        std::string(action.action_id) == "read_latest_log") {
        return true;
    }
    if (((lower_query.find("read") != std::string::npos &&
          lower_query.find("document") != std::string::npos) ||
         lower_query.find("read file") != std::string::npos) &&
        std::string(action.action_id) == "read_document") {
        return true;
    }
    if (((lower_query.find("write") != std::string::npos &&
          lower_query.find("document") != std::string::npos) ||
         lower_query.find("write file") != std::string::npos) &&
        std::string(action.action_id) == "write_document") {
        return true;
    }
    if ((lower_query.find("task") != std::string::npos &&
         lower_query.find("status") != std::string::npos) &&
        std::string(action.action_id) == "get_task_status") {
        return true;
    }
    if ((lower_query.find("configure") != std::string::npos &&
         lower_query.find("project") != std::string::npos) &&
        std::string(action.action_id) == "configure_project") {
        return true;
    }
    if (lower_query.find("build") != std::string::npos &&
        std::string(action.action_id) == "build_target") {
        return true;
    }
    if ((lower_query.find("test") != std::string::npos &&
         lower_query.find("project") != std::string::npos) &&
        std::string(action.action_id) == "run_project_tests") {
        return true;
    }
    if (lower_query.find("diff") != std::string::npos &&
        lower_query.find("review") != std::string::npos &&
        std::string(action.action_id) == "basic_diff_review") {
        return true;
    }
    if (lower_query.find("diff") != std::string::npos &&
        std::string(action.action_id) == "get_git_diff") {
        return true;
    }
    if ((lower_query.find("test") != std::string::npos ||
         lower_query.find("ctest") != std::string::npos ||
         lower_query.find("log classify") != std::string::npos) &&
        std::string(action.action_id) == "read_test_result") {
        return true;
    }
    if ((lower_query.find("report") != std::string::npos ||
         lower_query.find("handoff") != std::string::npos) &&
        std::string(action.action_id) == "generate_thread_report") {
        return true;
    }
    if ((lower_query.find("rag") != std::string::npos &&
         lower_query.find("basic") != std::string::npos) &&
        std::string(action.action_id) == "rag.basic_comm.check") {
        return true;
    }
    if (lower_query.find("slice") != std::string::npos &&
        lower_query.find("dialog") != std::string::npos) {
        if (std::string(action.action_id) == "record_dialog_slice" &&
            (lower_query.find("record") != std::string::npos ||
             lower_query.find("store") != std::string::npos)) {
            return true;
        }
        if (std::string(action.action_id) == "analyze_dialog_slices" &&
            (lower_query.find("analy") != std::string::npos ||
             lower_query.find("review") != std::string::npos)) {
            return true;
        }
    }
    if (lower_query.find(ToLowerAscii(action.description)) != std::string::npos) {
        return true;
    }
    return false;
}

CommandResult BuildSemanticActionResolveResult(
    const std::string & action_id,
    const std::string & query) {
    CommandResult result;
    const std::vector<SemanticActionSpec> & actions = GetSemanticActionSpecs();
    const SemanticActionSpec * selected = nullptr;
    for (const SemanticActionSpec & action : actions) {
        if (!action_id.empty() && action_id == action.action_id) {
            selected = &action;
            break;
        }
    }
    if (selected == nullptr) {
        const std::string lower_query = ToLowerAscii(query);
        if (lower_query.find("diff") != std::string::npos &&
            lower_query.find("review") != std::string::npos) {
            selected = FindSemanticActionById("basic_diff_review");
        } else if (lower_query.find("git") != std::string::npos &&
                   lower_query.find("diff") != std::string::npos) {
            selected = FindSemanticActionById("get_git_diff");
        }
    }
    if (selected == nullptr) {
        for (const SemanticActionSpec & action : actions) {
            if (SemanticActionMatchesQuery(action, query)) {
                selected = &action;
                break;
            }
        }
    }

    result.fields["query"] = query;
    result.fields["requested_action_id"] = action_id;
    result.fields["resolver"] = "semantic_action_resolve";
    result.fields["schema"] =
        "action_id,tool,arguments_schema,result_fields,success_rule,risk_level,dry_run_supported,side_effect,next_action";
    if (selected == nullptr) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["semantic_outcome"] = "unresolved_action";
        result.fields["insufficient_context"] = "true";
        result.fields["next_action"] = "call semantic_action_map or provide action_id";
        result.fields["fallback"] = "{\"tool\":\"semantic_action_map\",\"reason\":\"no shortcut matched\"}";
        return result;
    }

    result.fields["semantic_outcome"] = "resolved";
    result.fields["insufficient_context"] = "false";
    result.fields["action_id"] = selected->action_id;
    result.fields["description"] = selected->description;
    result.fields["tool"] = selected->tool;
    result.fields["arguments_schema"] = selected->arguments_schema;
    result.fields["result_fields"] = selected->result_fields;
    result.fields["success_rule"] = selected->success_rule;
    result.fields["fallback"] = selected->fallback;
    result.fields["risk_level"] = selected->risk_level;
    result.fields["dry_run_supported"] = selected->dry_run_supported;
    result.fields["side_effect"] = selected->side_effect;
    result.fields["next_action"] = std::string("call tool ") + selected->tool + " with arguments_schema";
    return result;
}

CommandResult BuildSemanticActionValidateResult(
    const std::string & action_id,
    const std::string & arguments_text) {
    CommandResult result;
    const SemanticActionSpec * action = FindSemanticActionById(action_id);
    result.fields["action_id"] = action_id;
    result.fields["arguments_text"] = arguments_text;
    result.fields["validator"] = "semantic_action_validate";
    if (action == nullptr) {
        result.ok = false;
        result.exit_code = 47;
        result.fields["semantic_outcome"] = "unknown_action";
        result.fields["insufficient_context"] = "true";
        result.fields["error"] = "unknown semantic action";
        result.fields["fallback"] = "{\"tool\":\"semantic_action_map\",\"reason\":\"unknown action_id\"}";
        result.fields["next_action"] = "resolve or list semantic actions";
        return result;
    }

    result.fields["tool"] = action->tool;
    result.fields["arguments_schema"] = action->arguments_schema;
    result.fields["risk_level"] = action->risk_level;
    result.fields["dry_run_supported"] = action->dry_run_supported;
    result.fields["side_effect"] = action->side_effect;
    result.fields["fallback"] = action->fallback;

    std::vector<std::string> missing;
    const auto needs = [&arguments_text](const std::string & key) {
        return arguments_text.find(key) == std::string::npos;
    };
    if (action_id == "get_task_status" && needs("task_id")) {
        missing.push_back("task_id");
    } else if (action_id == "run_light_command" && needs("profile")) {
        missing.push_back("profile");
    } else if (action_id == "build_target") {
        if (needs("build_dir")) {
            missing.push_back("build_dir");
        }
        if (needs("target")) {
            missing.push_back("target");
        }
    } else if (action_id == "read_document") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
    } else if (action_id == "write_document") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("new_content")) {
            missing.push_back("new_content");
        }
    } else if (action_id == "configure_project") {
        if (needs("project_root")) {
            missing.push_back("project_root");
        }
        if (needs("build_dir")) {
            missing.push_back("build_dir");
        }
    } else if (action_id == "run_project_tests") {
        if (needs("build_dir")) {
            missing.push_back("build_dir");
        }
        if (needs("test_regex")) {
            missing.push_back("test_regex");
        }
    } else if (action_id == "record_dialog_slice") {
        if (needs("session_id")) {
            missing.push_back("session_id");
        }
        if (needs("turn_id")) {
            missing.push_back("turn_id");
        }
        if (needs("user_text")) {
            missing.push_back("user_text");
        }
        if (needs("assistant_text")) {
            missing.push_back("assistant_text");
        }
    }

    if (!missing.empty()) {
        result.ok = false;
        result.exit_code = 48;
        result.fields["semantic_outcome"] = "missing_required_args";
        result.fields["insufficient_context"] = "true";
        std::ostringstream missing_text;
        for (std::size_t index = 0; index < missing.size(); ++index) {
            if (index > 0) {
                missing_text << ",";
            }
            missing_text << missing[index];
        }
        result.fields["missing_args"] = missing_text.str();
        result.fields["next_action"] = "provide missing_args before calling tool";
        return result;
    }

    result.fields["semantic_outcome"] = "args_valid";
    result.fields["insufficient_context"] = "false";
    result.fields["safe_to_call"] =
        std::string(action->side_effect) == "none" ? "true" : "review_required";
    result.fields["recommend_dry_run"] =
        (std::string(action->dry_run_supported) == "true" &&
         std::string(action->side_effect) != "none") ? "true" : "false";
    result.fields["next_action"] =
        result.fields["recommend_dry_run"] == "true"
            ? "call tool with dry_run or validate_args first"
            : "call resolved tool";
    return result;
}

CommandResult BuildSemanticActionPrepareResult(
    const std::string & action_id,
    const std::string & query,
    const std::string & arguments_text) {
    CommandResult resolve = BuildSemanticActionResolveResult(action_id, query);
    if (!resolve.ok) {
        resolve.fields["preparer"] = "semantic_action_prepare";
        resolve.fields["arguments_text"] = arguments_text;
        return resolve;
    }

    const std::string resolved_action_id = GetFieldOrDefault(resolve, "action_id", "");
    CommandResult validate = BuildSemanticActionValidateResult(resolved_action_id, arguments_text);
    CommandResult result = resolve;
    result.fields["preparer"] = "semantic_action_prepare";
    result.fields["arguments_text"] = arguments_text;
    result.fields["validation_outcome"] = GetFieldOrDefault(validate, "semantic_outcome", "");
    result.fields["safe_to_call"] = GetFieldOrDefault(validate, "safe_to_call", "");
    result.fields["recommend_dry_run"] = GetFieldOrDefault(validate, "recommend_dry_run", "");
    result.fields["missing_args"] = GetFieldOrDefault(validate, "missing_args", "");
    result.fields["validation_next_action"] = GetFieldOrDefault(validate, "next_action", "");
    result.fields["ready_to_call"] = validate.ok ? "true" : "false";
    if (!validate.ok) {
        result.ok = false;
        result.exit_code = validate.exit_code;
        result.fields["semantic_outcome"] = "prepare_blocked";
        result.fields["insufficient_context"] = "true";
        result.fields["next_action"] = result.fields["validation_next_action"];
    }
    return result;
}

std::string ExtractArgumentTextValue(const std::string & arguments_text, const std::string & key) {
    const std::string marker = key + "=";
    const std::size_t start = arguments_text.find(marker);
    if (start == std::string::npos) {
        return std::string();
    }
    std::size_t value_start = start + marker.size();
    while (value_start < arguments_text.size() &&
           std::isspace(static_cast<unsigned char>(arguments_text[value_start])) != 0) {
        ++value_start;
    }
    if (value_start >= arguments_text.size()) {
        return std::string();
    }
    if (arguments_text[value_start] == '"') {
        ++value_start;
        const std::size_t end_quote = arguments_text.find('"', value_start);
        return end_quote == std::string::npos
            ? arguments_text.substr(value_start)
            : arguments_text.substr(value_start, end_quote - value_start);
    }
    std::size_t value_end = value_start;
    while (value_end < arguments_text.size() &&
           std::isspace(static_cast<unsigned char>(arguments_text[value_end])) == 0) {
        ++value_end;
    }
    return arguments_text.substr(value_start, value_end - value_start);
}

std::string BuildToolArgumentsJson(
    const std::string & action_id,
    const std::string & arguments_text,
    bool prefer_dry_run) {
    std::ostringstream output;
    output << "{";
    if (action_id == "build_target") {
        const std::string build_dir = ExtractArgumentTextValue(arguments_text, "build_dir");
        const std::string target = ExtractArgumentTextValue(arguments_text, "target");
        std::string config = ExtractArgumentTextValue(arguments_text, "config");
        if (config.empty()) {
            config = "Release";
        }
        output << "\"build_dir\":\"" << codex_lan_agent::JsonEscape(build_dir) << "\","
               << "\"target\":\"" << codex_lan_agent::JsonEscape(target) << "\","
               << "\"config\":\"" << codex_lan_agent::JsonEscape(config) << "\"";
        if (prefer_dry_run) {
            output << ",\"dry_run\":true";
        }
    } else if (action_id == "get_task_status") {
        output << "\"task_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "task_id"))
               << "\"";
    } else if (action_id == "run_light_command") {
        output << "\"profile\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "profile"))
               << "\",\"args\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "args"))
               << "\"";
    } else if (action_id == "basic_diff_review") {
        output << "\"diff_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "diff_text"))
               << "\"";
    } else if (action_id == "get_git_diff") {
        output << "\"repo_root\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "repo_root"))
               << "\"";
    } else if (action_id == "read_test_result") {
        output << "\"task_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "task_id"))
               << "\",\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"log_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "log_text"))
               << "\"";
    } else if (action_id == "read_document") {
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"max_lines\":200";
    } else if (action_id == "write_document") {
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"new_content\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "new_content"))
               << "\"";
    } else if (action_id == "configure_project") {
        output << "\"project_root\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "project_root"))
               << "\",\"build_dir\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "build_dir"))
               << "\",\"generator_kind\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "generator_kind"))
               << "\",\"cmake_args\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "cmake_args"))
               << "\",\"env\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "env"))
               << "\"";
    } else if (action_id == "run_project_tests") {
        output << "\"build_dir\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "build_dir"))
               << "\",\"test_regex\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "test_regex"))
               << "\",\"config\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "config"))
               << "\"";
    } else if (action_id == "record_dialog_slice") {
        output << "\"session_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "session_id"))
               << "\",\"turn_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "turn_id"))
               << "\",\"user_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "user_text"))
               << "\",\"assistant_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "assistant_text"))
               << "\",\"tags\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "tags"))
               << "\"";
    } else if (action_id == "analyze_dialog_slices") {
        output << "\"session_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "session_id"))
               << "\",\"max_entries\":20";
    }
    output << "}";
    return output.str();
}

CommandResult BuildSemanticActionToolCallResult(
    const std::string & action_id,
    const std::string & query,
    const std::string & arguments_text,
    bool prefer_dry_run) {
    CommandResult prepared = BuildSemanticActionPrepareResult(action_id, query, arguments_text);
    CommandResult result = prepared;
    result.fields["builder"] = "semantic_action_tool_call";
    if (!prepared.ok) {
        result.fields["tool_call_ready"] = "false";
        return result;
    }
    const std::string resolved_action_id = GetFieldOrDefault(prepared, "action_id", "");
    const std::string tool_name = GetFieldOrDefault(prepared, "tool", "");
    const bool use_dry_run =
        prefer_dry_run ||
        GetFieldOrDefault(prepared, "recommend_dry_run", "false") == "true";
    const std::string arguments_json =
        BuildToolArgumentsJson(resolved_action_id, arguments_text, use_dry_run);
    result.fields["tool_call_ready"] = "true";
    result.fields["tool_name"] = tool_name;
    result.fields["tool_arguments_json"] = arguments_json;
    result.fields["mcp_tool_call_json"] =
        std::string("{\"method\":\"tools/call\",\"params\":{\"name\":\"")
        + codex_lan_agent::JsonEscape(tool_name)
        + "\",\"arguments\":"
        + arguments_json
        + "}}";
    result.fields["dry_run_injected"] = use_dry_run ? "true" : "false";
    result.fields["next_action"] = "review mcp_tool_call_json then execute explicitly if intended";
    return result;
}

struct RouterDomainSpec {
    const char * domain;
    const char * intent;
    const char * tool_whitelist;
    const char * local_cli_commands;
    const char * when_to_use;
    const char * when_not_to_use;
};

const std::vector<RouterDomainSpec> & GetRouterDomainSpecs() {
    static const std::vector<RouterDomainSpec> domains = {
        {
            "knowledge_search",
            "Find knowledge, docs, or experience cards before execution.",
            "dispatch_contract_map,intent_dispatch_prepare,semantic_action_resolve",
            "log-latest",
            "Use before unfamiliar tasks or when prior failure patterns may help, and when model-side structured intent should be consumed first.",
            "Do not use for direct build or test execution without dispatch or validation."
        },
        {
            "system_ops",
            "Inspect agent health, chat status, tasks, and logs.",
            "local_cli,intent_dispatch_prepare,semantic_action_validate",
            "health,chat-status,task-latest,task,log-latest",
            "Use for status checks, task polling, and log discovery.",
            "Do not use to start build/test work."
        },
        {
            "code_ops",
            "Inspect code diff and produce basic non-executing review evidence.",
            "local_cli,rag.diff_review,semantic_action_tool_call",
            "diff",
            "Use for diff discovery, patch evidence, and basic review preflight.",
            "Do not use for writing files or applying patches."
        },
        {
            "project_ops",
            "Produce thread/project state reports and handoff summaries.",
            "dispatch_contract_map,intent_dispatch_prepare,lan_agent_tail_control_events",
            "thread-report,log-latest",
            "Use for thread report, handoff, event evidence, and frozen dispatch contract consumption.",
            "Do not use for heavy execution."
        },
        {
            "build_test_ops",
            "Validate, queue, and inspect build/test operations through agent flow.",
            "intent_dispatch_prepare,semantic_action_prepare,semantic_action_validate",
            "build-target,test-result,task,task-latest",
            "Use for build dry-run, queued build, task status, and test result classification after reasoning level is decided.",
            "Do not bypass agent queue or build-dir locks."
        }
    };
    return domains;
}

CommandResult BuildRouterDomainMapResult(const std::string & domain_filter) {
    CommandResult result;
    result.fields["schema"] =
        "domain,intent,tool_whitelist,local_cli_commands,when_to_use,when_not_to_use";
    int index = 0;
    for (const RouterDomainSpec & domain : GetRouterDomainSpecs()) {
        if (!domain_filter.empty() && domain_filter != domain.domain) {
            continue;
        }
        const std::string prefix = domain_filter.empty()
            ? ("domain_" + std::to_string(index) + "_")
            : "";
        result.fields[prefix + "domain"] = domain.domain;
        result.fields[prefix + "intent"] = domain.intent;
        result.fields[prefix + "tool_whitelist"] = domain.tool_whitelist;
        result.fields[prefix + "local_cli_commands"] = domain.local_cli_commands;
        result.fields[prefix + "when_to_use"] = domain.when_to_use;
        result.fields[prefix + "when_not_to_use"] = domain.when_not_to_use;
        ++index;
    }
    result.fields["domain_count"] = std::to_string(index);
    if (!domain_filter.empty() && index == 0) {
        result.ok = false;
        result.exit_code = 51;
        result.fields["error"] = "unknown router domain";
        result.fields["domain"] = domain_filter;
    }
    return result;
}

std::string NormalizeTaskState(const std::string & task_state, const std::string & primary_intent) {
    const std::string lowered = ToLowerAscii(task_state);
    if (!lowered.empty()) {
        if (lowered == "observe" || lowered == "clarify" || lowered == "plan" ||
            lowered == "execute_light" || lowered == "execute_heavy" ||
            lowered == "verify" || lowered == "recover") {
            return lowered;
        }
    }
    const std::string intent = ToLowerAscii(primary_intent);
    if (intent == "write_document" || intent == "build_target" ||
        intent == "configure_project" || intent == "run_project_tests") {
        return "execute_heavy";
    }
    if (intent == "read_document" || intent == "read_latest_log" ||
        intent == "get_task_status" || intent == "analyze_dialog_slices") {
        return "observe";
    }
    return "clarify";
}

std::string NormalizeReasoningLevel(const std::string & reasoning_level, const std::string & task_state) {
    const std::string lowered = ToLowerAscii(reasoning_level);
    if (lowered == "low" || lowered == "medium" || lowered == "high") {
        return lowered;
    }
    if (task_state == "observe" || task_state == "clarify") {
        return "low";
    }
    if (task_state == "plan" || task_state == "verify") {
        return "medium";
    }
    return "high";
}

std::string ReasoningLevelAllowedIndexDepth(const std::string & reasoning_level) {
    if (reasoning_level == "low") {
        return "none_or_shallow";
    }
    if (reasoning_level == "medium") {
        return "session_level";
    }
    return "session_plus_evidence";
}

std::string ReasoningLevelAllowedChainComplexity(const std::string & reasoning_level) {
    if (reasoning_level == "low") {
        return "single_step_or_read_only";
    }
    if (reasoning_level == "medium") {
        return "two_step_preflight_then_execute";
    }
    return "multi_step_validate_queue_verify";
}

std::string ResolvePrimaryIntentActionId(const std::string & primary_intent) {
    const std::string lowered = ToLowerAscii(primary_intent);
    if (FindSemanticActionById(lowered) != nullptr) {
        return lowered;
    }
    if (lowered == "health_check" || lowered == "check_remote_online") {
        return "check_remote_online";
    }
    if (lowered == "check_local_chat" || lowered == "local_chat_check") {
        return "check_local_chat";
    }
    if (lowered == "read_log" || lowered == "read_latest_log") {
        return "read_latest_log";
    }
    if (lowered == "task_status" || lowered == "get_task_status") {
        return "get_task_status";
    }
    if (lowered == "read_doc" || lowered == "read_document") {
        return "read_document";
    }
    if (lowered == "write_doc" || lowered == "write_document") {
        return "write_document";
    }
    if (lowered == "configure" || lowered == "configure_project") {
        return "configure_project";
    }
    if (lowered == "build" || lowered == "build_target") {
        return "build_target";
    }
    if (lowered == "run_tests" || lowered == "run_project_tests") {
        return "run_project_tests";
    }
    if (lowered == "diff_review" || lowered == "basic_diff_review") {
        return "basic_diff_review";
    }
    if (lowered == "test_result" || lowered == "read_test_result") {
        return "read_test_result";
    }
    if (lowered == "thread_report" || lowered == "generate_thread_report") {
        return "generate_thread_report";
    }
    if (lowered == "slice_record" || lowered == "record_dialog_slice") {
        return "record_dialog_slice";
    }
    if (lowered == "slice_analyze" || lowered == "analyze_dialog_slices") {
        return "analyze_dialog_slices";
    }
    return std::string();
}

std::string BuildDefaultActionChain(
    const std::string & reasoning_level,
    const std::string & action_id) {
    const SemanticActionSpec * action = FindSemanticActionById(action_id);
    const std::string side_effect = action == nullptr ? "unknown" : action->side_effect;
    if (reasoning_level == "low" && side_effect == "none") {
        return "task_state->intent_conclusion->tool_execute";
    }
    if (reasoning_level == "medium" || side_effect == "append_slice") {
        return "task_state->intent_conclusion->semantic_action_prepare->tool_execute";
    }
    return "task_state->reasoning_level->intent_conclusion->session_index->semantic_action_validate->semantic_action_tool_call->queued_or_guarded_execute->log_verify";
}

CommandResult BuildSemanticExecutionCardResult(
    const std::string & thread_name,
    const std::string & session_title,
    const std::string & task_id,
    const std::string & task_state,
    const std::string & reasoning_level,
    const std::string & primary_intent,
    const std::string & secondary_intents,
    const std::string & scope_modules,
    const std::string & expected_output,
    const std::string & evidence_required,
    const std::string & writeback_required,
    const std::string & next_action_if_blocked) {
    CommandResult result;
    result.fields["thread_name"] = thread_name;
    result.fields["session_title"] = session_title;
    result.fields["task_id"] = task_id;
    result.fields["task_state"] = task_state;
    result.fields["reasoning_level"] = reasoning_level;
    result.fields["primary_intent"] = primary_intent;
    result.fields["secondary_intents"] = secondary_intents;
    result.fields["scope_modules"] = scope_modules;
    result.fields["expected_output"] = expected_output;
    result.fields["evidence_required"] = evidence_required;
    result.fields["writeback_required"] = writeback_required;
    result.fields["next_action_if_blocked"] = next_action_if_blocked;

    std::ostringstream card;
    card << "{"
         << "\"thread_name\":\"" << codex_lan_agent::JsonEscape(thread_name) << "\","
         << "\"session_title\":\"" << codex_lan_agent::JsonEscape(session_title) << "\","
         << "\"task_id\":\"" << codex_lan_agent::JsonEscape(task_id) << "\","
         << "\"task_state\":\"" << codex_lan_agent::JsonEscape(task_state) << "\","
         << "\"reasoning_level\":\"" << codex_lan_agent::JsonEscape(reasoning_level) << "\","
         << "\"primary_intent\":\"" << codex_lan_agent::JsonEscape(primary_intent) << "\","
         << "\"secondary_intents\":" << (secondary_intents.empty() ? "\"\"" : secondary_intents) << ","
         << "\"scope_modules\":" << (scope_modules.empty() ? "\"\"" : scope_modules) << ","
         << "\"expected_output\":\"" << codex_lan_agent::JsonEscape(expected_output) << "\","
         << "\"evidence_required\":\"" << codex_lan_agent::JsonEscape(evidence_required) << "\","
         << "\"writeback_required\":\"" << codex_lan_agent::JsonEscape(writeback_required) << "\","
         << "\"next_action_if_blocked\":\"" << codex_lan_agent::JsonEscape(next_action_if_blocked) << "\""
         << "}";
    result.fields["semantic_execution_card"] = card.str();
    result.fields["result"] = "generated";
    return result;
}

CommandResult AllocateRemoteChatSessionResult(
    const AgentConfig & config,
    const std::string & thread_name,
    const std::string & module_name,
    const std::string & reasoning_level,
    const std::string & task_state,
    const std::string & short_goal,
    const std::string & task_id,
    const std::string & requested_session_title,
    const std::string & parent_session_id,
    const std::string & dispatch_mode) {
    CommandResult result;
    const std::filesystem::path dispatch_dir(BuildSessionDispatchDir(config));
    std::error_code ec;
    std::filesystem::create_directories(dispatch_dir, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 75;
        result.fields["error"] = "failed to create session_dispatch dir";
        return result;
    }

    const std::string normalized_module = SanitizeDispatchToken(module_name, "module");
    const std::string normalized_task_id = SanitizeDispatchToken(task_id, "task");
    const std::string remote_chat_session_id =
        "chat-" + normalized_module + "-" + normalized_task_id + "-" + TimeStampForFileName();
    const std::string session_title = requested_session_title.empty()
        ? BuildRemoteSessionTitle(module_name, reasoning_level, task_state, short_goal, task_id)
        : requested_session_title;
    const std::string session_path = BuildRemoteChatSessionsPath(config);
    const std::string evidence_ref = BuildRemoteControlEventsPath(config);

    std::ostringstream line;
    line << "{"
         << "\"remote_chat_session_id\":\"" << codex_lan_agent::JsonEscape(remote_chat_session_id) << "\","
         << "\"thread_name\":\"" << codex_lan_agent::JsonEscape(thread_name) << "\","
         << "\"module_name\":\"" << codex_lan_agent::JsonEscape(module_name) << "\","
         << "\"reasoning_level\":\"" << codex_lan_agent::JsonEscape(reasoning_level) << "\","
         << "\"task_state\":\"" << codex_lan_agent::JsonEscape(task_state) << "\","
         << "\"short_goal\":\"" << codex_lan_agent::JsonEscape(short_goal) << "\","
         << "\"task_id\":\"" << codex_lan_agent::JsonEscape(task_id) << "\","
         << "\"session_title\":\"" << codex_lan_agent::JsonEscape(session_title) << "\","
         << "\"parent_session_id\":\"" << codex_lan_agent::JsonEscape(parent_session_id) << "\","
         << "\"dispatch_mode\":\"" << codex_lan_agent::JsonEscape(dispatch_mode) << "\","
         << "\"created_at\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\""
         << "}\n";

    std::ofstream output(session_path, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 76;
        result.fields["error"] = "failed to open remote_chat_sessions.jsonl";
        return result;
    }
    output.write(line.str().data(), static_cast<std::streamsize>(line.str().size()));
    output.close();

    result.fields["remote_chat_session_id"] = remote_chat_session_id;
    result.fields["session_title"] = session_title;
    result.fields["title_template"] = "module | reasoning_level | task_state | short_goal | task_id";
    result.fields["session_registry_path"] = session_path;
    result.fields["parent_session_id"] = parent_session_id;
    result.fields["dispatch_mode"] = dispatch_mode;
    result.fields["evidence_ref"] = evidence_ref;
    result.fields["result"] = "allocated";
    return result;
}

std::string NormalizeIntentConfidenceRaw(const std::string & value) {
    std::string normalized = Trim(value);
    if (normalized.size() >= 2 &&
        normalized.front() == '"' &&
        normalized.back() == '"') {
        normalized = normalized.substr(1, normalized.size() - 2);
    }
    return normalized;
}

std::string FirstStructuredFieldString(
    const std::string & request_body,
    const std::string & key,
    const std::string & fallback) {
    const std::string value = ExtractStructuredConclusionString(request_body, key);
    return value.empty() ? fallback : value;
}

std::string FirstStructuredFieldRaw(
    const std::string & request_body,
    const std::string & key,
    const std::string & fallback) {
    const std::string value = ExtractStructuredConclusionRawValue(request_body, key);
    return value.empty() ? fallback : value;
}

CommandResult BuildDispatchContractMapResult(const std::string & table_name) {
    CommandResult result;
    result.fields["dispatch_main_path"] =
        "task_state->reasoning_level->primary_intent->session_index->action_chain->tool_execution->fallback_table";
    result.fields["compatibility_mode"] = "legacy_codex_fallback";
    result.fields["fallback_trigger_rule"] =
        "missing structured fields or low confidence or unresolved intent or index miss with insufficient evidence";
    result.fields["table_names"] =
        "task_state_table,reasoning_level_table,primary_intent_table,parameter_slot_table,session_object_table,default_action_chain_table,fallback_rule_table,model_output_contract";
    result.fields["schema"] = "table,row_key,meaning,consume_rule";
    int index = 0;
    const auto append_row =
        [&result, &index, &table_name](
            const std::string & current_table,
            const std::string & row_key,
            const std::string & fields) {
            if (!table_name.empty() && table_name != current_table) {
                return;
            }
            const std::string prefix = "row_" + std::to_string(index++) + "_";
            result.fields[prefix + "table"] = current_table;
            result.fields[prefix + "row_key"] = row_key;
            result.fields[prefix + "fields"] = fields;
        };

    append_row("task_state_table", "observe",
        "meaning=read_only_or_status_scan;default_reasoning_level=low;allowed_index_depth=none_or_shallow;allowed_chain_complexity=single_step");
    append_row("task_state_table", "clarify",
        "meaning=intent_not_final;default_reasoning_level=low;allowed_index_depth=none_or_shallow;allowed_chain_complexity=resolve_only");
    append_row("task_state_table", "plan",
        "meaning=preflight_and_contract_check;default_reasoning_level=medium;allowed_index_depth=session_level;allowed_chain_complexity=prepare_then_execute");
    append_row("task_state_table", "execute_light",
        "meaning=low_side_effect_execution;default_reasoning_level=medium;allowed_index_depth=session_level;allowed_chain_complexity=prepare_then_execute");
    append_row("task_state_table", "execute_heavy",
        "meaning=build_test_write_queue;default_reasoning_level=high;allowed_index_depth=session_plus_evidence;allowed_chain_complexity=validate_queue_verify");
    append_row("task_state_table", "verify",
        "meaning=confirm_result_and_evidence;default_reasoning_level=medium;allowed_index_depth=session_plus_evidence;allowed_chain_complexity=read_log_then_classify");
    append_row("task_state_table", "recover",
        "meaning=fall_back_after_error;default_reasoning_level=high;allowed_index_depth=session_plus_evidence;allowed_chain_complexity=fallback_then_verify");

    append_row("reasoning_level_table", "low",
        "allowed_index_depth=none_or_shallow;allowed_chain_complexity=single_step_or_read_only;when_to_use=status_or_clear_read_intent");
    append_row("reasoning_level_table", "medium",
        "allowed_index_depth=session_level;allowed_chain_complexity=two_step_preflight_then_execute;when_to_use=known_intent_with_some_context");
    append_row("reasoning_level_table", "high",
        "allowed_index_depth=session_plus_evidence;allowed_chain_complexity=multi_step_validate_queue_verify;when_to_use=write_build_test_or_recovery");

    append_row("primary_intent_table", "health_check", "mapped_action=check_remote_online;tool=lan_agent_health");
    append_row("primary_intent_table", "read_document", "mapped_action=read_document;tool=lan_agent_read_text_file");
    append_row("primary_intent_table", "write_document", "mapped_action=write_document;tool=lan_agent_apply_single_file_patch");
    append_row("primary_intent_table", "configure_project", "mapped_action=configure_project;tool=lan_agent_configure_project");
    append_row("primary_intent_table", "build_target", "mapped_action=build_target;tool=lan_agent_build_target");
    append_row("primary_intent_table", "run_project_tests", "mapped_action=run_project_tests;tool=lan_agent_run_ctest_target");
    append_row("primary_intent_table", "record_dialog_slice", "mapped_action=record_dialog_slice;tool=lan_agent_record_dialog_slice");
    append_row("primary_intent_table", "analyze_dialog_slices", "mapped_action=analyze_dialog_slices;tool=lan_agent_analyze_dialog_slices");
    append_row("primary_intent_table", "basic_diff_review", "mapped_action=basic_diff_review;tool=rag.diff_review");
    append_row("primary_intent_table", "generate_thread_report", "mapped_action=generate_thread_report;tool=lan_agent_runtime_overview");

    append_row("parameter_slot_table", "read_document", "required=file_path;optional=max_lines");
    append_row("parameter_slot_table", "write_document", "required=file_path,new_content");
    append_row("parameter_slot_table", "configure_project", "required=project_root,build_dir;optional=generator_kind,cmake_args,env");
    append_row("parameter_slot_table", "build_target", "required=build_dir,target;optional=config,dry_run,validate_args");
    append_row("parameter_slot_table", "run_project_tests", "required=build_dir,test_regex;optional=config");
    append_row("parameter_slot_table", "record_dialog_slice", "required=session_id,turn_id,user_text,assistant_text;optional=tags");

    append_row("session_object_table", "dialog_session", "key=session_id;use=group dialog slices and routing continuity");
    append_row("session_object_table", "dialog_slice", "key=session_id+turn_id;use=store one dialog turn and retrieve latest tail");
    append_row("session_object_table", "expression_key", "key=expression_keys;use=stable phrase to intent association");
    append_row("session_object_table", "execution_binding", "key=session_id+primary_intent;use=bind current chain to next verification step");

    append_row("default_action_chain_table", "low",
        "chain=task_state->intent_conclusion->tool_execute");
    append_row("default_action_chain_table", "medium",
        "chain=task_state->intent_conclusion->semantic_action_prepare->tool_execute");
    append_row("default_action_chain_table", "high",
        "chain=task_state->reasoning_level->intent_conclusion->session_index->semantic_action_validate->semantic_action_tool_call->queued_or_guarded_execute->log_verify");

    append_row("fallback_rule_table", "missing_structured_output",
        "fallback=legacy semantic_action_prepare by query and arguments_text");
    append_row("fallback_rule_table", "low_confidence",
        "threshold=intent_confidence<0.55;fallback=semantic_action_prepare");
    append_row("fallback_rule_table", "unresolved_primary_intent",
        "fallback=semantic_action_resolve then semantic_action_prepare");
    append_row("fallback_rule_table", "session_index_miss",
        "fallback=continue stateless with existing CODEX style chain");
    append_row("fallback_rule_table", "high_risk_side_effect",
        "fallback=force validate_or_dry_run_or_queue");

    append_row("model_output_contract", "required_fields",
        "task_state,reasoning_level,primary_intent,secondary_intents,intent_confidence,association_scope,entity_refs,evidence_refs,risk_flags,next_action,session_id,turn_id,slice_summary,expression_keys");

    result.fields["row_count"] = std::to_string(index);
    if (!table_name.empty() && index == 0) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = "unknown dispatch contract table";
        result.fields["table_name"] = table_name;
    }
    return result;
}

CommandResult BuildIntentDispatchPrepareResult(
    const AgentConfig & config,
    const std::string & task_state,
    const std::string & reasoning_level,
    const std::string & primary_intent,
    const std::string & secondary_intents,
    const std::string & intent_confidence_raw,
    const std::string & association_scope,
    const std::string & entity_refs,
    const std::string & evidence_refs,
    const std::string & risk_flags,
    const std::string & desired_next_action,
    const std::string & session_id,
    const std::string & turn_id,
    const std::string & slice_summary,
    const std::string & expression_keys,
    const std::string & summary,
    bool insufficient_context,
    const std::string & query,
    const std::string & arguments_text) {
    CommandResult result;
    result.fields["dispatcher"] = "intent_dispatch_prepare";
    result.fields["dispatch_mode"] = "enhanced_internal_layer";
    result.fields["compatibility_mode"] = "legacy_codex_fallback";
    result.fields["query"] = query;
    result.fields["arguments_text"] = arguments_text;
    result.fields["secondary_intents"] = secondary_intents;
    result.fields["association_scope"] = association_scope;
    result.fields["entity_refs"] = entity_refs;
    result.fields["evidence_refs"] = evidence_refs;
    result.fields["risk_flags"] = risk_flags;
    result.fields["requested_next_action"] = desired_next_action;
    result.fields["session_id"] = session_id;
    result.fields["turn_id"] = turn_id;
    result.fields["slice_summary"] = slice_summary;
    result.fields["expression_keys"] = expression_keys;
    result.fields["summary"] = summary;
    result.fields["insufficient_context"] = insufficient_context ? "true" : "false";

    const std::string normalized_intent = ResolvePrimaryIntentActionId(primary_intent);
    const std::string normalized_task_state = NormalizeTaskState(task_state, normalized_intent);
    const std::string normalized_reasoning =
        NormalizeReasoningLevel(reasoning_level, normalized_task_state);
    const std::string normalized_confidence_raw = NormalizeIntentConfidenceRaw(intent_confidence_raw);
    const double confidence = normalized_confidence_raw.empty()
        ? 0.0
        : std::atof(normalized_confidence_raw.c_str());
    const bool structured_missing =
        task_state.empty() || reasoning_level.empty() || primary_intent.empty();
    const bool low_confidence = !normalized_confidence_raw.empty() && confidence < 0.55;
    const bool unresolved_intent = normalized_intent.empty();

    result.fields["task_state"] = normalized_task_state;
    result.fields["reasoning_level"] = normalized_reasoning;
    result.fields["primary_intent"] = primary_intent;
    result.fields["resolved_action_id"] = normalized_intent;
    result.fields["intent_confidence"] = normalized_confidence_raw.empty()
        ? "0"
        : normalized_confidence_raw;
    result.fields["allowed_index_depth"] =
        ReasoningLevelAllowedIndexDepth(normalized_reasoning);
    result.fields["allowed_chain_complexity"] =
        ReasoningLevelAllowedChainComplexity(normalized_reasoning);

    std::string session_index_status = "disabled";
    if (!session_id.empty()) {
        const std::filesystem::path slice_path = BuildDialogSlicePath(config, session_id);
        session_index_status = std::filesystem::exists(slice_path) ? "hit" : "miss";
        result.fields["session_slice_path"] = slice_path.string();
    }
    result.fields["session_index_status"] = session_index_status;
    result.fields["session_binding_mode"] =
        session_index_status == "hit" ? "dialog_session+execution_binding" : "stateless_or_new_session";

    const std::string scope_modules = association_scope.empty()
        ? "[\"unknown\"]"
        : ("[\"" + codex_lan_agent::JsonEscape(association_scope) + "\"]");
    const std::string short_goal = !summary.empty()
        ? summary
        : (!desired_next_action.empty() ? desired_next_action : primary_intent);
    CommandResult session_allocation = AllocateRemoteChatSessionResult(
        config,
        "intranet_migration",
        association_scope.empty() ? "generic" : association_scope,
        normalized_reasoning,
        normalized_task_state,
        short_goal,
        session_id.empty() ? (primary_intent.empty() ? "task" : primary_intent) : session_id,
        std::string(),
        std::string(),
        normalized_reasoning == "high" ? "composite" : (normalized_reasoning == "medium" ? "advance" : "observe"));
    result.fields["remote_chat_session_id"] = GetFieldOrDefault(session_allocation, "remote_chat_session_id", "");
    result.fields["session_title"] = GetFieldOrDefault(session_allocation, "session_title", "");
    result.fields["session_registry_path"] = GetFieldOrDefault(session_allocation, "session_registry_path", "");
    result.fields["dispatch_mode"] = GetFieldOrDefault(session_allocation, "dispatch_mode", "");
    result.fields["title_template"] = GetFieldOrDefault(session_allocation, "title_template", "");

    CommandResult execution_card = BuildSemanticExecutionCardResult(
        "intranet_migration",
        result.fields["session_title"],
        session_id.empty() ? result.fields["remote_chat_session_id"] : session_id,
        normalized_task_state,
        normalized_reasoning,
        primary_intent,
        secondary_intents,
        scope_modules,
        summary.empty() ? desired_next_action : summary,
        evidence_refs.empty() ? "remote_control_events or task/log evidence" : evidence_refs,
        insufficient_context ? "blocked" : "required",
        desired_next_action.empty() ? "fallback to semantic_action_prepare" : desired_next_action);
    result.fields["semantic_execution_card"] =
        GetFieldOrDefault(execution_card, "semantic_execution_card", "");

    if (structured_missing || low_confidence || unresolved_intent || insufficient_context) {
        CommandResult fallback = BuildSemanticActionPrepareResult(
            std::string(),
            query,
            arguments_text);
        result = fallback;
        result.fields["dispatcher"] = "intent_dispatch_prepare";
        result.fields["dispatch_mode"] = "enhanced_internal_layer";
        result.fields["compatibility_mode"] = "legacy_codex_fallback";
        result.fields["task_state"] = normalized_task_state;
        result.fields["reasoning_level"] = normalized_reasoning;
        result.fields["primary_intent"] = primary_intent;
        result.fields["resolved_action_id"] = normalized_intent;
        result.fields["intent_confidence"] = normalized_confidence_raw.empty() ? "0" : normalized_confidence_raw;
        result.fields["summary"] = summary;
        result.fields["insufficient_context"] = insufficient_context ? "true" : "false";
        result.fields["allowed_index_depth"] = ReasoningLevelAllowedIndexDepth(normalized_reasoning);
        result.fields["allowed_chain_complexity"] = ReasoningLevelAllowedChainComplexity(normalized_reasoning);
        result.fields["session_index_status"] = session_index_status;
        result.fields["session_binding_mode"] =
            session_index_status == "hit" ? "dialog_session+execution_binding" : "stateless_or_new_session";
        result.fields["fallback_applied"] = "true";
        result.fields["fallback_reason"] = insufficient_context
            ? "insufficient_context"
            : (structured_missing
                ? "missing_structured_output"
                : (low_confidence ? "low_confidence" : "unresolved_primary_intent"));
        result.fields["writeback_required"] = "true";
        result.fields["remote_chat_required"] = "true";
        result.fields["dispatch_prewrite_status"] = "recorded";
        result.fields["next_action"] = GetFieldOrDefault(
            fallback,
            "next_action",
            "continue with legacy semantic_action_prepare");
        return result;
    }

    CommandResult prepared = BuildSemanticActionPrepareResult(
        normalized_intent,
        query,
        arguments_text);
    result = prepared;
    result.fields["dispatcher"] = "intent_dispatch_prepare";
    result.fields["dispatch_mode"] = "enhanced_internal_layer";
    result.fields["compatibility_mode"] = "legacy_codex_fallback";
    result.fields["task_state"] = normalized_task_state;
    result.fields["reasoning_level"] = normalized_reasoning;
    result.fields["primary_intent"] = primary_intent;
    result.fields["secondary_intents"] = secondary_intents;
    result.fields["intent_confidence"] = normalized_confidence_raw;
    result.fields["association_scope"] = association_scope;
    result.fields["entity_refs"] = entity_refs;
    result.fields["evidence_refs"] = evidence_refs;
    result.fields["risk_flags"] = risk_flags;
    result.fields["requested_next_action"] = desired_next_action;
    result.fields["session_id"] = session_id;
    result.fields["turn_id"] = turn_id;
    result.fields["slice_summary"] = slice_summary;
    result.fields["expression_keys"] = expression_keys;
    result.fields["summary"] = summary;
    result.fields["insufficient_context"] = insufficient_context ? "true" : "false";
    result.fields["allowed_index_depth"] = ReasoningLevelAllowedIndexDepth(normalized_reasoning);
    result.fields["allowed_chain_complexity"] = ReasoningLevelAllowedChainComplexity(normalized_reasoning);
    result.fields["session_index_status"] = session_index_status;
    result.fields["session_binding_mode"] =
        session_index_status == "hit" ? "dialog_session+execution_binding" : "stateless_or_new_session";
    result.fields["default_action_chain"] =
        BuildDefaultActionChain(normalized_reasoning, normalized_intent);
    result.fields["fallback_applied"] = session_index_status == "miss" ? "true" : "false";
    result.fields["fallback_reason"] =
        session_index_status == "miss" ? "session_index_miss" : "none";
    result.fields["writeback_required"] = "true";
    result.fields["remote_chat_required"] = "true";
    result.fields["dispatch_prewrite_status"] = "recorded";

    const CommandResult tool_call = BuildSemanticActionToolCallResult(
        normalized_intent,
        query,
        arguments_text,
        normalized_reasoning == "high");
    result.fields["tool_call_ready"] = GetFieldOrDefault(tool_call, "tool_call_ready", "false");
    result.fields["tool_name"] = GetFieldOrDefault(tool_call, "tool_name", GetFieldOrDefault(result, "tool", ""));
    result.fields["tool_arguments_json"] = GetFieldOrDefault(tool_call, "tool_arguments_json", "");
    result.fields["mcp_tool_call_json"] = GetFieldOrDefault(tool_call, "mcp_tool_call_json", "");
    result.fields["dry_run_injected"] = GetFieldOrDefault(tool_call, "dry_run_injected", "false");
    result.fields["local_ai_thread_message_id"] =
        "msg-" + TimeStampForFileName() + "-" + SanitizeDispatchToken(turn_id, "turn");
    result.fields["evidence_ref"] =
        GetFieldOrDefault(session_allocation, "evidence_ref", BuildRemoteControlEventsPath(config));
    result.fields["result_ref"] = GetFieldOrDefault(tool_call, "tool_name", "");
    result.fields["execution_binding"] =
        std::string("{\"remote_chat_session_id\":\"")
        + codex_lan_agent::JsonEscape(result.fields["remote_chat_session_id"])
        + "\",\"local_ai_thread_message_id\":\""
        + codex_lan_agent::JsonEscape(result.fields["local_ai_thread_message_id"])
        + "\",\"task_id\":\""
        + codex_lan_agent::JsonEscape(session_id.empty() ? result.fields["remote_chat_session_id"] : session_id)
        + "\",\"evidence_ref\":\""
        + codex_lan_agent::JsonEscape(result.fields["evidence_ref"])
        + "\",\"result_ref\":\""
        + codex_lan_agent::JsonEscape(result.fields["result_ref"])
        + "\"}";
    result.fields["next_action"] =
        normalized_reasoning == "high"
            ? "review tool_call json, prefer dry_run_or_queue, then verify by task/log"
            : GetFieldOrDefault(result, "next_action", "execute resolved tool");
    return result;
}

std::string BuildLocalCliEvidenceJson(const CommandResult & result) {
    std::ostringstream evidence;
    evidence << "{";
    bool first = true;
    const std::vector<std::string> keys = {
        "log_path",
        "latest_log_path",
        "task_id",
        "status",
        "semantic_outcome",
        "remote_timestamp",
        "local_chat_ready",
        "repo_root"
    };
    for (const std::string & key : keys) {
        const auto it = result.fields.find(key);
        if (it == result.fields.end()) {
            continue;
        }
        if (!first) {
            evidence << ",";
        }
        first = false;
        evidence << "\"" << codex_lan_agent::JsonEscape(key) << "\":\""
                 << codex_lan_agent::JsonEscape(it->second) << "\"";
    }
    evidence << "}";
    return evidence.str();
}

void AppendExperienceCard(
    const AgentConfig & config,
    const std::string & command,
    const CommandResult & result,
    const std::string & fallback) {
    std::filesystem::create_directories(config.log_root);
    std::ofstream output(BuildExperienceCardsPath(config), std::ios::out | std::ios::app);
    if (!output.is_open()) {
        return;
    }
    output
        << "{"
        << "\"task_fingerprint\":{\"command\":\"" << codex_lan_agent::JsonEscape(command) << "\"},"
        << "\"user_pattern\":[\"" << codex_lan_agent::JsonEscape(command) << "\"],"
        << "\"optimal_path\":[\"local_cli\",\"" << codex_lan_agent::JsonEscape(command) << "\"],"
        << "\"decision_rules\":[\"use local_cli before direct tool selection\"],"
        << "\"failure_patterns\":[\"" << codex_lan_agent::JsonEscape(fallback) << "\"],"
        << "\"compressed_prompt\":\"local_cli " << codex_lan_agent::JsonEscape(command) << "\","
        << "\"metrics\":{\"turns\":1,\"tool_calls\":1,\"success_rate\":" << (result.ok ? "1" : "0") << "}"
        << "}\n";
}

std::string BuildLocalCliTraceId() {
    static std::mutex mutex;
    static unsigned long long next_id = 1;
    std::lock_guard<std::mutex> lock(mutex);
    return "local_cli-" + TimeStampForFileName() + "-" + std::to_string(next_id++);
}

CommandResult BuildLocalCliEnvelope(
    const AgentConfig & config,
    const std::string & command,
    const CommandResult & payload,
    const std::string & fallback) {
    CommandResult result;
    result.ok = payload.ok;
    result.exit_code = payload.exit_code;
    result.fields["command"] = command;
    result.fields["mcp_tool"] = "local_cli";
    result.fields["mapped_cli_command"] = command;
    result.fields["execution_path"] = "AI->MCP local_cli->codex_local_cli->codex-lan-agent";
    result.fields["trace_id"] = BuildLocalCliTraceId();
    result.fields["recorded_at"] = IsoTimestampNow();
    result.fields["trace_log_path"] = BuildRemoteControlEventsPath(config);
    result.fields["result"] = ResultToJson(payload);
    result.fields["evidence"] = BuildLocalCliEvidenceJson(payload);
    result.fields["fallback"] = fallback.empty() ? "null" : fallback;
    result.fields["experience_card_path"] = BuildExperienceCardsPath(config);
    if (!payload.ok) {
        result.fields["error"] = GetFieldOrDefault(payload, "error", "local_cli command failed");
    }
    AppendExperienceCard(config, command, result, result.fields["fallback"]);
    return result;
}

CommandResult LocalCliResult(
    const AgentConfig & config,
    const std::string & command,
    const std::string & task_id,
    const std::string & repo_root,
    const std::string & action_id,
    const std::string & build_dir,
    const std::string & target,
    const std::string & config_name,
    const std::string & log_path,
    const std::string & args_text,
    bool dry_run) {
    if (command == "health") {
        return BuildLocalCliEnvelope(config, command, BuildLivenessResult(config), "null");
    }
    if (command == "chat-status") {
        CommandResult health = BuildHealthResult(config);
        CommandResult result;
        result.fields["local_chat_ready"] = GetFieldOrDefault(health, "local_chat_ready", "false");
        result.fields["local_chat_endpoint"] = GetFieldOrDefault(health, "local_chat_endpoint", "");
        result.fields["local_chat_detail"] = GetFieldOrDefault(health, "local_chat_detail", "");
        result.ok = result.fields["local_chat_ready"] == "true";
        result.exit_code = result.ok ? 0 : 50;
        if (!result.ok) {
            result.fields["error"] = "local chat is not ready";
        }
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"health\",\"reason\":\"chat status unavailable\"}");
    }
    if (command == "task-latest") {
        CommandResult result = g_task_manager == nullptr
            ? CommandResult()
            : g_task_manager->GetLatestTaskResult();
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
        }
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"log-latest\",\"reason\":\"no task available\"}");
    }
    if (command == "task") {
        CommandResult result = g_task_manager == nullptr
            ? CommandResult()
            : g_task_manager->GetTaskResult(task_id);
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
        }
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"task-latest\",\"reason\":\"task_id unavailable or not found\"}");
    }
    if (command == "log-latest") {
        return BuildLocalCliEnvelope(
            config,
            command,
            DiscoverLogsResult(config, 10, 80),
            "{\"command\":\"health\",\"reason\":\"log discovery unavailable\"}");
    }
    if (command == "diff") {
        return BuildLocalCliEnvelope(
            config,
            command,
            SnapshotDiffResult(config, repo_root),
            "{\"command\":\"log-latest\",\"reason\":\"git diff unavailable\"}");
    }
    if (command == "test-result") {
        return BuildLocalCliEnvelope(
            config,
            command,
            RagLogClassifyResult(config, log_path, task_id, args_text),
            "{\"command\":\"log-latest\",\"reason\":\"test log unavailable\"}");
    }
    if (command == "thread-report") {
        CommandResult result = BuildRuntimeOverviewResult(config);
        CommandResult events = TailTextFileResult(config, BuildRemoteControlEventsPath(config), 10);
        result.fields["module"] = "intranet_migration";
        result.fields["remote_entry"] = config.listen_host + ":" + std::to_string(config.listen_port);
        result.fields["action"] = "thread_report";
        result.fields["result"] = ComputeCommandOutcome(result);
        result.fields["next_action"] = "continue through semantic_action_prepare or local_cli";
        result.fields["latest_events"] = GetFieldOrDefault(events, "content", "");
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"health\",\"reason\":\"thread report unavailable\"}");
    }
    if (command == "build-target") {
        CommandResult result;
        std::string resolved_config = config_name.empty() ? "Release" : config_name;
        if (build_dir.empty() || target.empty()) {
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "build_dir and target are required";
            result.fields["missing_args"] = build_dir.empty() ? "build_dir" : "target";
        } else if (dry_run) {
            result = BuildTargetDryRunResult(build_dir, target, resolved_config);
        } else if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
        } else {
            const std::string queued_task_id = g_task_manager->EnqueueCliProfile(
                "build_target",
                "-BuildDir \"" + build_dir + "\" -Config " + resolved_config + " -Target " + target);
            result = BuildQueuedTaskResult(queued_task_id);
        }
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"task-latest\",\"reason\":\"build queue unavailable\"}");
    }
    if (command == "run-light") {
        CommandResult result;
        if (action_id == "check_remote_online") {
            return LocalCliResult(config, "health", "", "", "", "", "", "", "", "", false);
        }
        if (action_id == "check_local_chat") {
            return LocalCliResult(config, "chat-status", "", "", "", "", "", "", "", "", false);
        }
        if (action_id == "read_latest_log") {
            return LocalCliResult(config, "log-latest", "", "", "", "", "", "", "", "", false);
        }
        if (action_id == "get_git_diff") {
            return LocalCliResult(config, "diff", "", repo_root, "", "", "", "", "", "", false);
        }
        if (action_id == "read_test_result") {
            return LocalCliResult(config, "test-result", task_id, "", "", "", "", "", log_path, args_text, false);
        }
        result.ok = false;
        result.exit_code = 45;
        result.fields["error"] = "action_id is not in run-light allowlist";
        result.fields["action_id"] = action_id;
        result.fields["safe_action_allowlist"] =
            "check_remote_online,check_local_chat,read_latest_log,get_git_diff,read_test_result";
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"health\",\"reason\":\"unsupported run-light action\"}");
    }

    CommandResult result;
    result.ok = false;
    result.exit_code = 49;
    result.fields["error"] = "unsupported local_cli command";
    result.fields["supported_commands"] =
        "health,chat-status,task-latest,task,log-latest,diff,run-light,build-target,test-result,thread-report";
    return BuildLocalCliEnvelope(
        config,
        command,
        result,
        "{\"command\":\"health\",\"reason\":\"unsupported command\"}");
}

bool VerifyExpectedMarker(
    const std::string & profile_name,
    const CommandResult & result,
    const std::string & semantic_outcome,
    const std::string & log_content) {
    if (!result.ok || result.exit_code != 0) {
        return false;
    }
    if (profile_name == "configure_project") {
        return semantic_outcome == "succeeded";
    }
    if (profile_name == "run_ctest_target") {
        return semantic_outcome == "ctest_tests_passed";
    }
    if (profile_name == "prepare_build_dir") {
        return TextContainsCaseInsensitive(log_content, "prepare_build_dir_done=true");
    }
    return semantic_outcome == "succeeded";
}

std::string BuildPatchBackupPath(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(
        config.log_root,
        "single_file_patch_backup_" + TimeStampForFileName() + ".bak");
}

CommandResult PreviewPatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & new_content) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["would_write"] = "false";
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveWorkspaceFilePath(config, file_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 45;
        result.fields["error"] = path_error;
        return result;
    }

    std::string old_content;
    std::string read_error;
    if (!ReadWholeFile(normalized, &old_content, &read_error)) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["error"] = read_error;
        return result;
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["old_bytes"] = std::to_string(old_content.size());
    result.fields["new_bytes"] = std::to_string(new_content.size());
    result.fields["changed"] = old_content == new_content ? "false" : "true";
    result.fields["diff"] = BuildSimpleUnifiedDiff(normalized.string(), old_content, new_content);
    return result;
}

CommandResult ApplySingleFilePatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & new_content) {
    CommandResult result = PreviewPatchResult(config, file_path, new_content);
    if (!result.ok) {
        return result;
    }
    const std::filesystem::path normalized(result.fields["normalized_path"]);
    const std::string resource_key = "file:" + normalized.string();
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "target file is busy";
        return result;
    }

    std::string backup_content;
    std::string backup_read_error;
    if (!ReadWholeFile(normalized, &backup_content, &backup_read_error)) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["error"] = backup_read_error;
        return result;
    }

    const std::string backup_path = BuildPatchBackupPath(config);
    std::filesystem::create_directories(config.log_root);
    std::ofstream backup_output(backup_path, std::ios::binary | std::ios::trunc);
    if (!backup_output.is_open()) {
        result.ok = false;
        result.exit_code = 49;
        result.fields["error"] = "failed to open backup file";
        return result;
    }
    backup_output.write(backup_content.data(), static_cast<std::streamsize>(backup_content.size()));
    backup_output.close();

    const std::filesystem::path temp_path =
        normalized.string() + ".patching." + TimeStampForFileName() + ".tmp";
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 47;
        result.fields["error"] = "failed to open temp file";
        return result;
    }
    output.write(new_content.data(), static_cast<std::streamsize>(new_content.size()));
    output.close();

    std::error_code ec;
    std::filesystem::rename(temp_path, normalized, ec);
    if (ec) {
        std::filesystem::remove(normalized, ec);
        ec.clear();
        std::filesystem::rename(temp_path, normalized, ec);
    }
    if (ec) {
        std::filesystem::remove(temp_path, ec);
        result.ok = false;
        result.exit_code = 48;
        result.fields["error"] = "failed to replace target file";
        return result;
    }

    const std::string log_path = BuildLogPath(config, "apply_single_file_patch");
    std::ofstream log(log_path, std::ios::out | std::ios::trunc);
    log << "file_path=" << normalized.string() << "\n";
    log << "old_bytes=" << result.fields["old_bytes"] << "\n";
    log << "new_bytes=" << result.fields["new_bytes"] << "\n";
    log << "changed=" << result.fields["changed"] << "\n";
    log << "diff=\n" << result.fields["diff"] << "\n";

    result.fields["would_write"] = "true";
    result.fields["backup_path"] = backup_path;
    result.fields["log_path"] = log_path;
    result.fields["result"] = "applied";
    return result;
}

CommandResult RevertSingleFilePatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & backup_path) {
    CommandResult result;
    result.fields["file_path"] = file_path;
    result.fields["backup_path"] = backup_path;

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveWorkspaceFilePath(config, file_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 45;
        result.fields["error"] = path_error;
        return result;
    }

    std::filesystem::path normalized_backup;
    if (!TryResolveAllowedPath(config, backup_path, &normalized_backup, &path_error)) {
        result.ok = false;
        result.exit_code = 50;
        result.fields["error"] = path_error;
        return result;
    }

    std::string current_content;
    std::string backup_content;
    std::string read_error;
    if (!ReadWholeFile(normalized, &current_content, &read_error) ||
        !ReadWholeFile(normalized_backup, &backup_content, &read_error)) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["error"] = read_error;
        return result;
    }

    const std::string resource_key = "file:" + normalized.string();
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "target file is busy";
        return result;
    }

    const std::filesystem::path temp_path =
        normalized.string() + ".reverting." + TimeStampForFileName() + ".tmp";
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 47;
        result.fields["error"] = "failed to open temp file";
        return result;
    }
    output.write(backup_content.data(), static_cast<std::streamsize>(backup_content.size()));
    output.close();

    std::error_code ec;
    std::filesystem::rename(temp_path, normalized, ec);
    if (ec) {
        std::filesystem::remove(normalized, ec);
        ec.clear();
        std::filesystem::rename(temp_path, normalized, ec);
    }
    if (ec) {
        std::filesystem::remove(temp_path, ec);
        result.ok = false;
        result.exit_code = 48;
        result.fields["error"] = "failed to replace target file";
        return result;
    }

    const std::string log_path = BuildLogPath(config, "revert_single_file_patch");
    const std::string diff = BuildSimpleUnifiedDiff(normalized.string(), current_content, backup_content);
    std::ofstream log(log_path, std::ios::out | std::ios::trunc);
    log << "file_path=" << normalized.string() << "\n";
    log << "backup_path=" << normalized_backup.string() << "\n";
    log << "diff=\n" << diff << "\n";

    result.fields["normalized_path"] = normalized.string();
    result.fields["normalized_backup_path"] = normalized_backup.string();
    result.fields["diff"] = diff;
    result.fields["log_path"] = log_path;
    result.fields["result"] = "reverted";
    return result;
}

std::string BuildOptFileRuntimeDir(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "optfile_runtime");
}

std::string SanitizeOptFileTargetName(const std::string & target_name) {
    std::string name = target_name.empty() ? "optfile_state.jsonl" : target_name;
    std::replace(name.begin(), name.end(), '\\', '/');
    const std::size_t slash_pos = name.find_last_of('/');
    if (slash_pos != std::string::npos) {
        name = name.substr(slash_pos + 1);
    }
    std::string sanitized;
    for (char ch : name) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0 || ch == '.' || ch == '_' || ch == '-') {
            sanitized.push_back(ch);
        }
    }
    if (sanitized.empty() || sanitized == "." || sanitized == "..") {
        sanitized = "optfile_state.jsonl";
    }
    const std::string lower = ToLowerAscii(sanitized);
    if (lower == "optfile.exe" || lower == "codex_lan_agent.exe") {
        sanitized = "optfile_state.jsonl";
    }
    return sanitized;
}

std::filesystem::path BuildOptFileTargetPath(
    const AgentConfig & config,
    const std::string & target_name) {
    return std::filesystem::path(BuildOptFileRuntimeDir(config)) / SanitizeOptFileTargetName(target_name);
}

std::string BuildDialogSlicesDir(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "dialog_slices");
}

std::string BuildSessionDispatchDir(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "session_dispatch");
}

std::string BuildRemoteChatSessionsPath(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(BuildSessionDispatchDir(config), "remote_chat_sessions.jsonl");
}

std::string BuildExecutionBindingsPath(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(BuildSessionDispatchDir(config), "execution_bindings.jsonl");
}

std::string BuildDispatchAuditPath(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(BuildSessionDispatchDir(config), "dispatch_audit.jsonl");
}

std::string SanitizeDialogSliceSessionId(const std::string & session_id) {
    std::string sanitized;
    for (char ch : session_id) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0 || ch == '.' || ch == '_' || ch == '-') {
            sanitized.push_back(ch);
        } else if (std::isspace(uch) != 0) {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty() || sanitized == "." || sanitized == "..") {
        sanitized = "default_session";
    }
    return sanitized;
}

std::filesystem::path BuildDialogSlicePath(
    const AgentConfig & config,
    const std::string & session_id) {
    return std::filesystem::path(BuildDialogSlicesDir(config))
        / (SanitizeDialogSliceSessionId(session_id) + ".jsonl");
}

std::string SanitizeDispatchToken(const std::string & value, const std::string & fallback) {
    std::string sanitized;
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0 || ch == '.' || ch == '_' || ch == '-') {
            sanitized.push_back(ch);
        } else if (std::isspace(uch) != 0) {
            sanitized.push_back('_');
        }
    }
    return sanitized.empty() ? fallback : sanitized;
}

std::string BuildRemoteSessionTitle(
    const std::string & module_name,
    const std::string & reasoning_level,
    const std::string & task_state,
    const std::string & short_goal,
    const std::string & task_id) {
    std::ostringstream output;
    output << SanitizeDispatchToken(module_name, "module")
           << " | " << SanitizeDispatchToken(reasoning_level, "level")
           << " | " << SanitizeDispatchToken(task_state, "state")
           << " | " << (short_goal.empty() ? "goal" : short_goal)
           << " | " << SanitizeDispatchToken(task_id, "task");
    return output.str();
}

std::string StableContentChecksum(const std::string & content) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : content) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

CommandResult BuildOptFileBaseResult(
    const AgentConfig & config,
    const std::string & target_name,
    const std::string & operation,
    bool append) {
    CommandResult result;
    const std::filesystem::path runtime_dir(BuildOptFileRuntimeDir(config));
    const std::filesystem::path target_path = BuildOptFileTargetPath(config, target_name);
    result.fields["module"] = "intranet_migration";
    result.fields["action"] = "optfile_" + operation;
    result.fields["operation"] = operation;
    result.fields["target_name"] = SanitizeOptFileTargetName(target_name);
    result.fields["target_path"] = target_path.string();
    result.fields["runtime_dir"] = runtime_dir.string();
    result.fields["append"] = append ? "true" : "false";
    result.fields["safety_scope"] = "log_root/optfile_runtime";
    result.fields["optfile_exe_policy"] = "never_modify_optfile_exe";
    result.fields["trace_log_path"] = BuildRemoteControlEventsPath(config);
    return result;
}

CommandResult OptFileReadResult(
    const AgentConfig & config,
    const std::string & target_name,
    int max_bytes) {
    CommandResult result = BuildOptFileBaseResult(config, target_name, "read", false);
    const std::filesystem::path target_path(result.fields["target_path"]);
    if (!std::filesystem::exists(target_path)) {
        result.ok = false;
        result.exit_code = 64;
        result.fields["error"] = "optfile target does not exist";
        result.fields["content"] = "";
        result.fields["bytes"] = "0";
        result.fields["checksum"] = StableContentChecksum("");
        result.fields["next_action"] = "call lan_agent_optfile_apply_write to create the runtime optfile";
        return result;
    }
    std::string content;
    std::string read_error;
    if (!ReadWholeFile(target_path, &content, &read_error)) {
        result.ok = false;
        result.exit_code = 65;
        result.fields["error"] = read_error;
        return result;
    }
    const std::size_t bounded_max = max_bytes > 0
        ? static_cast<std::size_t>(max_bytes)
        : static_cast<std::size_t>(65536);
    result.fields["bytes"] = std::to_string(content.size());
    result.fields["checksum"] = StableContentChecksum(content);
    result.fields["truncated"] = content.size() > bounded_max ? "true" : "false";
    result.fields["content"] = content.substr(0, std::min<std::size_t>(content.size(), bounded_max));
    result.fields["result"] = "read";
    return result;
}

CommandResult OptFileWritePreviewResult(
    const AgentConfig & config,
    const std::string & target_name,
    const std::string & data,
    bool append) {
    CommandResult result = BuildOptFileBaseResult(config, target_name, "write_preview", append);
    std::string old_content;
    std::string read_error;
    const std::filesystem::path target_path(result.fields["target_path"]);
    if (std::filesystem::exists(target_path)) {
        ReadWholeFile(target_path, &old_content, &read_error);
    }
    const std::string new_content = append ? (old_content + data) : data;
    result.fields["would_write"] = "false";
    result.fields["old_bytes"] = std::to_string(old_content.size());
    result.fields["new_bytes"] = std::to_string(new_content.size());
    result.fields["data_bytes"] = std::to_string(data.size());
    result.fields["old_checksum"] = StableContentChecksum(old_content);
    result.fields["new_checksum"] = StableContentChecksum(new_content);
    result.fields["changed"] = old_content == new_content ? "false" : "true";
    result.fields["result"] = "preview";
    result.fields["next_action"] = "call lan_agent_optfile_apply_write only if this preview is intended";
    return result;
}

CommandResult OptFileApplyWriteResult(
    const AgentConfig & config,
    const std::string & target_name,
    const std::string & data,
    bool append) {
    CommandResult result = OptFileWritePreviewResult(config, target_name, data, append);
    const std::filesystem::path runtime_dir(result.fields["runtime_dir"]);
    const std::filesystem::path target_path(result.fields["target_path"]);
    std::error_code ec;
    std::filesystem::create_directories(runtime_dir, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 66;
        result.fields["error"] = "failed to create optfile runtime dir";
        return result;
    }

    const std::string resource_key = "optfile:" + target_path.string();
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "optfile target is busy";
        return result;
    }

    if (append) {
        std::ofstream output(target_path, std::ios::binary | std::ios::app);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 67;
            result.fields["error"] = "failed to open optfile target for append";
            return result;
        }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
    } else {
        const std::filesystem::path temp_path =
            target_path.string() + ".optfile." + TimeStampForFileName() + ".tmp";
        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 67;
            result.fields["error"] = "failed to open optfile temp file";
            return result;
        }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        std::filesystem::rename(temp_path, target_path, ec);
        if (ec) {
            std::filesystem::remove(target_path, ec);
            ec.clear();
            std::filesystem::rename(temp_path, target_path, ec);
        }
        if (ec) {
            std::filesystem::remove(temp_path, ec);
            result.ok = false;
            result.exit_code = 68;
            result.fields["error"] = "failed to replace optfile target";
            return result;
        }
    }

    std::string final_content;
    std::string read_error;
    ReadWholeFile(target_path, &final_content, &read_error);
    const std::string log_path = BuildLogPath(config, "optfile_apply_write");
    std::ofstream log(log_path, std::ios::out | std::ios::trunc);
    log << "operation=apply_write\n";
    log << "target_path=" << target_path.string() << "\n";
    log << "append=" << (append ? "true" : "false") << "\n";
    log << "data_bytes=" << data.size() << "\n";
    log << "final_bytes=" << final_content.size() << "\n";
    log << "final_checksum=" << StableContentChecksum(final_content) << "\n";
    log.close();

    result.fields["would_write"] = "true";
    result.fields["log_path"] = log_path;
    result.fields["final_bytes"] = std::to_string(final_content.size());
    result.fields["final_checksum"] = StableContentChecksum(final_content);
    result.fields["result"] = "applied";
    result.fields["next_action"] = "call lan_agent_optfile_read to verify content";
    return result;
}

CommandResult RecordDialogSliceResult(
    const AgentConfig & config,
    const std::string & session_id,
    const std::string & turn_id,
    const std::string & user_text,
    const std::string & assistant_text,
    const std::string & tags) {
    CommandResult result;
    result.fields["module"] = "intranet_migration";
    result.fields["action"] = "record_dialog_slice";
    result.fields["session_id"] = SanitizeDialogSliceSessionId(session_id);
    result.fields["turn_id"] = turn_id;
    result.fields["analysis_root"] = BuildDialogSlicesDir(config);
    result.fields["trace_log_path"] = BuildRemoteControlEventsPath(config);

    if (session_id.empty() || turn_id.empty() || user_text.empty() || assistant_text.empty()) {
        result.ok = false;
        result.exit_code = 69;
        result.fields["error"] = "session_id, turn_id, user_text, and assistant_text are required";
        result.fields["next_action"] = "provide complete dialog turn fields";
        return result;
    }

    const std::filesystem::path slice_dir(BuildDialogSlicesDir(config));
    const std::filesystem::path slice_path = BuildDialogSlicePath(config, session_id);
    std::error_code ec;
    std::filesystem::create_directories(slice_dir, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 70;
        result.fields["error"] = "failed to create dialog_slices dir";
        return result;
    }

    const std::string resource_key = "dialog_slice:" + slice_path.string();
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "dialog slice target is busy";
        result.fields["resource_key"] = resource_key;
        return result;
    }

    std::ostringstream line;
    line << "{"
         << "\"timestamp\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\","
         << "\"session_id\":\"" << codex_lan_agent::JsonEscape(SanitizeDialogSliceSessionId(session_id)) << "\","
         << "\"turn_id\":\"" << codex_lan_agent::JsonEscape(turn_id) << "\","
         << "\"user_text\":\"" << codex_lan_agent::JsonEscape(user_text) << "\","
         << "\"assistant_text\":\"" << codex_lan_agent::JsonEscape(assistant_text) << "\","
         << "\"tags\":\"" << codex_lan_agent::JsonEscape(tags) << "\""
         << "}\n";
    const std::string line_text = line.str();

    std::ofstream output(slice_path, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 71;
        result.fields["error"] = "failed to open dialog slice file";
        return result;
    }
    output.write(line_text.data(), static_cast<std::streamsize>(line_text.size()));
    output.close();

    std::string final_content;
    std::string read_error;
    ReadWholeFile(slice_path, &final_content, &read_error);
    const std::string log_path = BuildLogPath(config, "record_dialog_slice");
    std::ofstream log(log_path, std::ios::out | std::ios::trunc);
    log << "slice_path=" << slice_path.string() << "\n";
    log << "session_id=" << SanitizeDialogSliceSessionId(session_id) << "\n";
    log << "turn_id=" << turn_id << "\n";
    log << "bytes=" << line_text.size() << "\n";
    log << "final_checksum=" << StableContentChecksum(final_content) << "\n";
    log.close();

    result.fields["slice_path"] = slice_path.string();
    result.fields["bytes"] = std::to_string(line_text.size());
    result.fields["checksum"] = StableContentChecksum(line_text);
    result.fields["file_checksum"] = StableContentChecksum(final_content);
    result.fields["log_path"] = log_path;
    result.fields["tags"] = tags;
    result.fields["result"] = "recorded";
    result.fields["next_action"] = "call lan_agent_analyze_dialog_slices to inspect stored turns";
    return result;
}

CommandResult AnalyzeDialogSlicesResult(
    const AgentConfig & config,
    const std::string & session_id,
    int max_entries) {
    CommandResult result;
    result.fields["module"] = "intranet_migration";
    result.fields["action"] = "analyze_dialog_slices";
    result.fields["analysis_root"] = BuildDialogSlicesDir(config);
    result.fields["session_id"] = SanitizeDialogSliceSessionId(session_id);

    const std::filesystem::path slice_dir(BuildDialogSlicesDir(config));
    std::error_code ec;
    if (!std::filesystem::exists(slice_dir, ec)) {
        result.ok = false;
        result.exit_code = 72;
        result.fields["error"] = "dialog_slices dir does not exist";
        result.fields["slice_file_count"] = "0";
        result.fields["result"] = "empty";
        result.fields["next_action"] = "call lan_agent_record_dialog_slice first";
        return result;
    }

    struct SliceEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type write_time;
    };

    std::vector<SliceEntry> entries;
    if (!session_id.empty()) {
        const std::filesystem::path session_path = BuildDialogSlicePath(config, session_id);
        if (std::filesystem::exists(session_path, ec) && !ec) {
            entries.push_back({session_path, std::filesystem::last_write_time(session_path, ec)});
            if (ec) {
                entries.clear();
                ec.clear();
            }
        }
    } else {
        for (const auto & entry : std::filesystem::directory_iterator(slice_dir, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() != ".jsonl") {
                continue;
            }
            entries.push_back({entry.path(), entry.last_write_time(ec)});
            if (ec) {
                entries.pop_back();
                ec.clear();
            }
        }
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const SliceEntry & left, const SliceEntry & right) {
            return left.write_time > right.write_time;
        });

    result.fields["slice_file_count"] = std::to_string(entries.size());
    result.fields["max_entries"] = std::to_string(max_entries > 0 ? max_entries : 20);
    if (entries.empty()) {
        result.ok = false;
        result.exit_code = 73;
        result.fields["error"] = "no dialog slice files found";
        result.fields["result"] = "empty";
        result.fields["summary"] = "dialog slice folder is empty";
        result.fields["next_action"] = "record a dialog slice before analysis";
        return result;
    }

    const std::filesystem::path latest_path = entries.front().path;
    result.fields["latest_slice_path"] = latest_path.string();
    result.fields["latest_slice_name"] = latest_path.filename().string();

    std::string content;
    std::string read_error;
    if (!ReadWholeFile(latest_path, &content, &read_error)) {
        result.ok = false;
        result.exit_code = 74;
        result.fields["error"] = read_error;
        return result;
    }

    int total_entries = 0;
    std::istringstream input(content);
    std::string line;
    std::deque<std::string> tail_entries;
    const int bounded_max_entries = max_entries > 0 ? max_entries : 20;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        if (static_cast<int>(tail_entries.size()) >= bounded_max_entries) {
            tail_entries.pop_front();
        }
        tail_entries.push_back(line);
        ++total_entries;
    }

    std::ostringstream tail_text;
    for (const std::string & entry : tail_entries) {
        tail_text << entry << "\n";
    }

    result.fields["entry_count"] = std::to_string(total_entries);
    result.fields["latest_slice_tail"] = tail_text.str();
    result.fields["latest_slice_checksum"] = StableContentChecksum(content);
    result.fields["result"] = "analyzed";
    result.fields["summary"] = session_id.empty()
        ? "dialog slice folder analyzed"
        : "dialog slice session analyzed";
    result.fields["next_action"] = "read latest_slice_tail or continue recording new turns";
    return result;
}

bool IsSafeLightProfile(const std::string & profile) {
    return profile == "check_build_dir" ||
           profile == "run_script" ||
           profile == "run_local_chat";
}

std::string FindGitRootUnderWorkspace(const AgentConfig & config) {
    std::filesystem::path workspace(config.workspace_root);
    std::error_code ec;
    if (std::filesystem::exists(workspace / ".git", ec)) {
        return workspace.string();
    }
    for (const auto & entry : std::filesystem::directory_iterator(workspace, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_directory()) {
            continue;
        }
        const std::filesystem::path candidate = entry.path();
        if (std::filesystem::exists(candidate / ".git", ec) && !ec) {
            return candidate.string();
        }
        ec.clear();
    }
    return config.workspace_root;
}

CommandResult BuildTargetDryRunResult(
    const std::string & build_dir,
    const std::string & target,
    const std::string & config_name) {
    CommandResult result;
    result.fields["status"] = "validated";
    result.fields["dry_run"] = "true";
    result.fields["build_dir"] = build_dir;
    result.fields["target"] = target;
    result.fields["config"] = config_name;
    result.fields["profile"] = "build_target";
    result.fields["expected_marker"] = ExpectedMarkerForProfile("build_target");
    result.fields["semantic_outcome"] = "args_valid";
    result.fields["summary"] = "build_target arguments validated; no task queued";
    result.fields["next_action"] = "submit without dry_run to queue build";
    return result;
}

CommandResult SnapshotDiffResult(
    const AgentConfig & config,
    const std::string & repo_root,
    int timeout_sec) {
    CommandResult result;
    const std::string log_path = BuildLogPath(config, "snapshot_diff");
    std::string working_directory = repo_root.empty()
        ? FindGitRootUnderWorkspace(config)
        : config.workspace_root;
    if (!repo_root.empty()) {
        std::filesystem::path normalized_repo;
        std::string path_error;
        if (!TryResolveAllowedPath(config, repo_root, &normalized_repo, &path_error)) {
            result.ok = false;
            result.exit_code = 53;
            result.fields["error"] = path_error;
            result.fields["semantic_outcome"] = "git_root_invalid";
            result.fields["log_path"] = log_path;
            return result;
        }
        working_directory = normalized_repo.string();
    }
#ifdef _WIN32
    const std::string command_line =
        "cmd.exe /c git rev-parse --show-toplevel && git status --short && git diff --no-ext-diff --stat && git diff --no-ext-diff --";
#else
    const std::string command_line =
        "git rev-parse --show-toplevel && git status --short && git diff --no-ext-diff --stat && git diff --no-ext-diff --";
#endif
    codex_lan_agent::ProcessRunResult run_result;
    std::string error_message;
    if (!codex_lan_agent::RunCommandWithLog(
            command_line,
            working_directory,
            log_path,
            timeout_sec,
            &run_result,
            &error_message)) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = error_message;
        result.fields["log_path"] = log_path;
        result.fields["semantic_outcome"] = "snapshot_diff_start_failed";
        return result;
    }

    result.ok = run_result.exit_code == 0;
    result.exit_code = run_result.exit_code;
    result.fields["log_path"] = log_path;
    result.fields["repo_root"] = working_directory;
    result.fields["timed_out"] = run_result.timed_out ? "true" : "false";
    result.fields["stalled"] = run_result.stalled ? "true" : "false";

    std::string log_content;
    std::string read_error;
    if (ReadWholeFile(log_path, &log_content, &read_error)) {
        result.fields["semantic_outcome"] =
            result.ok ? "snapshot_diff_ready" : "git_root_or_diff_failed";
        result.fields["content"] = log_content;
    } else {
        result.fields["semantic_outcome"] = result.ok ? "snapshot_diff_ready" : "git_root_or_diff_failed";
        result.fields["log_read_error"] = read_error;
    }
    result.fields["expected_marker"] = "git_rev_parse_or_snapshot_diff_log";
    return result;
}

CommandResult BuildQueuedTaskResult(const std::string & task_id) {
    if (g_task_manager == nullptr) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "task manager is not active";
        return result;
    }

    CommandResult result = g_task_manager->GetTaskResult(task_id);
    result.fields["task_id"] = task_id;
    return result;
}

std::string BuildConfigureProjectArguments(
    const std::string & project_root,
    const std::string & build_dir,
    const std::string & generator_kind,
    const std::string & cmake_args,
    const std::string & env_args = std::string()) {
    std::string arguments =
        "-ProjectRoot \"" + project_root + "\" -BuildDir \"" + build_dir
        + "\" -GeneratorKind " + generator_kind;
    if (!cmake_args.empty()) {
        arguments += " -ExtraCMakeArgs \"" + cmake_args + "\"";
    }
    if (!env_args.empty()) {
        arguments += " -Env \"" + env_args + "\"";
    }
    return arguments;
}

std::string BuildPrepareBuildDirArguments(
    const std::string & build_dir,
    bool create_if_missing) {
    std::string arguments = "-BuildDir \"" + build_dir + "\"";
    if (create_if_missing) {
        arguments += " -CreateIfMissing";
    }
    return arguments;
}

bool ExtractJsonBool(const std::string & body, const std::string & key, bool default_value) {
    const std::string raw_value = ToLowerAscii(ExtractJsonRawValue(body, key));
    if (raw_value == "true" || raw_value == "\"true\"" || raw_value == "1") {
        return true;
    }
    if (raw_value == "false" || raw_value == "\"false\"" || raw_value == "0") {
        return false;
    }
    return default_value;
}

CommandResult ListDirectoryResult(
    const AgentConfig & config,
    const std::string & directory_path,
    int max_entries = 200) {
    CommandResult result;
    result.fields["directory_path"] = directory_path;

    if (directory_path.empty()) {
        result.ok = false;
        result.exit_code = 30;
        result.fields["error"] = "directory_path is required";
        return result;
    }

    std::filesystem::path requested(directory_path);
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(requested, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 31;
        result.fields["error"] = "failed to normalize directory path";
        return result;
    }

    const std::filesystem::path logs_root = std::filesystem::path(config.log_root);
    const std::filesystem::path workspace_root = std::filesystem::path(config.workspace_root);
    if (!StartsWithPath(normalized, logs_root) && !StartsWithPath(normalized, workspace_root)) {
        result.ok = false;
        result.exit_code = 32;
        result.fields["error"] = "directory is outside allowed roots";
        return result;
    }

    if (!std::filesystem::exists(normalized)) {
        result.ok = false;
        result.exit_code = 33;
        result.fields["error"] = "directory does not exist";
        return result;
    }

    if (!std::filesystem::is_directory(normalized)) {
        result.ok = false;
        result.exit_code = 34;
        result.fields["error"] = "path is not a directory";
        return result;
    }

    int index = 0;
    for (const auto & entry : std::filesystem::directory_iterator(normalized)) {
        if (index >= max_entries) {
            break;
        }
        const std::string key = "entry_" + std::to_string(index);
        const std::string type = entry.is_directory() ? "[dir] " : "[file] ";
        result.fields[key] = type + entry.path().filename().string();
        ++index;
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["entry_count"] = std::to_string(index);
    return result;
}

CommandResult DiscoverLogsResult(
    const AgentConfig & config,
    int max_entries,
    int tail_lines) {
    CommandResult result;
    result.fields["log_root"] = config.log_root;
    const int bounded_max_entries = max_entries > 0 ? max_entries : 20;
    const int bounded_tail_lines = tail_lines > 0 ? tail_lines : 20;

    struct LogEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type write_time;
        std::uintmax_t size = 0;
    };

    std::vector<LogEntry> entries;
    std::error_code ec;
    for (const auto & entry : std::filesystem::directory_iterator(config.log_root, ec)) {
        if (ec) {
            result.ok = false;
            result.exit_code = 49;
            result.fields["error"] = "failed to list log_root";
            return result;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".log") {
            continue;
        }
        LogEntry item;
        item.path = entry.path();
        item.write_time = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        item.size = entry.file_size(ec);
        if (ec) {
            item.size = 0;
            ec.clear();
        }
        entries.push_back(item);
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const LogEntry & left, const LogEntry & right) {
            return left.write_time > right.write_time;
        });

    const std::size_t count =
        std::min<std::size_t>(entries.size(), static_cast<std::size_t>(bounded_max_entries));
    result.fields["log_count"] = std::to_string(entries.size());
    result.fields["returned_count"] = std::to_string(count);
    for (std::size_t index = 0; index < count; ++index) {
        const LogEntry & item = entries[index];
        const auto system_now = std::chrono::system_clock::now();
        const auto file_now = std::filesystem::file_time_type::clock::now();
        const auto system_write_time =
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                item.write_time - file_now + system_now);
        const std::time_t write_time = std::chrono::system_clock::to_time_t(system_write_time);
        const std::string prefix = "log_" + std::to_string(index);
        result.fields[prefix + "_path"] = item.path.string();
        result.fields[prefix + "_name"] = item.path.filename().string();
        result.fields[prefix + "_time"] = std::to_string(static_cast<long long>(write_time));
        result.fields[prefix + "_bytes"] = std::to_string(item.size);
    }

    if (count > 0) {
        CommandResult tail = TailTextFileResult(config, entries.front().path.string(), bounded_tail_lines);
        result.fields["latest_log_path"] = entries.front().path.string();
        result.fields["latest_log_name"] = entries.front().path.filename().string();
        result.fields["latest_log_tail"] = GetFieldOrDefault(tail, "content", "");
    } else {
        result.fields["latest_log_path"] = "";
        result.fields["latest_log_name"] = "";
        result.fields["latest_log_tail"] = "";
    }
    return result;
}

CommandResult BuildHealthResult(const AgentConfig & config) {
    std::filesystem::create_directories(config.log_root);

    std::string generation_detail = "not configured";
    std::string embedding_detail = "not configured";
    std::string local_chat_detail = "not configured";
    const bool generation_ready = !config.generation_endpoint.empty()
        && codex_lan_agent::CheckTcpEndpoint(config.generation_endpoint, 2000, &generation_detail);
    const bool embedding_ready = !config.embedding_endpoint.empty()
        && codex_lan_agent::CheckTcpEndpoint(config.embedding_endpoint, 2000, &embedding_detail);
    const bool local_chat_ready = !config.local_chat_endpoint.empty()
        && codex_lan_agent::CheckTcpEndpoint(config.local_chat_endpoint, 2000, &local_chat_detail);

    CommandResult result;
    result.fields["status"] = "ok";
    result.fields["platform"] = CurrentPlatformName();
    result.fields["listen_host"] = config.listen_host;
    result.fields["listen_port"] = std::to_string(config.listen_port);
    result.fields["workspace_root"] = config.workspace_root;
    result.fields["log_root"] = config.log_root;
    result.fields["remote_timestamp"] = IsoTimestampNow();
    result.fields["observed_at"] = result.fields["remote_timestamp"];
    result.fields["remote_control_events_path"] = BuildRemoteControlEventsPath(config);
    result.fields["generation_endpoint"] = config.generation_endpoint;
    result.fields["generation_ready"] = generation_ready ? "true" : "false";
    result.fields["generation_detail"] = generation_detail;
    result.fields["embedding_endpoint"] = config.embedding_endpoint;
    result.fields["embedding_ready"] = embedding_ready ? "true" : "false";
    result.fields["embedding_detail"] = embedding_detail;
    result.fields["local_chat_endpoint"] = config.local_chat_endpoint;
    result.fields["local_chat_ready"] = local_chat_ready ? "true" : "false";
    result.fields["local_chat_detail"] = local_chat_detail;
    result.fields["profile_count"] = std::to_string(config.profiles.size());
    if (g_task_manager != nullptr) {
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
    }
    const std::vector<std::string> active_resource_keys = SnapshotActiveResourceKeys();
    result.fields["active_resource_lock_count"] = std::to_string(active_resource_keys.size());
    for (std::size_t index = 0; index < active_resource_keys.size(); ++index) {
        result.fields["active_resource_lock_" + std::to_string(index)] = active_resource_keys[index];
    }
    const auto last_event = SnapshotLastRemoteControlEvent();
    const auto copy_last_field = [&result, &last_event](const std::string & from, const std::string & to) {
        const auto it = last_event.find(from);
        result.fields[to] = it == last_event.end() ? "" : it->second;
    };
    copy_last_field("timestamp", "last_request_time");
    copy_last_field("entry_name", "last_request_entry");
    copy_last_field("method", "last_request_method");
    copy_last_field("status", "last_request_status");
    copy_last_field("duration_ms", "last_request_duration_ms");
    copy_last_field("source_thread", "last_request_thread");
    copy_last_field("task_id", "last_request_task_id");
    copy_last_field("command_name", "last_request_command_name");
    copy_last_field("request_type", "last_request_type");
    copy_last_field("trigger", "last_request_trigger");
    return result;
}

CommandResult BuildLivenessResult(const AgentConfig & config) {
    CommandResult result;
    result.fields["status"] = "ok";
    result.fields["platform"] = CurrentPlatformName();
    result.fields["listen_host"] = config.listen_host;
    result.fields["listen_port"] = std::to_string(config.listen_port);
    result.fields["workspace_root"] = config.workspace_root;
    result.fields["remote_timestamp"] = IsoTimestampNow();
    result.fields["observed_at"] = result.fields["remote_timestamp"];
    result.fields["remote_control_events_path"] = BuildRemoteControlEventsPath(config);
    result.fields["local_chat_endpoint"] = config.local_chat_endpoint;
    result.fields["profile_count"] = std::to_string(config.profiles.size());
    if (g_task_manager != nullptr) {
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
    }
    result.fields["active_resource_lock_count"] = std::to_string(SnapshotActiveResourceKeys().size());
    const auto last_event = SnapshotLastRemoteControlEvent();
    const auto copy_last_field = [&result, &last_event](const std::string & from, const std::string & to) {
        const auto it = last_event.find(from);
        result.fields[to] = it == last_event.end() ? "" : it->second;
    };
    copy_last_field("timestamp", "last_request_time");
    copy_last_field("entry_name", "last_request_entry");
    copy_last_field("method", "last_request_method");
    copy_last_field("status", "last_request_status");
    copy_last_field("duration_ms", "last_request_duration_ms");
    copy_last_field("source_thread", "last_request_thread");
    copy_last_field("task_id", "last_request_task_id");
    copy_last_field("command_name", "last_request_command_name");
    copy_last_field("request_type", "last_request_type");
    copy_last_field("trigger", "last_request_trigger");
    return result;
}

CommandResult BuildRuntimeOverviewResult(const AgentConfig & config) {
    CommandResult result = BuildHealthResult(config);
    result.fields["overview"] = "runtime_overview";
    result.fields["platform"] = CurrentPlatformName();

    int profile_index = 0;
    for (const auto & entry : config.profiles) {
        result.fields["profile_" + std::to_string(profile_index)] = entry.first;
        ++profile_index;
    }
    result.fields["profile_count"] = std::to_string(profile_index);
    result.fields["async_local_chat_enabled"] = config.local_chat_endpoint.empty() ? "false" : "true";
    result.fields["async_queue_enabled"] = g_task_manager != nullptr ? "true" : "false";
    const std::vector<std::string> active_resource_keys = SnapshotActiveResourceKeys();
    result.fields["active_resource_lock_count"] = std::to_string(active_resource_keys.size());
    for (std::size_t index = 0; index < active_resource_keys.size(); ++index) {
        result.fields["active_resource_lock_" + std::to_string(index)] = active_resource_keys[index];
    }
    return result;
}

std::string ComputeCommandOutcome(const CommandResult & result) {
    const auto status_it = result.fields.find("status");
    if (status_it != result.fields.end()) {
        if (status_it->second == "queued" || status_it->second == "running") {
            return "BUSY";
        }
    }

    const auto error_it = result.fields.find("error");
    if (error_it != result.fields.end() && error_it->second == "resource is busy") {
        return "BUSY";
    }

    return (result.ok && result.exit_code == 0) ? "PASS" : "FAIL";
}

std::string GetFieldOrDefault(
    const CommandResult & result,
    const std::string & key,
    const std::string & default_value) {
    const auto it = result.fields.find(key);
    return it == result.fields.end() ? default_value : it->second;
}

CommandResult BuildRagBasicCommSmokeResult(const AgentConfig & config) {
    CommandResult health = BuildHealthResult(config);
    CommandResult result;
    result.fields["module"] = "RAG-integration-thread";
    result.fields["remote_entry"] = "192.168.9.100:18080";
    result.fields["action"] = "rag_basic_comm_smoke";
    result.fields["owner_thread"] = "RAG-integration-thread";
    result.fields["healthz_status"] = GetFieldOrDefault(health, "status", "unknown");
    result.fields["healthz_outcome"] = ComputeCommandOutcome(health);
    result.fields["queue_depth"] = GetFieldOrDefault(health, "queue_depth", "0");
    result.fields["profile_count"] = GetFieldOrDefault(health, "profile_count", "0");
    result.fields["generation_ready"] = GetFieldOrDefault(health, "generation_ready", "false");
    result.fields["embedding_ready"] = GetFieldOrDefault(health, "embedding_ready", "false");
    result.fields["local_chat_ready"] = GetFieldOrDefault(health, "local_chat_ready", "false");
    result.fields["generation_endpoint"] = config.generation_endpoint;
    result.fields["embedding_endpoint"] = config.embedding_endpoint;
    result.fields["local_chat_endpoint"] = config.local_chat_endpoint;

    std::filesystem::path latest_log_path;
    std::filesystem::file_time_type latest_write_time{};
    bool found_log = false;
    std::error_code ec;
    for (const auto & entry : std::filesystem::directory_iterator(config.log_root, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path candidate = entry.path();
        if (candidate.extension() != ".log") {
            continue;
        }
        const auto write_time = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (!found_log || write_time > latest_write_time) {
            found_log = true;
            latest_write_time = write_time;
            latest_log_path = candidate;
        }
    }

    if (found_log) {
        result.fields["latest_log_path"] = latest_log_path.string();
        result.fields["latest_log_name"] = latest_log_path.filename().string();
        result.fields["latest_log_status"] = "found";
        const auto system_now = std::chrono::system_clock::now();
        const auto file_now = std::filesystem::file_time_type::clock::now();
        const auto system_write_time =
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                latest_write_time - file_now + system_now);
        const std::time_t write_time = std::chrono::system_clock::to_time_t(system_write_time);
        result.fields["latest_log_time"] = std::to_string(static_cast<long long>(write_time));
        CommandResult tail = TailTextFileResult(config, latest_log_path.string(), 20);
        result.fields["latest_log_tail"] = GetFieldOrDefault(tail, "content", "");
    } else {
        result.fields["latest_log_path"] = "";
        result.fields["latest_log_name"] = "";
        result.fields["latest_log_time"] = "";
        result.fields["latest_log_status"] = "not_found";
        result.fields["latest_log_tail"] = "";
    }

    std::vector<std::string> blocking_points;
    if (result.fields["generation_ready"] != "true") {
        blocking_points.push_back("generation_not_ready");
    }
    if (result.fields["embedding_ready"] != "true") {
        blocking_points.push_back("embedding_not_ready");
    }
    if (result.fields["local_chat_ready"] != "true") {
        blocking_points.push_back("local_chat_not_ready");
    }

    if (blocking_points.empty()) {
        result.fields["risk_level"] = "low";
        result.fields["blocking_points"] = "none";
        result.fields["result_summary"] = "basic communication smoke passed";
        result.fields["result"] = "pass";
        result.fields["next_action"] = "continue rag migration smoke";
    } else {
        std::ostringstream blocking;
        for (std::size_t index = 0; index < blocking_points.size(); ++index) {
            if (index > 0) {
                blocking << ",";
            }
            blocking << blocking_points[index];
        }
        result.ok = false;
        result.exit_code = 44;
        result.fields["risk_level"] = "medium";
        result.fields["blocking_points"] = blocking.str();
        result.fields["result_summary"] = "basic communication smoke has blocking points";
        result.fields["result"] = "fail";
        result.fields["next_action"] = "restore blocked provider then rerun smoke";
    }

    return result;
}

CommandResult BuildLlamaObserverSmokeResult(
    const AgentConfig & config,
    bool probe,
    const std::string & question) {
    CommandResult health = BuildHealthResult(config);
    CommandResult result;
    result.fields["module"] = "intranet_migration";
    result.fields["action"] = "llama_observer_smoke";
    result.fields["observer_role"] = "trace_codex_mcp_llama_chain_without_bypassing_agent";
    result.fields["chain"] = "CODEX->local_MCP->codex_lan_agent->local_chat->llama_cpp";
    result.fields["mcp_entry"] = config.listen_host + ":" + std::to_string(config.listen_port) + "/mcp";
    result.fields["local_chat_endpoint"] = config.local_chat_endpoint;
    result.fields["generation_endpoint"] = config.generation_endpoint;
    result.fields["embedding_endpoint"] = config.embedding_endpoint;
    result.fields["local_chat_ready"] = GetFieldOrDefault(health, "local_chat_ready", "false");
    result.fields["generation_ready"] = GetFieldOrDefault(health, "generation_ready", "false");
    result.fields["embedding_ready"] = GetFieldOrDefault(health, "embedding_ready", "false");
    result.fields["remote_control_events_path"] = BuildRemoteControlEventsPath(config);
    result.fields["experience_card_path"] = BuildExperienceCardsPath(config);
    result.fields["recommended_entry_tool"] = "rag.query";
    result.fields["recommended_observer_tool"] = "llama.observer_smoke";
    result.fields["recommended_trace_tool"] = "lan_agent_tail_control_events";
    result.fields["probe_requested"] = probe ? "true" : "false";
    result.fields["probe_question"] = question;
    result.fields["success_rule"] =
        "local_chat_ready=true and MCP event contains command_name=llama.observer_smoke or rag.query";
    result.fields["fallback"] =
        "{\"tool\":\"rag.basic_comm_smoke\",\"reason\":\"observer smoke unavailable or provider blocked\"}";

    std::vector<std::string> blocking_points;
    if (result.fields["local_chat_ready"] != "true") {
        blocking_points.push_back("local_chat_not_ready");
    }
    if (result.fields["generation_ready"] != "true") {
        blocking_points.push_back("generation_not_ready");
    }
    if (result.fields["embedding_ready"] != "true") {
        blocking_points.push_back("embedding_not_ready");
    }

    if (probe) {
        const std::string resolved_question = question.empty()
            ? "Reply with exactly: llama_observer_ok"
            : question;
        CommandResult probe_result = RunLocalChat(
            config,
            "llama_observer",
            resolved_question,
            "observer_smoke",
            15000);
        result.fields["probe_ok"] = probe_result.ok ? "true" : "false";
        result.fields["probe_status_code"] = GetFieldOrDefault(probe_result, "status_code", "");
        result.fields["probe_log_path"] = GetFieldOrDefault(probe_result, "log_path", "");
        result.fields["probe_output_text"] = ExtractOutputTextFallback(probe_result);
        result.fields["probe_semantic_outcome"] = GetFieldOrDefault(
            probe_result,
            "semantic_outcome",
            ComputeCommandOutcome(probe_result));
        if (!probe_result.ok) {
            blocking_points.push_back("local_chat_probe_failed");
        }
    } else {
        result.fields["probe_ok"] = "not_run";
        result.fields["probe_log_path"] = "";
        result.fields["probe_output_text"] = "";
        result.fields["next_action"] = "rerun llama.observer_smoke with probe=true for one controlled local_chat call";
    }

    if (blocking_points.empty()) {
        result.fields["risk_level"] = probe ? "low" : "medium";
        result.fields["blocking_points"] = "none";
        result.fields["result"] = probe ? "pass" : "ready_without_probe";
        result.fields["result_summary"] = probe
            ? "CODEX to local MCP to local llama path is observable"
            : "observer chain is ready; probe was not executed";
        if (probe) {
            result.fields["next_action"] = "tail remote_control_events and verify rag.query or observer call ordering";
        }
    } else {
        result.ok = false;
        result.exit_code = 44;
        result.fields["risk_level"] = "medium";
        std::ostringstream blocking;
        for (std::size_t index = 0; index < blocking_points.size(); ++index) {
            if (index > 0) {
                blocking << ",";
            }
            blocking << blocking_points[index];
        }
        result.fields["blocking_points"] = blocking.str();
        result.fields["result"] = "fail";
        result.fields["result_summary"] = "observer chain has blocking points";
        result.fields["next_action"] = "restore blocked local endpoint then rerun observer smoke";
    }
    return result;
}

CommandResult WithComputedOutcome(const CommandResult & original) {
    CommandResult decorated = original;
    decorated.fields["outcome"] = ComputeCommandOutcome(original);
    return decorated;
}

void PrintResultAsText(const CommandResult & result) {
    const CommandResult decorated = WithComputedOutcome(result);
    for (const auto & entry : decorated.fields) {
        std::cout << entry.first << "=" << entry.second << std::endl;
    }
}

std::string ResultToJson(const CommandResult & result) {
    const CommandResult decorated = WithComputedOutcome(result);
    std::ostringstream buffer;
    buffer << "{";
    buffer << "\"ok\":" << (decorated.ok ? "true" : "false");
    buffer << ",\"exit_code\":" << decorated.exit_code;
    for (const auto & entry : decorated.fields) {
        buffer << ",\"" << codex_lan_agent::JsonEscape(entry.first) << "\":\""
               << codex_lan_agent::JsonEscape(entry.second) << "\"";
    }
    buffer << "}";
    return buffer.str();
}

CommandResult RunCliProfile(
    const AgentConfig & config,
    const std::string & profile_name,
    const std::string & extra_arguments,
    const std::string & forced_log_path) {
    CommandResult result;
    result.fields["profile"] = profile_name;
    result.fields["expected_marker"] = ExpectedMarkerForProfile(profile_name);

    const auto it = config.profiles.find(profile_name);
    if (it == config.profiles.end()) {
        result.ok = false;
        result.exit_code = 3;
        result.fields["error"] = "unknown profile";
        return result;
    }

    const std::string resource_key = BuildTaskResourceKey(profile_name, extra_arguments);
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "resource is busy";
        result.fields["resource_key"] = resource_key;
        return result;
    }

    const std::string command_line = extra_arguments.empty()
        ? it->second
        : (it->second + " " + extra_arguments);
    const std::string log_path = forced_log_path.empty()
        ? BuildLogPath(config, profile_name)
        : forced_log_path;

    codex_lan_agent::ProcessRunResult run_result;
    std::string error_message;
    if (!codex_lan_agent::RunCommandWithLog(
            command_line,
            config.workspace_root,
            log_path,
            config.task_timeout_sec,
            &run_result,
            &error_message)) {
        result.ok = false;
        result.exit_code = 4;
        result.fields["error"] = error_message;
        return result;
    }

    result.ok = run_result.exit_code == 0 && !run_result.timed_out;
    result.exit_code = run_result.exit_code;
    result.fields["timed_out"] = run_result.timed_out ? "true" : "false";
    result.fields["stalled"] = run_result.stalled ? "true" : "false";
    result.fields["process_id"] = std::to_string(run_result.process_id);
    result.fields["log_path"] = run_result.log_path;
    result.fields["started_at"] = run_result.started_at;
    result.fields["finished_at"] = run_result.finished_at;
    result.fields["last_output_at"] = run_result.last_output_at;
    if (!resource_key.empty()) {
        result.fields["resource_key"] = resource_key;
    }
    std::string log_content;
    std::string log_read_error;
    if (ReadWholeFile(run_result.log_path, &log_content, &log_read_error)) {
        const std::string semantic_outcome = AnalyzeSemanticOutcome(profile_name, result, log_content);
        result.fields["semantic_outcome"] = semantic_outcome;
        result.fields["expected_marker_verified"] =
            VerifyExpectedMarker(profile_name, result, semantic_outcome, log_content) ? "true" : "false";
    } else {
        result.fields["semantic_outcome"] = result.ok ? "succeeded" : "failed";
        result.fields["expected_marker_verified"] = result.ok ? "true" : "false";
        result.fields["log_read_error"] = log_read_error;
    }
    return result;
}

CommandResult RunCase(
    const AgentConfig & config,
    const std::string & case_path) {
    return RunCliProfile(config, "run_case", "-CasePath \"" + case_path + "\"");
}

CommandResult RunRagFlow(
    const AgentConfig & config,
    const std::string & query,
    const std::string & mode) {
    CommandResult result;
    if (config.generation_endpoint.empty()) {
        result.ok = false;
        result.exit_code = 5;
        result.fields["error"] = "generation_endpoint is not configured";
        return result;
    }

    const std::string body =
        std::string("{\"model\":\"gpt-4.1\",\"temperature\":0,\"messages\":[")
        + "{\"role\":\"system\",\"content\":\"You are a LAN agent for codex-driven internal development. Return concise plain text.\"},"
        + "{\"role\":\"user\",\"content\":\"mode=" + codex_lan_agent::JsonEscape(mode)
        + "\\nquery=" + codex_lan_agent::JsonEscape(query) + "\"}]}";

    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(config.generation_endpoint, body, 10000);

    const std::string log_path = BuildLogPath(config, "run_rag_flow");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << config.generation_endpoint << "\n";
    output << "mode=" << mode << "\n";
    output << "query=" << query << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 6;
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["log_path"] = log_path;
    result.fields["mode"] = mode;
    result.fields["query"] = query;
    result.fields["body"] = response.body;
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }
    return result;
}

CommandResult RunLocalChat(
    const AgentConfig & config,
    const std::string & scope,
    const std::string & question,
    const std::string & mode,
    int timeout_ms) {
    CommandResult result;
    if (config.local_chat_endpoint.empty()) {
        result.ok = false;
        result.exit_code = 42;
        result.fields["error"] = "local_chat_endpoint is not configured";
        return result;
    }

    const std::string resolved_mode = mode.empty() ? "code_analysis" : mode;
    const std::string body =
        std::string("{")
        + "\"scope\":\"" + codex_lan_agent::JsonEscape(scope) + "\","
        + "\"question\":\"" + codex_lan_agent::JsonEscape(question) + "\","
        + "\"mode\":\"" + codex_lan_agent::JsonEscape(resolved_mode) + "\""
        + "}";

    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(config.local_chat_endpoint, body, timeout_ms);

    const std::string log_path = BuildLogPath(config, "call_local_chat");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << config.local_chat_endpoint << "\n";
    output << "scope=" << scope << "\n";
    output << "mode=" << resolved_mode << "\n";
    output << "timeout_ms=" << timeout_ms << "\n";
    output << "question=" << question << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 43;
    result.fields["scope"] = scope;
    result.fields["mode"] = resolved_mode;
    result.fields["question"] = question;
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    const std::string structured_conclusion = ExtractStructuredConclusionRaw(response.body);
    if (!structured_conclusion.empty()) {
        result.fields["structured_conclusion"] = structured_conclusion;
        result.fields["task_state"] = ExtractStructuredConclusionString(response.body, "task_state");
        result.fields["reasoning_level"] = ExtractStructuredConclusionString(response.body, "reasoning_level");
        result.fields["primary_intent"] = ExtractStructuredConclusionString(response.body, "primary_intent");
        result.fields["intent_confidence"] = NormalizeIntentConfidenceRaw(
            ExtractStructuredConclusionRawValue(response.body, "intent_confidence"));
        result.fields["association_scope"] = ExtractStructuredConclusionString(response.body, "association_scope");
        result.fields["next_action"] = ExtractStructuredConclusionString(response.body, "next_action");
        result.fields["session_id"] = ExtractStructuredConclusionString(response.body, "session_id");
        result.fields["turn_id"] = ExtractStructuredConclusionString(response.body, "turn_id");
        result.fields["slice_summary"] = ExtractStructuredConclusionString(response.body, "slice_summary");
        result.fields["expression_keys"] = ExtractStructuredConclusionRawValue(response.body, "expression_keys");
        result.fields["summary"] = ExtractStructuredConclusionString(response.body, "summary");
        result.fields["insufficient_context"] = ToLowerAscii(
            NormalizeIntentConfidenceRaw(ExtractStructuredConclusionRawValue(response.body, "insufficient_context")));
    }
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }
    AddRagEvidenceFields(
        &result,
        scope.empty() ? "local_chat" : scope,
        response.body.substr(0, std::min<std::size_t>(response.body.size(), 2000)),
        !response.ok || response.body.empty(),
        response.ok && !response.body.empty() ? "medium" : "low");
    return result;
}

CommandResult BuildProfileListResult(const AgentConfig & config) {
    CommandResult result;
    int index = 0;
    for (const auto & entry : config.profiles) {
        result.fields["profile_" + std::to_string(index)] = entry.first;
        ++index;
    }
    result.fields["profile_count"] = std::to_string(index);
    return result;
}

TaskManager::TaskManager(const AgentConfig & config)
    : config_(config),
      worker_(&TaskManager::WorkerLoop, this) {
}

TaskManager::~TaskManager() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string TaskManager::StatusTimeStamp() {
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

std::string TaskManager::TaskKindName(TaskKind kind) {
    switch (kind) {
    case TaskKind::kCliProfile:
        return "cli_profile";
    case TaskKind::kCase:
        return "case";
    case TaskKind::kRagFlow:
        return "rag_flow";
    case TaskKind::kLocalChat:
        return "local_chat";
    }
    return "unknown";
}

std::string TaskManager::EnqueueTask(
    TaskKind kind,
    const std::string & arg1,
    const std::string & arg2) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream id_builder;
    id_builder << "task-" << TimeStampForFileName() << "-" << next_id_++;

    TaskRecord record;
    record.task_id = id_builder.str();
    record.kind = kind;
    record.arg1 = arg1;
    record.arg2 = arg2;
    if (kind == TaskKind::kCliProfile) {
        record.resource_key = BuildTaskResourceKey(arg1, arg2);
        record.pending_log_path = BuildLogPath(config_, arg1);
    }
    record.status = "queued";
    record.submitted_at = StatusTimeStamp();

    tasks_[record.task_id] = record;
    pending_ids_.push_back(record.task_id);
    condition_.notify_one();
    return record.task_id;
}

std::string TaskManager::EnqueueCliProfile(const std::string & profile, const std::string & args) {
    return EnqueueTask(TaskKind::kCliProfile, profile, args);
}

std::string TaskManager::EnqueueCase(const std::string & case_path) {
    return EnqueueTask(TaskKind::kCase, case_path, "");
}

std::string TaskManager::EnqueueRagFlow(const std::string & query, const std::string & mode) {
    return EnqueueTask(TaskKind::kRagFlow, query, mode.empty() ? "review" : mode);
}

std::string TaskManager::EnqueueLocalChat(
    const std::string & scope,
    const std::string & question,
    const std::string & mode) {
    return EnqueueTask(
        TaskKind::kLocalChat,
        scope,
        std::string("{\"question\":\"")
            + codex_lan_agent::JsonEscape(question)
            + "\",\"mode\":\""
            + codex_lan_agent::JsonEscape(mode.empty() ? "code_analysis" : mode)
            + "\"}");
}

CommandResult TaskManager::GetTaskResult(const std::string & task_id) const {
    CommandResult result;
    result.fields["task_id"] = task_id;

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        result.ok = false;
        result.exit_code = 40;
        result.fields["error"] = "task not found";
        return result;
    }

    const TaskRecord & record = it->second;
    result.ok = record.status == "succeeded";
    result.exit_code = (record.status == "failed") ? record.result.exit_code : 0;
    result.fields["task_type"] = TaskKindName(record.kind);
    result.fields["status"] = record.status;
    result.fields["submitted_at"] = record.submitted_at;
    result.fields["started_at"] = record.started_at;
    result.fields["completed_at"] = record.completed_at;
    result.fields["queue_depth"] = std::to_string(static_cast<int>(pending_ids_.size()));
    if (!record.arg1.empty()) {
        result.fields["arg1"] = record.arg1;
        result.fields["expected_marker"] = ExpectedMarkerForProfile(record.arg1);
    }
    if (!record.arg2.empty()) {
        result.fields["arg2"] = record.arg2;
    }
    if (!record.resource_key.empty()) {
        result.fields["resource_key"] = record.resource_key;
    }
    if (!record.pending_log_path.empty()) {
        result.fields["log_path"] = record.pending_log_path;
    }
    for (const auto & entry : record.result.fields) {
        result.fields["result_" + entry.first] = entry.second;
    }
    const auto semantic_it = record.result.fields.find("semantic_outcome");
    if (semantic_it != record.result.fields.end()) {
        result.fields["semantic_outcome"] = semantic_it->second;
    }
    const auto expected_it = record.result.fields.find("expected_marker");
    if (expected_it != record.result.fields.end()) {
        result.fields["expected_marker"] = expected_it->second;
    }
    if (record.status == "running" || record.status == "queued") {
        result.ok = true;
        result.exit_code = 0;
    }
    if (record.status == "queued") {
        result.fields["summary"] = "queued";
        result.fields["next_action"] = "wait or query task again";
    } else if (record.status == "running") {
        result.fields["summary"] = "running";
        result.fields["next_action"] = "query task again or tail task log";
    } else {
        const auto stalled_it = record.result.fields.find("stalled");
        const auto timed_out_it = record.result.fields.find("timed_out");
        const auto log_it = record.result.fields.find("log_path");
        const bool stalled =
            stalled_it != record.result.fields.end() && stalled_it->second == "true";
        const bool timed_out =
            timed_out_it != record.result.fields.end() && timed_out_it->second == "true";

        if (stalled) {
            result.fields["summary"] = "stalled; inspect log tail";
            result.fields["next_action"] = "tail task log";
        } else if (timed_out) {
            result.fields["summary"] = "timed out; inspect log tail";
            result.fields["next_action"] = "tail task log";
        } else if (!record.result.ok) {
            result.fields["summary"] = "failed; inspect log tail";
            result.fields["next_action"] = "tail task log";
        } else if (log_it != record.result.fields.end() && !log_it->second.empty()) {
            result.fields["summary"] = "ok; log available";
            result.fields["next_action"] = "none";
        } else {
            result.fields["summary"] = record.result.ok ? "ok" : "failed";
            result.fields["next_action"] = record.result.ok ? "none" : "tail task log";
        }
    }
    return result;
}

CommandResult TaskManager::GetLatestTaskResult() const {
    std::string latest_task_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto & entry : tasks_) {
            if (latest_task_id.empty() || entry.first > latest_task_id) {
                latest_task_id = entry.first;
            }
        }
    }
    if (latest_task_id.empty()) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 40;
        result.fields["error"] = "no task found";
        result.fields["status"] = "empty";
        return result;
    }
    return GetTaskResult(latest_task_id);
}

int TaskManager::QueueDepth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(pending_ids_.size());
}

void TaskManager::PruneCompletedTasksLocked() {
    while (completed_ids_.size() > max_completed_history_) {
        const std::string oldest_id = completed_ids_.front();
        completed_ids_.pop_front();
        const auto it = tasks_.find(oldest_id);
        if (it != tasks_.end() &&
            it->second.status != "queued" &&
            it->second.status != "running") {
            tasks_.erase(it);
        }
    }
}

void TaskManager::WorkerLoop() {
    while (true) {
        TaskRecord task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return stop_ || !pending_ids_.empty(); });
            if (stop_ && pending_ids_.empty()) {
                return;
            }
            const std::string task_id = pending_ids_.front();
            pending_ids_.pop_front();
            TaskRecord & record = tasks_[task_id];
            record.status = "running";
            record.started_at = StatusTimeStamp();
            task = record;
        }

        CommandResult task_result;
        if (task.kind == TaskKind::kCliProfile) {
            task_result = RunCliProfile(config_, task.arg1, task.arg2, task.pending_log_path);
        } else if (task.kind == TaskKind::kCase) {
            task_result = RunCase(config_, task.arg1);
        } else if (task.kind == TaskKind::kLocalChat) {
            task_result = RunLocalChat(
                config_,
                task.arg1,
                ExtractJsonString(task.arg2, "question"),
                ExtractJsonString(task.arg2, "mode"));
        } else {
            task_result = RunRagFlow(config_, task.arg1, task.arg2);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            TaskRecord & record = tasks_[task.task_id];
            record.result = task_result;
            record.completed_at = StatusTimeStamp();
            record.status = task_result.ok ? "succeeded" : "failed";
            completed_ids_.push_back(task.task_id);
            PruneCompletedTasksLocked();
        }
    }
}

std::string ExtractJsonString(
    const std::string & body,
    const std::string & key) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_pos = body.find(pattern);
    if (key_pos == std::string::npos) {
        return "";
    }
    const std::size_t colon_pos = body.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return "";
    }
    const std::size_t quote_start = body.find('"', colon_pos + 1);
    if (quote_start == std::string::npos) {
        return "";
    }

    std::string value;
    bool escaping = false;
    for (std::size_t index = quote_start + 1; index < body.size(); ++index) {
        const char current = body[index];
        if (escaping) {
            switch (current) {
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case '/':
                value.push_back('/');
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                value.push_back(current);
                break;
            }
            escaping = false;
            continue;
        }
        if (current == '\\') {
            escaping = true;
            continue;
        }
        if (current == '"') {
            return value;
        }
        value.push_back(current);
    }
    return "";
}

std::string ExtractJsonRawValue(
    const std::string & body,
    const std::string & key) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_pos = body.find(pattern);
    if (key_pos == std::string::npos) {
        return "";
    }
    std::size_t value_pos = body.find(':', key_pos + pattern.size());
    if (value_pos == std::string::npos) {
        return "";
    }
    ++value_pos;
    while (value_pos < body.size() && std::isspace(static_cast<unsigned char>(body[value_pos])) != 0) {
        ++value_pos;
    }
    if (value_pos >= body.size()) {
        return "";
    }

    if (body[value_pos] == '"') {
        std::size_t index = value_pos + 1;
        bool escaping = false;
        for (; index < body.size(); ++index) {
            const char current = body[index];
            if (escaping) {
                escaping = false;
                continue;
            }
            if (current == '\\') {
                escaping = true;
                continue;
            }
            if (current == '"') {
                return body.substr(value_pos, index - value_pos + 1);
            }
        }
        return "";
    }

    std::size_t end_pos = value_pos;
    while (end_pos < body.size()
           && body[end_pos] != ','
           && body[end_pos] != '}'
           && body[end_pos] != '\r'
           && body[end_pos] != '\n') {
        ++end_pos;
    }
    return Trim(body.substr(value_pos, end_pos - value_pos));
}

std::string ExtractJsonObjectRaw(
    const std::string & body,
    const std::string & key) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_pos = body.find(pattern);
    if (key_pos == std::string::npos) {
        return "";
    }
    std::size_t value_pos = body.find(':', key_pos + pattern.size());
    if (value_pos == std::string::npos) {
        return "";
    }
    ++value_pos;
    while (value_pos < body.size() && std::isspace(static_cast<unsigned char>(body[value_pos])) != 0) {
        ++value_pos;
    }
    if (value_pos >= body.size()) {
        return "";
    }
    const char open_char = body[value_pos];
    const char close_char = open_char == '{' ? '}' : (open_char == '[' ? ']' : '\0');
    if (close_char == '\0') {
        return "";
    }

    bool in_string = false;
    bool escaping = false;
    int depth = 0;
    for (std::size_t index = value_pos; index < body.size(); ++index) {
        const char current = body[index];
        if (in_string) {
            if (escaping) {
                escaping = false;
            } else if (current == '\\') {
                escaping = true;
            } else if (current == '"') {
                in_string = false;
            }
            continue;
        }
        if (current == '"') {
            in_string = true;
            continue;
        }
        if (current == open_char) {
            ++depth;
            continue;
        }
        if (current == close_char) {
            --depth;
            if (depth == 0) {
                return body.substr(value_pos, index - value_pos + 1);
            }
        }
    }
    return "";
}

std::string ExtractStructuredConclusionRaw(const std::string & body) {
    return ExtractJsonObjectRaw(body, "structured_conclusion");
}

std::string ExtractStructuredConclusionString(
    const std::string & body,
    const std::string & key) {
    const std::string structured = ExtractStructuredConclusionRaw(body);
    return structured.empty() ? std::string() : ExtractJsonString(structured, key);
}

std::string ExtractStructuredConclusionRawValue(
    const std::string & body,
    const std::string & key) {
    const std::string structured = ExtractStructuredConclusionRaw(body);
    return structured.empty() ? std::string() : ExtractJsonRawValue(structured, key);
}

std::string BuildMcpErrorResponse(
    const std::string & id_raw,
    int code,
    const std::string & message) {
    std::ostringstream buffer;
    buffer << "{"
           << "\"jsonrpc\":\"2.0\","
           << "\"id\":" << (id_raw.empty() ? "null" : id_raw) << ","
           << "\"error\":{"
           << "\"code\":" << code << ","
           << "\"message\":\"" << codex_lan_agent::JsonEscape(message) << "\""
           << "}}";
    return buffer.str();
}

std::string BuildMcpInitializeResponse(const std::string & id_raw) {
    std::ostringstream buffer;
    buffer << "{"
           << "\"jsonrpc\":\"2.0\","
           << "\"id\":" << (id_raw.empty() ? "null" : id_raw) << ","
           << "\"result\":{"
           << "\"protocolVersion\":\"2025-03-26\","
           << "\"capabilities\":{\"tools\":{}},"
           << "\"serverInfo\":{\"name\":\"codex-lan-agent\",\"version\":\"0.1.0\"}"
           << "}}";
    return buffer.str();
}

std::string BuildMcpCapabilitiesResponse() {
    return "{"
        "\"name\":\"codex-lan-agent\","
        "\"transport\":\"streamable-http-minimal\","
        "\"endpoint\":\"/mcp\","
        "\"methods\":[\"HEAD\",\"GET\",\"POST\"],"
        "\"accept\":[\"application/json\",\"text/event-stream\"],"
        "\"message\":\"use POST /mcp for JSON-RPC requests\""
        "}";
}

std::string BuildMcpDiscoveryBaseUrl(const AgentConfig & config) {
    const std::string host = (config.listen_host.empty() || config.listen_host == "0.0.0.0")
        ? "127.0.0.1"
        : config.listen_host;
    return "http://" + host + ":" + std::to_string(config.listen_port);
}

std::string BuildMcpOAuthAuthorizationServerResponse(const AgentConfig & config) {
    const std::string base_url = BuildMcpDiscoveryBaseUrl(config);
    return "{"
        "\"issuer\":\"" + codex_lan_agent::JsonEscape(base_url) + "\","
        "\"authorization_endpoint\":\"" + codex_lan_agent::JsonEscape(base_url + "/mcp/no-auth") + "\","
        "\"token_endpoint\":\"" + codex_lan_agent::JsonEscape(base_url + "/mcp/no-auth") + "\","
        "\"registration_endpoint\":\"" + codex_lan_agent::JsonEscape(base_url + "/mcp/no-auth") + "\","
        "\"service_documentation\":\"" + codex_lan_agent::JsonEscape(base_url + "/mcp") + "\","
        "\"mcp_endpoint\":\"" + codex_lan_agent::JsonEscape(base_url + "/mcp") + "\","
        "\"auth_required\":false,"
        "\"response_types_supported\":[\"none\"],"
        "\"grant_types_supported\":[\"none\"],"
        "\"scopes_supported\":[],"
        "\"code_challenge_methods_supported\":[],"
        "\"message\":\"codex-lan-agent local MCP does not require OAuth\""
        "}";
}

std::string BuildMcpOAuthProtectedResourceResponse(const AgentConfig & config) {
    const std::string base_url = BuildMcpDiscoveryBaseUrl(config);
    return "{"
        "\"resource\":\"" + codex_lan_agent::JsonEscape(base_url + "/mcp") + "\","
        "\"authorization_servers\":[\"" + codex_lan_agent::JsonEscape(base_url) + "\"],"
        "\"bearer_methods_supported\":[],"
        "\"scopes_supported\":[],"
        "\"auth_required\":false,"
        "\"mcp_endpoint\":\"" + codex_lan_agent::JsonEscape(base_url + "/mcp") + "\","
        "\"message\":\"codex-lan-agent local MCP does not require OAuth\""
        "}";
}

std::string ToLowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return value;
}

std::string GetHeaderValue(const HttpRequest & request, const std::string & name) {
    const auto it = request.headers.find(ToLowerAscii(name));
    return it == request.headers.end() ? std::string() : it->second;
}

std::string ExtractResultField(const std::string & response_body, const std::string & key) {
    const std::string value = ExtractJsonString(response_body, key);
    return value;
}

std::string FirstNonEmpty(
    const std::string & first,
    const std::string & second,
    const std::string & third = std::string()) {
    if (!first.empty()) {
        return first;
    }
    if (!second.empty()) {
        return second;
    }
    return third;
}

std::string ClassifyRemoteCommandName(const HttpRequest & request) {
    if (request.path == "/mcp") {
        const std::string tool_name = ExtractJsonString(request.body, "name");
        if (!tool_name.empty()) {
            if (tool_name == "local_cli" || tool_name == "codex_local_cli") {
                const std::string local_command = ExtractJsonString(request.body, "command");
                if (!local_command.empty()) {
                    return tool_name + ":" + local_command;
                }
            }
            return tool_name;
        }
        const std::string method = ExtractJsonString(request.body, "method");
        return method.empty() ? "mcp" : method;
    }
    if (request.path.rfind("/tasks/", 0) == 0) {
        return "task";
    }
    if (request.path == "/task-log") {
        return "task-log";
    }
    if (request.path == "/health" || request.path == "/healthz") {
        return "health";
    }
    if (request.path.size() > 1 && request.path[0] == '/') {
        return request.path.substr(1);
    }
    return request.path.empty() ? "unknown" : request.path;
}

std::string ClassifyRemoteRequestType(const HttpRequest & request) {
    if (request.path == "/health" || request.path == "/healthz" || request.path == "/runtime-overview") {
        return "health";
    }
    if (request.path == "/mcp") {
        const std::string tool_name = ExtractJsonString(request.body, "name");
        if (tool_name == "local_cli" || tool_name == "codex_local_cli") {
            return "local_cli";
        }
        return "mcp";
    }
    if (request.path.rfind("/tasks/", 0) == 0) {
        return "task";
    }
    if (request.path == "/task-log") {
        return "task_log";
    }
    if (request.path.find("upload") != std::string::npos) {
        return "upload";
    }
    if (request.path.find("download") != std::string::npos) {
        return "download";
    }
    return "action";
}

std::string DefaultRemoteTrigger(const HttpRequest & request) {
    if (request.method == "GET" || request.method == "HEAD" || request.method == "OPTIONS") {
        return "auto";
    }
    return "manual";
}

std::string BuildRemoteControlEventJson(
    const std::unordered_map<std::string, std::string> & fields) {
    std::ostringstream output;
    output << "{";
    bool first = true;
    for (const auto & entry : fields) {
        if (!first) {
            output << ",";
        }
        first = false;
        output << "\"" << codex_lan_agent::JsonEscape(entry.first) << "\":\""
               << codex_lan_agent::JsonEscape(entry.second) << "\"";
    }
    output << "}";
    return output.str();
}

void AppendRemoteControlEvent(
    const AgentConfig & config,
    const HttpRequest & request,
    const HttpResponseSpec & response,
    const std::string & request_started_at,
    const std::string & request_finished_at,
    long long duration_ms) {
    std::unordered_map<std::string, std::string> event;
    const std::string source_thread = FirstNonEmpty(
        GetHeaderValue(request, "x-source-thread"),
        ExtractJsonString(request.body, "source_thread"),
        GetQueryParamValue(request, "source_thread"));
    const std::string trigger = FirstNonEmpty(
        GetHeaderValue(request, "x-trigger"),
        ExtractJsonString(request.body, "trigger"),
        FirstNonEmpty(GetQueryParamValue(request, "trigger"), DefaultRemoteTrigger(request)));
    const std::string task_id = FirstNonEmpty(
        ExtractResultField(response.body, "task_id"),
        ExtractTaskIdFromPath(request.path),
        ExtractJsonString(request.body, "task_id"));
    const std::string status = FirstNonEmpty(
        ExtractResultField(response.body, "status"),
        response.status_text,
        std::to_string(response.status_code));
    const std::string result_ref = FirstNonEmpty(
        ExtractResultField(response.body, "result_log_path"),
        ExtractResultField(response.body, "log_path"),
        FirstNonEmpty(
            ExtractResultField(response.body, "file_path"),
            ExtractResultField(response.body, "experience_card_path")));
    const bool record_to_latest = trigger != "auto";

    event["timestamp"] = request_finished_at;
    event["request_started_at"] = request_started_at;
    event["request_finished_at"] = request_finished_at;
    event["remote_timestamp"] = request_finished_at;
    event["observed_at"] = request_finished_at;
    event["source_thread"] = source_thread.empty() ? "unknown" : source_thread;
    event["target_thread"] = "codex_lan_agent";
    event["message_type"] = "remote_control";
    event["task_id"] = task_id;
    event["status"] = status;
    event["command_name"] = ClassifyRemoteCommandName(request);
    event["entry_name"] = request.method + " " + request.path;
    event["request_type"] = ClassifyRemoteRequestType(request);
    event["duration_ms"] = std::to_string(duration_ms);
    event["summary"] = ExtractResultField(response.body, "summary");
    event["next_action"] = ExtractResultField(response.body, "next_action");
    event["evidence_ref"] = task_id.empty()
        ? ExtractResultField(response.body, "trace_log_path")
        : ("task-log(" + task_id + ")");
    event["result_ref"] = result_ref;
    event["trigger"] = trigger;
    event["record_to_latest"] = record_to_latest ? "true" : "false";
    event["http_status"] = std::to_string(response.status_code);
    event["method"] = request.method;
    event["path"] = request.path;

    const std::string event_path = BuildRemoteControlEventsPath(config);
    {
        std::lock_guard<std::mutex> lock(g_remote_control_event_mutex);
        std::filesystem::create_directories(config.log_root);
        std::ofstream output(event_path, std::ios::out | std::ios::app);
        if (output.is_open()) {
            output << BuildRemoteControlEventJson(event) << "\n";
        }
        g_last_remote_control_event = event;
    }
}

std::string BuildMcpSessionId() {
    static std::mutex mutex;
    static unsigned long long next_id = 1;
    std::lock_guard<std::mutex> lock(mutex);
    return "mcp-" + TimeStampForFileName() + "-" + std::to_string(next_id++);
}

void ApplyMcpSessionHeaders(
    const HttpRequest & request,
    HttpResponseSpec * response,
    bool create_if_missing) {
    std::string session_id = GetHeaderValue(request, "mcp-session-id");
    if (session_id.empty() && create_if_missing) {
        session_id = BuildMcpSessionId();
    }
    if (!session_id.empty()) {
        response->headers["Mcp-Session-Id"] = session_id;
    }
}

void ApplyMcpCorsHeaders(HttpResponseSpec * response) {
    response->headers["Access-Control-Allow-Origin"] = "*";
    response->headers["Access-Control-Allow-Methods"] = "GET, HEAD, OPTIONS, POST";
    response->headers["Access-Control-Allow-Headers"] =
        "Accept, Authorization, Content-Type, Mcp-Session-Id, X-Source-Thread, X-Trigger";
    response->headers["Access-Control-Expose-Headers"] = "Mcp-Session-Id";
}

bool IsMcpOAuthAuthorizationServerPath(const std::string & path) {
    return path == "/.well-known/oauth-authorization-server" ||
           path == "/.well-known/oauth-authorization-server/mcp" ||
           path == "/mcp/.well-known/oauth-authorization-server";
}

bool IsMcpOAuthProtectedResourcePath(const std::string & path) {
    return path == "/.well-known/oauth-protected-resource" ||
           path == "/.well-known/oauth-protected-resource/mcp" ||
           path == "/mcp/.well-known/oauth-protected-resource";
}

bool HandleMcpDiscoveryRoute(
    const AgentConfig & config,
    const HttpRequest & request,
    HttpResponseSpec * response) {
    if (!IsMcpOAuthAuthorizationServerPath(request.path) &&
        !IsMcpOAuthProtectedResourcePath(request.path)) {
        return false;
    }

    ApplyMcpCorsHeaders(response);
    response->status_code = 200;
    response->status_text = "OK";
    response->content_type = "application/json";
    if (request.method == "HEAD" || request.method == "OPTIONS") {
        response->body.clear();
        return true;
    }
    if (request.method != "GET") {
        response->status_code = 405;
        response->status_text = "Method Not Allowed";
        response->body = "{\"error\":\"method not allowed\"}";
        return true;
    }
    response->body = IsMcpOAuthAuthorizationServerPath(request.path)
        ? BuildMcpOAuthAuthorizationServerResponse(config)
        : BuildMcpOAuthProtectedResourceResponse(config);
    return true;
}

enum class McpResponseMode {
    kJson,
    kSse,
    kUnsupported
};

bool HeaderContainsToken(const std::string & header_value, const std::string & token) {
    return ToLowerAscii(header_value).find(ToLowerAscii(token)) != std::string::npos;
}

McpResponseMode NegotiateMcpResponseMode(const HttpRequest & request) {
    const std::string accept = GetHeaderValue(request, "accept");
    if (accept.empty() ||
        HeaderContainsToken(accept, "*/*") ||
        HeaderContainsToken(accept, "application/json")) {
        if (HeaderContainsToken(accept, "text/event-stream") &&
            !HeaderContainsToken(accept, "application/json")) {
            return McpResponseMode::kSse;
        }
        return McpResponseMode::kJson;
    }
    if (HeaderContainsToken(accept, "text/event-stream")) {
        return McpResponseMode::kSse;
    }
    return McpResponseMode::kUnsupported;
}

std::string BuildSseMessage(const std::string & json_body) {
    std::ostringstream output;
    output << "event: message\n";
    std::istringstream input(json_body);
    std::string line;
    bool wrote_line = false;
    while (std::getline(input, line)) {
        output << "data: " << line << "\n";
        wrote_line = true;
    }
    if (!wrote_line) {
        output << "data: " << json_body << "\n";
    }
    output << "\n";
    return output.str();
}

void ApplyMcpTransport(HttpResponseSpec * response, McpResponseMode mode) {
    if (mode == McpResponseMode::kSse && !response->body.empty()) {
        response->body = BuildSseMessage(response->body);
        response->content_type = "text/event-stream";
    } else {
        response->content_type = "application/json";
    }
}

std::string BuildMcpToolsListResponse(
    const std::string & id_raw,
    const AgentConfig & config) {
    std::ostringstream buffer;
    buffer << "{"
           << "\"jsonrpc\":\"2.0\","
           << "\"id\":" << (id_raw.empty() ? "null" : id_raw) << ","
           << "\"result\":{\"tools\":[";

    buffer
        << "{"
        << "\"name\":\"lan_agent_health\","
        << "\"description\":\"Get codex_lan_agent health and endpoint readiness.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_list_profiles\","
        << "\"description\":\"List configured CLI profiles on the LAN agent.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_runtime_overview\","
        << "\"description\":\"Get one compact overview of agent health, queue depth, endpoints, and configured profiles.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_run_cli_profile\","
        << "\"description\":\"Run one safe lightweight CLI profile exposed by the LAN agent.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"profile\":{\"type\":\"string\"},"
        << "\"args\":{\"type\":\"string\"}"
        << "},\"required\":[\"profile\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_enqueue_cli_profile\","
        << "\"description\":\"Queue one CLI profile to run asynchronously on the LAN agent and return a task id.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"profile\":{\"type\":\"string\"},"
        << "\"args\":{\"type\":\"string\"}"
        << "},\"required\":[\"profile\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_run_case\","
        << "\"description\":\"Run the remote case preview/profile for one cxsc case path.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"case_path\":{\"type\":\"string\"}"
        << "},\"required\":[\"case_path\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_enqueue_case\","
        << "\"description\":\"Queue one remote case run and return a task id.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"case_path\":{\"type\":\"string\"}"
        << "},\"required\":[\"case_path\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_run_rag_flow\","
        << "\"description\":\"Send one generation request through the remote RAG flow endpoint.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"query\":{\"type\":\"string\"},"
        << "\"mode\":{\"type\":\"string\"}"
        << "},\"required\":[\"query\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_enqueue_rag_flow\","
        << "\"description\":\"Queue one generation request through the remote RAG flow endpoint and return a task id.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"query\":{\"type\":\"string\"},"
        << "\"mode\":{\"type\":\"string\"}"
        << "},\"required\":[\"query\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_run_local_chat\","
        << "\"description\":\"Call the remote local chat gateway for project-scoped code analysis.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"scope\":{\"type\":\"string\"},"
        << "\"question\":{\"type\":\"string\"},"
        << "\"mode\":{\"type\":\"string\"}"
        << "},\"required\":[\"scope\",\"question\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_enqueue_local_chat\","
        << "\"description\":\"Queue one remote local chat request and return a task id.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"scope\":{\"type\":\"string\"},"
        << "\"question\":{\"type\":\"string\"},"
        << "\"mode\":{\"type\":\"string\"}"
        << "},\"required\":[\"scope\",\"question\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"rag.query\","
        << "\"description\":\"Run one RAG/local-chat query through the configured local-chat gateway.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"query\":{\"type\":\"string\"},"
        << "\"scope\":{\"type\":\"string\"},"
        << "\"mode\":{\"type\":\"string\"}"
        << "},\"required\":[\"query\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"llama.observer_smoke\","
        << "\"description\":\"Observe the CODEX to local MCP to local llama.cpp chain; probe=false is read-only, probe=true performs one controlled local-chat call.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"probe\":{\"type\":\"boolean\"},"
        << "\"question\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_optfile_read\","
        << "\"description\":\"Read one short-link optfile runtime file under log_root/optfile_runtime for test/debug observation.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"target_name\":{\"type\":\"string\"},"
        << "\"max_bytes\":{\"type\":\"integer\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_optfile_write_preview\","
        << "\"description\":\"Preview a short-link optfile write under log_root/optfile_runtime without writing.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"target_name\":{\"type\":\"string\"},"
        << "\"data\":{\"type\":\"string\"},"
        << "\"append\":{\"type\":\"boolean\"}"
        << "},\"required\":[\"data\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_optfile_apply_write\","
        << "\"description\":\"Apply a controlled short-link optfile write under log_root/optfile_runtime and return audit evidence.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"target_name\":{\"type\":\"string\"},"
        << "\"data\":{\"type\":\"string\"},"
        << "\"append\":{\"type\":\"boolean\"}"
        << "},\"required\":[\"data\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_record_dialog_slice\","
        << "\"description\":\"Append one local AI dialog turn into log_root/dialog_slices as JSONL for later retrieval and analysis.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"session_id\":{\"type\":\"string\"},"
        << "\"turn_id\":{\"type\":\"string\"},"
        << "\"user_text\":{\"type\":\"string\"},"
        << "\"assistant_text\":{\"type\":\"string\"},"
        << "\"tags\":{\"type\":\"string\"}"
        << "},\"required\":[\"session_id\",\"turn_id\",\"user_text\",\"assistant_text\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_analyze_dialog_slices\","
        << "\"description\":\"Inspect stored dialog_slices files and return the latest session/file tail for semantic review.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"session_id\":{\"type\":\"string\"},"
        << "\"max_entries\":{\"type\":\"integer\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_allocate_remote_chat_session\","
        << "\"description\":\"Create or enter one remote chat session before execution and write a session registry record.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"thread_name\":{\"type\":\"string\"},"
        << "\"module_name\":{\"type\":\"string\"},"
        << "\"reasoning_level\":{\"type\":\"string\"},"
        << "\"task_state\":{\"type\":\"string\"},"
        << "\"short_goal\":{\"type\":\"string\"},"
        << "\"task_id\":{\"type\":\"string\"},"
        << "\"session_title\":{\"type\":\"string\"},"
        << "\"parent_session_id\":{\"type\":\"string\"},"
        << "\"dispatch_mode\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_build_semantic_execution_card\","
        << "\"description\":\"Build one semantic execution card for remote dispatch and writeback planning.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"thread_name\":{\"type\":\"string\"},"
        << "\"session_title\":{\"type\":\"string\"},"
        << "\"task_id\":{\"type\":\"string\"},"
        << "\"task_state\":{\"type\":\"string\"},"
        << "\"reasoning_level\":{\"type\":\"string\"},"
        << "\"primary_intent\":{\"type\":\"string\"},"
        << "\"secondary_intents\":{\"type\":\"string\"},"
        << "\"scope_modules\":{\"type\":\"string\"},"
        << "\"expected_output\":{\"type\":\"string\"},"
        << "\"evidence_required\":{\"type\":\"string\"},"
        << "\"writeback_required\":{\"type\":\"string\"},"
        << "\"next_action_if_blocked\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"local_cli\"," 
        << "\"description\":\"Execute one whitelisted codex_lan_agent local CLI command and return JSON result/evidence/fallback/trace fields.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"command\":{\"type\":\"string\"},"
        << "\"args\":{\"type\":\"object\",\"description\":\"Optional nested argument object; flat arguments are also accepted for compatibility.\"},"
        << "\"task_id\":{\"type\":\"string\"},"
        << "\"repo_root\":{\"type\":\"string\"},"
        << "\"action_id\":{\"type\":\"string\"},"
        << "\"build_dir\":{\"type\":\"string\"},"
        << "\"target\":{\"type\":\"string\"},"
        << "\"config\":{\"type\":\"string\"},"
        << "\"log_path\":{\"type\":\"string\"},"
        << "\"args_text\":{\"type\":\"string\"},"
        << "\"dry_run\":{\"type\":\"boolean\"}"
        << "},\"required\":[\"command\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"codex_local_cli\","
        << "\"description\":\"Compatibility alias for local_cli. Use the same command and args schema, with traceable JSON output.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"command\":{\"type\":\"string\"},"
        << "\"args\":{\"type\":\"object\",\"description\":\"Optional nested argument object; flat arguments are also accepted for compatibility.\"},"
        << "\"task_id\":{\"type\":\"string\"},"
        << "\"repo_root\":{\"type\":\"string\"},"
        << "\"action_id\":{\"type\":\"string\"},"
        << "\"build_dir\":{\"type\":\"string\"},"
        << "\"target\":{\"type\":\"string\"},"
        << "\"config\":{\"type\":\"string\"},"
        << "\"log_path\":{\"type\":\"string\"},"
        << "\"args_text\":{\"type\":\"string\"},"
        << "\"dry_run\":{\"type\":\"boolean\"}"
        << "},\"required\":[\"command\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"router_domain_map\","
        << "\"description\":\"Return Router capability domains with tool whitelist and local_cli command whitelist.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"domain\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"dispatch_contract_map\","
        << "\"description\":\"Return the frozen internal dispatch contract tables for task_state, reasoning_level, primary_intent, slots, session objects, action chains, and fallback rules.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"table_name\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"intent_dispatch_prepare\","
        << "\"description\":\"Consume structured model intent output as an internal enhancement layer, then prepare the next MCP tool call with automatic legacy fallback.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"task_state\":{\"type\":\"string\"},"
        << "\"reasoning_level\":{\"type\":\"string\"},"
        << "\"primary_intent\":{\"type\":\"string\"},"
        << "\"secondary_intents\":{\"type\":\"string\"},"
        << "\"intent_confidence\":{\"type\":\"string\"},"
        << "\"association_scope\":{\"type\":\"string\"},"
        << "\"entity_refs\":{\"type\":\"string\"},"
        << "\"evidence_refs\":{\"type\":\"string\"},"
        << "\"risk_flags\":{\"type\":\"string\"},"
        << "\"next_action\":{\"type\":\"string\"},"
        << "\"session_id\":{\"type\":\"string\"},"
        << "\"turn_id\":{\"type\":\"string\"},"
        << "\"slice_summary\":{\"type\":\"string\"},"
        << "\"expression_keys\":{\"type\":\"string\"},"
        << "\"summary\":{\"type\":\"string\"},"
        << "\"insufficient_context\":{\"type\":\"boolean\"},"
        << "\"structured_conclusion\":{\"type\":\"object\",\"description\":\"Optional top-level structured conclusion object from llama.cpp chat response; nested fields override flat arguments.\"},"
        << "\"query\":{\"type\":\"string\"},"
        << "\"arguments_text\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"semantic_action_map\","
        << "\"description\":\"Return the standard semantic action shortcuts that map user intent to codex_lan_agent tools or endpoints.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"action_id\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"tool_shortcuts\","
        << "\"description\":\"Compatibility alias for semantic_action_map.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"action_id\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"mcp_actions\","
        << "\"description\":\"Compatibility alias for semantic_action_map.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"action_id\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"semantic_action_resolve\","
        << "\"description\":\"Resolve a natural language query or action_id into one standard semantic action without executing it.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"action_id\":{\"type\":\"string\"},"
        << "\"query\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"semantic_action_validate\","
        << "\"description\":\"Validate semantic action arguments and side-effect risk without executing the action.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"action_id\":{\"type\":\"string\"},"
        << "\"arguments_text\":{\"type\":\"string\"}"
        << "},\"required\":[\"action_id\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"semantic_action_prepare\","
        << "\"description\":\"Resolve and validate one semantic action in a single non-executing preflight call.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"action_id\":{\"type\":\"string\"},"
        << "\"query\":{\"type\":\"string\"},"
        << "\"arguments_text\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"semantic_action_tool_call\","
        << "\"description\":\"Build a non-executing MCP tools/call JSON template for one prepared semantic action.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"action_id\":{\"type\":\"string\"},"
        << "\"query\":{\"type\":\"string\"},"
        << "\"arguments_text\":{\"type\":\"string\"},"
        << "\"prefer_dry_run\":{\"type\":\"boolean\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"rag.log_classify\","
        << "\"description\":\"Classify a log by file_path, task_id, or inline log_text and return semantic evidence fields.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"file_path\":{\"type\":\"string\"},"
        << "\"task_id\":{\"type\":\"string\"},"
        << "\"log_text\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"rag.diff_review\","
        << "\"description\":\"Review inline diff_text or fall back to a workspace snapshot diff with evidence fields.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"diff_text\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"rag.basic_comm_smoke\","
        << "\"description\":\"Return one compact ASCII-only RAG integration communication smoke report.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_basic_comm_smoke\","
        << "\"description\":\"Compatibility alias for rag.basic_comm_smoke.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_get_task\","
        << "\"description\":\"Get the current status and result fields for one queued LAN agent task.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"task_id\":{\"type\":\"string\"}"
        << "},\"required\":[\"task_id\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_task_log\","
        << "\"description\":\"Read the tail of the log for one queued or completed LAN agent task.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"task_id\":{\"type\":\"string\"},"
        << "\"max_lines\":{\"type\":\"integer\"}"
        << "},\"required\":[\"task_id\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_check_build_dir\","
        << "\"description\":\"Run the configured check_build_dir profile for one build directory.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"build_dir\":{\"type\":\"string\"}"
        << "},\"required\":[\"build_dir\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_prepare_build_dir\","
        << "\"description\":\"Run the configured prepare_build_dir profile for one build directory.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"build_dir\":{\"type\":\"string\"},"
        << "\"create_if_missing\":{\"type\":\"boolean\"}"
        << "},\"required\":[\"build_dir\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_build_target\","
        << "\"description\":\"Queue the configured build_target profile for one build directory and target.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"build_dir\":{\"type\":\"string\"},"
        << "\"target\":{\"type\":\"string\"},"
        << "\"config\":{\"type\":\"string\"},"
        << "\"dry_run\":{\"type\":\"boolean\"},"
        << "\"validate_args\":{\"type\":\"boolean\"}"
        << "},\"required\":[\"build_dir\",\"target\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_configure_project\","
        << "\"description\":\"Queue the configured configure_project profile for one project root and build directory.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"project_root\":{\"type\":\"string\"},"
        << "\"build_dir\":{\"type\":\"string\"},"
        << "\"generator_kind\":{\"type\":\"string\"},"
        << "\"cmake_args\":{\"type\":\"string\"},"
        << "\"env\":{\"type\":\"string\"}"
        << "},\"required\":[\"project_root\",\"build_dir\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_run_ctest_target\","
        << "\"description\":\"Queue the configured run_ctest_target profile for one build directory and test regex.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"build_dir\":{\"type\":\"string\"},"
        << "\"test_regex\":{\"type\":\"string\"},"
        << "\"config\":{\"type\":\"string\"}"
        << "},\"required\":[\"build_dir\",\"test_regex\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_discover_logs\","
        << "\"description\":\"Discover recent agent log files without using the task queue.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"max_entries\":{\"type\":\"integer\"},"
        << "\"tail_lines\":{\"type\":\"integer\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_tail_control_events\","
        << "\"description\":\"Read the recent remote_control_events.jsonl records written by codex_lan_agent.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"max_lines\":{\"type\":\"integer\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_preview_patch\","
        << "\"description\":\"Preview a single-file replacement diff under workspace_root without writing to disk.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"file_path\":{\"type\":\"string\"},"
        << "\"new_content\":{\"type\":\"string\"}"
        << "},\"required\":[\"file_path\",\"new_content\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_apply_single_file_patch\","
        << "\"description\":\"Apply a single-file replacement under workspace_root, return diff, and write an audit log.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"file_path\":{\"type\":\"string\"},"
        << "\"new_content\":{\"type\":\"string\"}"
        << "},\"required\":[\"file_path\",\"new_content\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_revert_single_file_patch\","
        << "\"description\":\"Revert a single-file replacement from a backup_path produced by apply_single_file_patch.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"file_path\":{\"type\":\"string\"},"
        << "\"backup_path\":{\"type\":\"string\"}"
        << "},\"required\":[\"file_path\",\"backup_path\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_snapshot_diff\","
        << "\"description\":\"Create a read-only Git snapshot diff log from workspace_root for browser-safe review.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"repo_root\":{\"type\":\"string\"}"
        << "},\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_read_text_file\","
        << "\"description\":\"Read a text file under the remote workspace or codex-lan-agent logs root. Useful for fetching remote build logs and diagnostics.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"file_path\":{\"type\":\"string\"},"
        << "\"max_lines\":{\"type\":\"integer\"}"
        << "},\"required\":[\"file_path\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_tail_text_file\","
        << "\"description\":\"Read the tail of a text file under the remote workspace or codex-lan-agent logs root. Useful for polling the latest remote build and watchdog log output.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"file_path\":{\"type\":\"string\"},"
        << "\"max_lines\":{\"type\":\"integer\"}"
        << "},\"required\":[\"file_path\"],\"additionalProperties\":false}"
        << "},"
        << "{"
        << "\"name\":\"lan_agent_list_directory\","
        << "\"description\":\"List a directory under the remote workspace or codex-lan-agent logs root. Useful for discovering remote build folders and log files.\","
        << "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        << "\"directory_path\":{\"type\":\"string\"},"
        << "\"max_entries\":{\"type\":\"integer\"}"
        << "},\"required\":[\"directory_path\"],\"additionalProperties\":false}"
        << "}";

    if (!config.profiles.empty()) {
        buffer << ",";
        buffer
            << "{"
            << "\"name\":\"lan_agent_profile_catalog\","
            << "\"description\":\"Return the configured profile catalog from the remote LAN agent.\","
            << "\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
            << "}";
    }

    buffer << "]}}";
    return buffer.str();
}

std::string BuildMcpToolCallResponse(
    const std::string & id_raw,
    const CommandResult & result) {
    const CommandResult decorated = WithComputedOutcome(result);
    std::ostringstream text;
    for (const auto & entry : decorated.fields) {
        text << entry.first << "=" << entry.second << "\n";
    }

    std::ostringstream buffer;
    buffer << "{"
           << "\"jsonrpc\":\"2.0\","
           << "\"id\":" << (id_raw.empty() ? "null" : id_raw) << ","
           << "\"result\":{"
           << "\"content\":[{\"type\":\"text\",\"text\":\""
           << codex_lan_agent::JsonEscape(text.str())
           << "\"}],"
           << "\"structuredContent\":" << ResultToJson(decorated)
           << ","
           << "\"isError\":" << (decorated.ok ? "false" : "true")
           << "}}";
    return buffer.str();
}

bool HandleMcpRoute(
    const AgentConfig & config,
    const HttpRequest & request,
    HttpResponseSpec * response) {
    if (request.path != "/mcp") {
        return false;
    }

    ApplyMcpCorsHeaders(response);

    if (request.method == "OPTIONS") {
        response->status_code = 204;
        response->status_text = "No Content";
        response->content_type = "application/json";
        response->body.clear();
        ApplyMcpSessionHeaders(request, response, false);
        return true;
    }

    if (request.method == "HEAD") {
        response->status_code = 204;
        response->status_text = "No Content";
        response->content_type = "application/json";
        response->body.clear();
        ApplyMcpSessionHeaders(request, response, false);
        return true;
    }

    if (request.method == "GET") {
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = BuildMcpCapabilitiesResponse();
        ApplyMcpSessionHeaders(request, response, false);
        return true;
    }

    if (request.method != "POST") {
        response->status_code = 405;
        response->status_text = "Method Not Allowed";
        response->body = "{\"error\":\"method not allowed\"}";
        return true;
    }

    const McpResponseMode response_mode = NegotiateMcpResponseMode(request);
    if (response_mode == McpResponseMode::kUnsupported) {
        response->status_code = 406;
        response->status_text = "Not Acceptable";
        response->body = "{\"error\":\"unsupported accept\"}";
        return true;
    }

    const std::string id_raw = ExtractJsonRawValue(request.body, "id");
    const std::string method = ExtractJsonString(request.body, "method");

    if (method.empty()) {
        response->status_code = 400;
        response->status_text = "Bad Request";
        response->body = BuildMcpErrorResponse(id_raw, -32600, "missing method");
        ApplyMcpSessionHeaders(request, response, false);
        ApplyMcpTransport(response, response_mode);
        return true;
    }

    if (method == "initialize") {
        response->body = BuildMcpInitializeResponse(id_raw);
        ApplyMcpSessionHeaders(request, response, true);
        ApplyMcpTransport(response, response_mode);
        return true;
    }

    if (method == "notifications/initialized") {
        response->status_code = 202;
        response->status_text = "Accepted";
        response->body.clear();
        ApplyMcpSessionHeaders(request, response, false);
        return true;
    }

    if (method == "tools/list") {
        response->body = BuildMcpToolsListResponse(id_raw, config);
        ApplyMcpSessionHeaders(request, response, false);
        ApplyMcpTransport(response, response_mode);
        return true;
    }

    if (method == "tools/call") {
        const std::string tool_name = ExtractJsonString(request.body, "name");
        CommandResult result;

        if (tool_name == "lan_agent_health") {
            result = BuildHealthResult(config);
        } else if (tool_name == "lan_agent_runtime_overview") {
            result = BuildRuntimeOverviewResult(config);
        } else if (tool_name == "lan_agent_list_profiles" || tool_name == "lan_agent_profile_catalog") {
            result = BuildProfileListResult(config);
        } else if (tool_name == "lan_agent_run_cli_profile") {
            const std::string profile = ExtractJsonString(request.body, "profile");
            if (!IsSafeLightProfile(profile)) {
                result.ok = false;
                result.exit_code = 45;
                result.fields["error"] = "profile is not in safe_profile_allowlist";
                result.fields["profile"] = profile;
                result.fields["safe_profile_allowlist"] = "check_build_dir,run_script,run_local_chat";
                result.fields["fallback"] = "{\"tool\":\"lan_agent_enqueue_cli_profile\",\"reason\":\"profile is not lightweight\"}";
            } else {
                result = RunCliProfile(
                    config,
                    profile,
                    ExtractJsonString(request.body, "args"));
            }
        } else if (tool_name == "lan_agent_enqueue_cli_profile") {
            if (g_task_manager == nullptr) {
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
            } else {
                result.fields["task_id"] = g_task_manager->EnqueueCliProfile(
                    ExtractJsonString(request.body, "profile"),
                    ExtractJsonString(request.body, "args"));
                result.fields["status"] = "queued";
                result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
            }
        } else if (tool_name == "lan_agent_run_case") {
            result = RunCase(config, ExtractJsonString(request.body, "case_path"));
        } else if (tool_name == "lan_agent_enqueue_case") {
            if (g_task_manager == nullptr) {
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
            } else {
                result.fields["task_id"] = g_task_manager->EnqueueCase(
                    ExtractJsonString(request.body, "case_path"));
                result.fields["status"] = "queued";
                result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
            }
        } else if (tool_name == "lan_agent_run_rag_flow") {
            std::string mode = ExtractJsonString(request.body, "mode");
            if (mode.empty()) {
                mode = "review";
            }
            result = RunRagFlow(config, ExtractJsonString(request.body, "query"), mode);
        } else if (tool_name == "lan_agent_enqueue_rag_flow") {
            if (g_task_manager == nullptr) {
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
            } else {
                result.fields["task_id"] = g_task_manager->EnqueueRagFlow(
                    ExtractJsonString(request.body, "query"),
                    ExtractJsonString(request.body, "mode"));
                result.fields["status"] = "queued";
                result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
            }
        } else if (tool_name == "lan_agent_run_local_chat") {
            std::string mode = ExtractJsonString(request.body, "mode");
            if (mode.empty()) {
                mode = "code_analysis";
            }
            result = RunLocalChat(
                config,
                ExtractJsonString(request.body, "scope"),
                ExtractJsonString(request.body, "question"),
                mode);
        } else if (tool_name == "lan_agent_enqueue_local_chat") {
            if (g_task_manager == nullptr) {
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
            } else {
                result.fields["task_id"] = g_task_manager->EnqueueLocalChat(
                    ExtractJsonString(request.body, "scope"),
                    ExtractJsonString(request.body, "question"),
                    ExtractJsonString(request.body, "mode"));
                result.fields["status"] = "queued";
                result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
            }
        } else if (tool_name == "rag.query") {
            std::string scope = ExtractJsonString(request.body, "scope");
            std::string mode = ExtractJsonString(request.body, "mode");
            if (scope.empty()) {
                scope = "workspace";
            }
            if (mode.empty()) {
                mode = "rag_query";
            }
            std::string query = ExtractJsonString(request.body, "query");
            std::filesystem::path file_scope_path;
            if (LooksLikeFileScope(config, scope, &file_scope_path)) {
                std::string file_content;
                std::string read_error;
                if (ReadWholeFile(file_scope_path, &file_content, &read_error)) {
                    if (file_content.size() > 8000) {
                        file_content = file_content.substr(0, 8000);
                    }
                    query += "\n\n[file_scope=" + file_scope_path.string() + "]\n" + file_content;
                }
            }
            result = RunLocalChat(
                config,
                scope,
                query,
                mode);
        } else if (tool_name == "llama.observer_smoke") {
            result = BuildLlamaObserverSmokeResult(
                config,
                ExtractJsonBool(request.body, "probe", false),
                ExtractJsonString(request.body, "question"));
        } else if (tool_name == "lan_agent_optfile_read") {
            int max_bytes = 65536;
            const std::string max_bytes_raw = ExtractJsonRawValue(request.body, "max_bytes");
            if (!max_bytes_raw.empty()) {
                const int parsed_max_bytes = std::atoi(max_bytes_raw.c_str());
                max_bytes = parsed_max_bytes > 0 ? parsed_max_bytes : 1;
            }
            result = OptFileReadResult(
                config,
                ExtractJsonString(request.body, "target_name"),
                max_bytes);
        } else if (tool_name == "lan_agent_optfile_write_preview") {
            result = OptFileWritePreviewResult(
                config,
                ExtractJsonString(request.body, "target_name"),
                ExtractJsonString(request.body, "data"),
                ExtractJsonBool(request.body, "append", false));
        } else if (tool_name == "lan_agent_optfile_apply_write") {
            result = OptFileApplyWriteResult(
                config,
                ExtractJsonString(request.body, "target_name"),
                ExtractJsonString(request.body, "data"),
                ExtractJsonBool(request.body, "append", false));
        } else if (tool_name == "lan_agent_record_dialog_slice") {
            result = RecordDialogSliceResult(
                config,
                ExtractJsonString(request.body, "session_id"),
                ExtractJsonString(request.body, "turn_id"),
                ExtractJsonString(request.body, "user_text"),
                ExtractJsonString(request.body, "assistant_text"),
                ExtractJsonString(request.body, "tags"));
        } else if (tool_name == "lan_agent_analyze_dialog_slices") {
            int max_entries = 20;
            const std::string max_entries_raw = ExtractJsonRawValue(request.body, "max_entries");
            if (!max_entries_raw.empty()) {
                const int parsed_max_entries = std::atoi(max_entries_raw.c_str());
                max_entries = parsed_max_entries > 0 ? parsed_max_entries : 1;
            }
            result = AnalyzeDialogSlicesResult(
                config,
                ExtractJsonString(request.body, "session_id"),
                max_entries);
        } else if (tool_name == "lan_agent_allocate_remote_chat_session") {
            result = AllocateRemoteChatSessionResult(
                config,
                ExtractJsonString(request.body, "thread_name"),
                ExtractJsonString(request.body, "module_name"),
                ExtractJsonString(request.body, "reasoning_level"),
                ExtractJsonString(request.body, "task_state"),
                ExtractJsonString(request.body, "short_goal"),
                ExtractJsonString(request.body, "task_id"),
                ExtractJsonString(request.body, "session_title"),
                ExtractJsonString(request.body, "parent_session_id"),
                ExtractJsonString(request.body, "dispatch_mode"));
        } else if (tool_name == "lan_agent_build_semantic_execution_card") {
            result = BuildSemanticExecutionCardResult(
                ExtractJsonString(request.body, "thread_name"),
                ExtractJsonString(request.body, "session_title"),
                ExtractJsonString(request.body, "task_id"),
                ExtractJsonString(request.body, "task_state"),
                ExtractJsonString(request.body, "reasoning_level"),
                ExtractJsonString(request.body, "primary_intent"),
                ExtractJsonString(request.body, "secondary_intents"),
                ExtractJsonString(request.body, "scope_modules"),
                ExtractJsonString(request.body, "expected_output"),
                ExtractJsonString(request.body, "evidence_required"),
                ExtractJsonString(request.body, "writeback_required"),
                ExtractJsonString(request.body, "next_action_if_blocked"));
        } else if (tool_name == "local_cli" || tool_name == "codex_local_cli") {
            result = LocalCliResult(
                config,
                ExtractJsonString(request.body, "command"),
                ExtractJsonString(request.body, "task_id"),
                ExtractJsonString(request.body, "repo_root"),
                ExtractJsonString(request.body, "action_id"),
                ExtractJsonString(request.body, "build_dir"),
                ExtractJsonString(request.body, "target"),
                ExtractJsonString(request.body, "config"),
                ExtractJsonString(request.body, "log_path"),
                ExtractJsonString(request.body, "args_text"),
                ExtractJsonBool(request.body, "dry_run", false));
        } else if (tool_name == "router_domain_map") {
            result = BuildRouterDomainMapResult(ExtractJsonString(request.body, "domain"));
        } else if (tool_name == "dispatch_contract_map") {
            result = BuildDispatchContractMapResult(
                ExtractJsonString(request.body, "table_name"));
        } else if (tool_name == "intent_dispatch_prepare") {
            result = BuildIntentDispatchPrepareResult(
                config,
                FirstStructuredFieldString(request.body, "task_state", ExtractJsonString(request.body, "task_state")),
                FirstStructuredFieldString(request.body, "reasoning_level", ExtractJsonString(request.body, "reasoning_level")),
                FirstStructuredFieldString(request.body, "primary_intent", ExtractJsonString(request.body, "primary_intent")),
                FirstStructuredFieldRaw(request.body, "secondary_intents", ExtractJsonString(request.body, "secondary_intents")),
                FirstStructuredFieldRaw(request.body, "intent_confidence", ExtractJsonRawValue(request.body, "intent_confidence")),
                FirstStructuredFieldString(request.body, "association_scope", ExtractJsonString(request.body, "association_scope")),
                FirstStructuredFieldRaw(request.body, "entity_refs", ExtractJsonString(request.body, "entity_refs")),
                FirstStructuredFieldRaw(request.body, "evidence_refs", ExtractJsonString(request.body, "evidence_refs")),
                FirstStructuredFieldRaw(request.body, "risk_flags", ExtractJsonString(request.body, "risk_flags")),
                FirstStructuredFieldString(request.body, "next_action", ExtractJsonString(request.body, "next_action")),
                FirstStructuredFieldString(request.body, "session_id", ExtractJsonString(request.body, "session_id")),
                FirstStructuredFieldString(request.body, "turn_id", ExtractJsonString(request.body, "turn_id")),
                FirstStructuredFieldString(request.body, "slice_summary", ExtractJsonString(request.body, "slice_summary")),
                FirstStructuredFieldRaw(request.body, "expression_keys", ExtractJsonString(request.body, "expression_keys")),
                FirstStructuredFieldString(request.body, "summary", ExtractJsonString(request.body, "summary")),
                ToLowerAscii(FirstStructuredFieldRaw(request.body, "insufficient_context", ExtractJsonRawValue(request.body, "insufficient_context"))) == "true",
                FirstStructuredFieldString(request.body, "query", ExtractJsonString(request.body, "query")),
                FirstStructuredFieldString(request.body, "arguments_text", ExtractJsonString(request.body, "arguments_text")));
        } else if (tool_name == "semantic_action_map" ||
                   tool_name == "tool_shortcuts" ||
                   tool_name == "mcp_actions") {
            result = BuildSemanticActionMapResult(ExtractJsonString(request.body, "action_id"));
        } else if (tool_name == "semantic_action_resolve") {
            result = BuildSemanticActionResolveResult(
                ExtractJsonString(request.body, "action_id"),
                ExtractJsonString(request.body, "query"));
        } else if (tool_name == "semantic_action_validate") {
            result = BuildSemanticActionValidateResult(
                ExtractJsonString(request.body, "action_id"),
                ExtractJsonString(request.body, "arguments_text"));
        } else if (tool_name == "semantic_action_prepare") {
            result = BuildSemanticActionPrepareResult(
                ExtractJsonString(request.body, "action_id"),
                ExtractJsonString(request.body, "query"),
                ExtractJsonString(request.body, "arguments_text"));
        } else if (tool_name == "semantic_action_tool_call") {
            result = BuildSemanticActionToolCallResult(
                ExtractJsonString(request.body, "action_id"),
                ExtractJsonString(request.body, "query"),
                ExtractJsonString(request.body, "arguments_text"),
                ExtractJsonBool(request.body, "prefer_dry_run", false));
        } else if (tool_name == "rag.log_classify") {
            result = RagLogClassifyResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "task_id"),
                ExtractJsonString(request.body, "log_text"));
        } else if (tool_name == "rag.diff_review") {
            result = RagDiffReviewResult(config, ExtractJsonString(request.body, "diff_text"));
        } else if (tool_name == "rag.basic_comm_smoke" ||
                   tool_name == "lan_agent_basic_comm_smoke") {
            result = BuildRagBasicCommSmokeResult(config);
        } else if (tool_name == "lan_agent_get_task") {
            if (g_task_manager == nullptr) {
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
            } else {
                result = g_task_manager->GetTaskResult(ExtractJsonString(request.body, "task_id"));
            }
        } else if (tool_name == "lan_agent_task_log") {
            int max_lines = 60;
            const std::string max_lines_raw = ExtractJsonRawValue(request.body, "max_lines");
            if (!max_lines_raw.empty()) {
                const int parsed_max_lines = std::atoi(max_lines_raw.c_str());
                max_lines = parsed_max_lines > 0 ? parsed_max_lines : 1;
            }
            result = TaskLogTailResult(
                config,
                ExtractJsonString(request.body, "task_id"),
                max_lines);
        } else if (tool_name == "lan_agent_discover_logs") {
            int max_entries = 20;
            int tail_lines = 20;
            const std::string max_entries_raw = ExtractJsonRawValue(request.body, "max_entries");
            if (!max_entries_raw.empty()) {
                const int parsed_max_entries = std::atoi(max_entries_raw.c_str());
                max_entries = parsed_max_entries > 0 ? parsed_max_entries : 1;
            }
            const std::string tail_lines_raw = ExtractJsonRawValue(request.body, "tail_lines");
            if (!tail_lines_raw.empty()) {
                const int parsed_tail_lines = std::atoi(tail_lines_raw.c_str());
                tail_lines = parsed_tail_lines > 0 ? parsed_tail_lines : 1;
            }
            result = DiscoverLogsResult(config, max_entries, tail_lines);
        } else if (tool_name == "lan_agent_tail_control_events") {
            int max_lines = 10;
            const std::string max_lines_raw = ExtractJsonRawValue(request.body, "max_lines");
            if (!max_lines_raw.empty()) {
                const int parsed_max_lines = std::atoi(max_lines_raw.c_str());
                max_lines = parsed_max_lines > 0 ? parsed_max_lines : 1;
            }
            result = TailTextFileResult(config, BuildRemoteControlEventsPath(config), max_lines);
        } else if (tool_name == "lan_agent_preview_patch") {
            result = PreviewPatchResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "new_content"));
        } else if (tool_name == "lan_agent_apply_single_file_patch") {
            result = ApplySingleFilePatchResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "new_content"));
        } else if (tool_name == "lan_agent_revert_single_file_patch") {
            result = RevertSingleFilePatchResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "backup_path"));
        } else if (tool_name == "lan_agent_snapshot_diff") {
            result = SnapshotDiffResult(config, ExtractJsonString(request.body, "repo_root"));
        } else if (tool_name == "lan_agent_check_build_dir") {
            const std::string build_dir = ExtractJsonString(request.body, "build_dir");
            if (build_dir.empty()) {
                result.ok = false;
                result.exit_code = 400;
                result.fields["error"] = "build_dir is required";
            } else {
                result = RunCliProfile(config, "check_build_dir", "-BuildDir \"" + build_dir + "\"");
            }
        } else if (tool_name == "lan_agent_prepare_build_dir") {
            const std::string build_dir = ExtractJsonString(request.body, "build_dir");
            const bool create_if_missing = ExtractJsonBool(request.body, "create_if_missing", false);
            if (build_dir.empty()) {
                result.ok = false;
                result.exit_code = 400;
                result.fields["error"] = "build_dir is required";
            } else {
                result = RunCliProfile(
                    config,
                    "prepare_build_dir",
                    BuildPrepareBuildDirArguments(build_dir, create_if_missing));
            }
        } else if (tool_name == "lan_agent_build_target") {
            if (g_task_manager == nullptr) {
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
            } else {
                const std::string build_dir = ExtractJsonString(request.body, "build_dir");
                const std::string target = ExtractJsonString(request.body, "target");
                std::string config_name = ExtractJsonString(request.body, "config");
                const bool dry_run = ExtractJsonBool(request.body, "dry_run", false);
                const bool validate_args = ExtractJsonBool(request.body, "validate_args", false);
                if (config_name.empty()) {
                    config_name = "Release";
                }
                if (build_dir.empty() || target.empty()) {
                    result.ok = false;
                    result.exit_code = 400;
                    result.fields["error"] = "build_dir and target are required";
                } else if (dry_run || validate_args) {
                    result = BuildTargetDryRunResult(build_dir, target, config_name);
                } else {
                    const std::string task_id = g_task_manager->EnqueueCliProfile(
                        "build_target",
                        "-BuildDir \"" + build_dir + "\" -Config " + config_name + " -Target " + target);
                    result = BuildQueuedTaskResult(task_id);
                }
            }
        } else if (tool_name == "lan_agent_configure_project") {
            if (g_task_manager == nullptr) {
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
            } else {
                const std::string project_root = ExtractJsonString(request.body, "project_root");
                const std::string build_dir = ExtractJsonString(request.body, "build_dir");
                std::string generator_kind = ExtractJsonString(request.body, "generator_kind");
                const std::string cmake_args = ExtractJsonString(request.body, "cmake_args");
                const std::string env_args = ExtractJsonString(request.body, "env");
                if (generator_kind.empty()) {
                    generator_kind = "vs2022";
                }
                if (project_root.empty() || build_dir.empty()) {
                    result.ok = false;
                    result.exit_code = 400;
                    result.fields["error"] = "project_root and build_dir are required";
                } else {
                    const std::string task_id = g_task_manager->EnqueueCliProfile(
                        "configure_project",
                        BuildConfigureProjectArguments(
                            project_root,
                            build_dir,
                            generator_kind,
                            cmake_args,
                            env_args));
                    result = BuildQueuedTaskResult(task_id);
                    result.fields["generator_kind"] = generator_kind;
                    if (!cmake_args.empty()) {
                        result.fields["cmake_args"] = cmake_args;
                    }
                    if (!env_args.empty()) {
                        result.fields["env"] = env_args;
                    }
                }
            }
        } else if (tool_name == "lan_agent_run_ctest_target") {
            if (g_task_manager == nullptr) {
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
            } else {
                const std::string build_dir = ExtractJsonString(request.body, "build_dir");
                const std::string test_regex = ExtractJsonString(request.body, "test_regex");
                std::string config_name = ExtractJsonString(request.body, "config");
                if (config_name.empty()) {
                    config_name = "Release";
                }
                if (build_dir.empty() || test_regex.empty()) {
                    result.ok = false;
                    result.exit_code = 400;
                    result.fields["error"] = "build_dir and test_regex are required";
                } else {
                    const std::string task_id = g_task_manager->EnqueueCliProfile(
                        "run_ctest_target",
                        "-BuildDir \"" + build_dir + "\" -Config " + config_name
                            + " -TestRegex \"" + test_regex + "\"");
                    result = BuildQueuedTaskResult(task_id);
                }
            }
        } else if (tool_name == "lan_agent_read_text_file") {
            int max_lines = 200;
            const std::string max_lines_raw = ExtractJsonRawValue(request.body, "max_lines");
            if (!max_lines_raw.empty()) {
                const int parsed_max_lines = std::atoi(max_lines_raw.c_str());
                max_lines = parsed_max_lines > 0 ? parsed_max_lines : 1;
            }
            result = ReadTextFileResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                max_lines);
        } else if (tool_name == "lan_agent_tail_text_file") {
            int max_lines = 120;
            const std::string max_lines_raw = ExtractJsonRawValue(request.body, "max_lines");
            if (!max_lines_raw.empty()) {
                const int parsed_max_lines = std::atoi(max_lines_raw.c_str());
                max_lines = parsed_max_lines > 0 ? parsed_max_lines : 1;
            }
            result = TailTextFileResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                max_lines);
        } else if (tool_name == "lan_agent_list_directory") {
            int max_entries = 200;
            const std::string max_entries_raw = ExtractJsonRawValue(request.body, "max_entries");
            if (!max_entries_raw.empty()) {
                const int parsed_max_entries = std::atoi(max_entries_raw.c_str());
                max_entries = parsed_max_entries > 0 ? parsed_max_entries : 1;
            }
            result = ListDirectoryResult(
                config,
                ExtractJsonString(request.body, "directory_path"),
                max_entries);
        } else {
            response->status_code = 404;
            response->status_text = "Not Found";
            response->body = BuildMcpErrorResponse(id_raw, -32601, "tool not found");
            ApplyMcpSessionHeaders(request, response, false);
            ApplyMcpTransport(response, response_mode);
            return true;
        }

        response->body = BuildMcpToolCallResponse(id_raw, result);
        ApplyMcpSessionHeaders(request, response, false);
        ApplyMcpTransport(response, response_mode);
        return true;
    }

    response->status_code = 400;
    response->status_text = "Bad Request";
    response->body = BuildMcpErrorResponse(id_raw, -32601, "method not found");
    ApplyMcpSessionHeaders(request, response, false);
    ApplyMcpTransport(response, response_mode);
    return true;
}

bool HandleBinaryHttpRoute(
    const AgentConfig & config,
    const HttpRequest & request,
    HttpResponseSpec * response) {
    if (request.method == "GET" && IsTaskBoardPath(request.path)) {
        const std::string endpoint = BuildTaskBoardEndpoint(request.path);
        const codex_lan_agent::HttpResponse upstream =
            codex_lan_agent::GetUrl(endpoint, 5000);

        const std::string log_path = BuildLogPath(
            config,
            "task_board_" + request.path.substr(std::string("/rag/task/").size()));
        std::ofstream output(log_path, std::ios::out | std::ios::trunc);
        output << "endpoint=" << endpoint << "\n";
        output << "status_code=" << upstream.status_code << "\n";
        output << "ok=" << (upstream.ok ? "true" : "false") << "\n";
        output << "error=" << upstream.error_message << "\n";
        output << "body=\n" << upstream.body << "\n";

        if (!upstream.ok) {
            response->status_code = upstream.status_code > 0 ? upstream.status_code : 502;
            response->status_text = "Error";
            response->body =
                std::string("{\"ok\":false,\"error\":\"")
                + codex_lan_agent::JsonEscape(
                    upstream.error_message.empty() ? "task board request failed" : upstream.error_message)
                + "\",\"log_path\":\"" + codex_lan_agent::JsonEscape(log_path) + "\"}";
            return true;
        }

        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = upstream.body;
        return true;
    }

    if (request.method == "GET" && request.path == "/download-file") {
        const std::string file_path = GetQueryParamValue(request, "file_path");
        std::filesystem::path normalized;
        std::string path_error;
        if (!TryResolveAllowedPath(config, file_path, &normalized, &path_error)) {
            response->status_code = 400;
            response->status_text = "Bad Request";
            response->body = "{\"ok\":false,\"error\":\"" + codex_lan_agent::JsonEscape(path_error) + "\"}";
            return true;
        }

        std::ifstream input(normalized, std::ios::binary);
        if (!input.is_open()) {
            response->status_code = 404;
            response->status_text = "Not Found";
            response->body = "{\"ok\":false,\"error\":\"failed to open file\"}";
            return true;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/octet-stream";
        response->body = buffer.str();
        return true;
    }

    if (request.method == "POST" && request.path == "/upload-file") {
        const std::string file_path = GetQueryParamValue(request, "file_path");
        std::filesystem::path target_path(file_path);
        if (file_path.empty()) {
            response->status_code = 400;
            response->status_text = "Bad Request";
            response->body = "{\"ok\":false,\"error\":\"file_path is required\"}";
            return true;
        }

        const std::filesystem::path workspace_root = std::filesystem::path(config.workspace_root);
        const std::filesystem::path logs_root = std::filesystem::path(config.log_root);
        std::error_code ec;
        const std::filesystem::path absolute_target = std::filesystem::absolute(target_path, ec);
        if (ec || (!StartsWithPath(absolute_target, workspace_root) && !StartsWithPath(absolute_target, logs_root))) {
            response->status_code = 400;
            response->status_text = "Bad Request";
            response->body = "{\"ok\":false,\"error\":\"target path is outside allowed roots\"}";
            return true;
        }

        const std::string resource_key = "file:" + absolute_target.string();
        ScopedResourceLock resource_lock(resource_key);
        if (!resource_lock.acquired()) {
            response->status_code = 409;
            response->status_text = "Conflict";
            response->body = "{\"ok\":false,\"error\":\"target file is busy\"}";
            return true;
        }

        std::filesystem::create_directories(absolute_target.parent_path());
        const std::filesystem::path temp_path =
            absolute_target.string() + ".uploading." + TimeStampForFileName() + ".tmp";
        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            response->status_code = 500;
            response->status_text = "Error";
            response->body = "{\"ok\":false,\"error\":\"failed to open target file\"}";
            return true;
        }

        output.write(request.body.data(), static_cast<std::streamsize>(request.body.size()));
        output.close();
        std::filesystem::rename(temp_path, absolute_target, ec);
        if (ec) {
            std::filesystem::remove(absolute_target, ec);
            ec.clear();
            std::filesystem::rename(temp_path, absolute_target, ec);
        }
        if (ec) {
            std::filesystem::remove(temp_path, ec);
            response->status_code = 500;
            response->status_text = "Error";
            response->body = "{\"ok\":false,\"error\":\"failed to replace target file\"}";
            return true;
        }

        response->status_code = 200;
        response->status_text = "OK";
        response->body =
            "{\"ok\":true,\"written_bytes\":\"" + std::to_string(request.body.size()) +
            "\",\"file_path\":\"" + codex_lan_agent::JsonEscape(absolute_target.string()) + "\"}";
        return true;
    }

    return false;
}

bool SendHttpResponse(
    SocketHandle client,
    int status_code,
    const std::string & status_text,
    const std::string & body,
    const std::string & content_type = "application/json",
    const std::unordered_map<std::string, std::string> & extra_headers = {}) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    for (const auto & header : extra_headers) {
        response << header.first << ": " << header.second << "\r\n";
    }
    response << "Connection: close\r\n\r\n";
    response << body;
    const std::string raw = response.str();
#ifdef MSG_NOSIGNAL
    constexpr int send_flags = MSG_NOSIGNAL;
#else
    constexpr int send_flags = 0;
#endif
    return send(client, raw.data(), static_cast<int>(raw.size()), send_flags) != kSocketErrorResult;
}

std::string ReadRequestBody(SocketHandle client) {
    std::string request;
    char buffer[2048];
    std::size_t expected_body_size = 0;
    std::size_t header_end = std::string::npos;

    while (true) {
        const int received = static_cast<int>(recv(client, buffer, sizeof(buffer), 0));
        if (received <= 0) {
            break;
        }
        request.append(buffer, buffer + received);
        header_end = request.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            const std::string headers = request.substr(0, header_end);
            const std::string marker = "Content-Length:";
            const std::size_t marker_pos = headers.find(marker);
            if (marker_pos != std::string::npos) {
                expected_body_size = static_cast<std::size_t>(std::atoi(headers.c_str() + marker_pos + marker.size()));
            }
            const std::size_t available_body = request.size() - (header_end + 4);
            if (available_body >= expected_body_size) {
                break;
            }
        }
    }

    return request;
}

HttpRequest ParseHttpRequest(const std::string & raw_request) {
    HttpRequest request;
    const std::size_t first_space = raw_request.find(' ');
    if (first_space == std::string::npos) {
        return request;
    }
    request.method = raw_request.substr(0, first_space);
    const std::size_t second_space = raw_request.find(' ', first_space + 1);
    if (second_space == std::string::npos) {
        return request;
    }
    const std::string raw_path = raw_request.substr(first_space + 1, second_space - first_space - 1);
    const std::size_t query_pos = raw_path.find('?');
    if (query_pos == std::string::npos) {
        request.path = raw_path;
    } else {
        request.path = raw_path.substr(0, query_pos);
        request.query = raw_path.substr(query_pos + 1);
    }

    const std::size_t header_end = raw_request.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        const std::string header_block = raw_request.substr(0, header_end);
        std::istringstream header_stream(header_block);
        std::string line;
        bool first_line = true;
        while (std::getline(header_stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (first_line) {
                first_line = false;
                continue;
            }
            const std::size_t colon_pos = line.find(':');
            if (colon_pos == std::string::npos) {
                continue;
            }
            const std::string key = ToLowerAscii(Trim(line.substr(0, colon_pos)));
            const std::string value = Trim(line.substr(colon_pos + 1));
            if (!key.empty()) {
                request.headers[key] = value;
            }
        }
        request.body = raw_request.substr(header_end + 4);
    }
    return request;
}

std::string UrlDecode(const std::string & value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char current = value[index];
        if (current == '%' && index + 2 < value.size()) {
            const std::string hex = value.substr(index + 1, 2);
            char * end_ptr = nullptr;
            const long parsed = std::strtol(hex.c_str(), &end_ptr, 16);
            if (end_ptr != nullptr && *end_ptr == '\0') {
                decoded.push_back(static_cast<char>(parsed));
                index += 2;
                continue;
            }
        }
        if (current == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(current);
        }
    }
    return decoded;
}

std::string GetQueryParamValue(
    const HttpRequest & request,
    const std::string & key) {
    if (request.query.empty()) {
        return "";
    }
    const std::string prefix = key + "=";
    std::size_t start = 0;
    while (start <= request.query.size()) {
        const std::size_t end = request.query.find('&', start);
        const std::string pair = request.query.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start);
        if (pair.rfind(prefix, 0) == 0) {
            return UrlDecode(pair.substr(prefix.size()));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return "";
}

std::string ExtractTaskIdFromPath(const std::string & path) {
    const std::string prefix = "/tasks/";
    if (path.rfind(prefix, 0) != 0 || path.size() <= prefix.size()) {
        return "";
    }
    return path.substr(prefix.size());
}

bool IsTaskBoardPath(const std::string & path) {
    return path == "/rag/task/current"
        || path == "/rag/task/status"
        || path == "/rag/task/conclusion"
        || path == "/rag/task/next";
}

std::string BuildTaskBoardEndpoint(const std::string & path) {
    if (path == "/rag/task/current") {
        return "http://127.0.0.1:8082/task/current";
    }
    return std::string("http://127.0.0.1:8082") + path.substr(std::string("/rag").size());
}

bool IsEnqueuePath(const std::string & path) {
    return path == "/enqueue-cli-profile"
        || path == "/enqueue-case"
        || path == "/enqueue-rag-flow";
}

CommandResult HandleHttpRoute(
    const AgentConfig & config,
    const HttpRequest & request) {
    if (request.method == "GET" && request.path == "/health") {
        return BuildHealthResult(config);
    }
    if (request.method == "GET" && request.path == "/runtime-overview") {
        return BuildRuntimeOverviewResult(config);
    }
    if (request.method == "GET" && request.path == "/healthz") {
        return BuildLivenessResult(config);
    }
    if (request.method == "GET" && request.path == "/profiles") {
        return BuildProfileListResult(config);
    }
    if (request.method == "GET" &&
        (request.path == "/semantic-action-map" ||
         request.path == "/tool-shortcuts" ||
         request.path == "/mcp-actions")) {
        return BuildSemanticActionMapResult(GetQueryParamValue(request, "action_id"));
    }
    if (request.method == "GET" && request.path == "/semantic-action-resolve") {
        return BuildSemanticActionResolveResult(
            GetQueryParamValue(request, "action_id"),
            GetQueryParamValue(request, "query"));
    }
    if (request.method == "GET" && request.path == "/semantic-action-validate") {
        return BuildSemanticActionValidateResult(
            GetQueryParamValue(request, "action_id"),
            GetQueryParamValue(request, "arguments_text"));
    }
    if (request.method == "GET" && request.path == "/semantic-action-prepare") {
        return BuildSemanticActionPrepareResult(
            GetQueryParamValue(request, "action_id"),
            GetQueryParamValue(request, "query"),
            GetQueryParamValue(request, "arguments_text"));
    }
    if (request.method == "GET" && request.path == "/semantic-action-tool-call") {
        return BuildSemanticActionToolCallResult(
            GetQueryParamValue(request, "action_id"),
            GetQueryParamValue(request, "query"),
            GetQueryParamValue(request, "arguments_text"),
            GetQueryParamValue(request, "prefer_dry_run") == "true");
    }
    if (request.method == "GET" && request.path == "/router-domain-map") {
        return BuildRouterDomainMapResult(GetQueryParamValue(request, "domain"));
    }
    if (request.method == "GET" && request.path == "/check-build-dir") {
        const std::string build_dir = GetQueryParamValue(request, "build_dir");
        if (build_dir.empty()) {
            CommandResult result;
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "build_dir is required";
            return result;
        }
        return RunCliProfile(config, "check_build_dir", "-BuildDir \"" + build_dir + "\"");
    }
    if (request.method == "POST" && request.path == "/prepare-build-dir") {
        const std::string build_dir = ExtractJsonString(request.body, "build_dir");
        const bool create_if_missing = ExtractJsonBool(request.body, "create_if_missing", false);
        if (build_dir.empty()) {
            CommandResult result;
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "build_dir is required";
            return result;
        }
        return RunCliProfile(
            config,
            "prepare_build_dir",
            BuildPrepareBuildDirArguments(build_dir, create_if_missing));
    }
    if (request.method == "POST" && request.path == "/build-target") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        const std::string build_dir = ExtractJsonString(request.body, "build_dir");
        const std::string target = ExtractJsonString(request.body, "target");
        std::string config_name = ExtractJsonString(request.body, "config");
        const bool dry_run = ExtractJsonBool(request.body, "dry_run", false);
        const bool validate_args = ExtractJsonBool(request.body, "validate_args", false);
        if (config_name.empty()) {
            config_name = "Release";
        }
        if (build_dir.empty() || target.empty()) {
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "build_dir and target are required";
            return result;
        }
        if (dry_run || validate_args) {
            return BuildTargetDryRunResult(build_dir, target, config_name);
        }
        const std::string task_id = g_task_manager->EnqueueCliProfile(
            "build_target",
            "-BuildDir \"" + build_dir + "\" -Config " + config_name + " -Target " + target);
        return BuildQueuedTaskResult(task_id);
    }
    if (request.method == "POST" && request.path == "/configure-project") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        const std::string project_root = ExtractJsonString(request.body, "project_root");
        const std::string build_dir = ExtractJsonString(request.body, "build_dir");
        std::string generator_kind = ExtractJsonString(request.body, "generator_kind");
        const std::string cmake_args = ExtractJsonString(request.body, "cmake_args");
        const std::string env_args = ExtractJsonString(request.body, "env");
        if (generator_kind.empty()) {
            generator_kind = "vs2022";
        }
        if (project_root.empty() || build_dir.empty()) {
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "project_root and build_dir are required";
            return result;
        }
        const std::string task_id = g_task_manager->EnqueueCliProfile(
            "configure_project",
            BuildConfigureProjectArguments(
                project_root,
                build_dir,
                generator_kind,
                cmake_args,
                env_args));
        result = BuildQueuedTaskResult(task_id);
        result.fields["generator_kind"] = generator_kind;
        if (!cmake_args.empty()) {
            result.fields["cmake_args"] = cmake_args;
        }
        if (!env_args.empty()) {
            result.fields["env"] = env_args;
        }
        return result;
    }
    if (request.method == "POST" && request.path == "/run-ctest-target") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        const std::string build_dir = ExtractJsonString(request.body, "build_dir");
        const std::string test_regex = ExtractJsonString(request.body, "test_regex");
        std::string config_name = ExtractJsonString(request.body, "config");
        if (config_name.empty()) {
            config_name = "Release";
        }
        if (build_dir.empty() || test_regex.empty()) {
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "build_dir and test_regex are required";
            return result;
        }
        const std::string task_id = g_task_manager->EnqueueCliProfile(
            "run_ctest_target",
            "-BuildDir \"" + build_dir + "\" -Config " + config_name
                + " -TestRegex \"" + test_regex + "\"");
        return BuildQueuedTaskResult(task_id);
    }
    if (request.method == "POST" && request.path == "/run-cli-profile") {
        const std::string profile = ExtractJsonString(request.body, "profile");
        const std::string args = ExtractJsonString(request.body, "args");
        return RunCliProfile(config, profile, args);
    }
    if (request.method == "POST" && request.path == "/enqueue-cli-profile") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        result.fields["task_id"] = g_task_manager->EnqueueCliProfile(
            ExtractJsonString(request.body, "profile"),
            ExtractJsonString(request.body, "args"));
        result.fields["status"] = "queued";
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
        return result;
    }
    if (request.method == "POST" && request.path == "/run-case") {
        return RunCase(config, ExtractJsonString(request.body, "case_path"));
    }
    if (request.method == "POST" && request.path == "/enqueue-case") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        result.fields["task_id"] = g_task_manager->EnqueueCase(
            ExtractJsonString(request.body, "case_path"));
        result.fields["status"] = "queued";
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
        return result;
    }
    if (request.method == "POST" && request.path == "/run-rag-flow") {
        const std::string query = ExtractJsonString(request.body, "query");
        std::string mode = ExtractJsonString(request.body, "mode");
        if (mode.empty()) {
            mode = "review";
        }
        return RunRagFlow(config, query, mode);
    }
    if (request.method == "POST" && request.path == "/enqueue-rag-flow") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        result.fields["task_id"] = g_task_manager->EnqueueRagFlow(
            ExtractJsonString(request.body, "query"),
            ExtractJsonString(request.body, "mode"));
        result.fields["status"] = "queued";
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
        return result;
    }
    if (request.method == "POST" && request.path == "/run-local-chat") {
        std::string mode = ExtractJsonString(request.body, "mode");
        if (mode.empty()) {
            mode = "code_analysis";
        }
        return RunLocalChat(
            config,
            ExtractJsonString(request.body, "scope"),
            ExtractJsonString(request.body, "question"),
            mode);
    }
    if (request.method == "POST" && request.path == "/enqueue-local-chat") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        result.fields["task_id"] = g_task_manager->EnqueueLocalChat(
            ExtractJsonString(request.body, "scope"),
            ExtractJsonString(request.body, "question"),
            ExtractJsonString(request.body, "mode"));
        result.fields["status"] = "queued";
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
        return result;
    }
    if (request.method == "POST" && request.path == "/tail-text-file") {
        int max_lines = 120;
        const std::string max_lines_raw = ExtractJsonRawValue(request.body, "max_lines");
        if (!max_lines_raw.empty()) {
            const int parsed_max_lines = std::atoi(max_lines_raw.c_str());
            max_lines = parsed_max_lines > 0 ? parsed_max_lines : 1;
        }
        return TailTextFileResult(
            config,
            ExtractJsonString(request.body, "file_path"),
            max_lines);
    }
    if (request.method == "POST" && request.path == "/task-log") {
        int max_lines = 60;
        const std::string max_lines_raw = ExtractJsonRawValue(request.body, "max_lines");
        if (!max_lines_raw.empty()) {
            const int parsed_max_lines = std::atoi(max_lines_raw.c_str());
            max_lines = parsed_max_lines > 0 ? parsed_max_lines : 1;
        }
        return TaskLogTailResult(
            config,
            ExtractJsonString(request.body, "task_id"),
            max_lines);
    }
    if (request.method == "GET") {
        const std::string task_id = ExtractTaskIdFromPath(request.path);
        if (!task_id.empty()) {
            if (g_task_manager == nullptr) {
                CommandResult result;
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
                return result;
            }
            return g_task_manager->GetTaskResult(task_id);
        }
    }

    CommandResult result;
    result.ok = false;
    result.exit_code = 404;
    result.fields["error"] = "route not found";
    result.fields["method"] = request.method;
    result.fields["path"] = request.path;
    return result;
}

int RunServer(const AgentConfig & config) {
    ServerInstanceGuard instance_guard;
    if (!instance_guard.acquired()) {
        WriteServerStateFile(config, "rejected", instance_guard.error_message());
        LogServerEvent(config, "startup_rejected", instance_guard.error_message());
        std::cerr << instance_guard.error_message() << std::endl;
        return 11;
    }

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    WsaSession wsa;
    if (!wsa.valid()) {
        WriteServerStateFile(config, "failed", "winsock startup failed");
        LogServerEvent(config, "startup_failed", "winsock startup failed");
        std::cerr << "winsock startup failed" << std::endl;
        return 7;
    }

    SocketHandle listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == kInvalidSocket) {
        const std::string detail = "failed to create listen socket; "
            + DescribeSocketErrorCode(GetLastSocketErrorCode());
        WriteServerStateFile(config, "failed", detail);
        LogServerEvent(config, "startup_failed", detail);
        std::cerr << detail << std::endl;
        return 8;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(config.listen_port));
    address.sin_addr.s_addr = INADDR_ANY;

    const int reuse = 1;
    SetReuseAddrPortable(listen_socket, reuse);

    if (bind(listen_socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == kSocketErrorResult) {
        const std::string detail = "failed to bind listen socket on port "
            + std::to_string(config.listen_port) + "; "
            + DescribeSocketErrorCode(GetLastSocketErrorCode());
        WriteServerStateFile(config, "failed", detail);
        LogServerEvent(config, "startup_failed", detail);
        std::cerr << detail << std::endl;
        CloseSocketPortable(listen_socket);
        return 9;
    }

    if (listen(listen_socket, 8) == kSocketErrorResult) {
        const std::string detail = "failed to listen on port "
            + std::to_string(config.listen_port) + "; "
            + DescribeSocketErrorCode(GetLastSocketErrorCode());
        WriteServerStateFile(config, "failed", detail);
        LogServerEvent(config, "startup_failed", detail);
        std::cerr << detail << std::endl;
        CloseSocketPortable(listen_socket);
        return 10;
    }

    WriteServerStateFile(config, "running");
    LogServerEvent(
        config,
        "startup_complete",
        config.listen_host + ":" + std::to_string(config.listen_port));
    std::cout << "codex_lan_agent server listening on "
              << config.listen_host << ":" << config.listen_port << std::endl;

    TaskManager task_manager(config);
    g_task_manager = &task_manager;

    auto handle_client = [&config](SocketHandle client) {
        try {
            const auto request_start_clock = std::chrono::steady_clock::now();
            const std::string request_started_at = IsoTimestampNow();
            const std::string raw_request = ReadRequestBody(client);
            const HttpRequest request = ParseHttpRequest(raw_request);
            HttpResponseSpec response;
            const auto send_recorded_response = [&config, &client, &request, &request_start_clock, &request_started_at](
                HttpResponseSpec response_to_send) {
                ApplyMcpCorsHeaders(&response_to_send);
                const auto request_finish_clock = std::chrono::steady_clock::now();
                const long long duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    request_finish_clock - request_start_clock).count();
                AppendRemoteControlEvent(
                    config,
                    request,
                    response_to_send,
                    request_started_at,
                    IsoTimestampNow(),
                    duration_ms);
                SendHttpResponse(
                    client,
                    response_to_send.status_code,
                    response_to_send.status_text,
                    response_to_send.body,
                    response_to_send.content_type,
                    response_to_send.headers);
            };
            if (request.method == "OPTIONS") {
                response.status_code = 204;
                response.status_text = "No Content";
                response.body.clear();
                send_recorded_response(response);
                CloseSocketPortable(client);
                return;
            }
            if (HandleMcpDiscoveryRoute(config, request, &response)) {
                send_recorded_response(response);
                CloseSocketPortable(client);
                return;
            }
            if (HandleMcpRoute(config, request, &response)) {
                send_recorded_response(response);
                CloseSocketPortable(client);
                return;
            }
            if (HandleBinaryHttpRoute(config, request, &response)) {
                send_recorded_response(response);
                CloseSocketPortable(client);
                return;
            }

            const CommandResult result = HandleHttpRoute(config, request);
            if (result.exit_code == 404) {
                response.status_code = 404;
            } else if (result.ok) {
                response.status_code = IsEnqueuePath(request.path) ? 202 : 200;
            } else {
                response.status_code = 500;
            }
            response.status_text = response.status_code == 200 ? "OK" : (response.status_code == 404 ? "Not Found" : "Error");
            if (response.status_code == 202) {
                response.status_text = "Accepted";
            }
            response.body = ResultToJson(result);
            send_recorded_response(response);
        } catch (const std::exception & ex) {
            LogServerEvent(config, "request_exception", ex.what());
            SendHttpResponse(
                client,
                500,
                "Error",
                std::string("{\"ok\":false,\"exit_code\":500,\"error\":\"")
                    + codex_lan_agent::JsonEscape(ex.what()) + "\"}");
        } catch (...) {
            LogServerEvent(config, "request_exception", "unhandled server exception");
            SendHttpResponse(
                client,
                500,
                "Error",
                "{\"ok\":false,\"exit_code\":500,\"error\":\"unhandled server exception\"}");
        }
        CloseSocketPortable(client);
    };

    std::string last_accept_error;
    while (true) {
        SocketHandle client = accept(listen_socket, nullptr, nullptr);
        if (client == kInvalidSocket) {
            const std::string detail = DescribeSocketErrorCode(GetLastSocketErrorCode());
            if (detail != last_accept_error) {
                LogServerEvent(config, "accept_failed", detail);
                last_accept_error = detail;
            }
            continue;
        }
        last_accept_error.clear();
        std::thread(handle_client, client).detach();
    }

    g_task_manager = nullptr;
    WriteServerStateFile(config, "stopped", "server loop exited");
    LogServerEvent(config, "shutdown_complete", "server loop exited");
    return 0;
}

}  // namespace

int main(int argc, char ** argv) {
    try {
        std::vector<std::string> arguments;
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }

        if (arguments.empty()) {
            PrintUsage();
            return 1;
        }

        std::string config_path;
        if (!FindArgument(arguments, "--config", &config_path)) {
            PrintUsage();
            return 1;
        }

        AgentConfig config;
        std::string config_error;
        if (!codex_lan_agent::LoadAgentConfig(config_path, &config, &config_error)) {
            std::cerr << "config error: " << config_error << std::endl;
            return 2;
        }

        std::string command;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            if (arguments[index] == "--config") {
                ++index;
                continue;
            }
            if (!arguments[index].empty() && arguments[index].front() != '-') {
                command = arguments[index];
                break;
            }
        }

        if (command.empty()) {
            PrintUsage();
            return 1;
        }

        if (command == "health") {
            PrintResultAsText(BuildHealthResult(config));
            return 0;
        }

        if (command == "list-profiles") {
            PrintResultAsText(BuildProfileListResult(config));
            return 0;
        }

        if (command == "run-cli-profile") {
            for (std::size_t index = 0; index + 1 < arguments.size(); ++index) {
                if (arguments[index] == "run-cli-profile") {
                    const CommandResult result = RunCliProfile(
                        config,
                        arguments[index + 1],
                        JoinRemainingArguments(arguments, index + 2));
                    PrintResultAsText(result);
                    return result.exit_code;
                }
            }
            std::cerr << "run-cli-profile requires a profile name" << std::endl;
            return 1;
        }

        if (command == "run-case") {
            for (std::size_t index = 0; index + 1 < arguments.size(); ++index) {
                if (arguments[index] == "run-case") {
                    const CommandResult result = RunCase(config, arguments[index + 1]);
                    PrintResultAsText(result);
                    return result.exit_code;
                }
            }
            std::cerr << "run-case requires a case path" << std::endl;
            return 1;
        }

        if (command == "run-rag-flow") {
            for (std::size_t index = 0; index + 1 < arguments.size(); ++index) {
                if (arguments[index] == "run-rag-flow") {
                    const std::string mode = (index + 2 < arguments.size()) ? arguments[index + 2] : "review";
                    const CommandResult result = RunRagFlow(config, arguments[index + 1], mode);
                    PrintResultAsText(result);
                    return result.exit_code;
                }
            }
            std::cerr << "run-rag-flow requires a query" << std::endl;
            return 1;
        }

        if (command == "serve") {
            std::string provided_machine_code;
            FindArgument(arguments, "--machine-code", &provided_machine_code);
            std::string machine_code_error;
            if (!ValidateRemoteMachineCode(provided_machine_code, &machine_code_error)) {
                WriteServerStateFile(config, "failed", machine_code_error);
                LogServerEvent(config, "startup_failed", machine_code_error);
                std::cerr << machine_code_error << std::endl;
                return 12;
            }
            const int exit_code = RunServer(config);
            LogServerEvent(config, "serve_returned", "exit_code=" + std::to_string(exit_code));
            return exit_code;
        }

        PrintUsage();
        return 1;
    } catch (const std::exception & ex) {
        std::cerr << "fatal exception: " << ex.what() << std::endl;
        return 99;
    } catch (...) {
        std::cerr << "fatal exception: unknown" << std::endl;
        return 99;
    }
}
