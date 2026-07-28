#pragma once

#include "CapabilityRegistry.h"
#include "types.h"

#include <sstream>

std::string ToLowerAscii(std::string value);
std::string ExtractArgumentTextValue(const std::string & arguments_text, const std::string & key);

bool IsSourceFilePathForSemanticWritePolicy(const std::string & file_path) {
    const std::string lower = ToLowerAscii(file_path);
    const auto ends_with = [&](const char * suffix) {
        const std::size_t suffix_length = std::char_traits<char>::length(suffix);
        return lower.size() >= suffix_length
            && lower.compare(lower.size() - suffix_length, suffix_length, suffix) == 0;
    };
    return ends_with(".c")
        || ends_with(".cpp")
        || ends_with(".h")
        || ends_with(".hpp");
}

bool QueryImpliesSegmentedTextEditing(const std::string & lower_query) {
    const bool mentions_comments =
        lower_query.find("comment") != std::string::npos
        || lower_query.find("comments") != std::string::npos
        || lower_query.find("annotation") != std::string::npos;
    const bool mentions_cleanup =
        lower_query.find("clean") != std::string::npos
        || lower_query.find("cleanup") != std::string::npos
        || lower_query.find("remove") != std::string::npos
        || lower_query.find("delete") != std::string::npos
        || lower_query.find("strip") != std::string::npos;
    const bool mentions_editing =
        lower_query.find("edit") != std::string::npos
        || lower_query.find("modify") != std::string::npos
        || lower_query.find("rewrite") != std::string::npos
        || lower_query.find("patch") != std::string::npos
        || lower_query.find("refactor") != std::string::npos;
    const bool mentions_text_cleanup =
        lower_query.find("text clean") != std::string::npos
        || lower_query.find("text cleanup") != std::string::npos
        || lower_query.find("localized edit") != std::string::npos
        || lower_query.find("segment edit") != std::string::npos;
    return (mentions_comments && (mentions_cleanup || mentions_editing))
        || mentions_text_cleanup;
}

bool IsSegmentedTextEditingIntent(const std::string & value) {
    const std::string lower_value = ToLowerAscii(Trim(value));
    return lower_value == "comment_cleanup"
        || lower_value == "text_cleaning"
        || lower_value == "localized_edit"
        || lower_value == "source_edit_planning"
        || lower_value == "remove_comments"
        || lower_value == "strip_comments";
}

const SemanticActionSpec * FindSemanticActionById(const std::string & action_id) {
    const std::vector<SemanticActionSpec> & actions = GetSemanticActionSpecs();
    for (const SemanticActionSpec & action : actions) {
        if (action_id == action.action_id) {
            return &action;
        }
    }
    return nullptr;
}

CommandResult BuildSemanticActionMapResult(const std::string & action_id) {
    CommandResult result;
    result.fields["schema"] =
        "action_id,description,tool,arguments_schema,success_rule,fallback,result_fields,risk_level,dry_run_supported,side_effect,requires_preview,requires_approval,requires_revert_plan,requires_post_verify";
    const std::vector<SemanticActionSpec> & actions = GetSemanticActionSpecs();
    int index = 0;
    for (const SemanticActionSpec & action : actions) {
        if (!action_id.empty() && action_id != action.action_id) {
            continue;
        }
        const std::string prefix = action_id.empty()
            ? ("action_" + std::to_string(index) + "_")
            : "";
        result.fields[prefix + "action_id"] = action.action_id;
        result.fields[prefix + "description"] = action.description;
        result.fields[prefix + "tool"] = action.tool;
        result.fields[prefix + "arguments_schema"] = action.arguments_schema;
        result.fields[prefix + "success_rule"] = action.success_rule;
        result.fields[prefix + "fallback"] = action.fallback;
        result.fields[prefix + "result_fields"] = action.result_fields;
        result.fields[prefix + "risk_level"] = action.risk_level;
        result.fields[prefix + "dry_run_supported"] = action.dry_run_supported;
        result.fields[prefix + "side_effect"] = action.side_effect;
        result.fields[prefix + "requires_preview"] = action.requires_preview;
        result.fields[prefix + "requires_approval"] = action.requires_approval;
        result.fields[prefix + "requires_revert_plan"] = action.requires_revert_plan;
        result.fields[prefix + "requires_post_verify"] = action.requires_post_verify;
        ++index;
    }
    result.fields["action_count"] = std::to_string(index);
    if (!action_id.empty() && index == 0) {
        result.ok = false;
        result.exit_code = 44;
        result.fields["error"] = "unknown semantic action";
        result.fields["action_id"] = action_id;
    }
    return result;
}

bool SemanticActionMatchesQuery(const SemanticActionSpec & action, const std::string & query) {
    if (query.empty()) {
        return false;
    }
    const std::string lower_query = ToLowerAscii(query);
    if (lower_query.find(ToLowerAscii(action.action_id)) != std::string::npos ||
        lower_query.find(ToLowerAscii(action.tool)) != std::string::npos) {
        return true;
    }
    if ((lower_query.find("online") != std::string::npos ||
         lower_query.find("health") != std::string::npos ||
         lower_query.find("reachable") != std::string::npos) &&
        std::string(action.action_id) == "check_remote_online") {
        return true;
    }
    if ((lower_query.find("local chat") != std::string::npos ||
         lower_query.find("chat") != std::string::npos) &&
        std::string(action.action_id) == "check_local_chat") {
        return true;
    }
    if ((lower_query.find("latest log") != std::string::npos ||
         lower_query.find("recent log") != std::string::npos) &&
        std::string(action.action_id) == "read_latest_log") {
        return true;
    }
    if (QueryImpliesSegmentedTextEditing(lower_query) &&
        std::string(action.action_id) == "scan_text_ranges") {
        return true;
    }
    if (((lower_query.find("find line") != std::string::npos ||
          lower_query.find("line hash") != std::string::npos ||
          lower_query.find("line metadata") != std::string::npos) &&
         std::string(action.action_id) == "find_line_metadata")) {
        return true;
    }
    if (((lower_query.find("find content") != std::string::npos ||
          lower_query.find("locate content") != std::string::npos ||
          lower_query.find("match content") != std::string::npos) &&
         std::string(action.action_id) == "find_content_matches")) {
        return true;
    }
    if (((lower_query.find("locate") != std::string::npos ||
          lower_query.find("find anchor") != std::string::npos ||
          lower_query.find("anchor line") != std::string::npos ||
          lower_query.find("match count") != std::string::npos) &&
         lower_query.find("line") != std::string::npos) &&
        std::string(action.action_id) == "locate_text_lines") {
        return true;
    }
    if ((lower_query.find("delete line") != std::string::npos ||
         lower_query.find("remove line") != std::string::npos) &&
        std::string(action.action_id) == "delete_line_atomic") {
        return true;
    }
    if ((lower_query.find("delete content") != std::string::npos ||
         lower_query.find("remove content") != std::string::npos ||
         lower_query.find("delete comment line") != std::string::npos) &&
        std::string(action.action_id) == "delete_content_atomic") {
        return true;
    }
    if ((lower_query.find("insert") != std::string::npos ||
         lower_query.find("append after") != std::string::npos ||
         lower_query.find("insert after anchor") != std::string::npos) &&
        std::string(action.action_id) == "insert_after_anchor_atomic") {
        return true;
    }
    if ((lower_query.find("replace line") != std::string::npos ||
         lower_query.find("replace range") != std::string::npos ||
         lower_query.find("line range") != std::string::npos) &&
        std::string(action.action_id) == "replace_line_range_atomic") {
        return true;
    }
    if (((lower_query.find("read") != std::string::npos &&
          lower_query.find("document") != std::string::npos) ||
         lower_query.find("read file") != std::string::npos) &&
        !QueryImpliesSegmentedTextEditing(lower_query) &&
        std::string(action.action_id) == "read_document") {
        return true;
    }
    if (((lower_query.find("write") != std::string::npos &&
          lower_query.find("document") != std::string::npos) ||
         lower_query.find("write file") != std::string::npos) &&
        std::string(action.action_id) == "write_document") {
        return true;
    }
    if ((lower_query.find("apply_diff_patch") != std::string::npos ||
         lower_query.find("lan_agent_apply_diff_patch") != std::string::npos ||
         lower_query.find("unified diff") != std::string::npos ||
         lower_query.find("git diff") != std::string::npos ||
         (lower_query.find("diff") != std::string::npos &&
          lower_query.find("review") == std::string::npos &&
          (lower_query.find("apply") != std::string::npos ||
           lower_query.find("patch") != std::string::npos ||
           lower_query.find("write") != std::string::npos ||
           lower_query.find("modify") != std::string::npos))) &&
        std::string(action.action_id) == "apply_diff_patch") {
        return true;
    }
    if ((lower_query.find("refactor") != std::string::npos ||
         lower_query.find("patch") != std::string::npos ||
         lower_query.find("modify file") != std::string::npos) &&
        std::string(action.action_id) == "refactor_file") {
        return true;
    }
    if ((lower_query.find("patch") != std::string::npos &&
         lower_query.find("audit") != std::string::npos) &&
        std::string(action.action_id) == "inspect_patch_audit") {
        return true;
    }
    if ((lower_query.find("trace") != std::string::npos &&
         (lower_query.find("audit") != std::string::npos ||
          lower_query.find("replay") != std::string::npos)) &&
        std::string(action.action_id) == "inspect_trace_audit") {
        return true;
    }
    if ((lower_query.find("patch") != std::string::npos &&
         lower_query.find("verify") != std::string::npos) &&
        std::string(action.action_id) == "verify_patch_result") {
        return true;
    }
    if ((lower_query.find("task") != std::string::npos &&
         lower_query.find("status") != std::string::npos) &&
        std::string(action.action_id) == "get_task_status") {
        return true;
    }
    if ((lower_query.find("configure") != std::string::npos &&
         lower_query.find("project") != std::string::npos) &&
        std::string(action.action_id) == "configure_project") {
        return true;
    }
    if (lower_query.find("build") != std::string::npos &&
        std::string(action.action_id) == "build_target") {
        return true;
    }
    if ((lower_query.find("test") != std::string::npos &&
         lower_query.find("project") != std::string::npos) &&
        std::string(action.action_id) == "run_project_tests") {
        return true;
    }
    if (lower_query.find("diff") != std::string::npos &&
        lower_query.find("review") != std::string::npos &&
        std::string(action.action_id) == "basic_diff_review") {
        return true;
    }
    if (lower_query.find("diff") != std::string::npos &&
        std::string(action.action_id) == "get_git_diff") {
        return true;
    }
    if ((lower_query.find("test") != std::string::npos ||
         lower_query.find("ctest") != std::string::npos ||
         lower_query.find("log classify") != std::string::npos) &&
        std::string(action.action_id) == "read_test_result") {
        return true;
    }
    if ((lower_query.find("cxparser") != std::string::npos &&
         lower_query.find("test") != std::string::npos) ||
        lower_query.find("test statement") != std::string::npos) {
        if (std::string(action.action_id) == "run_cxparser_flow") {
            return true;
        }
    }
    if ((lower_query.find("report") != std::string::npos ||
         lower_query.find("handoff") != std::string::npos) &&
        std::string(action.action_id) == "generate_thread_report") {
        return true;
    }
    if ((lower_query.find("rag") != std::string::npos &&
         lower_query.find("basic") != std::string::npos) &&
        std::string(action.action_id) == "rag.basic_comm.check") {
        return true;
    }
    if ((lower_query.find("rag") != std::string::npos &&
         (lower_query.find("status") != std::string::npos ||
          lower_query.find("bridge") != std::string::npos ||
          lower_query.find("index") != std::string::npos)) &&
        std::string(action.action_id) == "inspect_rag_index_status") {
        return true;
    }
    if ((lower_query.find("clips") != std::string::npos ||
         lower_query.find("fact bundle") != std::string::npos ||
         lower_query.find("assertion") != std::string::npos) &&
        std::string(action.action_id) == "rag_clips_meta") {
        return true;
    }
    if ((lower_query.find("rag") != std::string::npos &&
         (lower_query.find("query") != std::string::npos ||
          lower_query.find("recall") != std::string::npos ||
          lower_query.find("retrieve") != std::string::npos ||
          lower_query.find("search") != std::string::npos)) &&
        std::string(action.action_id) == "run_rag_flow") {
        return true;
    }
    if (((lower_query.find("remote session") != std::string::npos &&
          (lower_query.find("new") != std::string::npos ||
           lower_query.find("create") != std::string::npos ||
           lower_query.find("start") != std::string::npos)) ||
         lower_query.find("new turn") != std::string::npos ||
         lower_query.find("start session") != std::string::npos) &&
        std::string(action.action_id) == "start_remote_session_turn") {
        return true;
    }
    if ((lower_query.find("append turn") != std::string::npos ||
         lower_query.find("continue session") != std::string::npos ||
         lower_query.find("follow-up turn") != std::string::npos) &&
        std::string(action.action_id) == "append_remote_session_turn") {
        return true;
    }
    if ((lower_query.find("remote session") != std::string::npos &&
         (lower_query.find("read") != std::string::npos ||
          lower_query.find("show") != std::string::npos ||
          lower_query.find("inspect") != std::string::npos ||
          lower_query.find("load") != std::string::npos)) &&
        std::string(action.action_id) == "get_remote_session") {
        return true;
    }
    if (lower_query.find("slice") != std::string::npos &&
        lower_query.find("dialog") != std::string::npos) {
        if (std::string(action.action_id) == "record_dialog_slice" &&
            (lower_query.find("record") != std::string::npos ||
             lower_query.find("store") != std::string::npos)) {
            return true;
        }
        if (std::string(action.action_id) == "analyze_dialog_slices" &&
            (lower_query.find("analy") != std::string::npos ||
             lower_query.find("review") != std::string::npos)) {
            return true;
        }
    }
    if (lower_query.find(ToLowerAscii(action.description)) != std::string::npos) {
        return true;
    }
    return false;
}

CommandResult BuildSemanticActionResolveResult(
    const std::string & action_id,
    const std::string & query) {
    CommandResult result;
    const std::vector<SemanticActionSpec> & actions = GetSemanticActionSpecs();
    const SemanticActionSpec * selected = nullptr;
    for (const SemanticActionSpec & action : actions) {
        if (!action_id.empty() && action_id == action.action_id) {
            selected = &action;
            break;
        }
    }
    if (selected == nullptr) {
        const std::string lower_query = ToLowerAscii(query);
        if (QueryImpliesSegmentedTextEditing(lower_query)) {
            selected = FindSemanticActionById("scan_text_ranges");
        } else if (lower_query.find("apply_diff_patch") != std::string::npos ||
            lower_query.find("lan_agent_apply_diff_patch") != std::string::npos ||
            lower_query.find("unified diff") != std::string::npos ||
            lower_query.find("git diff patch") != std::string::npos ||
            (lower_query.find("diff") != std::string::npos &&
             lower_query.find("review") == std::string::npos &&
             (lower_query.find("apply") != std::string::npos ||
              lower_query.find("patch") != std::string::npos ||
              lower_query.find("write") != std::string::npos ||
              lower_query.find("modify") != std::string::npos))) {
            selected = FindSemanticActionById("apply_diff_patch");
        } else if (lower_query.find("diff") != std::string::npos &&
            lower_query.find("review") != std::string::npos) {
            selected = FindSemanticActionById("basic_diff_review");
        } else if (lower_query.find("git") != std::string::npos &&
                   lower_query.find("diff") != std::string::npos) {
            selected = FindSemanticActionById("get_git_diff");
        }
    }
    if (selected == nullptr) {
        for (const SemanticActionSpec & action : actions) {
            if (SemanticActionMatchesQuery(action, query)) {
                selected = &action;
                break;
            }
        }
    }

    result.fields["query"] = query;
    result.fields["requested_action_id"] = action_id;
    result.fields["resolver"] = "semantic_action_resolve";
    result.fields["schema"] =
        "action_id,tool,arguments_schema,result_fields,success_rule,risk_level,dry_run_supported,side_effect,requires_preview,requires_approval,requires_revert_plan,requires_post_verify,next_action";
    if (selected == nullptr) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["semantic_outcome"] = "unresolved_action";
        result.fields["insufficient_context"] = "true";
        result.fields["next_action"] = "call semantic_action_map or provide action_id";
        result.fields["fallback"] = "{\"tool\":\"semantic_action_map\",\"reason\":\"no shortcut matched\"}";
        return result;
    }

    result.fields["semantic_outcome"] = "resolved";
    result.fields["insufficient_context"] = "false";
    result.fields["action_id"] = selected->action_id;
    result.fields["description"] = selected->description;
    result.fields["tool"] = selected->tool;
    result.fields["arguments_schema"] = selected->arguments_schema;
    result.fields["result_fields"] = selected->result_fields;
    result.fields["success_rule"] = selected->success_rule;
    result.fields["fallback"] = selected->fallback;
    result.fields["risk_level"] = selected->risk_level;
    result.fields["dry_run_supported"] = selected->dry_run_supported;
    result.fields["side_effect"] = selected->side_effect;
    result.fields["requires_preview"] = selected->requires_preview;
    result.fields["requires_approval"] = selected->requires_approval;
    result.fields["requires_revert_plan"] = selected->requires_revert_plan;
    result.fields["requires_post_verify"] = selected->requires_post_verify;
    result.fields["next_action"] = std::string("call tool ") + selected->tool + " with arguments_schema";
    return result;
}

CommandResult BuildSemanticActionValidateResult(
    const std::string & action_id,
    const std::string & arguments_text) {
    CommandResult result;
    const SemanticActionSpec * action = FindSemanticActionById(action_id);
    result.fields["action_id"] = action_id;
    result.fields["arguments_text"] = arguments_text;
    result.fields["validator"] = "semantic_action_validate";
    if (action == nullptr) {
        result.ok = false;
        result.exit_code = 47;
        result.fields["semantic_outcome"] = "unknown_action";
        result.fields["insufficient_context"] = "true";
        result.fields["error"] = "unknown semantic action";
        result.fields["fallback"] = "{\"tool\":\"semantic_action_map\",\"reason\":\"unknown action_id\"}";
        result.fields["next_action"] = "resolve or list semantic actions";
        return result;
    }

    result.fields["tool"] = action->tool;
    result.fields["arguments_schema"] = action->arguments_schema;
    result.fields["risk_level"] = action->risk_level;
    result.fields["dry_run_supported"] = action->dry_run_supported;
    result.fields["side_effect"] = action->side_effect;
    result.fields["requires_preview"] = action->requires_preview;
    result.fields["requires_approval"] = action->requires_approval;
    result.fields["requires_revert_plan"] = action->requires_revert_plan;
    result.fields["requires_post_verify"] = action->requires_post_verify;
    result.fields["fallback"] = action->fallback;

    std::vector<std::string> missing;
    const auto needs = [&arguments_text](const std::string & key) {
        return arguments_text.find(key) == std::string::npos;
    };
    if (action_id == "get_task_status" && needs("task_id")) {
        missing.push_back("task_id");
    } else if (action_id == "resolve_task_result_ref") {
        if (needs("task_id") && needs("task_ref")) {
            missing.push_back("task_id_or_task_ref");
        }
    } else if (action_id == "run_light_command" && needs("profile")) {
        missing.push_back("profile");
        } else if (action_id == "build_target") {
        if (needs("build_dir")) {
            missing.push_back("build_dir");
        }
        if (needs("target")) {
            missing.push_back("target");
        }
    } else if (action_id == "read_document") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (IsSegmentedTextEditingIntent(ExtractArgumentTextValue(arguments_text, "primary_intent"))) {
            result.ok = false;
            result.exit_code = 49;
            result.fields["semantic_outcome"] = "segmented_edit_requires_range_scan";
            result.fields["insufficient_context"] = "false";
            result.fields["read_guard_reason_code"] = "editing_intent_requires_range_scan";
            result.fields["next_action"] =
                "for comment cleanup, text cleaning, or localized source edits do not call read_document first; call scan_text_ranges with scan_mode=comments";
            return result;
        }
    } else if (action_id == "find_line_metadata") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("line") && needs("line_number")) {
            missing.push_back("line");
        }
    } else if (action_id == "find_content_matches") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("anchor_text") && needs("query_text") && needs("text") && needs("anchor")) {
            missing.push_back("anchor_text");
        }
    } else if (action_id == "locate_text_lines") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("anchor_text") && needs("query_text") && needs("text") && needs("anchor")) {
            missing.push_back("anchor_text");
        }
    } else if (action_id == "delete_line_atomic") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("line") && needs("line_number")) {
            missing.push_back("line");
        }
    } else if (action_id == "delete_content_atomic") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("anchor_text") && needs("query_text") && needs("text") && needs("anchor")) {
            missing.push_back("anchor_text");
        }
    } else if (action_id == "insert_after_anchor_atomic") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("anchor_text") && needs("anchor")) {
            missing.push_back("anchor_text");
        }
        if (needs("line_roi") && needs("line_roi_base64") &&
            needs("replacement_text") && needs("replacement_text_base64")) {
            missing.push_back("line_roi");
        }
    } else if (action_id == "replace_line_range_atomic") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("start_line") && needs("replace_start_line")) {
            missing.push_back("start_line");
        }
        if (needs("end_line") && needs("replace_end_line")) {
            missing.push_back("end_line");
        }
        if (needs("line_roi") && needs("line_roi_base64") &&
            needs("replacement_text") && needs("replacement_text_base64")) {
            missing.push_back("line_roi");
        }
    } else if (action_id == "write_document") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("content") && needs("new_content")) {
            missing.push_back("content");
        }
        const std::string file_path = ExtractArgumentTextValue(arguments_text, "file_path");
        if (!file_path.empty() && IsSourceFilePathForSemanticWritePolicy(file_path)) {
            result.ok = false;
            result.exit_code = 49;
            result.fields["semantic_outcome"] = "source_file_direct_write_blocked";
            result.fields["insufficient_context"] = "false";
            result.fields["write_guard_reason_code"] = "source_file_requires_patch_flow";
            result.fields["blocked_file_path"] = file_path;
            result.fields["next_action"] =
                "for source files do not use write_document; use refactor_file or apply_diff_patch with preview/apply/verify flow";
            return result;
        }
    } else if (action_id == "refactor_file") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("new_content") && needs("new_content_base64")) {
            missing.push_back("new_content");
        } else if (!needs("new_content") && ExtractArgumentTextValue(arguments_text, "new_content").empty() &&
                   ExtractArgumentTextValue(arguments_text, "allow_empty_content") != "true") {
            result.ok = false;
            result.exit_code = 50;
            result.fields["semantic_outcome"] = "empty_write_blocked";
            result.fields["insufficient_context"] = "false";
            result.fields["write_guard_reason_code"] = "empty_content_requires_explicit_allow";
            result.fields["next_action"] =
                "provide non-empty new_content/new_content_base64, or set allow_empty_content=true only for an intentional empty file";
            return result;
        }
    } else if (action_id == "apply_diff_patch") {
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
        if (needs("diff_text")) {
            missing.push_back("diff_text");
        } else if (ExtractArgumentTextValue(arguments_text, "diff_text").empty()) {
            result.ok = false;
            result.exit_code = 50;
            result.fields["semantic_outcome"] = "empty_diff_blocked";
            result.fields["insufficient_context"] = "false";
            result.fields["write_guard_reason_code"] = "empty_diff_text";
            result.fields["next_action"] = "provide a non-empty git-style unified diff_text before calling apply_diff_patch";
            return result;
        }
    } else if (action_id == "inspect_patch_audit") {
        if (needs("patch_id")) {
            missing.push_back("patch_id");
        }
    } else if (action_id == "inspect_trace_audit") {
        if (needs("trace_id")) {
            missing.push_back("trace_id");
        }
    } else if (action_id == "verify_patch_result") {
        if (needs("patch_id")) {
            missing.push_back("patch_id");
        }
        if (needs("file_path")) {
            missing.push_back("file_path");
        }
    } else if (action_id == "configure_project") {
        if (needs("project_root")) {
            missing.push_back("project_root");
        }
        if (needs("build_dir")) {
            missing.push_back("build_dir");
        }
    } else if (action_id == "run_project_tests") {
        if (needs("build_dir")) {
            missing.push_back("build_dir");
        }
        if (needs("test_regex")) {
            missing.push_back("test_regex");
        }
    } else if (action_id == "discover_project_tests") {
        if (needs("build_dir")) {
            missing.push_back("build_dir");
        }
    } else if (action_id == "rag_clips_meta") {
        if (needs("query")) {
            missing.push_back("query");
        }
    } else if (action_id == "run_rag_flow") {
        if (needs("query")) {
            missing.push_back("query");
        }
    } else if (action_id == "start_remote_session_turn") {
        if (needs("task_id")) {
            missing.push_back("task_id");
        }
        if (needs("speaker_mode")) {
            missing.push_back("speaker_mode");
        }
        if (needs("reasoning_level")) {
            missing.push_back("reasoning_level");
        }
        if (needs("prompt_purpose")) {
            missing.push_back("prompt_purpose");
        }
        if (needs("context_refs")) {
            missing.push_back("context_refs");
        }
        if (needs("response_mode")) {
            missing.push_back("response_mode");
        }
    } else if (action_id == "append_remote_session_turn") {
        if (needs("task_id")) {
            missing.push_back("task_id");
        }
        if (needs("session_id")) {
            missing.push_back("session_id");
        }
        if (needs("speaker_mode")) {
            missing.push_back("speaker_mode");
        }
        if (needs("reasoning_level")) {
            missing.push_back("reasoning_level");
        }
        if (needs("prompt_purpose")) {
            missing.push_back("prompt_purpose");
        }
        if (needs("context_refs")) {
            missing.push_back("context_refs");
        }
        if (needs("response_mode")) {
            missing.push_back("response_mode");
        }
    } else if (action_id == "get_remote_session") {
        if (needs("session_id")) {
            missing.push_back("session_id");
        }
    } else if (action_id == "record_dialog_slice") {
        if (needs("session_id")) {
            missing.push_back("session_id");
        }
        if (needs("turn_id")) {
            missing.push_back("turn_id");
        }
        if (needs("user_text")) {
            missing.push_back("user_text");
        }
        if (needs("assistant_text")) {
            missing.push_back("assistant_text");
        }
    }

    if (!missing.empty()) {
        result.ok = false;
        result.exit_code = 48;
        result.fields["semantic_outcome"] = "missing_required_args";
        result.fields["insufficient_context"] = "true";
        std::ostringstream missing_text;
        for (std::size_t index = 0; index < missing.size(); ++index) {
            if (index > 0) {
                missing_text << ",";
            }
            missing_text << missing[index];
        }
        result.fields["missing_args"] = missing_text.str();
        result.fields["next_action"] = "provide missing_args before calling tool";
        return result;
    }

    result.fields["semantic_outcome"] = "args_valid";
    result.fields["insufficient_context"] = "false";
    result.fields["safe_to_call"] =
        std::string(action->side_effect) == "none" ? "true" : "review_required";
    result.fields["recommend_dry_run"] =
        (std::string(action->dry_run_supported) == "true" &&
         std::string(action->side_effect) != "none") ? "true" : "false";
    result.fields["next_action"] =
        result.fields["recommend_dry_run"] == "true"
            ? "call tool with dry_run or validate_args first"
            : "call resolved tool";
    return result;
}

CommandResult BuildSemanticActionPrepareResult(
    const std::string & action_id,
    const std::string & query,
    const std::string & arguments_text) {
    CommandResult resolve = BuildSemanticActionResolveResult(action_id, query);
    if (!resolve.ok) {
        resolve.fields["preparer"] = "semantic_action_prepare";
        resolve.fields["arguments_text"] = arguments_text;
        return resolve;
    }

    const std::string resolved_action_id = GetFieldOrDefault(resolve, "action_id", "");
    CommandResult validate = BuildSemanticActionValidateResult(resolved_action_id, arguments_text);
    CommandResult result = resolve;
    result.fields["preparer"] = "semantic_action_prepare";
    result.fields["arguments_text"] = arguments_text;
    result.fields["validation_outcome"] = GetFieldOrDefault(validate, "semantic_outcome", "");
    result.fields["safe_to_call"] = GetFieldOrDefault(validate, "safe_to_call", "");
    result.fields["recommend_dry_run"] = GetFieldOrDefault(validate, "recommend_dry_run", "");
    result.fields["missing_args"] = GetFieldOrDefault(validate, "missing_args", "");
    result.fields["validation_next_action"] = GetFieldOrDefault(validate, "next_action", "");
    result.fields["ready_to_call"] = validate.ok ? "true" : "false";
    if (!validate.ok) {
        result.ok = false;
        result.exit_code = validate.exit_code;
        result.fields["semantic_outcome"] = "prepare_blocked";
        result.fields["insufficient_context"] = "true";
        result.fields["next_action"] = result.fields["validation_next_action"];
    }
    return result;
}

std::string ExtractArgumentTextValue(const std::string & arguments_text, const std::string & key) {
    const std::string marker = key + "=";
    const std::size_t start = arguments_text.find(marker);
    if (start == std::string::npos) {
        return std::string();
    }
    std::size_t value_start = start + marker.size();
    while (value_start < arguments_text.size() &&
           std::isspace(static_cast<unsigned char>(arguments_text[value_start])) != 0) {
        ++value_start;
    }
    if (value_start >= arguments_text.size()) {
        return std::string();
    }
    if (arguments_text[value_start] == '"') {
        ++value_start;
        const std::size_t end_quote = arguments_text.find('"', value_start);
        return end_quote == std::string::npos
            ? arguments_text.substr(value_start)
            : arguments_text.substr(value_start, end_quote - value_start);
    }
    std::size_t value_end = value_start;
    while (value_end < arguments_text.size() &&
           std::isspace(static_cast<unsigned char>(arguments_text[value_end])) == 0) {
        ++value_end;
    }
    return arguments_text.substr(value_start, value_end - value_start);
}

std::string BuildToolArgumentsJson(
    const std::string & action_id,
    const std::string & arguments_text,
    bool prefer_dry_run) {
    std::ostringstream output;
    output << "{";
    if (action_id == "build_target") {
        const std::string build_dir = ExtractArgumentTextValue(arguments_text, "build_dir");
        const std::string target = ExtractArgumentTextValue(arguments_text, "target");
        std::string config = ExtractArgumentTextValue(arguments_text, "config");
        if (config.empty()) {
            config = "Release";
        }
        output << "\"build_dir\":\"" << codex_lan_agent::JsonEscape(build_dir) << "\","
               << "\"target\":\"" << codex_lan_agent::JsonEscape(target) << "\","
               << "\"config\":\"" << codex_lan_agent::JsonEscape(config) << "\"";
        if (prefer_dry_run) {
            output << ",\"dry_run\":true";
        }
    } else if (action_id == "get_task_status") {
        output << "\"task_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "task_id"))
               << "\"";
    } else if (action_id == "resolve_task_result_ref") {
        output << "\"task_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "task_id"))
               << "\",\"task_ref\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "task_ref"))
               << "\"";
    } else if (action_id == "run_light_command") {
        output << "\"profile\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "profile"))
               << "\",\"args\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "args"))
               << "\"";
    } else if (action_id == "basic_diff_review") {
        output << "\"diff_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "diff_text"))
               << "\"";
    } else if (action_id == "get_git_diff") {
        output << "\"repo_root\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "repo_root"))
               << "\"";
    } else if (action_id == "read_test_result") {
        output << "\"task_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "task_id"))
               << "\",\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"log_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "log_text"))
               << "\"";
        } else if (action_id == "read_document") {
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"max_lines\":200";
        const std::string primary_intent = ExtractArgumentTextValue(arguments_text, "primary_intent");
        if (!primary_intent.empty()) {
            output << ",\"primary_intent\":\""
                   << codex_lan_agent::JsonEscape(primary_intent)
                   << "\"";
        }
    } else if (action_id == "scan_text_ranges") {
        std::string scan_mode = ExtractArgumentTextValue(arguments_text, "scan_mode");
        if (scan_mode.empty()) {
            scan_mode = "comments";
        }
        std::string primary_intent = ExtractArgumentTextValue(arguments_text, "primary_intent");
        if (primary_intent.empty() && scan_mode == "comments") {
            primary_intent = "comment_cleanup";
        }
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"scan_mode\":\""
               << codex_lan_agent::JsonEscape(scan_mode)
               << "\",\"max_ranges_per_call\":64";
        if (!primary_intent.empty()) {
            output << ",\"primary_intent\":\""
                   << codex_lan_agent::JsonEscape(primary_intent)
                   << "\"";
        }
        const std::string trace_id = ExtractArgumentTextValue(arguments_text, "trace_id");
        if (!trace_id.empty()) {
            output << ",\"trace_id\":\""
                   << codex_lan_agent::JsonEscape(trace_id)
                   << "\"";
        }
    } else if (action_id == "find_line_metadata") {
        const std::string line = FirstNonEmpty(
            ExtractArgumentTextValue(arguments_text, "line"),
            ExtractArgumentTextValue(arguments_text, "line_number"),
            std::string("0"));
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"line\":"
               << line
               << ",\"show_preview\":false";
        const std::string probe_ref = ExtractArgumentTextValue(arguments_text, "probe_ref");
        if (!probe_ref.empty()) {
            output << ",\"probe_ref\":\""
                   << codex_lan_agent::JsonEscape(probe_ref)
                   << "\",\"probe_ready\":true";
        }
    } else if (action_id == "find_content_matches") {
        std::string anchor_text = ExtractArgumentTextValue(arguments_text, "anchor_text");
        if (anchor_text.empty()) {
            anchor_text = ExtractArgumentTextValue(arguments_text, "query_text");
        }
        if (anchor_text.empty()) {
            anchor_text = ExtractArgumentTextValue(arguments_text, "text");
        }
        if (anchor_text.empty()) {
            anchor_text = ExtractArgumentTextValue(arguments_text, "anchor");
        }
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"anchor_text\":\""
               << codex_lan_agent::JsonEscape(anchor_text)
               << "\",\"show_preview\":false,\"fuzzy_threshold\":60";
        const std::string probe_ref = ExtractArgumentTextValue(arguments_text, "probe_ref");
        if (!probe_ref.empty()) {
            output << ",\"probe_ref\":\""
                   << codex_lan_agent::JsonEscape(probe_ref)
                   << "\",\"probe_ready\":true";
        }
    } else if (action_id == "locate_text_lines") {
        std::string anchor_text = ExtractArgumentTextValue(arguments_text, "anchor_text");
        if (anchor_text.empty()) {
            anchor_text = ExtractArgumentTextValue(arguments_text, "query_text");
        }
        if (anchor_text.empty()) {
            anchor_text = ExtractArgumentTextValue(arguments_text, "text");
        }
        if (anchor_text.empty()) {
            anchor_text = ExtractArgumentTextValue(arguments_text, "anchor");
        }
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"anchor_text\":\""
               << codex_lan_agent::JsonEscape(anchor_text)
               << "\",\"show_preview\":false,\"fuzzy_threshold\":60";
        const std::string probe_ref = ExtractArgumentTextValue(arguments_text, "probe_ref");
        if (!probe_ref.empty()) {
            output << ",\"probe_ref\":\""
                   << codex_lan_agent::JsonEscape(probe_ref)
                   << "\",\"probe_ready\":true";
        }
    } else if (action_id == "delete_line_atomic") {
        const std::string line = FirstNonEmpty(
            ExtractArgumentTextValue(arguments_text, "line"),
            ExtractArgumentTextValue(arguments_text, "line_number"),
            std::string("0"));
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"line\":"
               << line;
        const std::string expected_line_hash = ExtractArgumentTextValue(arguments_text, "expected_line_hash");
        if (!expected_line_hash.empty()) {
            output << ",\"expected_line_hash\":\""
                   << codex_lan_agent::JsonEscape(expected_line_hash)
                   << "\"";
        }
        const std::string probe_ref = ExtractArgumentTextValue(arguments_text, "probe_ref");
        if (!probe_ref.empty()) {
            output << ",\"probe_ref\":\""
                   << codex_lan_agent::JsonEscape(probe_ref)
                   << "\",\"probe_ready\":true";
        }
    } else if (action_id == "delete_content_atomic") {
        std::string anchor_text = ExtractArgumentTextValue(arguments_text, "anchor_text");
        if (anchor_text.empty()) {
            anchor_text = ExtractArgumentTextValue(arguments_text, "query_text");
        }
        if (anchor_text.empty()) {
            anchor_text = ExtractArgumentTextValue(arguments_text, "text");
        }
        if (anchor_text.empty()) {
            anchor_text = ExtractArgumentTextValue(arguments_text, "anchor");
        }
        const std::string occurrence = FirstNonEmpty(
            ExtractArgumentTextValue(arguments_text, "occurrence"),
            std::string("1"));
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"anchor_text\":\""
               << codex_lan_agent::JsonEscape(anchor_text)
               << "\",\"occurrence\":"
               << occurrence;
        const std::string expected_anchor_hash = ExtractArgumentTextValue(arguments_text, "expected_anchor_hash");
        if (!expected_anchor_hash.empty()) {
            output << ",\"expected_anchor_hash\":\""
                   << codex_lan_agent::JsonEscape(expected_anchor_hash)
                   << "\"";
        }
        const std::string probe_ref = ExtractArgumentTextValue(arguments_text, "probe_ref");
        if (!probe_ref.empty()) {
            output << ",\"probe_ref\":\""
                   << codex_lan_agent::JsonEscape(probe_ref)
                   << "\",\"probe_ready\":true";
        }
    } else if (action_id == "insert_after_anchor_atomic") {
        std::string anchor_text = ExtractArgumentTextValue(arguments_text, "anchor_text");
        if (anchor_text.empty()) {
            anchor_text = ExtractArgumentTextValue(arguments_text, "anchor");
        }
        std::string line_roi = ExtractArgumentTextValue(arguments_text, "line_roi");
        if (line_roi.empty()) {
            line_roi = ExtractArgumentTextValue(arguments_text, "replacement_text");
        }
        const std::string occurrence = FirstNonEmpty(
            ExtractArgumentTextValue(arguments_text, "occurrence"),
            std::string("1"));
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"anchor_text\":\""
               << codex_lan_agent::JsonEscape(anchor_text)
               << "\",\"line_roi\":\""
               << codex_lan_agent::JsonEscape(line_roi)
               << "\",\"occurrence\":"
               << occurrence;
        const std::string expected_anchor_hash = ExtractArgumentTextValue(arguments_text, "expected_anchor_hash");
        if (!expected_anchor_hash.empty()) {
            output << ",\"expected_anchor_hash\":\""
                   << codex_lan_agent::JsonEscape(expected_anchor_hash)
                   << "\"";
        }
        const std::string probe_ref = ExtractArgumentTextValue(arguments_text, "probe_ref");
        if (!probe_ref.empty()) {
            output << ",\"probe_ref\":\""
                   << codex_lan_agent::JsonEscape(probe_ref)
                   << "\",\"probe_ready\":true";
        }
    } else if (action_id == "replace_line_range_atomic") {
        std::string replacement_text = ExtractArgumentTextValue(arguments_text, "line_roi");
        if (replacement_text.empty()) {
            replacement_text = ExtractArgumentTextValue(arguments_text, "replacement_text");
        }
        const std::string start_line = FirstNonEmpty(
            ExtractArgumentTextValue(arguments_text, "start_line"),
            ExtractArgumentTextValue(arguments_text, "replace_start_line"),
            std::string("0"));
        const std::string end_line = FirstNonEmpty(
            ExtractArgumentTextValue(arguments_text, "end_line"),
            ExtractArgumentTextValue(arguments_text, "replace_end_line"),
            std::string("0"));
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"start_line\":"
               << start_line
               << ",\"end_line\":"
               << end_line
               << ",\"line_roi\":\""
               << codex_lan_agent::JsonEscape(replacement_text)
               << "\"";
        const std::string expected_range_hash = ExtractArgumentTextValue(arguments_text, "expected_range_hash");
        if (!expected_range_hash.empty()) {
            output << ",\"expected_range_hash\":\""
                   << codex_lan_agent::JsonEscape(expected_range_hash)
                   << "\"";
        }
        const std::string probe_ref = ExtractArgumentTextValue(arguments_text, "probe_ref");
        if (!probe_ref.empty()) {
            output << ",\"probe_ref\":\""
                   << codex_lan_agent::JsonEscape(probe_ref)
                   << "\",\"probe_ready\":true";
        }
    } else if (action_id == "prepare_edit_windows") {
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"ranges_json\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "ranges_json"))
               << "\",\"context_before\":8,\"context_after\":8,\"max_windows_per_call\":12";
        const std::string trace_id = ExtractArgumentTextValue(arguments_text, "trace_id");
        if (!trace_id.empty()) {
            output << ",\"trace_id\":\""
                   << codex_lan_agent::JsonEscape(trace_id)
                   << "\"";
        }
    } else if (action_id == "write_document") {
        std::string content = ExtractArgumentTextValue(arguments_text, "content");
        if (content.empty()) {
            content = ExtractArgumentTextValue(arguments_text, "new_content");
        }
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"content\":\""
               << codex_lan_agent::JsonEscape(content)
               << "\",\"append\":false";
    } else if (action_id == "refactor_file") {
        const std::string allow_empty_content = ExtractArgumentTextValue(arguments_text, "allow_empty_content");
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"new_content\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "new_content"))
               << "\",\"old_hash\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "old_hash"))
               << "\",\"request_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "request_id"))
               << "\",\"trace_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "trace_id"))
               << "\",\"patch_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "patch_id"))
               << "\",\"reason\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "reason"))
               << "\",\"allow_empty_content\":"
               << (allow_empty_content == "true" || allow_empty_content == "1" ? "true" : "false");
    } else if (action_id == "apply_diff_patch") {
        const std::string allow_empty_content = ExtractArgumentTextValue(arguments_text, "allow_empty_content");
        output << "\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"diff_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "diff_text"))
               << "\",\"old_hash\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "old_hash"))
               << "\",\"request_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "request_id"))
               << "\",\"trace_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "trace_id"))
               << "\",\"patch_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "patch_id"))
               << "\",\"reason\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "reason"))
               << "\",\"resolved_file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "resolved_file_path"))
               << "\",\"target_resolution_reason\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "target_resolution_reason"))
               << "\",\"allow_empty_content\":"
               << (allow_empty_content == "true" || allow_empty_content == "1" ? "true" : "false");
    } else if (action_id == "inspect_patch_audit") {
        output << "\"patch_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "patch_id"))
               << "\"";
    } else if (action_id == "inspect_trace_audit") {
        output << "\"trace_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "trace_id"))
               << "\"";
    } else if (action_id == "verify_patch_result") {
        output << "\"patch_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "patch_id"))
               << "\",\"file_path\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "file_path"))
               << "\",\"expected_hash\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "expected_hash"))
               << "\",\"contains_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "contains_text"))
               << "\",\"forbidden_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "forbidden_text"))
               << "\",\"request_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "request_id"))
               << "\",\"trace_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "trace_id"))
               << "\",\"reason\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "reason"))
               << "\"";
    } else if (action_id == "configure_project") {
        output << "\"project_root\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "project_root"))
               << "\",\"build_dir\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "build_dir"))
               << "\",\"generator_kind\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "generator_kind"))
               << "\",\"cmake_args\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "cmake_args"))
               << "\",\"env\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "env"))
               << "\"";
    } else if (action_id == "run_project_tests") {
        output << "\"build_dir\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "build_dir"))
               << "\",\"test_regex\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "test_regex"))
               << "\",\"config\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "config"))
               << "\"";
    } else if (action_id == "discover_project_tests") {
        std::string start_index = ExtractArgumentTextValue(arguments_text, "start_index");
        std::string max_entries = ExtractArgumentTextValue(arguments_text, "max_entries");
        if (start_index.empty()) {
            start_index = "0";
        }
        if (max_entries.empty()) {
            max_entries = "200";
        }
        output << "\"build_dir\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "build_dir"))
               << "\",\"config\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "config"))
               << "\",\"test_regex\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "test_regex"))
               << "\",\"start_index\":"
               << start_index
               << ",\"max_entries\":"
               << max_entries;
    } else if (action_id == "rag_clips_meta") {
        std::string top_k = ExtractArgumentTextValue(arguments_text, "top_k");
        if (top_k.empty()) {
            top_k = "3";
        }
        output << "\"query\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "query"))
               << "\",\"top_k\":"
               << top_k;
    } else if (action_id == "run_rag_flow") {
        std::string mode = ExtractArgumentTextValue(arguments_text, "mode");
        if (mode.empty()) {
            mode = "review";
        }
        output << "\"query\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "query"))
               << "\",\"mode\":\""
               << codex_lan_agent::JsonEscape(mode)
               << "\"";
    } else if (action_id == "start_remote_session_turn") {
        output << "\"task_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "task_id"))
               << "\",\"speaker_mode\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "speaker_mode"))
               << "\",\"reasoning_level\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "reasoning_level"))
               << "\",\"prompt_purpose\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "prompt_purpose"))
               << "\",\"context_refs\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "context_refs"))
               << "\",\"response_mode\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "response_mode"))
               << "\",\"prompt_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "prompt_text"))
               << "\"";
    } else if (action_id == "append_remote_session_turn") {
        output << "\"task_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "task_id"))
               << "\",\"session_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "session_id"))
               << "\",\"speaker_mode\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "speaker_mode"))
               << "\",\"reasoning_level\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "reasoning_level"))
               << "\",\"prompt_purpose\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "prompt_purpose"))
               << "\",\"context_refs\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "context_refs"))
               << "\",\"response_mode\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "response_mode"))
               << "\",\"prompt_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "prompt_text"))
               << "\"";
    } else if (action_id == "get_remote_session") {
        output << "\"session_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "session_id"))
               << "\"";
    } else if (action_id == "record_dialog_slice") {
        output << "\"session_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "session_id"))
               << "\",\"turn_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "turn_id"))
               << "\",\"user_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "user_text"))
               << "\",\"assistant_text\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "assistant_text"))
               << "\",\"tags\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "tags"))
               << "\"";
    } else if (action_id == "analyze_dialog_slices") {
        output << "\"session_id\":\""
               << codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "session_id"))
               << "\",\"max_entries\":20";
    }
    output << "}";
    return output.str();
}

CommandResult BuildSemanticActionToolCallResult(
    const std::string & action_id,
    const std::string & query,
    const std::string & arguments_text,
    bool prefer_dry_run) {
    CommandResult prepared = BuildSemanticActionPrepareResult(action_id, query, arguments_text);
    CommandResult result = prepared;
    result.fields["builder"] = "semantic_action_tool_call";
    if (!prepared.ok) {
        result.fields["tool_call_ready"] = "false";
        return result;
    }
    const std::string resolved_action_id = GetFieldOrDefault(prepared, "action_id", "");
    const std::string tool_name = GetFieldOrDefault(prepared, "tool", "");
    const bool use_dry_run =
        prefer_dry_run ||
        GetFieldOrDefault(prepared, "recommend_dry_run", "false") == "true";
    std::string arguments_json =
        BuildToolArgumentsJson(resolved_action_id, arguments_text, use_dry_run);
    if (tool_name == "lan_agent_resolve_task_result" && arguments_json == "{}") {
        const std::string fallback_task_id = ExtractArgumentTextValue(arguments_text, "task_id");
        const std::string fallback_task_ref = ExtractArgumentTextValue(arguments_text, "task_ref");
        arguments_json =
            std::string("{\"task_id\":\"")
            + codex_lan_agent::JsonEscape(fallback_task_id)
            + "\",\"task_ref\":\""
            + codex_lan_agent::JsonEscape(fallback_task_ref)
            + "\"}";
    } else if (tool_name == "lan_agent_discover_ctest_tests" && arguments_json == "{}") {
        std::string start_index = ExtractArgumentTextValue(arguments_text, "start_index");
        std::string max_entries = ExtractArgumentTextValue(arguments_text, "max_entries");
        if (start_index.empty()) {
            start_index = "0";
        }
        if (max_entries.empty()) {
            max_entries = "200";
        }
        arguments_json =
            std::string("{\"build_dir\":\"")
            + codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "build_dir"))
            + "\",\"config\":\""
            + codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "config"))
            + "\",\"test_regex\":\""
            + codex_lan_agent::JsonEscape(ExtractArgumentTextValue(arguments_text, "test_regex"))
            + "\",\"start_index\":"
            + start_index
            + ",\"max_entries\":"
            + max_entries
            + "}";
    }
    result.fields["tool_call_ready"] = "true";
    result.fields["tool_name"] = tool_name;
    result.fields["tool_arguments_json"] = arguments_json;
    result.fields["mcp_tool_call_json"] =
        std::string("{\"method\":\"tools/call\",\"params\":{\"name\":\"")
        + codex_lan_agent::JsonEscape(tool_name)
        + "\",\"arguments\":"
        + arguments_json
        + "}}";
    result.fields["dry_run_injected"] = use_dry_run ? "true" : "false";
    result.fields["next_action"] = "review mcp_tool_call_json then execute explicitly if intended";
    return result;
}

struct RouterDomainSpec {
    const char * domain;
    const char * intent;
    const char * tool_whitelist;
    const char * local_cli_commands;
    const char * when_to_use;
    const char * when_not_to_use;
};

const std::vector<RouterDomainSpec> & GetRouterDomainSpecs() {
    static const std::vector<RouterDomainSpec> domains = {
        {
            "knowledge_search",
            "Find knowledge, docs, or experience cards before execution.",
            "dispatch_contract_map,intent_dispatch_prepare,semantic_action_resolve",
            "log-latest",
            "Use before unfamiliar tasks or when prior failure patterns may help, and when model-side structured intent should be consumed first.",
            "Do not use for direct build or test execution without dispatch or validation."
        },
        {
            "system_ops",
            "Inspect agent health, chat status, tasks/logs, and perform small audited directory operations.",
            "local_cli,intent_dispatch_prepare,semantic_action_validate",
            "health,chat-status,task-latest,task,log-latest,mkdir",
            "Use for status checks, task polling, log discovery, and mkdir through optcmd.exe.",
            "Do not use to start build/test work."
        },
        {
            "code_ops",
            "Inspect code diff and produce basic non-executing review evidence.",
            "local_cli,rag.diff_review,semantic_action_tool_call",
            "diff",
            "Use for diff discovery, patch evidence, and basic review preflight.",
            "Do not use for writing files or applying patches."
        },
        {
            "project_ops",
            "Produce thread/project state reports and handoff summaries.",
            "dispatch_contract_map,intent_dispatch_prepare,lan_agent_tail_control_events",
            "thread-report,log-latest",
            "Use for thread report, handoff, event evidence, and frozen dispatch contract consumption.",
            "Do not use for heavy execution."
        },
        {
            "build_test_ops",
            "Validate, queue, and inspect build/test operations through agent flow.",
            "intent_dispatch_prepare,semantic_action_prepare,semantic_action_validate",
            "build-target,test-result,task,task-latest",
            "Use for build dry-run, queued build, task status, and test result classification after reasoning level is decided.",
            "Do not bypass agent queue or build-dir locks."
        },
        {
            "rag_remote_ops",
            "Inspect RAG bridge readiness, query retrieval paths, and drive browser-visible remote-session continuity.",
            "intent_dispatch_prepare,semantic_action_prepare,semantic_action_tool_call",
            "rag-status,rag-query,remote-session-new,remote-session-append,remote-session-read",
            "Use for RAG bridge status, clips-meta inspection, retrieval queries, and remote-session turn continuation through registered tools.",
            "Do not treat analysis-only rag flow output as trusted execution evidence."
        }
    };
    return domains;
}

CommandResult BuildRouterDomainMapResult(const std::string & domain_filter) {
    CommandResult result;
    result.fields["schema"] =
        "domain,intent,tool_whitelist,local_cli_commands,when_to_use,when_not_to_use";
    int index = 0;
    for (const RouterDomainSpec & domain : GetRouterDomainSpecs()) {
        if (!domain_filter.empty() && domain_filter != domain.domain) {
            continue;
        }
        const std::string prefix = domain_filter.empty()
            ? ("domain_" + std::to_string(index) + "_")
            : "";
        result.fields[prefix + "domain"] = domain.domain;
        result.fields[prefix + "intent"] = domain.intent;
        result.fields[prefix + "tool_whitelist"] = domain.tool_whitelist;
        result.fields[prefix + "local_cli_commands"] = domain.local_cli_commands;
        result.fields[prefix + "when_to_use"] = domain.when_to_use;
        result.fields[prefix + "when_not_to_use"] = domain.when_not_to_use;
        ++index;
    }
    result.fields["domain_count"] = std::to_string(index);
    if (!domain_filter.empty() && index == 0) {
        result.ok = false;
        result.exit_code = 51;
        result.fields["error"] = "unknown router domain";
        result.fields["domain"] = domain_filter;
    }
    return result;
}
