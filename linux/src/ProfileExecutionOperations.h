#pragma once

struct CxParserRuntimeBindingInfo {
    bool available = false;
    std::string source;
    std::string entrypoint;
};

bool IsCxParserPublicCliFlow(const std::string & flow_id) {
    return flow_id == "cxparser_ext_cxscript_cli";
}

std::string QuoteConfiguredCommandPath(const std::string & value) {
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

std::string CxParserCliExecutableName() {
#ifdef _WIN32
    return "cxparser_ext_cxscript_cli.exe";
#else
    return "cxparser_ext_cxscript_cli";
#endif
}

std::filesystem::path CanonicalCxParserPublicBuildRoot(const AgentConfig & config) {
    const std::filesystem::path workspace_root =
        std::filesystem::path(config.workspace_root);
    return workspace_root / "cxparser" / "build";
}

std::string DetermineProcessProgressSignal(const codex_lan_agent::ProcessRunResult & run_result) {
    if (run_result.process_output_observed) {
        return "process_output";
    }
    if (run_result.heartbeat_count > 0) {
        return "heartbeat_only";
    }
    return "no_observed_output";
}

std::string DetermineTimeoutDiagnostic(const codex_lan_agent::ProcessRunResult & run_result) {
    const std::string progress_signal = DetermineProcessProgressSignal(run_result);
    if (run_result.stalled) {
        return progress_signal == "process_output"
            ? "process_output_went_quiet_before_stall"
            : "no_observed_output_before_stall";
    }
    if (run_result.timed_out) {
        if (progress_signal == "process_output") {
            return "process_output_observed_before_total_timeout";
        }
        if (progress_signal == "heartbeat_only") {
            return "heartbeat_only_before_total_timeout";
        }
        return "no_observed_output_before_total_timeout";
    }
    return "not_applicable";
}

void AddCxParserCliCandidate(
    std::vector<std::filesystem::path> * candidates,
    const std::filesystem::path & directory) {
    if (candidates == nullptr || directory.empty()) {
        return;
    }
    const std::string executable_name = CxParserCliExecutableName();
    candidates->push_back(directory / executable_name);
    candidates->push_back(directory / "Release" / executable_name);
    candidates->push_back(directory / "Debug" / executable_name);
    candidates->push_back(directory / "RelWithDebInfo" / executable_name);
}

std::string FindCxParserCxScriptCliExecutablePath(const AgentConfig & config) {
    std::vector<std::filesystem::path> candidates;
    AddCxParserCliCandidate(&candidates, CanonicalCxParserPublicBuildRoot(config));
    for (const auto & candidate : candidates) {
        std::error_code candidate_ec;
        if (std::filesystem::is_regular_file(candidate, candidate_ec) && !candidate_ec) {
            return candidate.string();
        }
    }
    return std::string();
}

CxParserRuntimeBindingInfo ResolveCxParserRuntimeBindingInfo(
    const AgentConfig & config,
    const std::string & flow_id) {
    CxParserRuntimeBindingInfo info;
    if (IsCxParserPublicCliFlow(flow_id)) {
        const std::string cli_path = FindCxParserCxScriptCliExecutablePath(config);
        if (!cli_path.empty()) {
            info.available = true;
            info.source = "cxparser_public_build";
            info.entrypoint = cli_path;
            return info;
        }
        return info;
    }

    const auto runtime_it = config.cxparser_runtime_commands.find(flow_id);
    if (runtime_it != config.cxparser_runtime_commands.end() && !runtime_it->second.empty()) {
        info.available = true;
        info.source = "cxparser_runtime_command";
        info.entrypoint = "cxparser_runtime." + flow_id;
        return info;
    }

    const auto profile_it = config.profiles.find(flow_id);
    if (profile_it != config.profiles.end() && !profile_it->second.empty()) {
        info.available = true;
        info.source = "profile_fallback";
        info.entrypoint = "profile." + flow_id;
        return info;
    }
    return info;
}

CommandResult RunConfiguredCommand(
    const AgentConfig & config,
    const std::string & command_label,
    const std::string & expected_marker_profile,
    const std::string & command_line,
    const std::string & extra_arguments,
    const std::string & forced_log_path,
    int timeout_sec_override = -1,
    int stall_timeout_sec_override = -1) {
    CommandResult result;
    result.fields["profile"] = command_label;
    result.fields["expected_marker"] = expected_marker_profile.empty()
        ? std::string()
        : ExpectedMarkerForProfile(expected_marker_profile);

    const std::string resource_key = BuildTaskResourceKey(command_label, extra_arguments);
    ScopedResourceLock resource_lock(resource_key);
    if (!resource_lock.acquired()) {
        result.ok = false;
        result.exit_code = 41;
        result.fields["error"] = "resource is busy";
        result.fields["resource_key"] = resource_key;
        return result;
    }

    const std::string effective_command_line = extra_arguments.empty()
        ? command_line
        : (command_line + " " + extra_arguments);
    const std::string log_path = forced_log_path.empty()
        ? BuildLogPath(config, command_label)
        : forced_log_path;
    const int effective_timeout_sec =
        timeout_sec_override > 0 ? timeout_sec_override : config.task_timeout_sec;
    const int effective_stall_timeout_sec =
        stall_timeout_sec_override >= 0 ? stall_timeout_sec_override : 30;

    codex_lan_agent::ProcessRunResult run_result;
    std::string error_message;
    if (!codex_lan_agent::RunCommandWithLog(
            effective_command_line,
            config.workspace_root,
            log_path,
            effective_timeout_sec,
            effective_stall_timeout_sec,
            &run_result,
            &error_message)) {
        result.ok = false;
        result.exit_code = 4;
        result.fields["error"] = error_message;
        return result;
    }

    result.ok = run_result.exit_code == 0 && !run_result.timed_out;
    result.exit_code = run_result.exit_code;
    result.fields["timed_out"] = run_result.timed_out ? "true" : "false";
    result.fields["stalled"] = run_result.stalled ? "true" : "false";
    result.fields["process_exit_ok"] = result.ok ? "true" : "false";
    result.fields["process_id"] = std::to_string(run_result.process_id);
    result.fields["log_path"] = run_result.log_path;
    result.fields["started_at"] = run_result.started_at;
    result.fields["finished_at"] = run_result.finished_at;
    result.fields["last_output_at"] = run_result.last_output_at;
    result.fields["effective_timeout_sec"] = std::to_string(effective_timeout_sec);
    result.fields["effective_stall_timeout_sec"] = std::to_string(effective_stall_timeout_sec);
    result.fields["execution_completion_reason"] = run_result.completion_reason;
    result.fields["process_progress_signal"] = DetermineProcessProgressSignal(run_result);
    result.fields["timeout_diagnostic"] = DetermineTimeoutDiagnostic(run_result);
    result.fields["runtime_sec"] =
        std::to_string(run_result.runtime_sec < 0 ? 0LL : run_result.runtime_sec);
    result.fields["quiet_sec_at_finish"] =
        std::to_string(run_result.quiet_sec_at_finish < 0 ? 0LL : run_result.quiet_sec_at_finish);
    result.fields["heartbeat_count"] = std::to_string(run_result.heartbeat_count);
    result.fields["process_output_observed"] = run_result.process_output_observed ? "true" : "false";
    if (!resource_key.empty()) {
        result.fields["resource_key"] = resource_key;
    }

    std::string log_content;
    std::string log_read_error;
    if (ReadWholeFile(run_result.log_path, &log_content, &log_read_error)) {
        if (!expected_marker_profile.empty()) {
            const std::string semantic_outcome = AnalyzeSemanticOutcome(expected_marker_profile, result, log_content);
            result.fields["semantic_outcome"] = semantic_outcome;
            result.fields["expected_marker_verified"] =
                VerifyExpectedMarker(expected_marker_profile, result, semantic_outcome, log_content) ? "true" : "false";
            result.fields["verification_ok"] =
                result.fields["expected_marker_verified"] == "true" ? "true" : "false";
            ApplyVerificationFields(&result, semantic_outcome);
        } else {
            result.fields["semantic_outcome"] = result.ok ? "succeeded" : "failed";
            result.fields["expected_marker_verified"] = result.ok ? "true" : "false";
            result.fields["verification_ok"] = result.ok ? "true" : "false";
            ApplyVerificationFields(&result, result.fields["semantic_outcome"]);
        }
    } else {
        result.fields["semantic_outcome"] = result.ok ? "succeeded" : "failed";
        result.fields["expected_marker_verified"] = result.ok ? "true" : "false";
        result.fields["verification_ok"] = result.ok ? "true" : "false";
        ApplyVerificationFields(&result, result.fields["semantic_outcome"]);
        result.fields["log_read_error"] = log_read_error;
    }
    return result;
}

bool HasCxParserRuntimeBinding(
    const AgentConfig & config,
    const std::string & flow_id,
    std::string * source) {
    const CxParserRuntimeBindingInfo info = ResolveCxParserRuntimeBindingInfo(config, flow_id);
    if (info.available) {
        if (source != nullptr) {
            *source = info.source;
        }
        return true;
    }
    if (source != nullptr) {
        *source = "";
    }
    return false;
}

int ResolveCxParserRuntimeTimeoutSec(
    const AgentConfig & config,
    const std::string & flow_id) {
    const auto it = config.cxparser_runtime_timeouts_sec.find(flow_id);
    if (it != config.cxparser_runtime_timeouts_sec.end() && it->second > 0) {
        return it->second;
    }
    return config.task_timeout_sec;
}

int ResolveCxParserRuntimeStallTimeoutSec(
    const AgentConfig & config,
    const std::string & flow_id) {
    const auto it = config.cxparser_runtime_stall_timeouts_sec.find(flow_id);
    if (it != config.cxparser_runtime_stall_timeouts_sec.end() && it->second > 0) {
        return it->second;
    }
    return 30;
}

int ResolveProfileTimeoutSec(
    const AgentConfig & config,
    const std::string & profile_name) {
    const auto configured_it = config.profile_timeouts_sec.find(profile_name);
    if (configured_it != config.profile_timeouts_sec.end() && configured_it->second > 0) {
        return configured_it->second;
    }
    return config.task_timeout_sec;
}

int ResolveProfileStallTimeoutSec(
    const AgentConfig & config,
    const std::string & profile_name) {
    const auto configured_it = config.profile_stall_timeouts_sec.find(profile_name);
    if (configured_it != config.profile_stall_timeouts_sec.end() && configured_it->second >= 0) {
        return configured_it->second;
    }
    if (profile_name == "build_target") {
        return config.build_target_stall_timeout_sec;
    }
    if (profile_name == "configure_project") {
        return config.configure_project_stall_timeout_sec;
    }
    if (profile_name == "run_script") {
        return 0;
    }
    return -1;
}

CommandResult RunCxParserRuntimeCommand(
    const AgentConfig & config,
    const std::string & flow_id,
    const std::string & extra_arguments,
    const std::string & forced_log_path) {
    const int effective_timeout_sec =
        ResolveCxParserRuntimeTimeoutSec(config, flow_id);
    const int effective_stall_timeout_sec =
        ResolveCxParserRuntimeStallTimeoutSec(config, flow_id);
    const auto runtime_it = config.cxparser_runtime_commands.find(flow_id);
    if (runtime_it != config.cxparser_runtime_commands.end() && !runtime_it->second.empty()) {
        CommandResult result = RunConfiguredCommand(
            config,
            "cxparser_runtime_" + flow_id,
            std::string(),
            runtime_it->second,
            extra_arguments,
            forced_log_path,
            effective_timeout_sec,
            effective_stall_timeout_sec);
        result.fields["cxparser_runtime_source"] = "cxparser_runtime_command";
        result.fields["cxparser_runtime_flow_id"] = flow_id;
        result.fields["effective_timeout_scope"] = "cxparser_runtime_flow";
        result.fields["result_ref"] = GetFieldOrDefault(result, "log_path", "");
        result.fields["evidence_ref"] = GetFieldOrDefault(result, "log_path", "");
        return result;
    }

    const auto profile_it = config.profiles.find(flow_id);
    if (profile_it != config.profiles.end() && !profile_it->second.empty()) {
        CommandResult result = RunConfiguredCommand(
            config,
            flow_id,
            flow_id,
            profile_it->second,
            extra_arguments,
            forced_log_path,
            effective_timeout_sec,
            effective_stall_timeout_sec);
        result.fields["cxparser_runtime_source"] = "profile_fallback";
        result.fields["cxparser_runtime_flow_id"] = flow_id;
        result.fields["effective_timeout_scope"] = "cxparser_runtime_flow";
        return result;
    }

    if (IsCxParserPublicCliFlow(flow_id)) {
        const std::string cli_path = FindCxParserCxScriptCliExecutablePath(config);
        if (!cli_path.empty()) {
            CommandResult result = RunConfiguredCommand(
                config,
                "cxparser_runtime_" + flow_id,
                std::string(),
                QuoteConfiguredCommandPath(cli_path),
                extra_arguments,
                forced_log_path,
                effective_timeout_sec,
                effective_stall_timeout_sec);
            result.fields["cxparser_runtime_source"] = "cxparser_public_build";
            result.fields["cxparser_runtime_contract"] = "public_build_only";
            result.fields["cxparser_runtime_flow_id"] = flow_id;
            result.fields["cxparser_runtime_entrypoint"] = cli_path;
            result.fields["cxparser_runtime_public_build_root"] =
                CanonicalCxParserPublicBuildRoot(config).string();
            result.fields["effective_timeout_scope"] = "cxparser_runtime_flow";
            result.fields["result_ref"] = GetFieldOrDefault(result, "log_path", "");
            result.fields["evidence_ref"] = GetFieldOrDefault(result, "log_path", "");
            return result;
        }
    }

    CommandResult result;
    result.ok = false;
    result.exit_code = 74;
    result.fields["error"] = "cxparser runtime binding is not configured";
    result.fields["summary"] = "cxparser runtime binding is missing";
    result.fields["next_action"] = "configure cxparser_runtime." + flow_id + " or profile." + flow_id + " and retry";
    result.fields["cxparser_runtime_flow_id"] = flow_id;
    return result;
}

CommandResult RunCliProfile(
    const AgentConfig & config,
    const std::string & profile_name,
    const std::string & extra_arguments,
    const std::string & forced_log_path,
    int timeout_sec_override,
    int stall_timeout_sec_override) {
    CommandResult result;
    result.fields["profile"] = profile_name;
    result.fields["expected_marker"] = ExpectedMarkerForProfile(profile_name);

    const auto it = config.profiles.find(profile_name);
    if (it == config.profiles.end()) {
        result.ok = false;
        result.exit_code = 3;
        result.fields["error"] = "unknown profile";
        return result;
    }

    const int resolved_timeout_sec =
        timeout_sec_override > 0
            ? timeout_sec_override
            : ResolveProfileTimeoutSec(config, profile_name);
    const int resolved_stall_timeout_sec =
        stall_timeout_sec_override >= 0
            ? stall_timeout_sec_override
            : ResolveProfileStallTimeoutSec(config, profile_name);

    result = RunConfiguredCommand(
        config,
        profile_name,
        profile_name,
        it->second,
        extra_arguments,
        forced_log_path,
        resolved_timeout_sec,
        resolved_stall_timeout_sec);
    result.fields["profile"] = profile_name;
    result.fields["expected_marker"] = ExpectedMarkerForProfile(profile_name);
    result.fields["profile_timeout_sec"] = std::to_string(std::max(0, resolved_timeout_sec));
    result.fields["profile_timeout_source"] =
        timeout_sec_override > 0
            ? "request"
            : (config.profile_timeouts_sec.find(profile_name) != config.profile_timeouts_sec.end()
                ? "config.profile_timeout"
                : "config.task_timeout_sec");
    result.fields["profile_stall_timeout_sec"] = std::to_string(std::max(0, resolved_stall_timeout_sec));
    result.fields["profile_stall_timeout_source"] =
        stall_timeout_sec_override >= 0
            ? "request"
            : (config.profile_stall_timeouts_sec.find(profile_name) != config.profile_stall_timeouts_sec.end()
                ? "config.profile_stall_timeout"
                : (profile_name == "build_target"
                    ? "config.build_target_stall_timeout_sec"
                    : (profile_name == "configure_project"
                        ? "config.configure_project_stall_timeout_sec"
                        : (profile_name == "run_script" ? "built_in.run_script_no_stall" : "default_30s"))));
    if (profile_name == "run_ctest_target") {
        AddCtestDiscoveryHints(config, extra_arguments, &result);
    }
    return result;
}

CommandResult RunCase(
    const AgentConfig & config,
    const std::string & case_path) {
    return RunCliProfile(config, "run_case", "--case-path \"" + case_path + "\"");
}

CommandResult RunRagFlow(
    const AgentConfig & config,
    const std::string & query,
    const std::string & mode) {
    CommandResult result;
    if (config.generation_endpoint.empty()) {
        result.ok = false;
        result.exit_code = 5;
        result.fields["error"] = "generation_endpoint is not configured";
        return result;
    }

    const std::string body =
        std::string("{\"model\":\"gpt-4.1\",\"temperature\":0,\"messages\":[")
        + "{\"role\":\"system\",\"content\":\"You are a LAN agent for codex-driven internal development. This entry is analysis-only. Do not claim to have edited files, run builds, or executed tests unless a real MCP execution tool produced task_id, result_ref, or evidence_ref. Return concise plain text and point to the required MCP tools for real execution.\"},"
        + "{\"role\":\"user\",\"content\":\"mode=" + codex_lan_agent::JsonEscape(mode)
        + "\\nquery=" + codex_lan_agent::JsonEscape(query) + "\"}]}";

    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(config.generation_endpoint, body, 10000);

    const std::string log_path = BuildLogPath(config, "run_rag_flow");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << config.generation_endpoint << "\n";
    output << "mode=" << mode << "\n";
    output << "query=" << query << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 6;
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["log_path"] = log_path;
    result.fields["mode"] = mode;
    result.fields["query"] = query;
    result.fields["body"] = response.body;
    result.fields["analysis_only"] = "true";
    result.fields["execution_capability"] = "false";
    result.fields["trusted_execution_evidence_required"] = "true";
    result.fields["execution_evidence_contract"] =
        "For source edits use lan_agent_apply_diff_patch or lan_agent_preview_patch plus "
        "lan_agent_apply_single_file_patch and lan_agent_verify_single_file_patch. "
        "Use lan_agent_write_text_file only for non-source text files. "
        "For builds use lan_agent_configure_project or lan_agent_build_target. "
        "For tests use lan_agent_run_ctest_target. "
        "You may also use lan_agent_execute_semantic_action as the execution bridge. "
        "Return real task_id, result_ref, evidence_ref, patch_id, or log_path fields.";
    result.fields["real_execution_toolchain_json"] =
        "[\"lan_agent_apply_diff_patch\",\"lan_agent_preview_patch\","
        "\"lan_agent_apply_single_file_patch\",\"lan_agent_verify_single_file_patch\","
        "\"lan_agent_write_text_file\",\"lan_agent_execute_semantic_action\","
        "\"lan_agent_configure_project\",\"lan_agent_build_target\","
        "\"lan_agent_run_ctest_target\",\"lan_agent_get_task\","
        "\"lan_agent_resolve_task_result\"]";
    const std::string rag_output_text = ExtractJsonString(response.body, "output_text").empty()
        ? (ExtractJsonString(response.body, "response").empty()
            ? ExtractJsonString(response.body, "content")
            : ExtractJsonString(response.body, "response"))
        : ExtractJsonString(response.body, "output_text");
    if (!rag_output_text.empty()) {
        result.fields["direct_answer"] =
            rag_output_text.substr(0, std::min<std::size_t>(rag_output_text.size(), 240));
        result.fields["summary"] =
            rag_output_text.substr(0, std::min<std::size_t>(rag_output_text.size(), 160));
    }
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }
    return result;
}
