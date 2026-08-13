#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "clips_core/clips.h"
#include "clips_core/factfun.h"

#ifdef __cplusplus
}
#endif

#include "SemanticIntentLexicon.h"
#include <set>

// FactFactory 守卫层归一化钩子（可选，无外部依赖时为 stub）
//   返回: 归一后的标准 tag；若未匹配则返回空字符串，调用方回退原始值
std::string ApplyFactFactoryNormalizePrimaryIntent(const std::string & raw_intent);
//   对任意 slot 值做字节硬过滤 + CLIPS 安全加固（即使无外部依赖也生效）
std::string ApplyFactFactoryByteSanitizeSlot(const std::string & raw_value, bool is_token_slot);

std::string NormalizeMcpPrimaryIntentForClips(const std::string & primary_intent);

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
    std::string route_arguments_json;
    std::string fact_schema_id = "mcp_fact_schema_v1";
    std::string decision_schema_id = "clips_decision_schema_v1";
    std::string rule_root;
    std::string loaded_files;
    std::string engine_status = "not_started";
    int asserted_fact_count = 0;
    // — CLIPS 内部熔断器状态 —
    int fact_count_before_run = 0;          // Run 前已存在 fact 数
    int fact_count_after_run = 0;           // Run 后总 fact 数
    int rule_firings_actual = 0;            // Run 实际触发的规则数
    int duplicate_facts_blocked = 0;        // 被去重拦截的重复 fact 数
    bool circuit_breaker_facts_exceeded = false;  // 熔断①：fact 数量超限
    bool circuit_breaker_rules_exceeded = false;  // 熔断③：规则触发次数超限
};

// — CLIPS 熔断器阈值（编译期常量，可按需调整）—
constexpr int CLIPS_MAX_FACTS_PER_SESSION   = 500;   // 熔断①：单次 EvaluateClipsDecision 最大 fact 总数
constexpr int CLIPS_MAX_RULE_FIRINGS        = 200;   // 熔断③：Run 最多触发的规则数（替代 -1 无限）

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

bool IsClipsSafeTokenChar(char ch) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) != 0
        || ch == '_'
        || ch == '-'
        || ch == '.'
        || ch == ':'
        || ch == '/';
}

bool IsClipsSafeTokenString(const std::string & value) {
    if (value.size() > 160) {
        return false;
    }
    for (char ch : value) {
        if (!IsClipsSafeTokenChar(ch)) {
            return false;
        }
    }
    return true;
}

std::string NormalizeClipsCharsByTable(const std::string & value) {
    std::string output;
    output.reserve(value.size() + 16);
    bool last_underscore = false;
    auto append_token = [&](const std::string & token) {
        if (token == "_") {
            if (!last_underscore && !output.empty()) {
                output.push_back('_');
                last_underscore = true;
            }
            return;
        }
        output += token;
        last_underscore = !token.empty() && token.back() == '_';
    };
    const auto append_hex_token = [&](unsigned char ch) {
        static const char * digits = "0123456789abcdef";
        std::string token = "_x";
        token.push_back(digits[(ch >> 4) & 0x0f]);
        token.push_back(digits[ch & 0x0f]);
        token.push_back('_');
        output += token;
        last_underscore = true;
    };
    for (unsigned char ch : value) {
        if (std::isalnum(ch) != 0) {
            output.push_back(static_cast<char>(std::tolower(ch)));
            last_underscore = false;
            continue;
        }
        switch (ch) {
        case '_': append_token("_"); break;
        case '-': output.push_back('-'); last_underscore = false; break;
        case '.': output.push_back('.'); last_underscore = false; break;
        case ':': output.push_back(':'); last_underscore = false; break;
        case '/': output.push_back('/'); last_underscore = false; break;
        case '\\': output.push_back('/'); last_underscore = false; break;
        case ' ':
        case '\t': append_token("_"); break;
        case '\r': append_token("_cr_"); break;
        case '\n': append_token("_nl_"); break;
        case '"': append_token("_dq_"); break;
        case '\'': append_token("_sq_"); break;
        case '{': append_token("_lb_"); break;
        case '}': append_token("_rb_"); break;
        case '[': append_token("_ls_"); break;
        case ']': append_token("_rs_"); break;
        case '(': append_token("_lp_"); break;
        case ')': append_token("_rp_"); break;
        case ',': append_token("_cm_"); break;
        case ';': append_token("_sc_"); break;
        case '=': append_token("_eq_"); break;
        case '&': append_token("_and_"); break;
        case '|': append_token("_or_"); break;
        case '*': append_token("_star_"); break;
        case '?': append_token("_q_"); break;
        case '#': append_token("_hash_"); break;
        case '%': append_token("_pct_"); break;
        default:
            append_hex_token(ch);
            break;
        }
    }
    while (!output.empty() && output.front() == '_') {
        output.erase(output.begin());
    }
    while (!output.empty() && output.back() == '_') {
        output.pop_back();
    }
    return output.empty() ? "empty" : output;
}

std::string NormalizeClipsSlotValue(const char * name, const std::string & value) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return std::string();
    }
    const std::string slot_name = name == nullptr ? std::string() : std::string(name);
    if (slot_name == "primary_intent") {
        const std::string normalized_intent = NormalizeMcpPrimaryIntentForClips(trimmed);
        if (IsClipsSafeTokenString(normalized_intent)) {
            return normalized_intent;
        }
        return "intent_" + NormalizeClipsCharsByTable(normalized_intent);
    }
    const std::string table_value = NormalizeClipsCharsByTable(trimmed);
    const std::string bounded_value = table_value.size() <= 160
        ? table_value
        : (table_value.substr(0, 120) + "_h_" + StableContentChecksum(trimmed));
    if (slot_name.find("json") != std::string::npos
        || slot_name.find("text") != std::string::npos
        || slot_name.find("summary") != std::string::npos
        || slot_name.find("answer") != std::string::npos
        || slot_name.find("path") != std::string::npos
        || slot_name.find("ref") != std::string::npos
        || slot_name.find("error") != std::string::npos
        || slot_name.find("action") != std::string::npos
        || slot_name.find("fact") != std::string::npos) {
        return bounded_value;
    }
    return bounded_value;
}

std::string NormalizeMcpPrimaryIntentForClips(const std::string & primary_intent) {
    // 1. 优先走 fact-factory 守卫层，命中标准 tag 直接落地
    const std::string factory_tag = ApplyFactFactoryNormalizePrimaryIntent(primary_intent);
    if (!factory_tag.empty()) {
        return factory_tag;
    }
    // 2. 回退旧的 SemanticIntentLexicon 词典
    return codex_lan_agent::NormalizeIntentBySemanticLexicon(primary_intent);
}

std::string ClipsStringSlot(const char * name, const std::string & value) {
    return "(" + std::string(name) + " \"" + EscapeForClipsString(NormalizeClipsSlotValue(name, value)) + "\")";
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
              (slot directory_path (default ""))
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
              (slot requires_post_verify (default "false"))
              (slot pending_continuation_active (default "false"))
              (slot pending_required_tool (default ""))
              (slot pending_required_arguments_json (default ""))
              (slot pending_trace_id (default ""))
              (slot pending_goal_id (default ""))
              (slot pending_source_tool (default ""))
              (slot pending_hash (default ""))
              (slot pending_trace_match (default "false"))
              (slot continuation_takeover_allowed (default "true"))))",
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
              (slot tool_use_decision (default ""))
              (slot task_completion (default ""))
              (slot has_more (default "false"))
              (slot next_start_line (default ""))
              (slot next_call_json (default ""))
              (slot continue_required (default "false"))
              (slot auto_continue_required (default "false"))
              (slot analysis_allowed (default "true"))
              (slot batch_completion (default ""))
              (slot remaining_batch_file_count (default "0"))
              (slot next_batch_file_path (default ""))
              (slot known_file_list_complete (default ""))
              (slot directory_listing_complete (default ""))
              (slot directory_scope_active (default "false"))
              (slot directory_manifest_path (default ""))
              (slot directory_current_file_index (default "0"))
              (slot directory_next_file_index (default ""))
              (slot directory_total_code_file_count (default "0"))
              (slot directory_remaining_code_file_count (default "0"))
              (slot directory_scope_incomplete (default "false"))
              (slot directory_next_probe_call_json (default ""))
              (slot flow_id (default ""))
              (slot flow_task_list_required (default "false"))
              (slot flow_current_task_id (default ""))
              (slot flow_next_task_id (default ""))
              (slot flow_task_list_path (default ""))
              (slot flow_task_list_md_path (default ""))
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
              (slot route_target (default ""))
              (slot route_arguments_json (default ""))))",
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
            R"((defrule route-mismatched-pending-continuation
              (declare (salience 96))
              (mcp_tool_request (tool_name ?tool)
                                (pending_continuation_active "true")
                                (continuation_takeover_allowed "true")
                                (pending_required_tool ?expected&:(neq ?expected ""))
                                (pending_required_arguments_json ?args&:(neq ?args "")))
              (test (neq ?expected ?tool))
              =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "pending_continuation_mismatch")
                      (route_target ?expected)
                      (next_action "tool_call_only: a pending continuation already exists for this trace/goal; call its route_arguments_json exactly before any other tool")
                      (matched_rule "route-mismatched-pending-continuation")))))",
            R"((defrule route-pending-continuation-same-tool-missing-context
              (declare (salience 95))
              (mcp_tool_request (tool_name ?tool)
                                (trace_id ?trace)
                                (pending_continuation_active "true")
                                (continuation_takeover_allowed "true")
                                (pending_required_tool ?expected&:(neq ?expected ""))
                                (pending_required_arguments_json ?args&:(neq ?args ""))
                                (pending_trace_id ?pending_trace&:(neq ?pending_trace "")))
              (test (eq ?expected ?tool))
              (test (neq ?pending_trace ?trace))
              =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "pending_continuation_context_mismatch")
                      (route_target ?expected)
                      (next_action "tool_call_only: this is the expected tool but the saved continuation context is missing or changed; call route_arguments_json exactly")
                      (matched_rule "route-pending-continuation-same-tool-missing-context")))))",
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
            R"((defrule route-code-format-cleanup-to-clang-format
                  (declare (salience 85))
                  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_read_text_file")
                                                          (eq ?tool "lan_agent_scan_text_ranges")
                                                          (eq ?tool "lan_agent_prepare_edit_windows")
                                                          (eq ?tool "lan_agent_delete_line_atomic")
                                                          (eq ?tool "lan_agent_delete_content_atomic")
                                                          (eq ?tool "lan_agent_delete_next_text_range_atomic")
                                                          (eq ?tool "lan_agent_delete_text_range_window_atomic")
                                                          (eq ?tool "lan_agent_write_text_file")
                                                          (eq ?tool "lan_agent_apply_single_file_patch")
                                                          (eq ?tool "lan_agent_apply_diff_patch")))
                                    (primary_intent "code_format")
                                    (file_path ?file_path&:(neq ?file_path "")))
                  =>
                  (assert (clips_decision
                      (domain "mcp_tool_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "verified")
                      (reason_code "code_format_cleanup_prefers_clang_format")
                      (route_target "lan_agent_format_code_file")
                      (next_action "use lan_agent_format_code_file dry_run=true first for whitespace/newline/code-format cleanup; do not scan comments or delete text ranges for formatting")
                      (matched_rule "route-code-format-cleanup-to-clang-format")))))",
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
                                    (primary_intent "comment_cleanup")
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
                                                          (eq ?tool "lan_agent_prepare_edit_windows")))
                                    (primary_intent "comment_cleanup")
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
                                   (completion_claim_allowed "false")
                                   (next_call_json ?next&:(neq ?next "")))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "non_terminal_result_forbids_final_answer")
                      (next_action "tool_call_only: result is non-terminal and declares next_call_json; execute that continuation before any completion claim")
                      (route_target ?tool)
                      (matched_rule "non-terminal-result-forbids-final-answer")))))",
            R"((defrule directory-scope-next-file-requires-probe
                  (declare (salience 56))
                  (mcp_tool_result (tool_name ?tool)
                                   (directory_scope_active "true")
                                   (directory_scope_incomplete "true")
                                   (directory_next_probe_call_json ?next&:(neq ?next "")))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "directory_scope_next_file_required")
                      (next_action "tool_call_only: directory cleanup still has unprocessed files; call directory_next_probe_call_json for the next file before any completion claim")
                      (route_target "lan_agent_probe_text_file")
                      (matched_rule "directory-scope-next-file-requires-probe")))))",
            R"((defrule directory-scope-remaining-files-forbid-terminal
                  (declare (salience 55))
                  (mcp_tool_result (tool_name ?tool)
                                   (directory_scope_active "true")
                                   (terminal_state "true")
                                   (directory_remaining_code_file_count ?remaining&:(and (neq ?remaining "") (neq ?remaining "0"))))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "directory_scope_remaining_files_forbid_terminal")
                      (next_action "tool_call_only: directory-scope operation cannot be terminal while remaining files are recorded")
                      (route_target "lan_agent_probe_text_file")
                      (matched_rule "directory-scope-remaining-files-forbid-terminal")))))",
            R"((defrule declared-next-call-json-requires-continuation
                  (declare (salience 43))
                  (mcp_tool_result (tool_name ?tool)
                                   (next_call_json ?next&:(neq ?next "")))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "declared_next_call_requires_continuation")
                      (next_action "tool_call_only: result declared next_call_json; execute that continuation before any completion claim")
                      (matched_rule "declared-next-call-json-requires-continuation")))))",
            R"((defrule final-answer-disallowed-by-result
                  (declare (salience 46))
                  (mcp_tool_result (tool_name ?tool)
                                   (final_answer_allowed "false")
                                   (next_call_json ?next&:(neq ?next "")))
                  =>
                  (assert (clips_decision
                      (domain "mcp_result_guard")
                      (target ?tool)
                      (decision "route")
                      (verification "not_verified")
                      (reason_code "final_answer_disallowed_by_result")
                      (next_action "tool_call_only: the tool result disallows final answer and declares next_call_json; execute that continuation or use task_memory_execute_continuation_budget")
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
                                                          (eq ?tool "lan_agent_read_directory_files")
                                                          (eq ?tool "lan_agent_run_cxparser_flow")))
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

bool AllowsPendingContinuationTakeover(const std::string & tool_name) {
    if (tool_name == "lan_agent_mcp_route"
        || tool_name == "lan_agent_mcp_overview"
        || tool_name == "lan_agent_remote_mcp_overview"
        || tool_name == "lan_agent_clips_decide"
        || tool_name == "lan_agent_flow_task_list"
        || tool_name == "lan_agent_mcp_flow_visualize"
        || tool_name == "lan_agent_mcp_flow_analyze"
        || tool_name == "lan_agent_mcp_flow_export"
        || tool_name == "lan_agent_mcp_flow_conformance_check"
        || tool_name == "lan_agent_health"
        || tool_name == "mcp_capability_registry") {
        return false;
    }
    if (tool_name.find("_overview") != std::string::npos
        || tool_name.find("_status") != std::string::npos
        || tool_name.find("dashboard") != std::string::npos
        || tool_name.find("flow_visualize") != std::string::npos
        || tool_name.find("flow_analyze") != std::string::npos
        || tool_name.find("flow_export") != std::string::npos) {
        return false;
    }
    return true;
}

std::string ClipsContinuationArgumentsFactValue(const std::string & raw_arguments_json) {
    return Trim(raw_arguments_json).empty()
        ? std::string()
        : "__mcp_pending_required_arguments_json_ref__";
}

bool IsClipsContinuationArgumentsRef(const std::string & value) {
    const std::string trimmed = Trim(value);
    return trimmed == "__mcp_pending_required_arguments_json_ref__"
        || NormalizeClipsCharsByTable(trimmed) == "mcp_pending_required_arguments_json_ref";
}

std::string WriteClipsRouteArgumentsRef(
    const AgentConfig & config,
    const std::string & tool_name,
    const std::string & route_arguments_json) {
    if (Trim(route_arguments_json).empty()) {
        return std::string();
    }
    const std::string key = StableContentChecksum(tool_name + "\n" + route_arguments_json);
    const std::filesystem::path root =
        std::filesystem::path(config.log_root) / "clips_route_arguments";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    const std::filesystem::path path =
        root / (SanitizeDispatchToken(tool_name, "tool") + "-" + key + ".json");
    std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!output.is_open()) {
        return std::string();
    }
    output << route_arguments_json;
    return path.string();
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

struct McpPendingContinuationFields {
    bool active = false;
    std::string required_tool;
    std::string required_arguments_json;
    std::string trace_id;
    std::string goal_id;
    std::string source_tool;
    std::string hash;
    std::string path;
};

std::filesystem::path BuildMcpPendingContinuationRoot(const AgentConfig & config) {
    return std::filesystem::path(config.log_root) / "mcp_pending_continuations";
}

std::filesystem::path BuildMcpPendingContinuationPath(
    const AgentConfig & config,
    const std::string & key) {
    return BuildMcpPendingContinuationRoot(config)
        / (SanitizeDispatchToken(key, "trace") + ".kv");
}

std::string BuildMcpPendingContinuationPathKey(const std::string & path_text) {
    const std::string trimmed = Trim(path_text);
    if (trimmed.empty()) {
        return std::string();
    }
    std::filesystem::path path(trimmed);
    std::string normalized = path.lexically_normal().string();
    if (normalized.empty()) {
        normalized = trimmed;
    }
    return "path-" + StableContentChecksum(ToLowerAscii(normalized));
}

bool LoadMcpPendingContinuationByKey(
    const AgentConfig & config,
    const std::string & key,
    McpPendingContinuationFields * pending) {
    if (pending == nullptr || Trim(key).empty()) {
        return false;
    }
    const std::filesystem::path path = BuildMcpPendingContinuationPath(config, key);
    std::string content;
    std::string read_error;
    if (!ReadWholeFile(path, &content, &read_error)) {
        return false;
    }
    std::unordered_map<std::string, std::string> fields;
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        fields[line.substr(0, separator)] = line.substr(separator + 1);
    }
    if (fields["pending_continuation_active"] != "true"
        || fields["pending_required_tool"].empty()
        || fields["pending_required_arguments_json"].empty()) {
        return false;
    }
    pending->active = true;
    pending->required_tool = fields["pending_required_tool"];
    pending->required_arguments_json = fields["pending_required_arguments_json"];
    pending->trace_id = fields["pending_trace_id"];
    pending->goal_id = fields["pending_goal_id"];
    pending->source_tool = fields["pending_source_tool"];
    pending->hash = fields["pending_hash"];
    pending->path = path.string();
    return true;
}

McpPendingContinuationFields LoadMcpPendingContinuationForParams(
    const AgentConfig & config,
    const JsonRequestView & params) {
    McpPendingContinuationFields pending;
    const std::string trace_id = params.GetString("trace_id");
    const std::string goal_id = FirstNonEmpty(
        params.GetString("goal_id"),
        params.GetString("task_id"),
        "");
    if (LoadMcpPendingContinuationByKey(config, trace_id, &pending)) {
        return pending;
    }
    if (LoadMcpPendingContinuationByKey(config, goal_id, &pending)) {
        return pending;
    }
    const std::string path_key = BuildMcpPendingContinuationPathKey(FirstNonEmpty(
        params.GetString("file_path"),
        params.GetString("source_file"),
        params.GetString("directory_path"),
        params.GetString("normalized_path"),
        ""));
    LoadMcpPendingContinuationByKey(config, path_key, &pending);
    return pending;
}

void WriteMcpPendingContinuationByKey(
    const AgentConfig & config,
    const std::string & key,
    const CommandResult & result,
    const std::string & source_tool,
    bool active) {
    if (Trim(key).empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(BuildMcpPendingContinuationRoot(config), ec);
    const std::filesystem::path path = BuildMcpPendingContinuationPath(config, key);
    if (!active) {
        std::filesystem::remove(path, ec);
        return;
    }
    const std::string required_tool = GetFieldOrDefault(result, "required_tool_name", "");
    const std::string required_arguments_json =
        GetFieldOrDefault(result, "required_tool_arguments_json", "");
    if (required_tool.empty() || required_arguments_json.empty()) {
        return;
    }
    std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!output.is_open()) {
        return;
    }
    const std::string trace_id = GetFieldOrDefault(result, "trace_id", "");
    const std::string goal_id = GetFieldOrDefault(result, "goal_id", "");
    const std::string hash = StableContentChecksum(required_tool + "\n" + required_arguments_json);
    output << "pending_continuation_active=true\n"
           << "pending_required_tool=" << required_tool << "\n"
           << "pending_required_arguments_json=" << required_arguments_json << "\n"
           << "pending_trace_id=" << trace_id << "\n"
           << "pending_goal_id=" << goal_id << "\n"
           << "pending_source_tool=" << source_tool << "\n"
           << "pending_hash=" << hash << "\n"
           << "stored_at=" << IsoTimestampNow() << "\n";
}

void PersistMcpPendingContinuation(
    const AgentConfig & config,
    const std::string & source_tool,
    const CommandResult & result) {
    const std::string trace_id = GetFieldOrDefault(result, "trace_id", "");
    const std::string goal_id = GetFieldOrDefault(result, "goal_id", "");
    const bool continuation_active =
        GetFieldOrDefault(result, "continue_required", "false") == "true"
        && GetFieldOrDefault(result, "terminal_state", "") != "true"
        && !GetFieldOrDefault(result, "required_tool_name", "").empty()
        && !GetFieldOrDefault(result, "required_tool_arguments_json", "").empty();
    WriteMcpPendingContinuationByKey(config, trace_id, result, source_tool, continuation_active);
    if (!goal_id.empty() && goal_id != trace_id) {
        WriteMcpPendingContinuationByKey(config, goal_id, result, source_tool, continuation_active);
    }
    const std::string required_arguments_json =
        GetFieldOrDefault(result, "required_tool_arguments_json", "");
    const std::vector<std::string> path_values = {
        GetFieldOrDefault(result, "file_path", ""),
        GetFieldOrDefault(result, "source_file", ""),
        GetFieldOrDefault(result, "directory_path", ""),
        GetFieldOrDefault(result, "normalized_path", ""),
        GetFieldOrDefault(result, "next_file_path", ""),
        GetFieldOrDefault(result, "next_batch_file_path", ""),
        ExtractJsonString(required_arguments_json, "file_path"),
        ExtractJsonString(required_arguments_json, "source_file"),
        ExtractJsonString(required_arguments_json, "directory_path")
    };
    for (const std::string & path_value : path_values) {
        const std::string path_key = BuildMcpPendingContinuationPathKey(path_value);
        if (!path_key.empty()) {
            WriteMcpPendingContinuationByKey(config, path_key, result, source_tool, continuation_active);
        }
    }
}

std::string BuildMcpToolRequestFact(
    const AgentConfig & config,
    const std::string & tool_name,
    const JsonRequestView & params) {
    std::string file_path = params.GetString("file_path");
    if (file_path.empty() && tool_name.find("lan_agent_cmm_") != std::string::npos) {
        file_path = params.GetString("project");
    }
    const std::string directory_path = params.GetString("directory_path");
    const bool recent_probe_ready = HasRecentProbePath(file_path);
    const std::string probe_ref = FirstNonEmpty(
        params.GetString("probe_ref"),
        params.GetString("probe_result_ref"),
        recent_probe_ready ? file_path : std::string(),
        "");
    const std::string patch_id = params.GetString("patch_id");
    const std::string reason = params.GetString("reason");
    const std::string normalized_primary_intent =
        NormalizeMcpPrimaryIntentForClips(params.GetString("primary_intent"));
    const bool path_within_workspace = file_path.empty() ? true : IsWorkspacePatchPath(config, file_path);
    const bool preview_ready =
        tool_name == "lan_agent_apply_single_file_patch" ? HasPatchPreviewAuditEvent(config, patch_id) : false;
    const bool explicit_user_intent =
        !Trim(reason).empty() || !Trim(params.GetString("repair_candidate_id")).empty() || normalized_primary_intent == "refactor_file";
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
    McpPendingContinuationFields pending =
        LoadMcpPendingContinuationForParams(config, params);
    if (params.GetBool("pending_continuation_active", false)
        || ToLowerAscii(params.GetString("pending_continuation_active")) == "true") {
        pending.active = true;
        pending.required_tool = params.GetString("pending_required_tool");
        pending.required_arguments_json = params.GetString("pending_required_arguments_json");
        pending.trace_id = params.GetString("pending_trace_id");
        pending.goal_id = params.GetString("pending_goal_id");
        pending.source_tool = params.GetString("pending_source_tool");
        pending.hash = params.GetString("pending_hash");
    }
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
        + ClipsStringSlot("primary_intent", normalized_primary_intent) + " "
        + ClipsStringSlot("preflight_status", preflight_status) + " "
        + ClipsStringSlot("dedup_status", params.GetString("dedup_status")) + " "
        + ClipsStringSlot("canonical_slice_id", params.GetString("canonical_slice_id")) + " "
        + ClipsStringSlot("dup_of", params.GetString("dup_of")) + " "
        + ClipsStringSlot("route_hint", params.GetString("route_hint")) + " "
        + ClipsStringSlot("source_type", params.GetString("source_type")) + " "
        + ClipsStringSlot("file_path", file_path) + " "
        + ClipsStringSlot("directory_path", directory_path) + " "
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
        + ClipsBoolSlot("requires_post_verify", is_apply_patch_tool) + " "
        + ClipsBoolSlot("pending_continuation_active", pending.active) + " "
        + ClipsStringSlot("pending_required_tool", pending.required_tool) + " "
        + ClipsStringSlot(
            "pending_required_arguments_json",
            ClipsContinuationArgumentsFactValue(pending.required_arguments_json)) + " "
        + ClipsStringSlot("pending_trace_id", pending.trace_id) + " "
        + ClipsStringSlot("pending_goal_id", pending.goal_id) + " "
        + ClipsStringSlot("pending_source_tool", pending.source_tool) + " "
        + ClipsStringSlot("pending_hash", pending.hash) + " "
        + ClipsBoolSlot("pending_trace_match", !pending.trace_id.empty() && pending.trace_id == params.GetString("trace_id"))
        + " "
        + ClipsBoolSlot("continuation_takeover_allowed", AllowsPendingContinuationTakeover(tool_name))
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
        + ClipsStringSlot("tool_use_decision", GetFieldOrDefault(result, "tool_use_decision", "")) + " "
        + ClipsStringSlot("task_completion", GetFieldOrDefault(result, "task_completion", "")) + " "
        + ClipsStringSlot("has_more", GetFieldOrDefault(result, "has_more", "false")) + " "
        + ClipsStringSlot("next_start_line", GetFieldOrDefault(result, "next_start_line", "")) + " "
        + ClipsStringSlot("next_call_json", GetFieldOrDefault(result, "next_call_json", "")) + " "
        + ClipsStringSlot("continue_required", GetFieldOrDefault(result, "continue_required", "false")) + " "
        + ClipsStringSlot("auto_continue_required", GetFieldOrDefault(result, "auto_continue_required", "false")) + " "
        + ClipsStringSlot("analysis_allowed", GetFieldOrDefault(result, "analysis_allowed", "true")) + " "
        + ClipsStringSlot("batch_completion", GetFieldOrDefault(result, "batch_completion", "")) + " "
        + ClipsStringSlot("remaining_batch_file_count", GetFieldOrDefault(result, "remaining_batch_file_count", "0")) + " "
        + ClipsStringSlot("next_batch_file_path", GetFieldOrDefault(result, "next_batch_file_path", "")) + " "
        + ClipsStringSlot("known_file_list_complete", GetFieldOrDefault(result, "known_file_list_complete", "")) + " "
        + ClipsStringSlot("directory_listing_complete", GetFieldOrDefault(result, "directory_listing_complete", "")) + " "
        + ClipsStringSlot("directory_scope_active", GetFieldOrDefault(result, "directory_scope_active", "false")) + " "
        + ClipsStringSlot("directory_manifest_path", GetFieldOrDefault(result, "directory_manifest_path", "")) + " "
        + ClipsStringSlot("directory_current_file_index", GetFieldOrDefault(result, "directory_current_file_index", "0")) + " "
        + ClipsStringSlot("directory_next_file_index", GetFieldOrDefault(result, "directory_next_file_index", "")) + " "
        + ClipsStringSlot("directory_total_code_file_count", GetFieldOrDefault(result, "directory_total_code_file_count", "0")) + " "
        + ClipsStringSlot("directory_remaining_code_file_count", GetFieldOrDefault(result, "directory_remaining_code_file_count", "0")) + " "
        + ClipsStringSlot("directory_scope_incomplete", GetFieldOrDefault(result, "directory_scope_incomplete", "false")) + " "
        + ClipsStringSlot("directory_next_probe_call_json", GetFieldOrDefault(result, "directory_next_probe_call_json", "")) + " "
        + ClipsStringSlot("flow_id", GetFieldOrDefault(result, "flow_id", "")) + " "
        + ClipsStringSlot("flow_task_list_required", GetFieldOrDefault(result, "flow_task_list_required", "false")) + " "
        + ClipsStringSlot("flow_current_task_id", GetFieldOrDefault(result, "flow_current_task_id", "")) + " "
        + ClipsStringSlot("flow_next_task_id", GetFieldOrDefault(result, "flow_next_task_id", "")) + " "
        + ClipsStringSlot("flow_task_list_path", GetFieldOrDefault(result, "flow_task_list_path", "")) + " "
        + ClipsStringSlot("flow_task_list_md_path", GetFieldOrDefault(result, "flow_task_list_md_path", "")) + " "
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

    // 熔断②：相同 fact 签名去重，防止 C++ 侧重复断言
    std::set<std::string> asserted_signatures;

    auto assert_fact =
        [&](const std::string & fact_text) {
            if (fact_text.empty()) {
                return;
            }
            // 计算 fact 签名（取前 256 字符作为去重 key，覆盖正常 fact 长度）
            std::string signature = fact_text.substr(0, 256);
            if (asserted_signatures.count(signature) > 0) {
                ++decision.duplicate_facts_blocked;
                return;  // 重复 fact，跳过
            }
            // 熔断①前置检查：如果 fact 数已超限，不再断言新 fact
            if (GetNumberOfFacts(env) >= static_cast<unsigned long>(CLIPS_MAX_FACTS_PER_SESSION)) {
                decision.circuit_breaker_facts_exceeded = true;
                return;
            }
            void * assert_result = AssertString(env, fact_text.c_str());
            if (assert_result != nullptr) {
                ++decision.asserted_fact_count;
                asserted_signatures.insert(signature);
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

    // — 熔断①+③：Run 前记录 fact 数量；有界运行替代无限 —
    decision.fact_count_before_run = static_cast<int>(GetNumberOfFacts(env));

    // 熔断③：用有界 Run 替代 Run(env, -1) 无限运行，防止规则互相触发死循环
    int rules_fired = Run(env, CLIPS_MAX_RULE_FIRINGS);
    decision.rule_firings_actual = rules_fired;
    if (rules_fired >= CLIPS_MAX_RULE_FIRINGS) {
        decision.circuit_breaker_rules_exceeded = true;
    }

    // 熔断①：Run 后检查 fact 总数是否超限
    decision.fact_count_after_run = static_cast<int>(GetNumberOfFacts(env));
    if (decision.fact_count_after_run > CLIPS_MAX_FACTS_PER_SESSION) {
        decision.circuit_breaker_facts_exceeded = true;
    }

    // 熔断触发时的安全回退：如果任一熔断器触发，强制 decision=allow 并标记告警
    if (decision.circuit_breaker_facts_exceeded || decision.circuit_breaker_rules_exceeded) {
        decision.decision = "allow";
        decision.verification = "circuit_breaker_triggered";
        decision.engine_status = "circuit_breaker_fired";
        if (decision.reason_code.empty()) {
            decision.reason_code = "clips_internal_circuit_breaker";
        }
        decision.next_action = "circuit_breaker: clips internal limit exceeded, forcing allow to prevent hang";
        DestroyEnvironment(env);
        return decision;
    }

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
        decision.route_arguments_json = GetClipsFactSlotString(fact, "route_arguments_json");
        if (params != nullptr
            && (IsClipsContinuationArgumentsRef(decision.route_arguments_json)
                || ((decision.reason_code == "pending_continuation_mismatch"
                        || decision.reason_code == "pending_continuation_context_mismatch")
                    && decision.route_arguments_json.empty()))) {
            McpPendingContinuationFields pending =
                LoadMcpPendingContinuationForParams(config, *params);
            if (!pending.required_arguments_json.empty()) {
                decision.route_arguments_json = pending.required_arguments_json;
            } else {
                decision.route_arguments_json = params->GetString("pending_required_arguments_json");
            }
        }
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
    CommandResult * result,
    const AgentConfig * config = nullptr) {
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
    const bool route_arguments_available = !Trim(decision.route_arguments_json).empty();
    result->fields[prefix + "route_arguments_json"] = "";
    result->fields[prefix + "route_arguments_json_available"] =
        route_arguments_available ? "true" : "false";
    result->fields[prefix + "route_arguments_json_transport"] =
        route_arguments_available ? "artifact_ref" : "none";
    if (route_arguments_available && config != nullptr) {
        result->fields[prefix + "route_arguments_json_ref"] =
            WriteClipsRouteArgumentsRef(*config, decision.route_target, decision.route_arguments_json);
    } else {
        result->fields[prefix + "route_arguments_json_ref"] = "";
    }
    result->fields[prefix + "engine_status"] = decision.engine_status;
    result->fields[prefix + "loaded_from_files"] = decision.loaded_from_files ? "true" : "false";
    result->fields[prefix + "fallback_used"] = decision.fallback_used ? "true" : "false";
    result->fields[prefix + "rule_root"] = decision.rule_root;
    result->fields[prefix + "loaded_files"] = decision.loaded_files;
    result->fields[prefix + "asserted_fact_count"] = std::to_string(decision.asserted_fact_count);
    // — 熔断器状态输出 —
    result->fields[prefix + "fact_count_before_run"] = std::to_string(decision.fact_count_before_run);
    result->fields[prefix + "fact_count_after_run"] = std::to_string(decision.fact_count_after_run);
    result->fields[prefix + "rule_firings_actual"] = std::to_string(decision.rule_firings_actual);
    result->fields[prefix + "duplicate_facts_blocked"] = std::to_string(decision.duplicate_facts_blocked);
    result->fields[prefix + "circuit_breaker_facts_exceeded"] = decision.circuit_breaker_facts_exceeded ? "true" : "false";
    result->fields[prefix + "circuit_breaker_rules_exceeded"] = decision.circuit_breaker_rules_exceeded ? "true" : "false";
}

std::string BuildPreGuardRouteCallJson(
    const std::string & route_target,
    const JsonRequestView & params);

void ApplyDirectClipsDecisionContinuation(
    const AgentConfig & config,
    const ClipsDecision & decision,
    const JsonRequestView & params,
    CommandResult * result) {
    if (result == nullptr) {
        return;
    }

    if (decision.decision != "route") {
        return;
    }

    const std::string route_target = FirstNonEmpty(
        decision.route_target,
        params.GetString("pending_required_tool"),
        params.GetString("route_hint"),
        params.GetString("file_path").empty() ? std::string() : "lan_agent_probe_text_file");
    const std::string fallback_next_call_json =
        BuildPreGuardRouteCallJson(route_target, params);
    const std::string next_call_json = FirstNonEmpty(
        decision.route_arguments_json,
        params.GetString("pending_required_arguments_json"),
        fallback_next_call_json,
        "");
    if (route_target.empty() || next_call_json.empty()) {
        return;
    }

    result->fields["status"] = "needs_continue";
    result->fields["verification"] = "not_verified";
    result->fields["verification_status"] = "not_verified";
    result->fields["verification_ok"] = "false";
    result->fields["semantic_model_clamp"] = "tool_call_only";
    result->fields["assistant_response_allowed"] = "false";
    result->fields["final_answer_allowed"] = "false";
    result->fields["required_next_action_type"] = "mcp_tool_call";
    result->fields["required_tool_name"] = route_target;
    result->fields["required_tool_arguments_json"] = next_call_json;
    result->fields["next_call_json"] = next_call_json;
    result->fields["route_target"] = route_target;
    result->fields["clips_continuation_required"] = "true";
    result->fields["clips_continuation_policy"] =
        "do not return assistant text; call required_tool_arguments_json until the declared continuation completes";
    result->fields["pre_guard_route_arguments_source"] =
        decision.route_arguments_json.empty() ? "rebuilt_from_request" : "clips_route_arguments_json";
    result->fields["supervision_status"] = "closed_loop_continue";
    result->fields["goal_status"] = "not_complete";
    result->fields["terminal_state"] = "false";
    result->fields["task_done"] = "false";
    result->fields["continue_required"] = "true";
    result->fields["auto_continue_required"] = "false";
    result->fields["analysis_allowed"] = "false";
    result->fields["completion_claim_allowed"] = "false";
    result->fields["must_continue_until"] = "continuation_result_is_terminal";
    result->fields["completion_guard"] =
        "NON_TERMINAL_RESULT: do not claim completion; execute required_tool_arguments_json";
    result->fields["ai_conclusion_valid"] = "false";
    result->fields["invalid_conclusion_reason"] = FirstNonEmpty(
        decision.reason_code,
        "clips_route_required");
    result->fields["supervision_alarm"] = "false";
    result->fields["supervision_alarm_code"].clear();
    result->fields["supervision_alarm_message"].clear();
    result->fields["failure_mode"] = "none";
    result->ok = true;
    result->exit_code = 0;
    result->fields["ok"] = "true";
    result->fields["next_action_0_tool_name"] = route_target;
    result->fields["next_action_0_safety_class"] =
        route_target == "lan_agent_delete_text_range_window_atomic" ? "WRITE_CONTROLLED" : "READ_ONLY";
    result->fields["next_action_0_params_json"] = next_call_json;
    result->fields["next_action_0_reason"] = FirstNonEmpty(
        decision.next_action,
        "tool_call_only: execute required_tool_arguments_json");
    result->fields["next_action_0_trace_id"] = FirstNonEmpty(
        params.GetString("trace_id"),
        GetFieldOrDefault(*result, "trace_id", ""));
    result->fields["next_action_0_goal_id"] = FirstNonEmpty(
        params.GetString("goal_id"),
        params.GetString("task_id"),
        GetFieldOrDefault(*result, "goal_id", ""));
    result->fields["next_action_0_params_hash"] =
        StableContentChecksum(route_target + "\n" + next_call_json);
    result->fields["next_actions_count"] = "1";

    if (GetFieldOrDefault(*result, "next_action", "").empty()) {
        result->fields["next_action"] = FirstNonEmpty(
            decision.next_action,
            "tool_call_only: execute required_tool_arguments_json");
    }
    if (route_target == "lan_agent_delete_text_range_window_atomic") {
        result->fields["operation_granularity"] = "bounded_line_window_text_range_delete";
        result->fields["max_items_per_call"] = "200_lines";
        result->fields["max_lines_per_call"] = "200";
        result->fields["batch_mutation_allowed"] = "bounded_window_only";
        result->fields["window_batch_scope"] = "single_file_bounded_line_window";
        result->fields["effective_window_policy"] = "comment_cleanup_fixed_200_line_window";
    }
    if (GetFieldOrDefault(*result, "route_arguments_json_ref", "").empty()) {
        result->fields["route_arguments_json_ref"] =
            WriteClipsRouteArgumentsRef(config, route_target, next_call_json);
    }
    if (GetFieldOrDefault(*result, "route_arguments_json_available", "").empty()) {
        result->fields["route_arguments_json_available"] = "true";
    }
    if (GetFieldOrDefault(*result, "route_arguments_json_transport", "").empty()) {
        result->fields["route_arguments_json_transport"] = "artifact_ref";
    }
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
    if (tool_name == "lan_agent_clips_decide"
        || tool_name == "lan_agent_clips_chain_template") {
        return false;
    }
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
    if (tool_name == "lan_agent_clips_decide"
        || tool_name == "lan_agent_clips_chain_template") {
        return false;
    }
    return true;
}

std::string ClipsParamBoolString(
    const JsonRequestView & params,
    const std::string & key,
    const std::string & default_value) {
    const std::string text_value = params.GetString(key);
    if (!text_value.empty()) {
        return text_value;
    }
    return params.GetBool(key, default_value == "true") ? "true" : "false";
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
    seed_result.fields["tool_use_decision"] = params.GetString("tool_use_decision");
    seed_result.fields["ai_conclusion_valid"] = ClipsParamBoolString(params, "ai_conclusion_valid", "true");
    seed_result.fields["task_completion"] = params.GetString("task_completion");
    seed_result.fields["has_more"] = ClipsParamBoolString(params, "has_more", "false");
    seed_result.fields["next_start_line"] = params.GetString("next_start_line");
    seed_result.fields["next_call_json"] = params.GetString("next_call_json");
    seed_result.fields["continue_required"] = ClipsParamBoolString(params, "continue_required", "false");
    seed_result.fields["auto_continue_required"] = ClipsParamBoolString(params, "auto_continue_required", "false");
    seed_result.fields["analysis_allowed"] = ClipsParamBoolString(params, "analysis_allowed", "true");
    seed_result.fields["batch_completion"] = params.GetString("batch_completion");
    seed_result.fields["remaining_batch_file_count"] = params.GetString("remaining_batch_file_count", "0");
    seed_result.fields["next_batch_file_path"] = params.GetString("next_batch_file_path");
    seed_result.fields["known_file_list_complete"] = params.GetString("known_file_list_complete");
    seed_result.fields["directory_listing_complete"] = params.GetString("directory_listing_complete");
    seed_result.fields["directory_scope_active"] = ClipsParamBoolString(params, "directory_scope_active", "false");
    seed_result.fields["directory_manifest_path"] = params.GetString("directory_manifest_path");
    seed_result.fields["directory_current_file_index"] = params.GetString("directory_current_file_index", "0");
    seed_result.fields["directory_next_file_index"] = params.GetString("directory_next_file_index");
    seed_result.fields["directory_total_code_file_count"] = params.GetString("directory_total_code_file_count", "0");
    seed_result.fields["directory_remaining_code_file_count"] =
        params.GetString("directory_remaining_code_file_count", "0");
    seed_result.fields["directory_scope_incomplete"] = ClipsParamBoolString(params, "directory_scope_incomplete", "false");
    seed_result.fields["directory_next_probe_call_json"] = params.GetString("directory_next_probe_call_json");
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
    seed_result.fields["flow_id"] = params.GetString("flow_id");
    seed_result.fields["flow_task_list_required"] = ClipsParamBoolString(params, "flow_task_list_required", "false");
    seed_result.fields["flow_current_task_id"] = params.GetString("flow_current_task_id");
    seed_result.fields["flow_next_task_id"] = params.GetString("flow_next_task_id");
    seed_result.fields["flow_task_list_path"] = params.GetString("flow_task_list_path");
    seed_result.fields["flow_task_list_md_path"] = params.GetString("flow_task_list_md_path");

    const CommandResult * result_ptr = domain == "mcp_result_guard" ? &seed_result : nullptr;
    const JsonRequestView * params_ptr = domain == "mcp_result_guard" ? nullptr : &params;
    ClipsDecision decision;
    try {
        decision = EvaluateClipsDecision(config, domain, tool_name, params_ptr, result_ptr);
    } catch (const std::exception & ex) {
        decision.domain = domain;
        decision.target = tool_name;
        decision.fallback_used = true;
        decision.engine_status = std::string("exception:") + ex.what();
        decision.decision = "allow";
        decision.verification = "not_verified";
        decision.reason_code = "clips_decision_exception";
        decision.next_action = "CLIPS decision failed; use conservative MCP route fallback and inspect clips_explicit_engine_status";
    } catch (...) {
        decision.domain = domain;
        decision.target = tool_name;
        decision.fallback_used = true;
        decision.engine_status = "exception:unknown";
        decision.decision = "allow";
        decision.verification = "not_verified";
        decision.reason_code = "clips_decision_exception";
        decision.next_action = "CLIPS decision failed; use conservative MCP route fallback and inspect clips_explicit_engine_status";
    }

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
    const bool expose_route_arguments_json =
        params.GetBool("expose_route_arguments_json", false)
        || ToLowerAscii(params.GetString("expose_route_arguments_json")) == "true";
    const std::string route_arguments_json_ref =
        WriteClipsRouteArgumentsRef(config, tool_name, decision.route_arguments_json);
    result.fields["route_arguments_json"] =
        expose_route_arguments_json ? decision.route_arguments_json : std::string();
    result.fields["route_arguments_json_ref"] = route_arguments_json_ref;
    result.fields["route_arguments_json_available"] =
        decision.route_arguments_json.empty() ? "false" : "true";
    result.fields["route_arguments_json_transport"] =
        decision.route_arguments_json.empty()
            ? "none"
            : (expose_route_arguments_json ? "inline" : "artifact_ref");
    result.fields["schema"] =
        "fact_schema_id,decision_schema_id,decision,verification,next_action,reason_code,matched_rule,route_target,route_arguments_json_ref,engine_status";
    if (decision.reason_code == "clips_decision_exception") {
        result.fields["input_fact"] = "";
        result.fields["input_fact_status"] = "skipped_after_clips_exception";
    } else if (domain == "mcp_result_guard") {
        result.fields["input_fact"] = BuildMcpToolResultFact(tool_name, seed_result);
    } else if (domain == "slice_ingest_guard") {
        result.fields["input_fact"] = BuildSliceIngestFact(params);
    } else if (domain == "cxparser_preflight_guard") {
        result.fields["input_fact"] = BuildCxparserFact(tool_name, params);
    } else {
        result.fields["input_fact"] = BuildMcpToolRequestFact(config, tool_name, params);
    }
    ApplyClipsDecisionFields(decision, "explicit", &result, &config);

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
    const std::string primary_intent =
        NormalizeMcpPrimaryIntentForClips(params.GetString("primary_intent"));
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

    if (route_target == "lan_agent_list_directory") {
        arguments_json = "{";
        first_field = true;
        AppendJsonStringField(&arguments_json, &first_field, "directory_path", file_path);
        if (!first_field) {
            arguments_json += ",";
        }
        arguments_json += "\"max_entries\":200";
        first_field = false;
        AppendJsonStringField(&arguments_json, &first_field, "primary_intent", primary_intent);
        AppendJsonStringField(&arguments_json, &first_field, "trace_id", trace_id);
        AppendJsonStringField(&arguments_json, &first_field, "request_id", request_id);
    }

    if (route_target == "lan_agent_format_code_file") {
        arguments_json = "{";
        first_field = true;
        AppendJsonStringField(&arguments_json, &first_field, "source_file", file_path);
        if (!first_field) {
            arguments_json += ",";
        }
        arguments_json += "\"dry_run\":true";
        first_field = false;
    }

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
    const std::string next_call_json = FirstNonEmpty(
        decision.route_arguments_json,
        BuildPreGuardRouteCallJson(route_target, params),
        "");

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
    result->fields["pre_guard_route_arguments_source"] =
        decision.route_arguments_json.empty() ? "rebuilt_from_request" : "clips_route_arguments_json";
    if (route_target == "lan_agent_task_memory_freeze") {
        result->fields["task_execution_in_mcp_required"] = "true";
        result->fields["forced_task_memory_execution"] = "true";
        result->fields["long_loop_budget_recommended"] = "true";
        result->fields["long_loop_freeze_tool_name"] = "lan_agent_task_memory_freeze";
        result->fields["long_loop_budget_tool_name"] =
            "lan_agent_task_memory_execute_continuation_budget";
    }
    result->fields["local_ai_guidance_enforced"] = "true";
    result->fields["local_ai_required_first_tool"] = "lan_agent_mcp_overview";
    result->fields["local_ai_uncertain_route_tool"] = "lan_agent_clips_decide";
    result->fields["local_ai_completion_gate"] =
        "terminal_state=true + completion_claim_allowed=true + final_answer_allowed=true + verification_ok=true";
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
    const std::string file_path = params.GetString("file_path");
    if (!file_path.empty()) {
        result->fields["file_path"] = file_path;
        result->fields["normalized_path"] = file_path;
    }
    const std::string primary_intent =
        NormalizeMcpPrimaryIntentForClips(params.GetString("primary_intent"));
    if (!primary_intent.empty()) {
        result->fields["primary_intent"] = primary_intent;
    }
    const std::string scan_mode = params.GetString("scan_mode");
    if (!scan_mode.empty()) {
        result->fields["scan_mode"] = scan_mode;
    }
    const std::string probe_ref = params.GetString("probe_ref");
    if (!probe_ref.empty()) {
        result->fields["probe_ref"] = probe_ref;
    }
    if (!probe_ref.empty() || params.GetBool("probe_ready", false)) {
        result->fields["probe_ready"] = "true";
    }
    if (route_target == "lan_agent_delete_text_range_window_atomic") {
        result->fields["operation_granularity"] = "bounded_line_window_text_range_delete";
        result->fields["max_items_per_call"] = "200_lines";
        result->fields["max_lines_per_call"] = "200";
        result->fields["batch_mutation_allowed"] = "bounded_window_only";
        result->fields["window_batch_scope"] = "single_file_bounded_line_window";
        result->fields["effective_window_policy"] = "comment_cleanup_fixed_200_line_window";
    }
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
    if (tool_name == "lan_agent_mcp_route") {
        return false;
    }
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
    ApplyClipsDecisionFields(tool_decision, "pre_call_tool", result, &config);
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
    ApplyClipsDecisionFields(decision, "pre_call", result, &config);
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
    ApplyClipsDecisionFields(decision, "post_result", result, &config);
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
        const std::string directory_rule_next_call =
            (decision.reason_code == "directory_scope_next_file_required"
                || decision.reason_code == "directory_scope_remaining_files_forbid_terminal")
                ? GetFieldOrDefault(*result, "directory_next_probe_call_json", "")
                : std::string();
        const std::string required_arguments = FirstNonEmpty(
            GetFieldOrDefault(*result, "next_call_json", ""),
            directory_rule_next_call,
            "");
        const std::string existing_required_tool = GetFieldOrDefault(*result, "required_tool_name", "");
        const std::string required_tool = FirstNonEmpty(
            tool_name == "lan_agent_mcp_route" ? existing_required_tool : std::string(),
            ExtractJsonString(required_arguments, "name"),
            decision.route_target,
            GetFieldOrDefault(*result, "next_tool_name", ""),
            tool_name);
        result->fields["semantic_model_clamp"] = "tool_call_only";
        result->fields["assistant_response_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
        result->fields["required_next_action_type"] = "mcp_tool_call";
        result->fields["required_tool_name"] = required_tool;
        result->fields["required_tool_arguments_json"] = required_arguments;
        result->fields["next_call_json"] = required_arguments;
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
        result->fields["continue_required"] = "true";
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
    const bool pending_rule_continuation =
        (decision.decision == "route" || pending_text_range_delete)
        && !GetFieldOrDefault(*result, "required_tool_arguments_json", "").empty()
        && GetFieldOrDefault(*result, "error", "").empty();
    if (pending_rule_continuation) {
        result->ok = true;
        result->exit_code = 0;
        result->fields["ok"] = "true";
        result->fields["status"] = "needs_continue";
        result->fields["failure_mode"] = "none";
    }
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
    const bool pending_mcp_route_continuation =
        tool_name == "lan_agent_mcp_route"
        && GetFieldOrDefault(*result, "continue_required", "false") == "true"
        && !GetFieldOrDefault(*result, "required_tool_name", "").empty();
    if (pending_mcp_route_continuation
        && GetFieldOrDefault(*result, "supervision_alarm", "false") != "true") {
        result->ok = true;
        result->exit_code = 0;
        result->fields["ok"] = "true";
        result->fields["status"] = "needs_continue";
        result->fields["error"].clear();
        result->fields["error_code"].clear();
        result->fields["error_message"].clear();
        result->fields["failure_mode"] = "none";
        result->fields["semantic_model_clamp"] = "tool_call_only";
        result->fields["supervision_status"] = "closed_loop_continue";
        result->fields["goal_status"] = "not_complete";
        result->fields["terminal_state"] = "false";
        result->fields["completion_claim_allowed"] = "false";
        result->fields["final_answer_allowed"] = "false";
        result->fields["verification_ok"] = "false";
        result->fields["required_next_action_type"] = "mcp_tool_call";
        result->fields["next_action"] =
            "tool_call_only: result is non-terminal; continue with required_tool_arguments_json before any completion claim";
    }
    result->fields["result_hash"] = BuildResultEnvelopeHash(*result);
    result->fields["clips_post_result_fact"] = BuildMcpToolResultFact(tool_name, *result);
}

// ================================
// FactFactory 守卫层钩子实现
//  说明: 钩子独立实现，只依赖 ByteSanitizer 和 BusinessTagRegistry （header-only，无外部 lib）
//  CppJieba / marisa-trie 属于增强层，通过宏 CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE 控制开关
// ================================

#include "fact_factory/ByteSanitizer.h"
#include "fact_factory/BusinessTagRegistry.h"

// 完整4层管线：当前默认关闭（ClipsDecisionOperations.h 中 extern "C" 会污染 CppJieba include）
// 启用方式：使用独立编译单元 FactFactoryIntegration.cpp 并定义 CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE
#undef  CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE
#if defined(CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE)
#include "fact_factory/FactFactory.h"
#include <memory>
#include <mutex>
#endif

inline std::string ApplyFactFactoryNormalizePrimaryIntent(const std::string & raw_intent) {
    if (raw_intent.empty()) return {};

#ifdef CODEX_LAN_AGENT_FACT_FACTORY_FULL_PIPELINE
    struct FullPipelineHolder {
        std::mutex mtx;
        std::unique_ptr<fact_factory::FactFactory> ff;
        fact_factory::FactFactory * ensure() {
            std::lock_guard<std::mutex> lk(mtx);
            if (ff) return ff.get();
            fact_factory::FactFactoryConfig cfg;
            cfg.tokenizer.jieba_dict_dir = "D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/cppjieba/dict";
            cfg.tokenizer.business_dict_path = "D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/src/fact_factory/resources/business_dict.utf8";
            cfg.normalizer.supplement_path = "D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/src/fact_factory/resources/business_supplement.txt";
            cfg.normalizer.cilin_ext_path = "D:/Codex-WorkDir/Sean_WorkDir/codex-lan-agent/src/fact_factory/resources/cilin_ext.txt";
            ff = std::make_unique<fact_factory::FactFactory>(cfg);
            return ff.get();
        }
    };
    static FullPipelineHolder holder;
    auto * ff = holder.ensure();
    if (ff != nullptr) {
        auto r = ff->Process(raw_intent);
        if (!r.standard_tags.empty()) return r.standard_tags.front();
    }
    return {};
#else
    // 轻量 fast-path：只做字节过滤 + 别名映射 + 大小写折叠
    fact_factory::ByteSanitizerConfig sc;
    sc.max_field_bytes = 128;
    auto sr = fact_factory::SanitizeSlotValue(raw_intent, sc);
    const std::string & clean = sr.sanitized;
    if (clean.empty()) return {};
    std::string resolved = fact_factory::ResolveBusinessTag(clean);
    if (!resolved.empty()) return resolved;
    std::string lower;
    lower.reserve(clean.size());
    for (unsigned char c : clean) {
        lower.push_back(static_cast<char>(std::tolower(c)));
    }
    if (lower != clean) {
        resolved = fact_factory::ResolveBusinessTag(lower);
        if (!resolved.empty()) return resolved;
    }
    return {};
#endif
}

inline std::string ApplyFactFactoryByteSanitizeSlot(const std::string & raw_value, bool is_token_slot) {
    fact_factory::ByteSanitizerConfig sc;
    sc.max_field_bytes = is_token_slot ? 64u : 256u;
    auto r = fact_factory::SanitizeSlotValue(raw_value, sc);
    return std::move(r.sanitized);
}
