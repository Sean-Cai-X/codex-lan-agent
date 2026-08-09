#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "clips_core/clips.h"
#include "clips_core/factfun.h"

#ifdef __cplusplus
}
#endif

struct ClipsDecision {
    bool engine_ready = false;
    bool loaded_from_files = false;
    bool fallback_used = false;
    std::string domain = "mcp_tool_guard";
    std::string target = "tool";
    std::string decision = "allow";
    std::string verification = "verified";
    std::string next_action;
    std::string reason_code;
    std::string matched_rule;
    std::string route_target;
    std::string fact_schema_id = "mcp_fact_schema_v1";
    std::string decision_schema_id = "clips_decision_schema_v1";
    std::string rule_root;
    std::string loaded_files;
    std::string engine_status = "not_started";
    int asserted_fact_count = 0;
};

std::string EscapeForClipsString(const std::string & value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string ClipsStringSlot(const char * name, const std::string & value) {
    return "(" + std::string(name) + " \"" + EscapeForClipsString(value) + "\")";
}

std::string ClipsBoolSlot(const char * name, bool value) {
    return "(" + std::string(name) + " \"" + std::string(value ? "true" : "false") + "\")";
}

std::string ResolveClipsRuleRoot(const AgentConfig & config) {
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::path(config.config_dir) / "clips_rules",
        std::filesystem::path(config.workspace_root) / "src" / "clips_rules",
        std::filesystem::path(config.workspace_root) / "clips_rules",
        std::filesystem::path(config.workspace_root) / "codex-lan-agent" / "src" / "clips_rules"
    };
    for (const auto & candidate : candidates) {
        std::error_code ec;
        if (!candidate.empty() && std::filesystem::exists(candidate, ec) && !ec) {
            return candidate.string();
        }
    }
    return candidates[1].string();
}

std::vector<std::filesystem::path> BuildClipsRulePaths(
    const std::string & rule_root,
    const std::string & domain) {
    const std::filesystem::path root(rule_root);
    std::vector<std::filesystem::path> paths = {
        root / "templates" / "mcp_fact_templates.clp"
    };
    if (domain == "mcp_tool_guard") {
        paths.push_back(root / "rules" / "mcp_tool_guard.clp");
    } else if (domain == "mcp_result_guard") {
        paths.push_back(root / "rules" / "mcp_result_guard.clp");
    } else if (domain == "slice_ingest_guard") {
        paths.push_back(root / "rules" / "slice_ingest_guard.clp");
    } else if (domain == "cxparser_preflight_guard") {
        paths.push_back(root / "rules" / "cxparser_preflight_guard.clp");
    } else if (domain == "cmm_init_guard") {
        paths.push_back(root / "templates" / "cmm_fact_templates.clp");
        paths.push_back(root / "rules" / "cmm_init_guard.clp");
        paths.push_back(root / "graphs" / "cmm_init_flow.clp");
    }
    return paths;
}

bool LoadClipsFileIfExists(
    Environment * env,
    const std::filesystem::path & path,
    std::vector<std::string> * loaded_files) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return false;
    }
    if (Load(env, path.string().c_str()) == LE_NO_ERROR) {
        if (loaded_files != nullptr) {
            loaded_files->push_back(path.string());
        }
        return true;
    }
    return false;
}

std::vector<std::string> GetEmbeddedClipsTemplateBlocks() {
    return {
        R"((deftemplate mcp_tool_request
              (slot tool_name)
              (slot task_id (default ""))
              (slot provider_id (default ""))
              (slot capability_id (default ""))
              (slot build_dir (default ""))
              (slot project_root (default ""))
              (slot test_regex (default ""))
              (slot session_id (default ""))
              (slot turn_id (default ""))
              (slot reasoning_level (default ""))
              (slot primary_intent (default ""))
              (slot preflight_status (default "missing"))
              (slot dedup_status (default ""))
              (slot canonical_slice_id (default ""))
              (slot dup_of (default ""))
              (slot route_hint (default ""))
              (slot source_type (default ""))
              (slot file_path (default ""))
              (slot scan_mode (default ""))
              (slot probe_ref (default ""))
              (slot patch_id (default ""))
              (slot request_id (default ""))
              (slot trace_id (default ""))
              (slot old_hash (default ""))
              (slot probe_ready (default "false"))
              (slot preview_ready (default "false"))
              (slot revert_plan_ready (default "false"))
              (slot path_within_workspace (default "false"))
              (slot file_count (default "1"))
              (slot max_items_per_call (default ""))
              (slot single_step_required (default "false"))
              (slot operation_granularity (default ""))
              (slot batch_mutation_allowed (default "false"))
              (slot explicit_user_intent (default "false"))
              (slot probe_required (default "false"))
              (slot requires_preview (default "false"))
              (slot requires_approval (default "false"))
              (slot requires_revert_plan (default "false"))
              (slot requires_post_verify (default "false"))))",
        R"((deftemplate mcp_tool_result
              (slot tool_name)
              (slot request_id (default ""))
              (slot trace_id (default ""))
              (slot tool_call_id (default ""))
              (slot provider_id (default ""))
              (slot capability_id (default ""))
              (slot task_id (default ""))
              (slot session_id (default ""))
              (slot turn_id (default ""))
              (slot direct_answer (default ""))
              (slot summary (default ""))
              (slot assistant_text (default ""))
              (slot error (default ""))
              (slot error_code (default ""))
              (slot error_message (default ""))
              (slot status (default ""))
              (slot task_completion (default ""))
              (slot has_more (default "false"))
              (slot next_start_line (default ""))
              (slot continue_required (default "false"))
              (slot auto_continue_required (default "false"))
              (slot analysis_allowed (default "true"))
              (slot batch_completion (default ""))
              (slot remaining_batch_file_count (default "0"))
              (slot next_batch_file_path (default ""))
              (slot known_file_list_complete (default ""))
              (slot directory_listing_complete (default ""))
              (slot content_read_completion (default ""))
              (slot incomplete_scope (default ""))
              (slot terminal_state (default ""))
              (slot completion_claim_allowed (default ""))
              (slot final_answer_allowed (default ""))
              (slot result_hash (default ""))
              (slot schema_version (default ""))
              (slot result_schema_id (default ""))
              (slot ai_conclusion_valid (default "true"))
              (slot result_ref (default ""))
              (slot evidence_ref (default ""))
              (slot log_path (default ""))
              (slot patch_id (default ""))
              (slot write_verified (default ""))
              (slot disk_write_completed (default ""))
              (slot single_step_required (default ""))
              (slot operation_granularity (default ""))
              (slot max_items_per_call (default ""))
              (slot batch_mutation_allowed (default ""))
              (slot step_completion (default ""))
              (slot step_contract (default ""))))",
        R"((deftemplate mcp_tool_chain
              (slot chain_template_id (default "mcp_tool_chain_v1"))
              (slot tool_name)
              (slot chain_phase (default "pre_call"))
              (slot request_type (default "generic_mcp_tool"))
              (slot risk (default "medium"))
              (slot safety_class (default "controlled"))
              (slot execution_class (default "unknown"))
              (slot evidence_policy (default "standard_result_envelope"))
              (slot clips_required (default "true"))
              (slot rule_namespace (default "mcp_tool_guard"))
              (slot result_ref (default ""))
              (slot evidence_ref (default ""))
              (slot task_id (default ""))
              (slot analysis_only (default "false"))
              (slot execution_capability (default ""))))",
        R"((deftemplate slice_ingest_fact
              (slot task_id (default ""))
              (slot session_id (default ""))
              (slot turn_id (default ""))
              (slot provider_id (default ""))
              (slot capability_id (default ""))
              (slot business_user_text (default ""))
              (slot business_assistant_text (default ""))
              (slot business_summary (default ""))
              (slot dedup_hash (default ""))
              (slot canonical_slice_id (default ""))
              (slot dup_of (default ""))
              (slot dedup_status (default ""))
              (slot source_type (default ""))))",
        R"((deftemplate cxparser_fact
              (slot tool_name)
              (slot parse_status (default "missing"))
              (slot symbol_status (default "missing"))
              (slot target_status (default "missing"))
              (slot error_status (default "missing"))
              (slot preflight_status (default "missing"))))",
        R"((deftemplate clips_decision
              (slot domain)
              (slot target (default "tool"))
              (slot decision (default "allow"))
              (slot verification (default "verified"))
              (slot next_action (default ""))
              (slot reason_code (default ""))
              (slot matched_rule (default ""))
              (slot route_target (default ""))))",
        R"((deftemplate cmm_project_state
              (slot repo_path (default ""))
              (slot normalized_project (default ""))
              (slot project_indexed (default "false"))
              (slot index_status (default "unknown"))
              (slot last_check_time (default ""))
              (slot ensure_action (default ""))
              (slot parent_project (default ""))
              (slot note (default ""))))",
        R"((deftemplate cmm_search_request
              (slot repo_path (default ""))
              (slot project (default ""))
              (slot query (default ""))
              (slot path_filter (default ""))
              (slot file_pattern (default ""))
              (slot search_mode (default "semantic"))
              (slot requires_init (default "false"))))",
        R"((deftemplate cmm_workflow_stage
              (slot stage (default "init"))
              (slot project_ready (default "false"))
              (slot search_ready (default "false"))
              (slot current_phase (default "pre_call"))
              (slot completed_steps (default ""))
              (slot pending_steps (default ""))
              (slot error_stage (default ""))
              (slot error_reason (default ""))))"
    };
}

std::vector<std::string> GetEmbeddedClipsRuleBlocks(const std::string & domain) {
    if (domain == "mcp_tool_guard") {
        return {
            R"((defrule allow-single-file-patch-preview
                  (declare (salience 80))
                  (mcp_tool_request (tool_name "lan_agent_preview_patch")
                                    (path_within_workspace "true")
                                    (file_count "1"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target "lan_agent_preview_patch")
                      (decision "allow")
                      (verification "verified")
                      (matched_rule "allow-single-file-patch-preview")))))",
            R"((defrule block-single-file-patch-apply-without-explicit-intent
                  (declare (salience 89))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_apply_single_file_patch")
                                                          (eq ?tool "lan_agent_apply_diff_patch")
                                                          (eq ?tool "lan_agent_revert_single_file_patch")))
                                    (explicit_user_intent "false"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "missing_patch_intent")
                      (next_action "provide a non-empty reason or repair_candidate intent before apply")
                      (matched_rule "block-single-file-patch-apply-without-explicit-intent")))))",
            R"((defrule block-stepwise-file-tool-multi-item-request
                  (declare (salience 88))
                  (mcp_tool_request (tool_name ?tool)
                                    (single_step_required "true")
                                    (max_items_per_call ?count&:(neq ?count "")&:(neq ?count "1")))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "multi_item_file_step_not_allowed")
                      (next_action "redo this file operation with max_ranges_per_call=1 or max_windows_per_call=1; process one item, verify, then rescan")
                      (matched_rule "block-stepwise-file-tool-multi-item-request")))))",
            R"((defrule block-broad-file-mutation-for-stepwise-editing-intent
                  (declare (salience 88))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_write_text_file")
                                                          (eq ?tool "lan_agent_apply_single_file_patch")
                                                          (eq ?tool "lan_agent_apply_diff_patch")))
                                    (primary_intent ?intent&:(or (eq ?intent "comment_cleanup")
                                                                 (eq ?intent "text_cleaning")
                                                                 (eq ?intent "localized_edit")
                                                                 (eq ?intent "source_edit_planning")
                                                                 (eq ?intent "remove_comments")
                                                                 (eq ?intent "strip_comments"))))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "bulk_file_mutation_not_allowed_for_stepwise_edit")
                      (next_action "use lan_agent_delete_text_range_window_atomic(max_lines<=200) for large files, or the single-step scan/prepare/atomic-edit loop for high-risk local edits")
                      (matched_rule "block-broad-file-mutation-for-stepwise-editing-intent")))))",
            R"((defrule block-multi-file-patch-in-phase1
                  (declare (salience 87))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_preview_patch")
                                                          (eq ?tool "lan_agent_apply_single_file_patch")
                                                          (eq ?tool "lan_agent_apply_diff_patch")
                                                          (eq ?tool "lan_agent_revert_single_file_patch")))
                                    (file_count ?count&:(neq ?count "1")))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "multi_file_patch_not_allowed_phase1")
                      (next_action "split the change into one single-file patch candidate")
                      (matched_rule "block-multi-file-patch-in-phase1")))))",
            R"((defrule block-single-file-patch-without-revert-plan
                  (declare (salience 86))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_apply_single_file_patch")
                                                          (eq ?tool "lan_agent_apply_diff_patch")
                                                          (eq ?tool "lan_agent_revert_single_file_patch")))
                                    (requires_revert_plan "true")
                                    (revert_plan_ready "false"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "missing_patch_revert_plan")
                      (next_action "ensure backup/revert plan is available before apply")
                      (matched_rule "block-single-file-patch-without-revert-plan")))))",
            R"((defrule block-high-risk-write-without-path
                  (declare (salience 85))
                  (mcp_tool_chain (request_type "file_mutation"))
                  (mcp_tool_request (tool_name ?tool)
                                    (file_path ""))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "missing_file_path")
                      (next_action "provide a concrete single target file_path before write or patch execution")
                      (matched_rule "block-high-risk-write-without-path")))))",
            R"((defrule route-file-text-operations-to-probe-first
                  (declare (salience 84))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_read_text_file")
                                                          (eq ?tool "lan_agent_find_line_metadata")
                                                          (eq ?tool "lan_agent_find_content_matches")
                                                          (eq ?tool "lan_agent_locate_text_lines")
                                                          (eq ?tool "lan_agent_scan_text_ranges")
                                                          (eq ?tool "lan_agent_prepare_edit_windows")
                                                          (eq ?tool "lan_agent_delete_line_atomic")
                                                          (eq ?tool "lan_agent_delete_content_atomic")
                                                          (eq ?tool "lan_agent_delete_next_text_range_atomic")
                                                          (eq ?tool "lan_agent_delete_text_range_window_atomic")
                                                          (eq ?tool "lan_agent_insert_after_anchor_atomic")
                                                          (eq ?tool "lan_agent_replace_line_range_atomic")
                                                          (eq ?tool "lan_agent_write_text_file")
                                                          (eq ?tool "lan_agent_apply_single_file_patch")
                                                          (eq ?tool "lan_agent_apply_diff_patch")))
                                    (file_path ?file_path&:(neq ?file_path ""))
                                    (probe_required "true")
                                    (probe_ready "false"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "missing_probe_preflight")
                      (route_target "lan_agent_probe_text_file")
                      (next_action "call lan_agent_probe_text_file first and continue only with the emitted next_call_json/probe_ref chain before any text read, scan, write, or patch step")
                      (matched_rule "route-file-text-operations-to-probe-first")))))",
            R"((defrule route-read-text-file-to-window-delete-for-comment-cleanup
                  (declare (salience 84))
                  (mcp_tool_request (tool_name "lan_agent_read_text_file")
                                    (primary_intent ?intent&:(or (eq ?intent "comment_cleanup")
                                                                 (eq ?intent "remove_comments")
                                                                 (eq ?intent "strip_comments")
                                                                 (eq ?intent "删除注释")
                                                                 (eq ?intent "清理注释")
                                                                 (eq ?intent "去除注释")
                                                                 (eq ?intent "移除注释")
                                                                 (eq ?intent "删注释")))
                                    (probe_ready "true"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target "lan_agent_read_text_file")
                      (decision "route")
                      (verification "verified")
                      (reason_code "comment_cleanup_prefers_window_delete")
                      (route_target "lan_agent_delete_text_range_window_atomic")
                      (next_action "call lan_agent_delete_text_range_window_atomic with max_lines=200; do not use read/scan/prepare for comment cleanup")
                      (matched_rule "route-read-text-file-to-window-delete-for-comment-cleanup")))))",
            R"((defrule route-comment-cleanup-scaffold-to-window-delete
                  (declare (salience 83))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_scan_text_ranges")
                                                          (eq ?tool "lan_agent_prepare_edit_windows")
                                                          (eq ?tool "lan_agent_delete_next_text_range_atomic")))
                                    (primary_intent ?intent&:(or (eq ?intent "comment_cleanup")
                                                                 (eq ?intent "remove_comments")
                                                                 (eq ?intent "strip_comments")
                                                                 (eq ?intent "delete_comments")
                                                                 (eq ?intent "删除注释")
                                                                 (eq ?intent "清理注释")
                                                                 (eq ?intent "去除注释")
                                                                 (eq ?intent "移除注释")
                                                                 (eq ?intent "删注释")))
                                    (probe_ready "true"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "verified")
                      (reason_code "comment_cleanup_scaffold_prefers_bounded_window")
                      (route_target "lan_agent_delete_text_range_window_atomic")
                      (next_action "use lan_agent_delete_text_range_window_atomic with max_lines=200 as the default bounded comment cleanup step; reserve single-range delete for boundary-spanning leftovers")
                      (matched_rule "route-comment-cleanup-scaffold-to-window-delete")))))",
            R"((defrule route-read-text-file-to-range-scan-for-editing-intent
                  (declare (salience 82))
                  (mcp_tool_request (tool_name "lan_agent_read_text_file")
                                    (primary_intent ?intent&:(or (eq ?intent "text_cleaning")
                                                                 (eq ?intent "localized_edit")
                                                                 (eq ?intent "source_edit_planning")))
                                    (probe_ready "true"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target "lan_agent_read_text_file")
                      (decision "route")
                      (verification "verified")
                      (reason_code "editing_intent_prefers_range_scan")
                      (route_target "lan_agent_scan_text_ranges")
                      (next_action "call lan_agent_scan_text_ranges with scan_mode=comments before localized edit or comment cleanup")
                      (matched_rule "route-read-text-file-to-range-scan-for-editing-intent")))))",
            R"((defrule default-mcp-tool-allow
                  (declare (salience -100))
                  ?r <- (mcp_tool_request (tool_name ?tool))
                  (not (clips_decision (domain "mcp_tool_guard")))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "verified")
                      (matched_rule "default-mcp-tool-allow")))))"
        };
    }
    if (domain == "mcp_result_guard") {
        return {
            R"((defrule invalid-direct-answer-json-fragment
                  (declare (salience 50))
                  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_run_local_chat")
                                                          (eq ?tool "lan_agent_ventriloquist_reply")
                                                          (eq ?tool "lan_agent_remote_session_new_turn")
                                                          (eq ?tool "lan_agent_remote_session_append_turn")
                                                          (eq ?tool "rag.query")))
                                   (direct_answer "{"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "not_verified")
                      (reason_code "bad_direct_answer_fragment")
                      (next_action "re-run with a concrete tool-backed answer and non-fragment business reply")
                      (matched_rule "invalid-direct-answer-json-fragment")))))",
            R"((defrule invalid-direct-answer-empty
                  (declare (salience 45))
                  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_run_local_chat")
                                                          (eq ?tool "lan_agent_ventriloquist_reply")
                                                          (eq ?tool "lan_agent_remote_session_new_turn")
                                                          (eq ?tool "lan_agent_remote_session_append_turn")
                                                          (eq ?tool "rag.query")))
                                   (direct_answer ""))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "not_verified")
                      (reason_code "empty_direct_answer")
                      (next_action "return a non-empty direct_answer or route through fallback evidence collection")
                      (matched_rule "invalid-direct-answer-empty")))))",
            R"((defrule invalid-direct-answer-label-token
                  (declare (salience 44))
                  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_run_local_chat")
                                                          (eq ?tool "lan_agent_ventriloquist_reply")
                                                          (eq ?tool "lan_agent_remote_session_new_turn")
                                                          (eq ?tool "lan_agent_remote_session_append_turn")
                                                          (eq ?tool "rag.query")))
                                   (direct_answer "direct_answer"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "not_verified")
                      (reason_code "bad_direct_answer_label_token")
                      (next_action "re-run with the business answer text instead of the direct_answer label")
                      (matched_rule "invalid-direct-answer-label-token")))))",
            R"((defrule invalid-ai-conclusion-flag
                  (declare (salience 40))
                  (mcp_tool_result (tool_name ?tool) (ai_conclusion_valid "false"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "not_verified")
                      (reason_code "ai_conclusion_invalid")
                      (next_action "attach raw tool evidence before returning this conclusion")
                      (matched_rule "invalid-ai-conclusion-flag")))))",
            R"((defrule analysis-only-chat-claimed-execution-without-evidence
                  (declare (salience 41))
                  (mcp_tool_chain (request_type "analysis_review")
                                  (chain_phase "post_result"))
                  (mcp_tool_result (tool_name ?tool)
                                   (ai_conclusion_valid "false")
                                   (result_ref "")
                                   (evidence_ref "")
                                   (task_id ""))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "not_verified")
                      (reason_code "analysis_only_execution_claim_without_evidence")
                      (next_action "use real MCP execution tools for edits/builds/tests and return task_id, result_ref, and evidence_ref")
                      (matched_rule "analysis-only-chat-claimed-execution-without-evidence")))))",
            R"((defrule execution-task-result-missing-traceable-ref
                  (declare (salience 39))
                  (mcp_tool_chain (execution_class "execute")
                                  (chain_phase "post_result"))
                  (mcp_tool_result (tool_name ?tool)
                                   (task_id "")
                                   (result_ref "")
                                   (evidence_ref "")
                                   (log_path "")
                                   (error ""))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "not_verified")
                      (reason_code "execution_result_missing_traceable_ref")
                      (next_action "return task_id, result_ref, or evidence_ref for execution-class tools before treating the result as trusted evidence")
                      (matched_rule "execution-task-result-missing-traceable-ref")))))",
            R"((defrule audited-write-result-missing-proof
                  (declare (salience 38))
                  (mcp_tool_chain (request_type "file_mutation")
                                  (chain_phase "post_result"))
                  (mcp_tool_result (tool_name ?tool)
                                   (result_ref "")
                                   (evidence_ref "")
                                   (log_path "")
                                   (error ""))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "not_verified")
                      (reason_code "write_result_missing_audit_ref")
                      (next_action "return patch_id plus result_ref/evidence_ref/log_path before downstream consumers trust this write")
                      (matched_rule "audited-write-result-missing-proof")))))",
            R"((defrule text-range-delete-result-still-pending-by-has-more
                  (declare (salience 49))
                  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_delete_text_range_window_atomic")
                                                          (eq ?tool "lan_agent_delete_next_text_range_atomic")))
                                   (has_more "true"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "text_range_delete_incomplete")
                      (next_action "tool_call_only: deletion is not complete; call the same delete tool with next_start_line/probe_ref until has_more=false before any final completion claim")
                      (route_target ?tool)
                      (matched_rule "text-range-delete-result-still-pending-by-has-more")))))",
            R"((defrule text-range-delete-result-still-pending-by-continuation
                  (declare (salience 48))
                  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_delete_text_range_window_atomic")
                                                          (eq ?tool "lan_agent_delete_next_text_range_atomic")))
                                   (continue_required "true"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "text_range_delete_continue_required")
                      (next_action "tool_call_only: continue the delete loop; current write was verified but the overall comment deletion task is not complete")
                      (route_target ?tool)
                      (matched_rule "text-range-delete-result-still-pending-by-continuation")))))",
            R"((defrule non-terminal-result-forbids-final-answer
                  (declare (salience 47))
                  (mcp_tool_result (tool_name ?tool)
                                   (terminal_state "false")
                                   (completion_claim_allowed "false"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "non_terminal_result_forbids_final_answer")
                      (next_action "tool_call_only: result is non-terminal; continue with next_call_json or move the continuation into task_memory budget runner before any completion claim")
                      (route_target ?tool)
                      (matched_rule "non-terminal-result-forbids-final-answer")))))",
            R"((defrule final-answer-disallowed-by-result
                  (declare (salience 46))
                  (mcp_tool_result (tool_name ?tool)
                                   (final_answer_allowed "false"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "final_answer_disallowed_by_result")
                      (next_action "tool_call_only: the tool result explicitly disallows final answer; execute the required MCP continuation or use task_memory_execute_continuation_budget")
                      (route_target ?tool)
                      (matched_rule "final-answer-disallowed-by-result")))))",
            R"((defrule invalid-result-missing-hash
                  (declare (salience 35))
                  (mcp_tool_result (tool_name ?tool) (result_hash ""))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "not_verified")
                      (reason_code "result_hash_missing")
                      (next_action "finalize result envelope with result_hash before downstream consumption")
                      (matched_rule "invalid-result-missing-hash")))))",
            R"((defrule invalid-result-missing-schema
                  (declare (salience 34))
                  (mcp_tool_result (tool_name ?tool) (schema_version ""))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "not_verified")
                      (reason_code "schema_version_missing")
                      (next_action "finalize result envelope with schema_version before downstream consumption")
                      (matched_rule "invalid-result-missing-schema")))))",
            R"((defrule incomplete-read-result-requires-continuation
                  (declare (salience 33))
                  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_read_text_file")
                                                          (eq ?tool "lan_agent_find_line_metadata")
                                                          (eq ?tool "lan_agent_find_content_matches")
                                                          (eq ?tool "lan_agent_list_directory")
                                                          (eq ?tool "lan_agent_read_directory_files")
                                                          (eq ?tool "lan_agent_run_cxparser_flow")))
                                   (task_completion "incomplete"))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "read_chain_incomplete")
                      (next_action "tool_call_only: continue the declared next_call_json chain until task_completion=complete and analysis_allowed=true")
                      (route_target ?tool)
                      (matched_rule "incomplete-read-result-requires-continuation")))))",
            R"((defrule directory-batch-read-still-pending
                  (declare (salience 48))
                  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_read_text_file")
                                                          (eq ?tool "lan_agent_find_line_metadata")
                                                          (eq ?tool "lan_agent_find_content_matches")
                                                          (eq ?tool "lan_agent_list_directory")
                                                          (eq ?tool "lan_agent_read_directory_files")
                                                          (eq ?tool "lan_agent_run_cxparser_flow")
                                                          (eq ?tool "lan_agent_final_answer")))
                                   (analysis_allowed "false")
                                   (batch_completion "incomplete")
                                   (remaining_batch_file_count ?count&:(neq ?count "0")))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "directory_batch_pending")
                      (next_action "tool_call_only: directory file list is complete; call the required tool before forming a final conclusion")
                      (route_target ?tool)
                      (matched_rule "directory-batch-read-still-pending")))))",
            R"((defrule default-mcp-result-verified
                  (declare (salience -100))
                  (mcp_tool_result (tool_name ?tool))
                  (not (clips_decision (domain "mcp_result_guard")))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "verified")
                      (matched_rule "default-mcp-result-verified")))))"
        };
    }
    if (domain == "slice_ingest_guard") {
        return {
            R"((defrule duplicate-slice-route-canonical
                  (declare (salience 60))
                  (slice_ingest_fact (dedup_status "duplicate")
                                     (canonical_slice_id ?canonical&:(neq ?canonical "")))
                  =>
                  (assert (clips_decision
                      (domain "slice_ingest_guard")
                      (target "dialog_slice")
                      (decision "route")
                      (verification "verified")
                      (reason_code "duplicate_slice_merge")
                      (route_target ?canonical)
                      (next_action "merge into canonical slice instead of writing a new duplicate")
                      (matched_rule "duplicate-slice-route-canonical")))))",
            R"((defrule duplicate-slice-block-status
                  (declare (salience 55))
                  (slice_ingest_fact (dedup_status "duplicate"))
                  =>
                  (assert (clips_decision
                      (domain "slice_ingest_guard")
                      (target "dialog_slice")
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "duplicate_slice_rejected")
                      (next_action "reuse canonical_slice_id or set dedup_status to merged before retry")
                      (matched_rule "duplicate-slice-block-status")))))",
            R"((defrule default-slice-ingest-allow
                  (declare (salience -100))
                  (slice_ingest_fact)
                  (not (clips_decision (domain "slice_ingest_guard")))
                  =>
                  (assert (clips_decision
                      (domain "slice_ingest_guard")
                      (target "dialog_slice")
                      (decision "allow")
                      (verification "verified")
                      (matched_rule "default-slice-ingest-allow")))))"
        };
    }
    if (domain == "cxparser_preflight_guard") {
        return {
            R"((defrule block-build-without-preflight
                  (declare (salience 70))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_build_target")
                                                          (eq ?tool "lan_agent_run_ctest_target")))
                                    (preflight_status ?status&:(or (eq ?status "missing")
                                                                   (eq ?status "false")
                                                                   (eq ?status "blocked"))))
                  =>
                  (assert (clips_decision
                      (domain "cxparser_preflight_guard")
                      (target ?tool)
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "missing_cxparser_preflight")
                      (next_action "call lan_agent_preflight_build_target or lan_agent_preflight_run_ctest_target first, then pass the returned preflight_ref")
                      (matched_rule "block-build-without-preflight")))))",
            R"((defrule default-preflight-allow
                  (declare (salience -100))
                  (mcp_tool_request (tool_name ?tool))
                  (not (clips_decision (domain "cxparser_preflight_guard")))
                  =>
                  (assert (clips_decision
                      (domain "cxparser_preflight_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "verified")
                      (matched_rule "default-preflight-allow")))))"
        };
    }
    if (domain == "cmm_init_guard") {
        return {
            R"((defrule block-cmm-search-before-ensure-indexed
                  (declare (salience 95))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_search_code")
                                                          (eq ?tool "lan_agent_cmm_search_graph")
                                                          (eq ?tool "lan_agent_cmm_query_graph")
                                                          (eq ?tool "lan_agent_cmm_get_code_snippet")
                                                          (eq ?tool "lan_agent_cmm_trace_path")
                                                          (eq ?tool "lan_agent_cmm_get_graph_schema")
                                                          (eq ?tool "lan_agent_cmm_get_architecture")))
                                    (file_path ?repo_path&:(neq ?repo_path "")))
                  (not (cmm_project_state (repo_path ?repo_path)
                                          (project_indexed "true")))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "cmm_project_not_indexed")
                      (next_action "Step 1: Call lan_agent_cmm_index_status with repo_path to check indexing status. Step 2: If not indexed, call lan_agent_cmm_index_repository. Step 3: Re-run the original tool.")
                      (route_target "lan_agent_cmm_index_status")
                      (matched_rule "block-cmm-search-before-ensure-indexed")))))",
            R"((defrule route-cmm-ensure-indexed-as-first-step
                  (declare (salience 93))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_search_code")
                                                          (eq ?tool "lan_agent_cmm_search_graph")
                                                          (eq ?tool "lan_agent_cmm_query_graph")
                                                          (eq ?tool "lan_agent_cmm_get_code_snippet")
                                                          (eq ?tool "lan_agent_cmm_trace_path")
                                                          (eq ?tool "lan_agent_cmm_get_graph_schema")
                                                          (eq ?tool "lan_agent_cmm_get_architecture")))
                                    (file_path ?repo_path&:(neq ?repo_path ""))
                                    (probe_ready "false"))
                  (not (cmm_project_state (repo_path ?repo_path)
                                          (project_indexed "true")))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "cmm_ensure_indexed_required")
                      (next_action "Step 1: Call lan_agent_cmm_index_status with repo_path. Step 2: If not indexed, call lan_agent_cmm_index_repository. Step 3: Run the original tool with the resolved project name.")
                      (route_target "lan_agent_cmm_index_status")
                      (matched_rule "route-cmm-ensure-indexed-as-first-step")))))",
            R"((defrule allow-cmm-ensure-indexed
                  (declare (salience 92))
                  (mcp_tool_request (tool_name "lan_agent_cmm_index_status"))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target "lan_agent_cmm_index_status")
                      (decision "allow")
                      (verification "verified")
                      (matched_rule "allow-cmm-ensure-indexed")))))",
            R"((defrule block-cmm-search-without-project-parameter
                  (declare (salience 94))
                  (mcp_tool_request (tool_name "lan_agent_cmm_search_code")
                                    (primary_intent ?intent&:(or (eq ?intent "code_search")
                                                                 (eq ?intent "find_algorithm")
                                                                 (eq ?intent "semantic_search")))
                                    (file_path ?repo_path&:(neq ?repo_path ""))
                                    (probe_ready "true"))
                  (not (cmm_project_state (repo_path ?repo_path)
                                          (normalized_project ?proj&:(neq ?proj ""))))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target "lan_agent_cmm_search_code")
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "cmm_project_name_not_resolved")
                      (next_action "Call lan_agent_cmm_index_status first to get the normalized_project name, then re-run lan_agent_cmm_search_code with the project parameter")
                      (route_target "lan_agent_cmm_index_status")
                      (matched_rule "block-cmm-search-without-project-parameter")))))",
            R"((defrule route-cmm-re-indexing-when-needed
                  (declare (salience 90))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_index_repository")
                                                          (eq ?tool "lan_agent_cmm_detect_changes")))
                                    (file_path ?repo_path&:(neq ?repo_path "")))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "cmm_reindexing_recommended")
                      (next_action "If previous indexing is corrupted or out-of-date, call lan_agent_cmm_delete_project first, then lan_agent_cmm_index_repository")
                      (route_target "lan_agent_cmm_delete_project")
                      (matched_rule "route-cmm-re-indexing-when-needed")))))",
            R"((defrule allow-cmm-delete-project
                  (declare (salience 89))
                  (mcp_tool_request (tool_name "lan_agent_cmm_delete_project")
                                    (primary_intent "reindex_preparation"))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target "lan_agent_cmm_delete_project")
                      (decision "allow")
                      (verification "verified")
                      (matched_rule "allow-cmm-delete-project")))))",
            R"((defrule block-cmm-delete-project-without-intent
                  (declare (salience 88))
                  (mcp_tool_request (tool_name "lan_agent_cmm_delete_project")
                                    (primary_intent ""))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target "lan_agent_cmm_delete_project")
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "missing_delete_intent")
                      (next_action "Provide explicit primary_intent='reindex_preparation' before deleting a CMM project index")
                      (matched_rule "block-cmm-delete-project-without-intent")))))",
            R"((defrule allow-cmm-search-on-verified-project
                  (declare (salience 85))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_search_code")
                                                          (eq ?tool "lan_agent_cmm_search_graph")
                                                          (eq ?tool "lan_agent_cmm_query_graph")
                                                          (eq ?tool "lan_agent_cmm_get_code_snippet")
                                                          (eq ?tool "lan_agent_cmm_trace_path")
                                                          (eq ?tool "lan_agent_cmm_get_graph_schema")
                                                          (eq ?tool "lan_agent_cmm_get_architecture")))
                                    (file_path ?repo_path&:(neq ?repo_path "")))
                  (cmm_project_state (repo_path ?repo_path)
                                     (project_indexed "true"))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "verified")
                      (matched_rule "allow-cmm-search-on-verified-project")))))",
            R"((defrule suggest-path-filter-for-subdirectory-search
                  (declare (salience 82))
                  (mcp_tool_request (tool_name "lan_agent_cmm_search_code")
                                    (file_path ?repo_path&:(neq ?repo_path ""))
                                    (primary_intent ?intent&:(or (eq ?intent "subdirectory_search")
                                                                 (eq ?intent "focused_search"))))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target "lan_agent_cmm_search_code")
                      (decision "allow")
                      (verification "verified")
                      (reason_code "cmm_search_path_filter_suggested")
                      (next_action "Include path_filter and file_pattern parameters to narrow the search scope")
                      (matched_rule "suggest-path-filter-for-subdirectory-search")))))",
            R"((defrule handle-cmm-project-not-found-error
                  (declare (salience 75))
                  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_search_code")
                                                          (eq ?tool "lan_agent_cmm_search_graph")))
                                   (error ?err&:(or (eq ?err "project not found or not indexed")
                                                    (eq ?err "project_not_found")
                                                    (eq ?err "not_indexed"))))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target ?tool)
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "cmm_project_missing_or_not_indexed")
                      (next_action "Step 1: Call lan_agent_cmm_list_projects. Step 2: If missing, call lan_agent_cmm_index_repository. Step 3: If exists but not indexed, call lan_agent_cmm_index_status")
                      (matched_rule "handle-cmm-project-not-found-error")))))",
            R"((defrule handle-cmm-pattern-required-error
                  (declare (salience 74))
                  (mcp_tool_result (tool_name "lan_agent_cmm_search_code")
                                   (error ?err&:(or (eq ?err "pattern is required")
                                                    (eq ?err "missing_pattern")
                                                    (eq ?err "query_required"))))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target "lan_agent_cmm_search_code")
                      (decision "block")
                      (verification "not_verified")
                      (reason_code "cmm_search_pattern_missing")
                      (next_action "Provide the search query via the 'query' parameter, which will be mapped to CMM's 'pattern' field")
                      (matched_rule "handle-cmm-pattern-required-error")))))",
            R"((defrule default-cmm-init-allow
                  (declare (salience -100))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_index_repository")
                                                          (eq ?tool "lan_agent_cmm_list_projects")
                                                          (eq ?tool "lan_agent_cmm_index_status")
                                                          (eq ?tool "lan_agent_cmm_detect_changes")
                                                          (eq ?tool "lan_agent_cmm_search_code")
                                                          (eq ?tool "lan_agent_cmm_search_graph")
                                                          (eq ?tool "lan_agent_cmm_query_graph")
                                                          (eq ?tool "lan_agent_cmm_get_code_snippet")
                                                          (eq ?tool "lan_agent_cmm_trace_path")
                                                          (eq ?tool "lan_agent_cmm_get_graph_schema")
                                                          (eq ?tool "lan_agent_cmm_get_architecture")
                                                          (eq ?tool "lan_agent_cmm_delete_project"))))
                  (not (clips_decision (domain "cmm_init_guard")))
                  =>
                  (assert (clips_decision
                      (domain "cmm_init_guard")
                      (target ?tool)
                      (decision "allow")
                      (verification "verified")
                      (matched_rule "default-cmm-init-allow")))))"
        };
    }
    return {};
}

bool IsWorkspacePatchPath(
    const AgentConfig & config,
    const std::string & raw_path) {
    static_cast<void>(config);
    return !Trim(raw_path).empty();
}

bool HasPatchPreviewAuditEvent(const AgentConfig & config, const std::string & patch_id) {
    if (patch_id.empty()) {
        return false;
    }
    std::string content;
    std::string read_error;
    if (!ReadWholeFile(BuildPatchAuditEventsPath(config), &content, &read_error)) {
        return false;
    }
    return content.find("\"patch_id\":\"" + codex_lan_agent::JsonEscape(patch_id) + "\"") != std::string::npos &&
        content.find("\"stage\":\"PATCH_PREVIEW\"") != std::string::npos;
}

bool BuildEmbeddedClipsBlocks(
    Environment * env,
    const std::vector<std::string> & blocks) {
    for (const std::string & block : blocks) {
        if (Build(env, block.c_str()) != BE_NO_ERROR) {
            return false;
        }
    }
    return true;
}

std::string ResolveMcpChainRequestType(const std::string & tool_name) {
    if (tool_name == "lan_agent_run_local_chat"
        || tool_name == "lan_agent_enqueue_local_chat"
        || tool_name == "lan_agent_ventriloquist_reply"
        || tool_name == "lan_agent_run_rag_flow"
        || tool_name == "lan_agent_enqueue_rag_flow"
        || tool_name == "rag.query"
        || tool_name == "rag.diff_review"
        || tool_name == "rag.log_classify") {
        return "analysis_review";
    }
    if (tool_name == "lan_agent_remote_session_new_turn"
        || tool_name == "lan_agent_remote_session_append_turn"
        || tool_name == "lan_agent_allocate_remote_chat_session"
        || tool_name == "lan_agent_record_dialog_slice"
        || tool_name == "lan_agent_optfile_apply_write") {
        return "state_mutation";
    }
    if (tool_name.find("read_") != std::string::npos
        || tool_name.find("list_") != std::string::npos
        || tool_name.find("_overview") != std::string::npos
        || tool_name.find("_status") != std::string::npos
        || tool_name.find("lan_agent_cmm_search") != std::string::npos
        || tool_name.find("lan_agent_cmm_query") != std::string::npos
        || tool_name.find("lan_agent_cmm_get_") != std::string::npos
        || tool_name.find("lan_agent_cmm_list_projects") != std::string::npos
        || tool_name.find("lan_agent_cmm_index_status") != std::string::npos
        || tool_name.find("lan_agent_cmm_trace_path") != std::string::npos
        || tool_name.find("lan_agent_cmm_get_graph_schema") != std::string::npos
        || tool_name.find("lan_agent_cmm_get_architecture") != std::string::npos
        || tool_name == "lan_agent_discover_ctest_tests"
        || tool_name == "lan_agent_preflight_build_target"
        || tool_name == "lan_agent_preflight_run_ctest_target"
        || tool_name == "lan_agent_rag_index_status"
        || tool_name == "lan_agent_health"
        || tool_name == "lan_agent_profile_catalog"
        || tool_name == "lan_agent_remote_session_semantic_catalog"
        || tool_name == "lan_agent_get_remote_session"
        || tool_name == "lan_agent_get_task"
        || tool_name == "lan_agent_task_log"
        || tool_name == "lan_agent_resolve_task_result"
        || tool_name == "lan_agent_query_remote_task_result_refs"
        || tool_name == "lan_agent_patch_overview"
        || tool_name == "lan_agent_resolve_remote_session_task_refs"
        || tool_name == "lan_agent_analyze_dialog_slices"
        || tool_name == "lan_agent_discover_logs"
        || tool_name == "lan_agent_tail_control_events"
        || tool_name == "lan_agent_tail_text_file"
        || tool_name == "lan_agent_snapshot_diff"
        || tool_name == "lan_agent_get_patch_audit_trail"
        || tool_name == "lan_agent_get_trace_audit_trail"
        || tool_name == "lan_agent_verify_single_file_patch"
        || tool_name == "lan_agent_optfile_read"
        || tool_name == "lan_agent_find_line_metadata"
        || tool_name == "lan_agent_find_content_matches"
        || tool_name == "lan_agent_optfile_write_preview"
        || tool_name == "lan_agent_locate_text_lines"
        || tool_name == "llama.observer_smoke"
        || tool_name == "router_domain_map"
        || tool_name == "dispatch_contract_map"
        || tool_name == "mcp_capability_registry"
        || tool_name == "rag_memory_slice_contract"
        || tool_name == "semantic_action_map"
        || tool_name == "tool_shortcuts"
        || tool_name == "mcp_actions"
        || tool_name == "semantic_action_resolve"
        || tool_name == "semantic_action_validate"
        || tool_name == "semantic_action_prepare"
        || tool_name == "semantic_action_tool_call") {
        return "read_observe";
    }
    if (tool_name == "lan_agent_delete_line_atomic"
        || tool_name == "lan_agent_delete_content_atomic"
        || tool_name == "lan_agent_insert_after_anchor_atomic"
        || tool_name == "lan_agent_replace_line_range_atomic"
        || tool_name == "lan_agent_apply_diff_patch"
        || tool_name == "lan_agent_preview_patch"
        || tool_name == "lan_agent_apply_single_file_patch"
        || tool_name == "lan_agent_write_text_file"
        || tool_name == "lan_agent_revert_single_file_patch") {
        return "file_mutation";
    }
    if (tool_name == "lan_agent_build_target"
        || tool_name == "lan_agent_configure_project"
        || tool_name == "lan_agent_run_ctest_target"
        || tool_name == "lan_agent_run_cli_profile"
        || tool_name == "lan_agent_enqueue_cli_profile"
        || tool_name == "lan_agent_run_case"
        || tool_name == "lan_agent_enqueue_case"
        || tool_name == "lan_agent_check_build_dir"
        || tool_name == "lan_agent_prepare_build_dir"
        || tool_name == "lan_agent_cmm_index_repository"
        || tool_name == "lan_agent_cmm_delete_project"
        || tool_name == "lan_agent_cmm_detect_changes"
        || tool_name == "local_cli"
        || tool_name == "codex_local_cli") {
        return "execution_task";
    }
    if (tool_name == "lan_agent_run_cxparser_flow") {
        return "cxparser_flow_execution";
    }
    if (tool_name == "lan_agent_rag_clips_run"
        || tool_name == "lan_agent_rag_clips_meta"
        || tool_name == "lan_agent_execute_semantic_action"
        || tool_name == "intent_dispatch_prepare"
        || tool_name == "lan_agent_build_semantic_execution_card") {
        return "rag_clips_run";
    }
    if (tool_name == "lan_agent_clips_decide"
        || tool_name == "lan_agent_clips_chain_template") {
        return "clips_control";
    }
    return "generic_mcp_tool";
}

std::string ResolveMcpChainRisk(const std::string & tool_name) {
    const std::string request_type = ResolveMcpChainRequestType(tool_name);
    if (request_type == "read_observe"
        || request_type == "clips_control"
        || tool_name.find("preflight") != std::string::npos) {
        return "low";
    }
    if (request_type == "file_mutation"
        || request_type == "state_mutation") {
        return "high";
    }
    if (request_type == "execution_task") {
        return "medium";
    }
    return "medium";
}

std::string ResolveMcpChainSafetyClass(const std::string & tool_name) {
    const std::string request_type = ResolveMcpChainRequestType(tool_name);
    if (request_type == "analysis_review") {
        return "analysis_only";
    }
    if (request_type == "read_observe") {
        return "read_only";
    }
    if (request_type == "file_mutation") {
        return "write_audited";
    }
    if (request_type == "state_mutation") {
        return "state_write_audited";
    }
    if (request_type == "execution_task") {
        return "queued_execution";
    }
    if (request_type == "cxparser_flow_execution") {
        return "cxparser_supervised_flow";
    }
    if (request_type == "rag_clips_run") {
        return "direct_bridge_verified_refs";
    }
    if (request_type == "clips_control") {
        return "rule_control";
    }
    return "controlled";
}

std::string ResolveMcpChainExecutionClass(const std::string & tool_name) {
    const std::string request_type = ResolveMcpChainRequestType(tool_name);
    if (request_type == "analysis_review") {
        return "analysis";
    }
    if (request_type == "read_observe") {
        return "read";
    }
    if (request_type == "file_mutation") {
        return "write";
    }
    if (request_type == "state_mutation") {
        return "state_write";
    }
    if (request_type == "execution_task") {
        return "execute";
    }
    if (request_type == "cxparser_flow_execution") {
        return "supervised_flow";
    }
    if (request_type == "rag_clips_run") {
        return "bridge";
    }
    if (request_type == "clips_control") {
        return "rule";
    }
    return "controlled";
}

std::string ResolveMcpChainEvidencePolicy(const std::string & tool_name) {
    const std::string request_type = ResolveMcpChainRequestType(tool_name);
    if (request_type == "analysis_review") {
        return "caller_supplied_explicit_evidence_only";
    }
    if (request_type == "file_mutation") {
        return "preview_apply_verify_audit_required";
    }
    if (request_type == "state_mutation") {
        return "audit_ref_or_storage_ref_required";
    }
    if (request_type == "execution_task") {
        return "task_result_ref_evidence_ref_required";
    }
    if (request_type == "cxparser_flow_execution") {
        return "supervision_result_ref_evidence_ref_required";
    }
    if (tool_name == "lan_agent_rag_clips_run") {
        return "store_refs_verified_required";
    }
    if (request_type == "clips_control") {
        return "decision_fact_and_rule_trace_required";
    }
    return "standard_result_envelope";
}

std::string BuildMcpToolChainFact(
    const std::string & tool_name,
    const std::string & phase,
    const JsonRequestView * params,
    const CommandResult * result) {
    const std::string result_ref = result == nullptr
        ? std::string()
        : GetFieldOrDefault(*result, "result_ref", "");
    const std::string evidence_ref = result == nullptr
        ? std::string()
        : GetFieldOrDefault(*result, "evidence_ref", "");
    const std::string task_id = result == nullptr
        ? (params == nullptr ? std::string() : params->GetString("task_id"))
        : GetFieldOrDefault(*result, "task_id", "");
    const std::string analysis_only = result == nullptr
        ? (ResolveMcpChainRequestType(tool_name) == "analysis_review" ? "true" : "false")
        : GetFieldOrDefault(*result, "analysis_only", "");
    const std::string execution_capability = result == nullptr
        ? (ResolveMcpChainRequestType(tool_name) == "analysis_review" ? "false" : "")
        : GetFieldOrDefault(*result, "execution_capability", "");
    return "(mcp_tool_chain "
        + ClipsStringSlot("chain_template_id", "mcp_tool_chain_v1") + " "
        + ClipsStringSlot("tool_name", tool_name) + " "
        + ClipsStringSlot("chain_phase", phase) + " "
        + ClipsStringSlot("request_type", ResolveMcpChainRequestType(tool_name)) + " "
        + ClipsStringSlot("risk", ResolveMcpChainRisk(tool_name)) + " "
        + ClipsStringSlot("safety_class", ResolveMcpChainSafetyClass(tool_name)) + " "
        + ClipsStringSlot("execution_class", ResolveMcpChainExecutionClass(tool_name)) + " "
        + ClipsStringSlot("evidence_policy", ResolveMcpChainEvidencePolicy(tool_name)) + " "
        + ClipsStringSlot("clips_required", "true") + " "
        + ClipsStringSlot("rule_namespace", phase == "post_result" ? "mcp_result_guard" : "mcp_tool_guard") + " "
        + ClipsStringSlot("result_ref", result_ref) + " "
        + ClipsStringSlot("evidence_ref", evidence_ref) + " "
        + ClipsStringSlot("task_id", task_id) + " "
        + ClipsStringSlot("analysis_only", analysis_only) + " "
        + ClipsStringSlot("execution_capability", execution_capability)
        + ")";
}

int CountUnifiedDiffHunks(const std::string & diff_text) {
    if (diff_text.empty()) {
        return 0;
    }
    int count = 0;
    bool at_line_start = true;
    for (std::size_t index = 0; index + 1 < diff_text.size(); ++index) {
        if (at_line_start && diff_text[index] == '@' && diff_text[index + 1] == '@') {
            ++count;
        }
        at_line_start = diff_text[index] == '\n' || diff_text[index] == '\r';
        if (diff_text[index] == '\r' && index + 1 < diff_text.size() && diff_text[index + 1] == '\n') {
            ++index;
            at_line_start = true;
        }
    }
    return count;
}

std::string BuildMcpToolRequestFact(
    const AgentConfig & config,
    const std::string & tool_name,
    const JsonRequestView & params) {
    std::string file_path = params.GetString("file_path");
    if (file_path.empty() && tool_name.find("lan_agent_cmm_") != std::string::npos) {
        file_path = params.GetString("project");
    }
    const bool recent_probe_ready = HasRecentProbePath(file_path);
    const std::string probe_ref = FirstNonEmpty(
        params.GetString("probe_ref"),
        params.GetString("probe_result_ref"),
        recent_probe_ready ? file_path : std::string(),
        "");
    const std::string patch_id = params.GetString("patch_id");
    const std::string reason = params.GetString("reason");
    const bool path_within_workspace = file_path.empty() ? true : IsWorkspacePatchPath(config, file_path);
    const bool preview_ready =
        tool_name == "lan_agent_apply_single_file_patch" ? HasPatchPreviewAuditEvent(config, patch_id) : false;
    const bool explicit_user_intent =
        !Trim(reason).empty() || !Trim(params.GetString("repair_candidate_id")).empty() || params.GetString("primary_intent") == "refactor_file";
    const bool probe_required =
        tool_name == "lan_agent_read_text_file"
        || tool_name == "lan_agent_find_line_metadata"
        || tool_name == "lan_agent_find_content_matches"
        || tool_name == "lan_agent_locate_text_lines"
        || tool_name == "lan_agent_scan_text_ranges"
        || tool_name == "lan_agent_prepare_edit_windows"
        || tool_name == "lan_agent_delete_line_atomic"
        || tool_name == "lan_agent_delete_content_atomic"
        || tool_name == "lan_agent_delete_next_text_range_atomic"
        || tool_name == "lan_agent_delete_text_range_window_atomic"
        || tool_name == "lan_agent_insert_after_anchor_atomic"
        || tool_name == "lan_agent_replace_line_range_atomic"
        || tool_name == "lan_agent_write_text_file"
        || tool_name == "lan_agent_apply_single_file_patch"
        || tool_name == "lan_agent_apply_diff_patch";
    const bool probe_ready = !Trim(probe_ref).empty() || params.GetBool("probe_ready", false) || recent_probe_ready;
    const bool is_patch_tool =
        tool_name == "lan_agent_preview_patch"
        || tool_name == "lan_agent_apply_single_file_patch"
        || tool_name == "lan_agent_apply_diff_patch"
        || tool_name == "lan_agent_revert_single_file_patch";
    const bool is_apply_patch_tool =
        tool_name == "lan_agent_apply_single_file_patch"
        || tool_name == "lan_agent_apply_diff_patch"
        || tool_name == "lan_agent_revert_single_file_patch";
    const bool single_step_required =
        tool_name == "lan_agent_scan_text_ranges"
        || tool_name == "lan_agent_prepare_edit_windows"
        || tool_name == "lan_agent_find_line_metadata"
        || tool_name == "lan_agent_find_content_matches"
        || tool_name == "lan_agent_locate_text_lines"
        || tool_name == "lan_agent_delete_line_atomic"
        || tool_name == "lan_agent_delete_content_atomic"
        || tool_name == "lan_agent_delete_next_text_range_atomic"
        || tool_name == "lan_agent_insert_after_anchor_atomic"
        || tool_name == "lan_agent_replace_line_range_atomic"
        || tool_name == "lan_agent_write_text_file"
        || tool_name == "lan_agent_apply_single_file_patch"
        || tool_name == "lan_agent_apply_diff_patch";
    std::string max_items_per_call;
    std::string operation_granularity;
    if (tool_name == "lan_agent_scan_text_ranges") {
        max_items_per_call = params.GetRawJson("max_ranges_per_call");
        operation_granularity = "single_text_range";
    } else if (tool_name == "lan_agent_prepare_edit_windows") {
        max_items_per_call = params.GetRawJson("max_windows_per_call");
        operation_granularity = "single_edit_window";
    } else if (tool_name == "lan_agent_delete_line_atomic"
               || tool_name == "lan_agent_delete_content_atomic"
               || tool_name == "lan_agent_delete_next_text_range_atomic"
               || tool_name == "lan_agent_insert_after_anchor_atomic"
               || tool_name == "lan_agent_replace_line_range_atomic") {
        max_items_per_call = "1";
        operation_granularity = tool_name == "lan_agent_delete_next_text_range_atomic"
            ? "single_text_range_delete"
            : "single_atomic_mutation";
    } else if (tool_name == "lan_agent_apply_diff_patch") {
        const int diff_hunk_count = CountUnifiedDiffHunks(params.GetString("diff_text"));
        max_items_per_call = diff_hunk_count > 0
            ? std::to_string(diff_hunk_count)
            : params.GetRawJson("mutation_count", "1");
        operation_granularity = "single_diff_hunk";
    } else if (tool_name == "lan_agent_write_text_file"
               || tool_name == "lan_agent_apply_single_file_patch") {
        max_items_per_call = params.GetRawJson("mutation_count", "1");
        operation_granularity = "broad_file_mutation";
    }
    const std::string preflight_status = FirstNonEmpty(
        params.GetString("preflight_status"),
        params.GetString("cxparser_preflight_status"),
        params.GetString("preflight_ref").empty() ? std::string() : "ready",
        "missing");
    return "(mcp_tool_request "
        + ClipsStringSlot("tool_name", tool_name) + " "
        + ClipsStringSlot("task_id", params.GetString("task_id")) + " "
        + ClipsStringSlot("provider_id", params.GetString("provider_id")) + " "
        + ClipsStringSlot("capability_id", params.GetString("capability_id")) + " "
        + ClipsStringSlot("build_dir", params.GetString("build_dir")) + " "
        + ClipsStringSlot("project_root", params.GetString("project_root")) + " "
        + ClipsStringSlot("test_regex", params.GetString("test_regex")) + " "
        + ClipsStringSlot("session_id", params.GetString("session_id")) + " "
        + ClipsStringSlot("turn_id", params.GetString("turn_id")) + " "
        + ClipsStringSlot("reasoning_level", params.GetString("reasoning_level")) + " "
        + ClipsStringSlot("primary_intent", params.GetString("primary_intent")) + " "
        + ClipsStringSlot("preflight_status", preflight_status) + " "
        + ClipsStringSlot("dedup_status", params.GetString("dedup_status")) + " "
        + ClipsStringSlot("canonical_slice_id", params.GetString("canonical_slice_id")) + " "
        + ClipsStringSlot("dup_of", params.GetString("dup_of")) + " "
        + ClipsStringSlot("route_hint", params.GetString("route_hint")) + " "
        + ClipsStringSlot("source_type", params.GetString("source_type")) + " "
        + ClipsStringSlot("file_path", file_path) + " "
        + ClipsStringSlot("scan_mode", params.GetString("scan_mode")) + " "
        + ClipsStringSlot("probe_ref", probe_ref) + " "
        + ClipsStringSlot("patch_id", patch_id) + " "
        + ClipsStringSlot("request_id", params.GetString("request_id")) + " "
        + ClipsStringSlot("trace_id", params.GetString("trace_id")) + " "
        + ClipsStringSlot("old_hash", params.GetString("old_hash")) + " "
        + ClipsBoolSlot("probe_ready", probe_ready) + " "
        + ClipsBoolSlot("preview_ready", preview_ready) + " "
        + ClipsBoolSlot("revert_plan_ready", is_patch_tool) + " "
        + ClipsBoolSlot("path_within_workspace", path_within_workspace) + " "
        + ClipsStringSlot("file_count", file_path.empty() ? "0" : "1") + " "
        + ClipsStringSlot("max_items_per_call", max_items_per_call) + " "
        + ClipsBoolSlot("single_step_required", single_step_required) + " "
        + ClipsStringSlot("operation_granularity", operation_granularity) + " "
        + ClipsBoolSlot("batch_mutation_allowed", false) + " "
        + ClipsBoolSlot("explicit_user_intent", explicit_user_intent) + " "
        + ClipsBoolSlot("probe_required", probe_required) + " "
        + ClipsBoolSlot("requires_preview", tool_name == "lan_agent_preview_patch") + " "
        + ClipsBoolSlot("requires_approval", is_apply_patch_tool) + " "
        + ClipsBoolSlot("requires_revert_plan", is_apply_patch_tool) + " "
        + ClipsBoolSlot("requires_post_verify", is_apply_patch_tool)
        + ")";
}

std::string BuildCmmProjectStateFact(const JsonRequestView & params) {
    std::string file_path = params.GetString("file_path");
    if (file_path.empty()) {
        file_path = params.GetString("project");
    }
    const std::string normalized_project = params.GetString("normalized_project");
    bool project_indexed = params.GetBool("project_indexed", false);
    if (!project_indexed && !file_path.empty()) {
        const bool has_path_separator =
            file_path.find('/') != std::string::npos
            || file_path.find('\\') != std::string::npos;
        if (!has_path_separator) {
            project_indexed = true;
        }
    }
    const std::string index_status = FirstNonEmpty(
        params.GetString("index_status"),
        project_indexed ? "indexed" : "unknown",
        "unknown");
    const std::string last_check_time = params.GetString("last_check_time");
    const std::string ensure_action = params.GetString("ensure_action");
    const std::string parent_project = params.GetString("parent_project");
    const std::string note = params.GetString("note");
    return "(cmm_project_state "
        + ClipsStringSlot("repo_path", file_path) + " "
        + ClipsStringSlot("normalized_project", normalized_project) + " "
        + ClipsStringSlot("project_indexed", project_indexed ? "true" : "false") + " "
        + ClipsStringSlot("index_status", index_status) + " "
        + ClipsStringSlot("last_check_time", last_check_time) + " "
        + ClipsStringSlot("ensure_action", ensure_action) + " "
        + ClipsStringSlot("parent_project", parent_project) + " "
        + ClipsStringSlot("note", note) + ")";
}

std::string BuildSliceIngestFact(const JsonRequestView & params) {
    const std::string business_user_text = FirstNonEmpty(
        params.GetString("business_user_text"),
        params.GetString("user_text"),
        "");
    const std::string business_assistant_text = FirstNonEmpty(
        params.GetString("business_assistant_text"),
        params.GetString("assistant_text"),
        "");
    return "(slice_ingest_fact "
        + ClipsStringSlot("task_id", params.GetString("task_id")) + " "
        + ClipsStringSlot("session_id", params.GetString("session_id")) + " "
        + ClipsStringSlot("turn_id", params.GetString("turn_id")) + " "
        + ClipsStringSlot("provider_id", params.GetString("provider_id")) + " "
        + ClipsStringSlot("capability_id", params.GetString("capability_id")) + " "
        + ClipsStringSlot("business_user_text", business_user_text) + " "
        + ClipsStringSlot("business_assistant_text", business_assistant_text) + " "
        + ClipsStringSlot("business_summary", FirstNonEmpty(
            params.GetString("business_summary"),
            params.GetString("slice_summary"),
            "")) + " "
        + ClipsStringSlot("dedup_hash", params.GetString("dedup_hash")) + " "
        + ClipsStringSlot("canonical_slice_id", params.GetString("canonical_slice_id")) + " "
        + ClipsStringSlot("dup_of", params.GetString("dup_of")) + " "
        + ClipsStringSlot("dedup_status", params.GetString("dedup_status")) + " "
        + ClipsStringSlot("source_type", params.GetString("source_type"))
        + ")";
}

std::string BuildCxparserFact(
    const std::string & tool_name,
    const JsonRequestView & params) {
    const std::string preflight_status = FirstNonEmpty(
        params.GetString("preflight_status"),
        params.GetString("cxparser_preflight_status"),
        params.GetString("preflight_ref").empty() ? std::string() : "ready",
        "missing");
    return "(cxparser_fact "
        + ClipsStringSlot("tool_name", tool_name) + " "
        + ClipsStringSlot("parse_status", FirstNonEmpty(params.GetString("parse_status"), preflight_status, "missing")) + " "
        + ClipsStringSlot("symbol_status", FirstNonEmpty(params.GetString("symbol_status"), "missing", "missing")) + " "
        + ClipsStringSlot("target_status", FirstNonEmpty(params.GetString("target_status"), "missing", "missing")) + " "
        + ClipsStringSlot("error_status", FirstNonEmpty(params.GetString("error_status"), "missing", "missing")) + " "
        + ClipsStringSlot("preflight_status", preflight_status)
        + ")";
}

std::string BuildMcpToolResultFact(
    const std::string & tool_name,
    const CommandResult & result) {
    return "(mcp_tool_result "
        + ClipsStringSlot("tool_name", tool_name) + " "
        + ClipsStringSlot("request_id", GetFieldOrDefault(result, "request_id", "")) + " "
        + ClipsStringSlot("trace_id", GetFieldOrDefault(result, "trace_id", "")) + " "
        + ClipsStringSlot("tool_call_id", GetFieldOrDefault(result, "tool_call_id", "")) + " "
        + ClipsStringSlot("provider_id", GetFieldOrDefault(result, "provider_id", "")) + " "
        + ClipsStringSlot("capability_id", GetFieldOrDefault(result, "capability_id", "")) + " "
        + ClipsStringSlot("task_id", GetFieldOrDefault(result, "task_id", "")) + " "
        + ClipsStringSlot("session_id", GetFieldOrDefault(result, "session_id", "")) + " "
        + ClipsStringSlot("turn_id", GetFieldOrDefault(result, "turn_id", "")) + " "
        + ClipsStringSlot("direct_answer", GetFieldOrDefault(result, "direct_answer", "")) + " "
        + ClipsStringSlot("summary", GetFieldOrDefault(result, "summary", "")) + " "
        + ClipsStringSlot("assistant_text", GetFieldOrDefault(result, "assistant_text", "")) + " "
        + ClipsStringSlot("error", GetFieldOrDefault(result, "error", "")) + " "
        + ClipsStringSlot("error_code", GetFieldOrDefault(result, "error_code", "")) + " "
        + ClipsStringSlot("error_message", GetFieldOrDefault(result, "error_message", "")) + " "
        + ClipsStringSlot("status", GetFieldOrDefault(result, "status", "")) + " "
        + ClipsStringSlot("task_completion", GetFieldOrDefault(result, "task_completion", "")) + " "
        + ClipsStringSlot("has_more", GetFieldOrDefault(result, "has_more", "false")) + " "
        + ClipsStringSlot("next_start_line", GetFieldOrDefault(result, "next_start_line", "")) + " "
        + ClipsStringSlot("continue_required", GetFieldOrDefault(result, "continue_required", "false")) + " "
        + ClipsStringSlot("auto_continue_required", GetFieldOrDefault(result, "auto_continue_required", "false")) + " "
        + ClipsStringSlot("analysis_allowed", GetFieldOrDefault(result, "analysis_allowed", "true")) + " "
        + ClipsStringSlot("batch_completion", GetFieldOrDefault(result, "batch_completion", "")) + " "
        + ClipsStringSlot("remaining_batch_file_count", GetFieldOrDefault(result, "remaining_batch_file_count", "0")) + " "
        + ClipsStringSlot("next_batch_file_path", GetFieldOrDefault(result, "next_batch_file_path", "")) + " "
        + ClipsStringSlot("known_file_list_complete", GetFieldOrDefault(result, "known_file_list_complete", "")) + " "
        + ClipsStringSlot("directory_listing_complete", GetFieldOrDefault(result, "directory_listing_complete", "")) + " "
        + ClipsStringSlot("content_read_completion", GetFieldOrDefault(result, "content_read_completion", "")) + " "
        + ClipsStringSlot("incomplete_scope", GetFieldOrDefault(result, "incomplete_scope", "")) + " "
        + ClipsStringSlot("terminal_state", GetFieldOrDefault(result, "terminal_state", "")) + " "
        + ClipsStringSlot("completion_claim_allowed", GetFieldOrDefault(result, "completion_claim_allowed", "")) + " "
        + ClipsStringSlot("final_answer_allowed", GetFieldOrDefault(result, "final_answer_allowed", "")) + " "
        + ClipsStringSlot("result_hash", GetFieldOrDefault(result, "result_hash", "")) + " "
        + ClipsStringSlot("schema_version", GetFieldOrDefault(result, "schema_version", "")) + " "
        + ClipsStringSlot("result_schema_id", GetFieldOrDefault(result, "result_schema_id", "")) + " "
        + ClipsStringSlot("ai_conclusion_valid", GetFieldOrDefault(result, "ai_conclusion_valid", "true")) + " "
        + ClipsStringSlot("result_ref", GetFieldOrDefault(result, "result_ref", "")) + " "
        + ClipsStringSlot("evidence_ref", GetFieldOrDefault(result, "evidence_ref", "")) + " "
        + ClipsStringSlot("log_path", GetFieldOrDefault(result, "log_path", "")) + " "
        + ClipsStringSlot("patch_id", GetFieldOrDefault(result, "patch_id", "")) + " "
        + ClipsStringSlot("write_verified", GetFieldOrDefault(result, "write_verified", "")) + " "
        + ClipsStringSlot("disk_write_completed", GetFieldOrDefault(result, "disk_write_completed", "")) + " "
        + ClipsStringSlot("single_step_required", GetFieldOrDefault(result, "single_step_required", "")) + " "
        + ClipsStringSlot("operation_granularity", GetFieldOrDefault(result, "operation_granularity", "")) + " "
        + ClipsStringSlot("max_items_per_call", GetFieldOrDefault(result, "max_items_per_call", "")) + " "
        + ClipsStringSlot("batch_mutation_allowed", GetFieldOrDefault(result, "batch_mutation_allowed", "")) + " "
        + ClipsStringSlot("step_completion", GetFieldOrDefault(result, "step_completion", "")) + " "
        + ClipsStringSlot("step_contract", FirstNonEmpty(
            GetFieldOrDefault(result, "step_contract", ""),
            GetFieldOrDefault(result, "scan_contract", ""),
            GetFieldOrDefault(result, "edit_window_contract", "")))
        + ")";
}

std::string ClipsValueToString(const CLIPSValue & value) {
    if (value.header == nullptr) {
        return std::string();
    }
    switch (value.header->type) {
    case SYMBOL_TYPE:
    case STRING_TYPE:
    case INSTANCE_NAME_TYPE:
        return value.lexemeValue == nullptr ? std::string() : std::string(value.lexemeValue->contents);
    case INTEGER_TYPE:
        return value.integerValue == nullptr ? std::string() : std::to_string(value.integerValue->contents);
    case FLOAT_TYPE: {
        std::ostringstream output;
        output << value.floatValue->contents;
        return output.str();
    }
    default:
        return std::string();
    }
}

std::string GetClipsFactSlotString(Fact * fact, const char * slot_name) {
    CLIPSValue value{};
    if (fact == nullptr || GetFactSlot(fact, slot_name, &value) != GSE_NO_ERROR) {
        return std::string();
    }
    return ClipsValueToString(value);
}

std::string GetClipsFactTemplateName(Fact * fact) {
    if (fact == nullptr || fact->whichDeftemplate == nullptr || fact->whichDeftemplate->header.name == nullptr) {
        return std::string();
    }
    return fact->whichDeftemplate->header.name->contents == nullptr
        ? std::string()
        : std::string(fact->whichDeftemplate->header.name->contents);
}

ClipsDecision EvaluateClipsDecision(
    const AgentConfig & config,
    const std::string & domain,
    const std::string & tool_name,
    const JsonRequestView * params,
    const CommandResult * result) {
    ClipsDecision decision;
    decision.domain = domain;
    decision.target = tool_name;
    decision.rule_root = ResolveClipsRuleRoot(config);

    Environment * env = CreateEnvironment();
    if (env == nullptr) {
        decision.fallback_used = true;
        decision.engine_status = "create_environment_failed";
        return decision;
    }

    const bool embedded_templates_ready = BuildEmbeddedClipsBlocks(env, GetEmbeddedClipsTemplateBlocks());
    std::vector<std::string> loaded_files;
    const std::vector<std::filesystem::path> rule_paths = BuildClipsRulePaths(decision.rule_root, domain);
    for (const auto & rule_path : rule_paths) {
        LoadClipsFileIfExists(env, rule_path, &loaded_files);
    }
    if (!loaded_files.empty()) {
        decision.loaded_from_files = true;
        decision.engine_ready = true;
        decision.engine_status = "file_rules_loaded";
    } else if (embedded_templates_ready
        && BuildEmbeddedClipsBlocks(env, GetEmbeddedClipsRuleBlocks(domain))) {
        decision.engine_ready = true;
        decision.engine_status = "embedded_rules_loaded";
    }

    if (!decision.engine_ready) {
        decision.fallback_used = true;
        decision.engine_status = "no_rules_loaded";
        DestroyEnvironment(env);
        return decision;
    }

    Reset(env);

    auto assert_fact =
        [&](const std::string & fact_text) {
            if (fact_text.empty()) {
                return;
            }
            void * assert_result = AssertString(env, fact_text.c_str());
            if (assert_result != nullptr) {
                ++decision.asserted_fact_count;
            } else {
                decision.engine_status = "assert_failed:" + fact_text.substr(0, 100);
            }
        };

    if (domain == "mcp_tool_guard" || domain == "cxparser_preflight_guard" || domain == "cmm_init_guard") {
        if (params != nullptr) {
            assert_fact(BuildMcpToolChainFact(tool_name, "pre_call", params, nullptr));
            assert_fact(BuildMcpToolRequestFact(config, tool_name, *params));
            if (domain == "cxparser_preflight_guard") {
                assert_fact(BuildCxparserFact(tool_name, *params));
            } else if (domain == "cmm_init_guard") {
                assert_fact(BuildCmmProjectStateFact(*params));
            }
        }
    } else if (domain == "slice_ingest_guard") {
        if (params != nullptr) {
            assert_fact(BuildMcpToolChainFact(tool_name, "pre_call", params, nullptr));
            assert_fact(BuildSliceIngestFact(*params));
            assert_fact(BuildMcpToolRequestFact(config, tool_name, *params));
        }
    } else if (domain == "mcp_result_guard") {
        if (result != nullptr) {
            assert_fact(BuildMcpToolChainFact(tool_name, "post_result", nullptr, result));
            assert_fact(BuildMcpToolResultFact(tool_name, *result));
        }
    }

    Run(env, -1);

    decision.engine_status = "after_run_facts_collecting";

    for (Fact * fact = GetNextFact(env, nullptr); fact != nullptr; fact = GetNextFact(env, fact)) {
        if (GetClipsFactTemplateName(fact) != "clips_decision") {
            continue;
        }
        const std::string fact_domain = GetClipsFactSlotString(fact, "domain");
        if (!fact_domain.empty() && fact_domain != domain) {
            continue;
        }
        decision.decision = FirstNonEmpty(GetClipsFactSlotString(fact, "decision"), decision.decision, decision.decision);
        decision.verification = FirstNonEmpty(GetClipsFactSlotString(fact, "verification"), decision.verification, decision.verification);
        decision.next_action = GetClipsFactSlotString(fact, "next_action");
        decision.reason_code = GetClipsFactSlotString(fact, "reason_code");
        decision.matched_rule = GetClipsFactSlotString(fact, "matched_rule");
        decision.route_target = GetClipsFactSlotString(fact, "route_target");
        decision.engine_status = "decision_found_by_rule:" + decision.matched_rule;
        break;
    }

    if (decision.matched_rule.empty()) {
        decision.engine_status = "no_decision_matched_using_default";
    }

    if (!loaded_files.empty()) {
        std::ostringstream output;
        for (std::size_t index = 0; index < loaded_files.size(); ++index) {
            if (index > 0) {
                output << ";";
            }
            output << loaded_files[index];
        }
        decision.loaded_files = output.str();
    }

    DestroyEnvironment(env);
    return decision;
}

void ApplyClipsDecisionFields(
    const ClipsDecision & decision,
    const std::string & phase,
    CommandResult * result) {
    if (result == nullptr) {
        return;
    }
    const std::string prefix = phase.empty() ? "clips_" : ("clips_" + phase + "_");
    result->fields[prefix + "fact_schema_id"] = decision.fact_schema_id;
    result->fields[prefix + "decision_schema_id"] = decision.decision_schema_id;
    result->fields[prefix + "domain"] = decision.domain;
    result->fields[prefix + "decision"] = decision.decision;
    result->fields[prefix + "verification"] = decision.verification;
    result->fields[prefix + "reason_code"] = decision.reason_code;
    result->fields[prefix + "matched_rule"] = decision.matched_rule;
    result->fields[prefix + "next_action"] = decision.next_action;
    result->fields[prefix + "route_target"] = decision.route_target;
    result->fields[prefix + "engine_status"] = decision.engine_status;
    result->fields[prefix + "loaded_from_files"] = decision.loaded_from_files ? "true" : "false";
    result->fields[prefix + "fallback_used"] = decision.fallback_used ? "true" : "false";
    result->fields[prefix + "rule_root"] = decision.rule_root;
    result->fields[prefix + "loaded_files"] = decision.loaded_files;
    result->fields[prefix + "asserted_fact_count"] = std::to_string(decision.asserted_fact_count);
}

void ApplyClipsChainTemplateFields(
    const std::string & tool_name,
    const std::string & phase,
    CommandResult * result) {
    if (result == nullptr) {
        return;
    }
    const std::string prefix = phase.empty() ? "clips_chain_" : ("clips_" + phase + "_chain_");
    result->fields[prefix + "template_id"] = "mcp_tool_chain_v1";
    result->fields[prefix + "request_type"] = ResolveMcpChainRequestType(tool_name);
    result->fields[prefix + "risk"] = ResolveMcpChainRisk(tool_name);
    result->fields[prefix + "safety_class"] = ResolveMcpChainSafetyClass(tool_name);
    result->fields[prefix + "execution_class"] = ResolveMcpChainExecutionClass(tool_name);
    result->fields[prefix + "evidence_policy"] = ResolveMcpChainEvidencePolicy(tool_name);
    result->fields[prefix + "clips_required"] = "true";
}

bool ToolRequiresClipsPreflight(const std::string & tool_name) {
    (void) tool_name;
    return true;
}

std::string ResolveClipsPreflightDomain(const std::string & tool_name) {
    if (tool_name == "lan_agent_record_dialog_slice") {
        return "slice_ingest_guard";
    }
    if (tool_name == "lan_agent_build_target"
        || tool_name == "lan_agent_run_ctest_target") {
        return "cxparser_preflight_guard";
    }
    if (tool_name.find("lan_agent_cmm_") != std::string::npos
        && tool_name != "lan_agent_cmm_index_repository"
        && tool_name != "lan_agent_cmm_delete_project"
        && tool_name != "lan_agent_cmm_detect_changes") {
        return "cmm_init_guard";
    }
    return "mcp_tool_guard";
}

bool ToolRequiresClipsResultGuard(const std::string & tool_name) {
    (void) tool_name;
    return true;
}

CommandResult BuildClipsDecisionResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    std::string domain = FirstNonEmpty(
        params.GetString("decision_domain"),
        params.GetString("domain"),
        "");
    const std::string tool_name = FirstNonEmpty(
        params.GetString("tool_name"),
        params.GetString("name"),
        "lan_agent_clips_decide");
    if (domain.empty()) {
        if (tool_name.find("lan_agent_cmm_") != std::string::npos
            && tool_name != "lan_agent_cmm_index_repository"
            && tool_name != "lan_agent_cmm_delete_project"
            && tool_name != "lan_agent_cmm_detect_changes") {
            domain = "cmm_init_guard";
        } else {
            domain = "mcp_tool_guard";
        }
    }

    CommandResult seed_result;
    seed_result.fields["direct_answer"] = params.GetString("direct_answer");
    seed_result.fields["summary"] = params.GetString("summary");
    seed_result.fields["assistant_text"] = params.GetString("assistant_text");
    seed_result.fields["error"] = params.GetString("error");
    seed_result.fields["ai_conclusion_valid"] = params.GetString("ai_conclusion_valid", "true");
    seed_result.fields["task_completion"] = params.GetString("task_completion");
    seed_result.fields["has_more"] = params.GetString("has_more", "false");
    seed_result.fields["next_start_line"] = params.GetString("next_start_line");
    seed_result.fields["continue_required"] = params.GetString("continue_required", "false");
    seed_result.fields["auto_continue_required"] = params.GetString("auto_continue_required", "false");
    seed_result.fields["analysis_allowed"] = params.GetString("analysis_allowed", "true");
    seed_result.fields["batch_completion"] = params.GetString("batch_completion");
    seed_result.fields["remaining_batch_file_count"] = params.GetString("remaining_batch_file_count", "0");
    seed_result.fields["next_batch_file_path"] = params.GetString("next_batch_file_path");
    seed_result.fields["known_file_list_complete"] = params.GetString("known_file_list_complete");
    seed_result.fields["directory_listing_complete"] = params.GetString("directory_listing_complete");
    seed_result.fields["content_read_completion"] = params.GetString("content_read_completion");
    seed_result.fields["incomplete_scope"] = params.GetString("incomplete_scope");
    seed_result.fields["terminal_state"] = params.GetString("terminal_state");
    seed_result.fields["completion_claim_allowed"] = params.GetString("completion_claim_allowed");
    seed_result.fields["final_answer_allowed"] = params.GetString("final_answer_allowed");
    seed_result.fields["task_id"] = params.GetString("task_id");
    seed_result.fields["session_id"] = params.GetString("session_id");
    seed_result.fields["turn_id"] = params.GetString("turn_id");
    seed_result.fields["request_id"] = params.GetString("request_id");
    seed_result.fields["trace_id"] = params.GetString("trace_id");
    seed_result.fields["tool_call_id"] = params.GetString("tool_call_id");
    seed_result.fields["provider_id"] = params.GetString("provider_id");
    seed_result.fields["capability_id"] = params.GetString("capability_id");
    seed_result.fields["result_ref"] = params.GetString("result_ref");
    seed_result.fields["evidence_ref"] = params.GetString("evidence_ref");
    seed_result.fields["log_path"] = params.GetString("log_path");
    seed_result.fields["patch_id"] = params.GetString("patch_id");
    seed_result.fields["write_verified"] = params.GetString("write_verified");
    seed_result.fields["disk_write_completed"] = params.GetString("disk_write_completed");
    seed_result.fields["result_hash"] = params.GetString("result_hash");
    seed_result.fields["schema_version"] = params.GetString("schema_version");
    seed_result.fields["result_schema_id"] = params.GetString("result_schema_id");

    const CommandResult * result_ptr = domain == "mcp_result_guard" ? &seed_result : nullptr;
    const JsonRequestView * params_ptr = domain == "mcp_result_guard" ? nullptr : &params;
    const ClipsDecision decision = EvaluateClipsDecision(config, domain, tool_name, params_ptr, result_ptr);

    CommandResult result;
    result.ok = true;
    result.exit_code = 0;
    result.fields["tool_name"] = "lan_agent_clips_decide";
    result.fields["status"] = "ok";
    result.fields["result"] = "clips_decision_completed";
    result.fields["decision_result_status"] = "complete";
    result.fields["decision_domain"] = domain;
    result.fields["target_tool_name"] = tool_name;
    result.fields["decision"] = decision.decision;
    result.fields["target_verification"] = decision.verification;
    result.fields["verification"] = "verified";
    result.fields["next_action"] = decision.next_action;
    result.fields["reason_code"] = decision.reason_code;
    result.fields["matched_rule"] = decision.matched_rule;
    result.fields["route_target"] = decision.route_target;
    result.fields["schema"] =
        "fact_schema_id,decision_schema_id,decision,verification,next_action,reason_code,matched_rule,route_target,engine_status";
    if (domain == "mcp_result_guard") {
        result.fields["input_fact"] = BuildMcpToolResultFact(tool_name, seed_result);
    } else if (domain == "slice_ingest_guard") {
        result.fields["input_fact"] = BuildSliceIngestFact(params);
    } else if (domain == "cxparser_preflight_guard") {
        result.fields["input_fact"] = BuildCxparserFact(tool_name, params);
    } else {
        result.fields["input_fact"] = BuildMcpToolRequestFact(config, tool_name, params);
    }
    ApplyClipsDecisionFields(decision, "explicit", &result);

    // Debug: Add parameter summary for debugging
    result.fields["debug_file_path"] = params.GetString("file_path");
    result.fields["debug_probe_ready"] = params.GetBool("probe_ready", false) ? "true" : "false";
    result.fields["debug_primary_intent"] = params.GetString("primary_intent");
    result.fields["debug_tool_name"] = params.GetString("tool_name");
    result.fields["debug_domain"] = domain;
    result.fields["debug_decision"] = decision.decision;
    result.fields["debug_matched_rule"] = decision.matched_rule;
    result.fields["debug_engine_status"] = decision.engine_status;
    result.fields["debug_loaded_from_files"] = decision.loaded_from_files ? "true" : "false";
    result.fields["debug_input_fact"] = result.fields["input_fact"];

    return result;
}

CommandResult BuildClipsChainTemplateResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    (void) config;
    const std::string tool_name = FirstNonEmpty(
        params.GetString("tool_name"),
        params.GetString("name"),
        "lan_agent_clips_chain_template");
    const std::string phase = FirstNonEmpty(
        params.GetString("chain_phase"),
        params.GetString("phase"),
        "pre_call");

    CommandResult seed_result;
    seed_result.fields["task_id"] = params.GetString("task_id");
    seed_result.fields["result_ref"] = params.GetString("result_ref");
    seed_result.fields["evidence_ref"] = params.GetString("evidence_ref");
    seed_result.fields["analysis_only"] = params.GetString("analysis_only");
    seed_result.fields["execution_capability"] = params.GetString("execution_capability");

    const bool post_result_phase = phase == "post_result";
    CommandResult result;
    result.fields["status"] = "ok";
    result.fields["target_tool_name"] = tool_name;
    result.fields["chain_phase"] = phase;
    result.fields["chain_template_id"] = "mcp_tool_chain_v1";
    result.fields["request_type"] = ResolveMcpChainRequestType(tool_name);
    result.fields["risk"] = ResolveMcpChainRisk(tool_name);
    result.fields["safety_class"] = ResolveMcpChainSafetyClass(tool_name);
    result.fields["execution_class"] = ResolveMcpChainExecutionClass(tool_name);
    result.fields["evidence_policy"] = ResolveMcpChainEvidencePolicy(tool_name);
    result.fields["clips_required"] = "true";
    result.fields["rule_namespace"] = post_result_phase ? "mcp_result_guard" : "mcp_tool_guard";
    result.fields["input_fact"] = BuildMcpToolChainFact(
        tool_name,
        phase,
        post_result_phase ? nullptr : &params,
        post_result_phase ? &seed_result : nullptr);
    result.fields["template_contract"] =
        "Every MCP tool should emit mcp_tool_chain plus request/result facts; CLIPS rules own routing, verification, and next_action policy.";
    result.fields["next_action"] =
        "Add tool-specific CLIPS rules by matching mcp_tool_chain request_type, safety_class, execution_class, and evidence_policy.";
    result.fields["summary"] = "clips chain template returned";
    return result;
}

void AppendJsonStringField(
    std::string * json,
    bool * first_field,
    const char * key,
    const std::string & value) {
    if (json == nullptr || first_field == nullptr || key == nullptr || value.empty()) {
        return;
    }
    if (!*first_field) {
        *json += ",";
    }
    *json += "\"";
    *json += codex_lan_agent::JsonEscape(key);
    *json += "\":\"";
    *json += codex_lan_agent::JsonEscape(value);
    *json += "\"";
    *first_field = false;
}

void AppendJsonBoolField(
    std::string * json,
    bool * first_field,
    const char * key,
    bool value,
    bool emit_when_false = false) {
    if (json == nullptr || first_field == nullptr || key == nullptr) {
        return;
    }
    if (!value && !emit_when_false) {
        return;
    }
    if (!*first_field) {
        *json += ",";
    }
    *json += "\"";
    *json += codex_lan_agent::JsonEscape(key);
    *json += "\":";
    *json += (value ? "true" : "false");
    *first_field = false;
}

std::string BuildPreGuardRouteCallJson(
    const std::string & route_target,
    const JsonRequestView & params) {
    const std::string file_path = params.GetString("file_path");
    const std::string primary_intent = params.GetString("primary_intent");
    const std::string trace_id = params.GetString("trace_id");
    const std::string request_id = params.GetString("request_id");
    const bool recent_probe_ready = HasRecentProbePath(file_path);
    const std::string probe_ref = FirstNonEmpty(
        params.GetString("probe_ref"),
        params.GetString("probe_result_ref"),
        recent_probe_ready ? file_path : std::string(),
        "");
    std::string arguments_json = "{";
    bool first_field = true;
    AppendJsonStringField(&arguments_json, &first_field, "file_path", file_path);
    AppendJsonStringField(&arguments_json, &first_field, "primary_intent", primary_intent);
    AppendJsonStringField(&arguments_json, &first_field, "trace_id", trace_id);
    AppendJsonStringField(&arguments_json, &first_field, "request_id", request_id);

    if (route_target == "lan_agent_scan_text_ranges") {
        AppendJsonStringField(
            &arguments_json,
            &first_field,
            "scan_mode",
            params.GetString("scan_mode", "comments"));
        AppendJsonStringField(&arguments_json, &first_field, "probe_ref", probe_ref);
        AppendJsonBoolField(
            &arguments_json,
            &first_field,
            "probe_ready",
            !Trim(probe_ref).empty() || params.GetBool("probe_ready", false),
            !Trim(probe_ref).empty() || params.GetBool("probe_ready", false));
    }

    if (route_target == "lan_agent_delete_text_range_window_atomic") {
        AppendJsonStringField(
            &arguments_json,
            &first_field,
            "scan_mode",
            params.GetString("scan_mode", "comments"));
        const int start_line = std::max(1, params.GetInt("start_line", params.GetInt("next_start_line", 1)));
        const int max_lines = std::min(200, std::max(1, params.GetInt("max_lines", 200)));
        if (!first_field) {
            arguments_json += ",";
        }
        arguments_json += "\"start_line\":";
        arguments_json += std::to_string(start_line);
        arguments_json += ",\"next_start_line\":";
        arguments_json += std::to_string(start_line);
        arguments_json += ",\"max_lines\":";
        arguments_json += std::to_string(max_lines);
        first_field = false;
        AppendJsonStringField(&arguments_json, &first_field, "probe_ref", probe_ref);
        AppendJsonBoolField(
            &arguments_json,
            &first_field,
            "probe_ready",
            !Trim(probe_ref).empty() || params.GetBool("probe_ready", false),
            !Trim(probe_ref).empty() || params.GetBool("probe_ready", false));
    }

    if (route_target == "lan_agent_read_text_file") {
        const int start_line = params.GetInt("start_line", 1);
        const int max_lines = params.GetInt("max_lines", 500);
        if (!first_field) {
            arguments_json += ",";
        }
        arguments_json += "\"start_line\":";
        arguments_json += std::to_string(start_line);
        arguments_json += ",\"max_lines\":";
        arguments_json += std::to_string(max_lines);
        first_field = false;
        AppendJsonStringField(&arguments_json, &first_field, "probe_ref", probe_ref);
        AppendJsonBoolField(
            &arguments_json,
            &first_field,
            "probe_ready",
            !Trim(probe_ref).empty() || params.GetBool("probe_ready", false),
            !Trim(probe_ref).empty() || params.GetBool("probe_ready", false));
    }

    arguments_json += "}";
    return "{\"name\":\""
        + codex_lan_agent::JsonEscape(route_target)
        + "\",\"arguments\":"
        + arguments_json
        + "}";
}

void ApplyClipsPreflightRouteResult(
    const ClipsDecision & decision,
    const JsonRequestView & params,
    const std::string & fallback_error,
    CommandResult * result) {
    if (result == nullptr) {
        return;
    }
    const std::string route_target = FirstNonEmpty(decision.route_target, params.GetString("route_hint"), params.GetString("file_path").empty() ? std::string() : "lan_agent_probe_text_file");
    const std::string next_call_json = BuildPreGuardRouteCallJson(route_target, params);

    result->ok = true;
    result->exit_code = 0;
    result->fields["result"] = "pre_guard_rerouted";
    result->fields["error"].clear();
    result->fields["error_code"].clear();
    result->fields["error_message"].clear();
    result->fields["route_target"] = route_target;
    result->fields["clips_gate"] = "rerouted_before_execution";
    result->fields["summary"] = "pre-guard rerouted before execution";
    result->fields["verification"] = "not_verified";
    result->fields["verification_status"] = "not_verified";
    result->fields["verification_ok"] = "false";
    result->fields["semantic_model_clamp"] = "tool_call_only";
    result->fields["supervision_status"] = "closed_loop_continue";
    result->fields["goal_status"] = "not_complete";
    result->fields["assistant_response_allowed"] = "false";
    result->fields["final_answer_allowed"] = "false";
    result->fields["supervision_alarm"] = "false";
    result->fields["supervision_alarm_code"].clear();
    result->fields["supervision_alarm_message"].clear();
    result->fields["required_next_action_type"] = "mcp_tool_call";
    result->fields["required_tool_name"] = route_target;
    result->fields["required_tool_arguments_json"] = next_call_json;
    result->fields["next_call_json"] = next_call_json;
    result->fields["clips_continuation_required"] = "true";
    result->fields["clips_continuation_policy"] =
        "do not treat reroute as failure; call required_tool_arguments_json and continue the declared chain";
    result->fields["continue_required"] = "true";
    result->fields["auto_continue_required"] = "false";
    result->fields["analysis_allowed"] = "false";
    result->fields["status"] = "CONTINUE";
    result->fields["ai_conclusion_valid"] = "false";
    result->fields["invalid_conclusion_reason"] = FirstNonEmpty(
        decision.reason_code,
        fallback_error,
        "clips_route_required");
    if (!decision.next_action.empty()) {
        result->fields["next_action"] = decision.next_action;
    }
}

bool TryAutoExecuteCmmInitChain(
    const AgentConfig & config,
    const std::string & original_tool_name,
    const std::string & original_params_body,
    const ClipsDecision & decision,
    CommandResult * result);

bool MaybeApplyClipsPreflightBlock(
    const AgentConfig & config,
    const std::string & tool_name,
    const JsonRequestView & params,
    CommandResult * result) {
    if (!ToolRequiresClipsPreflight(tool_name) || result == nullptr) {
        return false;
    }
    const ClipsDecision tool_decision = EvaluateClipsDecision(
        config,
        "mcp_tool_guard",
        tool_name,
        &params,
        nullptr);
    ApplyClipsChainTemplateFields(tool_name, "pre_call_tool", result);
    ApplyClipsDecisionFields(tool_decision, "pre_call_tool", result);
    result->fields["clips_pre_call_tool_chain_fact"] =
        BuildMcpToolChainFact(tool_name, "pre_call", &params, nullptr);
    result->fields["clips_pre_call_tool_fact"] = BuildMcpToolRequestFact(config, tool_name, params);
    if (tool_decision.decision == "block") {
        result->ok = false;
        result->exit_code = 52;
        result->fields["error"] = FirstNonEmpty(
            tool_decision.reason_code,
            "clips_tool_call_blocked",
            "clips_tool_call_blocked");
        result->fields["error_code"] = result->fields["error"];
        result->fields["error_message"] = FirstNonEmpty(
            tool_decision.next_action,
            tool_decision.reason_code,
            "CLIPS pre-guard blocked the tool call before execution.");
        result->fields["failure_mode"] = result->fields["error"];
        if (!tool_decision.next_action.empty()) {
            result->fields["next_action"] = tool_decision.next_action;
        }
        result->fields["clips_gate"] = "blocked_before_execution";
        result->fields["verification"] = "not_verified";
        result->fields["verification_status"] = "not_verified";
        result->fields["verification_ok"] = "false";
        result->fields["semantic_model_clamp"] = "supervision_alarm";
        result->fields["supervision_status"] = "alarm";
        result->fields["goal_status"] = "failed";
        result->fields["assistant_response_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
        result->fields["supervision_alarm"] = "true";
        result->fields["supervision_alarm_code"] = "PRE_GUARD_BLOCKED";
        result->fields["supervision_alarm_message"] =
            "CLIPS pre-guard blocked the tool call before execution.";
        return true;
    }
    if (tool_decision.decision == "route") {
        if (TryAutoExecuteCmmInitChain(
                config,
                tool_name,
                params.body(),
                tool_decision,
                result)) {
            return true;
        }
        ApplyClipsPreflightRouteResult(
            tool_decision,
            params,
            "clips_tool_call_rerouted",
            result);
        return true;
    }

    const std::string preflight_domain = ResolveClipsPreflightDomain(tool_name);
    if (preflight_domain == "mcp_tool_guard") {
        return false;
    }
    const ClipsDecision decision = EvaluateClipsDecision(
        config,
        preflight_domain,
        tool_name,
        &params,
        nullptr);
    ApplyClipsChainTemplateFields(tool_name, "pre_call", result);
    ApplyClipsDecisionFields(decision, "pre_call", result);
    result->fields["clips_pre_call_chain_fact"] =
        BuildMcpToolChainFact(tool_name, "pre_call", &params, nullptr);
    result->fields["clips_pre_call_fact"] = BuildMcpToolRequestFact(config, tool_name, params);
    if (preflight_domain == "cxparser_preflight_guard") {
        result->fields["clips_cxparser_fact"] = BuildCxparserFact(tool_name, params);
    }
    if (preflight_domain == "cmm_init_guard") {
        result->fields["clips_cmm_project_fact"] = BuildCmmProjectStateFact(params);
    }
    if (decision.decision == "block") {
        result->ok = false;
        result->exit_code = 52;
        result->fields["error"] = FirstNonEmpty(
            decision.reason_code,
            "clips_pre_call_blocked",
            "clips_pre_call_blocked");
        result->fields["error_code"] = result->fields["error"];
        result->fields["error_message"] = FirstNonEmpty(
            decision.next_action,
            decision.reason_code,
            "CLIPS pre-guard blocked the tool call before execution.");
        result->fields["failure_mode"] = result->fields["error"];
        if (!decision.next_action.empty()) {
            result->fields["next_action"] = decision.next_action;
        }
        result->fields["clips_gate"] = "blocked_before_execution";
        result->fields["verification"] = "not_verified";
        result->fields["verification_status"] = "not_verified";
        result->fields["verification_ok"] = "false";
        result->fields["semantic_model_clamp"] = "supervision_alarm";
        result->fields["supervision_status"] = "alarm";
        result->fields["goal_status"] = "failed";
        result->fields["assistant_response_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
        result->fields["supervision_alarm"] = "true";
        result->fields["supervision_alarm_code"] = "PRE_GUARD_BLOCKED";
        result->fields["supervision_alarm_message"] =
            "CLIPS pre-guard blocked the tool call before execution.";
        return true;
    }
    if (decision.decision == "route") {
        if (TryAutoExecuteCmmInitChain(
                config,
                tool_name,
                params.body(),
                decision,
                result)) {
            return true;
        }
        ApplyClipsPreflightRouteResult(
            decision,
            params,
            "clips_pre_call_rerouted",
            result);
        return true;
    }
    return false;
}

void ApplyClipsResultGuard(
    const AgentConfig & config,
    const std::string & tool_name,
    CommandResult * result) {
    if (!ToolRequiresClipsResultGuard(tool_name) || result == nullptr) {
        return;
    }
    const ClipsDecision decision = EvaluateClipsDecision(
        config,
        "mcp_result_guard",
        tool_name,
        nullptr,
        result);
    ApplyClipsChainTemplateFields(tool_name, "post_result", result);
    ApplyClipsDecisionFields(decision, "post_result", result);
    result->fields["clips_post_result_chain_fact"] =
        BuildMcpToolChainFact(tool_name, "post_result", nullptr, result);
    const bool pending_text_range_delete =
        (tool_name == "lan_agent_delete_text_range_window_atomic"
            || tool_name == "lan_agent_delete_next_text_range_atomic")
        && (GetFieldOrDefault(*result, "has_more", "false") == "true"
            || GetFieldOrDefault(*result, "continue_required", "false") == "true"
            || GetFieldOrDefault(*result, "status", "") == "needs_continue"
            || GetFieldOrDefault(*result, "task_completion", "") == "window_applied"
            || GetFieldOrDefault(*result, "task_completion", "") == "single_item_applied"
            || GetFieldOrDefault(*result, "task_completion", "") == "boundary_blocked");
    const std::string existing_verification = GetFieldOrDefault(*result, "verification", "");
    const bool invalid_existing_verification =
        existing_verification == "invalid" || existing_verification == "not_verified";
    const bool expected_structured_failure =
        GetFieldOrDefault(*result, "result", "") == "preflight_blocked"
        || GetFieldOrDefault(*result, "preflight_status", "") == "blocked"
        || GetFieldOrDefault(*result, "failure_mode", "") == "task_evicted_from_history";
    const bool expected_structured_decision =
        tool_name == "lan_agent_clips_decide"
        && GetFieldOrDefault(*result, "result", "") == "clips_decision_completed"
        && (GetFieldOrDefault(*result, "decision", "") == "block"
            || GetFieldOrDefault(*result, "decision", "") == "route"
            || GetFieldOrDefault(*result, "decision", "") == "allow");
    const std::string effective_verification =
        pending_text_range_delete
            ? "not_verified"
        : (expected_structured_failure || expected_structured_decision)
            ? "verified"
            : (invalid_existing_verification
            ? existing_verification
            : (!result->ok ? "not_verified" : decision.verification));
    result->fields["verification"] = effective_verification;
    result->fields["verification_status"] = effective_verification;
    result->fields["verification_ok"] = effective_verification == "verified" ? "true" : "false";
    if (effective_verification != "verified") {
        const std::string fallback_reason = FirstNonEmpty(
            GetFieldOrDefault(*result, "invalid_conclusion_reason", ""),
            GetFieldOrDefault(*result, "error", ""),
            "clips_not_verified");
        result->fields["not_verified_reason"] = FirstNonEmpty(
            decision.reason_code,
            fallback_reason,
            "clips_not_verified");
        if (!decision.next_action.empty()) {
            result->fields["next_action"] = decision.next_action;
        }
    } else if (expected_structured_failure || expected_structured_decision) {
        result->fields["not_verified_reason"].clear();
    }
    if (decision.decision == "route" || pending_text_range_delete) {
        const std::string required_tool = FirstNonEmpty(
            decision.route_target,
            GetFieldOrDefault(*result, "next_tool_name", ""),
            tool_name);
        const std::string required_arguments = GetFieldOrDefault(*result, "next_call_json", "");
        result->fields["semantic_model_clamp"] = "tool_call_only";
        result->fields["assistant_response_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
        result->fields["required_next_action_type"] = "mcp_tool_call";
        result->fields["required_tool_name"] = required_tool;
        result->fields["required_tool_arguments_json"] = required_arguments;
        result->fields["route_target"] = required_tool;
        result->fields["clips_continuation_required"] = "true";
        result->fields["clips_continuation_policy"] =
            "do not return assistant text; call required_tool_arguments_json until CLIPS returns verification=verified";
        if (GetFieldOrDefault(*result, "invalid_conclusion_reason", "").empty()) {
            result->fields["invalid_conclusion_reason"] = FirstNonEmpty(
                decision.reason_code,
                pending_text_range_delete ? "text_range_delete_incomplete" : "",
                "clips_route_required",
                "clips_route_required");
        }
        result->fields["ai_conclusion_valid"] = "false";
        result->fields["supervision_status"] = "closed_loop_continue";
        result->fields["goal_status"] = "not_complete";
        result->fields["terminal_state"] = "false";
        result->fields["task_done"] = "false";
        result->fields["completion_claim_allowed"] = "false";
        result->fields["must_continue_until"] = "has_more=false";
        result->fields["completion_guard"] =
            "NON_TERMINAL_RESULT: do not claim completion; execute required_tool_arguments_json";
        if (pending_text_range_delete && GetFieldOrDefault(*result, "next_action", "").empty()) {
            result->fields["next_action"] =
                "tool_call_only: continue the delete loop until has_more=false before any final completion claim";
        }
    } else if (GetFieldOrDefault(*result, "semantic_model_clamp", "").empty()) {
        result->fields["semantic_model_clamp"] = "none";
        result->fields["assistant_response_allowed"] = "true";
        result->fields["final_answer_allowed"] = "true";
        if (GetFieldOrDefault(*result, "terminal_state", "").empty()) {
            result->fields["terminal_state"] = "true";
        }
        if (GetFieldOrDefault(*result, "task_done", "").empty()) {
            result->fields["task_done"] = "true";
        }
        if (GetFieldOrDefault(*result, "completion_claim_allowed", "").empty()) {
            result->fields["completion_claim_allowed"] = "true";
        }
        if (GetFieldOrDefault(*result, "must_continue_until", "").empty()) {
            result->fields["must_continue_until"] = "";
        }
    }
    result->fields["status"] = ResolveResultEnvelopeStatus(*result);
    if (GetFieldOrDefault(*result, "error_message", "").empty()) {
        result->fields["error_message"] = GetFieldOrDefault(*result, "error", "");
    }
    if (GetFieldOrDefault(*result, "error_code", "").empty()) {
        if (result->fields["status"] == "INVALID") {
            result->fields["error_code"] = "invalid_result";
        } else if (result->fields["status"] == "FAILED") {
            result->fields["error_code"] = "tool_exit_" + std::to_string(result->exit_code);
        }
    }
    result->fields["result_hash"] = BuildResultEnvelopeHash(*result);
    result->fields["clips_post_result_fact"] = BuildMcpToolResultFact(tool_name, *result);
}
