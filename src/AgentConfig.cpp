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
    loaded.config_dir = std::filesystem::path(config_path).parent_path().string();

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
        } else if (key == "generation_endpoint") {
            loaded.generation_endpoint = value;
        } else if (key == "embedding_endpoint") {
            loaded.embedding_endpoint = value;
        } else if (key == "local_chat_endpoint") {
            loaded.local_chat_endpoint = value;
        } else if (key == "task_timeout_sec") {
            if (!ParseInt(value, &loaded.task_timeout_sec)) {
                if (error_message) {
                    *error_message = "invalid task_timeout_sec on line " + std::to_string(line_number);
                }
                return false;
            }
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

    *config = loaded;
    return true;
}

std::string JoinPath(
    const std::string & left,
    const std::string & right) {
    return (std::filesystem::path(left) / right).string();
}

}  // namespace codex_lan_agent
