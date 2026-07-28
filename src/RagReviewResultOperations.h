#pragma once

CommandResult TaskLogTailResult(
    const AgentConfig & config,
    const std::string & task_id,
    int max_lines) {
    if (g_task_manager == nullptr) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "task manager is not active";
        return result;
    }

    const CommandResult task_result = g_task_manager->GetTaskResult(task_id);
    const auto result_log_it = task_result.fields.find("result_log_path");
    const auto live_log_it = task_result.fields.find("log_path");
    const std::string resolved_log_path =
        (result_log_it != task_result.fields.end() && !result_log_it->second.empty())
            ? result_log_it->second
            : ((live_log_it != task_result.fields.end() && !live_log_it->second.empty())
                ? live_log_it->second
                : std::string());
    if (resolved_log_path.empty()) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 42;
        result.fields["task_id"] = task_id;
        result.fields["error"] = "task has no log_path";
        return result;
    }

    CommandResult result = TailTextFileResult(config, resolved_log_path, max_lines);
    result.fields["task_id"] = task_id;
    result.fields["log_path"] = resolved_log_path;
    const std::string profile_name = GetFieldOrDefault(task_result, "arg1", "");
    result.fields["expected_marker"] = ExpectedMarkerForProfile(profile_name);
    const std::string semantic_outcome = AnalyzeSemanticOutcome(
        profile_name,
        task_result,
        GetFieldOrDefault(result, "content", ""));
    result.fields["semantic_outcome"] = semantic_outcome;
    result.fields["expected_marker_verified"] = GetFieldOrDefault(
        task_result,
        "result_expected_marker_verified",
        GetFieldOrDefault(task_result, "expected_marker_verified", ""));
    ApplyVerificationFields(&result, semantic_outcome);
    return result;
}

CommandResult RagLogClassifyResult(
    const AgentConfig & config,
    const std::string & file_path,
    const std::string & task_id,
    const std::string & log_text) {
    CommandResult result;
    std::string content = log_text;
    std::string source_ref = "inline_log_text";
    if (content.empty() && !task_id.empty()) {
        CommandResult tail = TaskLogTailResult(config, task_id, 120);
        content = GetFieldOrDefault(tail, "content", "");
        source_ref = "task-log(" + task_id + ")";
        result.fields["task_id"] = task_id;
        result.fields["log_path"] = GetFieldOrDefault(tail, "log_path", "");
    }
    if (content.empty() && !file_path.empty()) {
        CommandResult tail = TailTextFileResult(config, file_path, 120);
        content = GetFieldOrDefault(tail, "content", "");
        source_ref = file_path;
        result.fields["log_path"] = file_path;
    }
    const bool insufficient = content.empty();
    result.ok = !insufficient;
    result.exit_code = insufficient ? 60 : 0;
    result.fields["semantic_outcome"] = insufficient
        ? "insufficient_context"
        : AnalyzeSemanticOutcome("", result, content);
    if (result.fields["semantic_outcome"] == "failure") {
        result.ok = false;
        result.exit_code = 62;
    }
    result.fields["output_text"] = result.fields["semantic_outcome"];
    result.fields["source_refs"] = source_ref;
    result.fields["evidence_lines"] = content.substr(0, std::min<std::size_t>(content.size(), 2000));
    result.fields["confidence"] = insufficient ? "low" : "high";
    result.fields["risk_level"] =
        result.fields["semantic_outcome"] == "failure" ? "high" : (insufficient ? "medium" : "low");
    result.fields["insufficient_context"] = insufficient ? "true" : "false";
    result.fields["summary"] = insufficient ? "no log content available" : result.fields["semantic_outcome"];
    result.fields["next_action"] = insufficient ? "provide file_path, task_id, or log_text" : "inspect evidence_lines";
    return result;
}

CommandResult RagDiffReviewResult(
    const AgentConfig & config,
    const std::string & diff_text) {
    CommandResult result;
    std::string content = diff_text;
    std::string source_ref = "inline_diff_text";
    (void)config;
    const bool insufficient = content.empty();
    result.ok = !insufficient;
    result.exit_code = insufficient ? 61 : 0;
    result.fields["semantic_outcome"] = insufficient ? "insufficient_context" : "diff_review_ready";
    std::string output_text = "insufficient context: provide diff_text or make workspace diff readable";
    std::string confidence = "low";
    if (!insufficient) {
        output_text =
            "diff evidence captured; use evidence_lines for basic review or call rag.query for deeper analysis";
        confidence = "medium";
        result.fields["review_status_code"] = "skipped";
        result.fields["review_log_path"] = "";
        result.fields["review_error"] = "";
    }
    result.fields["output_text"] = output_text;
    result.fields["source_refs"] = source_ref;
    result.fields["evidence_lines"] = content.substr(0, std::min<std::size_t>(content.size(), 4000));
    result.fields["confidence"] = confidence;
    if (result.fields.find("insufficient_context") == result.fields.end()) {
        result.fields["insufficient_context"] = insufficient ? "true" : "false";
    }
    if (result.fields.find("risk_level") == result.fields.end()) {
        result.fields["risk_level"] = insufficient ? "medium" : "low";
    }
    if (result.fields.find("fallback") == result.fields.end()) {
        result.fields["fallback"] = insufficient
            ? "{\"tool\":\"lan_agent_snapshot_diff\",\"reason\":\"diff_text unavailable\"}"
            : "{\"tool\":\"rag.query\",\"reason\":\"deeper AI review requested\"}";
    }
    result.fields["summary"] = result.fields["output_text"];
    result.fields["next_action"] = insufficient ? "provide diff_text or fix git root" : "review evidence_lines";
    return result;
}
