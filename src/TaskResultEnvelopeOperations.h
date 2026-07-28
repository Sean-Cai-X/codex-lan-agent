#pragma once

std::string StableContentChecksum(const std::string & content);

CommandResult DiscoverCtestTestsResult(
    const AgentConfig & config,
    const std::string & build_dir,
    const std::string & config_name,
    const std::string & test_regex,
    int start_index,
    int max_entries);

bool ContainsNonAsciiText(const std::string & value) {
    for (unsigned char ch : value) {
        if (ch > 0x7F) {
            return true;
        }
    }
    return false;
}

std::string BuildPreflightReference(
    const std::string & tool_name,
    const std::vector<std::string> & parts) {
    std::ostringstream seed;
    seed << tool_name;
    for (const std::string & part : parts) {
        seed << "|" << part;
    }
    std::ostringstream encoded;
    encoded << "preflight:" << tool_name << ":" << StableContentChecksum(seed.str());
    static const char hex_digits[] = "0123456789ABCDEF";
    for (const std::string & part : parts) {
        encoded << ":";
        for (unsigned char ch : part) {
            const bool is_unreserved =
                (ch >= 'A' && ch <= 'Z')
                || (ch >= 'a' && ch <= 'z')
                || (ch >= '0' && ch <= '9')
                || ch == '-' || ch == '_' || ch == '.' || ch == '~';
            if (is_unreserved) {
                encoded << static_cast<char>(ch);
            } else {
                encoded << "%"
                        << hex_digits[(ch >> 4) & 0x0F]
                        << hex_digits[ch & 0x0F];
            }
        }
    }
    return encoded.str();
}

bool TryParsePreflightReference(
    const std::string & preflight_ref,
    const std::string & expected_tool_name,
    std::vector<std::string> * parts,
    std::string * checksum) {
    if (parts != nullptr) {
        parts->clear();
    }
    if (checksum != nullptr) {
        checksum->clear();
    }
    const std::string trimmed = Trim(preflight_ref);
    const std::string prefix = "preflight:" + expected_tool_name + ":";
    if (trimmed.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::size_t checksum_end = trimmed.find(':', prefix.size());
    if (checksum_end == std::string::npos) {
        if (checksum != nullptr) {
            *checksum = trimmed.substr(prefix.size());
        }
        return true;
    }
    if (checksum != nullptr) {
        *checksum = trimmed.substr(prefix.size(), checksum_end - prefix.size());
    }
    if (parts == nullptr) {
        return true;
    }
    std::size_t cursor = checksum_end + 1;
    while (cursor <= trimmed.size()) {
        const std::size_t next = trimmed.find(':', cursor);
        const std::string token = next == std::string::npos
            ? trimmed.substr(cursor)
            : trimmed.substr(cursor, next - cursor);
        parts->push_back(UrlDecode(token));
        if (next == std::string::npos) {
            break;
        }
        cursor = next + 1;
    }
    return true;
}

void ApplyEncodingAdvisory(
    CommandResult * result,
    const std::string & input_text,
    const std::string & source_field) {
    if (result == nullptr) {
        return;
    }
    if (ContainsNonAsciiText(input_text)) {
        result->fields["encoding_warning"] =
            "non-ASCII input detected; prefer UTF-8 end-to-end and validate remote logs/echo for mojibake";
        result->fields["encoding_contract"] = "utf8_preferred_ascii_safe";
        result->fields["encoding_input_field"] = source_field;
        result->fields["encoding_input_contains_non_ascii"] = "true";
    } else if (!input_text.empty()) {
        result->fields["encoding_contract"] = "utf8_preferred_ascii_safe";
        result->fields["encoding_input_field"] = source_field;
        result->fields["encoding_input_contains_non_ascii"] = "false";
    }
}

CommandResult BuildTargetDryRunResult(
    const std::string & build_dir,
    const std::string & target,
    const std::string & config_name) {
    CommandResult result;
    result.fields["status"] = "validated";
    result.fields["dry_run"] = "true";
    result.fields["build_dir"] = build_dir;
    result.fields["target"] = target;
    result.fields["config"] = config_name;
    result.fields["profile"] = "build_target";
    result.fields["expected_marker"] = ExpectedMarkerForProfile("build_target");
    result.fields["semantic_outcome"] = "args_valid";
    result.fields["summary"] = "build_target arguments validated; no task queued";
    result.fields["next_action"] = "submit without dry_run to queue build";
    return result;
}

CommandResult BuildBuildTargetPreflightResult(
    const std::string & build_dir,
    const std::string & target,
    const std::string & config_name) {
    CommandResult result = BuildTargetDryRunResult(build_dir, target, config_name);
    result.fields["result"] = "preflight_ready";
    result.fields["preflight_scope"] = "cxparser_preflight_guard";
    result.fields["preflight_status"] = "ready";
    result.fields["preflight_reason_code"] = "validated_build_target_args";
    result.fields["preflight_ref"] =
        BuildPreflightReference("lan_agent_build_target", {build_dir, target, config_name});
    result.fields["preflight_tool_name"] = "lan_agent_preflight_build_target";
    result.fields["summary"] = "build target preflight ready";
    result.fields["next_action"] =
        "call lan_agent_build_target with the returned preflight_ref";
    result.fields["preflight_contract"] =
        "Use preflight_ref for lan_agent_build_target instead of hand-writing preflight_status=ready.";
    ApplyEncodingAdvisory(&result, target, "target");
    return result;
}

CommandResult BuildRunCtestPreflightResult(
    const AgentConfig & config,
    const std::string & build_dir,
    const std::string & config_name,
    const std::string & test_regex) {
    if (build_dir.empty() || test_regex.empty()) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "build_dir and test_regex are required";
        result.fields["result"] = "preflight_blocked";
        result.fields["preflight_scope"] = "cxparser_preflight_guard";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_required_args";
        result.fields["summary"] = "ctest preflight blocked";
        result.fields["next_action"] = "provide build_dir and test_regex";
        return result;
    }

    CommandResult discover = DiscoverCtestTestsResult(config, build_dir, config_name, test_regex, 0, 200);
    discover.fields["preflight_scope"] = "cxparser_preflight_guard";
    discover.fields["preflight_tool_name"] = "lan_agent_preflight_run_ctest_target";
    discover.fields["preflight_contract"] =
        "Use preflight_ref for lan_agent_run_ctest_target instead of hand-writing preflight_status=ready.";
    if (!discover.ok || GetFieldOrDefault(discover, "semantic_outcome", "") == "no_tests_found"
        || GetFieldOrDefault(discover, "semantic_outcome", "") == "build_dir_missing"
        || GetFieldOrDefault(discover, "semantic_outcome", "") == "not_configured") {
        discover.ok = false;
        if (discover.exit_code == 0) {
            discover.exit_code = 52;
        }
        discover.fields["result"] = "preflight_blocked";
        discover.fields["preflight_status"] = "blocked";
        discover.fields["preflight_reason_code"] = FirstNonEmpty(
            GetFieldOrDefault(discover, "semantic_outcome", ""),
            GetFieldOrDefault(discover, "error", ""),
            "ctest_preflight_blocked");
        if (GetFieldOrDefault(discover, "summary", "").empty()) {
            discover.fields["summary"] = "ctest preflight blocked";
        }
        if (GetFieldOrDefault(discover, "next_action", "").empty()) {
            discover.fields["next_action"] =
                "fix ctest discovery preflight before queueing lan_agent_run_ctest_target";
        }
        return discover;
    }

    discover.fields["result"] = "preflight_ready";
    discover.fields["preflight_status"] = "ready";
    discover.fields["preflight_reason_code"] = "ctest_discovery_ready";
    discover.fields["preflight_ref"] =
        BuildPreflightReference("lan_agent_run_ctest_target", {build_dir, config_name, test_regex});
    discover.fields["summary"] = "ctest preflight ready";
    discover.fields["next_action"] =
        "call lan_agent_run_ctest_target with the returned preflight_ref";
    ApplyEncodingAdvisory(&discover, test_regex, "test_regex");
    return discover;
}

CommandResult BuildQueuedTaskResult(const std::string & task_id) {
    if (g_task_manager == nullptr) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "task manager is not active";
        return result;
    }

    CommandResult result = g_task_manager->GetTaskResult(task_id);
    result.fields["task_id"] = task_id;
    return result;
}

std::string ExtractTaskIdFromReference(const std::string & value) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return std::string();
    }
    if (trimmed.rfind("task-log(", 0) == 0 && trimmed.back() == ')') {
        return trimmed.substr(9, trimmed.size() - 10);
    }
    if (trimmed.rfind("task:", 0) == 0) {
        return trimmed.substr(5);
    }
    if (trimmed.rfind("task-", 0) == 0) {
        return trimmed;
    }
    return std::string();
}

CommandResult ResolveTaskResultReferenceResult(
    const std::string & task_id,
    const std::string & task_ref) {
    CommandResult result;
    const std::string resolved_task_id = !task_id.empty()
        ? task_id
        : ExtractTaskIdFromReference(task_ref);
    result.fields["task_id"] = resolved_task_id;
    result.fields["task_ref"] = task_ref;
    if (resolved_task_id.empty()) {
        result.ok = false;
        result.exit_code = 42;
        result.fields["error"] = "task_id or task_ref is required";
        result.fields["failure_mode"] = "task_reference_missing";
        result.fields["summary"] = "task result reference unresolved";
        return result;
    }
    if (g_task_manager == nullptr) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "task manager is not active";
        result.fields["failure_mode"] = "task_manager_unavailable";
        result.fields["summary"] = "task manager unavailable";
        return result;
    }

    CommandResult task_result = g_task_manager->GetTaskResult(resolved_task_id);
    result = task_result;
    result.fields["task_id"] = resolved_task_id;
    result.fields["task_ref"] = task_ref.empty() ? ("task-log(" + resolved_task_id + ")") : task_ref;
    result.fields["task_log_ref"] = GetFieldOrDefault(
        task_result,
        "task_log_ref",
        "task-log(" + resolved_task_id + ")");
    result.fields["resolved_result_ref"] = GetFieldOrDefault(
        task_result,
        "result_ref",
        GetFieldOrDefault(task_result, "resolved_log_path", ""));
    result.fields["resolved_evidence_ref"] = GetFieldOrDefault(
        task_result,
        "evidence_ref",
        GetFieldOrDefault(task_result, "resolved_log_path", ""));
    result.fields["resolved_log_path"] = GetFieldOrDefault(
        task_result,
        "resolved_log_path",
        FirstNonEmpty(
            GetFieldOrDefault(task_result, "result_log_path", ""),
            GetFieldOrDefault(task_result, "log_path", "")));
    result.fields["result_ref"] = result.fields["resolved_result_ref"];
    result.fields["evidence_ref"] = result.fields["resolved_evidence_ref"];
    result.fields["summary"] = task_result.ok
        ? "task result reference resolved"
        : GetFieldOrDefault(task_result, "summary", "task result reference resolved with failure");
    if (GetFieldOrDefault(result, "next_action", "").empty()) {
        result.fields["next_action"] = GetFieldOrDefault(result, "resolved_log_path", "").empty()
            ? "inspect task status"
            : "inspect resolved_log_path";
    }
    return result;
}
