#include "CmmBridge.h"

#include "HttpClient.h"
#include "ProcessRunner.h"
#include "StructuredJsonOperations.h"

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace codex_lan_agent {
namespace {

std::atomic<int> g_cmm_bridge_sequence{0};

std::string CmmBridgeTimestampTag() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count();
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
        "%04d%02d%02d_%02d%02d%02d_%03lld",
        local_tm.tm_year + 1900,
        local_tm.tm_mon + 1,
        local_tm.tm_mday,
        local_tm.tm_hour,
        local_tm.tm_min,
        local_tm.tm_sec,
        static_cast<long long>(ms % 1000));
    return buffer;
}

std::string GetExecutableDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::string();
    }
    std::filesystem::path executable_path(buffer);
    return executable_path.parent_path().string();
#else
    char buffer[PATH_MAX] = {0};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return std::string();
    }
    buffer[length] = '\0';
    std::filesystem::path executable_path(buffer);
    return executable_path.parent_path().string();
#endif
}

std::string MakeCmmBridgeBaseDirectory(const AgentConfig & config) {
    std::filesystem::path base(config.log_root);
    base /= "cmm_bridge";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base.string();
}

std::string MakeCmmBridgeLogPath(
    const AgentConfig & config,
    const std::string & tool_name) {
    const std::string base = MakeCmmBridgeBaseDirectory(config);
    const int sequence = ++g_cmm_bridge_sequence;
    std::ostringstream path;
    path << base << "\\" << tool_name << "_"
         << CmmBridgeTimestampTag() << "_" << sequence << ".log";
    return path.str();
}

std::string TrimLine(const std::string & line) {
    std::size_t start = 0;
    while (start < line.size() &&
           std::isspace(static_cast<unsigned char>(line[start])) != 0) {
        ++start;
    }
    std::size_t end = line.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(line[end - 1])) != 0) {
        --end;
    }
    return line.substr(start, end - start);
}

bool LooksLikeLogMarker(const std::string & trimmed) {
    // CMM logs bracketed markers such as [task_start] or [task_end] before
    // the actual JSON payload. A real JSON array starts with a value token
    // (string, number, object, array, true/false/null), not an alphabetic
    // word like "task_start".
    if (trimmed.size() < 3 || trimmed.front() != '[') {
        return false;
    }
    return std::isalpha(static_cast<unsigned char>(trimmed[1])) != 0;
}

std::string ExtractCliJsonOutput(const std::string & log_path) {
    std::ifstream input(log_path, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        return std::string();
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = TrimLine(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.front() == '{') {
            return trimmed;
        }
        if (trimmed.front() == '[' && !LooksLikeLogMarker(trimmed)) {
            return trimmed;
        }
    }
    return std::string();
}

// Quote a single command-line argument so that the Windows or POSIX argv parser
// reconstructs the exact original string. Follows the inverse of
// CommandLineToArgvW rules used by MSVC and Go binaries on Windows.
std::string QuoteCommandArgument(const std::string & value) {
    if (value.empty()) {
        return "\"\"";
    }

    bool needs_quotes = false;
    for (char ch : value) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
            ch == '"' || ch == '\0') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return value;
    }

    std::string result;
    result.push_back('"');
    std::size_t backslash_count = 0;
    for (char ch : value) {
        if (ch == '\\') {
            ++backslash_count;
        } else if (ch == '"') {
            // Double every backslash that preceded the quote, then add \"
            result.append(backslash_count * 2 + 1, '\\');
            result.push_back('"');
            backslash_count = 0;
        } else {
            if (backslash_count > 0) {
                result.append(backslash_count, '\\');
                backslash_count = 0;
            }
            result.push_back(ch);
        }
    }
    if (backslash_count > 0) {
        // Double trailing backslashes before the closing quote
        result.append(backslash_count * 2, '\\');
    }
    result.push_back('"');
    return result;
}

CommandResult MakeCmmErrorResult(
    int exit_code,
    const std::string & message,
    const std::string & log_path) {
    CommandResult result;
    result.ok = false;
    result.exit_code = exit_code;
    result.fields["error"] = message;
    if (!log_path.empty()) {
        result.fields["log_path"] = log_path;
    }
    result.fields["cmm_bridge_status"] = "error";
    return result;
}

bool ShouldEmitRawJson(const std::string & raw) {
    if (raw == "true" || raw == "false" || raw == "null") {
        return true;
    }
    if (!raw.empty() &&
        (raw.front() == '[' || raw.front() == '{' || raw.front() == '"')) {
        return true;
    }
    // Integer or floating point literal, optional sign/exponent.
    static const std::regex number_re(
        R"(^-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?$)");
    return std::regex_match(raw, number_re);
}

constexpr std::size_t kCmmProjectNameMaxLen = 200;

bool IsPathRootSyntax(const std::string & path) {
    if (path.empty()) {
        return false;
    }
    for (char ch : path) {
        if (ch != '/' && ch != '\\' && ch != ':') {
            return false;
        }
    }
    return true;
}

uint32_t Fnv1aHash32(const std::string & text) {
    uint32_t h = 2166136261u;
    for (unsigned char c : text) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

// Mirror cbm_project_name_from_path() in CMM's src/pipeline/fqn.c so that
// codex-lan-agent can accept either the filesystem path or the already
// normalized CMM project name.
std::string NormalizeCmmProjectName(const std::string & abs_path) {
    if (abs_path.empty() || IsPathRootSyntax(abs_path)) {
        return "root";
    }

    std::string path = abs_path;
    for (char & ch : path) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    if (path.empty()) {
        return "root";
    }

    static const char hex_digits[] = "0123456789abcdef";
    std::string mapped;
    mapped.reserve(path.size() * 2 + 1);
    for (unsigned char c : path) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                          c == '-';
        if (safe) {
            mapped.push_back(static_cast<char>(c));
        } else if (c >= 0x80) {
            mapped.push_back(hex_digits[(c >> 4) & 0xF]);
            mapped.push_back(hex_digits[c & 0xF]);
        } else {
            mapped.push_back('-');
        }
    }

    std::string collapsed;
    collapsed.reserve(mapped.size());
    char prev = 0;
    for (char ch : mapped) {
        if ((ch == '-' && prev == '-') || (ch == '.' && prev == '.')) {
            continue;
        }
        collapsed.push_back(ch);
        prev = ch;
    }

    std::size_t start = 0;
    while (start < collapsed.size() &&
           (collapsed[start] == '-' || collapsed[start] == '.')) {
        ++start;
    }
    std::string result = collapsed.substr(start);

    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }

    if (result.empty()) {
        return "root";
    }

    if (result.size() > kCmmProjectNameMaxLen) {
        const uint32_t h = Fnv1aHash32(result);
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "-%08x", h);
        result.resize(kCmmProjectNameMaxLen - 9);
        result += suffix;
    }

    return result;
}

}  // namespace

std::string ResolveCmmBinaryPath(const AgentConfig & config) {
    if (!config.cmm_binary_path.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(config.cmm_binary_path, ec)) {
            return config.cmm_binary_path;
        }
    }

    const std::string exe_dir = GetExecutableDirectory();
    if (!exe_dir.empty()) {
        std::error_code ec;
        std::filesystem::path candidate(exe_dir);
#ifdef _WIN32
        candidate /= "codebase-memory-mcp.exe";
#else
        candidate /= "codebase-memory-mcp";
#endif
        if (std::filesystem::exists(candidate, ec)) {
            return candidate.string();
        }
    }

    return std::string();
}

std::string BuildCmmArgsJson(
    const JsonRequestView & params,
    const std::vector<std::string> & simple_keys) {
    const std::string raw_args_json = params.GetRawJson("args_json");
    if (!raw_args_json.empty()) {
        return raw_args_json;
    }

    std::ostringstream json;
    json << "{";
    bool first = true;
    for (const std::string & key : simple_keys) {
        const std::string raw = params.GetRawJson(key);
        if (raw.empty()) {
            continue;
        }
        if (!first) {
            json << ",";
        }
        first = false;
        json << "\"" << key << "\":";
        if (key == "project") {
            // CMM expects the normalized project name derived from the path.
            // Accept either an already-normalized name or a filesystem path.
            const std::string path_value = params.GetString("project");
            json << "\"" << JsonEscape(NormalizeCmmProjectName(path_value)) << "\"";
        } else if (ShouldEmitRawJson(raw)) {
            json << raw;
        } else {
            json << "\"" << JsonEscape(raw) << "\"";
        }
    }
    json << "}";
    return json.str();
}

CommandResult RunCmmToolCli(
    const AgentConfig & config,
    const std::string & tool_name,
    const std::string & args_json,
    int timeout_ms) {
    const std::string binary_path = ResolveCmmBinaryPath(config);
    if (binary_path.empty()) {
        return MakeCmmErrorResult(
            127,
            "codebase-memory-mcp binary not found; configure cmm_binary_path or place the "
            "binary next to codex_lan_agent",
            std::string());
    }

    const std::string log_path = MakeCmmBridgeLogPath(config, tool_name);

    std::ostringstream command_line;
    command_line << QuoteCommandArgument(binary_path)
                 << " cli "
                 << tool_name
                 << " "
                 << QuoteCommandArgument(args_json);

    const std::string working_directory =
        config.workspace_root.empty() ? config.config_dir : config.workspace_root;

    ProcessRunResult process_result;
    std::string error_message;
    const bool run_ok = RunCommandWithLog(
        command_line.str(),
        working_directory,
        log_path,
        (timeout_ms + 999) / 1000,
        0,
        &process_result,
        &error_message);

    if (!run_ok) {
        return MakeCmmErrorResult(
            125,
            "failed to run CMM tool: " +
                (error_message.empty() ? "unknown process error" : error_message),
            log_path);
    }

    const std::string raw_output = ExtractCliJsonOutput(log_path);

    if (process_result.exit_code != 0) {
        return MakeCmmErrorResult(
            process_result.exit_code,
            "CMM tool exited with code " + std::to_string(process_result.exit_code) +
                (raw_output.empty() ? "" : ": " + raw_output),
            log_path);
    }

    if (raw_output.empty()) {
        return MakeCmmErrorResult(
            124,
            "CMM tool produced no JSON output",
            log_path);
    }

    const bool is_error = ExtractJsonRawValue(raw_output, "isError") == "true";
    if (is_error) {
        const std::string error_text = ExtractJsonString(raw_output, "error");
        return MakeCmmErrorResult(
            1,
            error_text.empty() ? raw_output : error_text,
            log_path);
    }

    CommandResult result;
    result.ok = true;
    result.exit_code = 0;
    result.fields["result_json"] = raw_output;
    result.fields["result_text"] = ExtractJsonString(raw_output, "text");
    result.fields["log_path"] = log_path;
    result.fields["cmm_binary_path"] = binary_path;
    result.fields["cmm_tool"] = tool_name;
    result.fields["cmm_bridge_status"] = "ok";
    return result;
}

}  // namespace codex_lan_agent
