#pragma once

void AppendExperienceCard(
    const AgentConfig & config,
    const std::string & command,
    const CommandResult & result,
    const std::string & fallback) {
    std::filesystem::create_directories(config.log_root);
    std::ofstream output(BuildExperienceCardsPath(config), std::ios::out | std::ios::app);
    if (!output.is_open()) {
        return;
    }
    output
        << "{"
        << "\"task_fingerprint\":{\"command\":\"" << codex_lan_agent::JsonEscape(command) << "\"},"
        << "\"user_pattern\":[\"" << codex_lan_agent::JsonEscape(command) << "\"],"
        << "\"optimal_path\":[\"local_cli\",\"" << codex_lan_agent::JsonEscape(command) << "\"],"
        << "\"decision_rules\":[\"use local_cli before direct tool selection\"],"
        << "\"failure_patterns\":[\"" << codex_lan_agent::JsonEscape(fallback) << "\"],"
        << "\"compressed_prompt\":\"local_cli " << codex_lan_agent::JsonEscape(command) << "\","
        << "\"metrics\":{\"turns\":1,\"tool_calls\":1,\"success_rate\":" << (result.ok ? "1" : "0") << "}"
        << "}\n";
}

std::string BuildLocalCliTraceId() {
    static std::mutex mutex;
    static unsigned long long next_id = 1;
    std::lock_guard<std::mutex> lock(mutex);
    return "local_cli-" + TimeStampForFileName() + "-" + std::to_string(next_id++);
}

std::string QuoteProcessArgument(const std::string & value) {
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
}

std::filesystem::path BuildOptCmdExePath(const AgentConfig & config) {
    return std::filesystem::path(config.config_dir) / "optcmd.exe";
}

CommandResult OptCmdMkdirResult(
    const AgentConfig & config,
    const std::string & directory_path) {
    CommandResult result;
    result.fields["action"] = "optcmd_mkdir";
    result.fields["requested_path"] = directory_path;
    result.fields["execution_backend"] = "optcmd.exe";

    if (directory_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "args_text or directory_path is required";
        return result;
    }

    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, directory_path, &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        return result;
    }

    const std::filesystem::path optcmd_path = BuildOptCmdExePath(config);
    result.fields["normalized_path"] = normalized.string();
    result.fields["optcmd_path"] = optcmd_path.string();
    if (!std::filesystem::exists(optcmd_path)) {
        result.ok = false;
        result.exit_code = 22;
        result.fields["error"] = "optcmd.exe not found beside codex_lan_agent config";
        return result;
    }

    const std::string log_path = BuildLogPath(config, "optcmd_mkdir");
    const std::string command_line =
        QuoteProcessArgument(optcmd_path.string()) + " mkdir " + QuoteProcessArgument(normalized.string());
    codex_lan_agent::ProcessRunResult run_result;
    std::string run_error;
    if (!codex_lan_agent::RunCommandWithLog(
            command_line,
            config.config_dir,
            log_path,
            60,
            30,
            &run_result,
            &run_error)) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = run_error;
        result.fields["log_path"] = log_path;
        return result;
    }

    std::error_code exists_ec;
    const bool created_or_exists =
        std::filesystem::exists(normalized, exists_ec) &&
        std::filesystem::is_directory(normalized, exists_ec);
    result.ok = run_result.exit_code == 0 && created_or_exists;
    result.exit_code = result.ok ? 0 : (run_result.exit_code == 0 ? 24 : run_result.exit_code);
    result.fields["command_line"] = command_line;
    result.fields["log_path"] = log_path;
    result.fields["process_exit_code"] = std::to_string(run_result.exit_code);
    result.fields["process_id"] = std::to_string(run_result.process_id);
    result.fields["result"] = result.ok ? "created_or_exists" : "failed";
    if (!result.ok) {
        result.fields["error"] = created_or_exists
            ? "optcmd mkdir failed"
            : "directory was not created by optcmd";
    }
    return result;
}

CommandResult BuildLocalCliEnvelope(
    const AgentConfig & config,
    const std::string & command,
    const CommandResult & payload,
    const std::string & fallback) {
    CommandResult result;
    result.ok = payload.ok;
    result.exit_code = payload.exit_code;
    result.fields["command"] = command;
    result.fields["mcp_tool"] = "local_cli";
    result.fields["mapped_cli_command"] = command;
    result.fields["execution_path"] = "AI->MCP local_cli->codex_local_cli->codex-lan-agent";
    result.fields["trace_id"] = BuildLocalCliTraceId();
    result.fields["recorded_at"] = IsoTimestampNow();
    result.fields["trace_log_path"] = BuildRemoteControlEventsPath(config);
    result.fields["result"] = ResultToJson(payload);
    result.fields["evidence"] = BuildLocalCliEvidenceJson(payload);
    result.fields["fallback"] = fallback.empty() ? "null" : fallback;
    result.fields["experience_card_path"] = BuildExperienceCardsPath(config);
    if (!payload.ok) {
        result.fields["error"] = GetFieldOrDefault(payload, "error", "local_cli command failed");
    }
    AppendExperienceCard(config, command, result, result.fields["fallback"]);
    return result;
}

std::string BuildRunLightSafeActionAllowlist(const AgentConfig & config) {
    std::string allowlist =
        "check_remote_online,check_local_chat,read_latest_log,get_git_diff,read_test_result";
    for (const auto & entry : config.local_cli_run_light_profiles) {
        if (!entry.first.empty()) {
            allowlist += "," + entry.first;
        }
    }
    return allowlist;
}

std::string DefaultArgsForRunLightAction(const std::string & action_id) {
    if (action_id == "ui_screenshot") return "--action screenshot";
    if (action_id == "ui_cursor") return "--action cursor";
    if (action_id == "ui_move") return "--action move";
    if (action_id == "ui_click") return "--action click";
    if (action_id == "ui_key") return "--action key";
    if (action_id == "ui_key_press") return "--action key";
    if (action_id == "ui_type") return "--action type";
    if (action_id == "ui_hotkey") return "--action hotkey";
    if (action_id == "ui_activate_window") return "--action activate-window";
    if (action_id == "ui_get_focused_control") return "--action focused-control";
    if (action_id == "ui_analyze") return "--action analyze";
    if (action_id == "ui_screenshot_analyze") return "--action screenshot-analyze";
    return std::string();
}

std::string MergeRunLightArgs(const std::string & action_id, const std::string & args_text) {
    const std::string default_args = DefaultArgsForRunLightAction(action_id);
    if (default_args.empty()) {
        return args_text;
    }
    const std::string lower_args = ToLowerAscii(args_text);
    if (lower_args.find("--action") != std::string::npos || lower_args.find("-a ") != std::string::npos) {
        return args_text;
    }
    if (args_text.empty()) {
        return default_args;
    }
    return default_args + " " + args_text;
}

std::pair<std::string, std::string> NormalizeRunLightActionAndArgs(
    const AgentConfig & config,
    const std::string & action_id,
    const std::string & args_text) {
    if (!Trim(action_id).empty()) {
        return {action_id, args_text};
    }

    const std::string trimmed_args = Trim(args_text);
    if (trimmed_args.empty()) {
        return {action_id, args_text};
    }

    const std::size_t split_pos = trimmed_args.find_first_of(" \t\r\n");
    const std::string first_token = split_pos == std::string::npos
        ? trimmed_args
        : trimmed_args.substr(0, split_pos);
    const auto mapping_it = config.local_cli_run_light_profiles.find(first_token);
    if (mapping_it == config.local_cli_run_light_profiles.end() || mapping_it->second.empty()) {
        return {action_id, args_text};
    }

    const std::string remaining_args = split_pos == std::string::npos
        ? std::string()
        : Trim(trimmed_args.substr(split_pos + 1));
    return {first_token, remaining_args};
}

CommandResult BuildRunLightProfileResult(
    const AgentConfig & config,
    const std::string & action_id,
    const std::string & args_text,
    const std::string & log_path,
    bool dry_run) {
    CommandResult result;
    const auto mapping_it = config.local_cli_run_light_profiles.find(action_id);
    if (mapping_it == config.local_cli_run_light_profiles.end() || mapping_it->second.empty()) {
        result.ok = false;
        result.exit_code = 45;
        result.fields["error"] = "action_id is not in run-light allowlist";
        result.fields["action_id"] = action_id;
        result.fields["safe_action_allowlist"] = BuildRunLightSafeActionAllowlist(config);
        result.fields["small_model_hint"] = "Keep command=run-light and choose action_id from safe_action_allowlist; do not put an exe path, powershell, cmd, screenshot, or Headless in command.";
        result.fields["next_command"] = "run-light";
        result.fields["next_action_id"] = "ui_screenshot";
        result.fields["next_args_text"] = "";
        result.fields["next_dry_run"] = "true";
        return result;
    }

    const std::string profile = mapping_it->second;
    const std::string effective_args_text = MergeRunLightArgs(action_id, args_text);
    result.fields["action_id"] = action_id;
    result.fields["profile"] = profile;
    result.fields["args"] = effective_args_text;
    result.fields["raw_args"] = args_text;
    result.fields["execution_backend"] = "configured_run_light_profile";
    result.fields["config_key"] = "local_cli_run_light." + action_id;
    result.fields["safe_action_allowlist"] = BuildRunLightSafeActionAllowlist(config);

    if (config.profiles.find(profile) == config.profiles.end()) {
        result.ok = false;
        result.exit_code = 46;
        result.fields["error"] = "run-light action maps to unknown profile";
        result.fields["next_action"] = "configure profile." + profile + " or change local_cli_run_light." + action_id;
        return result;
    }

    if (dry_run) {
        result.ok = true;
        result.exit_code = 0;
        result.fields["status"] = "success";
        result.fields["result"] = "run_light_profile_dry_run_ready";
        result.fields["summary"] = "run-light action resolves to configured profile";
        result.fields["arbitrary_shell_allowed"] = "false";
        return result;
    }

    result = RunCliProfile(config, profile, effective_args_text, log_path, -1, -1);
    result.fields["action_id"] = action_id;
    result.fields["profile"] = profile;
    result.fields["args"] = effective_args_text;
    result.fields["raw_args"] = args_text;
    result.fields["execution_backend"] = "configured_run_light_profile";
    result.fields["config_key"] = "local_cli_run_light." + action_id;
    result.fields["safe_action_allowlist"] = BuildRunLightSafeActionAllowlist(config);
    return result;
}

bool IsCxvisionUiActionAllowed(const std::string & action) {
    return action == "list_windows" ||
           action == "screenshot" ||
           action == "analyze_image" ||
           action == "click" ||
           action == "type_text" ||
           action == "key";
}

std::string BuildCxvisionUiDefaultOutputPath(
    const AgentConfig & config,
    const std::string & action,
    const std::string & extension) {
    const std::filesystem::path output_dir =
        std::filesystem::path(config.log_root) / "cxvision_ui";
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    return (output_dir / (action + "_" + TimeStampForFileName() + extension)).string();
}

void AppendCxvisionFlag(
    std::ostringstream * args,
    const std::string & name,
    const std::string & value) {
    if (args == nullptr || value.empty()) {
        return;
    }
    *args << " --" << name << " " << QuoteProcessArgument(value);
}

CommandResult BuildCxvisionUiResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    const std::string profile = params.GetString("profile").empty()
        ? "cxvision_imgui_acceptance"
        : params.GetString("profile");
    const std::string action = ToLowerAscii(params.GetString("action"));
    const bool dry_run = params.GetBool("dry_run", false);

    CommandResult result;
    result.fields["tool"] = "lan_agent_cxvision_ui";
    result.fields["profile"] = profile;
    result.fields["action"] = action;
    result.fields["dry_run"] = dry_run ? "true" : "false";
    result.fields["execution_contract"] = "configured_profile_only";
    result.fields["arbitrary_shell_allowed"] = "false";
    result.fields["allowed_actions"] = "list_windows,screenshot,analyze_image,click,type_text,key";
    result.fields["shared_log_path"] =
        "D:/Codex-WorkDir/Sean_WorkDir/cxvisionai/cxscript_runs/_shared/cxvision_imgui_acceptance.jsonl";

    if (!IsCxvisionUiActionAllowed(action)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "unsupported cxvision ui action";
        result.fields["next_action"] = "use one of allowed_actions";
        return result;
    }
    if (config.profiles.find(profile) == config.profiles.end()) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["error"] = "cxvision ui profile is not configured";
        result.fields["next_action"] = "configure profile." + profile + " and restart codex_lan_agent serve";
        return result;
    }

    std::string output_path = params.GetString("output_path");
    if (output_path.empty() && action == "screenshot") {
        output_path = BuildCxvisionUiDefaultOutputPath(config, action, ".png");
    }
    if (output_path.empty() && (action == "list_windows" || action == "analyze_image")) {
        output_path = BuildCxvisionUiDefaultOutputPath(config, action, ".json");
    }

    std::ostringstream args;
    args << "--ui-action " << action;
    AppendCxvisionFlag(&args, "window-title", params.GetString("window_title"));
    AppendCxvisionFlag(&args, "window-class", params.GetString("window_class"));
    AppendCxvisionFlag(&args, "image", params.GetString("image_path"));
    AppendCxvisionFlag(&args, "text", params.GetString("text"));
    AppendCxvisionFlag(&args, "key", params.GetString("key"));
    AppendCxvisionFlag(&args, "button", params.GetString("button"));
    if (!output_path.empty()) {
        AppendCxvisionFlag(&args, "out", output_path);
    }
    if (!params.GetRawJson("x").empty()) {
        args << " --x " << params.GetInt("x", 0);
    }
    if (!params.GetRawJson("y").empty()) {
        args << " --y " << params.GetInt("y", 0);
    }
    if (!params.GetRawJson("width").empty()) {
        args << " --width " << params.GetInt("width", 0);
    }
    if (!params.GetRawJson("height").empty()) {
        args << " --height " << params.GetInt("height", 0);
    }
    const std::string extra_args = params.GetString("args").empty()
        ? params.GetString("arguments_text")
        : params.GetString("args");
    if (!extra_args.empty()) {
        args << " " << extra_args;
    }

    result.fields["args"] = args.str();
    result.fields["output_path"] = output_path;
    result.fields["artifact_path"] = output_path;
    if (action == "screenshot") {
        result.fields["image_path"] = output_path;
        result.fields["image_mime"] = "image/png";
    }

    if (dry_run) {
        result.ok = true;
        result.exit_code = 0;
        result.fields["status"] = "success";
        result.fields["result"] = "cxvision_ui_dry_run_ready";
        result.fields["summary"] = "cxvision ui action resolves to configured profile";
        return result;
    }

    result = RunCliProfile(config, profile, args.str(), std::string(), -1, -1);
    result.fields["tool"] = "lan_agent_cxvision_ui";
    result.fields["profile"] = profile;
    result.fields["action"] = action;
    result.fields["args"] = args.str();
    result.fields["output_path"] = output_path;
    result.fields["artifact_path"] = output_path;
    result.fields["shared_log_path"] =
        "D:/Codex-WorkDir/Sean_WorkDir/cxvisionai/cxscript_runs/_shared/cxvision_imgui_acceptance.jsonl";
    if (action == "screenshot") {
        result.fields["image_path"] = output_path;
        result.fields["image_mime"] = "image/png";
    }
    return result;
}

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
    bool dry_run) {
    if (Trim(command).empty()) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "local_cli command is required";
        result.fields["supported_commands"] =
            "health,chat-status,task-latest,task,log-latest,diff,run-light,build-target,test-result,thread-report,mkdir";
        result.fields["next_action"] =
            "provide command from supported_commands, or use a specialized lan_agent_* tool";
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "null");
    }
    if (command == "health") {
        return BuildLocalCliEnvelope(config, command, BuildLivenessResult(config), "null");
    }
    if (command == "chat-status") {
        CommandResult health = BuildHealthResult(config);
        CommandResult probe = RunLocalChat(
            config,
            "local_chat_health_probe",
            "Reply with exactly: ok",
            "health_probe",
            3000,
            nullptr);

        CommandResult result;
        result.fields["local_chat_tcp_ready"] = GetFieldOrDefault(health, "local_chat_ready", "false");
        result.fields["local_chat_ready"] = probe.ok ? "true" : "false";
        result.fields["local_chat_completion_ready"] = probe.ok ? "true" : "false";
        result.fields["local_chat_ready_semantics"] = "chat_completion_probe";
        result.fields["local_chat_endpoint"] = GetFieldOrDefault(health, "local_chat_endpoint", "");
        result.fields["local_chat_endpoint_effective"] = GetFieldOrDefault(probe, "local_chat_endpoint_effective", GetFieldOrDefault(health, "local_chat_endpoint_effective", ""));
        result.fields["local_chat_endpoint_source"] = GetFieldOrDefault(probe, "local_chat_endpoint_source", GetFieldOrDefault(health, "local_chat_endpoint_source", ""));
        result.fields["local_chat_detail"] = GetFieldOrDefault(health, "local_chat_detail", "");
        result.fields["local_chat_probe_status_code"] = GetFieldOrDefault(probe, "status_code", "0");
        result.fields["local_chat_probe_log_path"] = GetFieldOrDefault(probe, "log_path", "");
        result.fields["local_chat_probe_body_ref"] = GetFieldOrDefault(probe, "body_ref", "");
        result.fields["local_chat_probe_error"] = GetFieldOrDefault(probe, "error", "");
        result.fields["local_chat_probe_timeout_ms"] = "3000";
        result.ok = probe.ok;
        result.exit_code = result.ok ? 0 : 50;
        if (!result.ok) {
            result.fields["error"] = "local chat completion probe failed";
        }
        return BuildLocalCliEnvelope(config, command, result, "null");
    }
    if (command == "task-latest") {
        CommandResult result = g_task_manager == nullptr
            ? CommandResult()
            : g_task_manager->GetLatestTaskResult();
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
        }
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"log-latest\",\"reason\":\"no task available\"}");
    }
    if (command == "task") {
        CommandResult result = g_task_manager == nullptr
            ? CommandResult()
            : g_task_manager->GetTaskResult(task_id);
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
        }
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"task-latest\",\"reason\":\"task_id unavailable or not found\"}");
    }
    if (command == "log-latest") {
        return BuildLocalCliEnvelope(
            config,
            command,
            DiscoverLogsResult(config, 10, 80),
            "{\"command\":\"health\",\"reason\":\"log discovery unavailable\"}");
    }
    if (command == "diff") {
        return BuildLocalCliEnvelope(
            config,
            command,
            SnapshotDiffResult(config, repo_root),
            "{\"command\":\"log-latest\",\"reason\":\"git diff unavailable\"}");
    }
    if (command == "test-result") {
        return BuildLocalCliEnvelope(
            config,
            command,
            RagLogClassifyResult(config, log_path, task_id, args_text),
            "{\"command\":\"log-latest\",\"reason\":\"test log unavailable\"}");
    }
    if (command == "thread-report") {
        CommandResult result = BuildRuntimeOverviewResult(config);
        CommandResult events = TailTextFileResult(config, BuildRemoteControlEventsPath(config), 10);
        result.fields["module"] = "intranet_migration";
        result.fields["remote_entry"] = config.listen_host + ":" + std::to_string(config.listen_port);
        result.fields["action"] = "thread_report";
        result.fields["result"] = ComputeCommandOutcome(result);
        result.fields["next_action"] = "continue through semantic_action_prepare or local_cli";
        result.fields["latest_events"] = GetFieldOrDefault(events, "content", "");
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"health\",\"reason\":\"thread report unavailable\"}");
    }
    if (command == "mkdir") {
        return BuildLocalCliEnvelope(
            config,
            command,
            OptCmdMkdirResult(config, args_text),
            "{\"command\":\"lan_agent_list_directory\",\"reason\":\"verify mkdir target after optcmd execution\"}");
    }
    if (command == "build-target") {
        CommandResult result;
        std::string resolved_config = config_name.empty() ? "Release" : config_name;
        if (build_dir.empty() || target.empty()) {
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "build_dir and target are required";
            result.fields["missing_args"] = build_dir.empty() ? "build_dir" : "target";
        } else if (dry_run) {
            result = BuildTargetDryRunResult(build_dir, target, resolved_config);
        } else if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
        } else {
            const std::string queued_task_id = g_task_manager->EnqueueCliProfile(
                "build_target",
                "-BuildDir \"" + build_dir + "\" -Config " + resolved_config + " -Target " + target);
            result = BuildQueuedTaskResult(queued_task_id);
        }
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"task-latest\",\"reason\":\"build queue unavailable\"}");
    }
    if (command == "run-light") {
        CommandResult result;
        const auto normalized_run_light = NormalizeRunLightActionAndArgs(config, action_id, args_text);
        const std::string normalized_action_id = normalized_run_light.first;
        const std::string normalized_args_text = normalized_run_light.second;
        if (normalized_action_id == "check_remote_online") {
            return LocalCliResult(config, "health", "", "", "", "", "", "", "", "", false);
        }
        if (normalized_action_id == "check_local_chat") {
            return LocalCliResult(config, "chat-status", "", "", "", "", "", "", "", "", false);
        }
        if (normalized_action_id == "read_latest_log") {
            return LocalCliResult(config, "log-latest", "", "", "", "", "", "", "", "", false);
        }
        if (normalized_action_id == "get_git_diff") {
            return LocalCliResult(config, "diff", "", repo_root, "", "", "", "", "", "", false);
        }
        if (normalized_action_id == "read_test_result") {
            return LocalCliResult(config, "test-result", task_id, "", "", "", "", "", log_path, normalized_args_text, false);
        }
        result = BuildRunLightProfileResult(config, normalized_action_id, normalized_args_text, log_path, dry_run);
        result.fields["requested_action_id"] = action_id;
        result.fields["requested_args_text"] = args_text;
        result.fields["run_light_input_compat"] = action_id.empty() && normalized_action_id != action_id ? "args_text_first_token" : "action_id_field";
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "null");
    }

    const std::string lower_command = ToLowerAscii(command);
    if (lower_command.rfind("echo", 0) == 0 || command.find(">>") != std::string::npos || command.find(">") != std::string::npos) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 49;
        result.fields["error"] = "unsupported local_cli shell redirection";
        result.fields["unsupported_pattern"] = "echo_or_redirection";
        result.fields["recommended_tool"] = "lan_agent_write_text_file";
        result.fields["tool_selection_rule"] =
            "For generate/create/write/append file content, call lan_agent_write_text_file with file_path, content, and append instead of local_cli echo.";
        result.fields["supported_commands"] =
            "health,chat-status,task-latest,task,log-latest,diff,run-light,build-target,test-result,thread-report,mkdir";
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"tool\":\"lan_agent_write_text_file\",\"reason\":\"local_cli does not execute shell redirection\"}");
    }

    CommandResult result;
    result.ok = false;
    result.exit_code = 49;
    result.fields["error"] = "unsupported local_cli command";
    result.fields["recommended_tool_for_file_write"] = "lan_agent_write_text_file";
    result.fields["tool_selection_rule"] =
        "Use lan_agent_write_text_file for text file writes. For desktop screenshot/mouse/image analysis, use command=run-light with action_id, not a shell/exe command.";
    result.fields["supported_commands"] =
        "health,chat-status,task-latest,task,log-latest,diff,run-light,build-target,test-result,thread-report,mkdir";
    result.fields["desktop_ui_actions_csv"] = "ui_screenshot,ui_cursor,ui_move,ui_click,ui_key,ui_key_press,ui_type,ui_hotkey,ui_activate_window,ui_get_focused_control,ui_analyze,ui_screenshot_analyze";
    result.fields["small_model_hint"] = "If the user asks for screenshot/click/desktop image analysis, retry with command=run-light and action_id=ui_screenshot or another desktop_ui_actions_csv item.";
    result.fields["next_command"] = "run-light";
    result.fields["next_action_id"] = "ui_screenshot";
    result.fields["next_args_text"] = "";
    result.fields["next_dry_run"] = "true";
    return BuildLocalCliEnvelope(
        config,
        command,
        result,
        "{\"command\":\"health\",\"reason\":\"unsupported command\"}");
}