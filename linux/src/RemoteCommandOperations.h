#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

inline std::vector<std::string> ParseRemoteCommandArguments(
    const std::string & text,
    std::string * error_message) {
    std::vector<std::string> values;
    std::string current;
    char quote = '\0';
    bool escaping = false;
    bool token_started = false;
    for (char ch : text) {
        if (escaping) {
            current.push_back(ch);
            escaping = false;
            token_started = true;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            token_started = true;
            continue;
        }
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            } else {
                current.push_back(ch);
            }
            token_started = true;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            token_started = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (token_started) {
                values.push_back(current);
                current.clear();
                token_started = false;
            }
            continue;
        }
        current.push_back(ch);
        token_started = true;
    }
    if (escaping || quote != '\0') {
        if (error_message != nullptr) {
            *error_message = escaping
                ? "arguments_text ends with an incomplete escape"
                : "arguments_text contains an unterminated quote";
        }
        return {};
    }
    if (token_started) {
        values.push_back(current);
    }
    return values;
}

inline std::string QuoteRemoteCommandArgument(const std::string & value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

inline bool IsRemoteCommandExecutableAllowed(
    const std::string & executable,
    bool allow_shell) {
    static const std::unordered_set<std::string> kAllowed = {
        "gh", "git", "cmake", "ninja", "ctest", "make",
        "gcc", "g++", "clang", "clang++", "python3",
        "mkdir", "ls", "pwd", "find", "rg"
    };
    if (kAllowed.find(executable) != kAllowed.end()) {
        return true;
    }
    return allow_shell && (executable == "bash" || executable == "sh");
}

inline std::string BuildRemoteCommandAllowedExecutablesJson(bool allow_shell) {
    std::ostringstream output;
    output << "[\"gh\",\"git\",\"cmake\",\"ninja\",\"ctest\",\"make\",";
    output << "\"gcc\",\"g++\",\"clang\",\"clang++\",\"python3\",";
    output << "\"mkdir\",\"ls\",\"pwd\",\"find\",\"rg\"";
    if (allow_shell) {
        output << ",\"bash\",\"sh\"";
    }
    output << "]";
    return output.str();
}

inline std::string BuildRemoteCommandCapabilityJson(const AgentConfig & config) {
    std::ostringstream output;
    output << "{"
           << "\"capability_id\":\"remote_command\","
           << "\"target_tool_name\":\"lan_agent_run_command\","
           << "\"route_mode\":\"call\","
           << "\"enabled\":" << (config.remote_command_enabled ? "true" : "false") << ","
           << "\"authorization_required\":true,"
           << "\"authorization_scheme\":\"machine-code\","
           << "\"authorization_value_exposed\":false,"
           << "\"working_directory_root\":\""
           << codex_lan_agent::JsonEscape(config.remote_command_root) << "\","
           << "\"shell_enabled\":" << (config.remote_command_allow_shell ? "true" : "false") << ","
           << "\"allowed_executables\":"
           << BuildRemoteCommandAllowedExecutablesJson(config.remote_command_allow_shell) << ","
           << "\"timeout_sec\":{\"default\":600,\"minimum\":1,\"maximum\":3600},"
           << "\"required_arguments\":[\"executable\",\"authorization\"],"
           << "\"optional_arguments\":[\"arguments_text\",\"working_directory\",\"timeout_sec\"],"
           << "\"request_template\":{"
           << "\"mode\":\"call\","
           << "\"target_tool_name\":\"lan_agent_run_command\","
           << "\"executable\":\"git\","
           << "\"arguments_text\":\"status --short\","
           << "\"working_directory\":\""
           << codex_lan_agent::JsonEscape(config.remote_command_root) << "\","
           << "\"authorization\":\"<machine-code>\","
           << "\"timeout_sec\":600}"
           << "}";
    return output.str();
}

inline bool IsPathInsideRemoteCommandRoot(
    const std::filesystem::path & path,
    const std::filesystem::path & root) {
    std::error_code path_error;
    std::error_code root_error;
    const auto normalized_path = std::filesystem::weakly_canonical(path, path_error);
    const auto normalized_root = std::filesystem::weakly_canonical(root, root_error);
    if (path_error || root_error) {
        return false;
    }
    auto path_it = normalized_path.begin();
    auto root_it = normalized_root.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++path_it) {
        if (path_it == normalized_path.end() || *path_it != *root_it) {
            return false;
        }
    }
    return true;
}

inline std::string BuildUniqueRemoteCommandLogPath(const AgentConfig & config) {
    static std::atomic<unsigned long long> sequence{0};
    const unsigned long long next_sequence = sequence.fetch_add(1, std::memory_order_relaxed);
    return BuildLogPath(config, "remote_command_" + std::to_string(next_sequence));
}

inline std::string ReadRemoteCommandLogTail(
    const std::string & path,
    std::size_t max_bytes = 65536) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    const std::streamoff start = size > static_cast<std::streamoff>(max_bytes)
        ? size - static_cast<std::streamoff>(max_bytes)
        : 0;
    input.seekg(start, std::ios::beg);
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

inline CommandResult RunRemoteCommandResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    result.fields["capability_id"] = "remote_command";
    result.fields["request_type"] = "remote_command_execution";
    result.fields["risk"] = "high";
    result.fields["remote_command_enabled"] = config.remote_command_enabled ? "true" : "false";
    result.fields["remote_command_root"] = config.remote_command_root;

    if (!config.remote_command_enabled) {
        result.ok = false;
        result.exit_code = 403;
        result.fields["error"] = "remote command execution is disabled by configuration";
        return result;
    }

    const std::string authorization = params.GetString("authorization");
    if (authorization.empty() || authorization != BuildRemoteMachineCode()) {
        result.ok = false;
        result.exit_code = 403;
        result.fields["error"] = "remote command authorization failed";
        return result;
    }

    const std::string executable = Trim(params.GetString("executable"));
    if (executable.empty() || executable.find('/') != std::string::npos ||
        executable.find('\\') != std::string::npos ||
        !IsRemoteCommandExecutableAllowed(executable, config.remote_command_allow_shell)) {
        result.ok = false;
        result.exit_code = 403;
        result.fields["error"] = "remote command executable is not allowed";
        result.fields["executable"] = executable;
        return result;
    }

    const std::filesystem::path command_root(config.remote_command_root);
    const std::string requested_working_directory = params.GetString(
        "working_directory",
        config.remote_command_root);
    const std::filesystem::path working_directory = std::filesystem::absolute(
        std::filesystem::path(requested_working_directory)).lexically_normal();
    std::error_code path_error;
    if (!std::filesystem::is_directory(working_directory, path_error) || path_error ||
        !IsPathInsideRemoteCommandRoot(working_directory, command_root)) {
        result.ok = false;
        result.exit_code = 403;
        result.fields["error"] = "working_directory is missing or outside remote_command_root";
        result.fields["working_directory"] = working_directory.string();
        return result;
    }

    std::string parse_error;
    const std::vector<std::string> arguments = ParseRemoteCommandArguments(
        params.GetString("arguments_text"),
        &parse_error);
    if (!parse_error.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = parse_error;
        return result;
    }

    std::string command_line = QuoteRemoteCommandArgument(executable);
    for (const std::string & argument : arguments) {
        command_line += " " + QuoteRemoteCommandArgument(argument);
    }

    const int requested_timeout = params.GetInt("timeout_sec", 600);
    const int timeout_sec = std::max(1, std::min(requested_timeout, 3600));
    const std::string log_path = BuildUniqueRemoteCommandLogPath(config);
    codex_lan_agent::ProcessRunResult process_result;
    std::string process_error;
    const bool launched = codex_lan_agent::RunCommandWithLog(
        command_line,
        working_directory.string(),
        log_path,
        timeout_sec,
        0,
        &process_result,
        &process_error);

    result.ok = launched && process_result.exit_code == 0;
    result.exit_code = launched ? process_result.exit_code : 126;
    result.fields["status"] = result.ok ? "success" : "failed";
    result.fields["executable"] = executable;
    result.fields["argument_count"] = std::to_string(arguments.size());
    result.fields["working_directory"] = working_directory.string();
    result.fields["timeout_sec"] = std::to_string(timeout_sec);
    result.fields["process_id"] = std::to_string(process_result.process_id);
    result.fields["runtime_sec"] = std::to_string(process_result.runtime_sec);
    result.fields["completion_reason"] = process_result.completion_reason;
    result.fields["timed_out"] = process_result.timed_out ? "true" : "false";
    result.fields["log_path"] = log_path;
    result.fields["result_ref"] = log_path;
    result.fields["evidence_ref"] = log_path;
    result.fields["output_tail"] = ReadRemoteCommandLogTail(log_path);
    result.fields["summary"] = result.ok
        ? "remote command completed"
        : "remote command failed";
    if (!launched) {
        result.fields["error"] = process_error;
    }
    return result;
}
