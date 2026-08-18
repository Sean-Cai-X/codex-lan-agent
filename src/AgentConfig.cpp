#include "AgentConfig.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace codex_lan_agent {
namespace {

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

std::string StripQuotes(const std::string & value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

bool StartsWith(const std::string & value, const std::string & prefix) {
    return value.rfind(prefix, 0) == 0;
}

void ReplaceAll(
    std::string * value,
    const std::string & token,
    const std::string & replacement) {
    std::size_t position = value->find(token);
    while (position != std::string::npos) {
        value->replace(position, token.size(), replacement);
        position = value->find(token, position + replacement.size());
    }
}

std::string PlatformPowershellCommand() {
#ifdef _WIN32
    return "powershell -ExecutionPolicy Bypass";
#else
    return "pwsh";
#endif
}

std::string PlatformName() {
#ifdef _WIN32
    return "windows";
#else
    return "linux";
#endif
}

std::string PlatformPathSeparator() {
#ifdef _WIN32
    return "\\";
#else
    return "/";
#endif
}

std::string ExpandConfigValue(
    const std::string & raw_value,
    const std::string & config_dir) {
    std::string expanded = raw_value;
    ReplaceAll(&expanded, "${AGENT_DIR}", config_dir);
    ReplaceAll(&expanded, "${POWERSHELL}", PlatformPowershellCommand());
    ReplaceAll(&expanded, "${PLATFORM}", PlatformName());
    ReplaceAll(&expanded, "${PATH_SEP}", PlatformPathSeparator());
    return expanded;
}

std::string NormalizePathValue(
    const std::string & raw_value,
    const std::string & config_dir) {
    if (raw_value.empty()) {
        return std::string();
    }
    std::filesystem::path candidate(raw_value);
    if (candidate.is_relative()) {
        candidate = std::filesystem::path(config_dir) / candidate;
    }
    return candidate.lexically_normal().string();
}

std::string NormalizePathListValue(
    const std::string & raw_value,
    const std::string & config_dir) {
    std::ostringstream output;
    std::size_t start = 0;
    bool first = true;
    while (start <= raw_value.size()) {
        const std::size_t end = raw_value.find(';', start);
        const std::string item = Trim(raw_value.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start));
        if (!item.empty()) {
            if (!first) {
                output << ";";
            }
            output << NormalizePathValue(item, config_dir);
            first = false;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return output.str();
}
bool ParseInt(
    const std::string & text,
    int * parsed_value) {
    std::istringstream buffer(text);
    int value = 0;
    buffer >> value;
    if (buffer.fail()) {
        return false;
    }
    *parsed_value = value;
    return true;
}

bool ParseBool(
    const std::string & text,
    bool * parsed_value) {
    std::string lowered;
    lowered.reserve(text.size());
    for (unsigned char ch : text) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
        *parsed_value = true;
        return true;
    }
    if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
        *parsed_value = false;
        return true;
    }
    return false;
}

}  // namespace

bool LoadAgentConfig(
    const std::string & config_path,
    AgentConfig * config,
    std::string * error_message) {
    if (!config) {
        if (error_message) {
            *error_message = "config output pointer is null";
        }
        return false;
    }

    std::ifstream input(config_path);
    if (!input.is_open()) {
        if (error_message) {
            *error_message = "failed to open config file: " + config_path;
        }
        return false;
    }

    AgentConfig loaded = *config;
    const std::filesystem::path absolute_config_path = std::filesystem::absolute(std::filesystem::path(config_path));
    loaded.config_dir = absolute_config_path.parent_path().lexically_normal().string();

    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        const std::size_t separator = trimmed.find('=');
        if (separator == std::string::npos) {
            if (error_message) {
                *error_message = "invalid config line " + std::to_string(line_number) + ": missing '='";
            }
            return false;
        }

        const std::string key = Trim(trimmed.substr(0, separator));
        const std::string value = ExpandConfigValue(
            StripQuotes(Trim(trimmed.substr(separator + 1))),
            loaded.config_dir);

        if (key == "workspace_root") {
            loaded.workspace_root = value;
        } else if (key == "manual_workspace_root") {
            loaded.manual_workspace_root = value;
        } else if (key == "allowed_roots") {
            loaded.allowed_roots = value;
        } else if (key == "listen_host") {
            loaded.listen_host = value;
        } else if (key == "listen_port") {
            if (!ParseInt(value, &loaded.listen_port)) {
                if (error_message) {
                    *error_message = "invalid listen_port on line " + std::to_string(line_number);
                }
                return false;
            }
        } else if (key == "log_root") {
            loaded.log_root = value;
        } else if (key == "data_root") {
            loaded.data_root = value;
        } else if (key == "dialog_slices_root") {
            loaded.dialog_slices_root = value;
        } else if (key == "session_dispatch_root") {
            loaded.session_dispatch_root = value;
        } else if (key == "remote_session_slices_root") {
            loaded.remote_session_slices_root = value;
        } else if (key == "generation_endpoint") {
            loaded.generation_endpoint = value;
        } else if (key == "embedding_endpoint") {
            loaded.embedding_endpoint = value;
        } else if (key == "local_chat_endpoint") {
            loaded.local_chat_endpoint = value;
        } else if (key == "tool_config_path") {
            loaded.tool_config_path = value;
        } else if (key == "result_fields_config_path") {
            loaded.result_fields_config_path = value;
        } else if (key == "cmm_binary_path") {
            loaded.cmm_binary_path = value;
        } else if (key == "cmm_store_path") {
            loaded.cmm_store_path = value;
        } else if (key == "clang_indexer_binary_path") {
            loaded.clang_indexer_binary_path = value;
        } else if (key == "optfile_write_enabled") {
            if (!ParseBool(value, &loaded.optfile_write_enabled)) {
                if (error_message) {
                    *error_message = "invalid optfile_write_enabled on line " + std::to_string(line_number);
                }
                return false;
            }
        } else if (key == "direct_file_write_enabled") {
            if (!ParseBool(value, &loaded.direct_file_write_enabled)) {
                if (error_message) {
                    *error_message = "invalid direct_file_write_enabled on line " + std::to_string(line_number);
                }
                return false;
            }
        } else if (key == "task_timeout_sec") {
            if (!ParseInt(value, &loaded.task_timeout_sec)) {
                if (error_message) {
                    *error_message = "invalid task_timeout_sec on line " + std::to_string(line_number);
                }
                return false;
            }
        } else if (key == "build_target_stall_timeout_sec") {
            if (!ParseInt(value, &loaded.build_target_stall_timeout_sec) ||
                loaded.build_target_stall_timeout_sec < 0) {
                if (error_message) {
                    *error_message = "invalid build_target_stall_timeout_sec on line " + std::to_string(line_number);
                }
                return false;
            }
        } else if (key == "configure_project_stall_timeout_sec") {
            if (!ParseInt(value, &loaded.configure_project_stall_timeout_sec) ||
                loaded.configure_project_stall_timeout_sec < 0) {
                if (error_message) {
                    *error_message = "invalid configure_project_stall_timeout_sec on line " + std::to_string(line_number);
                }
                return false;
            }
        } else if (StartsWith(key, "profile_timeout.")) {
            int parsed_value = 0;
            if (!ParseInt(value, &parsed_value) || parsed_value <= 0) {
                if (error_message) {
                    *error_message = "invalid " + key + " on line " + std::to_string(line_number);
                }
                return false;
            }
            loaded.profile_timeouts_sec[key.substr(16)] = parsed_value;
        } else if (StartsWith(key, "profile_stall_timeout.")) {
            int parsed_value = 0;
            if (!ParseInt(value, &parsed_value) || parsed_value < 0) {
                if (error_message) {
                    *error_message = "invalid " + key + " on line " + std::to_string(line_number);
                }
                return false;
            }
            loaded.profile_stall_timeouts_sec[key.substr(22)] = parsed_value;
        } else if (StartsWith(key, "cxparser_runtime_timeout.")) {
            int parsed_value = 0;
            if (!ParseInt(value, &parsed_value) || parsed_value <= 0) {
                if (error_message) {
                    *error_message = "invalid " + key + " on line " + std::to_string(line_number);
                }
                return false;
            }
            loaded.cxparser_runtime_timeouts_sec[key.substr(25)] = parsed_value;
        } else if (StartsWith(key, "cxparser_runtime_stall_timeout.")) {
            int parsed_value = 0;
            if (!ParseInt(value, &parsed_value) || parsed_value <= 0) {
                if (error_message) {
                    *error_message = "invalid " + key + " on line " + std::to_string(line_number);
                }
                return false;
            }
            loaded.cxparser_runtime_stall_timeouts_sec[key.substr(31)] = parsed_value;
        } else if (StartsWith(key, "cxparser_runtime.")) {
            loaded.cxparser_runtime_commands[key.substr(17)] = value;
        } else if (StartsWith(key, "profile.")) {
            loaded.profiles[key.substr(8)] = value;
        }
    }

    if (loaded.workspace_root.empty()) {
        if (error_message) {
            *error_message = "workspace_root is required";
        }
        return false;
    }

    if (loaded.log_root.empty()) {
        if (error_message) {
            *error_message = "log_root is required";
        }
        return false;
    }

    loaded.workspace_root = NormalizePathListValue(loaded.workspace_root, loaded.config_dir);
    loaded.manual_workspace_root = NormalizePathListValue(loaded.manual_workspace_root, loaded.config_dir);
    loaded.allowed_roots = NormalizePathListValue(loaded.allowed_roots, loaded.config_dir);
    loaded.log_root = NormalizePathValue(loaded.log_root, loaded.config_dir);
    loaded.data_root = NormalizePathValue(
        loaded.data_root.empty() ? loaded.log_root : loaded.data_root,
        loaded.config_dir);
    loaded.dialog_slices_root = NormalizePathValue(
        loaded.dialog_slices_root.empty()
            ? (std::filesystem::path(loaded.data_root) / "dialog_slices").string()
            : loaded.dialog_slices_root,
        loaded.config_dir);
    loaded.session_dispatch_root = NormalizePathValue(
        loaded.session_dispatch_root.empty()
            ? (std::filesystem::path(loaded.data_root) / "session_dispatch").string()
            : loaded.session_dispatch_root,
        loaded.config_dir);
    loaded.remote_session_slices_root = NormalizePathValue(
        loaded.remote_session_slices_root.empty()
            ? (std::filesystem::path(loaded.data_root) / "remote_session_slices").string()
            : loaded.remote_session_slices_root,
        loaded.config_dir);
    loaded.tool_config_path = NormalizePathValue(loaded.tool_config_path, loaded.config_dir);
    loaded.result_fields_config_path = NormalizePathValue(loaded.result_fields_config_path, loaded.config_dir);
    loaded.cmm_binary_path = NormalizePathValue(loaded.cmm_binary_path, loaded.config_dir);
    loaded.cmm_store_path = NormalizePathValue(loaded.cmm_store_path, loaded.config_dir);
    loaded.clang_indexer_binary_path = NormalizePathValue(
        loaded.clang_indexer_binary_path, loaded.config_dir);

    if (loaded.tool_config_path.empty() && !loaded.profiles.empty()) {
        loaded.tool_config_path = absolute_config_path.lexically_normal().string();
    }

    *config = loaded;
    return true;
}

std::string JoinPath(
    const std::string & left,
    const std::string & right) {
    return (std::filesystem::path(left) / right).string();
}

}  // namespace codex_lan_agent