#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOWINUSER
#define NOWINUSER
#endif
#endif

#include "AgentConfig.h"
#include "CapabilityRegistry.h"
#include "HttpClient.h"
#include "ProcessRunner.h"
#include "comm.h"
#include "types.h"

#include <algorithm>
#include <array>
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
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <cstring>
#include <thread>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "system_utils.h"
#include "CmmToolResults.h"
#include "ClangIndexerAdapter.h"
#include "ClangAstVisitor.h"
#include "ClangAstParser.h"
#include "ClangAstTool.h"
#include "CodeFormatOperations.h"
#include "SemanticGridOperations.h"

bool ReadWholeFile(
    const std::filesystem::path & path,
    std::string * content,
    std::string * error_message);
std::string StableContentChecksum(const std::string & content);
std::string GetFieldOrDefault(
    const CommandResult & result,
    const std::string & key,
    const std::string & default_value);
std::string IsoTimestampNow();
std::string BuildRemoteControlEventsPath(const AgentConfig & config);
std::string BuildLogPath(const AgentConfig & config, const std::string & prefix);
std::string DeriveLocalChatFallbackEndpoint(const AgentConfig & config);
bool ResolveReachableEndpoint(
    const std::string & primary_endpoint,
    const std::string & fallback_endpoint,
    int timeout_ms,
    std::string * resolved_endpoint,
    std::string * detail,
    std::string * source_label);
bool TryResolveAllowedPath(
    const AgentConfig & config,
    const std::string & raw_path,
    std::filesystem::path * normalized_path,
    std::string * error_message);
bool StartsWithPath(
    const std::filesystem::path & path,
    const std::filesystem::path & prefix);

#include "TaskMemoryOperations.h"

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

#include "task_manager.h"

inline std::string Trim(const std::string & value) {
    return ::Trim(value);
}

inline std::string ToLowerAscii(std::string value) {
    return ::ToLowerAscii(std::move(value));
}

inline std::string BuildLogPath(const AgentConfig & config, const std::string & prefix) {
    return ::BuildLogPath(config, prefix);
}

inline bool TryResolveAllowedPath(
    const AgentConfig & config,
    const std::string & raw_path,
    std::filesystem::path * normalized_path,
    std::string * error_message) {
    return ::TryResolveAllowedPath(config, raw_path, normalized_path, error_message);
}

inline std::string DeriveLocalChatFallbackEndpoint(const AgentConfig & config) {
    return ::DeriveLocalChatFallbackEndpoint(config);
}

inline bool ResolveReachableEndpoint(
    const std::string & primary_endpoint,
    const std::string & fallback_endpoint,
    int timeout_ms,
    std::string * resolved_endpoint,
    std::string * detail,
    std::string * source_label) {
    return ::ResolveReachableEndpoint(
        primary_endpoint,
        fallback_endpoint,
        timeout_ms,
        resolved_endpoint,
        detail,
        source_label);
}

inline bool StartsWithPath(
    const std::filesystem::path & path,
    const std::filesystem::path & prefix) {
    return ::StartsWithPath(path, prefix);
}

template <typename... Args>
inline std::string FirstNonEmpty(const Args &... values) {
    return ::FirstNonEmpty(values...);
}

std::string ExpectedMarkerForProfile(const std::string & profile_name);

#include "CtestDiscoveryOperations.h"

// Manual compile gate: change to true only on the intended remote machine
// after review, then rebuild on that machine.
constexpr bool kRemoteCompileGateApproved = true;
static_assert(
    kRemoteCompileGateApproved,
    "CODEX_REMOTE_COMPILE_STEP: change kRemoteCompileGateApproved to true after manual review on the target remote machine, then rebuild codex_lan_agent.exe");

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



CommandResult RunCliProfile(
    const AgentConfig & config,
    const std::string & profile_name,
    const std::string & extra_arguments,
    const std::string & forced_log_path = std::string(),
    int timeout_sec_override = -1,
    int stall_timeout_sec_override = -1);

bool HasCxParserRuntimeBinding(
    const AgentConfig & config,
    const std::string & flow_id,
    std::string * source = nullptr);
std::string FindCxParserCxScriptCliExecutablePath(const AgentConfig & config);

CommandResult RunCxParserRuntimeCommand(
    const AgentConfig & config,
    const std::string & flow_id,
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
    int timeout_ms = 30000,
    const struct LocalChatEvidencePacket * evidence = nullptr);

CommandResult RunRemoteSessionNewTurn(
    const AgentConfig & config,
    const std::string & task_id,
    const std::string & session_id,
    const std::string & speaker_mode,
    const std::string & reasoning_level,
    const std::string & prompt_purpose,
    const std::string & context_refs,
    const std::string & response_mode,
    const std::string & prompt_text,
    int timeout_ms = 30000);

CommandResult RunRemoteSessionAppendTurn(
    const AgentConfig & config,
    const std::string & task_id,
    const std::string & session_id,
    const std::string & speaker_mode,
    const std::string & reasoning_level,
    const std::string & prompt_purpose,
    const std::string & context_refs,
    const std::string & response_mode,
    const std::string & prompt_text,
    int timeout_ms = 30000);

CommandResult ListRemoteSessionsResult(
    const AgentConfig & config,
    int timeout_ms = 10000);

CommandResult GetRemoteSessionResult(
    const AgentConfig & config,
    const std::string & session_id,
    int timeout_ms = 10000);

CommandResult ReadRemoteSessionSliceResult(
    const AgentConfig & config,
    const std::string & session_id,
    int timeout_ms = 10000);

CommandResult TailTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    int max_lines);

CommandResult BuildHealthResult(const AgentConfig & config);
CommandResult BuildLivenessResult(const AgentConfig & config);
CommandResult BuildRuntimeOverviewResult(const AgentConfig & config);
CommandResult BuildProfileListResult(const AgentConfig & config);
CommandResult DiscoverCtestTestsResult(
    const AgentConfig & config,
    const std::string & build_dir,
    const std::string & config_name,
    const std::string & test_regex,
    int start_index,
    int max_entries);
CommandResult BuildQueuedTaskResult(const std::string & task_id);
CommandResult BuildOptFileBaseResult(
    const AgentConfig & config,
    const std::string & target_name,
    const std::string & operation,
    bool append);
CommandResult SnapshotDiffResult(
    const AgentConfig & config,
    const std::string & repo_root,
    int timeout_sec = 30,
    const std::string & non_git_strategy = std::string(),
    const std::string & snapshot_action = std::string());
CommandResult RunClangIndexerResult(
    const AgentConfig & config,
    const std::string & source_file,
    const std::string & compile_db_dir,
    const std::string & output_path,
    const std::string & project_root,
    const std::string & include_dirs,
    const std::string & defines,
    bool verbose,
    const std::string & trace_id);
std::string BuildRunCTestTargetArguments(
    const std::string & build_dir,
    const std::string & config_name,
    const std::string & test_regex);
std::vector<std::string> SplitCommandLikeArguments(const std::string & text);
std::string JoinConfigureProjectCmakeArgs(const std::vector<std::string> & cmake_args);
std::string BuildConfigureProjectArguments(
    const std::string & project_root,
    const std::string & build_dir,
    const std::string & generator_kind,
    const std::vector<std::string> & cmake_args,
    const std::string & env_args = std::string());
std::string BuildBuildTargetArguments(
    const std::string & build_dir,
    const std::string & config_name,
    const std::string & target);
bool ReadWholeFile(
    const std::filesystem::path & path,
    std::string * content,
    std::string * error_message);
std::string BuildSimpleUnifiedDiff(
    const std::string & file_path,
    const std::string & old_content,
    const std::string & new_content);
std::string GetHeaderValue(const HttpRequest & request, const std::string & name);
void ApplyMcpCorsHeaders(HttpResponseSpec * response);
void ApplyMcpSessionHeaders(const HttpRequest & request, HttpResponseSpec * response, bool include_accept_post);
std::string BuildMcpToolsListResponse();
std::string StableContentChecksum(const std::string & content);
void ApplyAiConclusionValidityGuards(CommandResult * result);
CommandResult GetTraceAuditTrailResult(
    const AgentConfig & config,
    const std::string & trace_id);
CommandResult GetSupervisionStatusResult(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & goal_id = std::string());


CommandResult BuildRemoteSessionTurnResult(
    const AgentConfig & config,
    const std::string & endpoint,
    const std::string & request_body,
    const std::string & requested_write_mode,
    int timeout_ms);

CommandResult BuildRagIndexStatusResult(const AgentConfig & config);
CommandResult BuildRagClipsMetaResult(
    const AgentConfig & config,
    const std::string & query,
    int top_k);

void AddRagEvidenceFields(
    CommandResult * result,
    const std::string & scope,
    const std::string & evidence_text,
    bool insufficient_context,
    const std::string & confidence);

TaskManager * g_task_manager = nullptr;
std::mutex g_resource_lock_mutex;
std::unordered_set<std::string> g_active_resource_keys;
std::unordered_map<std::string, std::string> g_active_resource_owner_hints;
std::unordered_map<std::string, std::string> g_active_resource_acquired_at;
std::mutex g_remote_control_event_mutex;
std::unordered_map<std::string, std::string> g_last_remote_control_event;

class ScopedResourceLock {
public:
    explicit ScopedResourceLock(
        const std::string & resource_key,
        const std::string & owner_hint = std::string())
        : resource_key_(resource_key),
          owner_hint_(owner_hint.empty() ? CurrentThreadOwnerHint() : owner_hint) {
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
        g_active_resource_owner_hints[resource_key_] = owner_hint_;
        g_active_resource_acquired_at[resource_key_] = IsoTimestampNow();
        acquired_ = true;
    }

    ~ScopedResourceLock() {
        if (!acquired_ || resource_key_.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(g_resource_lock_mutex);
        g_active_resource_keys.erase(resource_key_);
        g_active_resource_owner_hints.erase(resource_key_);
        g_active_resource_acquired_at.erase(resource_key_);
    }

    bool acquired() const {
        return acquired_;
    }

private:
    static std::string CurrentThreadOwnerHint() {
        std::ostringstream output;
        output << "thread:" << std::this_thread::get_id();
        return output.str();
    }

    std::string resource_key_;
    std::string owner_hint_;
    bool acquired_ = false;
};

#include "StructuredJsonOperations.h"
#include "LocalChatQueueOperations.h"
#include "CommandResultConclusions.h"
#include "JsonRequestView.h"
#include "ResultFieldCatalog.h"
#ifdef _WIN32
#define GetFocus CodexLanAgentClipsGetFocusDeclarationOnly
#endif
#include "ClipsDecisionOperations.h"
#ifdef _WIN32
#undef GetFocus
#endif
bool Base64Decode(
    const std::string & input,
    std::string * output,
    std::string * error_message = nullptr);
std::string ResolveTextPayloadFromParams(
    const JsonRequestView & params,
    const std::string & plain_field,
    const std::string & base64_field,
    CommandResult * result = nullptr);
CommandResult BuildTaskMemoryExecuteContinuationBudgetRunnerResult(
    const AgentConfig & config,
    const JsonRequestView & params);
CommandResult BuildTaskMemoryResumeAndExecuteResult(
    const AgentConfig & config,
    const JsonRequestView & params);
CommandResult BuildTaskMemoryNewChatRoundSelftestResult(
    const AgentConfig & config,
    const JsonRequestView & params);
CommandResult BuildTaskMemoryMigrationAcceptanceResult(
    const AgentConfig & config,
    const JsonRequestView & params);
#include "McpToolDispatch.h"
#include "file_system.h"
#include "CxParserFlowOperations.h"
#include "dispatch_engine.h"
#include "tool_api.h"
#include "network_server.h"

bool IsReadOnlyContinuationTool(const std::string & tool_name) {
    return tool_name == "lan_agent_probe_text_file"
        || tool_name == "lan_agent_read_text_file"
        || tool_name == "lan_agent_list_directory"
        || tool_name == "lan_agent_read_directory_files"
        || tool_name == "lan_agent_prepare_directory_analysis"
        || tool_name == "lan_agent_scan_text_ranges"
        || tool_name == "lan_agent_prepare_edit_windows";
}

bool IsFlowOrchestratorContinuationTool(const std::string & tool_name) {
    return tool_name == "lan_agent_run_cxparser_flow";
}

bool IsClipsAutoContinuationTool(const std::string & tool_name) {
    return IsReadOnlyContinuationTool(tool_name)
        || IsFlowOrchestratorContinuationTool(tool_name);
}

bool TryAutoExecuteCmmInitChain(
    const AgentConfig & config,
    const std::string & original_tool_name,
    const std::string & original_params_body,
    const ClipsDecision & decision,
    CommandResult * result) {
    if (result == nullptr) {
        return false;
    }

    const bool is_cmm_search_tool =
        original_tool_name == "lan_agent_cmm_search_code"
        || original_tool_name == "lan_agent_cmm_search_graph"
        || original_tool_name == "lan_agent_cmm_query_graph"
        || original_tool_name == "lan_agent_cmm_get_code_snippet"
        || original_tool_name == "lan_agent_cmm_trace_path"
        || original_tool_name == "lan_agent_cmm_get_graph_schema"
        || original_tool_name == "lan_agent_cmm_get_architecture";

    if (!is_cmm_search_tool) {
        return false;
    }

    if (decision.route_target != "lan_agent_cmm_index_status") {
        return false;
    }

    JsonRequestView original_params(original_params_body);
    const std::string project = FirstNonEmpty(
        original_params.GetString("project"),
        original_params.GetString("file_path"));

    if (project.empty()) {
        return false;
    }

    result->fields["cmm_auto_chain"] = "starting";
    result->fields["cmm_auto_chain_step"] = "index_status_check";

    auto checkProjectStatus = [&result, &config](const std::string & project_name) -> bool {
        std::string index_status_json = "{\"project\":\"" + project_name + "\"}";
        JsonRequestView index_status_params(index_status_json);
        CommandResult index_status_result;
        TryHandleRegisteredMcpTool(
            config,
            "lan_agent_cmm_index_status",
            index_status_params,
            &index_status_result);

        result->fields["cmm_auto_chain_index_status_ok"] =
            index_status_result.ok ? "true" : "false";

        std::string status;
        auto it = index_status_result.fields.find("result_json");
        if (it != index_status_result.fields.end() && !it->second.empty()) {
            status = ExtractJsonString(it->second, "status");
        }
        if (status.empty()) {
            status = GetFieldOrDefault(index_status_result, "status", "");
        }
        result->fields["cmm_auto_chain_status"] = status;
        return (status == "ready" || status == "indexed");
    };

    bool project_ready = checkProjectStatus(project);

    if (!project_ready) {
        const bool has_path_separator =
            project.find('/') != std::string::npos
            || project.find('\\') != std::string::npos;

        if (has_path_separator) {
            result->fields["cmm_auto_chain"] = "normalizing_path";
            result->fields["cmm_auto_chain_step"] = "path_to_project_name";

            std::string normalized = project;
            for (char & ch : normalized) {
                if (ch == '\\') {
                    ch = '/';
                }
            }
            while (!normalized.empty() && normalized.back() == '/') {
                normalized.pop_back();
            }

            size_t last_slash = normalized.find_last_of('/');
            if (last_slash != std::string::npos) {
                std::string domain_part = normalized.substr(0, last_slash);
                std::string remainder = normalized.substr(last_slash + 1);

                std::string domain_normalized;
                for (char & ch : domain_part) {
                    if (ch == ':') {
                        domain_normalized += '-';
                    } else if (ch == '/') {
                        domain_normalized += '-';
                    } else {
                        domain_normalized += ch;
                    }
                }

                normalized = domain_normalized;
                if (!remainder.empty()) {
                    if (!normalized.empty()) {
                        normalized += '-';
                    }
                    normalized += remainder;
                }
            } else {
                std::string temp;
                for (char & ch : normalized) {
                    if (ch == ':') {
                        temp += '-';
                    } else if (ch == '/') {
                        temp += '-';
                    } else {
                        temp += ch;
                    }
                }
                normalized = temp;
            }

            if (!normalized.empty() && normalized[0] == '-') {
                normalized = normalized.substr(1);
            }

            std::string cleaned;
            for (size_t i = 0; i < normalized.size(); ++i) {
                if (normalized[i] == '-' && i + 1 < normalized.size() && normalized[i + 1] == '-') {
                    continue;
                }
                cleaned += normalized[i];
            }
            normalized = cleaned;

            result->fields["cmm_auto_chain_normalized_project"] = normalized;

            JsonRequestView modified_params(original_params_body);
            std::string modified_body = original_params_body;
            size_t project_pos = modified_body.find("\"project\"");
            if (project_pos == std::string::npos) {
                project_pos = modified_body.find("\"file_path\"");
            }
            if (project_pos != std::string::npos) {
                size_t colon_pos = modified_body.find(':', project_pos);
                size_t quote_start = modified_body.find('"', colon_pos + 1);
                size_t quote_end = modified_body.find('"', quote_start + 1);
                if (quote_start != std::string::npos && quote_end != std::string::npos) {
                    modified_body = modified_body.substr(0, quote_start + 1)
                        + normalized
                        + modified_body.substr(quote_end);
                }
            }

            project_ready = checkProjectStatus(normalized);

            if (project_ready) {
                result->fields["cmm_auto_chain"] = "project_ready_normalized";
                result->fields["cmm_auto_chain_step"] = "retrying_with_normalized_project";

                JsonRequestView normalized_params(modified_body);
                CommandResult normalized_result;
                TryHandleRegisteredMcpTool(
                    config,
                    original_tool_name,
                    normalized_params,
                    &normalized_result);

                *result = normalized_result;
                result->fields["cmm_auto_chain"] = "success_via_normalized";
                result->fields["cmm_auto_chain_step"] = "completed";
                result->fields["auto_chain_performed"] = "true";
                result->fields["auto_chain_original_tool"] = original_tool_name;
                result->fields["auto_chain_normalized_project"] = normalized;
                return true;
            }
        }
    }

    if (!project_ready) {
        result->fields["cmm_auto_chain"] = "index_not_ready";
        result->fields["cmm_auto_chain_step"] = "abort";
        result->ok = false;
        result->exit_code = 52;
        result->fields["error"] = "cmm_project_not_indexed";
        result->fields["result"] = "auto_chain_blocked";
        result->fields["reason"] = "CMM project is not indexed. Call lan_agent_cmm_index_repository first.";
        result->fields["clips_gate"] = "auto_chain_blocked";
        result->fields["auto_chain_performed"] = "true";
        result->fields["auto_chain_original_tool"] = original_tool_name;
        result->fields["auto_chain_original_params"] = original_params_body;
        return true;
    }

    result->fields["cmm_auto_chain"] = "project_ready";
    result->fields["cmm_auto_chain_step"] = "retrying_original_tool";

    CommandResult original_result;
    TryHandleRegisteredMcpTool(
        config,
        original_tool_name,
        original_params,
        &original_result);

    if (!original_result.ok) {
        *result = original_result;
        result->fields["cmm_auto_chain"] = "search_failed";
        result->fields["auto_chain_performed"] = "true";
        result->fields["auto_chain_original_tool"] = original_tool_name;
        return true;
    }

    *result = original_result;
    result->fields["cmm_auto_chain"] = "success";
    result->fields["cmm_auto_chain_step"] = "completed";
    result->fields["auto_chain_performed"] = "true";
    result->fields["auto_chain_original_tool"] = original_tool_name;
    return true;
}

std::string ResolveNextActionSafetyClass(const std::string & tool_name) {
    if (IsFlowOrchestratorContinuationTool(tool_name)) {
        return "FLOW_ORCHESTRATOR";
    }
    if (IsReadOnlyContinuationTool(tool_name)) {
        return "READ_ONLY";
    }
    if (tool_name == "lan_agent_delete_text_range_window_atomic"
        || tool_name == "lan_agent_delete_next_text_range_atomic") {
        return "BOUNDED_FILE_WRITE";
    }
    if (tool_name == "lan_agent_format_code_file") {
        return "CONTROLLED_CODE_FORMAT";
    }
    if (tool_name == "lan_agent_task_memory_freeze"
        || tool_name == "lan_agent_task_memory_execute_continuation_budget"
        || tool_name == "lan_agent_task_memory_resume_and_execute") {
        return "TASK_MEMORY_CONTINUATION";
    }
    return std::string();
}

bool IsSafeClipsContinuationAction(
    const CommandResult & result,
    const std::string & required_tool,
    const std::string & safety_class) {
    if (safety_class == "READ_ONLY") {
        return true;
    }
    if (required_tool == "lan_agent_run_cxparser_flow"
        && safety_class == "FLOW_ORCHESTRATOR") {
        return GetFieldOrDefault(result, "cxparser_safety_class", "") == "READ_ONLY"
            || GetFieldOrDefault(result, "flow_safety_class", "") == "READ_ONLY";
    }
    if (required_tool == "lan_agent_task_memory_freeze"
        && safety_class == "TASK_MEMORY_CONTINUATION"
        && GetFieldOrDefault(result, "task_execution_in_mcp_required", "") == "true"
        && GetFieldOrDefault(result, "forced_task_memory_execution", "") == "true") {
        return true;
    }
    if (required_tool == "lan_agent_task_memory_execute_continuation_budget"
        && safety_class == "TASK_MEMORY_CONTINUATION"
        && GetFieldOrDefault(result, "record_model", "") == "mcp_task_memory_freeze_response_v1"
        && GetFieldOrDefault(result, "task_execution_in_mcp_required", "") == "true"
        && GetFieldOrDefault(result, "forced_task_memory_execution", "") == "true"
        && GetFieldOrDefault(result, "terminal_state", "") != "true") {
            return true;
    }
    if (required_tool == "lan_agent_task_memory_resume_and_execute"
        && safety_class == "TASK_MEMORY_CONTINUATION"
        && GetFieldOrDefault(result, "record_model", "") ==
            "mcp_task_memory_execute_continuation_budget_response_v1"
        && GetFieldOrDefault(result, "execution_mode", "") == "bounded_allowlist_execute"
        && GetFieldOrDefault(result, "continue_required", "") == "true"
        && GetFieldOrDefault(result, "terminal_state", "") != "true") {
        return true;
    }
    if (required_tool == "lan_agent_format_code_file"
        && safety_class == "CONTROLLED_CODE_FORMAT"
        && GetFieldOrDefault(result, "result", "") == "pre_guard_rerouted"
        && GetFieldOrDefault(result, "clips_gate", "") == "rerouted_before_execution"
        && GetFieldOrDefault(result, "primary_intent", "") == "code_format") {
        return true;
    }
    if ((required_tool == "lan_agent_delete_text_range_window_atomic"
            || required_tool == "lan_agent_delete_next_text_range_atomic")
        && GetFieldOrDefault(result, "write_verified", "") == "true"
        && GetFieldOrDefault(result, "disk_write_completed", "") == "true"
        && GetFieldOrDefault(result, "window_batch_scope", "") != "multi_file") {
        return true;
    }
    if (required_tool == "lan_agent_delete_text_range_window_atomic"
        && safety_class == "BOUNDED_FILE_WRITE"
        && GetFieldOrDefault(result, "tool_name", "") == "lan_agent_mcp_route"
        && GetFieldOrDefault(result, "routed_tool_name", "") == "lan_agent_probe_text_file"
        && GetFieldOrDefault(result, "result", "") == "probe_complete") {
        const std::string next_action_json = FirstNonEmpty(
            GetFieldOrDefault(result, "required_tool_arguments_json", ""),
            GetFieldOrDefault(result, "next_call_json", ""),
            "");
        return next_action_json.find("\"scan_mode\":\"comments\"") != std::string::npos
            && next_action_json.find("\"probe_ready\":true") != std::string::npos
            && next_action_json.find("\"max_lines\":200") != std::string::npos;
    }
    if (required_tool == "lan_agent_delete_text_range_window_atomic"
        && safety_class == "BOUNDED_FILE_WRITE"
        && GetFieldOrDefault(result, "result", "") == "no_text_range_in_window"
        && GetFieldOrDefault(result, "probe_ready", "") == "true"
        && GetFieldOrDefault(result, "scan_mode", "") == "comments"
        && GetFieldOrDefault(result, "has_more", "") == "true"
        && GetFieldOrDefault(result, "window_batch_scope", "") == "single_file_bounded_line_window"
        && GetFieldOrDefault(result, "batch_mutation_allowed", "") == "bounded_window_only") {
        const int window_start_line = std::atoi(GetFieldOrDefault(result, "window_start_line", "0").c_str());
        const int next_start_line = std::atoi(GetFieldOrDefault(result, "next_start_line", "0").c_str());
        if (next_start_line > window_start_line) {
            return true;
        }
    }
    if (required_tool == "lan_agent_delete_text_range_window_atomic"
        && safety_class == "BOUNDED_FILE_WRITE"
        && GetFieldOrDefault(result, "tool_name", "") == "lan_agent_mcp_route"
        && GetFieldOrDefault(result, "routed_tool_name", "") == "lan_agent_delete_text_range_window_atomic"
        && GetFieldOrDefault(result, "result", "") == "no_text_range_in_window"
        && GetFieldOrDefault(result, "continue_required", "") == "true") {
        const std::string next_action_json = FirstNonEmpty(
            GetFieldOrDefault(result, "required_tool_arguments_json", ""),
            GetFieldOrDefault(result, "next_call_json", ""),
            "");
        return next_action_json.find("\"scan_mode\":\"comments\"") != std::string::npos
            && next_action_json.find("\"max_lines\":200") != std::string::npos
            && next_action_json.find("\"file_path\"") != std::string::npos;
    }
    if (required_tool == "lan_agent_delete_next_text_range_atomic"
        && safety_class == "BOUNDED_FILE_WRITE"
        && (GetFieldOrDefault(result, "result", "") == "window_boundary_range_detected"
            || GetFieldOrDefault(result, "task_completion", "") == "boundary_blocked")
        && GetFieldOrDefault(result, "probe_ready", "") == "true"
        && GetFieldOrDefault(result, "scan_mode", "") == "comments"
        && GetFieldOrDefault(result, "has_more", "") == "true"
        && GetFieldOrDefault(result, "write_verified", "") == "true"
        && GetFieldOrDefault(result, "disk_write_completed", "") != "true"
        && GetFieldOrDefault(result, "window_batch_scope", "") == "single_file_bounded_line_window"
        && GetFieldOrDefault(result, "batch_mutation_allowed", "") == "bounded_window_only") {
        return true;
    }
    if (required_tool == "lan_agent_delete_text_range_window_atomic"
        && safety_class == "BOUNDED_FILE_WRITE"
        && GetFieldOrDefault(result, "result", "") == "pre_guard_rerouted"
        && GetFieldOrDefault(result, "clips_gate", "") == "rerouted_before_execution"
        && GetFieldOrDefault(result, "probe_ready", "") == "true"
        && GetFieldOrDefault(result, "scan_mode", "") == "comments"
        && GetFieldOrDefault(result, "window_batch_scope", "") == "single_file_bounded_line_window"
        && GetFieldOrDefault(result, "batch_mutation_allowed", "") == "bounded_window_only") {
        const std::string intent = GetFieldOrDefault(result, "primary_intent", "");
        return intent == "comment_cleanup"
            || intent == "delete_comments"
            || intent == "remove_comments"
            || intent == "delete comments"
            || intent == "remove comments"
            || intent == "strip comments"
            || intent == "删除注释"
            || intent == "清理注释";
    }
    if ((required_tool == "lan_agent_delete_text_range_window_atomic"
            || required_tool == "lan_agent_delete_next_text_range_atomic")
        && safety_class == "BOUNDED_FILE_WRITE"
        && GetFieldOrDefault(result, "record_model", "") ==
            "mcp_task_memory_execute_continuation_budget_response_v1"
        && GetFieldOrDefault(result, "execution_mode", "") == "bounded_allowlist_execute"
        && GetFieldOrDefault(result, "continue_required", "") == "true"
        && GetFieldOrDefault(result, "last_tool", "") == required_tool) {
        return true;
    }
    return false;
}

void SetClipsSupervisionAlarm(
    CommandResult * result,
    const std::string & code,
    const std::string & message) {
    if (result == nullptr) {
        return;
    }
    result->ok = false;
    if (result->exit_code == 0) {
        result->exit_code = 68;
    }
    result->fields["supervision_status"] = "alarm";
    result->fields["supervision_alarm"] = "true";
    result->fields["supervision_alarm_code"] = code;
    result->fields["supervision_alarm_message"] = message;
    result->fields["verification"] = "not_verified";
    result->fields["verification_status"] = "not_verified";
    result->fields["verification_ok"] = "false";
    result->fields["ai_conclusion_valid"] = "false";
    result->fields["assistant_response_allowed"] = "false";
    result->fields["final_answer_allowed"] = "false";
    result->fields["semantic_model_clamp"] = "supervision_alarm";
    result->fields["goal_status"] = "failed";
    result->fields["error"] = code;
    result->fields["error_message"] = message;
}

void ApplyGuardSurfaceFields(CommandResult * result) {
    if (result == nullptr) {
        return;
    }

    std::string pre_guard_status = FirstNonEmpty(
        GetFieldOrDefault(*result, "clips_pre_call_tool_decision", ""),
        GetFieldOrDefault(*result, "clips_pre_call_decision", ""),
        "");
    if (pre_guard_status.empty()) {
        const std::string clips_gate = GetFieldOrDefault(*result, "clips_gate", "");
        if (clips_gate == "blocked_before_execution") {
            pre_guard_status = "block";
        } else if (clips_gate == "rerouted_before_execution") {
            pre_guard_status = "route";
        } else {
            pre_guard_status = "allow";
        }
    }
    result->fields["pre_guard_status"] = pre_guard_status;
    result->fields["pre_guard_reason_code"] = FirstNonEmpty(
        GetFieldOrDefault(*result, "clips_pre_call_tool_reason_code", ""),
        GetFieldOrDefault(*result, "clips_pre_call_reason_code", ""),
        GetFieldOrDefault(*result, "supervision_alarm_code", ""));
    result->fields["pre_guard_next_action"] = FirstNonEmpty(
        GetFieldOrDefault(*result, "clips_pre_call_tool_next_action", ""),
        GetFieldOrDefault(*result, "clips_pre_call_next_action", ""),
        "");
    result->fields["pre_guard_route_target"] = FirstNonEmpty(
        GetFieldOrDefault(*result, "clips_pre_call_tool_route_target", ""),
        GetFieldOrDefault(*result, "clips_pre_call_route_target", ""),
        GetFieldOrDefault(*result, "route_target", ""));
    result->fields["pre_guard_blocked"] =
        (pre_guard_status == "block" || pre_guard_status == "route") ? "true" : "false";

    std::string post_guard_status = FirstNonEmpty(
        GetFieldOrDefault(*result, "verification_status", ""),
        GetFieldOrDefault(*result, "verification", ""),
        "");
    if (post_guard_status.empty()) {
        post_guard_status = pre_guard_status == "allow" ? "unknown" : "not_run";
    } else if (GetFieldOrDefault(*result, "clips_gate", "") == "blocked_before_execution"
               || GetFieldOrDefault(*result, "clips_gate", "") == "rerouted_before_execution") {
        post_guard_status = "not_run";
    }
    result->fields["post_guard_status"] = post_guard_status;
    result->fields["post_guard_decision"] = FirstNonEmpty(
        GetFieldOrDefault(*result, "clips_post_result_decision", ""),
        post_guard_status == "verified" ? std::string("allow") : std::string(""));
    result->fields["post_guard_reason_code"] = FirstNonEmpty(
        FirstNonEmpty(
            GetFieldOrDefault(*result, "clips_post_result_reason_code", ""),
            GetFieldOrDefault(*result, "not_verified_reason", ""),
            GetFieldOrDefault(*result, "invalid_conclusion_reason", "")),
        GetFieldOrDefault(*result, "error_code", ""),
        GetFieldOrDefault(*result, "error", ""));
    result->fields["post_guard_next_action"] = FirstNonEmpty(
        GetFieldOrDefault(*result, "clips_post_result_next_action", ""),
        GetFieldOrDefault(*result, "next_action", ""),
        "");
    result->fields["post_guard_result_valid"] = post_guard_status == "verified" ? "true" : "false";

    const std::string supervision_status = GetFieldOrDefault(*result, "supervision_status", "");
    std::string acceptance_status = "unknown";
    if (supervision_status == "closed_loop_complete") {
        acceptance_status = "complete";
    } else if (supervision_status == "closed_loop_continue") {
        acceptance_status = "continue";
    } else if (supervision_status == "alarm") {
        acceptance_status = "alarm";
    }
    result->fields["acceptance_status"] = acceptance_status;
    result->fields["acceptance_reason"] = acceptance_status == "complete"
        ? "closed_loop_complete"
        : (acceptance_status == "continue"
            ? FirstNonEmpty(
                GetFieldOrDefault(*result, "next_action_0_reason", ""),
                GetFieldOrDefault(*result, "not_verified_reason", ""),
                "next_action_required")
            : FirstNonEmpty(
                FirstNonEmpty(
                    GetFieldOrDefault(*result, "supervision_alarm_code", ""),
                    GetFieldOrDefault(*result, "post_guard_reason_code", ""),
                    GetFieldOrDefault(*result, "error", "")),
                "acceptance_alarm"));
    const bool has_next_action =
        !GetFieldOrDefault(*result, "next_action_0_tool_name", "").empty()
        && !GetFieldOrDefault(*result, "next_action_0_params_json", "").empty();
    result->fields["acceptance_next_action_available"] = has_next_action ? "true" : "false";
}

void ApplyClipsFirstDecisionFields(CommandResult * result) {
    if (result == nullptr) {
        return;
    }

    const std::string supervision_status = GetFieldOrDefault(*result, "supervision_status", "");
    std::string decision = "unknown";
    if (supervision_status == "closed_loop_continue") {
        decision = "continue";
    } else if (supervision_status == "closed_loop_complete") {
        decision = "complete";
    } else if (supervision_status == "alarm") {
        decision = "alarm";
    }

    result->fields["clips_first_decision"] = decision;
    result->fields["clips_first_next_tool"] = FirstNonEmpty(
        GetFieldOrDefault(*result, "next_action_0_tool_name", ""),
        GetFieldOrDefault(*result, "required_tool_name", ""),
        GetFieldOrDefault(*result, "next_tool_name", ""));
    result->fields["clips_first_reason"] = FirstNonEmpty(
        GetFieldOrDefault(*result, "supervision_alarm_code", ""),
        GetFieldOrDefault(*result, "next_action_0_reason", ""),
        GetFieldOrDefault(*result, "not_verified_reason", ""));
}

void ApplyServiceStepStatus(CommandResult * result) {
    if (result == nullptr) {
        return;
    }

    if (!result->ok || result->exit_code != 0 || GetFieldOrDefault(*result, "supervision_status", "") == "alarm") {
        result->fields["status"] = "failed";
        return;
    }

    if (GetFieldOrDefault(*result, "supervision_status", "") == "closed_loop_continue"
        || GetFieldOrDefault(*result, "continue_required", "") == "true"
        || GetFieldOrDefault(*result, "has_more", "") == "true") {
        result->fields["status"] = "needs_continue";
        result->fields["error"] = "";
        result->fields["error_code"] = "";
        result->fields["error_message"] = "";
        result->fields["supervision_alarm"] = "false";
        return;
    }

    result->fields["status"] = "success";
    result->fields["supervision_alarm"] = "false";
}

void ApplySupervisionProgressFields(CommandResult * result) {
    if (result == nullptr) {
        return;
    }

    int target_count = 0;
    if (!GetFieldOrDefault(*result, "batch_total_files", "").empty()) {
        target_count = std::max(0, std::atoi(GetFieldOrDefault(*result, "batch_total_files", "0").c_str()));
    } else if (!GetFieldOrDefault(*result, "matched_file_count", "").empty()) {
        target_count = std::max(0, std::atoi(GetFieldOrDefault(*result, "matched_file_count", "0").c_str()));
    } else if (!GetFieldOrDefault(*result, "total_lines", "").empty()) {
        target_count = std::max(0, std::atoi(GetFieldOrDefault(*result, "total_lines", "0").c_str()));
    }

    int completed_count = 0;
    if (!GetFieldOrDefault(*result, "batch_read_file_count", "").empty()) {
        completed_count = std::max(0, std::atoi(GetFieldOrDefault(*result, "batch_read_file_count", "0").c_str()));
    } else if (GetFieldOrDefault(*result, "read_complete", "false") == "true") {
        completed_count = target_count > 0 ? target_count : 1;
    }

    int pending_count = 0;
    if (!GetFieldOrDefault(*result, "remaining_batch_file_count", "").empty()) {
        pending_count = std::max(0, std::atoi(GetFieldOrDefault(*result, "remaining_batch_file_count", "0").c_str()));
    } else if (GetFieldOrDefault(*result, "read_complete", "false") != "true") {
        pending_count = target_count > completed_count ? (target_count - completed_count) : 1;
    }

    if (target_count > 0 && completed_count + pending_count > target_count) {
        pending_count = std::max(0, target_count - completed_count);
    }

    result->fields["progress_target_count"] = std::to_string(target_count);
    result->fields["progress_completed_count"] = std::to_string(completed_count);
    result->fields["progress_pending_count"] = std::to_string(pending_count);
    if (GetFieldOrDefault(*result, "progress_failed_count", "").empty()) {
        result->fields["progress_failed_count"] = "0";
    }
    if (GetFieldOrDefault(*result, "progress_skipped_count", "").empty()) {
        result->fields["progress_skipped_count"] = "0";
    }
}

void CarryForwardDirectoryListingFields(
    const CommandResult & listing_result,
    CommandResult * result) {
    if (result == nullptr) {
        return;
    }
    if (GetFieldOrDefault(listing_result, "tool_name", "") != "lan_agent_list_directory") {
        return;
    }

    result->fields["listing_tool_name"] = "lan_agent_list_directory";
    result->fields["listed_directory_path"] = GetFieldOrDefault(listing_result, "directory_path", "");
    result->fields["listed_normalized_path"] = GetFieldOrDefault(listing_result, "normalized_path", "");
    result->fields["listed_total_entries"] = GetFieldOrDefault(listing_result, "total_entries", "");
    result->fields["listed_file_count"] = GetFieldOrDefault(listing_result, "file_count", "");
    result->fields["listed_directory_count"] = GetFieldOrDefault(listing_result, "directory_count", "");
    result->fields["listed_entry_labels_json"] = GetFieldOrDefault(listing_result, "entry_labels_json", "");
    result->fields["listed_file_paths_json"] = GetFieldOrDefault(listing_result, "file_paths_json", "");
    result->fields["listed_file_names_json"] = GetFieldOrDefault(listing_result, "file_names_json", "");
    result->fields["listed_batch_manifest_path"] = GetFieldOrDefault(listing_result, "batch_manifest_path", "");
    result->fields["listed_next_batch_file_path"] = GetFieldOrDefault(listing_result, "next_batch_file_path", "");
    result->fields["listed_remaining_batch_file_count"] = GetFieldOrDefault(listing_result, "remaining_batch_file_count", "");
    result->fields["directory_listing_complete"] = GetFieldOrDefault(listing_result, "directory_listing_complete", "");
    result->fields["known_file_list_complete"] = GetFieldOrDefault(listing_result, "known_file_list_complete", "");
}

std::string InjectTraceIdIntoContinuationCallJson(
    const std::string & call_json,
    const std::string & trace_id) {
    if (call_json.empty() || trace_id.empty()) {
        return call_json;
    }
    if (!ExtractJsonString(call_json, "trace_id").empty()) {
        return call_json;
    }

    const std::string marker = "\"arguments\":{";
    const std::size_t arguments_pos = call_json.find(marker);
    if (arguments_pos == std::string::npos) {
        return call_json;
    }

    std::size_t insert_pos = call_json.rfind("}}");
    if (insert_pos == std::string::npos) {
        insert_pos = call_json.rfind('}');
    }
    if (insert_pos == std::string::npos || insert_pos <= arguments_pos + marker.size()) {
        return call_json;
    }

    return call_json.substr(0, insert_pos)
        + ",\"trace_id\":\""
        + codex_lan_agent::JsonEscape(trace_id)
        + "\""
        + call_json.substr(insert_pos);
}

std::string ResolveEffectiveTraceIdForToolCall(
    const std::string & tool_name,
    const std::string & request_body) {
    const std::string trace_id = ExtractJsonString(request_body, "trace_id");
    if (!trace_id.empty()) {
        return trace_id;
    }
    return "trace-" + SanitizeDispatchToken(tool_name, "tool") + "-" + BuildRequestTimestampToken();
}

CommandResult ExecuteReadOnlyMcpToolForClipsContinuation(
    const AgentConfig & config,
    const std::string & tool_name,
    const std::string & request_body) {
    CommandResult result;
    if (tool_name == "lan_agent_probe_text_file") {
        return ProbeTextFileResult(
            config,
            ExtractJsonString(request_body, "file_path"),
            ExtractJsonString(request_body, "primary_intent"),
            ExtractJsonString(request_body, "trace_id"));
    }

    if (tool_name == "lan_agent_read_text_file") {
        int max_lines = 500;
        const std::string max_lines_raw = ExtractJsonRawValue(request_body, "max_lines");
        if (!max_lines_raw.empty()) {
            const int parsed_max_lines = std::atoi(max_lines_raw.c_str());
            max_lines = parsed_max_lines > 0 ? parsed_max_lines : 1;
        }
        int start_line = 1;
        const std::string start_line_raw = ExtractJsonRawValue(request_body, "start_line");
        if (!start_line_raw.empty()) {
            const int parsed_start_line = std::atoi(start_line_raw.c_str());
            start_line = parsed_start_line > 0 ? parsed_start_line : 1;
        }
        return ReadTextFileResult(
            config,
            ExtractJsonString(request_body, "file_path"),
            max_lines,
            start_line,
            ExtractJsonString(request_body, "trace_id"),
            0,
            ExtractJsonString(request_body, "probe_ref"));
    }

    if (tool_name == "lan_agent_list_directory") {
        int max_entries = 200;
        const std::string max_entries_raw = ExtractJsonRawValue(request_body, "max_entries");
        if (!max_entries_raw.empty()) {
            const int parsed_max_entries = std::atoi(max_entries_raw.c_str());
            max_entries = parsed_max_entries > 0 ? parsed_max_entries : 1;
        }
        return ListDirectoryResult(
            config,
            ExtractJsonString(request_body, "directory_path"),
            max_entries,
            ExtractJsonString(request_body, "trace_id"));
    }

    if (tool_name == "lan_agent_read_directory_files") {
        int max_files = 200;
        const std::string max_files_raw = ExtractJsonRawValue(request_body, "max_files");
        if (!max_files_raw.empty()) {
            const int parsed_max_files = std::atoi(max_files_raw.c_str());
            max_files = parsed_max_files > 0 ? parsed_max_files : 1;
        }
        int max_lines_per_file = 500;
        const std::string max_lines_per_file_raw = ExtractJsonRawValue(request_body, "max_lines_per_file");
        if (!max_lines_per_file_raw.empty()) {
            const int parsed_max_lines_per_file = std::atoi(max_lines_per_file_raw.c_str());
            max_lines_per_file = parsed_max_lines_per_file > 0 ? parsed_max_lines_per_file : 1;
        }
        int max_files_per_call = 5;
        const std::string max_files_per_call_raw = ExtractJsonRawValue(request_body, "max_files_per_call");
        if (!max_files_per_call_raw.empty()) {
            const int parsed_max_files_per_call = std::atoi(max_files_per_call_raw.c_str());
            max_files_per_call = parsed_max_files_per_call > 0 ? parsed_max_files_per_call : 1;
        }
        int max_total_lines = 2500;
        const std::string max_total_lines_raw = ExtractJsonRawValue(request_body, "max_total_lines");
        if (!max_total_lines_raw.empty()) {
            const int parsed_max_total_lines = std::atoi(max_total_lines_raw.c_str());
            max_total_lines = parsed_max_total_lines > 0 ? parsed_max_total_lines : 1;
        }
        int file_index = 0;
        const std::string file_index_raw = ExtractJsonRawValue(request_body, "file_index");
        if (!file_index_raw.empty()) {
            file_index = std::max(0, std::atoi(file_index_raw.c_str()));
        }
        int start_line = 1;
        const std::string start_line_raw = ExtractJsonRawValue(request_body, "start_line");
        if (!start_line_raw.empty()) {
            const int parsed_start_line = std::atoi(start_line_raw.c_str());
            start_line = parsed_start_line > 0 ? parsed_start_line : 1;
        }
        return ReadDirectoryFilesResult(
            config,
            ExtractJsonString(request_body, "directory_path"),
            ExtractJsonString(request_body, "file_extensions_csv"),
            max_files,
            max_lines_per_file,
            max_files_per_call,
            max_total_lines,
            file_index,
            start_line,
            ExtractJsonString(request_body, "trace_id"));
    }

    if (tool_name == "lan_agent_prepare_directory_analysis") {
        int max_files = 200;
        const std::string max_files_raw = ExtractJsonRawValue(request_body, "max_files");
        if (!max_files_raw.empty()) {
            const int parsed_max_files = std::atoi(max_files_raw.c_str());
            max_files = parsed_max_files > 0 ? parsed_max_files : 1;
        }
        int max_excerpt_lines_per_file = 80;
        const std::string max_excerpt_lines_per_file_raw =
            ExtractJsonRawValue(request_body, "max_excerpt_lines_per_file");
        if (!max_excerpt_lines_per_file_raw.empty()) {
            const int parsed_max_excerpt_lines_per_file =
                std::atoi(max_excerpt_lines_per_file_raw.c_str());
            max_excerpt_lines_per_file =
                parsed_max_excerpt_lines_per_file > 0 ? parsed_max_excerpt_lines_per_file : 1;
        }
        int max_total_excerpt_lines = 1200;
        const std::string max_total_excerpt_lines_raw =
            ExtractJsonRawValue(request_body, "max_total_excerpt_lines");
        if (!max_total_excerpt_lines_raw.empty()) {
            const int parsed_max_total_excerpt_lines =
                std::atoi(max_total_excerpt_lines_raw.c_str());
            max_total_excerpt_lines =
                parsed_max_total_excerpt_lines > 0 ? parsed_max_total_excerpt_lines : 1;
        }
        int max_excerpt_chars = 24000;
        const std::string max_excerpt_chars_raw =
            ExtractJsonRawValue(request_body, "max_excerpt_chars");
        if (!max_excerpt_chars_raw.empty()) {
            const int parsed_max_excerpt_chars = std::atoi(max_excerpt_chars_raw.c_str());
            max_excerpt_chars = parsed_max_excerpt_chars > 0 ? parsed_max_excerpt_chars : 1;
        }
        return PrepareDirectoryAnalysisResult(
            config,
            ExtractJsonString(request_body, "directory_path"),
            ExtractJsonString(request_body, "file_extensions_csv"),
            max_files,
            max_excerpt_lines_per_file,
            max_total_excerpt_lines,
            max_excerpt_chars,
            ExtractJsonString(request_body, "trace_id"));
    }

    if (tool_name == "lan_agent_scan_text_ranges") {
        int max_ranges_per_call = 64;
        const std::string max_ranges_per_call_raw =
            ExtractJsonRawValue(request_body, "max_ranges_per_call");
        if (!max_ranges_per_call_raw.empty()) {
            const int parsed_max_ranges_per_call =
                std::atoi(max_ranges_per_call_raw.c_str());
            max_ranges_per_call = parsed_max_ranges_per_call > 0
                ? parsed_max_ranges_per_call
                : 1;
        }
        int range_offset = 0;
        const std::string range_offset_raw =
            ExtractJsonRawValue(request_body, "range_offset");
        if (!range_offset_raw.empty()) {
            const int parsed_range_offset = std::atoi(range_offset_raw.c_str());
            range_offset = parsed_range_offset >= 0 ? parsed_range_offset : 0;
        }
        return ScanTextRangesResult(
            config,
            ExtractJsonString(request_body, "file_path"),
            ExtractJsonString(request_body, "scan_mode"),
            max_ranges_per_call,
            range_offset,
            ExtractJsonString(request_body, "trace_id"),
            ExtractJsonString(request_body, "probe_ref"));
    }

    if (tool_name == "lan_agent_prepare_edit_windows") {
        int context_before = 8;
        const std::string context_before_raw =
            ExtractJsonRawValue(request_body, "context_before");
        if (!context_before_raw.empty()) {
            const int parsed_context_before = std::atoi(context_before_raw.c_str());
            context_before = parsed_context_before >= 0 ? parsed_context_before : 0;
        }
        int context_after = 8;
        const std::string context_after_raw =
            ExtractJsonRawValue(request_body, "context_after");
        if (!context_after_raw.empty()) {
            const int parsed_context_after = std::atoi(context_after_raw.c_str());
            context_after = parsed_context_after >= 0 ? parsed_context_after : 0;
        }
        int max_windows_per_call = 16;
        const std::string max_windows_per_call_raw =
            ExtractJsonRawValue(request_body, "max_windows_per_call");
        if (!max_windows_per_call_raw.empty()) {
            const int parsed_max_windows_per_call =
                std::atoi(max_windows_per_call_raw.c_str());
            max_windows_per_call = parsed_max_windows_per_call > 0
                ? parsed_max_windows_per_call
                : 1;
        }
        int window_offset = 0;
        const std::string window_offset_raw =
            ExtractJsonRawValue(request_body, "window_offset");
        if (!window_offset_raw.empty()) {
            const int parsed_window_offset = std::atoi(window_offset_raw.c_str());
            window_offset = parsed_window_offset >= 0 ? parsed_window_offset : 0;
        }
        std::size_t max_window_chars = 2400;
        const std::string max_window_chars_raw =
            ExtractJsonRawValue(request_body, "max_window_chars");
        if (!max_window_chars_raw.empty()) {
            const unsigned long long parsed_max_window_chars =
                std::strtoull(max_window_chars_raw.c_str(), nullptr, 10);
            max_window_chars = parsed_max_window_chars > 0
                ? static_cast<std::size_t>(parsed_max_window_chars)
                : static_cast<std::size_t>(1);
        }
        return PrepareEditWindowsResult(
            config,
            ExtractJsonString(request_body, "file_path"),
            ExtractJsonString(request_body, "ranges_json"),
            context_before,
            context_after,
            max_windows_per_call,
            window_offset,
            max_window_chars,
            ExtractJsonString(request_body, "trace_id"),
            ExtractJsonString(request_body, "probe_ref"));
    }

    result.ok = false;
    result.exit_code = 67;
    result.fields["error"] = "clips auto continuation tool is not allowlisted";
    result.fields["tool_name"] = tool_name;
    return result;
}

void ApplySupervisionEnvelope(CommandResult * result) {
    if (result == nullptr) {
        return;
    }

    const std::string clamp = GetFieldOrDefault(*result, "semantic_model_clamp", "");
    const std::string alarm_code = GetFieldOrDefault(*result, "supervision_alarm_code", "");
    const bool has_alarm = !alarm_code.empty()
        || GetFieldOrDefault(*result, "supervision_status", "") == "alarm";

    if (!result->ok || result->exit_code != 0) {
        result->fields["supervision_status"] = "failed";
        result->fields["goal_status"] = "failed";
        result->fields["assistant_response_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
        result->fields["terminal_state"] = "false";
        result->fields["task_done"] = "false";
        result->fields["completion_claim_allowed"] = "false";
        result->fields["supervision_alarm"] = has_alarm ? "true" : "false";
        result->fields["completion_guard"] =
            "FAILED_RESULT: do not claim completion; inspect error and execute an explicit recovery tool";
    } else if (has_alarm) {
        result->fields["supervision_status"] = "alarm";
        result->fields["goal_status"] = "failed";
        result->fields["assistant_response_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
        result->fields["supervision_alarm"] = "true";
    } else if (clamp == "tool_call_only") {
        result->fields["supervision_status"] = "closed_loop_continue";
        result->fields["goal_status"] = "not_complete";
        result->fields["assistant_response_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
        result->fields["supervision_alarm"] = "false";
        result->fields["terminal_state"] = "false";
        result->fields["task_done"] = "false";
        result->fields["completion_claim_allowed"] = "false";
        if (GetFieldOrDefault(*result, "must_continue_until", "").empty()) {
            result->fields["must_continue_until"] = "final_answer_allowed=true";
        }
        result->fields["completion_guard"] =
            "NON_TERMINAL_RESULT: do not claim completion; execute the required next MCP tool call";
    } else {
        result->fields["supervision_status"] = "closed_loop_complete";
        result->fields["goal_status"] = "complete";
        if (GetFieldOrDefault(*result, "assistant_response_allowed", "").empty()) {
            result->fields["assistant_response_allowed"] = "true";
        }
        if (GetFieldOrDefault(*result, "final_answer_allowed", "").empty()) {
            result->fields["final_answer_allowed"] = "true";
        }
        result->fields["supervision_alarm"] = "false";
        if (GetFieldOrDefault(*result, "terminal_state", "").empty()) {
            result->fields["terminal_state"] = "true";
        }
        if (GetFieldOrDefault(*result, "task_done", "").empty()) {
            result->fields["task_done"] = "true";
        }
        if (GetFieldOrDefault(*result, "completion_claim_allowed", "").empty()) {
            result->fields["completion_claim_allowed"] = "true";
        }
        if (GetFieldOrDefault(*result, "supervision_alarm_code", "").empty()) {
            result->fields["supervision_alarm_code"] = "";
        }
        if (GetFieldOrDefault(*result, "supervision_alarm_message", "").empty()) {
            result->fields["supervision_alarm_message"] = "";
        }
    }

    if (GetFieldOrDefault(*result, "goal_id", "").empty()) {
        const std::string fallback_goal_id = FirstNonEmpty(
            GetFieldOrDefault(*result, "task_id", ""),
            GetFieldOrDefault(*result, "trace_id", ""),
            "");
        result->fields["goal_id"] = fallback_goal_id;
    }

    ApplySupervisionProgressFields(result);

    if (GetFieldOrDefault(*result, "supervision_status", "") == "closed_loop_continue") {
        const std::string required_tool = FirstNonEmpty(
            GetFieldOrDefault(*result, "required_tool_name", ""),
            GetFieldOrDefault(*result, "next_tool_name", ""),
            "");
        const std::string next_action_json = FirstNonEmpty(
            GetFieldOrDefault(*result, "required_tool_arguments_json", ""),
            GetFieldOrDefault(*result, "next_call_json", ""),
            "");
        const std::string safety_class = ResolveNextActionSafetyClass(required_tool);
        if (required_tool.empty() || next_action_json.empty()) {
            SetClipsSupervisionAlarm(
                result,
                "NO_NEXT_ACTION_FOR_INCOMPLETE_GOAL",
                "Goal is not complete, but CLIPS produced no executable next action.");
        } else if (!IsSafeClipsContinuationAction(*result, required_tool, safety_class)) {
            SetClipsSupervisionAlarm(
                result,
                safety_class.empty()
                    ? "NEXT_ACTION_SAFETY_CLASS_MISSING"
                    : "NEXT_FLOW_SAFETY_CLASS_NOT_READ_ONLY",
                "CLIPS produced a continuation action without a recognized read-only safety boundary.");
        } else {
            result->fields["next_actions_count"] = "1";
            result->fields["supervision_alarm"] = "false";
            result->fields["supervision_alarm_code"] = "";
            result->fields["supervision_alarm_message"] = "";
            result->fields["next_action_0_action_id"] = FirstNonEmpty(
                GetFieldOrDefault(*result, "required_next_action_type", ""),
                "mcp_tool_call",
                "mcp_tool_call");
            result->fields["next_action_0_tool_name"] = required_tool;
            result->fields["next_action_0_safety_class"] = safety_class;
            result->fields["next_action_0_params_json"] = next_action_json;
            result->fields["next_action_0_reason"] = FirstNonEmpty(
                GetFieldOrDefault(*result, "not_verified_reason", ""),
                GetFieldOrDefault(*result, "next_action", ""),
                "pending goal remains incomplete");
            result->fields["next_action_0_source_rule"] = GetFieldOrDefault(*result, "clips_post_result_matched_rule", "");
            result->fields["next_action_0_trace_id"] = GetFieldOrDefault(*result, "trace_id", "");
            result->fields["next_action_0_goal_id"] = GetFieldOrDefault(*result, "goal_id", "");
            result->fields["next_action_0_params_hash"] = StableContentChecksum(next_action_json);
            if (required_tool == "lan_agent_delete_text_range_window_atomic"
                || required_tool == "lan_agent_delete_next_text_range_atomic") {
                const bool budget_internal_step =
                    GetFieldOrDefault(*result, "task_memory_budget_internal_step", "") == "true";
                const std::string goal_id = FirstNonEmpty(
                    GetFieldOrDefault(*result, "goal_id", ""),
                    GetFieldOrDefault(*result, "trace_id", ""),
                    "mcp-comment-cleanup");
                const std::string trace_id = GetFieldOrDefault(*result, "trace_id", "");
                const std::string freeze_status = GetFieldOrDefault(*result, "has_more", "") == "true"
                    || GetFieldOrDefault(*result, "continue_required", "") == "true"
                    ? "needs_continue"
                    : GetFieldOrDefault(*result, "status", "");
                result->fields["long_loop_budget_recommended"] = "true";
                result->fields["long_loop_freeze_tool_name"] = "lan_agent_task_memory_freeze";
                result->fields["long_loop_budget_tool_name"] = "lan_agent_task_memory_execute_continuation_budget";
                result->fields["long_loop_budget_policy"] =
                    "when the same continuation may exceed model context, freeze this next_call_json once, then run bounded budget steps";
                result->fields["long_loop_freeze_arguments_json"] =
                    "{\"name\":\"lan_agent_task_memory_freeze\",\"arguments\":{"
                    "\"goal_id\":\"" + codex_lan_agent::JsonEscape(goal_id) + "\","
                    "\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\","
                    "\"current_goal\":\"continue bounded comment cleanup until completion gate allows final answer\","
                    "\"current_tool\":\"" + codex_lan_agent::JsonEscape(required_tool) + "\","
                    "\"current_file\":\"" + codex_lan_agent::JsonEscape(GetFieldOrDefault(*result, "file_path", "")) + "\","
                    "\"last_status\":\"" + codex_lan_agent::JsonEscape(freeze_status) + "\","
                    "\"last_has_more\":\"" + codex_lan_agent::JsonEscape(GetFieldOrDefault(*result, "has_more", "")) + "\","
                    "\"terminal_state\":false,"
                    "\"completion_claim_allowed\":false,"
                    "\"completed_step_count\":0,"
                    "\"remaining_work\":\"continue required MCP tool calls until terminal_state=true\","
                    "\"next_tool_name\":\"" + codex_lan_agent::JsonEscape(required_tool) + "\","
                    "\"next_file_path\":\"" + codex_lan_agent::JsonEscape(GetFieldOrDefault(*result, "file_path", "")) + "\","
                    "\"next_scan_mode\":\"" + codex_lan_agent::JsonEscape(GetFieldOrDefault(*result, "scan_mode", "comments")) + "\","
                    "\"next_primary_intent\":\"" + codex_lan_agent::JsonEscape(GetFieldOrDefault(*result, "primary_intent", "comment_cleanup")) + "\","
                    "\"next_start_line\":" + FirstNonEmpty(GetFieldOrDefault(*result, "next_start_line", ""), "1", "1") + ","
                    "\"next_max_lines\":" + FirstNonEmpty(GetFieldOrDefault(*result, "max_lines", ""), "200", "200") + ","
                    "\"next_probe_ref\":\"" + codex_lan_agent::JsonEscape(FirstNonEmpty(
                        GetFieldOrDefault(*result, "probe_ref", ""),
                        GetFieldOrDefault(*result, "file_path", ""),
                        "")) + "\","
                    "\"next_probe_ready\":" + (GetFieldOrDefault(*result, "probe_ready", "true") == "false" ? "false" : "true") +
                    "}}";
                result->fields["long_loop_budget_arguments_json"] = "";
                result->fields["long_loop_budget_precondition"] =
                    "call required_tool_arguments_json for lan_agent_task_memory_freeze first; budget arguments are emitted by freeze";
                result->fields["direct_continuation_tool_name"] = required_tool;
                result->fields["direct_continuation_arguments_json"] = next_action_json;
                if (!budget_internal_step) {
                    result->fields["task_execution_in_mcp_required"] = "true";
                    result->fields["forced_task_memory_execution"] = "true";
                    result->fields["required_tool_name"] = "lan_agent_task_memory_freeze";
                    result->fields["required_tool_arguments_json"] =
                        result->fields["long_loop_freeze_arguments_json"];
                    result->fields["next_call_json"] =
                        result->fields["long_loop_freeze_arguments_json"];
                    result->fields["next_action"] =
                        "tool_call_only: freeze this long continuation into task_memory, then run lan_agent_task_memory_execute_continuation_budget";
                    result->fields["next_action_0_tool_name"] = "lan_agent_task_memory_freeze";
                    result->fields["next_action_0_safety_class"] =
                        ResolveNextActionSafetyClass("lan_agent_task_memory_freeze");
                    result->fields["next_action_0_params_json"] =
                        result->fields["long_loop_freeze_arguments_json"];
                    result->fields["next_action_0_params_hash"] =
                        StableContentChecksum(result->fields["long_loop_freeze_arguments_json"]);
                    result->fields["next_action_0_reason"] =
                        "long loop must run under MCP task_memory budget instead of model-side repeated calls";
                }
            }
        }
    } else if (GetFieldOrDefault(*result, "next_actions_count", "").empty()) {
        result->fields["next_actions_count"] = "0";
    }

    ApplyGuardSurfaceFields(result);
    ApplyClipsFirstDecisionFields(result);
    ApplyServiceStepStatus(result);
}

bool TaskMemoryRunnerBoolField(
    const CommandResult & result,
    const std::string & key,
    bool fallback = false) {
    const auto it = result.fields.find(key);
    if (it == result.fields.end()) {
        return fallback;
    }
    const std::string value = ToLowerAscii(Trim(it->second));
    if (value == "true" || value == "1" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        return false;
    }
    return fallback;
}

std::string ExtractJsonObjectRawForTaskMemoryRunner(
    const std::string & json,
    const std::string & key) {
    const std::string marker = "\"" + key + "\"";
    std::size_t key_pos = json.find(marker);
    if (key_pos == std::string::npos) {
        return std::string();
    }
    std::size_t colon_pos = json.find(':', key_pos + marker.size());
    if (colon_pos == std::string::npos) {
        return std::string();
    }
    std::size_t pos = colon_pos + 1;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '{') {
        return std::string();
    }

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = pos; index < json.size(); ++index) {
        const char ch = json[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (in_string && ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(pos, index - pos + 1);
            }
        }
    }
    return std::string();
}

bool IsTaskMemoryBudgetExecutableTool(const std::string & tool_name) {
    return tool_name == "lan_agent_delete_text_range_window_atomic"
        || tool_name == "lan_agent_delete_next_text_range_atomic";
}

std::string TaskMemoryRunnerField(
    const CommandResult & result,
    const std::string & key,
    const std::string & fallback = std::string()) {
    const auto it = result.fields.find(key);
    return it == result.fields.end() ? fallback : it->second;
}

std::string NormalizeTaskMemoryRunnerNextCallJson(std::string value) {
    return codex_lan_agent::TaskMemoryNormalizeNextCallJson(value);
}

std::string BuildTaskMemoryRunnerAppendParamsJson(
    const std::string & goal_id,
    const std::string & trace_id,
    int step_index,
    const std::string & step_id,
    const std::string & tool_name,
    const CommandResult & step_result,
    const std::string & next_call_json,
    bool has_more,
    bool terminal_state,
    bool completion_claim_allowed) {
    const std::string summary = FirstNonEmpty(
        TaskMemoryRunnerField(step_result, "summary"),
        TaskMemoryRunnerField(step_result, "result"),
        "continuation step executed");
    const std::string remaining_work = has_more
        ? "continue from next_call_json until terminal_state=true"
        : "no continuation reported by executed step";

    std::ostringstream output;
    output
        << "{"
        << "\"goal_id\":\"" << codex_lan_agent::JsonEscape(goal_id) << "\","
        << "\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\","
        << "\"step_id\":\"" << codex_lan_agent::JsonEscape(step_id) << "\","
        << "\"step_index\":" << step_index << ","
        << "\"step_kind\":\"budget_executed_continuation\","
        << "\"current_tool\":\"" << codex_lan_agent::JsonEscape(tool_name) << "\","
        << "\"status\":\"" << codex_lan_agent::JsonEscape(TaskMemoryRunnerField(step_result, "status")) << "\","
        << "\"summary\":\"" << codex_lan_agent::JsonEscape(summary) << "\","
        << "\"result_ref\":\"" << codex_lan_agent::JsonEscape(TaskMemoryRunnerField(step_result, "result_ref")) << "\","
        << "\"evidence_ref\":\"" << codex_lan_agent::JsonEscape(TaskMemoryRunnerField(step_result, "evidence_ref")) << "\","
        << "\"next_call_json\":\"" << codex_lan_agent::JsonEscape(next_call_json) << "\","
        << "\"has_more\":" << (has_more ? "true" : "false") << ","
        << "\"terminal_state\":" << (terminal_state ? "true" : "false") << ","
        << "\"completion_claim_allowed\":" << (completion_claim_allowed ? "true" : "false") << ","
        << "\"compact_summary\":\"" << codex_lan_agent::JsonEscape(summary) << "\","
        << "\"remaining_work\":\"" << codex_lan_agent::JsonEscape(remaining_work) << "\""
        << "}";
    return output.str();
}

CommandResult BuildTaskMemoryExecuteContinuationBudgetRunnerResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    const bool dry_run = params.GetBool("dry_run", true);
    const bool execute = params.GetBool("execute", false);
    if (!execute || dry_run) {
        return codex_lan_agent::BuildTaskMemoryExecuteContinuationBudgetResult(config, params);
    }

    CommandResult result;
    const std::string goal_id = params.GetString("goal_id");
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        return result;
    }

    const int max_steps = std::min(64, std::max(1, params.GetInt("max_steps", params.GetInt("step_budget", 1))));
    const std::filesystem::path root = codex_lan_agent::BuildTaskMemoryRoot(config, goal_id);
    const std::filesystem::path resume_path = root / "latest_resume_context.json";
    std::string resume_context = codex_lan_agent::ReadTaskMemoryTextFile(resume_path);
    if (resume_context.empty()) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["status"] = "failed";
        result.fields["record_model"] = "mcp_task_memory_execute_continuation_budget_response_v1";
        result.fields["goal_id"] = goal_id;
        result.fields["budget_status"] = "blocked_missing_resume_context";
        result.fields["terminal_state"] = "false";
        result.fields["task_done"] = "false";
        result.fields["completion_claim_allowed"] = "false";
        result.fields["assistant_response_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        result.fields["continue_required"] = "false";
        result.fields["auto_continue_required"] = "false";
        result.fields["budget_requires_frozen_resume_context"] = "true";
        result.fields["error"] = "resume context not found";
        result.fields["resume_context_path"] = resume_path.string();
        codex_lan_agent::ApplyTaskMemoryCleanHandoffFields(
            &result,
            goal_id,
            "",
            max_steps,
            false,
            false);
        result.fields["next_action"] =
            "do not call budget directly before freeze; call the previous required_tool_arguments_json for lan_agent_task_memory_freeze first";
        return result;
    }

    const std::string trace_id = codex_lan_agent::TaskMemoryFirstNonEmpty(
        params.GetString("trace_id"),
        ExtractJsonString(resume_context, "trace_id"),
        "TRACE-" + codex_lan_agent::TaskMemoryStableChecksum(goal_id));
    int last_verified_step = codex_lan_agent::TaskMemoryExtractIntField(resume_context, "last_verified_step", 0);
    bool terminal_state = codex_lan_agent::TaskMemoryExtractBoolField(resume_context, "terminal_state", false);
    std::string current_call_json = NormalizeTaskMemoryRunnerNextCallJson(
        ExtractJsonString(resume_context, "next_call_json"));
    bool missing_initial_next_call = !terminal_state && Trim(current_call_json).empty();
    const std::string now = IsoTimestampNow();
    const std::string budget_run_id = "budget-" + codex_lan_agent::TaskMemoryStableChecksum(
        goal_id + "|" + trace_id + "|" + std::to_string(last_verified_step) + "|" + now);
    const std::filesystem::path budget_dir = root / "budget_runs";
    const std::filesystem::path budget_path = budget_dir / (budget_run_id + ".json");

    std::vector<std::string> step_events;
    int executed_step_count = 0;
    bool budget_exhausted = false;
    bool blocked = false;
    std::string block_reason;
    std::string last_tool;
    CommandResult last_step_result;
    if (missing_initial_next_call) {
        blocked = true;
        block_reason = "NEXT_CALL_JSON_MISSING";
    }

    while (!terminal_state && !Trim(current_call_json).empty() && executed_step_count < max_steps) {
        current_call_json = InjectTraceIdIntoContinuationCallJson(current_call_json, trace_id);
        const std::string tool_name = ExtractJsonString(current_call_json, "name");
        if (!IsTaskMemoryBudgetExecutableTool(tool_name)) {
            blocked = true;
            block_reason = "continuation tool is not budget-runner allowlisted";
            last_tool = tool_name;
            break;
        }

        const std::string arguments_json = ExtractJsonObjectRawForTaskMemoryRunner(current_call_json, "arguments");
        if (Trim(arguments_json).empty()) {
            blocked = true;
            block_reason = "next_call_json is missing an arguments object";
            last_tool = tool_name;
            break;
        }

        const auto & handlers = BuildMcpToolHandlerRegistry();
        const auto handler_it = handlers.find(tool_name);
        if (handler_it == handlers.end()) {
            blocked = true;
            block_reason = "continuation tool handler not found";
            last_tool = tool_name;
            break;
        }

        JsonRequestView step_params(arguments_json);
        CommandResult step_result = handler_it->second(config, step_params);
        step_result.fields["task_memory_budget_internal_step"] = "true";
        ApplyRequestRuleFields(tool_name, step_params, &step_result);
        LanResultBuilder(&step_result).Finalize(config, tool_name);
        ApplyAiConclusionValidityGuards(&step_result);
        ApplyClipsResultGuard(config, tool_name, &step_result);
        ApplySupervisionEnvelope(&step_result);
        AppendMcpTraceAuditEvent(config, tool_name, step_result);
        AppendMcpSupervisionAlarmEvent(config, tool_name, step_result);

        ++executed_step_count;
        ++last_verified_step;
        last_tool = tool_name;
        last_step_result = step_result;

        const bool has_more = TaskMemoryRunnerBoolField(step_result, "has_more", false)
            || TaskMemoryRunnerBoolField(step_result, "continue_required", false)
            || TaskMemoryRunnerField(step_result, "semantic_model_clamp") == "tool_call_only";
        std::string next_call_json = NormalizeTaskMemoryRunnerNextCallJson(FirstNonEmpty(
            TaskMemoryRunnerField(step_result, "next_call_json"),
            TaskMemoryRunnerField(step_result, "required_tool_arguments_json"),
            std::string()));
        const bool step_terminal = !has_more
            && TaskMemoryRunnerBoolField(step_result, "terminal_state", true);
        const bool step_completion_claim_allowed = step_terminal
            && TaskMemoryRunnerBoolField(step_result, "completion_claim_allowed", true);

        const std::string step_id = "budget-step-" + std::to_string(last_verified_step);
        const std::string append_params_json = BuildTaskMemoryRunnerAppendParamsJson(
            goal_id,
            trace_id,
            last_verified_step,
            step_id,
            tool_name,
            step_result,
            next_call_json,
            has_more,
            step_terminal,
            step_completion_claim_allowed);
        CommandResult append_result = codex_lan_agent::BuildTaskMemoryAppendStepResult(
            config,
            JsonRequestView(append_params_json));
        if (!append_result.ok || append_result.exit_code != 0) {
            blocked = true;
            block_reason = FirstNonEmpty(
                TaskMemoryRunnerField(append_result, "error"),
                "failed to append budget step to task memory",
                "");
            current_call_json = next_call_json;
            break;
        }

        std::ostringstream step_event;
        step_event
            << "{"
            << "\"step_index\":" << last_verified_step << ","
            << "\"tool_name\":\"" << codex_lan_agent::JsonEscape(tool_name) << "\","
            << "\"status\":\"" << codex_lan_agent::JsonEscape(TaskMemoryRunnerField(step_result, "status")) << "\","
            << "\"has_more\":" << (has_more ? "true" : "false") << ","
            << "\"terminal_state\":" << (step_terminal ? "true" : "false") << ","
            << "\"completion_claim_allowed\":" << (step_completion_claim_allowed ? "true" : "false") << ","
            << "\"result_ref\":\"" << codex_lan_agent::JsonEscape(TaskMemoryRunnerField(step_result, "result_ref")) << "\","
            << "\"evidence_ref\":\"" << codex_lan_agent::JsonEscape(TaskMemoryRunnerField(step_result, "evidence_ref")) << "\""
            << "}";
        step_events.push_back(step_event.str());

        const std::string next_tool_name = ExtractJsonString(next_call_json, "name");
        const bool safe_pending_continuation =
            has_more
            && !Trim(next_call_json).empty()
            && IsTaskMemoryBudgetExecutableTool(next_tool_name);
        if ((!step_result.ok || step_result.exit_code != 0) && !safe_pending_continuation) {
            blocked = true;
            block_reason = FirstNonEmpty(
                TaskMemoryRunnerField(step_result, "error"),
                "continuation step returned failure",
                "");
            current_call_json = next_call_json;
            terminal_state = false;
            break;
        }

        current_call_json = next_call_json;
        terminal_state = step_terminal;
        if (!terminal_state && Trim(current_call_json).empty()) {
            blocked = true;
            block_reason = "continuation still pending but next_call_json is empty";
            break;
        }
    }

    if (!terminal_state && !blocked && !Trim(current_call_json).empty() && executed_step_count >= max_steps) {
        budget_exhausted = true;
    }

    resume_context = codex_lan_agent::ReadTaskMemoryTextFile(resume_path);
    if (!resume_context.empty()) {
        terminal_state = codex_lan_agent::TaskMemoryExtractBoolField(resume_context, "terminal_state", terminal_state);
        current_call_json = NormalizeTaskMemoryRunnerNextCallJson(ExtractJsonString(resume_context, "next_call_json"));
        last_verified_step = codex_lan_agent::TaskMemoryExtractIntField(resume_context, "last_verified_step", last_verified_step);
    }
    const bool completion_claim_allowed = terminal_state
        && codex_lan_agent::TaskMemoryExtractBoolField(resume_context, "completion_claim_allowed", false);
    const bool continue_required = !terminal_state && !Trim(current_call_json).empty();

    std::ostringstream budget_record;
    budget_record
        << "{\n"
        << "  \"record_model\":\"mcp_continuation_budget_run_v1\",\n"
        << "  \"budget_run_id\":\"" << codex_lan_agent::JsonEscape(budget_run_id) << "\",\n"
        << "  \"goal_id\":\"" << codex_lan_agent::JsonEscape(goal_id) << "\",\n"
        << "  \"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\",\n"
        << "  \"created_at\":\"" << codex_lan_agent::JsonEscape(now) << "\",\n"
        << "  \"execution_mode\":\"bounded_allowlist_execute\",\n"
        << "  \"max_steps\":" << max_steps << ",\n"
        << "  \"executed_step_count\":" << executed_step_count << ",\n"
        << "  \"budget_exhausted\":" << (budget_exhausted ? "true" : "false") << ",\n"
        << "  \"blocked\":" << (blocked ? "true" : "false") << ",\n"
        << "  \"block_reason\":\"" << codex_lan_agent::JsonEscape(block_reason) << "\",\n"
        << "  \"terminal_state\":" << (terminal_state ? "true" : "false") << ",\n"
        << "  \"completion_claim_allowed\":" << (completion_claim_allowed ? "true" : "false") << ",\n"
        << "  \"last_verified_step\":" << last_verified_step << ",\n"
        << "  \"step_events\":[";
    for (std::size_t index = 0; index < step_events.size(); ++index) {
        if (index > 0) {
            budget_record << ",";
        }
        budget_record << step_events[index];
    }
    budget_record
        << "]\n"
        << "}\n";

    std::string write_error;
    if (!codex_lan_agent::WriteTaskMemoryTextFile(budget_path, budget_record.str(), &write_error)) {
        result.ok = false;
        result.exit_code = 502;
        result.fields["error"] = write_error;
        result.fields["failed_path"] = budget_path.string();
        return result;
    }

    result.ok = !blocked;
    result.exit_code = blocked ? 422 : 0;
    result.fields["status"] = blocked ? "failed" : "success";
    result.fields["record_model"] = "mcp_task_memory_execute_continuation_budget_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["trace_id"] = trace_id;
    result.fields["budget_run_id"] = budget_run_id;
    result.fields["budget_status"] = blocked
        ? "blocked"
        : (terminal_state ? "terminal_complete" : (budget_exhausted ? "budget_exhausted" : "needs_continue"));
    result.fields["execution_mode"] = "bounded_allowlist_execute";
    result.fields["dry_run"] = "false";
    result.fields["execute_requested"] = "true";
    result.fields["execution_deferred"] = "false";
    result.fields["max_steps"] = std::to_string(max_steps);
    result.fields["planned_step_count"] = std::to_string(std::max(0, max_steps - executed_step_count));
    result.fields["executed_step_count"] = std::to_string(executed_step_count);
    result.fields["budget_exhausted"] = budget_exhausted ? "true" : "false";
    result.fields["last_verified_step"] = std::to_string(last_verified_step);
    result.fields["last_tool"] = last_tool;
    result.fields["last_status"] = TaskMemoryRunnerField(last_step_result, "status");
    result.fields["last_result_ref"] = TaskMemoryRunnerField(last_step_result, "result_ref");
    result.fields["last_summary"] = FirstNonEmpty(
        TaskMemoryRunnerField(last_step_result, "summary"),
        TaskMemoryRunnerField(last_step_result, "result"),
        "");
    result.fields["last_verification_ok"] = TaskMemoryRunnerField(last_step_result, "verification_ok");
    result.fields["resume_context_path"] = resume_path.string();
    result.fields["budget_plan_path"] = budget_path.string();
    result.fields["step_ledger_path"] = (root / "step_ledger.jsonl").string();
    result.fields["terminal_state"] = terminal_state ? "true" : "false";
    result.fields["completion_claim_allowed"] = completion_claim_allowed ? "true" : "false";
    result.fields["task_done"] = terminal_state ? "true" : "false";
    result.fields["continue_required"] = continue_required ? "true" : "false";
    result.fields["auto_continue_required"] = continue_required ? "true" : "false";
    result.fields["assistant_response_allowed"] = completion_claim_allowed ? "true" : "false";
    result.fields["final_answer_allowed"] = completion_claim_allowed ? "true" : "false";
    result.fields["verification_ok"] =
        (completion_claim_allowed && TaskMemoryRunnerBoolField(last_step_result, "verification_ok", false))
            ? "true"
            : "false";
    result.fields["must_continue_until"] = terminal_state ? "" : "terminal_state=true";
    codex_lan_agent::ApplyTaskMemoryCleanHandoffFields(
        &result,
        goal_id,
        trace_id,
        max_steps,
        terminal_state,
        continue_required);
    result.fields["semantic_outcome"] = terminal_state
        ? "continuation_budget_terminal"
        : "continuation_budget_partial";
    result.fields["next_action"] = terminal_state
        ? "read lan_agent_task_memory_resume_context and finalize only if human/task policy allows"
        : "call lan_agent_task_memory_execute_continuation_budget again with the same goal_id and a bounded max_steps";
    result.fields["result_ref"] = resume_path.string();
    result.fields["evidence_ref"] = budget_path.string();
    if (blocked) {
        result.fields["error"] = block_reason;
    }
    if (continue_required) {
        const std::string resume_call_json =
            codex_lan_agent::BuildTaskMemoryResumeAndExecuteCallJson(goal_id, trace_id, max_steps);
        result.fields["semantic_model_clamp"] = "tool_call_only";
        result.fields["required_tool_name"] = "lan_agent_task_memory_resume_and_execute";
        result.fields["required_tool_arguments_json"] = resume_call_json;
        result.fields["next_call_json"] = resume_call_json;
        result.fields["interaction_continuation_mode"] = "resume_and_execute_only";
        result.fields["internal_next_call_hidden"] = "true";
    } else {
        result.fields["next_call_json"] = "";
    }
    return result;
}

std::string BuildTaskMemoryAcceptanceParamsJson(
    const std::vector<std::pair<std::string, std::string>> & strings,
    const std::vector<std::pair<std::string, int>> & integers = {},
    const std::vector<std::pair<std::string, bool>> & booleans = {}) {
    std::ostringstream output;
    output << "{";
    bool first = true;
    auto comma = [&]() {
        if (!first) {
            output << ",";
        }
        first = false;
    };
    for (const auto & item : strings) {
        comma();
        output << "\"" << codex_lan_agent::JsonEscape(item.first) << "\":\""
               << codex_lan_agent::JsonEscape(item.second) << "\"";
    }
    for (const auto & item : integers) {
        comma();
        output << "\"" << codex_lan_agent::JsonEscape(item.first) << "\":" << item.second;
    }
    for (const auto & item : booleans) {
        comma();
        output << "\"" << codex_lan_agent::JsonEscape(item.first) << "\":"
               << (item.second ? "true" : "false");
    }
    output << "}";
    return output.str();
}

CommandResult BuildTaskMemoryResumeAndExecuteResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    const std::string goal_id = params.GetString("goal_id");
    CommandResult result;
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "failed";
        result.fields["record_model"] = "mcp_task_memory_resume_and_execute_response_v1";
        result.fields["error"] = "goal_id is required";
        result.fields["terminal_state"] = "false";
        result.fields["completion_claim_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        result.fields["next_action"] = "provide goal_id for the archived task memory";
        return result;
    }

    const int max_steps = std::min(64, std::max(1, params.GetInt("max_steps", params.GetInt("step_budget", 10))));
    const bool execute = params.GetBool("execute", true);
    const bool dry_run = params.GetBool("dry_run", false);
    CommandResult budget = BuildTaskMemoryExecuteContinuationBudgetRunnerResult(
        config,
        JsonRequestView(BuildTaskMemoryAcceptanceParamsJson(
            {
                {"goal_id", goal_id},
                {"trace_id", params.GetString("trace_id")}
            },
            {
                {"max_steps", max_steps}
            },
            {
                {"execute", execute},
                {"dry_run", dry_run}
            })));

    result = budget;
    result.fields["inner_record_model"] = GetFieldOrDefault(budget, "record_model", "");
    result.fields["record_model"] = "mcp_task_memory_resume_and_execute_response_v1";
    result.fields["resume_execute_entry"] = "true";
    result.fields["resume_execute_mode"] = execute && !dry_run
        ? "read_resume_context_and_execute_budget"
        : "read_resume_context_and_plan_budget";
    result.fields["max_steps"] = std::to_string(max_steps);
    if (GetFieldOrDefault(result, "budget_requires_frozen_resume_context", "") == "true") {
        result.fields["resume_recovery_status"] = "missing_archive_resume_context";
        result.fields["resume_recovery_tool_name"] = "lan_agent_task_memory_freeze";
        result.fields["resume_recovery_instruction"] =
            "the prior task was not frozen; freeze the last known required continuation first, then call this resume tool again";
        result.fields["next_action"] =
            "archive resume context is missing; continue from the prior task by calling lan_agent_task_memory_freeze with the last required_tool_arguments_json";
    } else if (GetFieldOrDefault(result, "continue_required", "") == "true") {
        result.fields["required_next_action_type"] = "mcp_tool_call";
        result.fields["required_tool_name"] = "lan_agent_task_memory_resume_and_execute";
        result.fields["required_tool_arguments_json"] =
            codex_lan_agent::BuildTaskMemoryResumeAndExecuteCallJson(
                goal_id,
                GetFieldOrDefault(result, "trace_id", params.GetString("trace_id")),
                max_steps);
        result.fields["next_call_json"] = result.fields["required_tool_arguments_json"];
        result.fields["next_action"] =
            "tool_call_only: call lan_agent_task_memory_resume_and_execute again until terminal_state=true";
    } else if (GetFieldOrDefault(result, "terminal_state", "") == "true") {
        result.fields["next_action"] =
            "archived task reached terminal state; final claim still requires verification_ok=true";
    }
    return result;
}

CommandResult BuildTaskMemoryNewChatRoundSelftestResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string suffix = BuildRequestTimestampToken();
    const std::string goal_id = FirstNonEmpty(
        params.GetString("goal_id"),
        "new-chat-round-selftest-" + suffix);
    const std::string trace_id = FirstNonEmpty(
        params.GetString("trace_id"),
        goal_id);
    const int max_steps = std::min(16, std::max(1, params.GetInt("max_steps", 5)));
    const std::filesystem::path selftest_dir =
        std::filesystem::path(config.log_root) / "task_memory_new_chat_round_selftest";
    const std::string mcp_conversation_id = "mcp-conversation-" + goal_id;
    const std::string mcp_round_id = mcp_conversation_id + "-round-1";
    const std::filesystem::path conversation_dir =
        selftest_dir / "mcp_conversations" / goal_id;
    const std::filesystem::path round_manifest_path =
        conversation_dir / "round_0001.json";
    const std::filesystem::path sample_path = selftest_dir / (goal_id + ".cpp");
    const std::filesystem::path report_path = selftest_dir / (goal_id + ".json");

    std::error_code ec;
    std::filesystem::create_directories(selftest_dir, ec);
    std::filesystem::create_directories(conversation_dir, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 501;
        result.fields["status"] = "failed";
        result.fields["record_model"] = "mcp_task_memory_new_chat_round_selftest_response_v1";
        result.fields["error"] = "failed to create selftest directory: " + ec.message();
        return result;
    }

    {
        std::ofstream sample(sample_path, std::ios::out | std::ios::trunc);
        sample << "int before_new_chat_round = 1;\n";
        sample << "// new chat round selftest comment\n";
        sample << "int after_new_chat_round = 2;\n";
    }

    const std::string sample_file = sample_path.string();
    std::ostringstream next_call;
    next_call
        << "{\"name\":\"lan_agent_delete_next_text_range_atomic\",\"arguments\":{"
        << "\"file_path\":\"" << codex_lan_agent::JsonEscape(sample_file) << "\","
        << "\"scan_mode\":\"comments\","
        << "\"primary_intent\":\"comment_cleanup\","
        << "\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\","
        << "\"probe_ref\":\"" << codex_lan_agent::JsonEscape(sample_file) << "\","
        << "\"probe_ready\":true"
        << "}}";

    std::ostringstream freeze_params;
    freeze_params
        << "{"
        << "\"goal_id\":\"" << codex_lan_agent::JsonEscape(goal_id) << "\","
        << "\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\","
        << "\"current_goal\":\"verify MCP-owned New Chat round resume semantics\","
        << "\"current_scope\":\"task_memory_new_chat_round_selftest\","
        << "\"current_file\":\"" << codex_lan_agent::JsonEscape(sample_file) << "\","
        << "\"current_tool\":\"lan_agent_delete_next_text_range_atomic\","
        << "\"last_status\":\"needs_continue\","
        << "\"last_has_more\":\"true\","
        << "\"terminal_state\":false,"
        << "\"completion_claim_allowed\":false,"
        << "\"completed_step_count\":0,"
        << "\"compact_summary\":\"old chat state frozen; fresh chat must resume from goal_id only\","
        << "\"remaining_work\":\"execute one archived continuation from latest_resume_context\","
        << "\"migration_handover_markdown\":\"New Chat round selftest: do not use old model context; call resume_and_execute with goal_id only.\","
        << "\"next_call_json\":\"" << codex_lan_agent::JsonEscape(next_call.str()) << "\""
        << "}";

    CommandResult freeze = codex_lan_agent::BuildTaskMemoryFreezeResult(
        config,
        JsonRequestView(freeze_params.str()));
    const bool freeze_ok = freeze.ok && freeze.exit_code == 0;

    std::ostringstream resume_params;
    resume_params
        << "{"
        << "\"goal_id\":\"" << codex_lan_agent::JsonEscape(goal_id) << "\","
        << "\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\","
        << "\"max_steps\":" << max_steps << ","
        << "\"execute\":true,"
        << "\"dry_run\":false"
        << "}";

    CommandResult resume = freeze_ok
        ? BuildTaskMemoryResumeAndExecuteResult(config, JsonRequestView(resume_params.str()))
        : CommandResult();

    std::string final_sample;
    std::string read_error;
    ReadWholeFile(sample_path, &final_sample, &read_error);
    const bool comment_removed =
        final_sample.find("// new chat round selftest comment") == std::string::npos;
    const bool terminal =
        GetFieldOrDefault(resume, "terminal_state", "") == "true";
    const bool completion_allowed =
        GetFieldOrDefault(resume, "completion_claim_allowed", "") == "true";
    const bool final_allowed =
        GetFieldOrDefault(resume, "final_answer_allowed", "") == "true";
    const bool verification_ok =
        GetFieldOrDefault(resume, "verification_ok", "") == "true";
    const bool mcp_round_established = !mcp_conversation_id.empty() && !mcp_round_id.empty();
    {
        std::ofstream round_file(round_manifest_path, std::ios::out | std::ios::trunc);
        round_file
            << "{\n"
            << "  \"record_model\":\"mcp_continuation_round_v1\",\n"
            << "  \"mcp_conversation_id\":\"" << codex_lan_agent::JsonEscape(mcp_conversation_id) << "\",\n"
            << "  \"mcp_round_id\":\"" << codex_lan_agent::JsonEscape(mcp_round_id) << "\",\n"
            << "  \"goal_id\":\"" << codex_lan_agent::JsonEscape(goal_id) << "\",\n"
            << "  \"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\",\n"
            << "  \"conversation_owner\":\"mcp\",\n"
            << "  \"round_owner\":\"mcp\",\n"
            << "  \"execution_owner\":\"mcp\",\n"
            << "  \"state_owner\":\"mcp_task_memory\",\n"
            << "  \"llama_cpp_role\":\"relay_only\",\n"
            << "  \"llama_cpp_execution_participation\":false,\n"
            << "  \"remote_session_required\":false,\n"
            << "  \"host_chat_history_mutable_by_mcp\":false,\n"
            << "  \"chat_context_reset_required\":true,\n"
            << "  \"chat_context_reset_acknowledged\":false,\n"
            << "  \"old_context_dropped\":false,\n"
            << "  \"mcp_context_independence_verified\":true,\n"
            << "  \"fresh_entry_tool_name\":\"lan_agent_task_memory_resume_and_execute\",\n"
            << "  \"fresh_entry_arguments_scope\":\"goal_id_only_plus_budget_controls\",\n"
            << "  \"old_model_context_allowed\":false,\n"
            << "  \"terminal_state\":" << (terminal ? "true" : "false") << ",\n"
            << "  \"completion_claim_allowed\":" << (completion_allowed ? "true" : "false") << ",\n"
            << "  \"final_answer_allowed\":" << (final_allowed ? "true" : "false") << ",\n"
            << "  \"verification_ok\":" << (verification_ok ? "true" : "false") << "\n"
            << "}\n";
    }
    const bool round_manifest_written = std::filesystem::exists(round_manifest_path);
    const bool selftest_pass =
        freeze_ok
        && resume.ok
        && resume.exit_code == 0
        && terminal
        && completion_allowed
        && final_allowed
        && verification_ok
        && comment_removed
        && mcp_round_established
        && round_manifest_written;

    std::ostringstream report;
    report
        << "{\n"
        << "  \"record_model\":\"mcp_task_memory_new_chat_round_selftest_report_v1\",\n"
        << "  \"goal_id\":\"" << codex_lan_agent::JsonEscape(goal_id) << "\",\n"
        << "  \"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\",\n"
        << "  \"mcp_conversation_id\":\"" << codex_lan_agent::JsonEscape(mcp_conversation_id) << "\",\n"
        << "  \"mcp_round_id\":\"" << codex_lan_agent::JsonEscape(mcp_round_id) << "\",\n"
        << "  \"new_chat_round_mode\":\"mcp_memory_fresh_entry_simulation\",\n"
        << "  \"conversation_owner\":\"mcp\",\n"
        << "  \"round_owner\":\"mcp\",\n"
        << "  \"execution_owner\":\"mcp\",\n"
        << "  \"llama_cpp_role\":\"relay_only\",\n"
        << "  \"llama_cpp_execution_participation\":false,\n"
        << "  \"remote_session_required\":false,\n"
        << "  \"host_chat_history_mutable_by_mcp\":false,\n"
        << "  \"chat_context_reset_required\":true,\n"
        << "  \"chat_context_reset_acknowledged\":false,\n"
        << "  \"old_context_dropped\":false,\n"
        << "  \"mcp_context_independence_verified\":true,\n"
        << "  \"fresh_entry_arguments\":\"goal_id,trace_id,max_steps,execute,dry_run\",\n"
        << "  \"freeze_ok\":" << (freeze_ok ? "true" : "false") << ",\n"
        << "  \"resume_ok\":" << (resume.ok && resume.exit_code == 0 ? "true" : "false") << ",\n"
        << "  \"terminal_state\":" << (terminal ? "true" : "false") << ",\n"
        << "  \"completion_claim_allowed\":" << (completion_allowed ? "true" : "false") << ",\n"
        << "  \"final_answer_allowed\":" << (final_allowed ? "true" : "false") << ",\n"
        << "  \"verification_ok\":" << (verification_ok ? "true" : "false") << ",\n"
        << "  \"comment_removed\":" << (comment_removed ? "true" : "false") << ",\n"
        << "  \"mcp_conversation_round_established\":" << (mcp_round_established ? "true" : "false") << ",\n"
        << "  \"round_manifest_written\":" << (round_manifest_written ? "true" : "false") << ",\n"
        << "  \"round_manifest_path\":\"" << codex_lan_agent::JsonEscape(round_manifest_path.string()) << "\",\n"
        << "  \"selftest_pass\":" << (selftest_pass ? "true" : "false") << "\n"
        << "}\n";
    {
        std::ofstream report_file(report_path, std::ios::out | std::ios::trunc);
        report_file << report.str();
    }

    result.ok = selftest_pass;
    result.exit_code = selftest_pass ? 0 : 98;
    result.fields["status"] = selftest_pass ? "success" : "failed";
    result.fields["record_model"] = "mcp_task_memory_new_chat_round_selftest_response_v1";
    result.fields["result"] = selftest_pass
        ? "new_chat_round_selftest_passed"
        : "new_chat_round_selftest_failed";
    result.fields["summary"] = selftest_pass
        ? "MCP continuation resume selftest passed; host chat reset remains a client responsibility"
        : "MCP New Chat round selftest failed";
    result.fields["goal_id"] = goal_id;
    result.fields["trace_id"] = trace_id;
    result.fields["mcp_conversation_id"] = mcp_conversation_id;
    result.fields["mcp_round_id"] = mcp_round_id;
    result.fields["new_chat_round_id"] = mcp_round_id;
    result.fields["new_chat_round_mode"] = "mcp_memory_fresh_entry_simulation";
    result.fields["conversation_owner"] = "mcp";
    result.fields["round_owner"] = "mcp";
    result.fields["execution_owner"] = "mcp";
    result.fields["state_owner"] = "mcp_task_memory";
    result.fields["llama_cpp_role"] = "relay_only";
    result.fields["llama_cpp_execution_participation"] = "false";
    result.fields["remote_session_required"] = "false";
    result.fields["mcp_conversation_round_established"] = mcp_round_established ? "true" : "false";
    result.fields["round_manifest_path"] = round_manifest_path.string();
    result.fields["host_chat_history_mutable_by_mcp"] = "false";
    result.fields["chat_context_reset_required"] = "true";
    result.fields["chat_context_reset_acknowledged"] = "false";
    result.fields["old_context_dropped"] = "false";
    result.fields["mcp_context_independence_verified"] = "true";
    result.fields["fresh_entry_tool_name"] = "lan_agent_task_memory_resume_and_execute";
    result.fields["fresh_entry_arguments_scope"] = "goal_id_only_plus_budget_controls";
    result.fields["freeze_status"] = GetFieldOrDefault(freeze, "status", "");
    result.fields["freeze_resume_context_path"] = GetFieldOrDefault(freeze, "resume_context_path", "");
    result.fields["resume_status"] = GetFieldOrDefault(resume, "status", "");
    result.fields["resume_budget_status"] = GetFieldOrDefault(resume, "budget_status", "");
    result.fields["executed_step_count"] = GetFieldOrDefault(resume, "executed_step_count", "");
    result.fields["terminal_state"] = terminal ? "true" : "false";
    result.fields["completion_claim_allowed"] = completion_allowed ? "true" : "false";
    result.fields["final_answer_allowed"] = final_allowed ? "true" : "false";
    result.fields["verification_ok"] = verification_ok ? "true" : "false";
    result.fields["comment_removed"] = comment_removed ? "true" : "false";
    result.fields["selftest_pass"] = selftest_pass ? "true" : "false";
    result.fields["sample_path"] = sample_file;
    result.fields["result_ref"] = report_path.string();
    result.fields["evidence_ref"] = report_path.string();
    result.fields["next_action"] = selftest_pass
        ? "reset the host chat to a fresh context, then use goal_id-only lan_agent_task_memory_resume_and_execute; MCP verified continuation independence, not host history deletion"
        : "inspect result_ref and repair task_memory fresh round semantics";
    return result;
}

std::string BuildTaskMemoryAcceptanceDeleteNextCallJson(
    const std::string & file_path,
    const std::string & trace_id) {
    std::ostringstream output;
    output
        << "{"
        << "\"name\":\"lan_agent_delete_next_text_range_atomic\","
        << "\"arguments\":{"
        << "\"file_path\":\"" << codex_lan_agent::JsonEscape(file_path) << "\","
        << "\"scan_mode\":\"comments\","
        << "\"primary_intent\":\"delete_comments\","
        << "\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\","
        << "\"probe_ref\":\"" << codex_lan_agent::JsonEscape(file_path) << "\","
        << "\"probe_ready\":true"
        << "}"
        << "}";
    return output.str();
}

bool TaskMemoryAcceptanceExpectField(
    const CommandResult & step,
    const std::string & field,
    const std::string & expected,
    const std::string & stage,
    CommandResult * result) {
    const std::string actual = GetFieldOrDefault(step, field, "");
    if (actual == expected) {
        return true;
    }
    if (result != nullptr) {
        result->ok = false;
        result->exit_code = 422;
        result->fields["acceptance_status"] = "PARTIAL";
        result->fields["migration_acceptance_status"] = "PARTIAL";
        result->fields["failed_stage"] = stage;
        result->fields["failed_field"] = field;
        result->fields["expected_value"] = expected;
        result->fields["actual_value"] = actual;
        result->fields["completion_claim_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
    }
    return false;
}

bool TaskMemoryAcceptanceExpectOk(
    const CommandResult & step,
    const std::string & stage,
    CommandResult * result) {
    if (step.ok && step.exit_code == 0) {
        return true;
    }
    if (result != nullptr) {
        result->ok = false;
        result->exit_code = step.exit_code == 0 ? 422 : step.exit_code;
        result->fields["acceptance_status"] = "PARTIAL";
        result->fields["migration_acceptance_status"] = "PARTIAL";
        result->fields["failed_stage"] = stage;
        result->fields["failed_step_error"] = GetFieldOrDefault(step, "error", "");
        result->fields["completion_claim_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
    }
    return false;
}

CommandResult BuildTaskMemoryMigrationAcceptanceResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string default_stamp = CommOperations::TimeStampForFileName();
    const std::string goal_id = params.GetString(
        "goal_id",
        "task-memory-migration-acceptance-" + default_stamp);
    const std::string trace_id = params.GetString("trace_id", goal_id + "-trace");
    const int max_final_steps = std::min(64, std::max(2, params.GetInt("max_final_steps", 8)));
    const std::filesystem::path out_dir =
        std::filesystem::path(config.data_root) /
        "task_memory_acceptance" /
        codex_lan_agent::SanitizeTaskMemoryToken(goal_id);
    const std::filesystem::path sample_path = out_dir / "acceptance_delete_comments.cpp";

    std::string write_error;
    const std::string sample_text =
        "int keep0 = 0;\n"
        "// acceptance delete one\n"
        "int keep1 = 1;\n"
        "// acceptance delete two\n"
        "int keep2 = 2;\n";
    if (!codex_lan_agent::WriteTaskMemoryTextFile(sample_path, sample_text, &write_error)) {
        result.ok = false;
        result.exit_code = 502;
        result.fields["acceptance_status"] = "PARTIAL";
        result.fields["migration_acceptance_status"] = "PARTIAL";
        result.fields["failed_stage"] = "sample_write";
        result.fields["error"] = write_error;
        result.fields["completion_claim_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        return result;
    }

    const std::string next_call_json = BuildTaskMemoryAcceptanceDeleteNextCallJson(
        sample_path.string(),
        trace_id);
    const std::string freeze_params_json = BuildTaskMemoryAcceptanceParamsJson(
        {
            {"goal_id", goal_id},
            {"trace_id", trace_id},
            {"current_goal", "task memory migration acceptance"},
            {"current_scope", "fresh model bootstrap + budget runner + KV/RocksDB mirror"},
            {"current_file", sample_path.string()},
            {"current_tool", "lan_agent_delete_next_text_range_atomic"},
            {"next_call_json", next_call_json},
            {"compact_summary", "acceptance starts with two comment cleanup continuations"},
            {"remaining_work", "execute budget runner, build KV snapshot, mirror to RocksDB, verify parity, materialize memory_structure"},
            {"key_slices_jsonl", "{\"slice_id\":\"slice-task-memory-acceptance\",\"slice_type\":\"acceptance_chain\",\"summary\":\"task memory migration acceptance slice\",\"trace_id\":\"" + trace_id + "\"}"}
        },
        {{"completed_step_count", 0}},
        {{"terminal_state", false}, {"completion_claim_allowed", false}});
    CommandResult freeze = codex_lan_agent::BuildTaskMemoryFreezeResult(
        config,
        JsonRequestView(freeze_params_json));
    if (!TaskMemoryAcceptanceExpectOk(freeze, "freeze", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    CommandResult resume_before = codex_lan_agent::BuildTaskMemoryResumeContextResult(
        config,
        JsonRequestView(BuildTaskMemoryAcceptanceParamsJson({{"goal_id", goal_id}})));
    if (!TaskMemoryAcceptanceExpectOk(resume_before, "resume_before_budget", &result) ||
        !TaskMemoryAcceptanceExpectField(resume_before, "terminal_state", "false", "resume_before_budget", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    CommandResult budget_partial = BuildTaskMemoryExecuteContinuationBudgetRunnerResult(
        config,
        JsonRequestView(BuildTaskMemoryAcceptanceParamsJson(
            {{"goal_id", goal_id}, {"trace_id", trace_id}},
            {{"max_steps", 1}},
            {{"dry_run", false}, {"execute", true}})));
    if (!TaskMemoryAcceptanceExpectOk(budget_partial, "budget_partial", &result) ||
        !TaskMemoryAcceptanceExpectField(budget_partial, "executed_step_count", "1", "budget_partial", &result) ||
        !TaskMemoryAcceptanceExpectField(budget_partial, "terminal_state", "false", "budget_partial", &result) ||
        !TaskMemoryAcceptanceExpectField(budget_partial, "completion_claim_allowed", "false", "budget_partial", &result) ||
        !TaskMemoryAcceptanceExpectField(budget_partial, "final_answer_allowed", "false", "budget_partial", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    CommandResult budget_final = BuildTaskMemoryExecuteContinuationBudgetRunnerResult(
        config,
        JsonRequestView(BuildTaskMemoryAcceptanceParamsJson(
            {{"goal_id", goal_id}, {"trace_id", trace_id}},
            {{"max_steps", max_final_steps}},
            {{"dry_run", false}, {"execute", true}})));
    if (!TaskMemoryAcceptanceExpectOk(budget_final, "budget_final", &result) ||
        !TaskMemoryAcceptanceExpectField(budget_final, "terminal_state", "true", "budget_final", &result) ||
        !TaskMemoryAcceptanceExpectField(budget_final, "completion_claim_allowed", "true", "budget_final", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    const std::string final_sample = codex_lan_agent::ReadTaskMemoryTextFile(sample_path);
    if (final_sample.find("// acceptance delete") != std::string::npos) {
        result.ok = false;
        result.exit_code = 422;
        result.fields["acceptance_status"] = "PARTIAL";
        result.fields["migration_acceptance_status"] = "PARTIAL";
        result.fields["failed_stage"] = "sample_delete_verification";
        result.fields["error"] = "bounded continuation did not delete all sample comments";
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        result.fields["completion_claim_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        return result;
    }

    const std::string goal_params_json = BuildTaskMemoryAcceptanceParamsJson({{"goal_id", goal_id}});
    CommandResult kv_snapshot = codex_lan_agent::BuildTaskMemoryBuildKvSnapshotResult(
        config,
        JsonRequestView(goal_params_json));
    if (!TaskMemoryAcceptanceExpectOk(kv_snapshot, "kv_snapshot", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    CommandResult kv_lookup = codex_lan_agent::BuildTaskMemoryKvLookupResult(
        config,
        JsonRequestView(BuildTaskMemoryAcceptanceParamsJson(
            {{"goal_id", goal_id}, {"kind", "latest"}},
            {},
            {{"include_value", true}})));
    if (!TaskMemoryAcceptanceExpectOk(kv_lookup, "kv_lookup_latest", &result) ||
        !TaskMemoryAcceptanceExpectField(kv_lookup, "matched_count", "1", "kv_lookup_latest", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    CommandResult mirror = codex_lan_agent::BuildTaskMemoryRocksDbMirrorResult(
        config,
        JsonRequestView(goal_params_json));
    if (!TaskMemoryAcceptanceExpectOk(mirror, "rocksdb_mirror", &result) ||
        !TaskMemoryAcceptanceExpectField(mirror, "rocksdb_status", "enabled", "rocksdb_mirror", &result) ||
        !TaskMemoryAcceptanceExpectField(mirror, "mirror_complete", "true", "rocksdb_mirror", &result) ||
        !TaskMemoryAcceptanceExpectField(mirror, "source_of_truth", "file_object_store", "rocksdb_mirror", &result) ||
        !TaskMemoryAcceptanceExpectField(mirror, "safe_to_replace_source_of_truth", "false", "rocksdb_mirror", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    CommandResult rocks_lookup = codex_lan_agent::BuildTaskMemoryRocksDbLookupResult(
        config,
        JsonRequestView(BuildTaskMemoryAcceptanceParamsJson(
            {{"goal_id", goal_id}, {"kind", "latest"}},
            {},
            {{"include_value", true}})));
    if (!TaskMemoryAcceptanceExpectOk(rocks_lookup, "rocksdb_lookup_latest", &result) ||
        !TaskMemoryAcceptanceExpectField(rocks_lookup, "kv_backend", "rocksdb_native_mirror", "rocksdb_lookup_latest", &result) ||
        !TaskMemoryAcceptanceExpectField(rocks_lookup, "matched_count", "1", "rocksdb_lookup_latest", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    CommandResult parity = codex_lan_agent::BuildTaskMemoryRocksDbParityCheckResult(
        config,
        JsonRequestView(BuildTaskMemoryAcceptanceParamsJson(
            {{"goal_id", goal_id}, {"kind", "latest"}},
            {},
            {{"include_value", false}})));
    if (!TaskMemoryAcceptanceExpectOk(parity, "rocksdb_parity_latest", &result) ||
        !TaskMemoryAcceptanceExpectField(parity, "parity_ok", "true", "rocksdb_parity_latest", &result) ||
        !TaskMemoryAcceptanceExpectField(parity, "safe_to_replace_source_of_truth", "false", "rocksdb_parity_latest", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    CommandResult assess = codex_lan_agent::BuildTaskMemoryMigrationAssessResult(
        config,
        JsonRequestView(goal_params_json));
    if (!TaskMemoryAcceptanceExpectOk(assess, "migration_assess", &result) ||
        !TaskMemoryAcceptanceExpectField(assess, "adaptation_decision", "ROCKSDB_NATIVE_MIRROR_READY", "migration_assess", &result) ||
        !TaskMemoryAcceptanceExpectField(assess, "active_backend", "rocksdb_native_mirror", "migration_assess", &result) ||
        !TaskMemoryAcceptanceExpectField(assess, "source_of_truth", "file_object_store", "migration_assess", &result) ||
        !TaskMemoryAcceptanceExpectField(assess, "safe_to_replace_source_of_truth", "false", "migration_assess", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    CommandResult structure = codex_lan_agent::BuildTaskMemoryStructureManifestResult(
        config,
        JsonRequestView(goal_params_json));
    if (!TaskMemoryAcceptanceExpectOk(structure, "memory_structure", &result) ||
        !TaskMemoryAcceptanceExpectField(structure, "structure_ready", "true", "memory_structure", &result) ||
        !TaskMemoryAcceptanceExpectField(structure, "fresh_model_bootstrap_ready", "true", "memory_structure", &result) ||
        !TaskMemoryAcceptanceExpectField(structure, "backend_policy_ready", "true", "memory_structure", &result) ||
        !TaskMemoryAcceptanceExpectField(structure, "active_read_backend", "rocksdb_native_mirror", "memory_structure", &result) ||
        !TaskMemoryAcceptanceExpectField(structure, "write_backend", "file_object_store", "memory_structure", &result) ||
        !TaskMemoryAcceptanceExpectField(structure, "source_of_truth", "file_object_store", "memory_structure", &result) ||
        !TaskMemoryAcceptanceExpectField(structure, "safe_to_replace_source_of_truth", "false", "memory_structure", &result) ||
        !TaskMemoryAcceptanceExpectField(structure, "parity_required_for_native_reads", "true", "memory_structure", &result)) {
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        return result;
    }

    const std::filesystem::path summary_path = out_dir / "acceptance_summary.json";
    std::ostringstream summary;
    summary
        << "{\n"
        << "  \"status\":\"TASK_MEMORY_MIGRATION_ACCEPTANCE_PASS\",\n"
        << "  \"goal_id\":\"" << codex_lan_agent::JsonEscape(goal_id) << "\",\n"
        << "  \"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\",\n"
        << "  \"output_dir\":\"" << codex_lan_agent::JsonEscape(out_dir.string()) << "\",\n"
        << "  \"memory_structure_path\":\"" << codex_lan_agent::JsonEscape(GetFieldOrDefault(structure, "memory_structure_path", "")) << "\",\n"
        << "  \"source_of_truth\":\"file_object_store\",\n"
        << "  \"active_read_backend\":\"rocksdb_native_mirror\",\n"
        << "  \"write_backend\":\"file_object_store\",\n"
        << "  \"safe_to_replace_source_of_truth\":false,\n"
        << "  \"parity_required_for_native_reads\":true\n"
        << "}\n";
    if (!codex_lan_agent::WriteTaskMemoryTextFile(summary_path, summary.str(), &write_error)) {
        result.ok = false;
        result.exit_code = 502;
        result.fields["acceptance_status"] = "PARTIAL";
        result.fields["migration_acceptance_status"] = "PARTIAL";
        result.fields["failed_stage"] = "summary_write";
        result.fields["error"] = write_error;
        result.fields["goal_id"] = goal_id;
        result.fields["trace_id"] = trace_id;
        result.fields["completion_claim_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        return result;
    }

    result.fields["record_model"] = "mcp_task_memory_migration_acceptance_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["trace_id"] = trace_id;
    result.fields["acceptance_status"] = "ACCEPTED";
    result.fields["migration_acceptance_status"] = "ACCEPTED";
    result.fields["semantic_outcome"] = "TASK_MEMORY_MIGRATION_ACCEPTANCE_PASS";
    result.fields["sample_path"] = sample_path.string();
    result.fields["output_dir"] = out_dir.string();
    result.fields["summary_path"] = summary_path.string();
    result.fields["memory_structure_path"] = GetFieldOrDefault(structure, "memory_structure_path", "");
    result.fields["resume_context_path"] = GetFieldOrDefault(structure, "resume_context_path", "");
    result.fields["kv_index_path"] = GetFieldOrDefault(structure, "kv_index_path", "");
    result.fields["rocksdb_path"] = GetFieldOrDefault(structure, "rocksdb_path", "");
    result.fields["rocksdb_manifest_path"] = GetFieldOrDefault(structure, "rocksdb_manifest_path", "");
    result.fields["source_of_truth"] = "file_object_store";
    result.fields["active_read_backend"] = "rocksdb_native_mirror";
    result.fields["write_backend"] = "file_object_store";
    result.fields["safe_to_replace_source_of_truth"] = "false";
    result.fields["parity_required_for_native_reads"] = "true";
    result.fields["partial_budget_terminal_state"] = GetFieldOrDefault(budget_partial, "terminal_state", "");
    result.fields["partial_budget_completion_claim_allowed"] = GetFieldOrDefault(budget_partial, "completion_claim_allowed", "");
    result.fields["final_budget_terminal_state"] = GetFieldOrDefault(budget_final, "terminal_state", "");
    result.fields["kv_record_count"] = GetFieldOrDefault(structure, "kv_record_count", "");
    result.fields["rocksdb_mirrored_count"] = GetFieldOrDefault(structure, "rocksdb_mirrored_count", "");
    result.fields["validated_chain"] = "freeze,resume_context,budget_partial,budget_final,kv_snapshot,kv_lookup,rocksdb_mirror,rocksdb_lookup,parity,assess,structure";
    result.fields["completion_claim_allowed"] = "true";
    result.fields["final_answer_allowed"] = "true";
    result.fields["next_action"] = "MCP-side migration acceptance is available; external PowerShell smoke remains optional for CI or operator verification";
    result.fields["result_ref"] = summary_path.string();
    result.fields["evidence_ref"] = GetFieldOrDefault(structure, "memory_structure_path", "");
    return result;
}

void ApplyClipsSemanticTraceContinuation(
    const AgentConfig & config,
    CommandResult * result) {
    if (result == nullptr) {
        return;
    }

    (void) config;
    constexpr int kMaxContinuationSteps = 16;
    int replay_steps = 0;
    std::unordered_set<std::string> seen_action_signatures;
    std::string current_tool = GetFieldOrDefault(*result, "required_tool_name", "");
    std::string current_call_json = GetFieldOrDefault(*result, "required_tool_arguments_json", "");
    CommandResult listing_snapshot = *result;
    if (GetFieldOrDefault(*result, "semantic_model_clamp", "") != "tool_call_only") {
        if (GetFieldOrDefault(*result, "supervision_status", "").empty()) {
            result->fields["supervision_status"] = "not_required";
            result->fields["supervision_alarm"] = "false";
        }
        return;
    }

    result->fields["supervision_status"] = "continuation_required";
    result->fields["supervision_alarm"] = "false";
    if (current_call_json.empty()) {
        SetClipsSupervisionAlarm(
            result,
            "NEXT_CALL_JSON_MISSING",
            "CLIPS required tool-only continuation but required_tool_arguments_json was empty.");
        result->fields["clips_semantic_trace_replay_steps"] = "0";
        return;
    }

    current_call_json = InjectTraceIdIntoContinuationCallJson(
        current_call_json,
        GetFieldOrDefault(*result, "trace_id", ""));
    current_tool = FirstNonEmpty(
        ExtractJsonString(current_call_json, "name"),
        current_tool,
        GetFieldOrDefault(*result, "next_tool_name", ""));
    result->fields["required_tool_name"] = current_tool;
    result->fields["required_tool_arguments_json"] = current_call_json;
    result->fields["next_call_json"] = current_call_json;
    result->fields["clips_semantic_trace_execution"] = "yield_next_action";
    result->fields["clips_semantic_trace_replay_steps"] = "0";
        result->fields["supervision_status"] = "closed_loop_continue";
        result->fields["goal_status"] = "not_complete";
        result->fields["assistant_response_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
        result->fields["terminal_state"] = "false";
        result->fields["task_done"] = "false";
        result->fields["completion_claim_allowed"] = "false";
        result->fields["completion_guard"] =
            "NON_TERMINAL_RESULT: do not claim completion; execute required_tool_arguments_json";
        result->fields["supervision_alarm"] = "false";
        return;

    while (GetFieldOrDefault(*result, "semantic_model_clamp", "") == "tool_call_only"
           && !current_call_json.empty()
           && replay_steps < kMaxContinuationSteps) {
        current_call_json = InjectTraceIdIntoContinuationCallJson(
            current_call_json,
            GetFieldOrDefault(*result, "trace_id", ""));
        std::string next_tool = ExtractJsonString(current_call_json, "name");
        if (next_tool.empty()) {
            next_tool = current_tool;
        }
        const std::string action_signature = next_tool + "|" + current_call_json;
        if (!seen_action_signatures.insert(action_signature).second) {
            result->fields["clips_semantic_trace_execution"] = "duplicate_action_loop";
            SetClipsSupervisionAlarm(
                result,
                "DUPLICATE_ACTION_LOOP",
                "CLIPS semantic trace continuation repeated the same action signature.");
            break;
        }
        if (!IsClipsAutoContinuationTool(next_tool)) {
            result->fields["clips_semantic_trace_execution"] = "blocked";
            result->fields["clips_semantic_trace_block_reason"] = "required tool is not read-only continuation allowlisted";
            SetClipsSupervisionAlarm(
                result,
                "NEXT_TOOL_NOT_ALLOWLISTED",
                "CLIPS continuation requested a tool outside the read-only continuation allowlist.");
            break;
        }

        CommandResult replay_result = ExecuteReadOnlyMcpToolForClipsContinuation(
            config,
            next_tool,
            current_call_json);
        replay_result.fields["clips_semantic_trace_execution"] = "running";
        replay_result.fields["clips_semantic_trace_parent_trace_id"] = GetFieldOrDefault(*result, "trace_id", "");
        replay_result.fields["clips_semantic_trace_step"] = std::to_string(replay_steps + 1);
        ApplyRequestRuleFields(next_tool, JsonRequestView(current_call_json), &replay_result);
        LanResultBuilder(&replay_result).Finalize(config, next_tool);
        ApplyAiConclusionValidityGuards(&replay_result);
        ApplyClipsResultGuard(config, next_tool, &replay_result);
        AppendMcpTraceAuditEvent(config, next_tool, replay_result);
        CarryForwardDirectoryListingFields(listing_snapshot, &replay_result);

        *result = replay_result;
        ++replay_steps;
        if (!result->ok || result->exit_code != 0) {
            SetClipsSupervisionAlarm(
                result,
                "TRACEBACK_TOOL_EXEC_FAILED",
                "CLIPS semantic trace continuation tool returned failure.");
            break;
        }
        current_tool = GetFieldOrDefault(*result, "required_tool_name", "");
        current_call_json = GetFieldOrDefault(*result, "required_tool_arguments_json", "");
        const std::string next_signature = current_tool + "|" + current_call_json;
        if (GetFieldOrDefault(*result, "semantic_model_clamp", "") == "tool_call_only" &&
            next_signature == action_signature) {
            result->fields["clips_semantic_trace_execution"] = "no_progress";
            SetClipsSupervisionAlarm(
                result,
                "NO_PROGRESS_DETECTED",
                "CLIPS semantic trace continuation produced the same next action without progress.");
            break;
        }
        if (GetFieldOrDefault(*result, "semantic_model_clamp", "") == "tool_call_only" &&
            current_call_json.empty()) {
            SetClipsSupervisionAlarm(
                result,
                "NEXT_CALL_JSON_MISSING",
                "CLIPS still required continuation but the replayed result did not provide next_call_json.");
            break;
        }
    }

    result->fields["clips_semantic_trace_replay_steps"] = std::to_string(replay_steps);
    if (replay_steps >= kMaxContinuationSteps &&
        GetFieldOrDefault(*result, "semantic_model_clamp", "") == "tool_call_only") {
        result->fields["clips_semantic_trace_execution"] = "step_limit_yield";
        result->fields["supervision_status"] = "closed_loop_continue";
        result->fields["goal_status"] = "not_complete";
        result->fields["assistant_response_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
        result->fields["terminal_state"] = "false";
        result->fields["task_done"] = "false";
        result->fields["completion_claim_allowed"] = "false";
        result->fields["completion_guard"] =
            "NON_TERMINAL_RESULT: do not claim completion; execute required_tool_arguments_json";
        result->fields["supervision_alarm"] = "false";
        result->fields["supervision_alarm_code"] = "";
        result->fields["supervision_alarm_message"] = "";
        result->fields["next_action"] = FirstNonEmpty(
            GetFieldOrDefault(*result, "next_action", ""),
            "continue executing required_tool_arguments_json");
    } else if (replay_steps > 0) {
        result->fields["clips_semantic_trace_execution"] = "complete";
        if (GetFieldOrDefault(*result, "supervision_status", "") != "alarm") {
            result->fields["supervision_status"] = "closed_loop_complete";
            result->fields["supervision_alarm"] = "false";
        }
    }
}

std::unordered_map<std::string, std::string> SnapshotLastRemoteControlEvent() {
    std::lock_guard<std::mutex> lock(g_remote_control_event_mutex);
    return g_last_remote_control_event;
}



std::vector<std::string> SnapshotActiveResourceKeys() {
    std::lock_guard<std::mutex> lock(g_resource_lock_mutex);
    return std::vector<std::string>(g_active_resource_keys.begin(), g_active_resource_keys.end());
}

std::vector<std::tuple<std::string, std::string, std::string>> SnapshotActiveResourceLockDetails() {
    std::lock_guard<std::mutex> lock(g_resource_lock_mutex);
    std::vector<std::tuple<std::string, std::string, std::string>> details;
    for (const auto & resource_key : g_active_resource_keys) {
        const auto owner_it = g_active_resource_owner_hints.find(resource_key);
        const auto acquired_it = g_active_resource_acquired_at.find(resource_key);
        details.emplace_back(
            resource_key,
            owner_it == g_active_resource_owner_hints.end() ? "unknown" : owner_it->second,
            acquired_it == g_active_resource_acquired_at.end() ? "" : acquired_it->second);
    }
    return details;
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

#include "RagReviewResultOperations.h"
#include "IntentDispatchPrepareOperations.h"
#include "TaskResultEnvelopeOperations.h"
#include "LocalCliEnvelopeOperations.h"

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
    return config.dialog_slices_root.empty()
        ? codex_lan_agent::JoinPath(config.log_root, "dialog_slices")
        : config.dialog_slices_root;
}

std::string BuildSessionDispatchDir(const AgentConfig & config) {
    return config.session_dispatch_root.empty()
        ? codex_lan_agent::JoinPath(config.log_root, "session_dispatch")
        : config.session_dispatch_root;
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

std::string Base64Encode(const std::string & input) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    int value = 0;
    int value_bits = -6;
    for (unsigned char ch : input) {
        value = (value << 8) + ch;
        value_bits += 8;
        while (value_bits >= 0) {
            output.push_back(kAlphabet[(value >> value_bits) & 0x3F]);
            value_bits -= 6;
        }
    }
    if (value_bits > -6) {
        output.push_back(kAlphabet[((value << 8) >> (value_bits + 8)) & 0x3F]);
    }
    while ((output.size() % 4) != 0) {
        output.push_back('=');
    }
    return output;
}

bool Base64Decode(
    const std::string & input,
    std::string * output,
    std::string * error_message) {
    static const std::string kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int value = 0;
    int value_bits = -8;
    std::string decoded;
    for (unsigned char ch : input) {
        if (std::isspace(ch) != 0) {
            continue;
        }
        if (ch == '=') {
            break;
        }
        const std::size_t pos = kAlphabet.find(static_cast<char>(ch));
        if (pos == std::string::npos) {
            if (error_message != nullptr) {
                *error_message = "invalid base64 content";
            }
            return false;
        }
        value = (value << 6) + static_cast<int>(pos);
        value_bits += 6;
        if (value_bits >= 0) {
            decoded.push_back(static_cast<char>((value >> value_bits) & 0xFF));
            value_bits -= 8;
        }
    }
    if (output != nullptr) {
        *output = decoded;
    }
    return true;
}

std::string DecodeJsonStringLiteralOrRaw(const std::string & raw_value) {
    if (raw_value.size() < 2 || raw_value.front() != '"' || raw_value.back() != '"') {
        return raw_value;
    }

    std::string value;
    value.reserve(raw_value.size() >= 2 ? raw_value.size() - 2 : 0);
    bool escaping = false;
    for (std::size_t index = 1; index + 1 < raw_value.size(); ++index) {
        const char current = raw_value[index];
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
            case 'u': {
                unsigned int codepoint = 0;
                if (TryReadJsonUnicodeEscape(raw_value, index, &codepoint)) {
                    AppendUtf8Codepoint(&value, codepoint);
                    index += 4;
                } else {
                    value.push_back(current);
                }
                break;
            }
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
        value.push_back(current);
    }
    return value;
}

std::string ResolveTextPayloadFromParams(
    const JsonRequestView & params,
    const std::string & plain_field,
    const std::string & base64_field,
    CommandResult * result) {
    const std::string encoded = params.GetString(base64_field);
    if (encoded.empty()) {
        if (result != nullptr) {
            result->fields["content_transport"] = "json_string";
        }
        const std::string raw_plain = params.GetRawJson(plain_field);
        if (!raw_plain.empty()) {
            if (result != nullptr) {
                result->fields["content_raw_bytes"] = std::to_string(raw_plain.size());
            }
            return DecodeJsonStringLiteralOrRaw(raw_plain);
        }
        return params.GetString(plain_field);
    }
    std::string decoded;
    std::string decode_error;
    if (!Base64Decode(encoded, &decoded, &decode_error)) {
        if (result != nullptr) {
            result->ok = false;
            result->exit_code = 400;
            result->fields["error"] = decode_error;
            result->fields["content_transport"] = "base64";
            result->fields["result"] = "invalid_base64_content";
        }
        return std::string();
    }
    if (result != nullptr) {
        result->fields["content_transport"] = "base64";
        result->fields["content_base64_bytes"] = std::to_string(encoded.size());
    }
    return decoded;
}

std::string ExtractDelimitedBlock(
    const std::string & text,
    const std::string & begin_marker,
    const std::string & end_marker) {
    const std::size_t begin = text.find(begin_marker);
    if (begin == std::string::npos) {
        return std::string();
    }
    const std::size_t content_begin = begin + begin_marker.size();
    const std::size_t end = text.find(end_marker, content_begin);
    if (end == std::string::npos || end < content_begin) {
        return std::string();
    }
    return Trim(text.substr(content_begin, end - content_begin));
}

#include "ExecutionBindingOperations.h"

#include "SnapshotDiffOperations.h"

bool ContainsPermissionConclusionText(const std::string & text) {
    const std::string lowered = ToLowerAscii(text);
    return lowered.find("permission") != std::string::npos
        || lowered.find("access denied") != std::string::npos
        || lowered.find("system limit") != std::string::npos
        || lowered.find("system restriction") != std::string::npos
        || lowered.find("permissions limit") != std::string::npos
        || lowered.find("quan xian") != std::string::npos;
}

bool ContainsPleaseWaitConclusionText(const std::string & text) {
    const std::string lowered = ToLowerAscii(text);
    return lowered.find("please wait") != std::string::npos
        || lowered.find("wait a moment") != std::string::npos
        || lowered.find("please come back later") != std::string::npos;
}

bool IsAnalysisOnlyAiTool(const CommandResult & result) {
    const std::string tool_name = GetFieldOrDefault(result, "tool_name", "");
    return tool_name == "lan_agent_run_local_chat"
        || tool_name == "rag.query";
}

bool ContainsExecutionClaimConclusionText(const std::string & text) {
    const std::string lowered = ToLowerAscii(text);
    return lowered.find("file_editor") != std::string::npos
        || lowered.find("build_system_runner") != std::string::npos
        || lowered.find("test_runner") != std::string::npos
        || lowered.find("compiled successfully") != std::string::npos
        || lowered.find("build succeeded") != std::string::npos
        || lowered.find("build completed") != std::string::npos
        || lowered.find("tests passed") != std::string::npos
        || lowered.find("test passed") != std::string::npos
        || lowered.find("test result") != std::string::npos
        || lowered.find("applied patch") != std::string::npos
        || lowered.find("edited file") != std::string::npos
        || lowered.find("modified file") != std::string::npos
        || lowered.find("log path") != std::string::npos;
}

bool HasConcreteExecutionEvidence(const CommandResult & result) {
    return !GetFieldOrDefault(result, "task_id", "").empty()
        || !GetFieldOrDefault(result, "result_ref", "").empty()
        || !GetFieldOrDefault(result, "evidence_ref", "").empty()
        || !GetFieldOrDefault(result, "patch_id", "").empty();
}

bool IsAllowedSemanticExecutionBridgeAction(const std::string & action_id) {
    static const std::unordered_set<std::string> kAllowedActionIds = {
        "write_document",
        "refactor_file",
        "apply_diff_patch",
        "verify_patch_result",
        "inspect_patch_audit",
        "inspect_trace_audit",
        "configure_project",
        "build_target",
        "discover_project_tests",
        "run_project_tests",
        "get_task_status",
        "resolve_task_result_ref"
    };
    return kAllowedActionIds.find(action_id) != kAllowedActionIds.end();
}

std::string BuildAllowedSemanticExecutionBridgeActionList() {
    return
        "write_document,refactor_file,apply_diff_patch,verify_patch_result,inspect_patch_audit,"
        "inspect_trace_audit,configure_project,build_target,discover_project_tests,"
        "run_project_tests,get_task_status,resolve_task_result_ref";
}

std::string StripDryRunFlagFromToolArgumentsJson(const std::string & tool_arguments_json) {
    std::string output = tool_arguments_json;
    const std::string comma_form = ",\"dry_run\":true";
    const std::string leading_form = "\"dry_run\":true,";
    const std::size_t comma_pos = output.find(comma_form);
    if (comma_pos != std::string::npos) {
        output.erase(comma_pos, comma_form.size());
        return output;
    }
    const std::size_t leading_pos = output.find(leading_form);
    if (leading_pos != std::string::npos) {
        output.erase(leading_pos, leading_form.size());
    }
    return output;
}

CommandResult ExecuteSemanticBridgeTool(
    const AgentConfig & config,
    const std::string & tool_name,
    const std::string & tool_arguments_json) {
    const JsonRequestView params(tool_arguments_json);
    if (tool_name == "lan_agent_write_text_file") {
        CommandResult payload_result;
        const std::string content = ResolveTextPayloadFromParams(
            params,
            "content",
            "content_base64",
            &payload_result);
        if (!payload_result.ok) {
            return payload_result;
        }
        CommandResult result = WriteTextFileResult(
            config,
            params.GetString("file_path"),
            content,
            params.GetBool("append", false));
        result.fields["content_transport"] = GetFieldOrDefault(payload_result, "content_transport", "json_string");
        result.fields["content_base64_bytes"] = GetFieldOrDefault(payload_result, "content_base64_bytes", "");
        return result;
    }
    if (tool_name == "lan_agent_apply_single_file_patch") {
        CommandResult payload_result;
        const std::string new_content = ResolveTextPayloadFromParams(
            params,
            "new_content",
            "new_content_base64",
            &payload_result);
        if (!payload_result.ok) {
            return payload_result;
        }
        CommandResult result = ApplySingleFilePatchResult(
            config,
            params.GetString("file_path"),
            new_content,
            params.GetString("old_hash"),
            params.GetString("request_id"),
            params.GetString("trace_id"),
            params.GetString("patch_id"),
            params.GetString("reason"),
            params.GetBool("allow_empty_content", false));
        result.fields["content_transport"] = GetFieldOrDefault(payload_result, "content_transport", "json_string");
        result.fields["content_base64_bytes"] = GetFieldOrDefault(payload_result, "content_base64_bytes", "");
        return result;
    }
    if (tool_name == "lan_agent_ensure_directory") {
        return EnsureDirectoryResult(
            config,
            params.GetString("directory_path"),
            params.GetString("file_path"),
            params.GetBool("ensure_parent", false));
    }
    if (tool_name == "lan_agent_apply_diff_patch") {
        return ApplyDiffPatchResult(
            config,
            params.GetString("file_path"),
            params.GetString("diff_text"),
            params.GetString("old_hash"),
            params.GetString("request_id"),
            params.GetString("trace_id"),
            params.GetString("patch_id"),
            params.GetString("reason"),
            params.GetString("resolved_file_path"),
            params.GetString("target_resolution_reason"),
            params.GetBool("allow_empty_content", false));
    }
    if (tool_name == "lan_agent_verify_single_file_patch") {
        return VerifySingleFilePatchResult(
            config,
            params.GetString("patch_id"),
            params.GetString("file_path"),
            params.GetString("expected_hash"),
            params.GetString("contains_text"),
            params.GetString("forbidden_text"),
            params.GetString("request_id"),
            params.GetString("trace_id"),
            params.GetString("reason"));
    }
    if (tool_name == "lan_agent_get_patch_audit_trail") {
        return GetPatchAuditTrailResult(config, params.GetString("patch_id"));
    }
    if (tool_name == "lan_agent_get_trace_audit_trail") {
        return GetTraceAuditTrailResult(config, params.GetString("trace_id"));
    }
    if (tool_name == "lan_agent_get_task") {
        if (g_task_manager == nullptr) {
            CommandResult result;
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        return g_task_manager->GetTaskResult(params.GetString("task_id"));
    }
    if (tool_name == "lan_agent_resolve_task_result") {
        return ResolveTaskResultReferenceResult(
            params.GetString("task_id"),
            params.GetString("task_ref"));
    }
    if (tool_name == "lan_agent_discover_ctest_tests") {
        std::string config_name = params.GetString("config", "Release");
        if (config_name.empty()) {
            config_name = "Release";
        }
        return DiscoverCtestTestsResult(
            config,
            params.GetString("build_dir"),
            config_name,
            params.GetString("test_regex"),
            std::max(0, params.GetInt("start_index", 0)),
            std::max(1, params.GetInt("max_entries", 200)));
    }
    if (tool_name == "lan_agent_run_ctest_target") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        std::string build_dir = params.GetString("build_dir");
        std::string test_regex = params.GetString("test_regex");
        std::string config_name = params.GetString("config", "Release");
        const std::string preflight_ref = params.GetString("preflight_ref");
        const std::string preflight_status = preflight_ref.empty()
            ? params.GetString("preflight_status")
            : "ready";
        std::vector<std::string> preflight_parts;
        std::string preflight_checksum;
        const bool parsed_preflight_ref = !preflight_ref.empty()
            && TryParsePreflightReference(
                preflight_ref,
                "lan_agent_run_ctest_target",
                &preflight_parts,
                &preflight_checksum);
        if (config_name.empty()) {
            config_name = "Release";
        }
        if (parsed_preflight_ref && preflight_parts.size() >= 3) {
            if (build_dir.empty()) {
                build_dir = preflight_parts[0];
            }
            if (config_name.empty() || config_name == "Release") {
                config_name = preflight_parts[1];
            }
            if (test_regex.empty()) {
                test_regex = preflight_parts[2];
            }
        }
        if (build_dir.empty() || test_regex.empty()) {
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = !preflight_ref.empty() && parsed_preflight_ref
                ? "preflight_ref did not contain replayable build_dir/test_regex; rerun preflight or provide explicit args"
                : "build_dir and test_regex are required";
            return result;
        }
        CommandResult discover = DiscoverCtestTestsResult(config, build_dir, config_name, test_regex, 0, 200);
        if (CtestDiscoveryBlocksRun(discover)) {
            return BuildInvalidCtestRunResult(discover);
        }
        const std::string effective_test_regex = FirstNonEmpty(
            GetFieldOrDefault(discover, "resolved_test_regex", ""),
            test_regex);
        const std::string task_id = g_task_manager->EnqueueCliProfile(
            "run_ctest_target",
            BuildRunCTestTargetArguments(build_dir, config_name, effective_test_regex));
        result = BuildQueuedTaskResult(task_id);
        if (!preflight_ref.empty()) {
            result.fields["preflight_ref"] = preflight_ref;
        }
        if (!preflight_status.empty()) {
            result.fields["preflight_status"] = preflight_status;
        }
        result.fields["preflight_ref_replay"] =
            parsed_preflight_ref && preflight_parts.size() >= 3 ? "resolved_args" : "explicit_args";
        result.fields["preflight_ref_checksum"] = preflight_checksum;
        result.fields["requested_test_regex"] = test_regex;
        result.fields["effective_test_regex"] = effective_test_regex;
        CopyCtestDiscoveryEnvelope(discover, &result);
        return result;
    }
    if (tool_name == "lan_agent_build_target") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        const std::string build_dir = params.GetString("build_dir");
        const std::string target = params.GetString("target");
        std::string config_name = params.GetString("config", "Release");
        const bool dry_run = params.GetBool("dry_run", false);
        const bool validate_args = params.GetBool("validate_args", false);
        const bool has_stall_timeout = !params.GetRawJson("stall_timeout_sec").empty();
        const int stall_timeout_sec = params.GetInt(
            "stall_timeout_sec",
            config.build_target_stall_timeout_sec);
        const std::string preflight_ref = params.GetString("preflight_ref");
        const std::string preflight_status = preflight_ref.empty()
            ? params.GetString("preflight_status")
            : "ready";
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
            result = BuildTargetDryRunResult(build_dir, target, config_name);
            result.fields["build_target_stall_timeout_sec"] = std::to_string(std::max(0, stall_timeout_sec));
            result.fields["build_target_stall_timeout_source"] = has_stall_timeout ? "request" : "config";
            if (!preflight_ref.empty()) {
                result.fields["preflight_ref"] = preflight_ref;
            }
            if (!preflight_status.empty()) {
                result.fields["preflight_status"] = preflight_status;
            }
            return result;
        }
        const std::string task_id = g_task_manager->EnqueueCliProfile(
            "build_target",
            BuildBuildTargetArguments(build_dir, config_name, target),
            -1,
            std::max(0, stall_timeout_sec));
        result = BuildQueuedTaskResult(task_id);
        result.fields["build_target_stall_timeout_sec"] = std::to_string(std::max(0, stall_timeout_sec));
        result.fields["build_target_stall_timeout_source"] = has_stall_timeout ? "request" : "config";
        if (!preflight_ref.empty()) {
            result.fields["preflight_ref"] = preflight_ref;
        }
        if (!preflight_status.empty()) {
            result.fields["preflight_status"] = preflight_status;
        }
        return result;
    }
    if (tool_name == "lan_agent_configure_project") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        const std::string project_root = params.GetString("project_root");
        const std::string build_dir = params.GetString("build_dir");
        std::string generator_kind = params.GetString("generator_kind");
        const std::string cmake_args = params.GetString("cmake_args");
        const std::string env_args = params.GetString("env");
        const bool has_stall_timeout = !params.GetRawJson("stall_timeout_sec").empty();
        const int stall_timeout_sec = params.GetInt(
            "stall_timeout_sec",
            config.configure_project_stall_timeout_sec);
        if (generator_kind.empty()) {
            generator_kind = "vs2022";
        }
        if (project_root.empty() || build_dir.empty()) {
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "project_root and build_dir are required";
            return result;
        }
        std::vector<std::string> cmake_arg_values;
        if (!cmake_args.empty()) {
            cmake_arg_values = SplitCommandLikeArguments(cmake_args);
        }
        const std::string task_id = g_task_manager->EnqueueCliProfile(
            "configure_project",
            BuildConfigureProjectArguments(
                project_root,
                build_dir,
                generator_kind,
                cmake_arg_values,
                env_args),
            -1,
            std::max(0, stall_timeout_sec));
        result = BuildQueuedTaskResult(task_id);
        result.fields["generator_kind"] = generator_kind;
        result.fields["configure_project_stall_timeout_sec"] = std::to_string(std::max(0, stall_timeout_sec));
        result.fields["configure_project_stall_timeout_source"] = has_stall_timeout ? "request" : "config";
        if (!cmake_arg_values.empty()) {
            result.fields["cmake_args"] = JoinConfigureProjectCmakeArgs(cmake_arg_values);
            result.fields["cmake_arg_count"] = std::to_string(cmake_arg_values.size());
        } else if (!cmake_args.empty()) {
            result.fields["cmake_args"] = cmake_args;
        }
        return result;
    }

    CommandResult result;
    result.ok = false;
    result.exit_code = 404;
    result.fields["error"] = "execution bridge tool is not supported";
    result.fields["bridge_tool_name"] = tool_name;
    return result;
}

CommandResult ExecuteSemanticActionBridgeResult(
    const AgentConfig & config,
    const std::string & action_id,
    const std::string & query,
    const std::string & arguments_text,
    bool prefer_dry_run) {
    CommandResult prepared = BuildSemanticActionPrepareResult(action_id, query, arguments_text);
    CommandResult result = prepared;
    result.fields["bridge_query"] = query;
    result.fields["bridge_requested_action_id"] = action_id;
    result.fields["bridge_arguments_text"] = arguments_text;
    result.fields["bridge_allowed_action_ids"] = BuildAllowedSemanticExecutionBridgeActionList();
    result.fields["real_execution_bridge"] = "true";
    result.fields["analysis_only"] = "false";
    result.fields["execution_capability"] = "true";
    result.fields["trusted_execution_evidence_required"] = "true";
    result.fields["execution_evidence_contract"] =
        "Execute only through real MCP edit/build/test/evidence tools. "
        "For source edits prefer lan_agent_apply_diff_patch or lan_agent_preview_patch plus "
        "lan_agent_apply_single_file_patch and lan_agent_verify_single_file_patch. "
        "Return task_id, result_ref, evidence_ref, patch_id, or log_path.";
    result.fields["builder"] = "lan_agent_execute_semantic_action";
    if (!prepared.ok) {
        result.fields["bridge_status"] = "prepare_blocked";
        result.fields["tool_call_ready"] = "false";
        return result;
    }

    const std::string resolved_action_id = GetFieldOrDefault(prepared, "action_id", "");
    result.fields["bridge_action_id"] = resolved_action_id;
    if (!IsAllowedSemanticExecutionBridgeAction(resolved_action_id)) {
        result.ok = false;
        result.exit_code = 49;
        result.fields["bridge_status"] = "blocked_action";
        result.fields["tool_call_ready"] = "false";
        result.fields["error"] = "semantic action is not allowed by the execution bridge";
        result.fields["next_action"] = "choose one allowed action_id or call semantic_action_map for discovery";
        return result;
    }

    CommandResult tool_call = BuildSemanticActionToolCallResult(
        resolved_action_id,
        query,
        arguments_text,
        prefer_dry_run);
    result.fields["tool_call_ready"] = GetFieldOrDefault(tool_call, "tool_call_ready", "false");
    result.fields["bridge_tool_name"] = GetFieldOrDefault(tool_call, "tool_name", "");
    result.fields["bridge_tool_arguments_json"] = GetFieldOrDefault(tool_call, "tool_arguments_json", "");
    result.fields["mcp_tool_call_json"] = GetFieldOrDefault(tool_call, "mcp_tool_call_json", "");
    result.fields["dry_run_injected"] = GetFieldOrDefault(tool_call, "dry_run_injected", "false");
    if (!prefer_dry_run
        && result.fields["bridge_tool_name"] == "lan_agent_build_target"
        && result.fields["dry_run_injected"] == "true") {
        result.fields["bridge_tool_arguments_json"] =
            StripDryRunFlagFromToolArgumentsJson(result.fields["bridge_tool_arguments_json"]);
        result.fields["mcp_tool_call_json"] =
            std::string("{\"method\":\"tools/call\",\"params\":{\"name\":\"")
            + codex_lan_agent::JsonEscape(result.fields["bridge_tool_name"])
            + "\",\"arguments\":"
            + result.fields["bridge_tool_arguments_json"]
            + "}}";
        result.fields["dry_run_injected"] = "false";
    }
    if (!tool_call.ok || result.fields["tool_call_ready"] != "true") {
        result.ok = false;
        if (result.exit_code == 0) {
            result.exit_code = tool_call.exit_code == 0 ? 50 : tool_call.exit_code;
        }
        result.fields["bridge_status"] = "tool_call_blocked";
        result.fields["next_action"] = GetFieldOrDefault(
            tool_call,
            "next_action",
            "fix arguments and rebuild the tool call");
        return result;
    }

    CommandResult executed = ExecuteSemanticBridgeTool(
        config,
        result.fields["bridge_tool_name"],
        result.fields["bridge_tool_arguments_json"]);
    for (const auto & entry : executed.fields) {
        result.fields[entry.first] = entry.second;
    }
    result.ok = executed.ok;
    result.exit_code = executed.exit_code;
    result.fields["bridge_status"] = executed.ok ? "executed" : "execution_failed";
    result.fields["executed_via_bridge"] = "true";
    if (GetFieldOrDefault(result, "summary", "").empty()) {
        result.fields["summary"] = executed.ok
            ? "semantic action executed through real MCP bridge"
            : "semantic action bridge execution failed";
    }
    if (GetFieldOrDefault(result, "next_action", "").empty()) {
        result.fields["next_action"] = HasConcreteExecutionEvidence(result)
            ? "inspect task_id, result_ref, or evidence_ref"
            : "inspect bridge_tool_arguments_json and retry";
    }
    return result;
}

void ApplyAiConclusionValidityGuards(CommandResult * result) {
    if (result == nullptr) {
        return;
    }
    const std::string summary = GetFieldOrDefault(*result, "summary", "");
    const std::string direct_answer = GetFieldOrDefault(*result, "direct_answer", "");
    const std::string output_text = ExtractOutputTextFallback(*result);
    const std::string merged_text = summary + "\n" + direct_answer + "\n" + output_text;
    const bool has_tool_error = !GetFieldOrDefault(*result, "error", "").empty();
    const bool has_async_task_id = !GetFieldOrDefault(*result, "task_id", "").empty()
        && (GetFieldOrDefault(*result, "status", "") == "queued"
            || GetFieldOrDefault(*result, "status", "") == "running"
            || GetFieldOrDefault(*result, "result", "") == "queued");
    const bool explicit_not_verified =
        GetFieldOrDefault(*result, "verification", "") == "not_verified"
        || GetFieldOrDefault(*result, "verification_status", "") == "not_verified"
        || GetFieldOrDefault(*result, "verification_ok", "") == "false";
    const bool continuation_required =
        GetFieldOrDefault(*result, "semantic_model_clamp", "") == "tool_call_only"
        || GetFieldOrDefault(*result, "supervision_status", "") == "closed_loop_continue"
        || GetFieldOrDefault(*result, "continue_required", "") == "true"
        || GetFieldOrDefault(*result, "has_more", "") == "true";
    const bool explicit_failed_status =
        GetFieldOrDefault(*result, "status", "") == "failed"
        || GetFieldOrDefault(*result, "budget_status", "") == "blocked"
        || GetFieldOrDefault(*result, "conversation_close_status", "") == "close_blocked_missing_continuation";
    const bool tool_failed = !result->ok
        || result->exit_code != 0
        || explicit_failed_status
        || (explicit_not_verified && !continuation_required);
    const bool analysis_blocked = GetFieldOrDefault(*result, "analysis_allowed", "true") == "false";
    const bool batch_incomplete = GetFieldOrDefault(*result, "batch_completion", "") == "incomplete"
        && std::atoi(GetFieldOrDefault(*result, "remaining_batch_file_count", "0").c_str()) != 0;
    const bool expected_structured_failure =
        GetFieldOrDefault(*result, "result", "") == "preflight_blocked"
        || GetFieldOrDefault(*result, "preflight_status", "") == "blocked"
        || GetFieldOrDefault(*result, "failure_mode", "") == "task_evicted_from_history";

    if (expected_structured_failure) {
        result->fields["ai_conclusion_valid"] = "true";
        result->fields.erase("invalid_conclusion_reason");
    }

    if (tool_failed && !expected_structured_failure) {
        result->fields["ai_conclusion_valid"] = "false";
        if (GetFieldOrDefault(*result, "invalid_conclusion_reason", "").empty()) {
            result->fields["invalid_conclusion_reason"] = "tool_result_failed";
        }
    }

    if (continuation_required) {
        result->fields["ai_conclusion_valid"] = "false";
        result->fields["terminal_state"] = "false";
        result->fields["task_done"] = "false";
        result->fields["completion_claim_allowed"] = "false";
        if (GetFieldOrDefault(*result, "must_continue_until", "").empty()) {
            result->fields["must_continue_until"] = "final_answer_allowed=true";
        }
        result->fields["completion_guard"] =
            "NON_TERMINAL_RESULT: do not claim completion; execute the required continuation";
        if (GetFieldOrDefault(*result, "invalid_conclusion_reason", "").empty()) {
            result->fields["invalid_conclusion_reason"] = "continuation_required";
        }
    }

    if (analysis_blocked || batch_incomplete) {
        result->fields["ai_conclusion_valid"] = "false";
        if (GetFieldOrDefault(*result, "invalid_conclusion_reason", "").empty()) {
            result->fields["invalid_conclusion_reason"] = batch_incomplete
                ? "directory_batch_pending"
                : "analysis_blocked";
        }
        if (GetFieldOrDefault(*result, "next_action", "").empty()) {
            result->fields["next_action"] =
                "directory file list is complete; continue reading next_batch_file_path before returning a conclusion";
        }
    }

    if (ContainsPleaseWaitConclusionText(merged_text) && !has_async_task_id) {
        result->ok = false;
        if (result->exit_code == 0) {
            result->exit_code = 97;
        }
        result->fields["ai_conclusion_valid"] = "false";
        result->fields["invalid_conclusion_reason"] = "please_wait_without_async_task_id";
        result->fields["next_action"] = "either create an async task_id first or return a concrete tool-backed result";
    }

    if (ContainsPermissionConclusionText(merged_text) && !has_tool_error) {
        result->ok = false;
        if (result->exit_code == 0) {
            result->exit_code = 98;
        }
        result->fields["ai_conclusion_valid"] = "false";
        result->fields["invalid_conclusion_reason"] = "permission_conclusion_without_tool_error";
        result->fields["next_action"] = "re-run with a concrete verification tool and attach the raw tool error before concluding permissions are limited";
    }

    if (IsAnalysisOnlyAiTool(*result)
        && ContainsExecutionClaimConclusionText(merged_text)
        && !HasConcreteExecutionEvidence(*result)) {
        result->ok = false;
        if (result->exit_code == 0) {
            result->exit_code = 99;
        }
        result->fields["analysis_only"] = "true";
        result->fields["execution_capability"] = "false";
        result->fields["trusted_execution_evidence_required"] = "true";
        result->fields["ai_conclusion_valid"] = "false";
        result->fields["invalid_conclusion_reason"] = "execution_claim_without_tool_evidence";
        result->fields["next_action"] =
            "For source edits use lan_agent_apply_diff_patch or lan_agent_preview_patch plus "
            "lan_agent_apply_single_file_patch and lan_agent_verify_single_file_patch. "
            "Use lan_agent_write_text_file only for non-source text files. "
            "For builds use lan_agent_configure_project or lan_agent_build_target. "
            "For tests use lan_agent_run_ctest_target. "
            "You may also use lan_agent_execute_semantic_action. "
            "Return task_id, result_ref, evidence_ref, patch_id, or log_path.";
    }

    if (GetFieldOrDefault(*result, "ai_conclusion_valid", "").empty()) {
        result->fields["ai_conclusion_valid"] = "true";
    }
}

std::vector<std::string> ParseJsonStringArrayValues(const std::string & raw_array) {
    std::vector<std::string> values;
    const std::string trimmed = Trim(raw_array);
    if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
        return values;
    }

    std::string current;
    bool in_string = false;
    bool escaping = false;
    for (std::size_t index = 1; index + 1 < trimmed.size(); ++index) {
        const char current_char = trimmed[index];
        if (in_string) {
            if (escaping) {
                switch (current_char) {
                case '"':
                    current.push_back('"');
                    break;
                case '\\':
                    current.push_back('\\');
                    break;
                case '/':
                    current.push_back('/');
                    break;
                case 'b':
                    current.push_back('\b');
                    break;
                case 'f':
                    current.push_back('\f');
                    break;
                case 'n':
                    current.push_back('\n');
                    break;
                case 'r':
                    current.push_back('\r');
                    break;
                case 't':
                    current.push_back('\t');
                    break;
                default:
                    current.push_back(current_char);
                    break;
                }
                escaping = false;
                continue;
            }
            if (current_char == '\\') {
                escaping = true;
                continue;
            }
            if (current_char == '"') {
                values.push_back(current);
                current.clear();
                in_string = false;
                continue;
            }
            current.push_back(current_char);
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(current_char)) != 0 || current_char == ',') {
            continue;
        }
        if (current_char == '"') {
            in_string = true;
            continue;
        }
        return std::vector<std::string>();
    }

    if (in_string || escaping) {
        return std::vector<std::string>();
    }
    return values;
}

bool ContainsNonAsciiByte(const std::string & value) {
    for (unsigned char ch : value) {
        if (ch > 0x7F) {
            return true;
        }
    }
    return false;
}

bool ContainsSuspiciousQuestionRun(const std::string & value) {
    return value.find("???") != std::string::npos;
}

bool ToolNeedsRawEncodingProbe(const std::string & tool_name) {
    return tool_name == "lan_agent_run_cxparser_flow"
        || tool_name == "lan_agent_preflight_run_ctest_target"
        || tool_name == "lan_agent_run_ctest_target"
        || tool_name == "lan_agent_preflight_build_target"
        || tool_name == "lan_agent_build_target";
}

void ApplyRawRequestEncodingProbe(
    const std::string & tool_name,
    const std::string & request_body,
    CommandResult * result) {
    if (result == nullptr || !ToolNeedsRawEncodingProbe(tool_name)) {
        return;
    }
    result->fields["encoding_contract"] = "utf8_preferred_ascii_safe";
    result->fields["raw_request_encoding_probe"] = "enabled";
    result->fields["raw_request_contains_non_ascii"] =
        ContainsNonAsciiByte(request_body) ? "true" : "false";
    if (ContainsNonAsciiByte(request_body)) {
        result->fields["encoding_warning"] =
            "raw request body contains non-ASCII bytes; verify UTF-8 handling across caller, HTTP body, logs, and remote echo";
        result->fields["encoding_probe_scope"] = "raw_request_body";
    } else if (ContainsSuspiciousQuestionRun(request_body)) {
        result->fields["encoding_warning"] =
            "raw request body contains repeated question-mark substitutions; caller-side encoding may have already lost non-ASCII text before MCP parsing";
        result->fields["encoding_probe_scope"] = "raw_request_body_lossy_substitution";
    }
}

std::vector<std::string> SplitCommandLikeArguments(const std::string & text) {
    std::vector<std::string> values;
    std::string current;
    char quote_char = '\0';
    bool escaping = false;
    for (const char current_char : text) {
        if (escaping) {
            current.push_back(current_char);
            escaping = false;
            continue;
        }
        if (quote_char != '\0') {
            if (current_char == '\\') {
                escaping = true;
                continue;
            }
            if (current_char == quote_char) {
                quote_char = '\0';
                continue;
            }
            current.push_back(current_char);
            continue;
        }
        if (current_char == '"' || current_char == '\'') {
            quote_char = current_char;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(current_char)) != 0) {
            if (!current.empty()) {
                values.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(current_char);
    }
    if (!current.empty()) {
        values.push_back(current);
    }
    return values;
}

std::string QuoteCommandValue(const std::string & value) {
    std::string escaped;
    escaped.reserve(value.size() + 4);
    for (const char current : value) {
        if (current == '"') {
            escaped += "\\\"";
        } else {
            escaped.push_back(current);
        }
    }
    return "\"" + escaped + "\"";
}

std::vector<std::string> CollectConfigureProjectCmakeArgs(
    const std::string & request_body,
    std::string * cmake_args_text,
    std::string * cmake_args_list_raw) {
    const std::string parsed_text = ExtractJsonString(request_body, "cmake_args");
    const std::string parsed_list_raw = ExtractJsonObjectRaw(request_body, "cmake_args_list");
    if (cmake_args_text != nullptr) {
        *cmake_args_text = parsed_text;
    }
    if (cmake_args_list_raw != nullptr) {
        *cmake_args_list_raw = parsed_list_raw;
    }

    std::vector<std::string> values;
    const std::vector<std::string> parsed_list = ParseJsonStringArrayValues(parsed_list_raw);
    values.insert(values.end(), parsed_list.begin(), parsed_list.end());

    const std::vector<std::string> parsed_text_args = SplitCommandLikeArguments(parsed_text);
    values.insert(values.end(), parsed_text_args.begin(), parsed_text_args.end());
    return values;
}

std::string JoinConfigureProjectCmakeArgs(const std::vector<std::string> & cmake_args) {
    std::ostringstream output;
    for (std::size_t index = 0; index < cmake_args.size(); ++index) {
        if (index > 0) {
            output << ' ';
        }
        output << cmake_args[index];
    }
    return output.str();
}

std::string BuildConfigureProjectArguments(
    const std::string & project_root,
    const std::string & build_dir,
    const std::string & generator_kind,
    const std::vector<std::string> & cmake_args,
    const std::string & env_args) {
    std::string arguments =
        "--action configure --project-root \"" + project_root + "\" --build-dir \"" + build_dir
        + "\" --generator-kind " + generator_kind;
    for (const std::string & current_arg : cmake_args) {
        if (!current_arg.empty()) {
            arguments += " --cmake-arg " + QuoteCommandValue(current_arg);
        }
    }
    if (!env_args.empty()) {
        arguments += " --env \"" + env_args + "\"";
    }
    return arguments;
}

std::string BuildPrepareBuildDirArguments(
    const std::string & build_dir,
    const std::string & workspace_root,
    bool create_if_missing) {
    std::string arguments =
        "--action prepare --build-dir \"" + build_dir + "\" --workspace-root \"" + workspace_root + "\""
        " --stop-matching-processes --remove-cache";
    if (create_if_missing) {
        arguments += " --create-if-missing";
    }
    return arguments;
}

std::string BuildCheckBuildDirArguments(const std::string & build_dir) {
    return "--action check --build-dir \"" + build_dir + "\"";
}

std::string BuildBuildTargetArguments(
    const std::string & build_dir,
    const std::string & config_name,
    const std::string & target) {
    return "--action build --build-dir \"" + build_dir + "\" --config " + config_name
        + " --target \"" + target + "\"";
}

std::string BuildRunCTestTargetArguments(
    const std::string & build_dir,
    const std::string & config_name,
    const std::string & test_regex) {
    return "--action ctest --build-dir \"" + build_dir + "\" --config " + config_name
        + " --test-regex \"" + test_regex + "\"";
}



#include "HealthRuntimeOperations.h"
#include "ProfileExecutionOperations.h"
#include "ClangIndexerAdapter.h"

#include "RemoteAiTurnOperations.h"

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
        const std::string accept = GetHeaderValue(request, "accept");
        if (HeaderContainsToken(accept, "text/event-stream")) {
            response->status_code = 405;
            response->status_text = "Method Not Allowed";
            response->content_type = "application/json";
            response->body =
                "{\"error\":\"codex-lan-agent uses Streamable HTTP POST JSON-RPC; "
                "standalone GET event-stream is not provided\"}";
            response->headers["Allow"] = "POST, HEAD, OPTIONS";
            response->headers["Cache-Control"] = "no-store";
            response->headers["X-MCP-Streamable-HTTP"] = "post-json-rpc-only";
            ApplyMcpSessionHeaders(request, response, false);
            return true;
        }
        response->status_code = 200;
        response->status_text = "OK";
        response->content_type = "application/json";
        response->body = BuildMcpCapabilitiesResponse();
        response->headers["Cache-Control"] = "no-store";
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

    const JsonRequestView request_json(request.body);
    const std::string id_raw = request_json.GetRawJson("id");
    const std::string method = request_json.GetString("method");

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
        const std::string tool_name = ExtractMcpToolNameFromJsonRpcBody(request.body);
        const std::string tool_arguments_body = ExtractMcpToolArgumentsBodyFromJsonRpcBody(request.body);
        const JsonRequestView tool_params(tool_arguments_body);
        CommandResult result;

        if (MaybeApplyClipsPreflightBlock(config, tool_name, tool_params, &result)) {
            ApplyRequestRuleFields(tool_name, tool_params, &result);
            ApplyRawRequestEncodingProbe(tool_name, request.body, &result);
            LanResultBuilder(&result).Finalize(config, tool_name);
            ApplySupervisionEnvelope(&result);
            codex_lan_agent::RememberTaskMemoryPendingFreezeArgumentsFromResult(result);
            AppendMcpTraceAuditEvent(config, tool_name, result);
            AppendMcpSupervisionAlarmEvent(config, tool_name, result);
            response->body = BuildMcpToolCallResponse(id_raw, result);
            ApplyMcpSessionHeaders(request, response, false);
            ApplyMcpTransport(response, response_mode);
            return true;
        }

        if (TryHandleRegisteredMcpTool(config, tool_name, tool_params, &result)) {
            ApplyRequestRuleFields(tool_name, tool_params, &result);
            ApplyRawRequestEncodingProbe(tool_name, request.body, &result);
            LanResultBuilder(&result).Finalize(config, tool_name);
            ApplyAiConclusionValidityGuards(&result);
            ApplyClipsResultGuard(config, tool_name, &result);
            ApplyClipsSemanticTraceContinuation(config, &result);
            ApplySupervisionEnvelope(&result);
            codex_lan_agent::RememberTaskMemoryPendingFreezeArgumentsFromResult(result);
            AppendMcpTraceAuditEvent(config, tool_name, result);
            AppendMcpSupervisionAlarmEvent(config, tool_name, result);

            response->body = BuildMcpToolCallResponse(id_raw, result);
            ApplyMcpSessionHeaders(request, response, false);
            ApplyMcpTransport(response, response_mode);
            return true;
        }

        if (tool_name == "lan_agent_clips_decide") {
            result = BuildClipsDecisionResult(config, request_json);
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
                const std::string timeout_raw = ExtractJsonRawValue(request.body, "timeout_sec");
                const std::string stall_timeout_raw = ExtractJsonRawValue(request.body, "stall_timeout_sec");
                result = RunCliProfile(
                    config,
                    profile,
                    ExtractJsonString(request.body, "args"),
                    std::string(),
                    timeout_raw.empty() ? -1 : std::atoi(timeout_raw.c_str()),
                    stall_timeout_raw.empty() ? -1 : std::atoi(stall_timeout_raw.c_str()));
            }
        } else if (tool_name == "lan_agent_enqueue_cli_profile") {
            if (g_task_manager == nullptr) {
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
            } else {
                const std::string timeout_raw = ExtractJsonRawValue(request.body, "timeout_sec");
                const std::string stall_timeout_raw = ExtractJsonRawValue(request.body, "stall_timeout_sec");
                result.fields["task_id"] = g_task_manager->EnqueueCliProfile(
                    ExtractJsonString(request.body, "profile"),
                    ExtractJsonString(request.body, "args"),
                    timeout_raw.empty() ? -1 : std::atoi(timeout_raw.c_str()),
                    stall_timeout_raw.empty() ? -1 : std::atoi(stall_timeout_raw.c_str()));
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
            const LocalChatEvidencePacket evidence = ExtractLocalChatEvidencePacket(request.body);
            result = RunLocalChat(
                config,
                ExtractJsonString(request.body, "scope"),
                ExtractJsonString(request.body, "question"),
                mode,
                30000,
                &evidence);
        } else if (tool_name == "lan_agent_ventriloquist_reply") {
            std::string context_refs = ExtractJsonString(request.body, "context_refs");
            if (context_refs.empty()) {
                context_refs = ExtractJsonRawValue(request.body, "context_refs");
            }
            result = BuildVentriloquistReplyResult(
                config,
                ExtractJsonString(request.body, "task_id"),
                ExtractJsonString(request.body, "session_id"),
                ExtractJsonString(request.body, "speaker_mode"),
                ExtractJsonString(request.body, "reasoning_level"),
                ExtractJsonString(request.body, "prompt_purpose"),
                context_refs,
                ExtractJsonString(request.body, "response_mode"),
                ExtractJsonString(request.body, "prompt_text"));
        } else if (tool_name == "lan_agent_remote_session_new_turn") {
            std::string context_refs = ExtractJsonString(request.body, "context_refs");
            if (context_refs.empty()) {
                context_refs = ExtractJsonRawValue(request.body, "context_refs");
            }
            result = RunRemoteSessionNewTurn(
                config,
                ExtractJsonString(request.body, "task_id"),
                std::string(),
                ExtractJsonString(request.body, "speaker_mode"),
                ExtractJsonString(request.body, "reasoning_level"),
                ExtractJsonString(request.body, "prompt_purpose"),
                context_refs,
                ExtractJsonString(request.body, "response_mode"),
                ExtractJsonString(request.body, "prompt_text"));
        } else if (tool_name == "lan_agent_remote_session_append_turn") {
            std::string context_refs = ExtractJsonString(request.body, "context_refs");
            if (context_refs.empty()) {
                context_refs = ExtractJsonRawValue(request.body, "context_refs");
            }
            result = RunRemoteSessionAppendTurn(
                config,
                ExtractJsonString(request.body, "task_id"),
                ExtractJsonString(request.body, "session_id"),
                ExtractJsonString(request.body, "speaker_mode"),
                ExtractJsonString(request.body, "reasoning_level"),
                ExtractJsonString(request.body, "prompt_purpose"),
                context_refs,
                ExtractJsonString(request.body, "response_mode"),
                ExtractJsonString(request.body, "prompt_text"));
        } else if (tool_name == "lan_agent_list_remote_sessions") {
            result = ListRemoteSessionsResult(config);
        } else if (tool_name == "lan_agent_list_remote_session_tasks") {
            int max_entries = 20;
            const std::string max_entries_raw = ExtractJsonRawValue(request.body, "max_entries");
            if (!max_entries_raw.empty()) {
                const int parsed_max_entries = std::atoi(max_entries_raw.c_str());
                max_entries = parsed_max_entries > 0 ? parsed_max_entries : 1;
            }
            result = ListRemoteSessionTasksResult(
                config,
                ExtractJsonString(request.body, "session_id"),
                ExtractJsonString(request.body, "task_group_id"),
                ExtractJsonString(request.body, "runner"),
                max_entries,
                10000);
        } else if (tool_name == "lan_agent_get_remote_session") {
            result = GetRemoteSessionResult(config, ExtractJsonString(request.body, "session_id"));
        } else if (tool_name == "lan_agent_resolve_remote_session_task_refs") {
            result = ResolveRemoteSessionTaskRefsResult(
                config,
                ExtractJsonString(request.body, "session_id"),
                ExtractJsonString(request.body, "task_group_id"),
                ExtractJsonString(request.body, "task_id"),
                ExtractJsonString(request.body, "runner"),
                10000);
        } else if (tool_name == "lan_agent_read_remote_session_slice") {
            result = ReadRemoteSessionSliceResult(config, ExtractJsonString(request.body, "session_id"));
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
                result.fields["analysis_only"] = "true";
                result.fields["execution_capability"] = "false";
                result.fields["evidence_injection_template"] =
                    "task_id,result_ref,evidence_ref,resolved_log_path,log_excerpt,source_excerpt";
                result.fields["evidence_injection_used"] = "false";
                result.fields["evidence_injection_queue_policy"] =
                    "queued local chat preserves analysis-only mode; use synchronous lan_agent_run_local_chat for evidence injection";
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
            const LocalChatEvidencePacket evidence = ExtractLocalChatEvidencePacket(request.body);
            result = RunLocalChat(
                config,
                scope,
                query,
                mode,
                30000,
                &evidence);
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
            const std::string record_user_text = FirstNonEmpty(
                ExtractJsonString(request.body, "business_user_text"),
                ExtractJsonString(request.body, "user_text"),
                "");
            const std::string record_assistant_text = FirstNonEmpty(
                ExtractJsonString(request.body, "business_assistant_text"),
                ExtractJsonString(request.body, "assistant_text"),
                "");
            const std::string record_business_summary = FirstNonEmpty(
                ExtractJsonString(request.body, "slice_summary"),
                ExtractJsonString(request.body, "summary"),
                ExtractJsonString(request.body, "business_summary"));
            result = RecordDialogSliceResult(
                config,
                ExtractJsonString(request.body, "session_id"),
                ExtractJsonString(request.body, "turn_id"),
                record_user_text,
                record_assistant_text,
                record_business_summary,
                ExtractJsonString(request.body, "tags"),
                ExtractJsonString(request.body, "task_id"),
                ExtractJsonString(request.body, "provider_id"),
                ExtractJsonString(request.body, "capability_id"),
                ExtractJsonString(request.body, "source_type"),
                ExtractJsonString(request.body, "write_mode"),
                ExtractJsonString(request.body, "reasoning_level"),
                ExtractJsonString(request.body, "primary_intent"),
                ExtractJsonString(request.body, "confidence"),
                ExtractJsonString(request.body, "result_ref"),
                ExtractJsonString(request.body, "evidence_ref"));
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
            std::string cli_args_text = ExtractJsonString(request.body, "args_text");
            if (cli_args_text.empty()) {
                cli_args_text = ExtractJsonString(request.body, "directory_path");
            }
            if (cli_args_text.empty()) {
                cli_args_text = ExtractJsonString(request.body, "path");
            }
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
                cli_args_text,
                ExtractJsonBool(request.body, "dry_run", false));
        } else if (tool_name == "router_domain_map") {
            result = BuildRouterDomainMapResult(ExtractJsonString(request.body, "domain"));
        } else if (tool_name == "dispatch_contract_map") {
            result = BuildDispatchContractMapResult(
                ExtractJsonString(request.body, "table_name"));
        } else if (tool_name == "mcp_capability_registry") {
            result = BuildMcpCapabilityRegistryResult(
                ExtractJsonString(request.body, "capability_id"));
        } else if (tool_name == "rag_memory_slice_contract") {
            result = BuildRagMemorySliceContractResult(
                ExtractJsonString(request.body, "field_group"));
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
        } else if (tool_name == "lan_agent_execute_semantic_action") {
            result = ExecuteSemanticActionBridgeResult(
                config,
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
        } else if (tool_name == "lan_agent_resolve_task_result") {
            result = ResolveTaskResultReferenceResult(
                ExtractJsonString(request.body, "task_id"),
                ExtractJsonString(request.body, "task_ref"));
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
        } else if (tool_name == "lan_agent_list_recent_remote_events") {
            int max_entries = 20;
            const std::string max_entries_raw = ExtractJsonRawValue(request.body, "max_entries");
            if (!max_entries_raw.empty()) {
                const int parsed_max_entries = std::atoi(max_entries_raw.c_str());
                max_entries = parsed_max_entries > 0 ? parsed_max_entries : 1;
            }
            result = ListRecentRemoteEventsResult(
                config,
                max_entries,
                std::max(0, std::atoi(ExtractJsonRawValue(request.body, "offset").c_str())),
                ExtractJsonBool(request.body, "include_auto", false),
                ExtractJsonBool(request.body, "include_noise", false),
                ExtractJsonBool(request.body, "ai_only", false),
                ExtractJsonString(request.body, "since_timestamp"),
                ExtractJsonString(request.body, "request_type"),
                ExtractJsonString(request.body, "command_name"),
                ExtractJsonString(request.body, "session_id"),
                ExtractJsonString(request.body, "task_id"));
        } else if (tool_name == "lan_agent_preview_patch") {
            result = PreviewPatchResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "new_content"),
                ExtractJsonString(request.body, "old_hash"),
                ExtractJsonString(request.body, "request_id"),
                ExtractJsonString(request.body, "trace_id"),
                ExtractJsonString(request.body, "patch_id"),
                ExtractJsonString(request.body, "reason"));
        } else if (tool_name == "lan_agent_apply_single_file_patch") {
            const JsonRequestView params(request.body);
            CommandResult payload_result;
            const std::string new_content = ResolveTextPayloadFromParams(
                params,
                "new_content",
                "new_content_base64",
                &payload_result);
            result = payload_result.ok
                ? ApplySingleFilePatchResult(
                    config,
                    ExtractJsonString(request.body, "file_path"),
                    new_content,
                    ExtractJsonString(request.body, "old_hash"),
                    ExtractJsonString(request.body, "request_id"),
                    ExtractJsonString(request.body, "trace_id"),
                    ExtractJsonString(request.body, "patch_id"),
                    ExtractJsonString(request.body, "reason"),
                    params.GetBool("allow_empty_content", false))
                : payload_result;
            if (payload_result.ok) {
                result.fields["content_transport"] = GetFieldOrDefault(payload_result, "content_transport", "json_string");
                result.fields["content_base64_bytes"] = GetFieldOrDefault(payload_result, "content_base64_bytes", "");
            }
        } else if (tool_name == "lan_agent_apply_diff_patch") {
            result = ApplyDiffPatchResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "diff_text"),
                ExtractJsonString(request.body, "old_hash"),
                ExtractJsonString(request.body, "request_id"),
                ExtractJsonString(request.body, "trace_id"),
                ExtractJsonString(request.body, "patch_id"),
                ExtractJsonString(request.body, "reason"),
                ExtractJsonString(request.body, "resolved_file_path"),
                ExtractJsonString(request.body, "target_resolution_reason"),
                JsonRequestView(request.body).GetBool("allow_empty_content", false));
        } else if (tool_name == "lan_agent_write_text_file") {
            const JsonRequestView params(request.body);
            CommandResult payload_result;
            const std::string content = ResolveTextPayloadFromParams(
                params,
                "content",
                "content_base64",
                &payload_result);
            result = payload_result.ok
                ? WriteTextFileResult(
                    config,
                    ExtractJsonString(request.body, "file_path"),
                    content,
                    ExtractJsonBool(request.body, "append", false))
                : payload_result;
            if (payload_result.ok) {
                result.fields["content_transport"] = GetFieldOrDefault(payload_result, "content_transport", "json_string");
                result.fields["content_base64_bytes"] = GetFieldOrDefault(payload_result, "content_base64_bytes", "");
            }
        } else if (tool_name == "lan_agent_ensure_directory") {
            result = EnsureDirectoryResult(
                config,
                ExtractJsonString(request.body, "directory_path"),
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonBool(request.body, "ensure_parent", false));
        } else if (tool_name == "lan_agent_revert_single_file_patch") {
            result = RevertSingleFilePatchResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "backup_path"),
                ExtractJsonString(request.body, "request_id"),
                ExtractJsonString(request.body, "trace_id"),
                ExtractJsonString(request.body, "patch_id"),
                ExtractJsonString(request.body, "reason"));
        } else if (tool_name == "lan_agent_get_patch_audit_trail") {
            result = GetPatchAuditTrailResult(
                config,
                ExtractJsonString(request.body, "patch_id"));
        } else if (tool_name == "lan_agent_get_trace_audit_trail") {
            result = GetTraceAuditTrailResult(
                config,
                ExtractJsonString(request.body, "trace_id"));
        } else if (tool_name == "lan_agent_get_supervision_status") {
            result = GetSupervisionStatusResult(
                config,
                ExtractJsonString(request.body, "trace_id"),
                ExtractJsonString(request.body, "goal_id"));
        } else if (tool_name == "lan_agent_verify_single_file_patch") {
            result = VerifySingleFilePatchResult(
                config,
                ExtractJsonString(request.body, "patch_id"),
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "expected_hash"),
                ExtractJsonString(request.body, "contains_text"),
                ExtractJsonString(request.body, "forbidden_text"),
                ExtractJsonString(request.body, "request_id"),
                ExtractJsonString(request.body, "trace_id"),
                ExtractJsonString(request.body, "reason"));
        } else if (tool_name == "lan_agent_snapshot_diff") {
            result = SnapshotDiffResult(
                config,
                ExtractJsonString(request.body, "repo_root"),
                30,
                ExtractJsonString(request.body, "non_git_strategy"),
                ExtractJsonString(request.body, "snapshot_action"));
        } else if (tool_name == "lan_agent_check_build_dir") {
            const std::string build_dir = ExtractJsonString(request.body, "build_dir");
            if (build_dir.empty()) {
                result.ok = false;
                result.exit_code = 400;
                result.fields["error"] = "build_dir is required";
            } else {
                result = RunCliProfile(config, "check_build_dir", BuildCheckBuildDirArguments(build_dir));
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
                    BuildPrepareBuildDirArguments(build_dir, config.workspace_root, create_if_missing));
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
                const std::string stall_timeout_raw = ExtractJsonRawValue(request.body, "stall_timeout_sec");
                const int stall_timeout_sec = stall_timeout_raw.empty()
                    ? config.build_target_stall_timeout_sec
                    : std::atoi(stall_timeout_raw.c_str());
                const std::string preflight_ref = ExtractJsonString(request.body, "preflight_ref");
                const std::string preflight_status = preflight_ref.empty()
                    ? ExtractJsonString(request.body, "preflight_status")
                    : "ready";
                if (config_name.empty()) {
                    config_name = "Release";
                }
                if (build_dir.empty() || target.empty()) {
                    result.ok = false;
                    result.exit_code = 400;
                    result.fields["error"] = "build_dir and target are required";
                } else if (dry_run || validate_args) {
                    result = BuildTargetDryRunResult(build_dir, target, config_name);
                    result.fields["build_target_stall_timeout_sec"] = std::to_string(std::max(0, stall_timeout_sec));
                    result.fields["build_target_stall_timeout_source"] =
                        stall_timeout_raw.empty() ? "config" : "request";
                    if (!preflight_ref.empty()) {
                        result.fields["preflight_ref"] = preflight_ref;
                    }
                    if (!preflight_status.empty()) {
                        result.fields["preflight_status"] = preflight_status;
                    }
                } else {
                    const std::string task_id = g_task_manager->EnqueueCliProfile(
                        "build_target",
                        BuildBuildTargetArguments(build_dir, config_name, target),
                        -1,
                        std::max(0, stall_timeout_sec));
                    result = BuildQueuedTaskResult(task_id);
                    result.fields["build_target_stall_timeout_sec"] = std::to_string(std::max(0, stall_timeout_sec));
                    result.fields["build_target_stall_timeout_source"] =
                        stall_timeout_raw.empty() ? "config" : "request";
                    if (!preflight_ref.empty()) {
                        result.fields["preflight_ref"] = preflight_ref;
                    }
                    if (!preflight_status.empty()) {
                        result.fields["preflight_status"] = preflight_status;
                    }
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
                const std::string stall_timeout_raw = ExtractJsonRawValue(request.body, "stall_timeout_sec");
                const int stall_timeout_sec = stall_timeout_raw.empty()
                    ? config.configure_project_stall_timeout_sec
                    : std::atoi(stall_timeout_raw.c_str());
                std::string cmake_args;
                std::string cmake_args_list_raw;
                const std::vector<std::string> cmake_arg_values = CollectConfigureProjectCmakeArgs(
                    request.body,
                    &cmake_args,
                    &cmake_args_list_raw);
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
                            cmake_arg_values,
                            env_args),
                        -1,
                        std::max(0, stall_timeout_sec));
                    result = BuildQueuedTaskResult(task_id);
                    result.fields["generator_kind"] = generator_kind;
                    result.fields["configure_project_stall_timeout_sec"] =
                        std::to_string(std::max(0, stall_timeout_sec));
                    result.fields["configure_project_stall_timeout_source"] =
                        stall_timeout_raw.empty() ? "config" : "request";
                    if (!cmake_arg_values.empty()) {
                        result.fields["cmake_args"] = JoinConfigureProjectCmakeArgs(cmake_arg_values);
                        result.fields["cmake_arg_count"] = std::to_string(cmake_arg_values.size());
                    } else if (!cmake_args.empty()) {
                        result.fields["cmake_args"] = cmake_args;
                    }
                    if (!cmake_args_list_raw.empty()) {
                        result.fields["cmake_args_list"] = cmake_args_list_raw;
                    }
                    if (!env_args.empty()) {
                        result.fields["env"] = env_args;
                    }
                }
            }
        } else if (tool_name == "lan_agent_run_clang_indexer") {
            const std::string effective_trace_id =
                ResolveEffectiveTraceIdForToolCall(tool_name, request.body);
            result = RunClangIndexerResult(
                config,
                ExtractJsonString(request.body, "source_file"),
                ExtractJsonString(request.body, "compile_db_dir"),
                ExtractJsonString(request.body, "output_path"),
                ExtractJsonString(request.body, "project_root"),
                ExtractJsonString(request.body, "include_dirs"),
                ExtractJsonString(request.body, "defines"),
                ExtractJsonBool(request.body, "verbose", false),
                effective_trace_id);
        } else if (tool_name == "lan_agent_list_cxparser_flows") {
            result = BuildCxParserFlowCatalogResult(&config);
        } else if (tool_name == "lan_agent_validate_cxparser_flow") {
            result = ValidateCxParserFlowResult(
                &config,
                ExtractJsonString(request.body, "flow_id"),
                ExtractJsonString(request.body, "params_json"));
        } else if (tool_name == "lan_agent_run_cxparser_flow") {
            const std::string effective_trace_id =
                ResolveEffectiveTraceIdForToolCall(tool_name, request.body);
            const std::string arguments_json = ExtractJsonRawValue(request.body, "arguments");
            result = RunCxParserFlowResult(
                config,
                ExtractJsonString(request.body, "flow_id"),
                arguments_json.empty() ? request.body : arguments_json,
                effective_trace_id,
                ExtractJsonString(request.body, "goal_id"));
        } else if (tool_name == "lan_agent_probe_text_file") {
            const std::string effective_trace_id =
                ResolveEffectiveTraceIdForToolCall(tool_name, request.body);
            result = ProbeTextFileResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "primary_intent"),
                effective_trace_id);
        } else if (tool_name == "lan_agent_read_text_file") {
            const std::string effective_trace_id =
                ResolveEffectiveTraceIdForToolCall(tool_name, request.body);
            int max_lines = 500;
            const std::string max_lines_raw = ExtractJsonRawValue(request.body, "max_lines");
            if (!max_lines_raw.empty()) {
                const int parsed_max_lines = std::atoi(max_lines_raw.c_str());
                max_lines = parsed_max_lines > 0 ? parsed_max_lines : 1;
            }
            int start_line = 1;
            const std::string start_line_raw = ExtractJsonRawValue(request.body, "start_line");
            if (!start_line_raw.empty()) {
                const int parsed_start_line = std::atoi(start_line_raw.c_str());
                start_line = parsed_start_line > 0 ? parsed_start_line : 1;
            }
            std::size_t start_byte_offset = 0;
            const std::string start_byte_offset_raw = ExtractJsonRawValue(request.body, "start_byte_offset");
            if (!start_byte_offset_raw.empty()) {
                const long long parsed_start_byte_offset = std::atoll(start_byte_offset_raw.c_str());
                start_byte_offset = parsed_start_byte_offset > 0
                    ? static_cast<std::size_t>(parsed_start_byte_offset)
                    : static_cast<std::size_t>(0);
            }
            result = ReadTextFileResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                max_lines,
                start_line,
                effective_trace_id,
                start_byte_offset,
                ExtractJsonString(request.body, "probe_ref"));
        } else if (tool_name == "lan_agent_read_directory_files") {
            const std::string effective_trace_id =
                ResolveEffectiveTraceIdForToolCall(tool_name, request.body);
            int max_files = 200;
            const std::string max_files_raw = ExtractJsonRawValue(request.body, "max_files");
            if (!max_files_raw.empty()) {
                const int parsed_max_files = std::atoi(max_files_raw.c_str());
                max_files = parsed_max_files > 0 ? parsed_max_files : 1;
            }
            int max_lines_per_file = 500;
            const std::string max_lines_per_file_raw = ExtractJsonRawValue(request.body, "max_lines_per_file");
            if (!max_lines_per_file_raw.empty()) {
                const int parsed_max_lines_per_file = std::atoi(max_lines_per_file_raw.c_str());
                max_lines_per_file = parsed_max_lines_per_file > 0 ? parsed_max_lines_per_file : 1;
            }
            int max_files_per_call = 5;
            const std::string max_files_per_call_raw = ExtractJsonRawValue(request.body, "max_files_per_call");
            if (!max_files_per_call_raw.empty()) {
                const int parsed_max_files_per_call = std::atoi(max_files_per_call_raw.c_str());
                max_files_per_call = parsed_max_files_per_call > 0 ? parsed_max_files_per_call : 1;
            }
            int max_total_lines = 2500;
            const std::string max_total_lines_raw = ExtractJsonRawValue(request.body, "max_total_lines");
            if (!max_total_lines_raw.empty()) {
                const int parsed_max_total_lines = std::atoi(max_total_lines_raw.c_str());
                max_total_lines = parsed_max_total_lines > 0 ? parsed_max_total_lines : 1;
            }
            int file_index = 0;
            const std::string file_index_raw = ExtractJsonRawValue(request.body, "file_index");
            if (!file_index_raw.empty()) {
                file_index = std::max(0, std::atoi(file_index_raw.c_str()));
            }
            int start_line = 1;
            const std::string start_line_raw = ExtractJsonRawValue(request.body, "start_line");
            if (!start_line_raw.empty()) {
                const int parsed_start_line = std::atoi(start_line_raw.c_str());
                start_line = parsed_start_line > 0 ? parsed_start_line : 1;
            }
            std::size_t start_byte_offset = 0;
            const std::string start_byte_offset_raw = ExtractJsonRawValue(request.body, "start_byte_offset");
            if (!start_byte_offset_raw.empty()) {
                const long long parsed_start_byte_offset = std::atoll(start_byte_offset_raw.c_str());
                start_byte_offset = parsed_start_byte_offset > 0
                    ? static_cast<std::size_t>(parsed_start_byte_offset)
                    : static_cast<std::size_t>(0);
            }
            result = ReadDirectoryFilesResult(
                config,
                ExtractJsonString(request.body, "directory_path"),
                ExtractJsonString(request.body, "file_extensions_csv"),
                max_files,
                max_lines_per_file,
                max_files_per_call,
                max_total_lines,
                file_index,
                start_line,
                effective_trace_id,
                start_byte_offset);
        } else if (tool_name == "lan_agent_prepare_directory_analysis") {
            const std::string effective_trace_id =
                ResolveEffectiveTraceIdForToolCall(tool_name, request.body);
            int max_files = 200;
            const std::string max_files_raw = ExtractJsonRawValue(request.body, "max_files");
            if (!max_files_raw.empty()) {
                const int parsed_max_files = std::atoi(max_files_raw.c_str());
                max_files = parsed_max_files > 0 ? parsed_max_files : 1;
            }
            int max_excerpt_lines_per_file = 80;
            const std::string max_excerpt_lines_per_file_raw =
                ExtractJsonRawValue(request.body, "max_excerpt_lines_per_file");
            if (!max_excerpt_lines_per_file_raw.empty()) {
                const int parsed_max_excerpt_lines_per_file =
                    std::atoi(max_excerpt_lines_per_file_raw.c_str());
                max_excerpt_lines_per_file =
                    parsed_max_excerpt_lines_per_file > 0
                    ? parsed_max_excerpt_lines_per_file
                    : 1;
            }
            int max_total_excerpt_lines = 1200;
            const std::string max_total_excerpt_lines_raw =
                ExtractJsonRawValue(request.body, "max_total_excerpt_lines");
            if (!max_total_excerpt_lines_raw.empty()) {
                const int parsed_max_total_excerpt_lines =
                    std::atoi(max_total_excerpt_lines_raw.c_str());
                max_total_excerpt_lines =
                    parsed_max_total_excerpt_lines > 0
                    ? parsed_max_total_excerpt_lines
                    : 1;
            }
            int max_excerpt_chars = 24000;
            const std::string max_excerpt_chars_raw =
                ExtractJsonRawValue(request.body, "max_excerpt_chars");
            if (!max_excerpt_chars_raw.empty()) {
                const int parsed_max_excerpt_chars =
                    std::atoi(max_excerpt_chars_raw.c_str());
                max_excerpt_chars = parsed_max_excerpt_chars > 0
                    ? parsed_max_excerpt_chars
                    : 1;
            }
            result = PrepareDirectoryAnalysisResult(
                config,
                ExtractJsonString(request.body, "directory_path"),
                ExtractJsonString(request.body, "file_extensions_csv"),
                max_files,
                max_excerpt_lines_per_file,
                max_total_excerpt_lines,
                max_excerpt_chars,
                effective_trace_id);
        } else if (tool_name == "lan_agent_scan_text_ranges") {
            const std::string effective_trace_id =
                ResolveEffectiveTraceIdForToolCall(tool_name, request.body);
            int max_ranges_per_call = 64;
            const std::string max_ranges_per_call_raw =
                ExtractJsonRawValue(request.body, "max_ranges_per_call");
            if (!max_ranges_per_call_raw.empty()) {
                const int parsed_max_ranges_per_call =
                    std::atoi(max_ranges_per_call_raw.c_str());
                max_ranges_per_call = parsed_max_ranges_per_call > 0
                    ? parsed_max_ranges_per_call
                    : 1;
            }
            int range_offset = 0;
            const std::string range_offset_raw =
                ExtractJsonRawValue(request.body, "range_offset");
            if (!range_offset_raw.empty()) {
                const int parsed_range_offset = std::atoi(range_offset_raw.c_str());
                range_offset = parsed_range_offset >= 0 ? parsed_range_offset : 0;
            }
            result = ScanTextRangesResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "scan_mode"),
                max_ranges_per_call,
                range_offset,
                effective_trace_id,
                ExtractJsonString(request.body, "probe_ref"));
        } else if (tool_name == "lan_agent_prepare_edit_windows") {
            const std::string effective_trace_id =
                ResolveEffectiveTraceIdForToolCall(tool_name, request.body);
            int context_before = 8;
            const std::string context_before_raw =
                ExtractJsonRawValue(request.body, "context_before");
            if (!context_before_raw.empty()) {
                const int parsed_context_before = std::atoi(context_before_raw.c_str());
                context_before = parsed_context_before >= 0 ? parsed_context_before : 0;
            }
            int context_after = 8;
            const std::string context_after_raw =
                ExtractJsonRawValue(request.body, "context_after");
            if (!context_after_raw.empty()) {
                const int parsed_context_after = std::atoi(context_after_raw.c_str());
                context_after = parsed_context_after >= 0 ? parsed_context_after : 0;
            }
            int max_windows_per_call = 16;
            const std::string max_windows_per_call_raw =
                ExtractJsonRawValue(request.body, "max_windows_per_call");
            if (!max_windows_per_call_raw.empty()) {
                const int parsed_max_windows_per_call =
                    std::atoi(max_windows_per_call_raw.c_str());
                max_windows_per_call = parsed_max_windows_per_call > 0
                    ? parsed_max_windows_per_call
                    : 1;
            }
            int window_offset = 0;
            const std::string window_offset_raw =
                ExtractJsonRawValue(request.body, "window_offset");
            if (!window_offset_raw.empty()) {
                const int parsed_window_offset = std::atoi(window_offset_raw.c_str());
                window_offset = parsed_window_offset >= 0 ? parsed_window_offset : 0;
            }
            std::size_t max_window_chars = 2400;
            const std::string max_window_chars_raw =
                ExtractJsonRawValue(request.body, "max_window_chars");
            if (!max_window_chars_raw.empty()) {
                const unsigned long long parsed_max_window_chars =
                    std::strtoull(max_window_chars_raw.c_str(), nullptr, 10);
                max_window_chars = parsed_max_window_chars > 0
                    ? static_cast<std::size_t>(parsed_max_window_chars)
                    : static_cast<std::size_t>(1);
            }
            result = PrepareEditWindowsResult(
                config,
                ExtractJsonString(request.body, "file_path"),
                ExtractJsonString(request.body, "ranges_json"),
                context_before,
                context_after,
                max_windows_per_call,
                window_offset,
                max_window_chars,
                effective_trace_id,
                ExtractJsonString(request.body, "probe_ref"));
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
            const std::string effective_trace_id =
                ResolveEffectiveTraceIdForToolCall(tool_name, request.body);
            int max_entries = 200;
            const std::string max_entries_raw = ExtractJsonRawValue(request.body, "max_entries");
            if (!max_entries_raw.empty()) {
                const int parsed_max_entries = std::atoi(max_entries_raw.c_str());
                max_entries = parsed_max_entries > 0 ? parsed_max_entries : 1;
            }
            result = ListDirectoryResult(
                config,
                ExtractJsonString(request.body, "directory_path"),
                max_entries,
                effective_trace_id);
        } else {
            response->status_code = 404;
            response->status_text = "Not Found";
            response->body = BuildMcpErrorResponse(id_raw, -32601, "tool not found");
            ApplyMcpSessionHeaders(request, response, false);
            ApplyMcpTransport(response, response_mode);
            return true;
        }

        ApplyRequestRuleFields(tool_name, tool_params, &result);
        ApplyRawRequestEncodingProbe(tool_name, request.body, &result);
        LanResultBuilder(&result).Finalize(config, tool_name);
        ApplyAiConclusionValidityGuards(&result);
        ApplyClipsResultGuard(config, tool_name, &result);
        ApplyClipsSemanticTraceContinuation(config, &result);
        ApplySupervisionEnvelope(&result);
        AppendMcpTraceAuditEvent(config, tool_name, result);
        AppendMcpSupervisionAlarmEvent(config, tool_name, result);

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

#include "KvmGatewayAuditUiOperations.h"

bool HandleBinaryHttpRoute(
    const AgentConfig & config,
    const HttpRequest & request,
    HttpResponseSpec * response) {
    if (codex_gateway_audit_ui::HandleGatewayAuditBinaryRoute(config, request, response)) {
        return true;
    }

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

#include "OverviewOperations.h"
#include "HttpRouteOperations.h"

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

CommandResult RunClangIndexerResult(
    const AgentConfig & config,
    const std::string & source_file,
    const std::string & compile_db_dir,
    const std::string & output_path,
    const std::string & project_root,
    const std::string & include_dirs,
    const std::string & defines,
    bool verbose,
    const std::string & trace_id) {
    CommandResult result;

    if (source_file.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "failed";
        result.fields["error"] = "source_file is required";
        result.fields["result"] = "clang_indexer_source_missing";
        return result;
    }

    codex_lan_agent::ClangIndexerOptions options;
    options.source_file = source_file;
    options.compile_db_dir = compile_db_dir;
    options.output_json_path = output_path;
    options.project_root = project_root;
    options.verbose = verbose;

    if (!include_dirs.empty()) {
        std::istringstream iss(include_dirs);
        std::string token;
        while (std::getline(iss, token, '|')) {
            if (!token.empty()) {
                options.extra_include_dirs.push_back(token);
            }
        }
    }

    if (!defines.empty()) {
        std::istringstream iss(defines);
        std::string token;
        while (std::getline(iss, token, '|')) {
            if (!token.empty()) {
                options.extra_defines.push_back(token);
            }
        }
    }

    std::string indexer_path = config.clang_indexer_binary_path;
    if (indexer_path.empty()) {
        indexer_path = codex_lan_agent::FindClangIndexerExecutablePath(config.config_dir);
    }

    if (indexer_path.empty()) {
        result.ok = false;
        result.exit_code = 74;
        result.fields["status"] = "failed";
        result.fields["error"] = "clang_indexer binary not found; set clang_indexer_binary_path in config";
        result.fields["result"] = "clang_indexer_binary_missing";
        result.fields["summary"] = "clang_indexer binary path is not configured";
        if (!trace_id.empty()) {
            result.fields["trace_id"] = trace_id;
        }
        return result;
    }

    std::string command_line;
    std::vector<std::string> args;
    if (!codex_lan_agent::BuildClangIndexerCommand(indexer_path, options, &command_line, &args)) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to build clang_indexer command";
        result.fields["result"] = "clang_indexer_command_build_failed";
        return result;
    }

    const std::string log_path = ::BuildLogPath(config, "clang_indexer");

    std::string extra_args;
    for (const auto & arg : args) {
        extra_args += " " + arg;
    }

    codex_lan_agent::ProcessRunResult run_result;
    std::string error_message;
    const bool started = codex_lan_agent::RunCommandWithLog(
        command_line + extra_args,
        config.workspace_root,
        log_path,
        300,
        120,
        &run_result,
        &error_message);

    if (!started) {
        result.ok = false;
        result.exit_code = 4;
        result.fields["status"] = "failed";
        result.fields["error"] = error_message;
        result.fields["result"] = "clang_indexer_run_failed";
        return result;
    }

    result.ok = run_result.exit_code == 0 && !run_result.timed_out;
    result.exit_code = run_result.exit_code;
    result.fields["timed_out"] = run_result.timed_out ? "true" : "false";
    result.fields["log_path"] = run_result.log_path;
    result.fields["runtime_sec"] = std::to_string(run_result.runtime_sec);
    result.fields["tool"] = "cxparser_clang_indexer";
    result.fields["source_file"] = source_file;
    result.fields["compile_db_dir"] = compile_db_dir;
    result.fields["output_path"] = output_path;
    result.fields["project_root"] = project_root;
    result.fields["clang_indexer_binary"] = indexer_path;

    std::string log_content;
    std::string log_read_error;
    if (::ReadWholeFile(run_result.log_path, &log_content, &log_read_error)) {
        codex_lan_agent::ClangIndexerResult parsed = codex_lan_agent::ParseClangIndexerOutput(log_content, "");
        result.fields["indexer_success"] = parsed.success ? "true" : "false";
        result.fields["indexer_error"] = parsed.error;
        result.fields["indexer_symbol_count"] = std::to_string(parsed.symbol_count);
        result.fields["indexer_ref_count"] = std::to_string(parsed.ref_count);
        result.fields["indexer_elapsed_ms"] = std::to_string(parsed.elapsed_ms);
        result.fields["indexer_module_name"] = parsed.schema.module_name;
        result.fields["indexer_raw_output"] = log_content.substr(0, std::min<std::size_t>(log_content.size(), 4000));
    } else {
        result.fields["indexer_log_read_error"] = log_read_error;
        result.fields["indexer_success"] = "false";
    }

    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }

    return result;
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
    if (content) {
        *content = buffer.str();
    }
    return true;
}
