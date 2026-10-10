#pragma once

#include "AgentConfig.h"
#include "HttpClient.h"
#include "types.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOWINUSER
#define NOWINUSER
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winreg.h>
#else
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#endif

using codex_lan_agent::AgentConfig;

#ifndef CODEX_LAN_AGENT_SOCKET_HANDLE_DEFINED
#define CODEX_LAN_AGENT_SOCKET_HANDLE_DEFINED
#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
constexpr int kSocketErrorResult = SOCKET_ERROR;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
constexpr int kSocketErrorResult = -1;
#endif
#endif

struct CommOperations final {
    static std::string TimeStampForFileName() {
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

    static int CloseSocketPortable(SocketHandle socket_handle) {
#ifdef _WIN32
        return closesocket(socket_handle);
#else
        return close(socket_handle);
#endif
    }

    static std::string IsoTimestampNow() {
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

    static std::string Trim(const std::string & value) {
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

    static std::string ToLowerAscii(std::string value) {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
        return value;
    }

    static std::string CurrentPlatformName() {
#ifdef _WIN32
        return "windows";
#else
        return "linux";
#endif
    }

    static std::string ReadTextFileTrimmed(const std::string & path) {
        std::ifstream input(path);
        if (!input.is_open()) {
            return std::string();
        }
        std::ostringstream content;
        content << input.rdbuf();
        return Trim(content.str());
    }

    static std::string GetHostNamePortable() {
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

    static std::string GetStableMachineId() {
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

    static std::string BuildRemoteMachineCodeSource() {
        return CurrentPlatformName()
            + "|" + GetHostNamePortable()
            + "|" + GetStableMachineId();
    }

    static std::uint64_t Fnv1a64(const std::string & text) {
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char ch : text) {
            hash ^= static_cast<std::uint64_t>(ch);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    static std::string FormatMachineCode(std::uint64_t value) {
        std::ostringstream hex_stream;
        hex_stream << std::uppercase << std::hex << std::setfill('0') << std::setw(16) << value;
        const std::string hex_value = hex_stream.str();
        return hex_value.substr(0, 4) + "-"
            + hex_value.substr(4, 4) + "-"
            + hex_value.substr(8, 4) + "-"
            + hex_value.substr(12, 4);
    }

    static std::string BuildRemoteMachineCode() {
        return FormatMachineCode(Fnv1a64(BuildRemoteMachineCodeSource()));
    }

    static bool ValidateRemoteMachineCode(
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

    static std::string BuildRequestTimestampToken() {
        return TimeStampForFileName();
    }

    static std::string SanitizeDispatchToken(
        const std::string & value,
        const std::string & fallback) {
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

    static bool FindArgument(
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

    static std::string JoinRemainingArguments(
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

    static std::string BuildLogPath(
        const AgentConfig & config,
        const std::string & prefix) {
        return codex_lan_agent::JoinPath(
            config.log_root,
            prefix + "_" + TimeStampForFileName() + ".log");
    }

    static std::string BuildServerStatePath(const AgentConfig & config) {
        return codex_lan_agent::JoinPath(config.log_root, "agent_server_state.json");
    }

    static std::string GetServerLockFilePath() {
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

    static void IgnoreBrokenPipePortable() {
#ifndef _WIN32
        signal(SIGPIPE, SIG_IGN);
#endif
    }

    static std::string BuildRemoteControlEventsPath(const AgentConfig & config) {
        return codex_lan_agent::JoinPath(config.log_root, "remote_control_events.jsonl");
    }

    static std::string BuildAgentServerStdoutLogPath(const AgentConfig & config) {
        return codex_lan_agent::JoinPath(config.log_root, "agent_server_stdout.log");
    }

    static std::mutex & AgentServerStdoutFileLogMutex() {
        static std::mutex mutex;
        return mutex;
    }

    static bool & AgentServerStdoutFileLogEnabled() {
        static bool enabled = false;
        return enabled;
    }

    static std::string & AgentServerStdoutFileLogPath() {
        static std::string path;
        return path;
    }

    static void ConfigureAgentServerStdoutFileLog(
        const AgentConfig & config,
        bool enabled,
        const std::string & path = "") {
        std::lock_guard<std::mutex> lock(AgentServerStdoutFileLogMutex());
        AgentServerStdoutFileLogEnabled() = enabled;
        AgentServerStdoutFileLogPath() = path.empty()
            ? BuildAgentServerStdoutLogPath(config)
            : path;
    }

    static bool IsAgentServerStdoutFileLogEnabled() {
        std::lock_guard<std::mutex> lock(AgentServerStdoutFileLogMutex());
        return AgentServerStdoutFileLogEnabled();
    }

    static std::string GetAgentServerStdoutFileLogPath(const AgentConfig & config) {
        std::lock_guard<std::mutex> lock(AgentServerStdoutFileLogMutex());
        return AgentServerStdoutFileLogPath().empty()
            ? BuildAgentServerStdoutLogPath(config)
            : AgentServerStdoutFileLogPath();
    }

    static void AppendAgentServerStdoutFileLogLine(
        const AgentConfig & config,
        const std::string & line) {
        std::lock_guard<std::mutex> lock(AgentServerStdoutFileLogMutex());
        if (!AgentServerStdoutFileLogEnabled()) {
            return;
        }
        const std::string path = AgentServerStdoutFileLogPath().empty()
            ? BuildAgentServerStdoutLogPath(config)
            : AgentServerStdoutFileLogPath();
        const std::filesystem::path log_path(path);
        const std::filesystem::path parent_path = log_path.parent_path();
        if (!parent_path.empty()) {
            std::filesystem::create_directories(parent_path);
        }
        std::ofstream output(path, std::ios::out | std::ios::app);
        if (output.is_open()) {
            output << line << "\n";
        }
    }

    static std::string BuildExperienceCardsPath(const AgentConfig & config) {
        return codex_lan_agent::JoinPath(config.log_root, "experience_cards.jsonl");
    }

    static std::string BuildPatchAuditEventsPath(const AgentConfig & config) {
        return codex_lan_agent::JoinPath(config.log_root, "patch_audit_events.jsonl");
    }

    static void WriteServerStateFile(
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

    static void LogServerEvent(
        const AgentConfig & config,
        const std::string & event,
        const std::string & detail = "") {
        std::ostringstream output;
        output << "[" << TimeStampForFileName() << "] server_event=" << event;
        if (!detail.empty()) {
            output << " detail=\"" << detail << "\"";
        }
        const std::string line = output.str();
        std::cerr << line << std::endl;
        AppendAgentServerStdoutFileLogLine(config, line);
    }
    static int GetLastSocketErrorCode() {
#ifdef _WIN32
        return static_cast<int>(WSAGetLastError());
#else
        return errno;
#endif
    }

    static std::string DescribeSocketErrorCode(int error_code) {
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

    static std::filesystem::path NormalizeForPathComparison(const std::filesystem::path & path) {
        std::error_code ec;
        const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
        if (!ec) {
            return normalized;
        }
        return path.lexically_normal();
    }

    static bool HasPathPrefixBoundary(const std::string & path_text, std::size_t prefix_size) {
        return prefix_size >= path_text.size()
            || path_text[prefix_size] == '\\'
            || path_text[prefix_size] == '/';
    }

    static std::vector<std::filesystem::path> ParsePathList(const std::string & path_list) {
        std::vector<std::filesystem::path> paths;
        std::size_t start = 0;
        while (start <= path_list.size()) {
            const std::size_t end = path_list.find(';', start);
            const std::string path_text = Trim(path_list.substr(
                start,
                end == std::string::npos ? std::string::npos : end - start));
            if (!path_text.empty()) {
                paths.emplace_back(path_text);
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return paths;
    }

    static std::vector<std::filesystem::path> GetManualWorkspaceRoots(const AgentConfig & config) {
        return ParsePathList(config.manual_workspace_root);
    }

    static std::vector<std::filesystem::path> GetConfiguredWorkspaceRoots(const AgentConfig & config) {
        return ParsePathList(config.workspace_root);
    }

    static std::vector<std::filesystem::path> GetWorkspaceRoots(const AgentConfig & config) {
        std::vector<std::filesystem::path> roots = GetManualWorkspaceRoots(config);
        const std::vector<std::filesystem::path> configured_roots = GetConfiguredWorkspaceRoots(config);
        roots.insert(roots.end(), configured_roots.begin(), configured_roots.end());
        return roots;
    }

    static std::filesystem::path GetPrimaryWorkspaceRoot(const AgentConfig & config) {
        const std::vector<std::filesystem::path> roots = GetWorkspaceRoots(config);
        return roots.empty() ? std::filesystem::path() : roots.front();
    }

    static std::filesystem::path RebaseRequestedPath(
        const AgentConfig & config,
        const std::string & raw_path) {
        std::filesystem::path requested(raw_path);
        if (requested.is_relative()) {
            requested = GetPrimaryWorkspaceRoot(config) / requested;
        }
        return requested;
    }

    static bool TryNormalizeRequestedPath(
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

        const std::filesystem::path requested = RebaseRequestedPath(config, raw_path);
        std::error_code ec;
        const std::filesystem::path normalized = std::filesystem::weakly_canonical(requested, ec);
        if (ec) {
            if (error_message) {
                *error_message = "failed to normalize path";
            }
            return false;
        }

        *normalized_path = normalized;
        return true;
    }

    static bool StartsWithPath(
        const std::filesystem::path & path,
        const std::filesystem::path & prefix) {
        if (prefix.empty()) {
            return false;
        }
        const std::string path_text = NormalizeForPathComparison(path).string();
        const std::string prefix_text = NormalizeForPathComparison(prefix).string();
#ifdef _WIN32
        if (path_text.size() < prefix_text.size()) {
            return false;
        }
        return _strnicmp(path_text.c_str(), prefix_text.c_str(), prefix_text.size()) == 0
            && HasPathPrefixBoundary(path_text, prefix_text.size());
#else
        return path_text.rfind(prefix_text, 0) == 0
            && HasPathPrefixBoundary(path_text, prefix_text.size());
#endif
    }

    static bool StartsWithAnyPath(
        const std::filesystem::path & path,
        const std::string & path_list) {
        const std::vector<std::filesystem::path> roots = ParsePathList(path_list);
        return std::any_of(
            roots.begin(),
            roots.end(),
            [&path](const std::filesystem::path & root) {
                return StartsWithPath(path, root);
            });
    }

    static bool TryResolveAllowedPath(
        const AgentConfig & config,
        const std::string & raw_path,
        std::filesystem::path * normalized_path,
        std::string * error_message) {
        std::filesystem::path normalized;
        if (!TryNormalizeRequestedPath(config, raw_path, &normalized, error_message)) {
            return false;
        }

        const std::filesystem::path logs_root(config.log_root);
        const bool is_allowed = StartsWithPath(normalized, logs_root)
            || StartsWithAnyPath(normalized, config.manual_workspace_root)
            || StartsWithAnyPath(normalized, config.workspace_root)
            || StartsWithAnyPath(normalized, config.allowed_roots);
        if (!is_allowed) {
            if (error_message) {
                *error_message = "path is outside allowed roots";
            }
            return false;
        }

        *normalized_path = normalized;
        return true;
    }

    static bool TryResolveWorkspaceFilePath(
        const AgentConfig & config,
        const std::string & raw_path,
        std::filesystem::path * normalized_path,
        std::string * error_message) {
        std::filesystem::path normalized;
        if (!TryNormalizeRequestedPath(config, raw_path, &normalized, error_message)) {
            return false;
        }
        std::error_code ec;
        if (!StartsWithAnyPath(normalized, config.manual_workspace_root) &&
            !StartsWithAnyPath(normalized, config.workspace_root)) {
            if (error_message) {
                *error_message = "path is outside workspace_root and manual_workspace_root";
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

    static std::string UrlDecode(const std::string & value) {
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

    static std::string GetQueryParamValue(
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

    static std::string ExtractTaskIdFromPath(const std::string & path) {
        const std::string prefix = "/tasks/";
        if (path.rfind(prefix, 0) != 0 || path.size() <= prefix.size()) {
            return "";
        }
        return path.substr(prefix.size());
    }

    static bool IsTaskBoardPath(const std::string & path) {
        return path == "/rag/task/current"
            || path == "/rag/task/status"
            || path == "/rag/task/conclusion"
            || path == "/rag/task/next";
    }

    static std::string BuildTaskBoardEndpoint(const std::string & path) {
        if (path == "/rag/task/current") {
            return "http://127.0.0.1:8082/task/current";
        }
        return std::string("http://127.0.0.1:8082") + path.substr(std::string("/rag").size());
    }

    static bool IsEnqueuePath(const std::string & path) {
        return path == "/enqueue-cli-profile"
            || path == "/enqueue-case"
            || path == "/enqueue-rag-flow";
    }

    static std::string ComputeCommandOutcome(const CommandResult & result) {
        const auto task_completion_it = result.fields.find("task_completion");
        if (task_completion_it != result.fields.end() &&
            !task_completion_it->second.empty() &&
            task_completion_it->second != "complete") {
            return "PARTIAL";
        }

        const auto has_more_it = result.fields.find("has_more");
        if (has_more_it != result.fields.end() &&
            has_more_it->second == "true") {
            return "PARTIAL";
        }

        const auto continue_required_it = result.fields.find("continue_required");
        if (continue_required_it != result.fields.end() &&
            continue_required_it->second == "true") {
            return "PARTIAL";
        }

        const auto terminal_state_it = result.fields.find("terminal_state");
        if (terminal_state_it != result.fields.end() &&
            terminal_state_it->second == "false") {
            return "PARTIAL";
        }

        const auto task_done_it = result.fields.find("task_done");
        if (task_done_it != result.fields.end() &&
            task_done_it->second == "false") {
            return "PARTIAL";
        }

        const auto completion_claim_allowed_it = result.fields.find("completion_claim_allowed");
        if (completion_claim_allowed_it != result.fields.end() &&
            completion_claim_allowed_it->second == "false") {
            return "PARTIAL";
        }

        const auto final_answer_allowed_it = result.fields.find("final_answer_allowed");
        if (final_answer_allowed_it != result.fields.end() &&
            final_answer_allowed_it->second == "false") {
            return "PARTIAL";
        }

        const auto supervision_status_it = result.fields.find("supervision_status");
        if (supervision_status_it != result.fields.end() &&
            supervision_status_it->second == "closed_loop_continue") {
            return "PARTIAL";
        }

        const auto analysis_allowed_it = result.fields.find("analysis_allowed");
        if (analysis_allowed_it != result.fields.end() &&
            analysis_allowed_it->second == "false") {
            return "PARTIAL";
        }

        const auto batch_completion_it = result.fields.find("batch_completion");
        if (batch_completion_it != result.fields.end() &&
            batch_completion_it->second == "incomplete") {
            return "PARTIAL";
        }

        const auto read_complete_it = result.fields.find("read_complete");
        if (read_complete_it != result.fields.end() &&
            read_complete_it->second == "false") {
            return "PARTIAL";
        }

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

    static std::string GetFieldOrDefault(
        const CommandResult & result,
        const std::string & key,
        const std::string & default_value) {
        const auto it = result.fields.find(key);
        return it == result.fields.end() ? default_value : it->second;
    }

    static std::string ResultToJson(const CommandResult & result) {
        CommandResult decorated = result;
        decorated.fields["outcome"] = ComputeCommandOutcome(result);
        std::ostringstream buffer;
        buffer << "{";
        // ── P1c: 强类型 ok/exit_code（布尔/整数），与字符串 fields 区分 ──
        buffer << "\"ok\":" << (decorated.ok ? "true" : "false");
        buffer << ",\"exit_code\":" << decorated.exit_code;
        // 跟踪已镜像输出的结构化字段名，避免重复输出
        std::unordered_set<std::string> mirrored_structured;
        for (const auto & entry : decorated.fields) {
            const std::string & key = entry.first;
            const std::string & value = entry.second;
            // ok/exit_code 已在顶层强类型输出；fields 中若再次出现则跳过，避免重复键
            if (key == "ok" || key == "exit_code") {
                continue;
            }
            const bool ends_with_json =
                key.size() > 5 && key.compare(key.size() - 5, 5, "_json") == 0;
            const bool looks_like_json =
                !value.empty() && (value.front() == '{' || value.front() == '[');
            if (ends_with_json && looks_like_json) {
                // P1a: 以 _json 结尾且内容是合法 JSON 起始字符 → 输出为结构化对象/数组
                buffer << ",\"" << codex_lan_agent::JsonEscape(key) << "\":" << value;
                // P1a: 同时镜像输出不带 _json 后缀的结构化字段，消除客户端双重解析
                const std::string mirror_key = key.substr(0, key.size() - 5);
                if (mirrored_structured.find(mirror_key) == mirrored_structured.end()) {
                    buffer << ",\"" << codex_lan_agent::JsonEscape(mirror_key)
                           << "\":" << value;
                    mirrored_structured.insert(mirror_key);
                }
            } else {
                buffer << ",\"" << codex_lan_agent::JsonEscape(entry.first) << "\":\""
                       << codex_lan_agent::JsonEscape(entry.second) << "\"";
            }
        }
        buffer << "}";
        return buffer.str();
    }

    static std::string ExtractResultField(
        const std::string & response_body,
        const std::string & key) {
        return ExtractJsonStringValue(response_body, key);
    }

    static int JsonUnicodeHexDigitValue(char ch) {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return 10 + (ch - 'a');
        }
        if (ch >= 'A' && ch <= 'F') {
            return 10 + (ch - 'A');
        }
        return -1;
    }

    static bool TryReadJsonUnicodeEscape(
        const std::string & text,
        std::size_t index,
        unsigned int * codepoint) {
        if (index + 4 >= text.size()) {
            return false;
        }
        unsigned int value = 0;
        for (std::size_t offset = 1; offset <= 4; ++offset) {
            const int digit = JsonUnicodeHexDigitValue(text[index + offset]);
            if (digit < 0) {
                return false;
            }
            value = (value << 4) | static_cast<unsigned int>(digit);
        }
        *codepoint = value;
        return true;
    }

    static void AppendUtf8Codepoint(std::string * output, unsigned int codepoint) {
        if (codepoint <= 0x7F) {
            output->push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            output->push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
            output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            output->push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
            output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    static std::string ExtractJsonStringValue(
        const std::string & body,
        const std::string & key) {
        const std::string marker = "\"" + key + "\"";
        const std::size_t key_pos = body.find(marker);
        if (key_pos == std::string::npos) {
            return std::string();
        }

        const std::size_t colon_pos = body.find(':', key_pos + marker.size());
        if (colon_pos == std::string::npos) {
            return std::string();
        }

        std::size_t value_pos = body.find_first_not_of(" \t\r\n", colon_pos + 1);
        if (value_pos == std::string::npos) {
            return std::string();
        }

        if (body[value_pos] != '"') {
            const std::size_t value_end = body.find_first_of(",}\r\n", value_pos);
            const std::string raw = body.substr(
                value_pos,
                value_end == std::string::npos ? std::string::npos : value_end - value_pos);
            const std::size_t trim_end = raw.find_last_not_of(" \t\r\n");
            return trim_end == std::string::npos ? std::string() : raw.substr(0, trim_end + 1);
        }

        ++value_pos;
        std::string value;
        bool escaping = false;
        for (std::size_t index = value_pos; index < body.size(); ++index) {
            const char ch = body[index];
            if (escaping) {
                switch (ch) {
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                case '\\':
                case '"':
                case '/':
                    value.push_back(ch);
                    break;
                case 'u': {
                    unsigned int codepoint = 0;
                    if (TryReadJsonUnicodeEscape(body, index, &codepoint)) {
                        AppendUtf8Codepoint(&value, codepoint);
                        index += 4;
                    } else {
                        value.push_back(ch);
                    }
                    break;
                }
                default:
                    value.push_back(ch);
                    break;
                }
                escaping = false;
                continue;
            }
            if (ch == '\\') {
                escaping = true;
                continue;
            }
            if (ch == '"') {
                return value;
            }
            value.push_back(ch);
        }
        return std::string();
    }

    static std::string GetHeaderValue(
        const HttpRequest & request,
        const std::string & name) {
        const auto it = request.headers.find(ToLowerAscii(name));
        return it == request.headers.end() ? std::string() : it->second;
    }

    template <typename... Args>
    static std::string FirstNonEmpty(const Args &... values) {
        static_assert(
            (std::is_convertible_v<const Args &, std::string_view> && ...),
            "FirstNonEmpty arguments must be convertible to std::string_view");

        const std::initializer_list<std::string_view> candidates{
            std::string_view(values)...
        };
        for (const std::string_view candidate : candidates) {
            if (!candidate.empty()) {
                return std::string(candidate);
            }
        }
        return std::string();
    }

    static std::string FileNameFromPathString(const std::string & value) {
        if (value.empty()) {
            return std::string();
        }
        std::error_code ec;
        const std::filesystem::path path(value);
        const std::string filename = path.filename().string();
        if (!filename.empty() && !ec) {
            return filename;
        }
        return value;
    }

    static std::string BuildLocalCliEvidenceJson(const CommandResult & result) {
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

    static std::string BuildMcpErrorResponse(
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

    static std::string BuildMcpInitializeResponse(const std::string & id_raw) {
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

    static std::string BuildMcpCapabilitiesResponse() {
        return "{"
            "\"name\":\"codex-lan-agent\","
            "\"transport\":\"streamable-http-minimal\","
            "\"endpoint\":\"/mcp\","
            "\"methods\":[\"HEAD\",\"GET\",\"POST\"],"
            "\"accept\":[\"application/json\",\"text/event-stream\"],"
            "\"streamable_http_mode\":\"POST JSON-RPC; GET returns JSON capabilities unless Accept requests text/event-stream\","
            "\"get_event_stream\":\"not_supported_post_json_rpc_only\","
            "\"message\":\"use POST /mcp for JSON-RPC requests\""
            "}";
    }

    static std::string BuildMcpDiscoveryBaseUrl(const AgentConfig & config) {
        const std::string host = (config.listen_host.empty() || config.listen_host == "0.0.0.0")
            ? "127.0.0.1"
            : config.listen_host;
        return "http://" + host + ":" + std::to_string(config.listen_port);
    }

    static std::string BuildMcpOAuthAuthorizationServerResponse(const AgentConfig & config) {
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

    static std::string BuildMcpOAuthProtectedResourceResponse(const AgentConfig & config) {
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

    static bool HeaderContainsToken(
        const std::string & header_value,
        const std::string & token) {
        return ToLowerAscii(header_value).find(ToLowerAscii(token)) != std::string::npos;
    }

    static std::string BuildSseMessage(const std::string & json_body) {
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

    static bool LooksManualSourceThread(const std::string & source_thread) {
        const std::string lowered = ToLowerAscii(source_thread);
        return lowered.empty() ||
            lowered == "unknown" ||
            lowered == "manual" ||
            lowered == "human" ||
            lowered.find("edge") != std::string::npos ||
            lowered.find("browser") != std::string::npos;
    }

    static std::string ClassifyUnifiedSourceLabel(
        const std::string & source_thread,
        const std::string & trigger,
        const HttpRequest & request) {
        const bool manual_source = LooksManualSourceThread(source_thread);
        const bool is_mcp = request.path == "/mcp";
        if (!manual_source && trigger == "manual" && is_mcp) {
            return "mixed";
        }
        if (!manual_source) {
            return "codex";
        }
        return "manual";
    }

    static std::string BuildTakeoverRelation(
        const std::string & source_label,
        const std::string & source_thread,
        const HttpRequest & request) {
        if (source_label == "mixed") {
            return "manual_request_with_codex_execution";
        }
        if (source_label == "codex") {
            return "codex_managed";
        }
        if (request.path == "/mcp") {
            return "manual_direct_mcp";
        }
        return source_thread.empty() || source_thread == "unknown"
            ? "manual_direct_remote"
            : "manual_thread_managed";
    }

    static std::string BuildRemoteTaskGroup(
        const std::string & task_id,
        const std::string & command_name,
        const std::string & request_type,
        const std::string & result_ref,
        const std::string & evidence_ref) {
        if (!task_id.empty()) {
            return "task:" + task_id;
        }
        if (!result_ref.empty()) {
            return request_type + ":" + command_name + ":" + FileNameFromPathString(result_ref);
        }
        if (!evidence_ref.empty()) {
            return request_type + ":" + command_name + ":" + FileNameFromPathString(evidence_ref);
        }
        return request_type + ":" + command_name + ":direct";
    }

    static bool IsAiInteractionCommandName(const std::string & command_name) {
        return command_name == "lan_agent_run_local_chat"
            || command_name == "lan_agent_enqueue_local_chat"
            || command_name == "rag.query"
            || command_name == "lan_agent_ventriloquist_reply"
            || command_name == "lan_agent_remote_session_new_turn"
            || command_name == "lan_agent_remote_session_append_turn"
            || command_name == "remote-session/new-turn"
            || command_name == "remote-session/append-turn"
            || command_name == "llama.observer_smoke";
    }

    static bool IsObservationNoiseCommandName(const std::string & command_name) {
        return command_name == "tools/list"
            || command_name == "initialize"
            || command_name == "notifications/initialized"
            || command_name == "initialized"
            || command_name == "llama-webui-mcp";
    }

    static std::string ClassifyRemoteCommandName(const HttpRequest & request) {
        if (request.path == "/mcp") {
            const std::string tool_name = ExtractJsonStringValue(request.body, "name");
            if (!tool_name.empty()) {
                if (tool_name == "local_cli" || tool_name == "codex_local_cli" || tool_name == "lan_agent_run_command") {
                    const std::string local_command = ExtractJsonStringValue(request.body, "command");
                    if (!local_command.empty()) {
                        return tool_name + ":" + local_command;
                    }
                }
                return tool_name;
            }
            const std::string method = ExtractJsonStringValue(request.body, "method");
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

    static std::string ClassifyRemoteRequestType(const HttpRequest & request) {
        if (request.path == "/health" || request.path == "/healthz" || request.path == "/runtime-overview") {
            return "health";
        }
        if (request.path == "/mcp") {
            const std::string tool_name = ExtractJsonStringValue(request.body, "name");
            if (tool_name == "local_cli" || tool_name == "codex_local_cli" || tool_name == "lan_agent_run_command") {
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

    static std::string DefaultRemoteTrigger(const HttpRequest & request) {
        if (request.method == "GET" || request.method == "HEAD" || request.method == "OPTIONS") {
            return "auto";
        }
        return "manual";
    }

    static std::string BuildRemoteControlEventJson(
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

    static std::string BuildMcpSessionId() {
        static std::mutex mutex;
        static unsigned long long next_id = 1;
        std::lock_guard<std::mutex> lock(mutex);
        return "mcp-" + TimeStampForFileName() + "-" + std::to_string(next_id++);
    }

    static void ApplyMcpSessionHeaders(
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

    static void ApplyMcpCorsHeaders(HttpResponseSpec * response) {
        response->headers["Access-Control-Allow-Origin"] = "*";
        response->headers["Access-Control-Allow-Methods"] = "GET, HEAD, OPTIONS, POST";
        response->headers["Access-Control-Allow-Headers"] =
            "Accept, Authorization, Content-Type, Mcp-Session-Id, X-Source-Thread, X-Trigger";
        response->headers["Access-Control-Expose-Headers"] = "Mcp-Session-Id";
    }

    static bool IsMcpOAuthAuthorizationServerPath(const std::string & path) {
        return path == "/.well-known/oauth-authorization-server" ||
               path == "/.well-known/oauth-authorization-server/mcp" ||
               path == "/mcp/.well-known/oauth-authorization-server";
    }

    static bool IsMcpOAuthProtectedResourcePath(const std::string & path) {
        return path == "/.well-known/oauth-protected-resource" ||
               path == "/.well-known/oauth-protected-resource/mcp" ||
               path == "/mcp/.well-known/oauth-protected-resource";
    }

    static std::string ExtractCliNamedArgument(
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

    static std::string BuildTaskResourceKey(
        const std::string & profile_name,
        const std::string & extra_arguments) {
        if (profile_name == "prepare_build_dir" ||
            profile_name == "check_build_dir" ||
            profile_name == "configure_project" ||
            profile_name == "build_target" ||
            profile_name == "build_and_test" ||
            profile_name == "run_ctest_target") {
            std::string build_dir = ExtractCliNamedArgument(extra_arguments, "-BuildDir");
            if (build_dir.empty()) {
                build_dir = ExtractCliNamedArgument(extra_arguments, "--build-dir");
            }
            if (!build_dir.empty()) {
                return "builddir:" + build_dir;
            }
        }
        return std::string();
    }

    static CommandResult WithComputedOutcome(const CommandResult & original) {
        CommandResult decorated = original;
        decorated.fields["outcome"] = ComputeCommandOutcome(original);
        return decorated;
    }

    static void PrintResultAsText(const CommandResult & result) {
        const CommandResult decorated = WithComputedOutcome(result);
        for (const auto & entry : decorated.fields) {
            std::cout << entry.first << "=" << entry.second << std::endl;
        }
    }
};

inline std::string TimeStampForFileName() {
    return CommOperations::TimeStampForFileName();
}

inline std::string IsoTimestampNow() {
    return CommOperations::IsoTimestampNow();
}

inline int CloseSocketPortable(SocketHandle socket_handle) {
    return CommOperations::CloseSocketPortable(socket_handle);
}

inline std::string Trim(const std::string & value) {
    return CommOperations::Trim(value);
}

inline std::string ToLowerAscii(std::string value) {
    return CommOperations::ToLowerAscii(std::move(value));
}

inline std::string CurrentPlatformName() {
    return CommOperations::CurrentPlatformName();
}

inline std::string ReadTextFileTrimmed(const std::string & path) {
    return CommOperations::ReadTextFileTrimmed(path);
}

inline std::string GetHostNamePortable() {
    return CommOperations::GetHostNamePortable();
}

inline std::string GetStableMachineId() {
    return CommOperations::GetStableMachineId();
}

inline std::string BuildRemoteMachineCodeSource() {
    return CommOperations::BuildRemoteMachineCodeSource();
}

inline std::uint64_t Fnv1a64(const std::string & text) {
    return CommOperations::Fnv1a64(text);
}

inline std::string FormatMachineCode(std::uint64_t value) {
    return CommOperations::FormatMachineCode(value);
}

inline std::string BuildRemoteMachineCode() {
    return CommOperations::BuildRemoteMachineCode();
}

inline bool ValidateRemoteMachineCode(
    const std::string & expected_code,
    std::string * error_message) {
    return CommOperations::ValidateRemoteMachineCode(expected_code, error_message);
}

inline std::string BuildRequestTimestampToken() {
    return CommOperations::BuildRequestTimestampToken();
}

inline std::string NormalizeRecentProbeCacheKey(const std::string & path) {
    if (path.empty()) {
        return std::string();
    }
    try {
        return ToLowerAscii(std::filesystem::path(path).lexically_normal().string());
    } catch (...) {
        return ToLowerAscii(path);
    }
}

inline std::unordered_map<std::string, std::uint64_t> & RecentProbePathCache() {
    static std::unordered_map<std::string, std::uint64_t> cache;
    return cache;
}

inline std::mutex & RecentProbePathCacheMutex() {
    static std::mutex mutex;
    return mutex;
}

inline void RememberRecentProbePath(const std::string & path) {
    const std::string key = NormalizeRecentProbeCacheKey(path);
    if (key.empty()) {
        return;
    }
    const std::uint64_t now_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    std::lock_guard<std::mutex> lock(RecentProbePathCacheMutex());
    RecentProbePathCache()[key] = now_ms;
}

inline bool HasRecentProbePath(
    const std::string & path,
    std::uint64_t max_age_ms = 10ULL * 60ULL * 1000ULL) {
    const std::string key = NormalizeRecentProbeCacheKey(path);
    if (key.empty()) {
        return false;
    }
    const std::uint64_t now_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    std::lock_guard<std::mutex> lock(RecentProbePathCacheMutex());
    auto & cache = RecentProbePathCache();
    const auto it = cache.find(key);
    if (it == cache.end()) {
        return false;
    }
    if (now_ms < it->second || (now_ms - it->second) > max_age_ms) {
        cache.erase(it);
        return false;
    }
    return true;
}

inline std::string SanitizeDispatchToken(
    const std::string & value,
    const std::string & fallback) {
    return CommOperations::SanitizeDispatchToken(value, fallback);
}

inline bool FindArgument(
    const std::vector<std::string> & arguments,
    const std::string & name,
    std::string * value) {
    return CommOperations::FindArgument(arguments, name, value);
}

inline std::string JoinRemainingArguments(
    const std::vector<std::string> & arguments,
    std::size_t start_index) {
    return CommOperations::JoinRemainingArguments(arguments, start_index);
}

inline std::string BuildLogPath(
    const AgentConfig & config,
    const std::string & prefix) {
    return CommOperations::BuildLogPath(config, prefix);
}

inline std::string BuildServerStatePath(const AgentConfig & config) {
    return CommOperations::BuildServerStatePath(config);
}

inline std::string BuildPatchAuditEventsPath(const AgentConfig & config) {
    return CommOperations::BuildPatchAuditEventsPath(config);
}

inline std::string GetServerLockFilePath() {
    return CommOperations::GetServerLockFilePath();
}

inline void IgnoreBrokenPipePortable() {
    CommOperations::IgnoreBrokenPipePortable();
}

inline std::string BuildRemoteControlEventsPath(const AgentConfig & config) {
    return CommOperations::BuildRemoteControlEventsPath(config);
}

inline std::string BuildAgentServerStdoutLogPath(const AgentConfig & config) {
    return CommOperations::BuildAgentServerStdoutLogPath(config);
}

inline void ConfigureAgentServerStdoutFileLog(
    const AgentConfig & config,
    bool enabled,
    const std::string & path = "") {
    CommOperations::ConfigureAgentServerStdoutFileLog(config, enabled, path);
}

inline bool IsAgentServerStdoutFileLogEnabled() {
    return CommOperations::IsAgentServerStdoutFileLogEnabled();
}

inline std::string GetAgentServerStdoutFileLogPath(const AgentConfig & config) {
    return CommOperations::GetAgentServerStdoutFileLogPath(config);
}

inline void AppendAgentServerStdoutFileLogLine(
    const AgentConfig & config,
    const std::string & line) {
    CommOperations::AppendAgentServerStdoutFileLogLine(config, line);
}

inline std::string BuildExperienceCardsPath(const AgentConfig & config) {
    return CommOperations::BuildExperienceCardsPath(config);
}

inline void WriteServerStateFile(
    const AgentConfig & config,
    const std::string & status,
    const std::string & detail = "") {
    CommOperations::WriteServerStateFile(config, status, detail);
}

inline void LogServerEvent(
    const AgentConfig & config,
    const std::string & event,
    const std::string & detail = "") {
    CommOperations::LogServerEvent(config, event, detail);
}

inline int GetLastSocketErrorCode() {
    return CommOperations::GetLastSocketErrorCode();
}

inline std::string DescribeSocketErrorCode(int error_code) {
    return CommOperations::DescribeSocketErrorCode(error_code);
}

inline bool StartsWithPath(
    const std::filesystem::path & path,
    const std::filesystem::path & prefix) {
    return CommOperations::StartsWithPath(path, prefix);
}

inline bool StartsWithAnyPath(
    const std::filesystem::path & path,
    const std::string & path_list) {
    return CommOperations::StartsWithAnyPath(path, path_list);
}

inline std::filesystem::path GetPrimaryWorkspaceRoot(const AgentConfig & config) {
    return CommOperations::GetPrimaryWorkspaceRoot(config);
}

inline std::vector<std::filesystem::path> GetWorkspaceRoots(const AgentConfig & config) {
    return CommOperations::GetWorkspaceRoots(config);
}

inline bool TryResolveAllowedPath(
    const AgentConfig & config,
    const std::string & raw_path,
    std::filesystem::path * normalized_path,
    std::string * error_message) {
    return CommOperations::TryResolveAllowedPath(config, raw_path, normalized_path, error_message);
}

inline bool TryResolveWorkspaceFilePath(
    const AgentConfig & config,
    const std::string & raw_path,
    std::filesystem::path * normalized_path,
    std::string * error_message) {
    return CommOperations::TryResolveWorkspaceFilePath(config, raw_path, normalized_path, error_message);
}

inline std::string UrlDecode(const std::string & value) {
    return CommOperations::UrlDecode(value);
}

inline std::string GetQueryParamValue(
    const HttpRequest & request,
    const std::string & key) {
    return CommOperations::GetQueryParamValue(request, key);
}

inline std::string ExtractTaskIdFromPath(const std::string & path) {
    return CommOperations::ExtractTaskIdFromPath(path);
}

inline bool IsTaskBoardPath(const std::string & path) {
    return CommOperations::IsTaskBoardPath(path);
}

inline std::string BuildTaskBoardEndpoint(const std::string & path) {
    return CommOperations::BuildTaskBoardEndpoint(path);
}

inline bool IsEnqueuePath(const std::string & path) {
    return CommOperations::IsEnqueuePath(path);
}

inline std::string ComputeCommandOutcome(const CommandResult & result) {
    return CommOperations::ComputeCommandOutcome(result);
}

inline std::string GetFieldOrDefault(
    const CommandResult & result,
    const std::string & key,
    const std::string & default_value) {
    return CommOperations::GetFieldOrDefault(result, key, default_value);
}

inline std::string DeriveEndpointBaseUrl(const std::string & endpoint) {
    const std::string normalized = Trim(endpoint);
    if (normalized.empty()) {
        return std::string();
    }
    const std::string chat_marker = "/v1/chat/completions";
    const std::string embed_marker = "/v1/embeddings";
    const std::string local_chat_marker = "/local-chat";
    const std::size_t chat_pos = normalized.find(chat_marker);
    if (chat_pos != std::string::npos) {
        return normalized.substr(0, chat_pos);
    }
    const std::size_t embed_pos = normalized.find(embed_marker);
    if (embed_pos != std::string::npos) {
        return normalized.substr(0, embed_pos);
    }
    const std::size_t local_chat_pos = normalized.find(local_chat_marker);
    if (local_chat_pos != std::string::npos) {
        return normalized.substr(0, local_chat_pos);
    }
    const std::size_t scheme_pos = normalized.find("://");
    const std::size_t host_start = scheme_pos == std::string::npos ? 0 : scheme_pos + 3;
    const std::size_t path_pos = normalized.find('/', host_start);
    if (path_pos != std::string::npos) {
        return normalized.substr(0, path_pos);
    }
    return normalized;
}

inline std::string DeriveEmbeddingFallbackEndpoint(const AgentConfig & config) {
    const std::string base_url = DeriveEndpointBaseUrl(config.generation_endpoint);
    return base_url.empty() ? std::string() : (base_url + "/v1/embeddings");
}

inline std::string DeriveLocalChatFallbackEndpoint(const AgentConfig & config) {
    return Trim(config.generation_endpoint);
}

inline bool ResolveReachableEndpoint(
    const std::string & primary_endpoint,
    const std::string & fallback_endpoint,
    int timeout_ms,
    std::string * resolved_endpoint,
    std::string * detail,
    std::string * source_label) {
    std::string primary_detail = "not configured";
    const bool primary_ready = !Trim(primary_endpoint).empty()
        && codex_lan_agent::CheckTcpEndpoint(primary_endpoint, timeout_ms, &primary_detail);
    if (primary_ready) {
        if (resolved_endpoint != nullptr) {
            *resolved_endpoint = Trim(primary_endpoint);
        }
        if (detail != nullptr) {
            *detail = primary_detail;
        }
        if (source_label != nullptr) {
            *source_label = "configured";
        }
        return true;
    }

    std::string fallback_detail = "not configured";
    const bool fallback_ready =
        !Trim(fallback_endpoint).empty()
        && Trim(fallback_endpoint) != Trim(primary_endpoint)
        && codex_lan_agent::CheckTcpEndpoint(fallback_endpoint, timeout_ms, &fallback_detail);
    if (fallback_ready) {
        if (resolved_endpoint != nullptr) {
            *resolved_endpoint = Trim(fallback_endpoint);
        }
        if (detail != nullptr) {
            *detail = primary_detail == "not configured"
                ? ("fallback active; " + fallback_detail)
                : ("configured endpoint unavailable (" + primary_detail + "); fallback active; " + fallback_detail);
        }
        if (source_label != nullptr) {
            *source_label = "generation_fallback";
        }
        return true;
    }

    if (resolved_endpoint != nullptr) {
        *resolved_endpoint = Trim(primary_endpoint);
    }
    if (detail != nullptr) {
        if (primary_detail != "not configured") {
            *detail = primary_detail;
        } else if (fallback_detail != "not configured" && !Trim(fallback_endpoint).empty()) {
            *detail = "configured endpoint not available; fallback also unavailable (" + fallback_detail + ")";
        } else {
            *detail = primary_detail;
        }
    }
    if (source_label != nullptr) {
        *source_label = "unreachable";
    }
    return false;
}

inline std::string ResultToJson(const CommandResult & result) {
    return CommOperations::ResultToJson(result);
}

template <typename... Args>
inline std::string FirstNonEmpty(const Args &... values) {
    return CommOperations::FirstNonEmpty(values...);
}
inline std::string BuildLocalCliEvidenceJson(const CommandResult& result) {
    return CommOperations::BuildLocalCliEvidenceJson(result);
}
inline std::string BuildTaskResourceKey(
    const std::string& profile_name,
    const std::string& extra_arguments) {
    return CommOperations::BuildTaskResourceKey(profile_name, extra_arguments);
}


inline CommandResult WithComputedOutcome(const CommandResult& original) {
    return CommOperations::WithComputedOutcome(original);
}

inline std::string ExtractCliNamedArgument(
    const std::string& arguments,
    const std::string& key) {
    return CommOperations::ExtractCliNamedArgument(arguments, key);
}

inline void PrintResultAsText(const CommandResult& result) {
    CommOperations::PrintResultAsText(result);
}
#if 0
inline std::string ExtractResultField(
    const std::string & response_body,
    const std::string & key) {
    return CommOperations::ExtractResultField(response_body, key);
}

inline std::string GetHeaderValue(
    const HttpRequest & request,
    const std::string & name) {
    return CommOperations::GetHeaderValue(request, name);
}

inline std::string FileNameFromPathString(const std::string & value) {
    return CommOperations::FileNameFromPathString(value);
}


inline std::string BuildMcpErrorResponse(
    const std::string & id_raw,
    int code,
    const std::string & message) {
    return CommOperations::BuildMcpErrorResponse(id_raw, code, message);
}

inline std::string BuildMcpInitializeResponse(const std::string & id_raw) {
    return CommOperations::BuildMcpInitializeResponse(id_raw);
}

inline std::string BuildMcpCapabilitiesResponse() {
    return CommOperations::BuildMcpCapabilitiesResponse();
}

inline std::string BuildMcpDiscoveryBaseUrl(const AgentConfig & config) {
    return CommOperations::BuildMcpDiscoveryBaseUrl(config);
}

inline std::string BuildMcpOAuthAuthorizationServerResponse(const AgentConfig & config) {
    return CommOperations::BuildMcpOAuthAuthorizationServerResponse(config);
}

inline std::string BuildMcpOAuthProtectedResourceResponse(const AgentConfig & config) {
    return CommOperations::BuildMcpOAuthProtectedResourceResponse(config);
}

inline bool HeaderContainsToken(
    const std::string & header_value,
    const std::string & token) {
    return CommOperations::HeaderContainsToken(header_value, token);
}

inline std::string BuildSseMessage(const std::string & json_body) {
    return CommOperations::BuildSseMessage(json_body);
}

inline bool LooksManualSourceThread(const std::string & source_thread) {
    return CommOperations::LooksManualSourceThread(source_thread);
}

inline std::string ClassifyUnifiedSourceLabel(
    const std::string & source_thread,
    const std::string & trigger,
    const HttpRequest & request) {
    return CommOperations::ClassifyUnifiedSourceLabel(source_thread, trigger, request);
}

inline std::string BuildTakeoverRelation(
    const std::string & source_label,
    const std::string & source_thread,
    const HttpRequest & request) {
    return CommOperations::BuildTakeoverRelation(source_label, source_thread, request);
}

inline std::string BuildRemoteTaskGroup(
    const std::string & task_id,
    const std::string & command_name,
    const std::string & request_type,
    const std::string & result_ref,
    const std::string & evidence_ref) {
    return CommOperations::BuildRemoteTaskGroup(
        task_id,
        command_name,
        request_type,
        result_ref,
        evidence_ref);
}

inline bool IsAiInteractionCommandName(const std::string & command_name) {
    return CommOperations::IsAiInteractionCommandName(command_name);
}

inline bool IsObservationNoiseCommandName(const std::string & command_name) {
    return CommOperations::IsObservationNoiseCommandName(command_name);
}

inline std::string ClassifyRemoteCommandName(const HttpRequest & request) {
    return CommOperations::ClassifyRemoteCommandName(request);
}

inline std::string ClassifyRemoteRequestType(const HttpRequest & request) {
    return CommOperations::ClassifyRemoteRequestType(request);
}

inline std::string DefaultRemoteTrigger(const HttpRequest & request) {
    return CommOperations::DefaultRemoteTrigger(request);
}

inline std::string BuildRemoteControlEventJson(
    const std::unordered_map<std::string, std::string> & fields) {
    return CommOperations::BuildRemoteControlEventJson(fields);
}

inline std::string BuildMcpSessionId() {
    return CommOperations::BuildMcpSessionId();
}

inline void ApplyMcpSessionHeaders(
    const HttpRequest & request,
    HttpResponseSpec * response,
    bool create_if_missing) {
    CommOperations::ApplyMcpSessionHeaders(request, response, create_if_missing);
}

inline void ApplyMcpCorsHeaders(HttpResponseSpec * response) {
    CommOperations::ApplyMcpCorsHeaders(response);
}

inline bool IsMcpOAuthAuthorizationServerPath(const std::string & path) {
    return CommOperations::IsMcpOAuthAuthorizationServerPath(path);
}

inline bool IsMcpOAuthProtectedResourcePath(const std::string & path) {
    return CommOperations::IsMcpOAuthProtectedResourcePath(path);
}

inline std::string BuildLocalCliEvidenceJson(const CommandResult & result) {
    return CommOperations::BuildLocalCliEvidenceJson(result);
}





#endif
