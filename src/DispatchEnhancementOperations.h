#pragma once

#include "SemanticDispatchOperations.h"
#include "StructuredJsonOperations.h"

std::string BuildSessionDispatchDir(const AgentConfig & config);

std::string BuildRemoteSessionTitle(
    const std::string & module_name,
    const std::string & reasoning_level,
    const std::string & task_state,
    const std::string & short_goal,
    const std::string & task_id);

std::string BuildRemoteChatSessionsPath(const AgentConfig & config);

struct DispatchEnhancementOperations final {
    static std::string NormalizeTaskState(
        const std::string & task_state,
        const std::string & primary_intent) {
        const std::string lowered = ToLowerAscii(task_state);
        if (!lowered.empty()) {
            if (lowered == "observe" || lowered == "clarify" || lowered == "plan" ||
                lowered == "execute_light" || lowered == "execute_heavy" ||
                lowered == "verify" || lowered == "recover") {
                return lowered;
            }
        }
        const std::string intent = ToLowerAscii(primary_intent);
        if (intent == "write_document" || intent == "build_target" ||
            intent == "configure_project" || intent == "run_project_tests") {
            return "execute_heavy";
        }
        if (intent == "read_document" || intent == "read_latest_log" ||
            intent == "get_task_status" || intent == "analyze_dialog_slices") {
            return "observe";
        }
        return "clarify";
    }

    static std::string NormalizeReasoningLevel(
        const std::string & reasoning_level,
        const std::string & task_state) {
        const std::string lowered = ToLowerAscii(reasoning_level);
        if (lowered == "low" || lowered == "medium" || lowered == "high") {
            return lowered;
        }
        if (task_state == "observe" || task_state == "clarify") {
            return "low";
        }
        if (task_state == "plan" || task_state == "verify") {
            return "medium";
        }
        return "high";
    }

    static std::string ReasoningLevelAllowedIndexDepth(const std::string & reasoning_level) {
        if (reasoning_level == "low") {
            return "none_or_shallow";
        }
        if (reasoning_level == "medium") {
            return "session_level";
        }
        return "session_plus_evidence";
    }

    static std::string ReasoningLevelAllowedChainComplexity(const std::string & reasoning_level) {
        if (reasoning_level == "low") {
            return "single_step_or_read_only";
        }
        if (reasoning_level == "medium") {
            return "two_step_preflight_then_execute";
        }
        return "multi_step_validate_queue_verify";
    }

    static std::string ResolvePrimaryIntentActionId(const std::string & primary_intent) {
        const std::string lowered = ToLowerAscii(primary_intent);
        if (FindSemanticActionById(lowered) != nullptr) {
            return lowered;
        }
        if (lowered == "health_check" || lowered == "check_remote_online") {
            return "check_remote_online";
        }
        if (lowered == "check_local_chat" || lowered == "local_chat_check") {
            return "check_local_chat";
        }
        if (lowered == "read_log" || lowered == "read_latest_log") {
            return "read_latest_log";
        }
        if (lowered == "task_status" || lowered == "get_task_status") {
            return "get_task_status";
        }
        if (lowered == "read_doc" || lowered == "read_document") {
            return "read_document";
        }
        if (lowered == "write_doc" || lowered == "write_document") {
            return "write_document";
        }
        if (lowered == "configure" || lowered == "configure_project") {
            return "configure_project";
        }
        if (lowered == "build" || lowered == "build_target") {
            return "build_target";
        }
        if (lowered == "run_tests" || lowered == "run_project_tests") {
            return "run_project_tests";
        }
        if (lowered == "diff_review" || lowered == "basic_diff_review") {
            return "basic_diff_review";
        }
        if (lowered == "test_result" || lowered == "read_test_result") {
            return "read_test_result";
        }
        if (lowered == "thread_report" || lowered == "generate_thread_report") {
            return "generate_thread_report";
        }
        if (lowered == "slice_record" || lowered == "record_dialog_slice") {
            return "record_dialog_slice";
        }
        if (lowered == "slice_analyze" || lowered == "analyze_dialog_slices") {
            return "analyze_dialog_slices";
        }
        return std::string();
    }

    static std::string BuildDefaultActionChain(
        const std::string & reasoning_level,
        const std::string & action_id) {
        const SemanticActionSpec * action = FindSemanticActionById(action_id);
        const std::string side_effect = action == nullptr ? "unknown" : action->side_effect;
        if (reasoning_level == "low" && side_effect == "none") {
            return "task_state->intent_conclusion->tool_execute";
        }
        if (reasoning_level == "medium" || side_effect == "append_slice") {
            return "task_state->intent_conclusion->semantic_action_prepare->tool_execute";
        }
        return "task_state->reasoning_level->intent_conclusion->session_index->semantic_action_validate->semantic_action_tool_call->queued_or_guarded_execute->log_verify";
    }

    static CommandResult BuildSemanticExecutionCardResult(
        const std::string & thread_name,
        const std::string & session_title,
        const std::string & task_id,
        const std::string & task_state,
        const std::string & reasoning_level,
        const std::string & primary_intent,
        const std::string & secondary_intents,
        const std::string & scope_modules,
        const std::string & expected_output,
        const std::string & evidence_required,
        const std::string & writeback_required,
        const std::string & next_action_if_blocked) {
        CommandResult result;
        result.fields["thread_name"] = thread_name;
        result.fields["session_title"] = session_title;
        result.fields["task_id"] = task_id;
        result.fields["task_state"] = task_state;
        result.fields["reasoning_level"] = reasoning_level;
        result.fields["primary_intent"] = primary_intent;
        result.fields["secondary_intents"] = secondary_intents;
        result.fields["scope_modules"] = scope_modules;
        result.fields["expected_output"] = expected_output;
        result.fields["evidence_required"] = evidence_required;
        result.fields["writeback_required"] = writeback_required;
        result.fields["next_action_if_blocked"] = next_action_if_blocked;

        std::ostringstream card;
        card << "{"
             << "\"thread_name\":\"" << codex_lan_agent::JsonEscape(thread_name) << "\","
             << "\"session_title\":\"" << codex_lan_agent::JsonEscape(session_title) << "\","
             << "\"task_id\":\"" << codex_lan_agent::JsonEscape(task_id) << "\","
             << "\"task_state\":\"" << codex_lan_agent::JsonEscape(task_state) << "\","
             << "\"reasoning_level\":\"" << codex_lan_agent::JsonEscape(reasoning_level) << "\","
             << "\"primary_intent\":\"" << codex_lan_agent::JsonEscape(primary_intent) << "\","
             << "\"secondary_intents\":" << (secondary_intents.empty() ? "\"\"" : secondary_intents) << ","
             << "\"scope_modules\":" << (scope_modules.empty() ? "\"\"" : scope_modules) << ","
             << "\"expected_output\":\"" << codex_lan_agent::JsonEscape(expected_output) << "\","
             << "\"evidence_required\":\"" << codex_lan_agent::JsonEscape(evidence_required) << "\","
             << "\"writeback_required\":\"" << codex_lan_agent::JsonEscape(writeback_required) << "\","
             << "\"next_action_if_blocked\":\"" << codex_lan_agent::JsonEscape(next_action_if_blocked) << "\""
             << "}";
        result.fields["semantic_execution_card"] = card.str();
        result.fields["result"] = "generated";
        return result;
    }

    static CommandResult AllocateRemoteChatSessionResult(
        const AgentConfig & config,
        const std::string & thread_name,
        const std::string & module_name,
        const std::string & reasoning_level,
        const std::string & task_state,
        const std::string & short_goal,
        const std::string & task_id,
        const std::string & requested_session_title,
        const std::string & parent_session_id,
        const std::string & dispatch_mode) {
        CommandResult result;
        const std::filesystem::path dispatch_dir(BuildSessionDispatchDir(config));
        std::error_code ec;
        std::filesystem::create_directories(dispatch_dir, ec);
        if (ec) {
            result.ok = false;
            result.exit_code = 75;
            result.fields["error"] = "failed to create session_dispatch dir";
            return result;
        }

        const std::string normalized_module = SanitizeDispatchToken(module_name, "module");
        const std::string normalized_task_id = SanitizeDispatchToken(task_id, "task");
        const std::string remote_chat_session_id =
            "chat-" + normalized_module + "-" + normalized_task_id + "-" + TimeStampForFileName();
        const std::string session_title = requested_session_title.empty()
            ? BuildRemoteSessionTitle(module_name, reasoning_level, task_state, short_goal, task_id)
            : requested_session_title;
        const std::string session_path = BuildRemoteChatSessionsPath(config);
        const std::string evidence_ref = BuildRemoteControlEventsPath(config);

        std::ostringstream line;
        line << "{"
             << "\"remote_chat_session_id\":\"" << codex_lan_agent::JsonEscape(remote_chat_session_id) << "\","
             << "\"thread_name\":\"" << codex_lan_agent::JsonEscape(thread_name) << "\","
             << "\"module_name\":\"" << codex_lan_agent::JsonEscape(module_name) << "\","
             << "\"reasoning_level\":\"" << codex_lan_agent::JsonEscape(reasoning_level) << "\","
             << "\"task_state\":\"" << codex_lan_agent::JsonEscape(task_state) << "\","
             << "\"short_goal\":\"" << codex_lan_agent::JsonEscape(short_goal) << "\","
             << "\"task_id\":\"" << codex_lan_agent::JsonEscape(task_id) << "\","
             << "\"session_title\":\"" << codex_lan_agent::JsonEscape(session_title) << "\","
             << "\"parent_session_id\":\"" << codex_lan_agent::JsonEscape(parent_session_id) << "\","
             << "\"dispatch_mode\":\"" << codex_lan_agent::JsonEscape(dispatch_mode) << "\","
             << "\"created_at\":\"" << codex_lan_agent::JsonEscape(IsoTimestampNow()) << "\""
             << "}\n";

        std::ofstream output(session_path, std::ios::binary | std::ios::app);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 76;
            result.fields["error"] = "failed to open remote_chat_sessions.jsonl";
            return result;
        }
        output.write(line.str().data(), static_cast<std::streamsize>(line.str().size()));
        output.close();

        const std::string allocation_id = remote_chat_session_id;
        const std::string minimal_new_turn_example =
            std::string("{\"name\":\"lan_agent_remote_session_new_turn\",\"arguments\":{")
            + "\"task_id\":\"" + codex_lan_agent::JsonEscape(task_id.empty() ? normalized_task_id : task_id) + "\","
            + "\"speaker_mode\":\"remote_ai\","
            + "\"reasoning_level\":\"" + codex_lan_agent::JsonEscape(reasoning_level.empty() ? "low" : reasoning_level) + "\","
            + "\"prompt_purpose\":\"session_bootstrap\","
            + "\"context_refs\":\"allocation_id=" + codex_lan_agent::JsonEscape(allocation_id) + "\","
            + "\"response_mode\":\"tool_first\","
            + "\"prompt_text\":\"create one minimal remote session turn\""
            + "}}";
        const std::string minimal_append_turn_example =
            "{\"name\":\"lan_agent_remote_session_append_turn\",\"arguments\":{"
            "\"task_id\":\"" + codex_lan_agent::JsonEscape(task_id.empty() ? normalized_task_id : task_id) + "\","
            "\"session_id\":\"<remote_session_id_from_new_turn>\","
            "\"speaker_mode\":\"remote_ai\","
            "\"reasoning_level\":\"" + codex_lan_agent::JsonEscape(reasoning_level.empty() ? "low" : reasoning_level) + "\","
            "\"prompt_purpose\":\"followup\","
            "\"context_refs\":\"minimal_append\","
            "\"response_mode\":\"tool_first\","
            "\"prompt_text\":\"append one minimal follow-up turn\""
            "}}";

        result.fields["remote_chat_session_id"] = remote_chat_session_id;
        result.fields["remote_chat_session_id_kind"] = "allocation_id";
        result.fields["remote_chat_session_id_usable_for_remote_session_calls"] = "false";
        result.fields["remote_chat_session_id_usage"] =
            "allocation only; do not use for lan_agent_get_remote_session or lan_agent_remote_session_append_turn";
        result.fields["allocation_id"] = allocation_id;
        result.fields["allocation_id_kind"] = "task_module_allocation_id";
        result.fields["session_allocation_id"] = allocation_id;
        result.fields["session_id"] = "";
        result.fields["remote_session_id"] = "";
        result.fields["remote_session_id_available"] = "false";
        result.fields["remote_session_id_source"] = "lan_agent_remote_session_new_turn";
        result.fields["session_id_kind"] = "remote_session_id";
        result.fields["legal_session_id_field"] = "remote_session_id";
        result.fields["session_id_validation"] =
            "allocation only; call lan_agent_remote_session_new_turn first and use its returned session_id";
        result.fields["session_title"] = session_title;
        result.fields["title_template"] = "module | reasoning_level | task_state | short_goal | task_id";
        result.fields["session_registry_path"] = session_path;
        result.fields["parent_session_id"] = parent_session_id;
        result.fields["dispatch_mode"] = dispatch_mode;
        result.fields["minimal_new_turn_example"] = minimal_new_turn_example;
        result.fields["minimal_append_turn_example"] = minimal_append_turn_example;
        result.fields["append_turn_ready"] = "false";
        result.fields["append_turn_ready_reason"] =
            "remote_session_id is not created by allocation; create a first remote-session turn before append_turn";
        result.fields["evidence_ref"] = evidence_ref;
        result.fields["result"] = "allocated";
        result.fields["summary"] =
            "allocation created; remote_session_id is not available until lan_agent_remote_session_new_turn succeeds";
        return result;
    }

    static std::string NormalizeIntentConfidenceRaw(const std::string & value) {
        std::string normalized = Trim(value);
        if (normalized.size() >= 2 &&
            normalized.front() == '"' &&
            normalized.back() == '"') {
            normalized = normalized.substr(1, normalized.size() - 2);
        }
        return normalized;
    }

    static std::string FirstStructuredFieldString(
        const std::string & request_body,
        const std::string & key,
        const std::string & fallback) {
        const std::string value = ExtractStructuredConclusionString(request_body, key);
        return value.empty() ? fallback : value;
    }

    static std::string FirstStructuredFieldRaw(
        const std::string & request_body,
        const std::string & key,
        const std::string & fallback) {
        const std::string value = ExtractStructuredConclusionRawValue(request_body, key);
        return value.empty() ? fallback : value;
    }

    static CommandResult BuildDispatchContractMapResult(const std::string & table_name) {
        CommandResult result;
        result.fields["dispatch_main_path"] =
            "task_state->reasoning_level->primary_intent->session_index->action_chain->tool_execution->fallback_table";
        result.fields["compatibility_mode"] = "legacy_codex_fallback";
        result.fields["fallback_trigger_rule"] =
            "missing structured fields or low confidence or unresolved intent or index miss with insufficient evidence";
        result.fields["table_names"] =
            "task_state_table,reasoning_level_table,primary_intent_table,parameter_slot_table,session_object_table,default_action_chain_table,fallback_rule_table,model_output_contract";
        result.fields["schema"] = "table,row_key,meaning,consume_rule";
        int index = 0;
        const auto append_row =
            [&result, &index, &table_name](
                const std::string & current_table,
                const std::string & row_key,
                const std::string & fields) {
                if (!table_name.empty() && table_name != current_table) {
                    return;
                }
                const std::string prefix = "row_" + std::to_string(index++) + "_";
                result.fields[prefix + "table"] = current_table;
                result.fields[prefix + "row_key"] = row_key;
                result.fields[prefix + "fields"] = fields;
            };

        append_row("task_state_table", "observe",
            "meaning=read_only_or_status_scan;default_reasoning_level=low;allowed_index_depth=none_or_shallow;allowed_chain_complexity=single_step");
        append_row("task_state_table", "clarify",
            "meaning=intent_not_final;default_reasoning_level=low;allowed_index_depth=none_or_shallow;allowed_chain_complexity=resolve_only");
        append_row("task_state_table", "plan",
            "meaning=preflight_and_contract_check;default_reasoning_level=medium;allowed_index_depth=session_level;allowed_chain_complexity=prepare_then_execute");
        append_row("task_state_table", "execute_light",
            "meaning=low_side_effect_execution;default_reasoning_level=medium;allowed_index_depth=session_level;allowed_chain_complexity=prepare_then_execute");
        append_row("task_state_table", "execute_heavy",
            "meaning=build_test_write_queue;default_reasoning_level=high;allowed_index_depth=session_plus_evidence;allowed_chain_complexity=validate_queue_verify");
        append_row("task_state_table", "verify",
            "meaning=confirm_result_and_evidence;default_reasoning_level=medium;allowed_index_depth=session_plus_evidence;allowed_chain_complexity=read_log_then_classify");
        append_row("task_state_table", "recover",
            "meaning=fall_back_after_error;default_reasoning_level=high;allowed_index_depth=session_plus_evidence;allowed_chain_complexity=fallback_then_verify");

        append_row("reasoning_level_table", "low",
            "allowed_index_depth=none_or_shallow;allowed_chain_complexity=single_step_or_read_only;when_to_use=status_or_clear_read_intent");
        append_row("reasoning_level_table", "medium",
            "allowed_index_depth=session_level;allowed_chain_complexity=two_step_preflight_then_execute;when_to_use=known_intent_with_some_context");
        append_row("reasoning_level_table", "high",
            "allowed_index_depth=session_plus_evidence;allowed_chain_complexity=multi_step_validate_queue_verify;when_to_use=write_build_test_or_recovery");

        append_row("primary_intent_table", "health_check", "mapped_action=check_remote_online;tool=lan_agent_health");
        append_row("primary_intent_table", "read_document", "mapped_action=read_document;tool=lan_agent_read_text_file");
        append_row("primary_intent_table", "write_document", "mapped_action=write_document;tool=lan_agent_apply_single_file_patch");
        append_row("primary_intent_table", "configure_project", "mapped_action=configure_project;tool=lan_agent_configure_project");
        append_row("primary_intent_table", "build_target", "mapped_action=build_target;tool=lan_agent_build_target");
        append_row("primary_intent_table", "run_project_tests", "mapped_action=run_project_tests;tool=lan_agent_run_ctest_target");
        append_row("primary_intent_table", "record_dialog_slice", "mapped_action=record_dialog_slice;tool=lan_agent_record_dialog_slice");
        append_row("primary_intent_table", "analyze_dialog_slices", "mapped_action=analyze_dialog_slices;tool=lan_agent_analyze_dialog_slices");
        append_row("primary_intent_table", "basic_diff_review", "mapped_action=basic_diff_review;tool=rag.diff_review");
        append_row("primary_intent_table", "generate_thread_report", "mapped_action=generate_thread_report;tool=lan_agent_runtime_overview");

        append_row("parameter_slot_table", "read_document", "required=file_path;optional=max_lines");
        append_row("parameter_slot_table", "write_document", "required=file_path,new_content");
        append_row("parameter_slot_table", "configure_project", "required=project_root,build_dir;optional=generator_kind,cmake_args,cmake_args_list,env");
        append_row("parameter_slot_table", "build_target", "required=build_dir,target;optional=config,dry_run,validate_args");
        append_row("parameter_slot_table", "run_project_tests", "required=build_dir,test_regex;optional=config");
        append_row("parameter_slot_table", "record_dialog_slice", "required=session_id,turn_id,user_text,assistant_text;optional=tags");

        append_row("session_object_table", "dialog_session", "key=session_id;use=group dialog slices and routing continuity");
        append_row("session_object_table", "dialog_slice", "key=session_id+turn_id;use=store one dialog turn and retrieve latest tail");
        append_row("session_object_table", "expression_key", "key=expression_keys;use=stable phrase to intent association");
        append_row("session_object_table", "execution_binding", "key=session_id+primary_intent;use=bind current chain to next verification step");

        append_row("default_action_chain_table", "low",
            "chain=task_state->intent_conclusion->tool_execute");
        append_row("default_action_chain_table", "medium",
            "chain=task_state->intent_conclusion->semantic_action_prepare->tool_execute");
        append_row("default_action_chain_table", "high",
            "chain=task_state->reasoning_level->intent_conclusion->session_index->semantic_action_validate->semantic_action_tool_call->queued_or_guarded_execute->log_verify");

        append_row("fallback_rule_table", "missing_structured_output",
            "fallback=legacy semantic_action_prepare by query and arguments_text");
        append_row("fallback_rule_table", "low_confidence",
            "threshold=intent_confidence<0.55;fallback=semantic_action_prepare");
        append_row("fallback_rule_table", "unresolved_primary_intent",
            "fallback=semantic_action_resolve then semantic_action_prepare");
        append_row("fallback_rule_table", "session_index_miss",
            "fallback=continue stateless with existing CODEX style chain");
        append_row("fallback_rule_table", "high_risk_side_effect",
            "fallback=force validate_or_dry_run_or_queue");

        append_row("model_output_contract", "required_fields",
            "task_state,reasoning_level,primary_intent,secondary_intents,intent_confidence,association_scope,entity_refs,evidence_refs,risk_flags,next_action,session_id,turn_id,slice_summary,expression_keys");

        result.fields["row_count"] = std::to_string(index);
        if (!table_name.empty() && index == 0) {
            result.ok = false;
            result.exit_code = 52;
            result.fields["error"] = "unknown dispatch contract table";
            result.fields["table_name"] = table_name;
        }
        return result;
    }

    static CommandResult BuildIntentDispatchPrepareResult(
        const AgentConfig & config,
        const std::string & task_state,
        const std::string & reasoning_level,
        const std::string & primary_intent,
        const std::string & secondary_intents,
        const std::string & intent_confidence_raw,
        const std::string & association_scope,
        const std::string & entity_refs,
        const std::string & evidence_refs,
        const std::string & risk_flags,
        const std::string & desired_next_action,
        const std::string & session_id,
        const std::string & turn_id,
        const std::string & slice_summary,
        const std::string & expression_keys,
        const std::string & summary,
        bool insufficient_context,
        const std::string & query,
        const std::string & arguments_text);
};

inline std::string NormalizeTaskState(const std::string & task_state, const std::string & primary_intent) {
    return DispatchEnhancementOperations::NormalizeTaskState(task_state, primary_intent);
}

inline std::string NormalizeReasoningLevel(const std::string & reasoning_level, const std::string & task_state) {
    return DispatchEnhancementOperations::NormalizeReasoningLevel(reasoning_level, task_state);
}

inline std::string ReasoningLevelAllowedIndexDepth(const std::string & reasoning_level) {
    return DispatchEnhancementOperations::ReasoningLevelAllowedIndexDepth(reasoning_level);
}

inline std::string ReasoningLevelAllowedChainComplexity(const std::string & reasoning_level) {
    return DispatchEnhancementOperations::ReasoningLevelAllowedChainComplexity(reasoning_level);
}

inline std::string ResolvePrimaryIntentActionId(const std::string & primary_intent) {
    return DispatchEnhancementOperations::ResolvePrimaryIntentActionId(primary_intent);
}

inline std::string BuildDefaultActionChain(const std::string & reasoning_level, const std::string & action_id) {
    return DispatchEnhancementOperations::BuildDefaultActionChain(reasoning_level, action_id);
}

inline CommandResult BuildSemanticExecutionCardResult(
    const std::string & thread_name,
    const std::string & session_title,
    const std::string & task_id,
    const std::string & task_state,
    const std::string & reasoning_level,
    const std::string & primary_intent,
    const std::string & secondary_intents,
    const std::string & scope_modules,
    const std::string & expected_output,
    const std::string & evidence_required,
    const std::string & writeback_required,
    const std::string & next_action_if_blocked) {
    return DispatchEnhancementOperations::BuildSemanticExecutionCardResult(
        thread_name,
        session_title,
        task_id,
        task_state,
        reasoning_level,
        primary_intent,
        secondary_intents,
        scope_modules,
        expected_output,
        evidence_required,
        writeback_required,
        next_action_if_blocked);
}

inline CommandResult AllocateRemoteChatSessionResult(
    const AgentConfig & config,
    const std::string & thread_name,
    const std::string & module_name,
    const std::string & reasoning_level,
    const std::string & task_state,
    const std::string & short_goal,
    const std::string & task_id,
    const std::string & requested_session_title,
    const std::string & parent_session_id,
    const std::string & dispatch_mode) {
    return DispatchEnhancementOperations::AllocateRemoteChatSessionResult(
        config,
        thread_name,
        module_name,
        reasoning_level,
        task_state,
        short_goal,
        task_id,
        requested_session_title,
        parent_session_id,
        dispatch_mode);
}

inline std::string NormalizeIntentConfidenceRaw(const std::string & value) {
    return DispatchEnhancementOperations::NormalizeIntentConfidenceRaw(value);
}

inline std::string FirstStructuredFieldString(
    const std::string & request_body,
    const std::string & key,
    const std::string & fallback) {
    return DispatchEnhancementOperations::FirstStructuredFieldString(request_body, key, fallback);
}

inline std::string FirstStructuredFieldRaw(
    const std::string & request_body,
    const std::string & key,
    const std::string & fallback) {
    return DispatchEnhancementOperations::FirstStructuredFieldRaw(request_body, key, fallback);
}

inline CommandResult BuildDispatchContractMapResult(const std::string & table_name) {
    return DispatchEnhancementOperations::BuildDispatchContractMapResult(table_name);
}
