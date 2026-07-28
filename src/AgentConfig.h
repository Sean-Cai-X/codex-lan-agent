#pragma once

#include <string>
#include <unordered_map>

namespace codex_lan_agent {

struct AgentConfig {
    std::string listen_host = "0.0.0.0";
    int listen_port = 18080;
    std::string workspace_root;
    std::string log_root;
    std::string data_root;
    std::string dialog_slices_root;
    std::string session_dispatch_root;
    std::string remote_session_slices_root;
    std::string generation_endpoint = "http://127.0.0.1:8095/v1/chat/completions";
    std::string embedding_endpoint = "http://127.0.0.1:8096/v1/embeddings";
    std::string local_chat_endpoint;
    std::string tool_config_path;
    std::string result_fields_config_path;
    std::string cmm_binary_path;
    std::string cmm_store_path;
    std::string clang_indexer_binary_path;
    int task_timeout_sec = 1800;
    int build_target_stall_timeout_sec = 0;
    int configure_project_stall_timeout_sec = 0;
    std::unordered_map<std::string, std::string> profiles;
    std::unordered_map<std::string, int> profile_timeouts_sec;
    std::unordered_map<std::string, int> profile_stall_timeouts_sec;
    std::unordered_map<std::string, std::string> cxparser_runtime_commands;
    std::unordered_map<std::string, int> cxparser_runtime_timeouts_sec;
    std::unordered_map<std::string, int> cxparser_runtime_stall_timeouts_sec;
    std::string config_dir;
};

bool LoadAgentConfig(
    const std::string & config_path,
    AgentConfig * config,
    std::string * error_message);

std::string JoinPath(
    const std::string & left,
    const std::string & right);

}  // namespace codex_lan_agent
