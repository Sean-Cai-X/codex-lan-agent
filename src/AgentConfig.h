#pragma once

#include <string>
#include <unordered_map>

namespace codex_lan_agent {

struct AgentConfig {
    std::string listen_host = "0.0.0.0";
    int listen_port = 18080;
    std::string workspace_root;
    std::string log_root;
    std::string generation_endpoint = "http://127.0.0.1:8095/v1/chat/completions";
    std::string embedding_endpoint = "http://127.0.0.1:8096/v1/embeddings";
    std::string local_chat_endpoint;
    int task_timeout_sec = 1800;
    std::unordered_map<std::string, std::string> profiles;
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
