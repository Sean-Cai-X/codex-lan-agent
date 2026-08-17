#pragma once

#include "CmmToolResults.h"
#include "ClangAstTool.h"
#include "ProcessRunner.h"

#include <filesystem>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <vector>

CommandResult BuildBuildTargetPreflightResult(
    const std::string & build_dir,
    const std::string & target,
    const std::string & config_name);

CommandResult BuildRunCtestPreflightResult(
    const AgentConfig & config,
    const std::string & build_dir,
    const std::string & config_name,
    const std::string & test_regex);
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
    bool dry_run);
bool TryParsePreflightReference(
    const std::string & preflight_ref,
    const std::string & expected_tool_name,
    std::vector<std::string> * parts,
    std::string * checksum);
CommandResult BuildRagClipsRunResult(
    const AgentConfig & config,
    const std::string & query,
    int top_k,
    const std::string & request_id,
    const std::string & trace_id,
    const std::string & query_id,
    int timeout_ms);
CommandResult BuildRagStorageLookupResult(
    const AgentConfig & config,
    const std::string & kind,
    const std::string & id,
    const std::string & slice_id,
    const std::string & trace_id,
    const std::string & query_id,
    const std::string & node_id,
    const std::string & edge_id,
    int limit,
    int timeout_ms);
CommandResult BuildRagStoragePageResult(
    const AgentConfig & config,
    const std::string & kind,
    const std::string & trace_id,
    const std::string & query_id,
    const std::string & test_bucket,
    const std::string & coverage_gap,
    const std::string & result_stage,
    const std::string & coverage_status,
    const std::string & run_kind,
    const std::string & fact_type,
    int limit,
    int offset,
    int timeout_ms);
CommandResult BuildRagReviewObservationResult(
    const AgentConfig & config,
    const std::string & summary,
    const std::string & scenario,
    const std::string & train,
    const std::string & request_id,
    const std::string & trace_id,
    const std::string & query_id,
    const std::string & module,
    const std::string & task_layer,
    const std::string & task_case,
    const std::string & dataset_bridge,
    const std::string & test_bucket,
    const std::string & test_flow,
    const std::string & baseline_objective,
    const std::string & best_objective,
    const std::string & objective_delta,
    const std::string & comparison_status,
    const std::string & comparison_magnitude,
    const std::string & optimization_signal,
    const std::string & risk_axis,
    const std::string & bucket_coverage,
    const std::string & coverage_gap,
    const std::string & coverage_status,
    const std::string & next_review_action,
    const std::string & review_scope,
    const std::string & result_stage,
    const std::string & primary_review_ref,
    const std::string & summary_ref,
    const std::string & compare_ref,
    const std::string & replay_ref,
    const std::string & best_params_ref,
    bool human_review_required,
    const std::string & conclusion_id,
    const std::string & short_conclusion,
    const std::string & why_it_matters,
    const std::string & next_observation,
    const std::string & source_refs,
    const std::string & tags,
    int timeout_ms);
CommandResult RecordDialogSliceResult(
    const AgentConfig & config,
    const std::string & session_id,
    const std::string & turn_id,
    const std::string & user_text,
    const std::string & assistant_text,
    const std::string & business_summary_override,
    const std::string & tags,
    const std::string & task_id,
    const std::string & provider_id,
    const std::string & capability_id,
    const std::string & source_type,
    const std::string & write_mode,
    const std::string & reasoning_level,
    const std::string & primary_intent,
    const std::string & confidence,
    const std::string & result_ref,
    const std::string & evidence_ref);
CommandResult WriteTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & content,
    bool append);
CommandResult WriteWholeTextViaOptfileResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & content,
    bool append,
    const std::string & expected_file_hash);
CommandResult ApplySingleFilePatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & new_content,
    const std::string & old_hash,
    const std::string & request_id,
    const std::string & trace_id,
    const std::string & patch_id,
    const std::string & reason,
    bool allow_empty_content);
CommandResult ApplyDiffPatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & diff_text,
    const std::string & old_hash,
    const std::string & request_id,
    const std::string & trace_id,
    const std::string & patch_id,
    const std::string & reason,
    const std::string & resolved_file_path,
    const std::string & target_resolution_reason,
    bool allow_empty_content);
CommandResult RevertSingleFilePatchResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & backup_path,
    const std::string & request_id,
    const std::string & trace_id,
    const std::string & patch_id,
    const std::string & reason);
CommandResult EnsureDirectoryResult(
    const AgentConfig & config,
    const std::string & directory_path,
    const std::string & file_path,
    bool ensure_parent);
CommandResult ScanTextRangesResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & scan_mode,
    int max_ranges_per_call,
    int range_offset,
    const std::string & trace_id,
    const std::string & probe_ref);
CommandResult DeleteNextTextRangeAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & scan_mode,
    const std::string & primary_intent,
    const std::string & trace_id,
    const std::string & probe_ref);
CommandResult DeleteTextRangeWindowAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & scan_mode,
    int start_line,
    int max_lines,
    const std::string & primary_intent,
    const std::string & trace_id,
    const std::string & probe_ref,
    const std::string & directory_manifest_path,
    int directory_current_file_index,
    int directory_total_code_file_count);
CommandResult PrepareEditWindowsResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & ranges_json,
    int context_before,
    int context_after,
    int max_windows_per_call,
    int window_offset,
    std::size_t max_window_chars,
    const std::string & trace_id,
    const std::string & probe_ref);
CommandResult FindLineMetadataResult(
    const AgentConfig & config,
    const std::string & file_path,
    int line_number,
    bool show_preview);
CommandResult FindContentMatchesResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & anchor_text,
    bool show_preview,
    int fuzzy_threshold);
CommandResult SearchTextResult(
    const AgentConfig & config,
    const std::string & path,
    const std::string & query_text,
    bool recursive,
    bool show_preview,
    int max_matches,
    const std::string & trace_id);
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

CommandResult LocateTextLinesResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & anchor_text,
    bool show_preview,
    int fuzzy_threshold);
CommandResult DeleteLineAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    int line_number,
    const std::string & expected_line_hash,
    const std::string & request_id,
    const std::string & trace_id);
CommandResult DeleteContentAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & anchor_text,
    int occurrence,
    const std::string & expected_anchor_hash,
    const std::string & request_id,
    const std::string & trace_id);
CommandResult InsertAfterAnchorAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & anchor_text,
    int occurrence,
    const std::string & insert_text,
    const std::string & expected_anchor_hash,
    const std::string & request_id,
    const std::string & trace_id);
CommandResult ReplaceLineRangeAtomicResult(
    const AgentConfig & config,
    const std::string & file_path,
    int start_line,
    int end_line,
    const std::string & replacement_text,
    const std::string & expected_range_hash,
    const std::string & request_id,
    const std::string & trace_id);

CommandResult BuildRemoteSessionSemanticCatalogResult(const AgentConfig & config);
CommandResult BuildCxParserFlowCatalogResult(const AgentConfig * config = nullptr);
CommandResult ValidateCxParserFlowResult(
    const AgentConfig * config,
    const std::string & flow_id,
    const std::string & params_json = std::string());
CommandResult RunCxParserFlowResult(
    const AgentConfig & config,
    const std::string & flow_id,
    const std::string & request_body,
    const std::string & trace_id,
    const std::string & goal_id);
CommandResult BuildRemoteSessionOverviewResult(const AgentConfig & config, int timeout_ms);
CommandResult BuildTaskOverviewResult(const AgentConfig & config, int max_entries);
CommandResult BuildEventOverviewResult(
    const AgentConfig & config,
    int max_entries,
    int offset,
    bool include_auto,
    bool include_noise,
    const std::string & command_name_filter,
    const std::string & session_id_filter,
    const std::string & task_id_filter,
    const std::string & since_timestamp);
CommandResult BuildMcpOverviewResult(const AgentConfig & config);
CommandResult BuildRagOverviewResult(const AgentConfig & config);
CommandResult BuildPatchOverviewResult(
    const AgentConfig & config,
    int max_entries,
    int offset,
    const std::string & patch_id_filter,
    const std::string & trace_id_filter,
    const std::string & file_path_filter);
CommandResult BuildBrowserListOverviewResult(
    const AgentConfig & config,
    int task_max_entries,
    int event_max_entries,
    int patch_max_entries);

class LanResultBuilder {
public:
    explicit LanResultBuilder(CommandResult * result) : result_(result) {}

    LanResultBuilder & Error(int exit_code, const std::string & message) {
        if (result_ == nullptr) {
            return *this;
        }
        result_->ok = false;
        result_->exit_code = exit_code;
        result_->fields["error"] = message;
        return *this;
    }

    LanResultBuilder & InvalidVerification(
        const std::string & reason,
        const std::string & summary,
        const std::string & next_action) {
        if (result_ == nullptr) {
            return *this;
        }
        result_->fields["verification"] = "invalid";
        result_->fields["verification_status"] = "invalid";
        result_->fields["verification_ok"] = "false";
        result_->fields["invalid_verification_reason"] = reason;
        if (!summary.empty()) {
            result_->fields["summary"] = summary;
        }
        if (!next_action.empty()) {
            result_->fields["next_action"] = next_action;
        }
        return *this;
    }

    LanResultBuilder & CopyFields(
        const CommandResult & source,
        const std::vector<std::pair<std::string, std::string>> & fields) {
        if (result_ == nullptr) {
            return *this;
        }
        for (const auto & field : fields) {
            result_->fields[field.second] = GetFieldOrDefault(source, field.first, "");
        }
        return *this;
    }

    LanResultBuilder & Finalize(const AgentConfig & config, const std::string & tool_name) {
        FinalizeResultEnvelope(config, tool_name, result_);
        return *this;
    }

private:
    CommandResult * result_;
};

void CopyCtestDiscoveryEnvelope(const CommandResult & discover, CommandResult * result) {
    LanResultBuilder(result).CopyFields(
        discover,
        {
            {"semantic_outcome", "discover_semantic_outcome"},
            {"test_count", "discover_test_count"},
            {"selector_mode", "discover_selector_mode"},
            {"matched_labels", "discover_matched_labels"},
            {"matched_label_count", "discover_matched_label_count"},
            {"resolved_test_regex", "discover_resolved_test_regex"},
            {"result_ref", "discover_result_ref"},
            {"evidence_ref", "discover_evidence_ref"},
            {"log_path", "discover_log_path"}
        });
}

bool CtestDiscoveryBlocksRun(const CommandResult & discover) {
    const std::string outcome = GetFieldOrDefault(discover, "semantic_outcome", "");
    return !discover.ok || outcome == "no_tests_found" || outcome == "build_dir_missing" || outcome == "not_configured";
}

CommandResult BuildInvalidCtestRunResult(CommandResult discover) {
    const std::string outcome = GetFieldOrDefault(discover, "semantic_outcome", "ctest_discovery_failed");
    const bool no_tests = outcome == "no_tests_found";
    LanResultBuilder(&discover).InvalidVerification(
        no_tests ? "ctest_no_tests_found" : outcome,
        no_tests ? "ctest discovery found no registered tests; the queued test run was skipped" : "",
        no_tests
            ? "run configure_project, confirm build_dir, widen test_regex, or register tests before retrying run_ctest_target"
            : GetFieldOrDefault(discover, "next_action", "fix ctest discovery preflight before queueing run_ctest_target"));
    return discover;
}
bool IsStructuredBodyReadCandidate(const std::filesystem::path & normalized_path);
std::string StructuredPayloadFormatForPath(const std::filesystem::path & normalized_path);

using McpToolHandler = std::function<CommandResult(const AgentConfig &, const JsonRequestView &)>;
CommandResult ProbeTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & primary_intent = std::string(),
    const std::string & trace_id = std::string(),
    const std::string & directory_manifest_path = std::string(),
    int directory_current_file_index = 0,
    int directory_total_code_file_count = 0);
CommandResult ReadTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    int max_lines,
    int start_line,
    const std::string & trace_id,
    std::size_t start_byte_offset,
    const std::string & probe_ref);
CommandResult TailTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    int max_lines);
CommandResult ListDirectoryResult(
    const AgentConfig & config,
    const std::string & directory_path,
    int max_entries,
    const std::string & trace_id,
    const std::string & primary_intent = std::string());
CommandResult ReadDirectoryFilesResult(
    const AgentConfig & config,
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files,
    int max_lines_per_file,
    int max_files_per_call,
    int max_total_lines,
    int file_index,
    int start_line,
    const std::string & trace_id,
    std::size_t start_byte_offset);

std::string ExtractMcpRouteWindowsPathFromText(const std::string & text) {
    for (std::size_t i = 0; i + 2 < text.size(); ++i) {
        const unsigned char drive = static_cast<unsigned char>(text[i]);
        if (!std::isalpha(drive) || text[i + 1] != ':' || (text[i + 2] != '\\' && text[i + 2] != '/')) {
            continue;
        }
        std::size_t end = i + 3;
        while (end < text.size()) {
            const unsigned char ch = static_cast<unsigned char>(text[end]);
            if (ch >= 128
                || std::iscntrl(ch)
                || ch == '?'
                || ch == '"'
                || ch == '\''
                || ch == '<'
                || ch == '>'
                || ch == '|') {
                break;
            }
            if (std::isspace(ch)) {
                break;
            }
            ++end;
        }
        std::string candidate = text.substr(i, end - i);
        const std::string lowered_candidate = ToLowerAscii(candidate);
        std::size_t extension_pos_best = std::string::npos;
        std::size_t extension_end = std::string::npos;
        const std::vector<std::string> source_extensions = {
            ".cpp", ".cxx", ".cc", ".c", ".hpp", ".hh", ".h", ".ipp", ".txt", ".md", ".json", ".clp"
        };
        for (const std::string & extension : source_extensions) {
            const std::size_t extension_pos = lowered_candidate.find(extension);
            if (extension_pos == std::string::npos) {
                continue;
            }
            const std::size_t candidate_end = extension_pos + extension.size();
            if (extension_pos_best == std::string::npos
                || extension_pos < extension_pos_best
                || (extension_pos == extension_pos_best && candidate_end > extension_end)) {
                extension_pos_best = extension_pos;
                extension_end = candidate_end;
            }
        }
        if (extension_end != std::string::npos) {
            candidate = candidate.substr(0, extension_end);
        }
        while (!candidate.empty()) {
            const char tail = candidate.back();
            if (tail == '.' || tail == ',' || tail == ';' || tail == ':' || tail == ')' || tail == ']' || tail == '}') {
                candidate.pop_back();
                continue;
            }
            break;
        }
        if (!candidate.empty()) {
            return candidate;
        }
    }
    return {};
}

std::string InferMcpRoutePrimaryIntent(
    const std::string & primary_intent,
    const std::string & request_text) {
    const std::string normalized =
        codex_lan_agent::InferIntentBySemanticLexicon(primary_intent, request_text);
    if (normalized == "comment_cleanup" || normalized == "code_format" || normalized == "source_edit") {
        return normalized;
    }
    const std::string lowered_intent = ToLowerAscii(Trim(normalized));
    const std::string lowered_request = ToLowerAscii(request_text);
    const bool mentions_source_file =
        lowered_request.find(".cpp") != std::string::npos
        || lowered_request.find(".cxx") != std::string::npos
        || lowered_request.find(".cc") != std::string::npos
        || lowered_request.find(".c") != std::string::npos
        || lowered_request.find(".hpp") != std::string::npos
        || lowered_request.find(".hh") != std::string::npos
        || lowered_request.find(".h") != std::string::npos;
    bool has_non_ascii_or_lossy_action_text = false;
    for (const unsigned char ch : request_text) {
        if (ch > 0x7F || ch == '?') {
            has_non_ascii_or_lossy_action_text = true;
            break;
        }
    }
    const bool mentions_comment =
        lowered_intent.find("comment") != std::string::npos
        || lowered_request.find("comment") != std::string::npos
        || (lowered_intent.find("cleanup") != std::string::npos
            && mentions_source_file)
        || request_text.find("\xE6\xB3\xA8") != std::string::npos
        || request_text.find("\xE6\xB3\xA8\xE9\x87\x8A") != std::string::npos
        || request_text.find("注释") != std::string::npos
        || lowered_request.find("\\u6ce8") != std::string::npos
        || lowered_request.find("\\u91ca") != std::string::npos
        || lowered_request.find("\\u6ce8\\u91ca") != std::string::npos;
    if (mentions_comment) {
        return "comment_cleanup";
    }
    const bool mentions_format =
        lowered_intent == "format"
        || lowered_intent == "format_code"
        || lowered_intent == "code_format"
        || lowered_intent.find("format code") != std::string::npos
        || lowered_intent.find("code format") != std::string::npos
        || lowered_intent.find("formatting") != std::string::npos
        || lowered_intent.find("newline") != std::string::npos
        || lowered_intent.find("whitespace") != std::string::npos
        || lowered_request.find("format") != std::string::npos
        || lowered_request.find("newline") != std::string::npos
        || lowered_request.find("whitespace") != std::string::npos
        || request_text.find("回车") != std::string::npos
        || request_text.find("换行") != std::string::npos
        || request_text.find("空行") != std::string::npos;
    if (mentions_format) {
        return "code_format";
    }
    return normalized;
}

std::string ExtractSearchQueryTextFromRequest(const std::string & request_text) {
    std::string text = Trim(request_text);
    const std::string lowered = ToLowerAscii(text);
    const std::vector<std::string> prefixes = {
        "search for ",
        "search text ",
        "find text ",
        "grep for ",
        "look for "
    };
    for (const std::string & prefix : prefixes) {
        const std::size_t pos = lowered.find(prefix);
        if (pos != std::string::npos) {
            text = Trim(text.substr(pos + prefix.size()));
            break;
        }
    }
    while (text.size() >= 2 && ((text.front() == '"' && text.back() == '"') || (text.front() == '\'' && text.back() == '\''))) {
        text = Trim(text.substr(1, text.size() - 2));
    }
    return text;
}

bool McpRouteRequestMentionsDirectoryListing(const std::string & request_text) {
    const std::string lowered = ToLowerAscii(request_text);
    return lowered.find("list") != std::string::npos
        || lowered.find("directory") != std::string::npos
        || lowered.find("folder") != std::string::npos
        || lowered.find("all files") != std::string::npos
        || request_text.find("列表") != std::string::npos
        || request_text.find("目录") != std::string::npos
        || request_text.find("文件夹") != std::string::npos
        || request_text.find("所有文件") != std::string::npos;
}

bool IsMcpGatewayReadOnlyTool(const std::string & tool_name) {
    return tool_name == "lan_agent_probe_text_file"
        || tool_name == "lan_agent_read_text_file"
        || tool_name == "lan_agent_list_directory"
        || tool_name == "lan_agent_read_directory_files"
        || tool_name == "lan_agent_find_line_metadata"
        || tool_name == "lan_agent_find_content_matches"
        || tool_name == "lan_agent_locate_text_lines"
        || tool_name == "lan_agent_search_text"
        || tool_name == "lan_agent_scan_text_ranges"
        || tool_name == "lan_agent_prepare_edit_windows";
}


bool McpRoutePathIsDirectory(const std::string & file_path) {
    if (Trim(file_path).empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_directory(std::filesystem::path(file_path), ec) && !ec;
}

struct McpBoundaryCase {
    std::string case_id;
    std::string purpose;
    std::string params_json;
    std::string expected_decision;
    std::string expected_reason_code;
};

std::string McpBoundaryJsonStringField(const std::string & key, const std::string & value) {
    return "\"" + codex_lan_agent::JsonEscape(key) + "\":\"" + codex_lan_agent::JsonEscape(value) + "\"";
}

std::vector<McpBoundaryCase> BuildDefaultMcpBoundaryCases() {
    return {
        {
            "route_no_tool_resolved",
            "Gateway route failed to resolve an executable tool; final answer must be blocked.",
            "{\"decision_domain\":\"mcp_result_guard\",\"tool_name\":\"lan_agent_mcp_route\","
            "\"tool_use_decision\":\"no_tool_resolved\",\"result_hash\":\"case-route-no-tool\","
            "\"schema_version\":\"mcp_result_schema_v1\"}",
            "route",
            "route_no_tool_resolved_not_terminal"
        },
        {
            "non_terminal_completion_gate",
            "A non-terminal result with completion_claim_allowed=false cannot enter assistant text.",
            "{\"decision_domain\":\"mcp_result_guard\",\"tool_name\":\"lan_agent_mcp_route\","
            "\"terminal_state\":\"false\",\"completion_claim_allowed\":\"false\","
            "\"result_hash\":\"case-non-terminal\",\"schema_version\":\"mcp_result_schema_v1\"}",
            "route",
            "non_terminal_result_forbids_final_answer"
        },
        {
            "delete_has_more",
            "Window deletion with has_more=true must continue through the delete loop.",
            "{\"decision_domain\":\"mcp_result_guard\",\"tool_name\":\"lan_agent_delete_text_range_window_atomic\","
            "\"has_more\":\"true\",\"result_hash\":\"case-delete-has-more\","
            "\"schema_version\":\"mcp_result_schema_v1\"}",
            "route",
            "text_range_delete_incomplete"
        },
        {
            "directory_remaining_forbid_terminal",
            "Directory-scope cleanup cannot be terminal while remaining files are recorded.",
            "{\"decision_domain\":\"mcp_result_guard\",\"tool_name\":\"lan_agent_delete_text_range_window_atomic\","
            "\"directory_scope_active\":\"true\",\"terminal_state\":\"true\","
            "\"directory_remaining_code_file_count\":\"2\",\"result_hash\":\"case-directory-remaining\","
            "\"schema_version\":\"mcp_result_schema_v1\"}",
            "route",
            "directory_scope_remaining_files_forbid_terminal"
        },
        {
            "declared_next_call_requires_continuation",
            "A result that declares next_call_json must not be summarized before that continuation runs.",
            "{\"decision_domain\":\"mcp_result_guard\",\"tool_name\":\"lan_agent_probe_text_file\","
            "\"next_call_json\":\"{\\\"name\\\":\\\"lan_agent_delete_text_range_window_atomic\\\",\\\"arguments\\\":{}}\","
            "\"result_hash\":\"case-next-call\",\"schema_version\":\"mcp_result_schema_v1\"}",
            "route",
            "declared_next_call_requires_continuation"
        },
        {
            "final_answer_disallowed",
            "Any result with final_answer_allowed=false must be blocked from assistant completion.",
            "{\"decision_domain\":\"mcp_result_guard\",\"tool_name\":\"lan_agent_mcp_route\","
            "\"final_answer_allowed\":\"false\",\"result_hash\":\"case-final-disallowed\","
            "\"schema_version\":\"mcp_result_schema_v1\"}",
            "route",
            "final_answer_disallowed_by_result"
        },
        {
            "pending_continuation_matching_tool_allows",
            "A pending continuation must not block the exact expected next tool.",
            "{\"decision_domain\":\"mcp_tool_guard\",\"tool_name\":\"lan_agent_probe_text_file\","
            "\"file_path\":\"D:\\\\Codex-WorkDir\\\\Sean_WorkDir\\\\codex-lan-agent\\\\logs\\\\boundary\\\\one.cpp\","
            "\"primary_intent\":\"comment_cleanup\",\"trace_id\":\"case-pending-match\","
            "\"pending_continuation_active\":true,"
            "\"pending_required_tool\":\"lan_agent_probe_text_file\","
            "\"pending_required_arguments_json\":\"{\\\"name\\\":\\\"lan_agent_probe_text_file\\\",\\\"arguments\\\":{\\\"trace_id\\\":\\\"case-pending-match\\\"}}\","
            "\"pending_trace_id\":\"case-pending-match\",\"pending_goal_id\":\"case-pending-match\"}",
            "allow",
            ""
        },
        {
            "pending_continuation_wrong_tool_reroutes",
            "A pending continuation must reroute any different tool before execution.",
            "{\"decision_domain\":\"mcp_tool_guard\",\"tool_name\":\"lan_agent_delete_text_range_window_atomic\","
            "\"file_path\":\"D:\\\\Codex-WorkDir\\\\Sean_WorkDir\\\\codex-lan-agent\\\\logs\\\\boundary\\\\one.cpp\","
            "\"primary_intent\":\"comment_cleanup\",\"trace_id\":\"case-pending-mismatch\","
            "\"pending_continuation_active\":true,"
            "\"pending_required_tool\":\"lan_agent_task_memory_freeze\","
            "\"pending_required_arguments_json\":\"{\\\"name\\\":\\\"lan_agent_task_memory_freeze\\\",\\\"arguments\\\":{\\\"goal_id\\\":\\\"case-pending-mismatch\\\"}}\","
            "\"pending_trace_id\":\"case-pending-mismatch\",\"pending_goal_id\":\"case-pending-mismatch\"}",
            "route",
            "pending_continuation_mismatch"
        }
    };
}

std::string BuildSyntheticNoToolResolvedJsonl() {
    const std::string tool_content =
        "ok=false\n"
        "status=failed\n"
        "exit_code=68\n"
        "summary=mcp gateway route returned\n"
        "tool_name=lan_agent_mcp_route\n"
        "result=clips_decision_completed\n"
        "error=NEXT_CALL_JSON_MISSING\n"
        "mcp_route_mode=route\n"
        "tool_use_decision=no_tool_resolved\n"
        "continue_required=true\n"
        "current_tool_chain_node=route_decision\n"
        "chain_state=needs_user_or_route_detail\n"
        "semantic_model_clamp=supervision_alarm\n"
        "assistant_response_allowed=false\n"
        "terminal_state=false\n"
        "completion_claim_allowed=false\n"
        "final_answer_allowed=false\n"
        "verification_ok=false\n";
    std::ostringstream jsonl;
    jsonl << "{\"type\":\"message\",\"message\":{\"role\":\"assistant\",\"content\":\"\","
          << "\"id\":\"a1\",\"toolCalls\":[{\"id\":\"tc1\",\"type\":\"function\",\"function\":{\"name\":\"lan_agent_mcp_route\","
          << "\"arguments\":\"{\\\"request_text\\\":\\\"please continue previous files\\\"}\"}}]}}\n";
    jsonl << "{\"type\":\"message\",\"message\":{\"role\":\"tool\",\"content\":\""
          << codex_lan_agent::JsonEscape(tool_content)
          << "\",\"toolCallId\":\"tc1\",\"id\":\"t1\"}}\n";
    jsonl << "{\"type\":\"message\",\"message\":{\"role\":\"assistant\",\"content\":\"我已经开始处理这些文件。\","
          << "\"id\":\"a2\",\"parent\":\"t1\"}}\n";
    return jsonl.str();
}

CommandResult BuildMcpBoundaryExploreResult(
    const AgentConfig & config,
    const std::string & out_dir,
    bool include_synthetic_flow) {
    CommandResult result;
    result.ok = true;
    result.exit_code = 0;
    result.fields["provider_id"] = "codex-lan-agent";
    result.fields["capability_id"] = "mcp_boundary_explore";
    result.fields["status"] = "success";
    result.fields["result"] = "mcp_boundary_explored";
    const std::filesystem::path output_dir =
        codex_lan_agent::FlowObsDefaultOutRoot(config, "mcp_boundary_explore", out_dir);
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 1;
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to create output directory";
        result.fields["error_message"] = ec.message();
        return result;
    }

    const std::filesystem::path cases_path = output_dir / "boundary_cases.jsonl";
    const std::filesystem::path summary_path = output_dir / "boundary_summary.json";
    const std::filesystem::path candidates_path = output_dir / "rule_candidates.md";
    std::ofstream cases(cases_path, std::ios::binary);
    int case_count = 0;
    int pass_count = 0;
    int fail_count = 0;
    std::ostringstream candidate_md;
    candidate_md << "# MCP Boundary Rule Candidates\n\n";
    candidate_md << "Generated by lan_agent_mcp_boundary_explore. C++ produced facts and invoked CLIPS; CLIPS made guard decisions.\n\n";

    for (const McpBoundaryCase & test_case : BuildDefaultMcpBoundaryCases()) {
        JsonRequestView params(test_case.params_json);
        CommandResult decision = BuildClipsDecisionResult(config, params);
        const std::string actual_decision = codex_lan_agent::FlowObsField(decision, "decision");
        const std::string actual_reason = codex_lan_agent::FlowObsField(decision, "reason_code");
        const bool passed =
            actual_decision == test_case.expected_decision
            && actual_reason == test_case.expected_reason_code;
        ++case_count;
        if (passed) {
            ++pass_count;
        } else {
            ++fail_count;
            candidate_md << "## " << test_case.case_id << "\n\n"
                         << "- Expected: `" << test_case.expected_decision << "` / `"
                         << test_case.expected_reason_code << "`\n"
                         << "- Actual: `" << actual_decision << "` / `" << actual_reason << "`\n"
                         << "- Candidate action: add or adjust CLIPS rule in `mcp_result_guard.clp`.\n\n";
        }
        if (cases.is_open()) {
            cases << "{"
                  << McpBoundaryJsonStringField("case_id", test_case.case_id) << ","
                  << McpBoundaryJsonStringField("purpose", test_case.purpose) << ","
                  << McpBoundaryJsonStringField("expected_decision", test_case.expected_decision) << ","
                  << McpBoundaryJsonStringField("expected_reason_code", test_case.expected_reason_code) << ","
                  << McpBoundaryJsonStringField("actual_decision", actual_decision) << ","
                  << McpBoundaryJsonStringField("actual_reason_code", actual_reason) << ","
                  << McpBoundaryJsonStringField("matched_rule", codex_lan_agent::FlowObsField(decision, "matched_rule")) << ","
                  << "\"passed\":" << (passed ? "true" : "false")
                  << "}\n";
        }
    }
    if (fail_count == 0) {
        candidate_md << "No new rule candidates. All boundary cases are guarded by existing CLIPS rules.\n";
    }

    int synthetic_violation_count = -1;
    std::filesystem::path synthetic_flow_dir;
    if (include_synthetic_flow) {
        const std::filesystem::path synthetic_jsonl_path = output_dir / "synthetic_no_tool_resolved.jsonl";
        codex_lan_agent::FlowObsWriteTextFile(synthetic_jsonl_path, BuildSyntheticNoToolResolvedJsonl());
        synthetic_flow_dir = output_dir / "synthetic_flow";
        CommandResult flow = codex_lan_agent::BuildMcpFlowVisualizeResult(
            config,
            synthetic_jsonl_path.string(),
            synthetic_flow_dir.string());
        synthetic_violation_count =
            codex_lan_agent::FlowObsExtractJsonInt(
                codex_lan_agent::FlowObsField(flow, "script_stdout_json"),
                "violation_count",
                -1);
        result.fields["synthetic_jsonl_path"] = synthetic_jsonl_path.string();
        result.fields["synthetic_flow_status"] = codex_lan_agent::FlowObsField(flow, "status");
        result.fields["synthetic_flow_completion_state"] = codex_lan_agent::FlowObsField(flow, "completion_state");
        result.fields["synthetic_flow_violation_count"] = std::to_string(synthetic_violation_count);
        result.fields["synthetic_flow_state_dashboard_html_path"] =
            (synthetic_flow_dir / "flow_state_dashboard.html").string();
    }

    const bool synthetic_ok = !include_synthetic_flow || synthetic_violation_count > 0;
    const bool accepted = fail_count == 0 && synthetic_ok;
    std::ostringstream summary;
    summary << "{\n"
            << "  \"status\":\"success\",\n"
            << "  \"accepted\":" << (accepted ? "true" : "false") << ",\n"
            << "  \"case_count\":" << case_count << ",\n"
            << "  \"pass_count\":" << pass_count << ",\n"
            << "  \"fail_count\":" << fail_count << ",\n"
            << "  \"synthetic_violation_count\":" << synthetic_violation_count << ",\n"
            << "  \"conclusion\":\"" << (accepted ? "MCP_BOUNDARY_EXPLORATION_ACCEPTED" : "MCP_BOUNDARY_EXPLORATION_HAS_GAPS") << "\"\n"
            << "}\n";
    codex_lan_agent::FlowObsWriteTextFile(summary_path, summary.str());
    codex_lan_agent::FlowObsWriteTextFile(candidates_path, candidate_md.str());

    result.fields["out_dir"] = output_dir.string();
    result.fields["boundary_cases_jsonl_path"] = cases_path.string();
    result.fields["boundary_summary_json_path"] = summary_path.string();
    result.fields["rule_candidates_md_path"] = candidates_path.string();
    result.fields["case_count"] = std::to_string(case_count);
    result.fields["pass_count"] = std::to_string(pass_count);
    result.fields["fail_count"] = std::to_string(fail_count);
    result.fields["accepted"] = accepted ? "true" : "false";
    result.fields["conclusion"] =
        accepted ? "MCP_BOUNDARY_EXPLORATION_ACCEPTED" : "MCP_BOUNDARY_EXPLORATION_HAS_GAPS";
    result.fields["summary"] = accepted
        ? "MCP boundary exploration passed"
        : "MCP boundary exploration found guard gaps";
    result.fields["result_ref"] = summary_path.string();
    result.fields["evidence_ref"] = cases_path.string();
    return result;
}

CommandResult BuildMcpGuardRegressionAcceptanceResult(
    const AgentConfig & config,
    const std::string & input_jsonl,
    const std::string & out_dir) {
    const std::filesystem::path output_dir =
        codex_lan_agent::FlowObsDefaultOutRoot(config, "mcp_guard_regression_acceptance", out_dir);
    CommandResult boundary = BuildMcpBoundaryExploreResult(config, (output_dir / "boundary").string(), true);
    CommandResult flow;
    const bool has_flow_input = !codex_lan_agent::FlowObsTrim(input_jsonl).empty();
    if (has_flow_input) {
        flow = codex_lan_agent::BuildMcpFlowConformanceCheckResult(
            config,
            input_jsonl,
            (output_dir / "flow").string());
    }
    const bool boundary_pass = codex_lan_agent::FlowObsField(boundary, "accepted") == "true";
    const bool flow_pass_or_not_run =
        !has_flow_input || codex_lan_agent::FlowObsField(flow, "conformance_pass") == "true";
    const bool accepted = boundary_pass && flow_pass_or_not_run;

    CommandResult result;
    result.ok = true;
    result.exit_code = 0;
    result.fields["provider_id"] = "codex-lan-agent";
    result.fields["capability_id"] = "mcp_guard_regression_acceptance";
    result.fields["status"] = "success";
    result.fields["result"] = "mcp_guard_regression_acceptance_complete";
    result.fields["out_dir"] = output_dir.string();
    result.fields["boundary_summary_json_path"] = codex_lan_agent::FlowObsField(boundary, "boundary_summary_json_path");
    result.fields["boundary_cases_jsonl_path"] = codex_lan_agent::FlowObsField(boundary, "boundary_cases_jsonl_path");
    result.fields["rule_candidates_md_path"] = codex_lan_agent::FlowObsField(boundary, "rule_candidates_md_path");
    result.fields["boundary_case_count"] = codex_lan_agent::FlowObsField(boundary, "case_count");
    result.fields["boundary_pass_count"] = codex_lan_agent::FlowObsField(boundary, "pass_count");
    result.fields["boundary_fail_count"] = codex_lan_agent::FlowObsField(boundary, "fail_count");
    result.fields["synthetic_flow_violation_count"] = codex_lan_agent::FlowObsField(boundary, "synthetic_flow_violation_count");
    result.fields["input_jsonl"] = input_jsonl;
    result.fields["flow_conformance_pass"] =
        has_flow_input ? codex_lan_agent::FlowObsField(flow, "conformance_pass") : "not_run";
    result.fields["flow_violation_count"] =
        has_flow_input ? codex_lan_agent::FlowObsField(flow, "violation_count") : "";
    result.fields["flow_state_dashboard_html_path"] =
        has_flow_input ? codex_lan_agent::FlowObsField(flow, "flow_state_dashboard_html_path") : "";
    result.fields["accepted"] = accepted ? "true" : "false";
    result.fields["conclusion"] =
        accepted ? "MCP_GUARD_REGRESSION_ACCEPTED" : "MCP_GUARD_REGRESSION_HAS_GAPS";
    result.fields["summary"] = accepted
        ? "MCP guard regression acceptance passed"
        : "MCP guard regression acceptance found gaps";
    result.fields["result_ref"] = codex_lan_agent::FlowObsField(boundary, "boundary_summary_json_path");
    result.fields["evidence_ref"] = codex_lan_agent::FlowObsField(boundary, "boundary_cases_jsonl_path");
    return result;
}

std::string BuildMcpRouteDecisionParamsJson(const JsonRequestView & params) {
    const std::string request_text = params.GetString("request_text");
    const std::string explicit_file_path = FirstNonEmpty(
        params.GetString("file_path"),
        params.GetString("source_file"),
        "");
    const std::string directory_path = params.GetString("directory_path");
    const std::string file_path = FirstNonEmpty(
        explicit_file_path,
        directory_path.empty() ? ExtractMcpRouteWindowsPathFromText(request_text) : std::string(),
        "");
    const std::string primary_intent =
        InferMcpRoutePrimaryIntent(params.GetString("primary_intent"), request_text);
    std::string json = "{";
    bool first = true;
    AppendJsonStringField(&json, &first, "decision_domain", params.GetString("decision_domain", "mcp_tool_guard"));
    AppendJsonStringField(&json, &first, "model_profile", params.GetString("model_profile"));
    AppendJsonStringField(&json, &first, "tool_name", params.GetString("tool_name", "lan_agent_clips_decide"));
    AppendJsonStringField(&json, &first, "request_text", request_text);
    AppendJsonStringField(&json, &first, "file_path", file_path);
    AppendJsonStringField(&json, &first, "source_file", file_path);
    AppendJsonStringField(&json, &first, "directory_path", directory_path);
    AppendJsonStringField(&json, &first, "primary_intent", primary_intent);
    AppendJsonStringField(
        &json,
        &first,
        "query_text",
        FirstNonEmpty(
            params.GetString("query_text"),
            params.GetString("text"),
            params.GetString("anchor_text"),
            ExtractSearchQueryTextFromRequest(request_text)));
    AppendJsonStringField(&json, &first, "content", params.GetString("content"));
    AppendJsonStringField(&json, &first, "content_base64", params.GetString("content_base64"));
    AppendJsonStringField(&json, &first, "target_name", params.GetString("target_name"));
    AppendJsonStringField(&json, &first, "data", params.GetString("data"));
    AppendJsonStringField(&json, &first, "data_base64", params.GetString("data_base64"));
    AppendJsonStringField(&json, &first, "anchor_text", params.GetString("anchor_text"));
    AppendJsonStringField(&json, &first, "build_dir", params.GetString("build_dir"));
    AppendJsonStringField(&json, &first, "target", params.GetString("target"));
    AppendJsonStringField(&json, &first, "config", params.GetString("config"));
    AppendJsonStringField(&json, &first, "command", params.GetString("command"));
    AppendJsonStringField(&json, &first, "args_text", params.GetString("args_text"));
    AppendJsonStringField(&json, &first, "path", params.GetString("path"));
    AppendJsonStringField(&json, &first, "task_id", params.GetString("task_id"));
    AppendJsonStringField(&json, &first, "repo_root", params.GetString("repo_root"));
    AppendJsonStringField(&json, &first, "action_id", params.GetString("action_id"));
    AppendJsonStringField(&json, &first, "log_path", params.GetString("log_path"));
    AppendJsonStringField(&json, &first, "codex_llvm_root", params.GetString("codex_llvm_root"));
    AppendJsonStringField(&json, &first, "preflight_ref", params.GetString("preflight_ref"));
    AppendJsonStringField(&json, &first, "preflight_status", params.GetString("preflight_status"));
    AppendJsonStringField(&json, &first, "scan_mode", params.GetString("scan_mode", primary_intent == "comment_cleanup" ? "comments" : ""));

    AppendJsonStringField(&json, &first, "trace_id", params.GetString("trace_id"));
    AppendJsonStringField(&json, &first, "request_id", params.GetString("request_id"));
    AppendJsonStringField(&json, &first, "probe_ref", params.GetString("probe_ref"));
    AppendJsonStringField(&json, &first, "expected_file_hash", params.GetString("expected_file_hash"));
    AppendJsonStringField(&json, &first, "expected_range_hash", params.GetString("expected_range_hash"));
    AppendJsonStringField(&json, &first, "expected_anchor_text", params.GetString("expected_anchor_text"));
    AppendJsonStringField(&json, &first, "expected_anchor_hash", params.GetString("expected_anchor_hash"));
    AppendJsonBoolField(&json, &first, "read_to_eof", params.GetBool("read_to_eof", false), true);
    AppendJsonBoolField(&json, &first, "allow_empty_content", params.GetBool("allow_empty_content", primary_intent == "delete_lines"), true);
    AppendJsonBoolField(&json, &first, "probe_ready", params.GetBool("probe_ready", false), params.GetBool("probe_ready", false));
    AppendJsonBoolField(&json, &first, "append", params.GetBool("append", false), true);
    AppendJsonBoolField(&json, &first, "recursive", params.GetBool("recursive", true), true);
    AppendJsonBoolField(&json, &first, "show_preview", params.GetBool("show_preview", false), true);
    const auto append_int_param = [&](const std::string & key, int default_value, int minimum_value) {
        if (params.GetRawJson(key).empty()) {
            return;
        }
        if (!first) {
            json += ",";
        }
        json += "\"" + codex_lan_agent::JsonEscape(key) + "\":"
            + std::to_string(std::max(minimum_value, params.GetInt(key, default_value)));
        first = false;
    };
    append_int_param("start_line", 1, 1);
    append_int_param("end_line", 1, 1);
    append_int_param("max_lines", 200, 1);
    append_int_param("max_matches", 100, 1);
    append_int_param("max_bytes", 65536, 1);
    append_int_param("timeout_sec", 1800, 1);
    append_int_param("stall_timeout_sec", 0, 0);
    // 透传 dry_run（可能在顶层或 arguments 子对象中）。
    // Build routes already require preflight_ref, so their default is execution.
    const std::string primary_intent_lower = ToLowerAscii(primary_intent);
    const bool build_route_request =
        primary_intent_lower == "build"
        || primary_intent_lower == "build_target"
        || primary_intent_lower == "compile"
        || (!Trim(params.GetString("build_dir")).empty()
            && !Trim(params.GetString("target")).empty());
    bool dry_run_val = params.GetBool("dry_run", build_route_request ? false : true);
    if (dry_run_val) {
        const std::string args_json = params.GetRawJson("arguments", "");
        if (!args_json.empty()) {
            JsonRequestView args_view(args_json);
            dry_run_val = args_view.GetBool("dry_run", build_route_request ? false : true);
        }
    }
    AppendJsonBoolField(&json, &first, "dry_run", dry_run_val, true);
    json += "}";
    return json;
}

// ── 方案B: route 三级匹配 —— 同义词 + 参数模式推断 ──
std::string ResolveRouteBySynonymAndPattern(
    const std::string & primary_intent,
    const JsonRequestView & params) {
    static const std::unordered_map<std::string, std::string> synonym_map = {
        {"read_file", "lan_agent_read_text_file"},
        {"read", "lan_agent_read_text_file"},
        {"file_read", "lan_agent_read_text_file"},
        {"search_text", "lan_agent_search_text"},
        {"search", "lan_agent_search_text"},
        {"grep", "lan_agent_search_text"},
        {"find_text", "lan_agent_search_text"},
        {"text_search", "lan_agent_search_text"},
        {"write_file", "lan_agent_write_text_file"},
        {"write", "lan_agent_write_text_file"},
        {"file_write", "lan_agent_write_text_file"},
        {"save_file", "lan_agent_write_text_file"},
        {"create_file", "lan_agent_write_text_file"},
        {"append_text", "lan_agent_append_text_file"},
        {"append", "lan_agent_append_text_file"},
        {"append_file", "lan_agent_append_text_file"},
        {"list_directory", "lan_agent_list_directory"},
        {"list_dir", "lan_agent_list_directory"},
        {"ls", "lan_agent_list_directory"},
        {"dir", "lan_agent_list_directory"},
        {"probe_file", "lan_agent_probe_text_file"},
        {"probe", "lan_agent_probe_text_file"},
        {"file_probe", "lan_agent_probe_text_file"},
        {"replace_lines", "lan_agent_replace_line_range_atomic"},
        {"replace", "lan_agent_replace_line_range_atomic"},
        {"replace_text", "lan_agent_replace_line_range_atomic"},
        {"delete_lines", "lan_agent_replace_line_range_atomic"},
        {"delete", "lan_agent_replace_line_range_atomic"},
        {"delete_text", "lan_agent_replace_line_range_atomic"},
        {"insert", "lan_agent_insert_after_anchor_atomic"},
        {"insert_after_anchor", "lan_agent_insert_after_anchor_atomic"},
        {"insert_text", "lan_agent_insert_after_anchor_atomic"},
        {"code_format", "lan_agent_format_code_file"},
        {"format", "lan_agent_format_code_file"},
        {"format_code", "lan_agent_format_code_file"},
        {"optfile_read", "lan_agent_optfile_read"},
        {"read_optfile", "lan_agent_optfile_read"},
        {"optfile_write_preview", "lan_agent_optfile_write_preview"},
        {"preview_optfile_write", "lan_agent_optfile_write_preview"},
        {"optfile_apply_write", "lan_agent_optfile_apply_write"},
        {"optfile_write", "lan_agent_optfile_apply_write"},
        {"write_optfile", "lan_agent_optfile_apply_write"},
        {"run_flow", "lan_agent_run_cxparser_flow"},
        {"run_script", "lan_agent_run_cxparser_flow"},
        {"execute_flow", "lan_agent_run_cxparser_flow"},
        {"build", "lan_agent_build_target"},
        {"build_target", "lan_agent_build_target"},
        {"compile", "lan_agent_build_target"},
        {"cmake_build", "lan_agent_build_target"},
        {"run_command", "local_cli"},
        {"run_cli", "local_cli"},
        {"local_cli", "local_cli"},
        {"codex_local_cli", "codex_local_cli"},
        {"cli", "local_cli"},
        {"cmd", "local_cli"},
        {"command", "local_cli"},
        {"shell", "local_cli"},
        {"terminal", "local_cli"},
        {"lan_agent_run_command", "local_cli"},
    };
    const std::string intent_lower = ToLowerAscii(Trim(primary_intent));
    {
        auto it = synonym_map.find(intent_lower);
        if (it != synonym_map.end()) {
            return it->second;
        }
    }
    // 参数模式推断
    const bool has_file_path = !Trim(params.GetString("file_path")).empty();
    const bool has_content = !Trim(params.GetString("content")).empty();
    const bool has_content_base64 = !Trim(params.GetString("content_base64")).empty();
    const bool has_directory = !Trim(params.GetString("directory_path")).empty();
    const bool has_build_dir = !Trim(params.GetString("build_dir")).empty();
    const bool has_target = !Trim(params.GetString("target")).empty();
    const bool has_command = !Trim(params.GetString("command")).empty();
    const bool has_query = !Trim(FirstNonEmpty(
        params.GetString("query_text"),
        params.GetString("text"),
        params.GetString("anchor_text"))).empty();
    const bool has_start_line = !Trim(params.GetString("start_line")).empty();
    const bool has_end_line = !Trim(params.GetString("end_line")).empty();
    const bool has_anchor = !Trim(params.GetString("anchor_text")).empty();
    if (has_build_dir && has_target) return "lan_agent_build_target";
    if (has_command) return "local_cli";
    if (has_directory && has_query) return "lan_agent_search_text";
    if (has_directory) return "lan_agent_list_directory";
    if (has_file_path && has_anchor) return "lan_agent_insert_after_anchor_atomic";
    if (has_file_path && has_start_line && has_end_line) return "lan_agent_replace_line_range_atomic";
    if (has_file_path && (has_content || has_content_base64)) return "lan_agent_write_text_file";
    if (has_file_path && has_start_line) return "lan_agent_read_text_file";
    if (has_file_path) return "lan_agent_probe_text_file";
    return "";
}

// ── 方案B: route 候选工具列表（route 未命中时返回给 LLM）──
std::string BuildCandidateToolsJson(const std::string & primary_intent) {
    static const std::vector<std::pair<std::string, std::string>> catalog = {
        {"lan_agent_probe_text_file", "read_file, probe, file_probe"},
        {"lan_agent_read_text_file", "read_file, read, file_read, get_file, view_file, cat"},
        {"lan_agent_search_text", "search_text, search, grep, find_text, text_search"},
        {"lan_agent_list_directory", "list_directory, list_dir, ls, dir"},
        {"lan_agent_write_text_file", "write_file, write, file_write, save_file, create_file"},
        {"lan_agent_append_text_file", "append_text, append, append_file"},
        {"lan_agent_insert_after_anchor_atomic", "insert, insert_after_anchor, insert_text"},
        {"lan_agent_replace_line_range_atomic", "replace_lines, replace, replace_text"},
        {"lan_agent_replace_line_range_atomic", "replace_lines, replace, replace_text, delete_lines, delete, delete_text"},
        {"lan_agent_format_code_file", "code_format, format, format_code"},
        {"lan_agent_optfile_read", "optfile_read, read_optfile"},
        {"lan_agent_optfile_write_preview", "optfile_write_preview, preview_optfile_write"},
        {"lan_agent_optfile_apply_write", "optfile_apply_write, optfile_write, write_optfile"},
        {"lan_agent_run_cxparser_flow", "run_flow, run_script, execute_flow"},
        {"lan_agent_build_target", "build, build_target, compile, cmake_build"},
        {"local_cli", "run_command, run_cli, local_cli, cli, cmd, command, shell, terminal"},
        {"codex_local_cli", "codex_local_cli"},
    };
    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto & entry : catalog) {
        if (!first) { json << ","; }
        json << "{\"name\":\"" << entry.first << "\",\"aliases\":\"" << entry.second << "\"}";
        first = false;
    }
    json << "]";
    return json.str();
}

std::string NormalizeMcpRouteTargetToolName(const std::string & target_tool_name) {
    const std::string trimmed = Trim(target_tool_name);
    const std::string lower = ToLowerAscii(trimmed);
    if (lower == "lan_agent_run_command"
        || lower == "run_command"
        || lower == "run_cli"
        || lower == "cli"
        || lower == "cmd"
        || lower == "command"
        || lower == "shell"
        || lower == "terminal") {
        return "local_cli";
    }
    if (lower == "local_cli") {
        return "local_cli";
    }
    if (lower == "codex_local_cli") {
        return "codex_local_cli";
    }
    return trimmed;
}

struct BuildLogDiagnosticSummary {
    std::string first_error;
    std::string first_warning;
    std::string excerpt;
    int diagnostic_line_count = 0;
};

bool LooksLikeBuildErrorLine(const std::string & lower_line) {
    return lower_line.find(": error ") != std::string::npos
        || lower_line.find(" error c") != std::string::npos
        || lower_line.find(" error lnk") != std::string::npos
        || lower_line.find(" error msb") != std::string::npos
        || lower_line.find("fatal error") != std::string::npos;
}

bool LooksLikeBuildWarningLine(const std::string & lower_line) {
    return lower_line.find(": warning ") != std::string::npos
        || lower_line.find(" warning c") != std::string::npos
        || lower_line.find(" warning lnk") != std::string::npos
        || lower_line.find(" warning msb") != std::string::npos;
}

std::string ClampDiagnosticLine(const std::string & line) {
    constexpr std::size_t kMaxDiagnosticLineChars = 800;
    if (line.size() <= kMaxDiagnosticLineChars) {
        return line;
    }
    return line.substr(0, kMaxDiagnosticLineChars) + "...";
}

BuildLogDiagnosticSummary ExtractBuildLogDiagnosticSummary(const std::string & log_path) {
    BuildLogDiagnosticSummary summary;
    std::ifstream input(log_path, std::ios::binary);
    if (!input) {
        return summary;
    }
    std::string line;
    std::vector<std::string> excerpt_lines;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }
        const std::string lower = ToLowerAscii(trimmed);
        const bool is_error = LooksLikeBuildErrorLine(lower);
        const bool is_warning = LooksLikeBuildWarningLine(lower);
        if (!is_error && !is_warning) {
            continue;
        }
        ++summary.diagnostic_line_count;
        const std::string clamped = ClampDiagnosticLine(trimmed);
        if (is_error && summary.first_error.empty()) {
            summary.first_error = clamped;
        }
        if (is_warning && summary.first_warning.empty()) {
            summary.first_warning = clamped;
        }
        if (excerpt_lines.size() < 8) {
            excerpt_lines.push_back(clamped);
        }
    }
    for (std::size_t i = 0; i < excerpt_lines.size(); ++i) {
        if (i > 0) {
            summary.excerpt += "\n";
        }
        summary.excerpt += excerpt_lines[i];
    }
    return summary;
}

CommandResult BuildLocalCliRegistryResult(
    const AgentConfig & config,
    const JsonRequestView & params,
    const std::string & registry_tool_name) {
    const std::string args_text = FirstNonEmpty(
        params.GetString("args_text"),
        params.GetString("directory_path"),
        params.GetString("path"));
    CommandResult result = LocalCliResult(
        config,
        params.GetString("command"),
        params.GetString("task_id"),
        params.GetString("repo_root"),
        params.GetString("action_id"),
        params.GetString("build_dir"),
        params.GetString("target"),
        params.GetString("config"),
        params.GetString("log_path"),
        args_text,
        params.GetBool("dry_run", false));
    result.fields["registry_tool_name"] = registry_tool_name;
    result.fields["local_cli_route_registry_enabled"] = "true";
    if (registry_tool_name == "lan_agent_run_command") {
        result.fields["compat_alias_target"] = "local_cli";
    }
    return result;
}

bool IsSafeBuildToken(const std::string & value) {
    if (value.empty() || value.size() > 160) {
        return false;
    }
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == ':') {
            continue;
        }
        return false;
    }
    return true;
}

std::string QuoteBuildCommandArgument(const std::string & value) {
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted += "\"";
    return quoted;
}

std::string ResolveCmakeCommandFromCache(const std::filesystem::path & cmake_cache_path) {
    std::ifstream input(cmake_cache_path);
    if (!input.is_open()) {
        return "cmake";
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::string prefix = "CMAKE_COMMAND:INTERNAL=";
        if (line.rfind(prefix, 0) != 0) {
            continue;
        }
        std::string command = Trim(line.substr(prefix.size()));
        if (command.size() >= 2 && command.front() == '"' && command.back() == '"') {
            command = command.substr(1, command.size() - 2);
        }
        return command.empty() ? std::string("cmake") : command;
    }
    return "cmake";
}

CommandResult BuildLocalCmakeTargetResult(
    const AgentConfig & config,
    const std::string & raw_build_dir,
    const std::string & raw_target,
    const std::string & raw_config_name,
    int timeout_sec,
    int stall_timeout_sec,
    bool dry_run,
    const std::string & raw_codex_llvm_root) {
    CommandResult result;
    result.fields["result"] = dry_run ? "build_target_dry_run" : "build_target_executed";
    result.fields["operation"] = "cmake_build_target";
    result.fields["build_dir"] = raw_build_dir;
    result.fields["target"] = raw_target;
    result.fields["config"] = raw_config_name.empty() ? "Release" : raw_config_name;
    result.fields["build_runner"] = "RunCommandWithLog";
    result.fields["command_policy"] = "whitelisted_cmake_build_target";
    result.fields["timeout_sec"] = std::to_string(std::max(1, timeout_sec));
    result.fields["stall_timeout_sec"] = std::to_string(std::max(0, stall_timeout_sec));

    if (Trim(raw_build_dir).empty() || Trim(raw_target).empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "build_dir and target are required";
        result.fields["summary"] = "build target request missing required arguments";
        return result;
    }

    const std::string config_name = raw_config_name.empty() ? "Release" : raw_config_name;
    if (!IsSafeBuildToken(raw_target)) {
        result.ok = false;
        result.exit_code = 422;
        result.fields["error"] = "target contains unsupported characters";
        result.fields["summary"] = "build target rejected by whitelist";
        return result;
    }
    if (!IsSafeBuildToken(config_name)) {
        result.ok = false;
        result.exit_code = 422;
        result.fields["error"] = "config contains unsupported characters";
        result.fields["summary"] = "build config rejected by whitelist";
        return result;
    }

    std::filesystem::path normalized_build_dir;
    std::string path_error;
    if (!TryResolveAllowedPath(config, raw_build_dir, &normalized_build_dir, &path_error)) {
        result.ok = false;
        result.exit_code = 403;
        result.fields["error"] = path_error;
        result.fields["summary"] = "build_dir is outside allowed roots";
        return result;
    }
    result.fields["normalized_build_dir"] = normalized_build_dir.string();

    std::error_code ec;
    if (!std::filesystem::is_directory(normalized_build_dir, ec) || ec) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["error"] = "build_dir does not exist or is not a directory";
        result.fields["summary"] = "build directory not found";
        return result;
    }
    const std::filesystem::path cmake_cache = normalized_build_dir / "CMakeCache.txt";
    result.fields["cmake_cache_path"] = cmake_cache.string();
    if (!std::filesystem::is_regular_file(cmake_cache, ec) || ec) {
        result.ok = false;
        result.exit_code = 409;
        result.fields["error"] = "CMakeCache.txt not found in build_dir";
        result.fields["summary"] = "build directory is not configured";
        result.fields["next_action"] = "run cmake configure for build_dir before lan_agent_build_target";
        return result;
    }

    std::string codex_llvm_root = raw_codex_llvm_root;
    if (codex_llvm_root.empty()) {
        const char * env_llvm_root = std::getenv("CODEX_LLVM_ROOT");
        if (env_llvm_root != nullptr) {
            codex_llvm_root = env_llvm_root;
        }
    }
    std::filesystem::path normalized_llvm_root;
    if (!codex_llvm_root.empty()) {
        std::string llvm_error;
        if (!TryResolveAllowedPath(config, codex_llvm_root, &normalized_llvm_root, &llvm_error)) {
            result.ok = false;
            result.exit_code = 403;
            result.fields["error"] = "codex_llvm_root rejected: " + llvm_error;
            result.fields["summary"] = "CODEX_LLVM_ROOT is outside allowed roots";
            return result;
        }
        result.fields["codex_llvm_root"] = normalized_llvm_root.string();
        result.fields["codex_llvm_root_source"] = raw_codex_llvm_root.empty() ? "process_env" : "request";
    } else {
        result.fields["codex_llvm_root_source"] = "unset";
    }

    const std::string cmake_command = ResolveCmakeCommandFromCache(cmake_cache);
    result.fields["cmake_command"] = cmake_command;
    result.fields["cmake_command_source"] =
        cmake_command == "cmake" ? "path_lookup" : "cmake_cache";

    std::ostringstream command;
    command << QuoteBuildCommandArgument(cmake_command);
    if (!codex_llvm_root.empty()) {
        command << " -E env "
                << QuoteBuildCommandArgument("CODEX_LLVM_ROOT=" + normalized_llvm_root.string())
                << " "
                << QuoteBuildCommandArgument(cmake_command);
    }
    command << " --build " << QuoteBuildCommandArgument(normalized_build_dir.string())
            << " --config " << QuoteBuildCommandArgument(config_name)
            << " --target " << QuoteBuildCommandArgument(raw_target);

    result.fields["command"] = command.str();
    result.fields["working_directory"] = normalized_build_dir.string();
    if (dry_run) {
        result.ok = true;
        result.exit_code = 0;
        result.fields["summary"] = "build target dry run returned";
        result.fields["next_action"] = "call lan_agent_build_target with dry_run=false to execute";
        return result;
    }

    const std::string log_path = BuildLogPath(config, "build_target_cmake");
    codex_lan_agent::ProcessRunResult run_result;
    std::string run_error;
    const int effective_timeout = std::max(1, timeout_sec);
    const int effective_stall_timeout = std::max(0, stall_timeout_sec);
    if (!codex_lan_agent::RunCommandWithLog(
            command.str(),
            normalized_build_dir.string(),
            log_path,
            effective_timeout,
            effective_stall_timeout,
            &run_result,
            &run_error)) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["error"] = run_error;
        result.fields["log_path"] = log_path;
        result.fields["result_ref"] = log_path;
        result.fields["evidence_ref"] = log_path;
        result.fields["summary"] = "failed to start cmake build";
        return result;
    }

    result.ok = run_result.exit_code == 0;
    result.exit_code = run_result.exit_code;
    result.fields["log_path"] = log_path;
    result.fields["result_ref"] = log_path;
    result.fields["evidence_ref"] = log_path;
    result.fields["resolved_log_path"] = log_path;
    result.fields["process_id"] = std::to_string(run_result.process_id);
    result.fields["runtime_sec"] = std::to_string(run_result.runtime_sec);
    result.fields["timed_out"] = run_result.timed_out ? "true" : "false";
    result.fields["stalled"] = run_result.stalled ? "true" : "false";
    result.fields["process_output_observed"] = run_result.process_output_observed ? "true" : "false";
    result.fields["completion_reason"] = run_result.completion_reason;
    result.fields["started_at"] = run_result.started_at;
    result.fields["finished_at"] = run_result.finished_at;
    result.fields["effective_timeout_sec"] = std::to_string(effective_timeout);
    result.fields["effective_stall_timeout_sec"] = std::to_string(effective_stall_timeout);
    if (!result.ok) {
        const BuildLogDiagnosticSummary diagnostics = ExtractBuildLogDiagnosticSummary(log_path);
        result.fields["diagnostic_source"] = "build_log";
        result.fields["build_diagnostic_line_count"] = std::to_string(diagnostics.diagnostic_line_count);
        result.fields["first_compiler_error"] = diagnostics.first_error;
        result.fields["first_compiler_warning"] = diagnostics.first_warning;
        result.fields["build_error_excerpt"] = diagnostics.excerpt;
        if (run_result.timed_out) {
            result.fields["failure_mode"] = "build_timeout";
        } else if (run_result.stalled) {
            result.fields["failure_mode"] = "build_stalled";
        } else if (!diagnostics.first_error.empty()) {
            result.fields["failure_mode"] = "compiler_error";
        } else {
            result.fields["failure_mode"] = "build_failed";
        }
    }
    result.fields["summary"] = result.ok
        ? "cmake build target completed"
        : (GetFieldOrDefault(result, "first_compiler_error", "").empty()
            ? "cmake build target failed"
            : "cmake build target failed: " + GetFieldOrDefault(result, "first_compiler_error", ""));
    result.fields["semantic_outcome"] = result.ok ? "build_succeeded" : "build_failed";
    result.fields["next_action"] = result.ok
        ? "build complete"
        : (GetFieldOrDefault(result, "first_compiler_error", "").empty()
            ? "inspect result_ref/evidence_ref build log"
            : "fix first_compiler_error; full build log is in result_ref/evidence_ref");
    return result;
}

const std::unordered_map<std::string, McpToolHandler> & BuildMcpToolHandlerRegistry() {
    static const std::unordered_map<std::string, McpToolHandler> handlers = {
        {"lan_agent_mcp_route", [](const AgentConfig & config, const JsonRequestView & params) {
            // ── Phase1-3: 入口首先解析 Model-Profile（必须所有模式分支都共享）──
            const std::string explicit_profile_name = params.GetString("model_profile");
            const bool has_explicit_profile = !Trim(explicit_profile_name).empty();
            const std::string raw_profile_name = explicit_profile_name;
            const std::string profile_source = has_explicit_profile
                ? std::string("explicit")
                : std::string("default_small_chain");
            const ModelProfileConfig profile = ResolveModelProfileByName(raw_profile_name);
            const bool profile_valid = profile.IsValid();
            auto InjectProfileMeta = [&profile, &profile_valid, &profile_source](CommandResult & r) {
                r.fields["request_model_profile"] = profile.name;
                r.fields["request_model_profile_valid"] = profile_valid ? "true" : "false";
                r.fields["request_model_profile_source"] = profile_source;
                r.fields["profile_allow_direct_call_mode"] = profile.allow_direct_call_mode ? "true" : "false";
                r.fields["profile_route_aggressiveness"] = RouteAggressivenessToString(profile.route_aggressiveness);
                r.fields["profile_gate_strictness_level"] = GateStrictnessToString(profile.gate_strictness);
                r.fields["profile_semantic_bundle_max_chars"] = std::to_string(profile.semantic_bundle_max_chars);
                r.fields["profile_semantic_bundle_max_nodes"] = std::to_string(profile.semantic_bundle_max_nodes);
                r.fields["profile_max_concurrent_tools_per_turn"] = std::to_string(profile.max_concurrent_tools_per_turn);
                r.fields["profile_snapshot_merge_every_n_steps"] = std::to_string(profile.snapshot_merge_every_n_steps);
                r.fields["profile_lenient_json_parsing"] = profile.lenient_json_parsing ? "true" : "false";
                r.fields["profile_max_repeated_verification_calls"] = std::to_string(profile.max_repeated_verification_calls);
            };

            const std::string requested_mode = ToLowerAscii(params.GetString("mode"));
            const bool has_routable_request =
                !Trim(params.GetString("request_text")).empty()
                || !Trim(params.GetString("primary_intent")).empty()
                || !Trim(params.GetString("file_path")).empty()
                || !Trim(params.GetString("source_file")).empty()
                || !Trim(params.GetString("directory_path")).empty()
                || (!Trim(params.GetString("build_dir")).empty()
                    && !Trim(params.GetString("target")).empty());
            const std::string mode = requested_mode.empty()
                ? (has_routable_request ? std::string("route") : std::string("overview"))
                : requested_mode;
            if (mode.empty() || mode == "overview" || mode == "guide" || mode == "guidance") {
                CommandResult result = BuildMcpOverviewResult(config);
                InjectProfileMeta(result);
                result.fields["mcp_route_mode"] = "overview";
                result.fields["tool_use_decision"] = "guidance_only";
                result.fields["current_tool_chain_node"] = "overview";
                result.fields["chain_state"] = "no_execution_started";
                result.fields["tool_surface_policy"] = "chat_layer_single_gateway";
                result.fields["visible_tool_count"] = "1";
                result.fields["visible_tool_name"] = "lan_agent_mcp_route";
                result.fields["internal_tool_surface"] = "full_registry_hidden_from_tools_list";
                result.fields["next_action"] =
                    "Use mode=route to ask MCP for an internal tool decision, or mode=call with target_tool_name and arguments to execute one internal tool.";
                return result;
            }

            if (mode == "route" || mode == "decide" || mode == "decision") {
                const std::string route_params_json = BuildMcpRouteDecisionParamsJson(params);
                JsonRequestView route_params(route_params_json);
                CommandResult result = BuildClipsDecisionResult(config, route_params);
                InjectProfileMeta(result);
                result.fields["mcp_route_mode"] = "route";
                result.fields["tool_surface_policy"] = "chat_layer_single_gateway";
                result.fields["visible_tool_name"] = "lan_agent_mcp_route";
                result.fields["internal_execution_performed"] = "false";
                result.fields["current_tool_chain_node"] = "route_decision";
                const std::string inferred_intent =
                    NormalizeMcpPrimaryIntentForClips(route_params.GetString("primary_intent"));
                const std::string raw_intent = route_params.GetString("primary_intent");
                // 注入映射 trace 到结果，供观测面板可视化
                {
                    const auto & trace = GetLastIntentMappingTrace();
                    result.fields["intent_mapping_raw"] = trace.raw_input;
                    result.fields["intent_mapping_final"] = trace.final_tag;
                    result.fields["intent_mapping_source"] = trace.source;
                }
                const std::string routed_file_path = FirstNonEmpty(
                    route_params.GetString("file_path"),
                    route_params.GetString("directory_path"),
                    "");
                const bool route_is_directory =
                    McpRoutePathIsDirectory(routed_file_path)
                    || McpRouteRequestMentionsDirectoryListing(route_params.GetString("request_text"));
                const std::string clips_route_target = GetFieldOrDefault(result, "route_target", "");
                // Deterministic route priority:
                // target_tool_name > explicit primary_intent > parameter shape > request text/CLIPS.
                const std::string explicit_target =
                    NormalizeMcpRouteTargetToolName(params.GetString("target_tool_name"));
                const std::string explicit_primary_intent = Trim(params.GetString("primary_intent"));
                const std::string explicit_intent_target = explicit_primary_intent.empty()
                    ? std::string()
                    : ResolveRouteBySynonymAndPattern(explicit_primary_intent, route_params);
                const std::string parameter_shape_target =
                    ResolveRouteBySynonymAndPattern("", route_params);
                const std::string request_inferred_target = explicit_primary_intent.empty()
                    ? FirstNonEmpty(
                        ResolveRouteBySynonymAndPattern(inferred_intent, route_params),
                        ResolveRouteBySynonymAndPattern(raw_intent, route_params),
                        "")
                    : std::string();
                const std::string route_target = FirstNonEmpty(
                    explicit_target,
                    explicit_intent_target,
                    parameter_shape_target,
                    route_params.GetString("route_hint"),
                    clips_route_target,
                    request_inferred_target,
                    route_is_directory ? std::string("lan_agent_list_directory") : std::string(),
                    routed_file_path.empty()
                        ? std::string()
                        : (route_is_directory
                            ? std::string("lan_agent_list_directory")
                            : (inferred_intent == "code_format"
                                ? std::string("lan_agent_format_code_file")
                                : std::string("lan_agent_probe_text_file"))),
                    "");

                if (!route_target.empty()) {
                    const bool deterministic_route =
                        !explicit_target.empty()
                        || !explicit_intent_target.empty()
                        || !parameter_shape_target.empty();
                    const std::string next_call_json = FirstNonEmpty(
                        deterministic_route
                            ? BuildPreGuardRouteCallJson(route_target, route_params)
                            : std::string(),
                        GetFieldOrDefault(result, "route_arguments_json", ""),
                        BuildPreGuardRouteCallJson(route_target, route_params),
                        "");
                    result.fields["route_target"] = route_target;
                    result.fields["route_resolution"] =
                        !explicit_target.empty() ? "explicit_target_tool" :
                        !explicit_intent_target.empty() ? "explicit_primary_intent" :
                        !parameter_shape_target.empty() ? "parameter_shape" :
                        !route_params.GetString("route_hint").empty() ? "route_hint" :
                        !clips_route_target.empty() ? "clips_request_text" :
                        !request_inferred_target.empty() ? "request_text" : "heuristic";

                    // ── 方案C: route auto_execute —— 命中后直接执行，不要求二次 mode=call ──
                    const auto & exec_registry = BuildMcpToolHandlerRegistry();
                    const auto exec_it = exec_registry.find(route_target);
                    if (exec_it != exec_registry.end() && route_target != "lan_agent_mcp_route") {
                        JsonRequestView exec_params(next_call_json);
                        // CLIPS pre-guard 仍然执行（安全不降级）
                        CommandResult preflight_result;
                        bool pending_was_resolved = false;
                        if (MaybeApplyClipsPreflightBlock(config, route_target, exec_params, &preflight_result)) {
                            LanResultBuilder(&preflight_result).Finalize(config, route_target);
                            InjectProfileMeta(preflight_result);
                            preflight_result.fields["mcp_route_mode"] = "route_auto_execute";
                            preflight_result.fields["routed_tool_name"] = route_target;
                            preflight_result.fields["internal_execution_performed"] = "false";
                            preflight_result.fields["auto_execute"] = "true";
                            preflight_result.fields["visible_tool_name"] = "lan_agent_mcp_route";
                            preflight_result.fields["tool_use_decision"] =
                                preflight_result.ok ? "pre_guard_rerouted" : "pre_guard_blocked";
                            preflight_result.fields["current_tool_chain_node"] = route_target;
                            // 保留意图映射 trace
                            preflight_result.fields["intent_mapping_raw"] = GetFieldOrDefault(result, "intent_mapping_raw", "");
                            preflight_result.fields["intent_mapping_final"] = GetFieldOrDefault(result, "intent_mapping_final", "");
                            preflight_result.fields["intent_mapping_source"] = GetFieldOrDefault(result, "intent_mapping_source", "");

                            // ── P2-fix: pending continuation 自动代偿执行 ──
                            // 问题: probe 创建 pending continuation 后，小模型不知道如何执行续行，
                            //       导致所有新请求被 pending_continuation_mismatch 拦截，形成死锁。
                            // 修复: 当检测到 pending_continuation_mismatch 时，自动加载并执行
                            //       pending continuation 中的工具，清除 pending 状态，
                            //       然后继续执行当前请求的目标工具。
                            // 注意: MaybeApplyClipsPreflightBlock 路径中 reason_code 存储在
                            //       clips_pre_call_tool_reason_code 字段（由 ApplyClipsDecisionFields 设置），
                            //       而非 pre_guard_reason_code（后者仅在 PatchOperations 路径中设置）
                            const std::string pre_guard_reason = FirstNonEmpty(
                                GetFieldOrDefault(preflight_result, "clips_pre_call_tool_reason_code", ""),
                                GetFieldOrDefault(preflight_result, "pre_guard_reason_code", ""),
                                GetFieldOrDefault(preflight_result, "clips_pre_call_tool_decision_reason_code", ""));
                            if (IsMcpGatewayReadOnlyTool(route_target)
                                && (pre_guard_reason == "pending_continuation_mismatch"
                                    || pre_guard_reason == "continuation_stale")) {
                                pending_was_resolved = true;
                                preflight_result.fields["read_only_pending_bypass"] = "true";
                                preflight_result.fields["read_only_pending_bypass_reason"] = pre_guard_reason;
                            } else if (pre_guard_reason == "pending_continuation_mismatch") {

                                // 1. 加载 pending continuation
                                McpPendingContinuationFields pending =
                                    LoadMcpPendingContinuationForParams(config, exec_params);
                                if (pending.active && !pending.required_tool.empty()) {
                                    // 2. 执行 pending continuation 中的工具
                                    const auto & pending_registry = BuildMcpToolHandlerRegistry();
                                    const auto pending_it = pending_registry.find(pending.required_tool);
                                    if (pending_it != pending_registry.end()) {
                                        JsonRequestView pending_params(pending.required_arguments_json);
                                        CommandResult pending_result = pending_it->second(config, pending_params);
                                        LanResultBuilder(&pending_result).Finalize(config, pending.required_tool);

                                        // 3. 清除 pending continuation 文件
                                        WriteMcpPendingContinuationByKey(
                                            config, pending.trace_id, CommandResult(),
                                            "auto_pending_resolver", false);
                                        if (!pending.goal_id.empty() && pending.goal_id != pending.trace_id) {
                                            WriteMcpPendingContinuationByKey(
                                                config, pending.goal_id, CommandResult(),
                                                "auto_pending_resolver", false);
                                        }
                                        const std::string pending_path_key = BuildMcpPendingContinuationPathKey(
                                            GetFieldOrDefault(pending_result, "file_path", ""));
                                        if (!pending_path_key.empty()) {
                                            WriteMcpPendingContinuationByKey(
                                                config, pending_path_key, CommandResult(),
                                                "auto_pending_resolver", false);
                                        }

                                        // 4. 标记 pending 已解决，跳过 return，走正常执行路径
                                        pending_was_resolved = true;
                                        preflight_result.fields["auto_pending_resolved"] = "true";
                                        preflight_result.fields["auto_pending_tool"] = pending.required_tool;
                                        preflight_result.fields["auto_pending_ok"] = pending_result.ok ? "true" : "false";
                                    }
                                }
                            }

                            if (!pending_was_resolved) {
                                return preflight_result;
                            }
                        }
                        // 直接执行目标工具（pending 已解决时也走这里）
                        CommandResult exec_result = exec_it->second(config, exec_params);
                        LanResultBuilder(&exec_result).Finalize(config, route_target);
                        InjectProfileMeta(exec_result);
                        exec_result.fields["mcp_route_mode"] = "route_auto_execute";
                        exec_result.fields["routed_tool_name"] = route_target;
                        exec_result.fields["internal_execution_performed"] = "true";
                        exec_result.fields["auto_execute"] = "true";
                        exec_result.fields["visible_tool_name"] = "lan_agent_mcp_route";
                        exec_result.fields["tool_use_decision"] = exec_result.ok ? "used_tool" : "tool_failed";
                        exec_result.fields["current_tool_chain_node"] = route_target;
                        exec_result.fields["route_target"] = route_target;
                        exec_result.fields["route_resolution"] = result.fields["route_resolution"];
                        // 保留意图映射 trace（供观测面板）
                        exec_result.fields["intent_mapping_raw"] = GetFieldOrDefault(result, "intent_mapping_raw", "");
                        exec_result.fields["intent_mapping_final"] = GetFieldOrDefault(result, "intent_mapping_final", "");
                        exec_result.fields["intent_mapping_source"] = GetFieldOrDefault(result, "intent_mapping_source", "");
                        // 如果 pending continuation 被自动解决，保留标记
                        if (pending_was_resolved) {
                            exec_result.fields["auto_pending_resolved"] = "true";
                            exec_result.fields["auto_pending_tool"] = GetFieldOrDefault(preflight_result, "auto_pending_tool", "");
                            exec_result.fields["auto_pending_ok"] = GetFieldOrDefault(preflight_result, "auto_pending_ok", "");
                        }
                        // 保留 CLIPS 决策元数据
                        exec_result.fields["route_params_enriched"] = "true";
                        exec_result.fields["route_pipeline"] =
                            inferred_intent == "comment_cleanup"
                                ? "comment_cleanup_then_optional_code_format"
                                : inferred_intent;

                        // ── P1b: auto_continue 只读链编排 ──
                        // probe → read 自动续行：当 probe 成功且推荐下一步是 read 时，直接执行
                        const std::string recommended_next =
                            GetFieldOrDefault(exec_result, "recommended_next_tool", "");
                        const std::string next_tool_name =
                            GetFieldOrDefault(exec_result, "required_tool_name", "");
                        const std::string actual_tool_used =
                            GetFieldOrDefault(exec_result, "routed_tool_name", route_target);
                        const bool is_read_only_probe =
                            (actual_tool_used == "lan_agent_probe_text_file"
                             || route_target == "lan_agent_probe_text_file")
                            && exec_result.ok
                            && !exec_result.fields.empty()
                            && (recommended_next == "lan_agent_read_text_file"
                                || next_tool_name == "lan_agent_read_text_file"
                                || route_target == "lan_agent_read_text_file");
                        const bool auto_continue_requested =
                            params.GetBool("auto_continue", false)
                            || GetFieldOrDefault(exec_result, "auto_continue_required", "") == "true";
                        // Debug: 记录 auto_continue 判定条件
                        exec_result.fields["_debug_is_read_only_probe"] = is_read_only_probe ? "true" : "false";
                        exec_result.fields["_debug_auto_continue_requested"] = auto_continue_requested ? "true" : "false";
                        exec_result.fields["_debug_actual_tool_used"] = actual_tool_used;
                        exec_result.fields["_debug_route_target"] = route_target;
                        exec_result.fields["_debug_required_tool_name"] = next_tool_name;
                        exec_result.fields["_debug_recommended_next"] = recommended_next;
                        exec_result.fields["_debug_auto_continue_param"] =
                            params.GetBool("auto_continue", false) ? "true" : "false";
                        if (is_read_only_probe && auto_continue_requested) {
                            // required_tool_arguments_json 格式: {"name":"tool_name","arguments":{...}}
                            // 需要从中提取 arguments 部分作为工具的实际参数
                            const std::string wrapped_json = FirstNonEmpty(
                                GetFieldOrDefault(exec_result, "next_call_json", ""),
                                GetFieldOrDefault(exec_result, "required_tool_arguments_json", ""));
                            if (!wrapped_json.empty()) {
                                // 解析包装 JSON 并提取 arguments
                                JsonRequestView wrapped_params(wrapped_json);
                                std::string tool_args_json = wrapped_params.GetRawJson("arguments", "");
                                if (tool_args_json.empty()) {
                                    // 如果没有 arguments 包装，直接用整个 JSON
                                    tool_args_json = wrapped_json;
                                }
                                if (!tool_args_json.empty()) {
                                    JsonRequestView follow_up_params(tool_args_json);
                                    CommandResult preflight2;
                                    if (!MaybeApplyClipsPreflightBlock(
                                            config,
                                            "lan_agent_read_text_file",
                                            follow_up_params,
                                            &preflight2)) {
                                        auto read_it = exec_registry.find("lan_agent_read_text_file");
                                        if (read_it != exec_registry.end()) {
                                            CommandResult read_result =
                                                read_it->second(config, follow_up_params);
                                            LanResultBuilder(&read_result).Finalize(
                                                config, "lan_agent_read_text_file");
                                            // 合并 read 结果到 exec_result
                                            exec_result.fields["auto_continue_executed"] = "true";
                                            exec_result.fields["auto_continue_chain"] = "probe→read";
                                            exec_result.fields["auto_continue_tool"] =
                                                "lan_agent_read_text_file";
                                            exec_result.fields["auto_continue_ok"] =
                                                read_result.ok ? "true" : "false";
                                            exec_result.fields["content"] =
                                                GetFieldOrDefault(read_result, "content", "");
                                            exec_result.fields["content_begin"] =
                                                GetFieldOrDefault(read_result, "content_begin", "");
                                            exec_result.fields["content_end"] =
                                                GetFieldOrDefault(read_result, "content_end", "");
                                            exec_result.fields["total_lines"] =
                                                GetFieldOrDefault(read_result, "total_lines", "");
                                            exec_result.fields["returned_lines"] =
                                                GetFieldOrDefault(read_result, "returned_lines", "");
                                            exec_result.fields["has_more"] =
                                                GetFieldOrDefault(read_result, "has_more", "false");
                                            exec_result.fields["read_complete"] =
                                                GetFieldOrDefault(read_result, "read_complete", "true");
                                            exec_result.fields["next_action"] =
                                                GetFieldOrDefault(read_result, "next_action", "");
                                            exec_result.fields["required_tool_name"] =
                                                GetFieldOrDefault(read_result, "required_tool_name", "");
                                            exec_result.fields["required_tool_arguments_json"] =
                                                GetFieldOrDefault(read_result, "required_tool_arguments_json", "");
                                            exec_result.fields["status"] =
                                                GetFieldOrDefault(read_result, "status", "success");
                                            exec_result.fields["tool_use_decision"] =
                                                read_result.ok ? "used_tool" : "tool_failed";
                                            exec_result.fields["current_tool_chain_node"] =
                                                "lan_agent_read_text_file";
                                            // 如果 read 完成，标记终态
                                            if (GetFieldOrDefault(read_result, "read_complete", "") == "true"
                                                && GetFieldOrDefault(read_result, "has_more", "false") == "false") {
                                                exec_result.fields["terminal_state"] = "true";
                                                exec_result.fields["completion_claim_allowed"] = "true";
                                                exec_result.fields["final_answer_allowed"] = "true";
                                                exec_result.fields["verification_ok"] = "true";
                                                exec_result.fields["chain_state"] = "terminal";
                                                exec_result.fields["continue_required"] = "false";
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // ── P0-fix: probe/scan 等中间步骤有 next_tool_name 时，强制非终态 ──
                        // 根因: probe 结果未设 semantic_model_clamp，ApplySupervisionEnvelope
                        //       默认 terminal_state=true，导致小模型误判任务完成，
                        //       不执行 next_tool_name 指向的 delete/edit 操作
                        const std::string exec_next_tool = FirstNonEmpty(
                            GetFieldOrDefault(exec_result, "next_tool_name", ""),
                            GetFieldOrDefault(exec_result, "required_tool_name", ""),
                            GetFieldOrDefault(exec_result, "recommended_next_tool", ""));
                        const std::string exec_result_type = GetFieldOrDefault(exec_result, "result", "");
                        const bool bounded_read_page_terminal =
                            route_target == "lan_agent_read_text_file"
                            && !route_params.GetBool("read_to_eof", false);
                        if (bounded_read_page_terminal) {
                            exec_result.fields["source_has_more"] = GetFieldOrDefault(exec_result, "has_more", "false");
                            exec_result.fields["source_next_start_line"] = GetFieldOrDefault(exec_result, "next_start_line", "");
                            exec_result.fields["bounded_read_complete"] = "true";
                            exec_result.fields["read_to_eof"] = "false";
                            exec_result.fields["has_more"] = "false";
                            exec_result.fields["read_complete"] = "true";
                            exec_result.fields["file_complete"] = "true";
                            exec_result.fields["read_status"] = "complete";
                            exec_result.fields["task_completion"] = "complete";
                            exec_result.fields["content_read_completion"] = "complete";
                            exec_result.fields["page_status"] = "bounded_page";
                            exec_result.fields["requires_followup"] = "false";
                            exec_result.fields["continue_required"] = "false";
                            exec_result.fields["auto_continue_required"] = "false";
                            exec_result.fields["semantic_model_clamp"] = "none";
                            exec_result.fields["terminal_state"] = "true";
                            exec_result.fields["final_answer_allowed"] = "true";
                            exec_result.fields["completion_claim_allowed"] = "true";
                            exec_result.fields["assistant_response_allowed"] = "true";
                            exec_result.fields["verification_ok"] = "true";
                            exec_result.fields["next_start_line"] = "";
                            exec_result.fields["next_tool_name"] = "";
                            exec_result.fields["required_tool_name"] = "";
                            exec_result.fields["next_call_json"] = "";
                            exec_result.fields["required_tool_arguments_json"] = "";
                        }
                        const bool is_intermediate_step =
                            exec_result.ok
                            && !bounded_read_page_terminal
                            && !exec_next_tool.empty()
                            && exec_result_type != "delete_complete"
                            && exec_result_type != "directory_cleanup_complete"
                            && exec_result_type != "format_complete"
                            && exec_result_type != "edit_complete";
                        if (is_intermediate_step) {
                            exec_result.fields["semantic_model_clamp"] = "tool_call_only";
                            exec_result.fields["terminal_state"] = "false";
                            exec_result.fields["final_answer_allowed"] = "false";
                            exec_result.fields["completion_claim_allowed"] = "false";
                            exec_result.fields["assistant_response_allowed"] = "false";
                            exec_result.fields["continue_required"] = "true";
                            exec_result.fields["goal_status"] = "not_complete";
                            exec_result.fields["task_done"] = "false";
                            exec_result.fields["chain_state"] = "needs_tool_call";
                            exec_result.fields["required_next_action_type"] = "mcp_tool_call";
                            exec_result.fields["verification_ok"] = "false";
                            if (GetFieldOrDefault(exec_result, "must_continue_until", "").empty()) {
                                exec_result.fields["must_continue_until"] = "terminal_state=true";
                            }
                            if (GetFieldOrDefault(exec_result, "completion_guard", "").empty()) {
                                exec_result.fields["completion_guard"] =
                                    "NON_TERMINAL_RESULT: do not claim completion; execute next_tool_name via lan_agent_mcp_route mode=call";
                            }
                        }

                        // ── P1-fix: 自动链式执行 (Auto-Chaining) ──
                        // 问题根源：小模型（如 gemma-4-E4B）无法理解 next_tool_name 是强指令，
                        //           导致 probe 后停滞（tc=0）。
                        // 解决方案：在网关层自动代偿执行 next_tool_name 指向的工具，直到链路结束。
                        {
                            constexpr int kMaxAutoChainDepth = 5;
                            int chain_depth = 0;
                            std::string current_tool = bounded_read_page_terminal ? std::string() : exec_next_tool;
                            CommandResult current_result = exec_result;

                            while (chain_depth < kMaxAutoChainDepth && !current_tool.empty() && current_result.ok) {
                                const auto next_it = exec_registry.find(current_tool);
                                if (next_it == exec_registry.end()) {
                                    // 找不到工具，跳出循环，交给 LLM
                                    break;
                                }

                                // 1. 构造下一个工具的参数字符串
                                std::string args_json = GetFieldOrDefault(current_result, "required_tool_arguments_json", "");
                                // 兜底：如果没有参数，传递 file_path
                                if (args_json.empty()) {
                                    const std::string fp = GetFieldOrDefault(current_result, "file_path", "");
                                    if (!fp.empty()) {
                                        args_json = "{\"file_path\":\"" + fp + "\"";
                                        // 如果是 delete 操作，补充 default 参数
                                        if (current_tool.find("delete") != std::string::npos) {
                                            args_json += ",\"max_lines\":200,\"start_line\":1";
                                        }
                                        args_json += "}";
                                    }
                                }
                                // 如果还是空，构造一个默认的空 JSON 对象
                                if (args_json.empty()) {
                                    args_json = "{}";
                                }

                                // 2. 用确定的 args_json 构造 JsonRequestView (无引用悬挂风险)
                                JsonRequestView next_params(args_json);

                                // 3. CLIPS pre-guard 检查
                                CommandResult preflight_next;
                                if (MaybeApplyClipsPreflightBlock(config, current_tool, next_params, &preflight_next)) {
                                    LanResultBuilder(&preflight_next).Finalize(config, current_tool);
                                    // 检查是否是 pending_continuation_mismatch，如果是则自动解决
                                    const std::string chain_guard_reason = FirstNonEmpty(
                                        GetFieldOrDefault(preflight_next, "clips_pre_call_tool_reason_code", ""),
                                        GetFieldOrDefault(preflight_next, "pre_guard_reason_code", ""));
                                    if (chain_guard_reason == "pending_continuation_mismatch") {
                                        // 自动执行 pending continuation 中的工具
                                        McpPendingContinuationFields chain_pending =
                                            LoadMcpPendingContinuationForParams(config, next_params);
                                        if (chain_pending.active && !chain_pending.required_tool.empty()) {
                                            const auto & chain_pend_registry = BuildMcpToolHandlerRegistry();
                                            const auto chain_pend_it = chain_pend_registry.find(chain_pending.required_tool);
                                            if (chain_pend_it != chain_pend_registry.end()) {
                                                JsonRequestView chain_pend_params(chain_pending.required_arguments_json);
                                                CommandResult chain_pend_result = chain_pend_it->second(config, chain_pend_params);
                                                LanResultBuilder(&chain_pend_result).Finalize(config, chain_pending.required_tool);
                                                // 清除 pending
                                                WriteMcpPendingContinuationByKey(
                                                    config, chain_pending.trace_id, CommandResult(),
                                                    "auto_chain_pending_resolver", false);
                                                if (!chain_pending.goal_id.empty() && chain_pending.goal_id != chain_pending.trace_id) {
                                                    WriteMcpPendingContinuationByKey(
                                                        config, chain_pending.goal_id, CommandResult(),
                                                        "auto_chain_pending_resolver", false);
                                                }
                                                // 跳过 break，继续执行当前工具
                                                exec_result.fields["auto_chain_pending_resolved"] = "true";
                                                // 重新执行当前工具（不再被 pending 阻塞）
                                                CommandResult reexec_next = next_it->second(config, next_params);
                                                LanResultBuilder(&reexec_next).Finalize(config, current_tool);
                                                current_result = reexec_next;
                                                // 合并结果
                                                for (const auto & kv : reexec_next.fields) {
                                                    if (kv.first == "tool_use_decision" || kv.first == "current_tool_chain_node" || kv.first == "route_target") continue;
                                                    exec_result.fields[kv.first] = kv.second;
                                                }
                                                // 检查是否还有下一步
                                                const std::string reexec_next_tool = FirstNonEmpty(
                                                    GetFieldOrDefault(reexec_next, "next_tool_name", ""),
                                                    GetFieldOrDefault(reexec_next, "required_tool_name", ""));
                                                const std::string reexec_result_type = GetFieldOrDefault(reexec_next, "result", "");
                                                if (reexec_next_tool.empty() || !reexec_next.ok
                                                    || reexec_result_type == "delete_complete"
                                                    || reexec_result_type == "directory_cleanup_complete"
                                                    || reexec_result_type == "format_complete"
                                                    || reexec_result_type == "edit_complete") {
                                                    exec_result.fields["terminal_state"] = "true";
                                                    exec_result.fields["final_answer_allowed"] = "true";
                                                    exec_result.fields["auto_chain_completed"] = "true";
                                                    exec_result.fields["continue_required"] = "false";
                                                    break;
                                                }
                                                current_tool = reexec_next_tool;
                                                chain_depth++;
                                                continue;
                                            }
                                        }
                                    }
                                    current_result = preflight_next;
                                    // 被拦截，跳出
                                    break;
                                }

                                // 4. 执行工具
                                CommandResult next_result = next_it->second(config, next_params);
                                LanResultBuilder(&next_result).Finalize(config, current_tool);

                                // 5. 合并结果回主结果
                                exec_result.fields["auto_chain_depth"] = std::to_string(chain_depth + 1);
                                exec_result.fields["auto_chain_step_" + std::to_string(chain_depth + 1) + "_tool"] = current_tool;
                                exec_result.fields["auto_chain_step_" + std::to_string(chain_depth + 1) + "_ok"] = next_result.ok ? "true" : "false";

                                // 将下一个结果的核心字段覆盖到 exec_result
                                for (const auto & kv : next_result.fields) {
                                    const std::string & key = kv.first;
                                    // 跳过某些内部状态，但保留关键信息
                                    if (key == "tool_use_decision" || key == "current_tool_chain_node" || key == "route_target") continue;
                                    exec_result.fields[key] = kv.second;
                                }

                                // 6. 检查是否还有下一步
                                const std::string next_next_tool = FirstNonEmpty(
                                    GetFieldOrDefault(next_result, "next_tool_name", ""),
                                    GetFieldOrDefault(next_result, "required_tool_name", ""));
                                const std::string next_result_type = GetFieldOrDefault(next_result, "result", "");
                                const std::string next_has_more = GetFieldOrDefault(next_result, "has_more", "");
                                const bool next_terminal = GetFieldOrDefault(next_result, "terminal_state", "") == "true";
                                // 完成判定：显式完成结果类型 或 terminal_state=true
                                const bool is_done =
                                    !next_next_tool.empty()
                                    && (next_result_type == "delete_complete"
                                        || next_result_type == "directory_cleanup_complete"
                                        || next_result_type == "format_complete"
                                        || next_result_type == "edit_complete"
                                        || (next_result_type == "no_text_range_in_window"
                                            && next_has_more == "false")
                                        || next_terminal);

                                if (next_next_tool.empty() || is_done || !next_result.ok) {
                                    // 链路结束，设置终态标记
                                    exec_result.fields["terminal_state"] = "true";
                                    exec_result.fields["final_answer_allowed"] = "true";
                                    exec_result.fields["completion_claim_allowed"] = "true";
                                    exec_result.fields["semantic_model_clamp"] = "final_answer_only";
                                    exec_result.fields["auto_chain_completed"] = "true";
                                    exec_result.fields["continue_required"] = "false";
                                    break;
                                }

                                // 准备下一轮循环
                                current_tool = next_next_tool;
                                current_result = next_result;
                                chain_depth++;
                            }

                            // 如果因达到最大深度而退出，标记状态让 LLM 接力
                            if (chain_depth >= kMaxAutoChainDepth) {
                                exec_result.fields["auto_chain_max_depth_reached"] = "true";
                                exec_result.fields["terminal_state"] = "false";
                                exec_result.fields["continue_required"] = "true";
                            }
                        }

                        return exec_result;
                    }

                    // Fallback: 工具不在 registry 中，返回 CONTINUE 让 LLM 用 mode=call
                    result.fields["tool_use_decision"] = "use_tool";
                    result.fields["chain_state"] = "needs_tool_call";
                    result.fields["required_next_action_type"] = "mcp_tool_call";
                    result.fields["required_tool_name"] = route_target;
                    result.fields["required_tool_arguments_json"] = next_call_json;
                    result.fields["next_call_json"] = next_call_json;
                    result.fields["next_action"] =
                        "tool_call_only: call lan_agent_mcp_route with mode=call, target_tool_name=required_tool_name, and arguments from required_tool_arguments_json; do not ask for local file permissions.";
                    result.fields["status"] = "CONTINUE";
                    result.fields["supervision_status"] = "closed_loop_continue";
                    result.fields["goal_status"] = "not_complete";
                    result.fields["continue_required"] = "true";
                    result.fields["terminal_state"] = "false";
                    result.fields["completion_claim_allowed"] = "false";
                    result.fields["assistant_response_allowed"] = "false";
                    result.fields["final_answer_allowed"] = "false";
                    result.fields["verification_ok"] = "false";
                    result.fields["semantic_model_clamp"] = "tool_call_only";
                    result.fields["route_params_enriched"] = "true";
                    result.fields["route_pipeline"] =
                        inferred_intent == "comment_cleanup"
                            ? "comment_cleanup_then_optional_code_format"
                            : inferred_intent;
                    if (inferred_intent == "comment_cleanup" && route_is_directory) {
                        const std::string route_trace_id = route_params.GetString("trace_id");
                        const std::string route_goal_id = FirstNonEmpty(
                            GetFieldOrDefault(result, "goal_id", ""),
                            route_trace_id,
                            "directory_comment_cleanup");
                        CommandResult task_list = codex_lan_agent::BuildDirectoryCommentCleanupTaskListResult(
                            config,
                            route_goal_id,
                            route_trace_id,
                            routed_file_path,
                            "T2");
                        result.fields["flow_id"] = "directory_comment_cleanup_bounded_window_v1";
                        result.fields["flow_task_list_required"] = "true";
                        result.fields["flow_current_task_id"] = "T2";
                        result.fields["flow_next_task_id"] = "T2";
                        result.fields["flow_task_list_path"] = GetFieldOrDefault(task_list, "flow_task_list_path", "");
                        result.fields["flow_task_list_md_path"] = GetFieldOrDefault(task_list, "flow_task_list_md_path", "");
                    }
                } else {
                    // ── route 未命中：返回候选工具列表 + 建议调用 ──
                    result.fields["tool_use_decision"] = "no_tool_resolved";
                    result.fields["chain_state"] = "needs_user_or_route_detail";
                    result.fields["route_missing_reason"] = 
                        "primary_intent '" + inferred_intent + "' did not match any registered tool";
                    result.fields["candidate_tools"] = BuildCandidateToolsJson(inferred_intent);
                    result.fields["next_action"] =
                        "route did not resolve a tool. Either: (1) provide file_path and primary_intent (e.g. read_file, write_file, list_directory), or (2) call mode=call with target_tool_name from candidate_tools list.";
                }
                return result;
            }

            if (mode != "call" && mode != "execute") {
                CommandResult result;
                InjectProfileMeta(result);
                result.ok = false;
                result.exit_code = 400;
                result.fields["status"] = "failed";
                result.fields["result"] = "invalid_mcp_route_mode";
                result.fields["mcp_route_mode"] = mode;
                result.fields["error"] = "mode must be overview, route, or call";
                return result;
            }

            const std::string requested_target_tool_name = params.GetString("target_tool_name");
            const std::string target_tool_name = NormalizeMcpRouteTargetToolName(requested_target_tool_name);
            if (target_tool_name.empty()) {
                CommandResult result;
                InjectProfileMeta(result);
                result.ok = false;
                result.exit_code = 400;
                result.fields["status"] = "failed";
                result.fields["result"] = "missing_target_tool_name";
                result.fields["mcp_route_mode"] = "call";
                result.fields["error"] = "mode=call requires target_tool_name";
                return result;
            }
            if (target_tool_name == "lan_agent_mcp_route") {
                CommandResult result;
                InjectProfileMeta(result);
                result.ok = false;
                result.exit_code = 400;
                result.fields["status"] = "failed";
                result.fields["result"] = "recursive_mcp_route_blocked";
                result.fields["mcp_route_mode"] = "call";
                result.fields["routed_tool_name"] = target_tool_name;
                result.fields["requested_target_tool_name"] = requested_target_tool_name;
                result.fields["route_compat_canonical_tool_name"] = target_tool_name;
                result.fields["error"] = "lan_agent_mcp_route cannot call itself";
                return result;
            }

            if (!profile.allow_direct_call_mode) {
                CommandResult result;
                InjectProfileMeta(result);
                result.ok = false;
                result.exit_code = 403;
                result.fields["status"] = "failed";
                result.fields["result"] = "direct_call_blocked_by_model_profile";
                result.fields["mcp_route_mode"] = "call";
                result.fields["mcp_route_entry_tool"] = "lan_agent_mcp_route";
                result.fields["routed_tool_name"] = target_tool_name;
                result.fields["requested_target_tool_name"] = requested_target_tool_name;
                result.fields["route_compat_canonical_tool_name"] = target_tool_name;
                result.fields["routed_tool_surface"] = "internal_registry_hidden_from_tools_list";
                result.fields["internal_execution_performed"] = "false";
                result.fields["visible_tool_name"] = "lan_agent_mcp_route";
                result.fields["tool_use_decision"] = "blocked_by_profile";
                result.fields["current_tool_chain_node"] = target_tool_name;
                result.fields["chain_state"] = "blocked_before_execution";
                result.fields["error"] = "mode=call is blocked for this model_profile; use mode=route or pass model_profile=large-llm explicitly";
                result.fields["suggested_next_call"] = "mode=route";
                result.fields["profile_direct_call_policy"] = "profile_blocks_direct_call";
                return result;
            }

            std::string arguments_json = params.GetString("arguments_json");
            if (arguments_json.empty()) {
                arguments_json = ExtractJsonObjectRaw(params.body(), "arguments");
            }
            if (arguments_json.empty()) {
                arguments_json = ExtractJsonObjectRaw(params.body(), "params");
            }
            if (arguments_json.empty()) {
                arguments_json = "{}";
            }
            if (has_explicit_profile && ExtractJsonString(arguments_json, "model_profile").empty()) {
                const std::size_t object_end = arguments_json.find_last_of('}');
                if (object_end != std::string::npos) {
                    const std::size_t first_value = arguments_json.find_first_not_of(" \t\r\n{");
                    const bool has_existing_argument_fields =
                        first_value != std::string::npos && first_value < object_end;
                    std::string augmented_arguments_json = arguments_json.substr(0, object_end);
                    if (has_existing_argument_fields) {
                        augmented_arguments_json += ",";
                    }
                    const std::string dq(1, '"');
                    augmented_arguments_json += dq + "model_profile" + dq + ":" + dq;
                    augmented_arguments_json += codex_lan_agent::JsonEscape(explicit_profile_name);
                    augmented_arguments_json += dq;
                    augmented_arguments_json += arguments_json.substr(object_end);
                    arguments_json = augmented_arguments_json;
                }
            }
            JsonRequestView routed_params(arguments_json);
            CommandResult preflight_result;
            if (MaybeApplyClipsPreflightBlock(config, target_tool_name, routed_params, &preflight_result)) {
                LanResultBuilder(&preflight_result).Finalize(config, target_tool_name);
                InjectProfileMeta(preflight_result);
                preflight_result.fields["mcp_route_mode"] = "call";
                preflight_result.fields["mcp_route_entry_tool"] = "lan_agent_mcp_route";
                preflight_result.fields["routed_tool_name"] = target_tool_name;
                preflight_result.fields["requested_target_tool_name"] = requested_target_tool_name;
                preflight_result.fields["route_compat_canonical_tool_name"] = target_tool_name;
                preflight_result.fields["routed_tool_surface"] = "internal_registry_hidden_from_tools_list";
                preflight_result.fields["internal_execution_performed"] = "false";
                preflight_result.fields["visible_tool_name"] = "lan_agent_mcp_route";
                preflight_result.fields["tool_use_decision"] =
                    preflight_result.ok ? "pre_guard_rerouted" : "pre_guard_blocked";
                preflight_result.fields["current_tool_chain_node"] = target_tool_name;
                preflight_result.fields["chain_state"] =
                    preflight_result.ok ? "needs_tool_call" : "blocked_before_execution";
                const std::string pre_guard_reason = FirstNonEmpty(
                    GetFieldOrDefault(preflight_result, "clips_pre_call_tool_reason_code", ""),
                    GetFieldOrDefault(preflight_result, "pre_guard_reason_code", ""),
                    GetFieldOrDefault(preflight_result, "clips_pre_call_tool_decision_reason_code", ""));
                if (!(IsMcpGatewayReadOnlyTool(target_tool_name)
                        && (pre_guard_reason == "pending_continuation_mismatch"
                            || pre_guard_reason == "continuation_stale"))) {
                    return preflight_result;
                }
                preflight_result.fields["read_only_pending_bypass"] = "true";
                preflight_result.fields["read_only_pending_bypass_reason"] = pre_guard_reason;

            }

            const auto & registry = BuildMcpToolHandlerRegistry();
            const auto tool_it = registry.find(target_tool_name);
            if (tool_it == registry.end()) {
                CommandResult remote_result;
                if (::codex_lan_agent::remote_mcp_bridge::TryHandleRemoteMcpTool(
                        config,
                        target_tool_name,
                        routed_params,
                        &remote_result)) {
                    LanResultBuilder(&remote_result).Finalize(config, target_tool_name);
                    InjectProfileMeta(remote_result);
                    remote_result.fields["mcp_route_mode"] = "call";
                    remote_result.fields["mcp_route_entry_tool"] = "lan_agent_mcp_route";
                    remote_result.fields["routed_tool_name"] = target_tool_name;
                    remote_result.fields["requested_target_tool_name"] = requested_target_tool_name;
                    remote_result.fields["route_compat_canonical_tool_name"] = target_tool_name;
                    remote_result.fields["routed_tool_surface"] = "remote_mcp_internal_proxy_hidden_from_tools_list";
                    remote_result.fields["internal_execution_performed"] = "true";
                    remote_result.fields["visible_tool_name"] = "lan_agent_mcp_route";
                    remote_result.fields["tool_use_decision"] = remote_result.ok ? "used_remote_tool" : "remote_tool_failed";
                    remote_result.fields["current_tool_chain_node"] = target_tool_name;
                    return remote_result;
                }

                CommandResult result;
                InjectProfileMeta(result);
                result.ok = false;
                result.exit_code = 404;
                result.fields["status"] = "failed";
                result.fields["result"] = "internal_tool_not_found";
                result.fields["mcp_route_mode"] = "call";
                result.fields["routed_tool_name"] = target_tool_name;
                result.fields["requested_target_tool_name"] = requested_target_tool_name;
                result.fields["route_compat_canonical_tool_name"] = target_tool_name;
                result.fields["error"] = "target_tool_name is not registered in the internal MCP registry";
                // 提供恢复建议：如果 target_tool_name 匹配已知 primary_intent，建议用 mode=route
                const std::string inferred = InferMcpRoutePrimaryIntent(
                    target_tool_name, params.GetString("request_text"));
                if (!inferred.empty()) {
                    result.fields["suggested_next_call"] = "mode=route";
                    result.fields["suggested_primary_intent"] = inferred;
                    result.fields["recovery_hint"] =
                        "Use mode=route with primary_intent=" + inferred +
                        " instead of mode=call with target_tool_name=" + target_tool_name;
                }
                LanResultBuilder(&result).Finalize(config, target_tool_name);
                result.fields["mcp_route_mode"] = "call";
                result.fields["routed_tool_name"] = target_tool_name;
                result.fields["requested_target_tool_name"] = requested_target_tool_name;
                result.fields["route_compat_canonical_tool_name"] = target_tool_name;
                result.fields["visible_tool_name"] = "lan_agent_mcp_route";
                result.fields["internal_execution_performed"] = "false";
                return result;
            }

            CommandResult result = tool_it->second(config, routed_params);
            LanResultBuilder(&result).Finalize(config, target_tool_name);
            InjectProfileMeta(result);
            result.fields["profile_direct_call_policy"] = profile.allow_direct_call_mode
                ? "profile_allows_direct_call"
                : "gateway_chain_preflight_controls_direct_call";
            result.fields["mcp_route_mode"] = "call";
            result.fields["mcp_route_entry_tool"] = "lan_agent_mcp_route";
            result.fields["routed_tool_name"] = target_tool_name;
            result.fields["requested_target_tool_name"] = requested_target_tool_name;
            result.fields["route_compat_canonical_tool_name"] = target_tool_name;
            result.fields["routed_tool_surface"] = "internal_registry_hidden_from_tools_list";
            result.fields["internal_execution_performed"] = "true";
            result.fields["visible_tool_name"] = "lan_agent_mcp_route";
            result.fields["tool_use_decision"] = "used_tool";
            result.fields["current_tool_chain_node"] = target_tool_name;
            const std::string required_next_tool = GetFieldOrDefault(result, "required_tool_name", "");
            if (!required_next_tool.empty() && required_next_tool != target_tool_name) {
                result.fields["chain_state"] = "needs_tool_call";
                result.fields["status"] = "needs_continue";
                result.fields["supervision_status"] = "closed_loop_continue";
                result.fields["goal_status"] = "not_complete";
                result.fields["continue_required"] = "true";
                result.fields["terminal_state"] = "false";
                result.fields["completion_claim_allowed"] = "false";
                result.fields["assistant_response_allowed"] = "false";
                result.fields["final_answer_allowed"] = "false";
                result.fields["verification_ok"] = "false";
                result.fields["semantic_model_clamp"] = "tool_call_only";
                result.fields["required_next_action_type"] = "mcp_tool_call";
                if (GetFieldOrDefault(result, "next_call_json", "").empty()
                    && !GetFieldOrDefault(result, "required_tool_arguments_json", "").empty()) {
                    result.fields["next_call_json"] = GetFieldOrDefault(result, "required_tool_arguments_json", "");
                }
                result.fields["next_action"] =
                    "tool_call_only: call required_tool_arguments_json through lan_agent_mcp_route mode=call; do not write waiting/progress text and do not claim completion yet.";
            } else if (GetFieldOrDefault(result, "final_answer_allowed", "") == "true"
                && GetFieldOrDefault(result, "verification_ok", "") == "true") {
                result.fields["chain_state"] = "terminal";
            } else {
                result.fields["chain_state"] = "tool_result_returned";
            }
            return result;
        }},
        {"lan_agent_health", [](const AgentConfig & config, const JsonRequestView &) {
            return BuildHealthResult(config);
        }},
        {"lan_agent_runtime_overview", [](const AgentConfig & config, const JsonRequestView &) {
            return BuildRuntimeOverviewResult(config);
        }},
        {"lan_agent_remote_session_overview", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildRemoteSessionOverviewResult(
                config,
                std::max(1000, params.GetInt("timeout_ms", 10000)));
        }},
        {"lan_agent_task_overview", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildTaskOverviewResult(
                config,
                std::max(1, params.GetInt("max_entries", 20)));
        }},
        {"lan_agent_event_overview", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildEventOverviewResult(
                config,
                std::max(1, params.GetInt("max_entries", 10)),
                std::max(0, params.GetInt("offset", 0)),
                params.GetBool("include_auto", false),
                params.GetBool("include_noise", false),
                params.GetString("command_name"),
                params.GetString("session_id"),
                params.GetString("task_id"),
                params.GetString("since_timestamp"));
        }},
        {"lan_agent_mcp_overview", [](const AgentConfig & config, const JsonRequestView &) {
            return BuildMcpOverviewResult(config);
        }},
        {"lan_agent_remote_mcp_overview", [](const AgentConfig & config, const JsonRequestView &) {
            return ::codex_lan_agent::remote_mcp_bridge::BuildRemoteMcpOverviewResult(config);
        }},
        {"lan_agent_rag_overview", [](const AgentConfig & config, const JsonRequestView &) {
            return BuildRagOverviewResult(config);
        }},
        {"lan_agent_patch_overview", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildPatchOverviewResult(
                config,
                std::max(1, params.GetInt("max_entries", 20)),
                std::max(0, params.GetInt("offset", 0)),
                params.GetString("patch_id"),
                params.GetString("trace_id"),
                params.GetString("file_path"));
        }},
        {"lan_agent_browser_list_overview", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildBrowserListOverviewResult(
                config,
                std::max(1, params.GetInt("task_max_entries", 20)),
                std::max(1, params.GetInt("event_max_entries", 10)),
                std::max(1, params.GetInt("patch_max_entries", 20)));
        }},
        {"lan_agent_rag_index_status", [](const AgentConfig & config, const JsonRequestView &) {
            return BuildRagIndexStatusResult(config);
        }},
        {"lan_agent_rag_clips_meta", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildRagClipsMetaResult(
                config,
                params.GetString("query"),
                std::max(1, params.GetInt("top_k", 3)));
        }},
        {"lan_agent_rag_clips_run", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildRagClipsRunResult(
                config,
                params.GetString("query"),
                std::max(1, params.GetInt("top_k", 3)),
                params.GetString("request_id"),
                params.GetString("trace_id"),
                params.GetString("query_id"),
                std::max(1000, params.GetInt("timeout_ms", 15000)));
        }},
        {"lan_agent_rag_storage_lookup", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildRagStorageLookupResult(
                config,
                params.GetString("kind"),
                params.GetString("id"),
                params.GetString("slice_id"),
                params.GetString("trace_id"),
                params.GetString("query_id"),
                params.GetString("node_id"),
                params.GetString("edge_id"),
                std::max(1, params.GetInt("limit", 32)),
                std::max(1000, params.GetInt("timeout_ms", 15000)));
        }},
        {"lan_agent_rag_review_observe", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildRagReviewObservationResult(
                config,
                params.GetString("summary"),
                params.GetString("scenario"),
                params.GetString("train"),
                params.GetString("request_id"),
                params.GetString("trace_id"),
                params.GetString("query_id"),
                params.GetString("module"),
                params.GetString("task_layer"),
                params.GetString("task_case"),
                params.GetString("dataset_bridge"),
                params.GetString("test_bucket"),
                params.GetString("test_flow"),
                params.GetString("baseline_objective"),
                params.GetString("best_objective"),
                params.GetString("objective_delta"),
                params.GetString("comparison_status"),
                params.GetString("comparison_magnitude"),
                params.GetString("optimization_signal"),
                params.GetString("risk_axis"),
                params.GetString("bucket_coverage"),
                params.GetString("coverage_gap"),
                params.GetString("coverage_status"),
                params.GetString("next_review_action"),
                params.GetString("review_scope"),
                params.GetString("result_stage"),
                params.GetString("primary_review_ref"),
                params.GetString("summary_ref"),
                params.GetString("compare_ref"),
                params.GetString("replay_ref"),
                params.GetString("best_params_ref"),
                params.GetBool("human_review_required", false),
                params.GetString("conclusion_id"),
                params.GetString("short_conclusion"),
                params.GetString("why_it_matters"),
                params.GetString("next_observation"),
                params.GetString("source_refs"),
                params.GetString("tags"),
                std::max(1000, params.GetInt("timeout_ms", 15000)));
        }},
        {"lan_agent_rag_storage_page", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildRagStoragePageResult(
                config,
                params.GetString("kind", "clips_facts"),
                params.GetString("trace_id"),
                params.GetString("query_id"),
                params.GetString("test_bucket"),
                params.GetString("coverage_gap"),
                params.GetString("result_stage"),
                params.GetString("coverage_status"),
                params.GetString("run_kind"),
                params.GetString("fact_type"),
                std::max(1, params.GetInt("limit", 32)),
                std::max(0, params.GetInt("offset", 0)),
                std::max(1000, params.GetInt("timeout_ms", 15000)));
        }},
        {"lan_agent_task_memory_freeze", [](const AgentConfig & config, const JsonRequestView & params) {
            const std::string freeze_goal_id =
                ::codex_lan_agent::TaskMemoryFirstNonEmpty(
                    params.GetString("goal_id"),
                    params.GetString("current_goal_id"),
                    params.GetString("task_goal_id"));
            if (Trim(params.GetString("next_call_json")).empty()
                && Trim(params.GetString("next_tool_name")).empty()) {
                const std::string recovered_arguments =
                    ::codex_lan_agent::LookupTaskMemoryPendingFreezeArguments(
                        freeze_goal_id,
                        params.GetString("trace_id"));
                if (!Trim(recovered_arguments).empty()) {
                    JsonRequestView recovered_params(recovered_arguments);
                    CommandResult recovered =
                        ::codex_lan_agent::BuildTaskMemoryFreezeResult(config, recovered_params);
                    recovered.fields["task_memory_freeze_recovery"] = "pending_arguments_cache";
                    recovered.fields["task_memory_freeze_recovered_from_short_call"] = "true";
                    return recovered;
                }
            }
            return ::codex_lan_agent::BuildTaskMemoryFreezeResult(config, params);
        }},
        {"lan_agent_task_memory_append_step", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildTaskMemoryAppendStepResult(config, params);
        }},
        {"lan_agent_task_memory_execute_continuation_budget", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildTaskMemoryExecuteContinuationBudgetRunnerResult(config, params);
        }},
        {"lan_agent_task_memory_resume_and_execute", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildTaskMemoryResumeAndExecuteResult(config, params);
        }},
        {"lan_agent_task_memory_new_chat_round_selftest", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildTaskMemoryNewChatRoundSelftestResult(config, params);
        }},
        {"lan_agent_task_memory_build_kv_snapshot", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildTaskMemoryBuildKvSnapshotResult(config, params);
        }},
        {"lan_agent_task_memory_kv_lookup", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildTaskMemoryKvLookupResult(config, params);
        }},
        {"lan_agent_task_memory_rocksdb_mirror", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildTaskMemoryRocksDbMirrorResult(config, params);
        }},
        {"lan_agent_task_memory_rocksdb_lookup", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildTaskMemoryRocksDbLookupResult(config, params);
        }},
        {"lan_agent_task_memory_rocksdb_parity_check", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildTaskMemoryRocksDbParityCheckResult(config, params);
        }},
        {"lan_agent_task_memory_migration_assess", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildTaskMemoryMigrationAssessResult(config, params);
        }},
        {"lan_agent_task_memory_structure_manifest", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildTaskMemoryStructureManifestResult(config, params);
        }},
        {"lan_agent_task_memory_migration_acceptance", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildTaskMemoryMigrationAcceptanceResult(config, params);
        }},
        {"lan_agent_task_memory_resume_context", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildTaskMemoryResumeContextResult(config, params);
        }},
        {"lan_agent_remote_session_semantic_catalog", [](const AgentConfig & config, const JsonRequestView &) {
            return BuildRemoteSessionSemanticCatalogResult(config);
        }},
        {"lan_agent_semantic_grid_ingest_text", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildSemanticGridIngestTextResult(config, params);
        }},
        {"lan_agent_semantic_grid_build", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildSemanticGridBuildResult(config, params);
        }},
        {"lan_agent_semantic_grid_query", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildSemanticGridQueryResult(config, params);
        }},
        {"lan_agent_semantic_grid_trace_source", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildSemanticGridTraceSourceResult(config, params);
        }},
        {"lan_agent_semantic_grid_context_bundle", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildSemanticGridContextBundleResult(config, params);
        }},
        {"lan_agent_semantic_grid_incremental_update", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildSemanticGridIncrementalUpdateResult(config, params);
        }},
        {"lan_agent_list_profiles", [](const AgentConfig & config, const JsonRequestView &) {
            return BuildProfileListResult(config);
        }},
        {"lan_agent_profile_catalog", [](const AgentConfig & config, const JsonRequestView &) {
            return BuildProfileListResult(config);
        }},
        {"lan_agent_list_cxparser_flows", [](const AgentConfig & config, const JsonRequestView &) {
            return BuildCxParserFlowCatalogResult(&config);
        }},
        {"lan_agent_validate_cxparser_flow", [](const AgentConfig & config, const JsonRequestView & params) {
            return ValidateCxParserFlowResult(
                &config,
                params.GetString("flow_id"),
                params.GetString("params_json"));
        }},
        {"lan_agent_run_cxparser_flow", [](const AgentConfig & config, const JsonRequestView & params) {
            const std::string flow_id = params.GetString("flow_id");
            const std::string params_json = params.GetString("params_json");
            const std::string test_statement = params.GetString("test_statement");
            const std::string test_text = params.GetString("test_text");
            const std::string input_text = params.GetString("input_text");
            const std::string arguments_text = params.GetString("arguments_text");
            const std::string args = params.GetString("args");
            const std::string script = params.GetString("script");
            const std::string script_path = params.GetString("script_path");
            const std::string script_dir = params.GetString("script_dir");
            const std::string script_directory = params.GetString("script_directory");
            const std::string kind = params.GetString("kind");
            const std::string layer = params.GetString("layer");
            const std::string module = params.GetString("module");
            const std::string case_name = params.GetString("case");
            const std::string mode = params.GetString("mode");
            const std::string route = params.GetString("route");
            const std::string report = params.GetString("report");
            const bool debug = params.GetBool("debug", false);
            std::ostringstream request_body;
            request_body << "{"
                         << "\"flow_id\":\"" << codex_lan_agent::JsonEscape(flow_id) << "\"";
            if (!params_json.empty()) {
                request_body << ",\"params_json\":\"" << codex_lan_agent::JsonEscape(params_json) << "\"";
            }
            if (!test_statement.empty()) {
                request_body << ",\"test_statement\":\"" << codex_lan_agent::JsonEscape(test_statement) << "\"";
            }
            if (!test_text.empty()) {
                request_body << ",\"test_text\":\"" << codex_lan_agent::JsonEscape(test_text) << "\"";
            }
            if (!input_text.empty()) {
                request_body << ",\"input_text\":\"" << codex_lan_agent::JsonEscape(input_text) << "\"";
            }
            if (!arguments_text.empty()) {
                request_body << ",\"arguments_text\":\"" << codex_lan_agent::JsonEscape(arguments_text) << "\"";
            }
            if (!args.empty()) {
                request_body << ",\"args\":\"" << codex_lan_agent::JsonEscape(args) << "\"";
            }
            if (!script.empty()) {
                request_body << ",\"script\":\"" << codex_lan_agent::JsonEscape(script) << "\"";
            }
            if (!script_path.empty()) {
                request_body << ",\"script_path\":\"" << codex_lan_agent::JsonEscape(script_path) << "\"";
            }
            if (!script_dir.empty()) {
                request_body << ",\"script_dir\":\"" << codex_lan_agent::JsonEscape(script_dir) << "\"";
            }
            if (!script_directory.empty()) {
                request_body << ",\"script_directory\":\"" << codex_lan_agent::JsonEscape(script_directory) << "\"";
            }
            if (!kind.empty()) {
                request_body << ",\"kind\":\"" << codex_lan_agent::JsonEscape(kind) << "\"";
            }
            if (!layer.empty()) {
                request_body << ",\"layer\":\"" << codex_lan_agent::JsonEscape(layer) << "\"";
            }
            if (!module.empty()) {
                request_body << ",\"module\":\"" << codex_lan_agent::JsonEscape(module) << "\"";
            }
            if (!case_name.empty()) {
                request_body << ",\"case\":\"" << codex_lan_agent::JsonEscape(case_name) << "\"";
            }
            if (!mode.empty()) {
                request_body << ",\"mode\":\"" << codex_lan_agent::JsonEscape(mode) << "\"";
            }
            if (!route.empty()) {
                request_body << ",\"route\":\"" << codex_lan_agent::JsonEscape(route) << "\"";
            }
            if (!report.empty()) {
                request_body << ",\"report\":\"" << codex_lan_agent::JsonEscape(report) << "\"";
            }
            if (debug) {
                request_body << ",\"debug\":true";
            }
            const std::string trace_id = params.GetString("trace_id");
            if (!trace_id.empty()) {
                request_body << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
            }
            const std::string goal_id = params.GetString("goal_id");
            if (!goal_id.empty()) {
                request_body << ",\"goal_id\":\"" << codex_lan_agent::JsonEscape(goal_id) << "\"";
            }
            request_body << "}";
            return RunCxParserFlowResult(
                config,
                flow_id,
                request_body.str(),
                trace_id,
                goal_id);
        }},
        {"lan_agent_discover_ctest_tests", [](const AgentConfig & config, const JsonRequestView & params) {
            const std::string config_name = params.GetString("config", "Release");
            return DiscoverCtestTestsResult(
                config,
                params.GetString("build_dir"),
                config_name.empty() ? "Release" : config_name,
                params.GetString("test_regex"),
                std::max(0, params.GetInt("start_index", 0)),
                std::max(1, params.GetInt("max_entries", 200)));
        }},
        {"local_cli", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildLocalCliRegistryResult(config, params, "local_cli");
        }},
        {"codex_local_cli", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildLocalCliRegistryResult(config, params, "codex_local_cli");
        }},
        {"lan_agent_run_command", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildLocalCliRegistryResult(config, params, "lan_agent_run_command");
        }},
        {"lan_agent_preflight_build_target", [](const AgentConfig &, const JsonRequestView & params) {
            std::string config_name = params.GetString("config", "Release");
            if (config_name.empty()) {
                config_name = "Release";
            }
            CommandResult result;
            const std::string build_dir = params.GetString("build_dir");
            const std::string target = params.GetString("target");
            if (build_dir.empty() || target.empty()) {
                LanResultBuilder(&result).Error(400, "build_dir and target are required");
                result.fields["result"] = "preflight_blocked";
                result.fields["preflight_scope"] = "cxparser_preflight_guard";
                result.fields["preflight_status"] = "blocked";
                result.fields["preflight_reason_code"] = "missing_required_args";
                result.fields["summary"] = "build target preflight blocked";
                result.fields["next_action"] = "provide build_dir and target";
                return result;
            }
            return BuildBuildTargetPreflightResult(build_dir, target, config_name);
        }},
        {"lan_agent_build_target", [](const AgentConfig & config, const JsonRequestView & params) {
            std::string config_name = params.GetString("config", "Release");
            if (config_name.empty()) {
                config_name = "Release";
            }
            const std::string build_dir = params.GetString("build_dir");
            const std::string target = params.GetString("target");
            const bool dry_run = params.GetBool("dry_run", false);
            std::string preflight_ref = params.GetString("preflight_ref");
            std::string preflight_status = params.GetString("preflight_status");
            bool auto_preflight_performed = false;
            std::string auto_preflight_reason_code;
            if (!dry_run
                && preflight_ref.empty()
                && preflight_status.empty()
                && !Trim(build_dir).empty()
                && !Trim(target).empty()) {
                CommandResult preflight = BuildBuildTargetPreflightResult(build_dir, target, config_name);
                preflight_ref = GetFieldOrDefault(preflight, "preflight_ref", "");
                preflight_status = GetFieldOrDefault(preflight, "preflight_status", "");
                auto_preflight_reason_code = GetFieldOrDefault(preflight, "preflight_reason_code", "");
                auto_preflight_performed = preflight_status == "ready" && !preflight_ref.empty();
            }
            const bool has_stall_timeout = !params.GetRawJson("stall_timeout_sec").empty();
            const int stall_timeout_sec = params.GetInt(
                "stall_timeout_sec",
                config.build_target_stall_timeout_sec);
            CommandResult result = BuildLocalCmakeTargetResult(
                config,
                build_dir,
                target,
                config_name,
                std::max(1, params.GetInt("timeout_sec", 1800)),
                std::max(0, stall_timeout_sec),
                dry_run,
                params.GetString("codex_llvm_root"));
            result.fields["build_target_stall_timeout_sec"] =
                std::to_string(std::max(0, stall_timeout_sec));
            result.fields["build_target_stall_timeout_source"] =
                has_stall_timeout ? "request" : "config";
            if (!preflight_ref.empty()) {
                result.fields["preflight_ref"] = preflight_ref;
                result.fields["preflight_status"] = "ready";
            } else if (!preflight_status.empty()) {
                result.fields["preflight_status"] = preflight_status;
            }
            result.fields["preflight_auto_performed"] = auto_preflight_performed ? "true" : "false";
            result.fields["preflight_auto_reason_code"] = auto_preflight_reason_code;
            return result;
        }},
        {"lan_agent_preflight_run_ctest_target", [](const AgentConfig & config, const JsonRequestView & params) {
            std::string config_name = params.GetString("config", "Release");
            if (config_name.empty()) {
                config_name = "Release";
            }
            return BuildRunCtestPreflightResult(
                config,
                params.GetString("build_dir"),
                config_name,
                params.GetString("test_regex"));
        }},
        {"lan_agent_run_ctest_target", [](const AgentConfig & config, const JsonRequestView & params) {
            CommandResult result;
            if (g_task_manager == nullptr) {
                LanResultBuilder(&result).Error(41, "task manager is not active");
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
                LanResultBuilder(&result).Error(
                    400,
                    !preflight_ref.empty() && parsed_preflight_ref
                        ? "preflight_ref did not contain replayable build_dir/test_regex; rerun preflight or provide explicit args"
                        : "build_dir and test_regex are required");
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
        }},
        {"lan_agent_clips_decide", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildClipsDecisionResult(config, params);
        }},
        {"lan_agent_clips_chain_template", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildClipsChainTemplateResult(config, params);
        }},
        {"lan_agent_mcp_flow_visualize", [](const AgentConfig & config, const JsonRequestView & params) {
            return codex_lan_agent::BuildMcpFlowVisualizeResult(
                config,
                params.GetString("input_jsonl", params.GetString("jsonl_path")),
                params.GetString("out_dir", params.GetString("output_dir")));
        }},
        {"lan_agent_mcp_flow_analyze", [](const AgentConfig & config, const JsonRequestView & params) {
            return codex_lan_agent::BuildMcpFlowAnalyzeResult(
                config,
                params.GetString("input_jsonl", params.GetString("jsonl_path")),
                params.GetString("rule_root"),
                params.GetString("out_root", params.GetString("out_dir", params.GetString("output_dir"))));
        }},
        {"lan_agent_mcp_flow_export", [](const AgentConfig & config, const JsonRequestView & params) {
            return codex_lan_agent::BuildMcpFlowExportResult(
                config,
                params.GetString("input_jsonl", params.GetString("jsonl_path")),
                params.GetString("rule_root"),
                params.GetString("out_root", params.GetString("out_dir", params.GetString("output_dir"))));
        }},
        {"lan_agent_mcp_boundary_explore", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildMcpBoundaryExploreResult(
                config,
                params.GetString("out_dir", params.GetString("output_dir")),
                params.GetBool("include_synthetic_flow", true));
        }},
        {"lan_agent_mcp_flow_conformance_check", [](const AgentConfig & config, const JsonRequestView & params) {
            return codex_lan_agent::BuildMcpFlowConformanceCheckResult(
                config,
                params.GetString("input_jsonl", params.GetString("jsonl_path")),
                params.GetString("out_dir", params.GetString("output_dir")));
        }},
        {"lan_agent_mcp_guard_regression_acceptance", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildMcpGuardRegressionAcceptanceResult(
                config,
                params.GetString("input_jsonl", params.GetString("jsonl_path")),
                params.GetString("out_dir", params.GetString("output_dir")));
        }},
        {"lan_agent_flow_task_list", [](const AgentConfig & config, const JsonRequestView & params) {
            const std::string flow_id = params.GetString(
                "flow_id",
                "directory_comment_cleanup_bounded_window_v1");
            if (flow_id == "directory_comment_cleanup_bounded_window_v1") {
                return codex_lan_agent::BuildDirectoryCommentCleanupTaskListResult(
                    config,
                    FirstNonEmpty(params.GetString("goal_id"), params.GetString("trace_id"), "directory_comment_cleanup"),
                    params.GetString("trace_id"),
                    params.GetString("directory_path", params.GetString("file_path")),
                    params.GetString("current_task_id", "T1"));
            }
            CommandResult result;
            result.ok = false;
            result.exit_code = 400;
            result.fields["status"] = "failed";
            result.fields["result"] = "unsupported_flow_task_list";
            result.fields["flow_id"] = flow_id;
            result.fields["error"] = "flow_id is not supported by lan_agent_flow_task_list";
            return result;
        }},
        {"lan_agent_scan_text_ranges", [](const AgentConfig & config, const JsonRequestView & params) {
            return ScanTextRangesResult(
                config,
                params.GetString("file_path"),
                params.GetString("scan_mode", "comments"),
                std::max(1, params.GetInt("max_ranges_per_call", 1)),
                std::max(0, params.GetInt("range_offset", 0)),
                params.GetString("trace_id"),
                params.GetString("probe_ref"));
        }},
        {"lan_agent_delete_next_text_range_atomic", [](const AgentConfig & config, const JsonRequestView & params) {
            return DeleteNextTextRangeAtomicResult(
                config,
                params.GetString("file_path"),
                params.GetString("scan_mode", "comments"),
                params.GetString("primary_intent", "comment_cleanup"),
                params.GetString("trace_id"),
                params.GetString("probe_ref"));
        }},
        {"lan_agent_delete_text_range_window_atomic", [](const AgentConfig & config, const JsonRequestView & params) {
            const std::string file_path = params.GetString("file_path", params.GetString("next_file_path"));
            const std::string probe_ref = FirstNonEmpty(
                params.GetString("probe_ref"),
                params.GetString("next_probe_ref"),
                params.GetBool("probe_ready", false) || params.GetBool("next_probe_ready", false)
                    ? file_path
                    : std::string(),
                "");
            return DeleteTextRangeWindowAtomicResult(
                config,
                file_path,
                params.GetString("scan_mode", params.GetString("next_scan_mode", "comments")),
                std::max(1, params.GetInt("start_line", params.GetInt("next_start_line", 1))),
                std::min(200, std::max(1, params.GetInt("max_lines", params.GetInt("next_max_lines", 200)))),
                params.GetString("primary_intent", params.GetString("next_primary_intent", "comment_cleanup")),
                params.GetString("trace_id"),
                probe_ref,
                params.GetString("directory_manifest_path"),
                params.GetInt("directory_current_file_index", 0),
                params.GetInt("directory_total_code_file_count", 0));
        }},
        {"lan_agent_prepare_edit_windows", [](const AgentConfig & config, const JsonRequestView & params) {
            return PrepareEditWindowsResult(
                config,
                params.GetString("file_path"),
                params.GetString("ranges_json"),
                std::max(0, params.GetInt("context_before", 6)),
                std::max(0, params.GetInt("context_after", 6)),
                std::max(1, params.GetInt("max_windows_per_call", 1)),
                std::max(0, params.GetInt("window_offset", 0)),
                static_cast<std::size_t>(std::max(256, params.GetInt("max_window_chars", 12000))),
                params.GetString("trace_id"),
                params.GetString("probe_ref"));
        }},
        {"lan_agent_probe_text_file", [](const AgentConfig & config, const JsonRequestView & params) {
            return ProbeTextFileResult(
                config,
                params.GetString("file_path"),
                params.GetString("primary_intent"),
                params.GetString("trace_id"),
                params.GetString("directory_manifest_path"),
                params.GetInt("directory_current_file_index", 0),
                params.GetInt("directory_total_code_file_count", 0));
        }},
        {"lan_agent_read_text_file", [](const AgentConfig & config, const JsonRequestView & params) {
            const long long raw_start_byte_offset =
                static_cast<long long>(params.GetInt("start_byte_offset", 0));
            CommandResult result = ReadTextFileResult(
                config,
                params.GetString("file_path"),
                std::max(1, params.GetInt("max_lines", 500)),
                std::max(1, params.GetInt("start_line", 1)),
                params.GetString("trace_id"),
                raw_start_byte_offset > 0
                    ? static_cast<std::size_t>(raw_start_byte_offset)
                    : static_cast<std::size_t>(0),
                params.GetString("probe_ref"));
            if (!params.GetBool("read_to_eof", false)) {
                result.fields["source_has_more"] = GetFieldOrDefault(result, "has_more", "false");
                result.fields["source_next_start_line"] = GetFieldOrDefault(result, "next_start_line", "");
                result.fields["bounded_read_complete"] = "true";
                result.fields["read_to_eof"] = "false";
                result.fields["has_more"] = "false";
                result.fields["read_complete"] = "true";
                result.fields["file_complete"] = "true";
                result.fields["read_status"] = "complete";
                result.fields["task_completion"] = "complete";
                result.fields["content_read_completion"] = "complete";
                result.fields["page_status"] = "bounded_page";
                result.fields["requires_followup"] = "false";
                result.fields["continue_required"] = "false";
                result.fields["auto_continue_required"] = "false";
                result.fields["semantic_model_clamp"] = "none";
                result.fields["terminal_state"] = "true";
                result.fields["final_answer_allowed"] = "true";
                result.fields["completion_claim_allowed"] = "true";
                result.fields["assistant_response_allowed"] = "true";
                result.fields["verification_ok"] = "true";
                result.fields["analysis_allowed"] = "true";
                result.fields["analysis_blocked_reason"] = "";
                result.fields["next_start_line"] = "";
                result.fields["next_tool_name"] = "";
                result.fields["required_tool_name"] = "";
                result.fields["next_call_json"] = "";
                result.fields["required_tool_arguments_json"] = "";
                result.fields["next_action"] = "bounded file page read is complete";
            }
            return result;
        }},
        {"lan_agent_tail_text_file", [](const AgentConfig & config, const JsonRequestView & params) {
            return TailTextFileResult(
                config,
                params.GetString("file_path"),
                std::max(1, params.GetInt("max_lines", 120)));
        }},
        {"lan_agent_list_directory", [](const AgentConfig & config, const JsonRequestView & params) {
            return ListDirectoryResult(
                config,
                params.GetString("directory_path"),
                std::max(1, params.GetInt("max_entries", 200)),
                params.GetString("trace_id"),
                params.GetString("primary_intent"));
        }},
        {"lan_agent_read_directory_files", [](const AgentConfig & config, const JsonRequestView & params) {
            const long long raw_start_byte_offset =
                static_cast<long long>(params.GetInt("start_byte_offset", 0));
            return ReadDirectoryFilesResult(
                config,
                params.GetString("directory_path"),
                params.GetString("file_extensions_csv"),
                std::max(1, params.GetInt("max_files", 200)),
                std::max(1, params.GetInt("max_lines_per_file", 500)),
                std::max(1, params.GetInt("max_files_per_call", 5)),
                std::max(1, params.GetInt("max_total_lines", 2500)),
                std::max(0, params.GetInt("file_index", 0)),
                std::max(1, params.GetInt("start_line", 1)),
                params.GetString("trace_id"),
                raw_start_byte_offset > 0
                    ? static_cast<std::size_t>(raw_start_byte_offset)
                    : static_cast<std::size_t>(0));
        }},
        {"lan_agent_record_dialog_slice", [](const AgentConfig & config, const JsonRequestView & params) {
            const std::string record_user_text = FirstNonEmpty(
                params.GetString("business_user_text"),
                params.GetString("user_text"),
                "");
            const std::string record_assistant_text = FirstNonEmpty(
                params.GetString("business_assistant_text"),
                params.GetString("assistant_text"),
                "");
            const std::string record_business_summary = FirstNonEmpty(

                params.GetString("slice_summary"),
                params.GetString("summary"),
                params.GetString("business_summary"));
            return RecordDialogSliceResult(
                config,
                params.GetString("session_id"),
                params.GetString("turn_id"),
                record_user_text,
                record_assistant_text,
                record_business_summary,
                params.GetString("tags"),
                params.GetString("task_id"),
                params.GetString("provider_id"),
                params.GetString("capability_id"),
                params.GetString("source_type"),
                params.GetString("write_mode"),
                params.GetString("reasoning_level"),
                params.GetString("primary_intent"),
                params.GetString("confidence"),
                params.GetString("result_ref"),
                params.GetString("evidence_ref"));
        }},
        {"lan_agent_search_text", [](const AgentConfig & config, const JsonRequestView & params) {
            return SearchTextResult(
                config,
                FirstNonEmpty(
                    params.GetString("directory_path"),
                    params.GetString("file_path"),
                    std::string()),
                FirstNonEmpty(
                    params.GetString("query_text"),
                    params.GetString("text"),
                    params.GetString("anchor_text"),
                    ExtractSearchQueryTextFromRequest(params.GetString("request_text"))),
                params.GetBool("recursive", true),
                params.GetBool("show_preview", false),
                std::max(1, params.GetInt("max_matches", 100)),
                params.GetString("trace_id"));
        }},
        {"lan_agent_optfile_read", [](const AgentConfig & config, const JsonRequestView & params) {
            return OptFileReadResult(
                config,
                params.GetString("target_name"),
                std::max(1, params.GetInt("max_bytes", 65536)));
        }},
        {"lan_agent_optfile_write_preview", [](const AgentConfig & config, const JsonRequestView & params) {
            CommandResult payload_result;
            const std::string data = ResolveTextPayloadFromParams(
                params,
                "data",
                "data_base64",
                &payload_result);
            if (!payload_result.ok) {
                return payload_result;
            }
            CommandResult result = OptFileWritePreviewResult(
                config,
                params.GetString("target_name"),
                data,
                params.GetBool("append", false));
            result.fields["content_transport"] = GetFieldOrDefault(
                payload_result,
                "content_transport",
                "json_string");
            return result;
        }},
        {"lan_agent_optfile_apply_write", [](const AgentConfig & config, const JsonRequestView & params) {
            CommandResult payload_result;
            const std::string data = ResolveTextPayloadFromParams(
                params,
                "data",
                "data_base64",
                &payload_result);
            if (!payload_result.ok) {
                return payload_result;
            }
            CommandResult result = OptFileApplyWriteResult(
                config,
                params.GetString("target_name"),
                data,
                params.GetBool("append", false));
            result.fields["content_transport"] = GetFieldOrDefault(
                payload_result,
                "content_transport",
                "json_string");
            return result;
        }},
        {"lan_agent_find_line_metadata", [](const AgentConfig & config, const JsonRequestView & params) {

            return FindLineMetadataResult(
                config,
                params.GetString("file_path"),
                params.GetInt("line", params.GetInt("line_number", 0)),
                params.GetBool("show_preview", false));
        }},
        {"lan_agent_find_content_matches", [](const AgentConfig & config, const JsonRequestView & params) {
            return FindContentMatchesResult(
                config,
                params.GetString("file_path"),
                FirstNonEmpty(
                    params.GetString("anchor_text"),
                    params.GetString("query_text"),
                    params.GetString("text"),
                    params.GetString("anchor"),
                    std::string()),
                params.GetBool("show_preview", false),
                std::max(1, params.GetInt("fuzzy_threshold", 60)));
        }},
        {"lan_agent_locate_text_lines", [](const AgentConfig & config, const JsonRequestView & params) {
            return LocateTextLinesResult(
                config,
                params.GetString("file_path"),
                FirstNonEmpty(
                    params.GetString("anchor_text"),
                    params.GetString("query_text"),
                    params.GetString("text"),
                    params.GetString("anchor"),
                    std::string()),
                params.GetBool("show_preview", false),
                std::max(1, params.GetInt("fuzzy_threshold", 60)));
        }},
        {"lan_agent_delete_line_atomic", [](const AgentConfig & config, const JsonRequestView & params) {
            return DeleteLineAtomicResult(
                config,
                params.GetString("file_path"),
                params.GetInt("line", params.GetInt("line_number", 0)),
                params.GetString("expected_line_hash"),
                params.GetString("request_id"),
                params.GetString("trace_id"));
        }},
        {"lan_agent_delete_content_atomic", [](const AgentConfig & config, const JsonRequestView & params) {
            return DeleteContentAtomicResult(
                config,
                params.GetString("file_path"),
                FirstNonEmpty(
                    params.GetString("anchor_text"),
                    params.GetString("query_text"),
                    params.GetString("text"),
                    params.GetString("anchor"),
                    std::string()),
                std::max(1, params.GetInt("occurrence", 1)),
                FirstNonEmpty(
                    params.GetString("expected_anchor_hash"),
                    params.GetString("expected_anchor_line_hash"),
                    std::string()),
                params.GetString("request_id"),
                params.GetString("trace_id"));
        }},
        {"lan_agent_insert_after_anchor_atomic", [](const AgentConfig & config, const JsonRequestView & params) {
            CommandResult payload_result;
            std::string insert_text = ResolveTextPayloadFromParams(
                params,
                "insert_text",
                "insert_text_base64",
                &payload_result);
            if (!payload_result.ok) {
                return payload_result;
            }
            if (insert_text.empty()) {
                insert_text = ResolveTextPayloadFromParams(
                    params,
                    "new_content",
                    "new_content_base64",
                    &payload_result);
            }
            if (!payload_result.ok) {
                return payload_result;
            }
            if (insert_text.empty()) {
                insert_text = ResolveTextPayloadFromParams(
                    params,
                    "content",
                    "content_base64",
                    &payload_result);
            }
            if (!payload_result.ok) {
                return payload_result;
            }
            if (insert_text.empty()) {
                insert_text = ResolveTextPayloadFromParams(
                    params,
                    "text",
                    "text_base64",
                    &payload_result);
            }
            if (!payload_result.ok) {
                return payload_result;
            }
            if (insert_text.empty()) {
                insert_text = ResolveTextPayloadFromParams(
                    params,
                    "line_roi",
                    "line_roi_base64",
                    &payload_result);
            }
            if (!payload_result.ok) {
                return payload_result;
            }
            if (insert_text.empty()) {
                insert_text = ResolveTextPayloadFromParams(
                    params,
                    "replacement_text",
                    "replacement_text_base64",
                    &payload_result);
            }
            if (!payload_result.ok) {
                return payload_result;
            }
            CommandResult result = InsertAfterAnchorAtomicResult(
                config,
                params.GetString("file_path"),
                FirstNonEmpty(
                    params.GetString("anchor_text"),
                    params.GetString("anchor"),
                    std::string()),
                std::max(1, params.GetInt("occurrence", 1)),
                insert_text,
                FirstNonEmpty(
                    params.GetString("expected_anchor_hash"),
                    params.GetString("expected_anchor_line_hash"),
                    std::string()),
                params.GetString("request_id"),
                params.GetString("trace_id"));
            result.fields["content_transport"] = GetFieldOrDefault(payload_result, "content_transport", "json_string");
            result.fields["content_base64_bytes"] = GetFieldOrDefault(payload_result, "content_base64_bytes", "");
            return result;
        }},
        {"lan_agent_replace_line_range_atomic", [](const AgentConfig & config, const JsonRequestView & params) {
            const bool has_supported_payload =
                !params.GetRawJson("replacement_text").empty()
                || !params.GetRawJson("replacement_text_base64").empty()
                || !params.GetRawJson("new_content").empty()
                || !params.GetRawJson("new_content_base64").empty()
                || !params.GetRawJson("content").empty()
                || !params.GetRawJson("content_base64").empty()
                || !params.GetRawJson("text").empty()
                || !params.GetRawJson("text_base64").empty()
                || !params.GetRawJson("line_roi").empty()
                || !params.GetRawJson("line_roi_base64").empty();
            CommandResult payload_result;
            if (!has_supported_payload) {
                payload_result.ok = false;
                payload_result.exit_code = 400;
                payload_result.fields["error"] = "replacement payload is required";
                payload_result.fields["result"] = "missing_replacement_payload";
                payload_result.fields["accepted_payload_fields"] =
                    "replacement_text,new_content,content,text,line_roi and matching *_base64 fields";
                payload_result.fields["destructive_fallback_blocked"] = "true";
                return payload_result;
            }

            std::string replacement_text = ResolveTextPayloadFromParams(
                params,
                "replacement_text",
                "replacement_text_base64",
                &payload_result);
            if (!payload_result.ok) {
                return payload_result;
            }
            if (replacement_text.empty()) {
                replacement_text = ResolveTextPayloadFromParams(
                    params,
                    "new_content",
                    "new_content_base64",
                    &payload_result);
            }
            if (!payload_result.ok) {
                return payload_result;
            }
            if (replacement_text.empty()) {
                replacement_text = ResolveTextPayloadFromParams(
                    params,
                    "content",
                    "content_base64",
                    &payload_result);
            }
            if (!payload_result.ok) {
                return payload_result;
            }
            if (replacement_text.empty()) {
                replacement_text = ResolveTextPayloadFromParams(
                    params,
                    "text",
                    "text_base64",
                    &payload_result);
            }
            if (!payload_result.ok) {
                return payload_result;
            }
            if (replacement_text.empty()) {
                replacement_text = ResolveTextPayloadFromParams(
                    params,
                    "line_roi",
                    "line_roi_base64",
                    &payload_result);
            }
            if (!payload_result.ok) {
                return payload_result;
            }
            if (replacement_text.empty() && !params.GetBool("allow_empty_content", false)) {
                payload_result.ok = false;
                payload_result.exit_code = 400;
                payload_result.fields["error"] =
                    "empty replacement payload requires allow_empty_content=true";
                payload_result.fields["result"] = "empty_replacement_payload_blocked";
                payload_result.fields["destructive_fallback_blocked"] = "true";
                payload_result.fields["recommended_delete_tool"] =
                    "lan_agent_delete_text_range_window_atomic";
                return payload_result;
            }

            CommandResult result = ReplaceLineRangeAtomicResult(
                config,
                params.GetString("file_path"),
                params.GetInt("start_line", params.GetInt("replace_start_line", 0)),
                params.GetInt("end_line", params.GetInt("replace_end_line", 0)),
                replacement_text,
                FirstNonEmpty(
                    params.GetString("expected_range_hash"),
                    params.GetString("range_hash"),
                    std::string()),
                params.GetString("request_id"),
                params.GetString("trace_id"));
            result.fields["content_transport"] = GetFieldOrDefault(payload_result, "content_transport", "json_string");
            result.fields["content_base64_bytes"] = GetFieldOrDefault(payload_result, "content_base64_bytes", "");
            result.fields["empty_replacement_explicitly_allowed"] =
                replacement_text.empty() ? "true" : "false";
            return result;
        }},
        {"lan_agent_write_text_file", [](const AgentConfig & config, const JsonRequestView & params) {
            CommandResult payload_result;
            const std::string content = ResolveTextPayloadFromParams(
                params,
                "content",
                "content_base64",
                &payload_result);
            if (!payload_result.ok) {
                return payload_result;
            }
            const std::string expected_file_hash = params.GetString("expected_file_hash");
            CommandResult result = expected_file_hash.empty()
                ? WriteTextFileResult(
                    config,
                    params.GetString("file_path"),
                    content,
                    params.GetBool("append", false))
                : WriteWholeTextViaOptfileResult(
                    config,
                    params.GetString("file_path"),
                    content,
                    params.GetBool("append", false),
                    expected_file_hash);
            result.fields["content_transport"] = GetFieldOrDefault(payload_result, "content_transport", "json_string");
            result.fields["content_base64_bytes"] = GetFieldOrDefault(payload_result, "content_base64_bytes", "");
            return result;
        }},
        {"lan_agent_append_text_file", [](const AgentConfig & config, const JsonRequestView & params) {
            CommandResult payload_result;
            const std::string content = ResolveTextPayloadFromParams(
                params,
                "content",
                "content_base64",
                &payload_result);
            if (!payload_result.ok) {
                return payload_result;
            }
            if (content.empty()) {
                payload_result.ok = false;
                payload_result.exit_code = 400;
                payload_result.fields["error"] = "append content is required";
                payload_result.fields["result"] = "missing_append_payload";
                payload_result.fields["destructive_fallback_blocked"] = "true";
                return payload_result;
            }
            CommandResult result = WriteTextFileResult(
                config,
                params.GetString("file_path"),
                content,
                true);
            result.fields["content_transport"] = GetFieldOrDefault(payload_result, "content_transport", "json_string");
            result.fields["content_base64_bytes"] = GetFieldOrDefault(payload_result, "content_base64_bytes", "");
            return result;
        }},
        {"lan_agent_apply_single_file_patch", [](const AgentConfig & config, const JsonRequestView & params) {
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
        }},
        {"lan_agent_apply_diff_patch", [](const AgentConfig & config, const JsonRequestView & params) {
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
        }},
        {"lan_agent_revert_single_file_patch", [](const AgentConfig & config, const JsonRequestView & params) {
            return RevertSingleFilePatchResult(
                config,
                params.GetString("file_path"),
                params.GetString("backup_path"),
                params.GetString("request_id"),
                params.GetString("trace_id"),
                params.GetString("patch_id"),
                params.GetString("reason"));
        }},
        {"lan_agent_format_code_file", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::codex_lan_agent::BuildFormatCodeFileResult(config, params);
        }},
        {"lan_agent_ensure_directory", [](const AgentConfig & config, const JsonRequestView & params) {
            return EnsureDirectoryResult(
                config,
                params.GetString("directory_path"),
                params.GetString("file_path"),
                params.GetBool("ensure_parent", false));
        }},
        {"lan_agent_get_supervision_status", [](const AgentConfig & config, const JsonRequestView & params) {
            return GetSupervisionStatusResult(
                config,
                params.GetString("trace_id"),
                params.GetString("goal_id"));
        }},
        {"lan_agent_cmm_index_repository", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmIndexRepositoryResult(config, params);
        }},
        {"lan_agent_cmm_search_code", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmSearchCodeResult(config, params);
        }},
        {"lan_agent_cmm_search_graph", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmSearchGraphResult(config, params);
        }},
        {"lan_agent_cmm_query_graph", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmQueryGraphResult(config, params);
        }},
        {"lan_agent_cmm_trace_path", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmTracePathResult(config, params);
        }},
        {"lan_agent_cmm_get_code_snippet", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmGetCodeSnippetResult(config, params);
        }},
        {"lan_agent_cmm_get_graph_schema", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmGetGraphSchemaResult(config, params);
        }},
        {"lan_agent_cmm_get_architecture", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmGetArchitectureResult(config, params);
        }},
        {"lan_agent_cmm_list_projects", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmListProjectsResult(config, params);
        }},
        {"lan_agent_cmm_index_status", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmIndexStatusResult(config, params);
        }},
        {"lan_agent_cmm_detect_changes", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmDetectChangesResult(config, params);
        }},
        {"lan_agent_cmm_delete_project", [](const AgentConfig & config, const JsonRequestView & params) {
            return BuildCmmDeleteProjectResult(config, params);
        }},
        {"lan_agent_run_clang_ast_parser", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::BuildRunClangAstParserResult(config, params);
        }},
        {"lan_agent_build_cfg", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::BuildRunCfgResult(config, params);
        }},
        {"lan_agent_query_cfg_artifact", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::BuildQueryCfgArtifactResult(config, params);
        }},
        {"lan_agent_build_call_graph", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::BuildRunCallGraphResult(config, params);
        }},
        {"lan_agent_build_dfg", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::BuildRunDfgResult(config, params);
        }},
        {"lan_agent_query_call_graph_artifact", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::BuildQueryCallGraphArtifactResult(config, params);
        }},
        {"lan_agent_query_dfg_artifact", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::BuildQueryDfgArtifactResult(config, params);
        }},
        {"lan_agent_build_program_slice", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::BuildRunProgramSliceResult(config, params);
        }},
        {"lan_agent_query_program_slice_artifact", [](const AgentConfig & config, const JsonRequestView & params) {
            return ::BuildQueryProgramSliceArtifactResult(config, params);
        }}
    };
    return handlers;
}

bool TryHandleRegisteredMcpTool(
    const AgentConfig & config,
    const std::string & tool_name,
    const JsonRequestView & params,
    CommandResult * result) {
    const auto & handlers = BuildMcpToolHandlerRegistry();
    const auto it = handlers.find(tool_name);
    if (it == handlers.end()) {
        if (::codex_lan_agent::remote_mcp_bridge::TryHandleRemoteMcpTool(config, tool_name, params, result)) {
            if (result != nullptr) {
                LanResultBuilder(result).Finalize(config, tool_name);
            }
            return true;
        }
        return false;
    }
    if (result != nullptr) {
        *result = it->second(config, params);
        LanResultBuilder(result).Finalize(config, tool_name);
    }
    return true;
}

struct RequestRule {
    const char * tool_name;
    const char * request_type;
    const char * trigger;
    const char * risk;
    const char * tags;
};

const std::vector<RequestRule> & GetRequestRules() {
    static const std::vector<RequestRule> rules = {
        {"lan_agent_mcp_route", "mcp_gateway_route", "mcp_route", "medium", "mcp,gateway,router,chat_layer,single_entry"},
        {"lan_agent_health", "read_observe", "health", "low", "runtime,health,read_only"},
        {"lan_agent_runtime_overview", "read_observe", "runtime_overview", "low", "runtime,overview,read_only"},
        {"lan_agent_remote_session_overview", "read_observe", "remote_session_overview", "low", "remote-session,overview,read_only"},
        {"lan_agent_task_overview", "read_observe", "task_overview", "low", "task,overview,read_only"},
        {"lan_agent_event_overview", "read_observe", "event_overview", "low", "event,overview,read_only"},
        {"lan_agent_mcp_overview", "read_observe", "mcp_overview", "low", "mcp,overview,read_only"},
        {"lan_agent_rag_overview", "read_observe", "rag_overview", "low", "rag,overview,read_only"},
        {"lan_agent_patch_overview", "read_observe", "patch_overview", "low", "patch,audit,overview,read_only"},
        {"lan_agent_browser_list_overview", "read_observe", "browser_list_overview", "low", "browser,overview,read_only"},
        {"lan_agent_rag_index_status", "rag_bridge_status", "rag_status", "low", "rag,bridge,status,read_only"},
        {"lan_agent_rag_clips_meta", "rag_bridge_clips_meta", "rag_clips_meta", "medium", "rag,bridge,clips,meta"},
        {"lan_agent_rag_clips_run", "rag_bridge_clips_run", "rag_clips_run", "medium", "rag,bridge,clips,run,store_refs"},
        {"lan_agent_rag_storage_lookup", "rag_bridge_storage_lookup", "rag_storage_lookup", "low", "rag,bridge,storage,lookup,read_only"},
        {"lan_agent_rag_review_observe", "rag_bridge_review_observe", "rag_review_observe", "medium", "rag,bridge,review,observe,write,store_refs"},
        {"lan_agent_rag_storage_page", "rag_bridge_storage_page", "rag_storage_page", "low", "rag,bridge,storage,page,read_only"},
        {"lan_agent_task_memory_freeze", "task_memory_write", "task_memory_freeze", "medium", "task-memory,resume-context,write,audited"},
        {"lan_agent_task_memory_append_step", "task_memory_write", "task_memory_append_step", "medium", "task-memory,step-ledger,write,audited"},
        {"lan_agent_task_memory_execute_continuation_budget", "task_memory_write", "task_memory_execute_continuation_budget", "medium", "task-memory,continuation-budget,step-ledger,write,audited"},
        {"lan_agent_task_memory_build_kv_snapshot", "task_memory_write", "task_memory_build_kv_snapshot", "medium", "task-memory,kv-snapshot,index,write,audited"},
        {"lan_agent_task_memory_kv_lookup", "task_memory_read", "task_memory_kv_lookup", "low", "task-memory,kv-snapshot,index,lookup,read_only"},
        {"lan_agent_task_memory_rocksdb_mirror", "task_memory_write", "task_memory_rocksdb_mirror", "medium", "task-memory,rocksdb,native,mirror,write,audited"},
        {"lan_agent_task_memory_rocksdb_lookup", "task_memory_read", "task_memory_rocksdb_lookup", "low", "task-memory,rocksdb,native,lookup,read_only"},
        {"lan_agent_task_memory_rocksdb_parity_check", "task_memory_read", "task_memory_rocksdb_parity_check", "low", "task-memory,rocksdb,parity,read_only"},
        {"lan_agent_task_memory_migration_assess", "task_memory_read", "task_memory_migration_assess", "low", "task-memory,migration,backend-readiness,read_only"},
        {"lan_agent_task_memory_structure_manifest", "task_memory_write", "task_memory_structure_manifest", "medium", "task-memory,structure,manifest,write,audited"},
        {"lan_agent_task_memory_resume_context", "task_memory_read", "task_memory_resume_context", "low", "task-memory,resume-context,read_only"},
        {"lan_agent_remote_session_semantic_catalog", "remote_session_semantic_catalog", "remote_session_semantic_catalog", "low", "remote-session,semantic-catalog,read_only"},
        {"lan_agent_semantic_grid_ingest_text", "semantic_grid_ingest", "semantic_grid_ingest_text", "low", "semantic-grid,ingest,text,read_only"},
        {"lan_agent_semantic_grid_build", "semantic_grid_build", "semantic_grid_build", "medium", "semantic-grid,build,artifact"},
        {"lan_agent_semantic_grid_query", "semantic_grid_query", "semantic_grid_query", "low", "semantic-grid,query,read_only"},
        {"lan_agent_semantic_grid_trace_source", "semantic_grid_trace", "semantic_grid_trace_source", "low", "semantic-grid,trace,read_only"},
        {"lan_agent_semantic_grid_context_bundle", "semantic_grid_context", "semantic_grid_context_bundle", "low", "semantic-grid,context,read_only"},
        {"lan_agent_semantic_grid_incremental_update", "semantic_grid_incremental_update", "semantic_grid_incremental_update", "medium", "semantic-grid,incremental,artifact"},
        {"lan_agent_list_profiles", "read_observe", "profile_catalog", "low", "profile,catalog,read_only"},
        {"lan_agent_profile_catalog", "read_observe", "profile_catalog", "low", "profile,catalog,read_only"},
        {"lan_agent_discover_ctest_tests", "ctest_discovery", "test_inventory", "low", "ctest,discover,read_only"},
        {"lan_agent_preflight_build_target", "build_preflight", "build_preflight", "low", "build,preflight,guard"},
        {"lan_agent_preflight_run_ctest_target", "ctest_preflight", "test_preflight", "low", "ctest,preflight,guard"},
        {"lan_agent_run_cli_profile", "execution_task", "cli_profile_run", "medium", "cli,profile,execute"},
        {"lan_agent_enqueue_cli_profile", "execution_task", "cli_profile_enqueue", "medium", "cli,profile,queue"},
        {"lan_agent_run_case", "execution_task", "case_run", "medium", "case,execute"},
        {"lan_agent_enqueue_case", "execution_task", "case_enqueue", "medium", "case,queue"},
        {"lan_agent_run_rag_flow", "analysis_review", "rag_flow_run", "medium", "rag,flow,analysis"},
        {"lan_agent_enqueue_rag_flow", "analysis_review", "rag_flow_enqueue", "medium", "rag,flow,queue"},
        {"lan_agent_run_local_chat", "analysis_review", "local_chat", "medium", "local-chat,analysis,evidence_injection"},
        {"lan_agent_enqueue_local_chat", "analysis_review", "local_chat_enqueue", "medium", "local-chat,analysis,queue"},
        {"rag.query", "analysis_review", "rag_query", "medium", "rag,query,analysis"},
        {"rag.diff_review", "analysis_review", "diff_review", "medium", "rag,diff,analysis"},
        {"rag.log_classify", "analysis_review", "log_classify", "low", "rag,log,analysis"},
        {"rag.basic_comm_smoke", "read_observe", "rag_basic_comm_smoke", "low", "rag,smoke,read_only"},
        {"lan_agent_basic_comm_smoke", "read_observe", "rag_basic_comm_smoke", "low", "rag,smoke,read_only"},
        {"lan_agent_ventriloquist_reply", "analysis_review", "ventriloquist_reply", "medium", "local-chat,proxy,analysis"},
        {"lan_agent_remote_session_new_turn", "state_mutation", "remote_session_new_turn", "high", "remote-session,write,audited"},
        {"lan_agent_remote_session_append_turn", "state_mutation", "remote_session_append_turn", "high", "remote-session,append,audited"},
        {"lan_agent_list_remote_sessions", "read_observe", "remote_session_list", "low", "remote-session,list,read_only"},
        {"lan_agent_list_remote_session_tasks", "read_observe", "remote_session_task_list", "low", "remote-session,task,list,read_only"},
        {"lan_agent_get_remote_session", "read_observe", "remote_session_get", "low", "remote-session,get,read_only"},
        {"lan_agent_resolve_remote_session_task_refs", "read_observe", "remote_session_task_refs", "low", "remote-session,resolve,read_only"},
        {"lan_agent_read_remote_session_slice", "read_observe", "remote_session_slice_read", "low", "remote-session,slice,read_only"},
        {"llama.observer_smoke", "read_observe", "llama_observer_smoke", "low", "llama,observer,smoke"},
        {"lan_agent_optfile_read", "read_observe", "optfile_read", "low", "optfile,read,read_only"},
        {"lan_agent_optfile_write_preview", "read_observe", "optfile_write_preview", "low", "optfile,write,preview"},
        {"lan_agent_optfile_apply_write", "state_mutation", "optfile_apply_write", "high", "optfile,write,audited"},
        {"lan_agent_record_dialog_slice", "state_mutation", "dialog_slice_record", "high", "dialog-slice,write,audited"},
        {"lan_agent_analyze_dialog_slices", "read_observe", "dialog_slice_analyze", "low", "dialog-slice,analyze,read_only"},
        {"lan_agent_allocate_remote_chat_session", "state_mutation", "remote_chat_allocate", "medium", "remote-chat,allocate,audited"},
        {"lan_agent_build_semantic_execution_card", "rag_bridge_clips_run", "semantic_execution_card", "medium", "semantic-execution,card,bridge"},
        {"router_domain_map", "read_observe", "router_domain_map", "low", "router,map,read_only"},
        {"dispatch_contract_map", "read_observe", "dispatch_contract_map", "low", "dispatch,contract,read_only"},
        {"mcp_capability_registry", "read_observe", "mcp_capability_registry", "low", "mcp,capability,read_only"},
        {"rag_memory_slice_contract", "read_observe", "rag_memory_slice_contract", "low", "rag,memory,contract,read_only"},
        {"intent_dispatch_prepare", "rag_bridge_clips_run", "intent_dispatch_prepare", "medium", "intent,dispatch,prepare,bridge"},
        {"semantic_action_map", "read_observe", "semantic_action_map", "low", "semantic-action,map,read_only"},
        {"tool_shortcuts", "read_observe", "semantic_action_map", "low", "semantic-action,map,read_only"},
        {"mcp_actions", "read_observe", "semantic_action_map", "low", "semantic-action,map,read_only"},
        {"semantic_action_resolve", "read_observe", "semantic_action_resolve", "low", "semantic-action,resolve,read_only"},
        {"semantic_action_validate", "read_observe", "semantic_action_validate", "low", "semantic-action,validate,read_only"},
        {"semantic_action_prepare", "read_observe", "semantic_action_prepare", "low", "semantic-action,prepare,read_only"},
        {"semantic_action_tool_call", "read_observe", "semantic_action_tool_call", "low", "semantic-action,tool-call-template,read_only"},
        {"lan_agent_run_ctest_target", "ctest_execution", "test_run", "medium", "ctest,verify,queued"},
        {"lan_agent_clips_decide", "clips_decision", "rule_guard", "medium", "clips,guard,decision"},
        {"lan_agent_get_supervision_status", "goal_supervision_status", "supervision_status", "low", "supervision,acceptance,read_only"},
        {"lan_agent_list_cxparser_flows", "cxparser_flow_catalog", "cxparser_flow_catalog", "low", "cxparser,flow,read_only"},
        {"lan_agent_validate_cxparser_flow", "cxparser_flow_validation", "cxparser_flow_validate", "low", "cxparser,flow,read_only"},
        {"lan_agent_run_cxparser_flow", "cxparser_flow_execution", "cxparser_flow_step", "medium", "cxparser,flow,orchestrated,supervised"},
        {"lan_agent_execute_semantic_action", "semantic_action_execution_bridge", "semantic_action_execute", "high", "semantic-action,bridge,edit,build,test,evidence"},
        {"lan_agent_clips_chain_template", "clips_chain_template", "clips_chain_template", "low", "clips,template,read_only"},
        {"lan_agent_flow_task_list", "flow_task_list", "flow_task_list", "low", "flow,task-list,read_only"},
        {"lan_agent_scan_text_ranges", "text_range_scan", "scan_text_ranges", "low", "file,scan,range,read_only"},
        {"lan_agent_find_line_metadata", "read_observe", "find_line_metadata", "low", "file,line,metadata_only"},
        {"lan_agent_find_content_matches", "read_observe", "find_content_matches", "low", "file,locate,metadata_only"},
        {"lan_agent_delete_line_atomic", "file_mutation", "delete_line_atomic", "high", "file,write,line,atomic,audited"},
        {"lan_agent_delete_content_atomic", "file_mutation", "delete_content_atomic", "high", "file,write,content,atomic,audited"},
        {"lan_agent_delete_next_text_range_atomic", "file_mutation", "delete_next_text_range_atomic", "high", "file,write,text-range,atomic,audited,single-step"},
        {"lan_agent_delete_text_range_window_atomic", "file_mutation", "delete_text_range_window_atomic", "high", "file,write,text-range,atomic,audited,bounded-window"},
        {"lan_agent_locate_text_lines", "read_observe", "locate_text_lines", "low", "file,locate,metadata_only"},
        {"lan_agent_insert_after_anchor_atomic", "file_mutation", "insert_after_anchor_atomic", "high", "file,write,anchor,atomic,audited"},
        {"lan_agent_replace_line_range_atomic", "file_mutation", "replace_line_range_atomic", "high", "file,write,line-range,atomic,audited"},
        {"lan_agent_prepare_edit_windows", "edit_window_bundle", "prepare_edit_windows", "low", "file,window,edit-prep,read_only"},
        {"lan_agent_probe_text_file", "file_probe", "probe_text_file", "low", "file,probe,read_only"},
        {"lan_agent_read_text_file", "file_read", "read_file", "low", "file,read,paged"},
        {"lan_agent_read_directory_files", "directory_file_read", "read_directory_files", "low", "directory,read,batch,typed"},
        {"lan_agent_tail_text_file", "file_read", "tail_file", "low", "file,read,tail"},
        {"lan_agent_list_directory", "directory_list", "list_directory", "low", "directory,list,read_only"},
        {"lan_agent_snapshot_diff", "diff_snapshot", "snapshot_diff", "low", "git,diff,read_only"},
        {"lan_agent_apply_diff_patch", "single_file_patch", "apply_diff_patch", "high", "file,write,diff,audited"},
        {"lan_agent_preview_patch", "single_file_patch_preview", "preview_patch", "high", "file,write,preview,audited"},
        {"lan_agent_apply_single_file_patch", "single_file_patch", "apply_single_file_patch", "high", "file,write,replace,audited"},
        {"lan_agent_write_text_file", "file_write", "write_file", "medium", "file,write,audited"},
        {"lan_agent_format_code_file", "code_format", "format_code_file", "medium", "file,format,clang-format,audited"},
        {"lan_agent_ensure_directory", "directory_write", "ensure_directory", "low", "directory,create,audited"},
        {"lan_agent_revert_single_file_patch", "single_file_patch_revert", "revert_patch", "high", "file,write,revert,audited"},
        {"lan_agent_verify_single_file_patch", "single_file_patch_verify", "verify_patch", "low", "file,write,verify,read_only"},
        {"lan_agent_get_patch_audit_trail", "patch_audit_read", "patch_audit", "low", "patch,audit,read_only"},
        {"lan_agent_get_trace_audit_trail", "trace_audit_read", "trace_audit", "low", "trace,audit,read_only"},
        {"lan_agent_get_task", "task_status", "get_task", "low", "task,status,read_only"},
        {"lan_agent_task_log", "task_log", "task_log", "low", "task,log,read_only"},
        {"lan_agent_resolve_task_result", "task_result_reference", "resolve_task_result", "low", "task,result,resolve,read_only"},
        {"lan_agent_discover_logs", "log_discovery", "discover_logs", "low", "logs,discover,read_only"},
        {"lan_agent_tail_control_events", "control_event_tail", "tail_control_events", "low", "events,tail,read_only"},
        {"lan_agent_list_recent_remote_events", "remote_event_list", "recent_remote_events", "low", "events,list,read_only"},
        {"lan_agent_query_remote_task_result_refs", "task_result_reference", "query_remote_task_result_refs", "low", "task,result,query,read_only"},
        {"lan_agent_check_build_dir", "execution_task", "check_build_dir", "medium", "build,check,execute"},
        {"lan_agent_prepare_build_dir", "execution_task", "prepare_build_dir", "medium", "build,prepare,execute"},
        {"lan_agent_build_target", "execution_task", "build_target", "medium", "build,queue,execute"},
        {"lan_agent_configure_project", "execution_task", "configure_project", "medium", "cmake,configure,queue"},
        {"local_cli", "local_cli", "cli_command", "medium", "cli,controlled"},
        {"codex_local_cli", "local_cli", "cli_command", "medium", "cli,controlled"},
        {"lan_agent_run_command", "local_cli", "cli_command", "medium", "cli,controlled,compat_alias"},
        {"lan_agent_cmm_list_projects", "cmm_bridge", "cmm_list_projects", "low", "cmm,bridge,read_only"},
        {"lan_agent_cmm_index_status", "cmm_bridge", "cmm_index_status", "low", "cmm,bridge,read_only"},
        {"lan_agent_cmm_search_code", "cmm_bridge", "cmm_search_code", "low", "cmm,bridge,read_only"},
        {"lan_agent_cmm_search_graph", "cmm_bridge", "cmm_search_graph", "low", "cmm,bridge,read_only"},
        {"lan_agent_cmm_query_graph", "cmm_bridge", "cmm_query_graph", "low", "cmm,bridge,read_only"},
        {"lan_agent_cmm_trace_path", "cmm_bridge", "cmm_trace_path", "low", "cmm,bridge,read_only"},
        {"lan_agent_cmm_get_code_snippet", "cmm_bridge", "cmm_get_code_snippet", "low", "cmm,bridge,read_only"},
        {"lan_agent_cmm_get_graph_schema", "cmm_bridge", "cmm_get_graph_schema", "low", "cmm,bridge,read_only"},
        {"lan_agent_cmm_get_architecture", "cmm_bridge", "cmm_get_architecture", "low", "cmm,bridge,read_only"},
        {"lan_agent_cmm_detect_changes", "cmm_bridge", "cmm_detect_changes", "medium", "cmm,bridge,read_only"},
        {"lan_agent_cmm_index_repository", "cmm_bridge", "cmm_index_repository", "medium", "cmm,bridge,write"},
        {"lan_agent_run_clang_ast_parser", "clang_ast_parse", "clang_ast_parser", "medium", "clang,ast,parse,analysis"},
        {"lan_agent_build_cfg", "clang_cfg_build", "clang_cfg_builder", "medium", "clang,cfg,control_flow,graph,analysis"},
        {"lan_agent_query_cfg_artifact", "clang_cfg_artifact_query", "clang_cfg_artifact_query", "low", "clang,cfg,artifact,query,read_only"},
        {"lan_agent_build_call_graph", "clang_call_graph_build", "clang_call_graph_builder", "medium", "clang,call_graph,call_refs,analysis"},
        {"lan_agent_build_dfg", "clang_dfg_build", "clang_dfg_builder", "medium", "clang,dfg,data_flow,def_use,analysis"},
        {"lan_agent_query_call_graph_artifact", "clang_call_graph_artifact_query", "clang_call_graph_artifact_query", "low", "clang,call_graph,artifact,query,read_only"},
        {"lan_agent_query_dfg_artifact", "clang_dfg_artifact_query", "clang_dfg_artifact_query", "low", "clang,dfg,artifact,query,read_only"},
        {"lan_agent_build_program_slice", "clang_program_slice_build", "clang_program_slicer", "medium", "clang,program_slicing,dfg,analysis"},
        {"lan_agent_query_program_slice_artifact", "clang_program_slice_artifact_query", "clang_program_slice_artifact_query", "low", "clang,program_slicing,artifact,query,read_only"}
    };
    return rules;
}

void ApplyRequestRuleFields(
    const std::string & tool_name,
    const JsonRequestView & params,
    CommandResult * result) {
    if (result == nullptr) {
        return;
    }
    const std::string request_id = params.GetString("request_id");
    if (!request_id.empty() && GetFieldOrDefault(*result, "request_id", "").empty()) {
        result->fields["request_id"] = request_id;
    }
    const std::string trace_id = params.GetString("trace_id");
    if (!trace_id.empty() && GetFieldOrDefault(*result, "trace_id", "").empty()) {
        result->fields["trace_id"] = trace_id;
    }
    const std::string goal_id = params.GetString("goal_id");
    if (!goal_id.empty() && GetFieldOrDefault(*result, "goal_id", "").empty()) {
        result->fields["goal_id"] = goal_id;
    }
    for (const RequestRule & rule : GetRequestRules()) {
        if (tool_name != rule.tool_name) {
            continue;
        }
        if (GetFieldOrDefault(*result, "request_type", "").empty()) {
            result->fields["request_type"] = rule.request_type;
        }
        if (GetFieldOrDefault(*result, "trigger", "").empty()) {
            result->fields["trigger"] = rule.trigger;
        }
        if (GetFieldOrDefault(*result, "risk", "").empty()) {
            result->fields["risk"] = rule.risk;
        }
        if (GetFieldOrDefault(*result, "tags", "").empty()) {
            result->fields["tags"] = rule.tags;
        }
        const std::string task_id = params.GetString("task_id");
        if (!task_id.empty() && GetFieldOrDefault(*result, "task_id", "").empty()) {
            result->fields["task_id"] = task_id;
        }
        return;
    }
}
