#pragma once

CommandResult BuildIntentDispatchPrepareResult(
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
    const std::string & arguments_text) {
    CommandResult result;
    result.fields["dispatcher"] = "intent_dispatch_prepare";
    result.fields["dispatch_mode"] = "enhanced_internal_layer";
    result.fields["compatibility_mode"] = "legacy_codex_fallback";
    result.fields["query"] = query;
    result.fields["arguments_text"] = arguments_text;
    result.fields["secondary_intents"] = secondary_intents;
    result.fields["association_scope"] = association_scope;
    result.fields["entity_refs"] = entity_refs;
    result.fields["evidence_refs"] = evidence_refs;
    result.fields["risk_flags"] = risk_flags;
    result.fields["requested_next_action"] = desired_next_action;
    result.fields["session_id"] = session_id;
    result.fields["turn_id"] = turn_id;
    result.fields["slice_summary"] = slice_summary;
    result.fields["expression_keys"] = expression_keys;
    result.fields["summary"] = summary;
    result.fields["insufficient_context"] = insufficient_context ? "true" : "false";

    const std::string normalized_intent = ResolvePrimaryIntentActionId(primary_intent);
    const std::string normalized_task_state = NormalizeTaskState(task_state, normalized_intent);
    const std::string normalized_reasoning =
        NormalizeReasoningLevel(reasoning_level, normalized_task_state);
    const std::string normalized_confidence_raw = NormalizeIntentConfidenceRaw(intent_confidence_raw);
    const double confidence = normalized_confidence_raw.empty()
        ? 0.0
        : std::atof(normalized_confidence_raw.c_str());
    const bool structured_missing =
        task_state.empty() || reasoning_level.empty() || primary_intent.empty();
    const bool low_confidence = !normalized_confidence_raw.empty() && confidence < 0.55;
    const bool unresolved_intent = normalized_intent.empty();

    result.fields["task_state"] = normalized_task_state;
    result.fields["reasoning_level"] = normalized_reasoning;
    result.fields["primary_intent"] = primary_intent;
    result.fields["resolved_action_id"] = normalized_intent;
    result.fields["intent_confidence"] = normalized_confidence_raw.empty()
        ? "0"
        : normalized_confidence_raw;
    result.fields["allowed_index_depth"] =
        ReasoningLevelAllowedIndexDepth(normalized_reasoning);
    result.fields["allowed_chain_complexity"] =
        ReasoningLevelAllowedChainComplexity(normalized_reasoning);

    std::string session_index_status = "disabled";
    if (!session_id.empty()) {
        const std::filesystem::path slice_path = BuildDialogSlicePath(config, session_id);
        session_index_status = std::filesystem::exists(slice_path) ? "hit" : "miss";
        result.fields["session_slice_path"] = slice_path.string();
    }
    result.fields["session_index_status"] = session_index_status;
    result.fields["session_binding_mode"] =
        session_index_status == "hit" ? "dialog_session+execution_binding" : "stateless_or_new_session";

    const std::string scope_modules = association_scope.empty()
        ? "[\"unknown\"]"
        : ("[\"" + codex_lan_agent::JsonEscape(association_scope) + "\"]");
    const std::string short_goal = !summary.empty()
        ? summary
        : (!desired_next_action.empty() ? desired_next_action : primary_intent);
    CommandResult session_allocation = AllocateRemoteChatSessionResult(
        config,
        "intranet_migration",
        association_scope.empty() ? "generic" : association_scope,
        normalized_reasoning,
        normalized_task_state,
        short_goal,
        session_id.empty() ? (primary_intent.empty() ? "task" : primary_intent) : session_id,
        std::string(),
        std::string(),
        normalized_reasoning == "high" ? "composite" : (normalized_reasoning == "medium" ? "advance" : "observe"));
    result.fields["remote_chat_session_id"] = GetFieldOrDefault(session_allocation, "remote_chat_session_id", "");
    result.fields["session_title"] = GetFieldOrDefault(session_allocation, "session_title", "");
    result.fields["session_registry_path"] = GetFieldOrDefault(session_allocation, "session_registry_path", "");
    result.fields["dispatch_mode"] = GetFieldOrDefault(session_allocation, "dispatch_mode", "");
    result.fields["title_template"] = GetFieldOrDefault(session_allocation, "title_template", "");

    CommandResult execution_card = BuildSemanticExecutionCardResult(
        "intranet_migration",
        result.fields["session_title"],
        session_id,
        normalized_task_state,
        normalized_reasoning,
        primary_intent,
        secondary_intents,
        scope_modules,
        summary.empty() ? desired_next_action : summary,
        evidence_refs.empty() ? "remote_control_events or task/log evidence" : evidence_refs,
        insufficient_context ? "blocked" : "required",
        desired_next_action.empty() ? "fallback to semantic_action_prepare" : desired_next_action);
    result.fields["semantic_execution_card"] =
        GetFieldOrDefault(execution_card, "semantic_execution_card", "");

    if (structured_missing || low_confidence || unresolved_intent || insufficient_context) {
        CommandResult fallback = BuildSemanticActionPrepareResult(
            std::string(),
            query,
            arguments_text);
        result = fallback;
        result.fields["dispatcher"] = "intent_dispatch_prepare";
        result.fields["dispatch_mode"] = "enhanced_internal_layer";
        result.fields["compatibility_mode"] = "legacy_codex_fallback";
        result.fields["task_state"] = normalized_task_state;
        result.fields["reasoning_level"] = normalized_reasoning;
        result.fields["primary_intent"] = primary_intent;
        result.fields["resolved_action_id"] = normalized_intent;
        result.fields["intent_confidence"] = normalized_confidence_raw.empty() ? "0" : normalized_confidence_raw;
        result.fields["summary"] = summary;
        result.fields["insufficient_context"] = insufficient_context ? "true" : "false";
        result.fields["allowed_index_depth"] = ReasoningLevelAllowedIndexDepth(normalized_reasoning);
        result.fields["allowed_chain_complexity"] = ReasoningLevelAllowedChainComplexity(normalized_reasoning);
        result.fields["session_index_status"] = session_index_status;
        result.fields["session_binding_mode"] =
            session_index_status == "hit" ? "dialog_session+execution_binding" : "stateless_or_new_session";
        result.fields["fallback_applied"] = "true";
        result.fields["fallback_reason"] = insufficient_context
            ? "insufficient_context"
            : (structured_missing
                ? "missing_structured_output"
                : (low_confidence ? "low_confidence" : "unresolved_primary_intent"));
        result.fields["writeback_required"] = "true";
        result.fields["remote_chat_required"] = "true";
        result.fields["dispatch_prewrite_status"] = "recorded";
        result.fields["next_action"] = GetFieldOrDefault(
            fallback,
            "next_action",
            "continue with legacy semantic_action_prepare");
        return result;
    }

    CommandResult prepared = BuildSemanticActionPrepareResult(
        normalized_intent,
        query,
        arguments_text);
    result = prepared;
    result.fields["dispatcher"] = "intent_dispatch_prepare";
    result.fields["dispatch_mode"] = "enhanced_internal_layer";
    result.fields["compatibility_mode"] = "legacy_codex_fallback";
    result.fields["task_state"] = normalized_task_state;
    result.fields["reasoning_level"] = normalized_reasoning;
    result.fields["primary_intent"] = primary_intent;
    result.fields["secondary_intents"] = secondary_intents;
    result.fields["intent_confidence"] = normalized_confidence_raw;
    result.fields["association_scope"] = association_scope;
    result.fields["entity_refs"] = entity_refs;
    result.fields["evidence_refs"] = evidence_refs;
    result.fields["risk_flags"] = risk_flags;
    result.fields["requested_next_action"] = desired_next_action;
    result.fields["session_id"] = session_id;
    result.fields["turn_id"] = turn_id;
    result.fields["slice_summary"] = slice_summary;
    result.fields["expression_keys"] = expression_keys;
    result.fields["summary"] = summary;
    result.fields["insufficient_context"] = insufficient_context ? "true" : "false";
    result.fields["allowed_index_depth"] = ReasoningLevelAllowedIndexDepth(normalized_reasoning);
    result.fields["allowed_chain_complexity"] = ReasoningLevelAllowedChainComplexity(normalized_reasoning);
    result.fields["session_index_status"] = session_index_status;
    result.fields["session_binding_mode"] =
        session_index_status == "hit" ? "dialog_session+execution_binding" : "stateless_or_new_session";
    result.fields["default_action_chain"] =
        BuildDefaultActionChain(normalized_reasoning, normalized_intent);
    result.fields["fallback_applied"] = session_index_status == "miss" ? "true" : "false";
    result.fields["fallback_reason"] =
        session_index_status == "miss" ? "session_index_miss" : "none";
    result.fields["writeback_required"] = "true";
    result.fields["remote_chat_required"] = "true";
    result.fields["dispatch_prewrite_status"] = "recorded";

    const CommandResult tool_call = BuildSemanticActionToolCallResult(
        normalized_intent,
        query,
        arguments_text,
        normalized_reasoning == "high");
    result.fields["tool_call_ready"] = GetFieldOrDefault(tool_call, "tool_call_ready", "false");
    result.fields["tool_name"] = GetFieldOrDefault(tool_call, "tool_name", GetFieldOrDefault(result, "tool", ""));
    result.fields["tool_arguments_json"] = GetFieldOrDefault(tool_call, "tool_arguments_json", "");
    result.fields["mcp_tool_call_json"] = GetFieldOrDefault(tool_call, "mcp_tool_call_json", "");
    result.fields["dry_run_injected"] = GetFieldOrDefault(tool_call, "dry_run_injected", "false");
    result.fields["local_ai_thread_message_id"] =
        "msg-" + TimeStampForFileName() + "-" + SanitizeDispatchToken(turn_id, "turn");
    result.fields["evidence_ref"] =
        GetFieldOrDefault(session_allocation, "evidence_ref", BuildRemoteControlEventsPath(config));
    result.fields["result_ref"] = GetFieldOrDefault(tool_call, "tool_name", "");
    result.fields["execution_binding"] =
        std::string("{\"remote_chat_session_id\":\"")
        + codex_lan_agent::JsonEscape(result.fields["remote_chat_session_id"])
        + "\",\"local_ai_thread_message_id\":\""
        + codex_lan_agent::JsonEscape(result.fields["local_ai_thread_message_id"])
        + "\",\"task_id\":\""
        + codex_lan_agent::JsonEscape(session_id)
        + "\",\"evidence_ref\":\""
        + codex_lan_agent::JsonEscape(result.fields["evidence_ref"])
        + "\",\"result_ref\":\""
        + codex_lan_agent::JsonEscape(result.fields["result_ref"])
        + "\"}";
    result.fields["next_action"] =
        normalized_reasoning == "high"
            ? "review tool_call json, prefer dry_run_or_queue, then verify by task/log"
            : GetFieldOrDefault(result, "next_action", "execute resolved tool");
    return result;
}
