#pragma once

#include <string>
#include <unordered_map>

#ifndef CODEX_LAN_AGENT_SOCKET_HANDLE_DEFINED
#define CODEX_LAN_AGENT_SOCKET_HANDLE_DEFINED
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
constexpr int kSocketErrorResult = SOCKET_ERROR;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
constexpr int kSocketErrorResult = -1;
#endif
#endif

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
    kCxParserRuntime,
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
    int timeout_sec_override = -1;
    int stall_timeout_sec_override = -1;
    std::string status = "queued";
    std::string submitted_at;
    std::string started_at;
    std::string completed_at;
    CommandResult result;
};

struct PatchRequest {
    std::string request_id;
    std::string trace_id;
    std::string patch_id;
    std::string file_path;
    std::string old_hash;
    std::string new_content;
    std::string reason;
};

struct PatchPreviewResult {
    std::string request_id;
    std::string trace_id;
    std::string patch_id;
    std::string file_path;
    std::string normalized_path;
    std::string old_hash;
    std::string new_hash;
    std::string diff_hash;
    std::string patch_audit_id;
};

struct PatchApplyResult {
    std::string request_id;
    std::string trace_id;
    std::string patch_id;
    std::string file_path;
    std::string normalized_path;
    std::string old_hash;
    std::string new_hash;
    std::string diff_hash;
    std::string backup_path;
    std::string patch_audit_id;
};

struct PatchRevertResult {
    std::string request_id;
    std::string trace_id;
    std::string patch_id;
    std::string file_path;
    std::string normalized_path;
    std::string old_hash;
    std::string new_hash;
    std::string diff_hash;
    std::string backup_path;
    std::string patch_audit_id;
};

struct PatchVerifyResult {
    std::string request_id;
    std::string trace_id;
    std::string patch_id;
    std::string verify_id;
    std::string file_path;
    std::string normalized_path;
    std::string expected_hash;
    std::string actual_hash;
    std::string semantic_outcome;
    std::string repair_candidate_id;
};
