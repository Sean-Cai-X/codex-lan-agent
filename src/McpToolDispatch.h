#pragma once

#include "CmmToolResults.h"
#include "ClangAstTool.h"

CommandResult BuildBuildTargetPreflightResult(
    const std::string & build_dir,
    const std::string & target,
    const std::string & config_name);

CommandResult BuildRunCtestPreflightResult(
    const AgentConfig & config,
    const std::string & build_dir,
    const std::string & config_name,
    const std::string & test_regex);
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
    const std::string & probe_ref);
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
    const std::string & trace_id = std::string());
const std::unordered_map<std::string, McpToolHandler> & BuildMcpToolHandlerRegistry() {
    static const std::unordered_map<std::string, McpToolHandler> handlers = {
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
            return DeleteTextRangeWindowAtomicResult(
                config,
                params.GetString("file_path"),
                params.GetString("scan_mode", "comments"),
                std::max(1, params.GetInt("start_line", params.GetInt("next_start_line", 1))),
                std::min(200, std::max(1, params.GetInt("max_lines", 200))),
                params.GetString("primary_intent", "comment_cleanup"),
                params.GetString("trace_id"),
                params.GetString("probe_ref"));
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
                params.GetString("trace_id"));
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
                "line_roi",
                "line_roi_base64",
                &payload_result);
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
            CommandResult payload_result;
            std::string replacement_text = ResolveTextPayloadFromParams(
                params,
                "line_roi",
                "line_roi_base64",
                &payload_result);
            if (!payload_result.ok) {
                return payload_result;
            }
            if (replacement_text.empty()) {
                replacement_text = ResolveTextPayloadFromParams(
                    params,
                    "replacement_text",
                    "replacement_text_base64",
                    &payload_result);
            }
            if (!payload_result.ok) {
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
            CommandResult result = WriteTextFileResult(
                config,
                params.GetString("file_path"),
                content,
                params.GetBool("append", false));
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
