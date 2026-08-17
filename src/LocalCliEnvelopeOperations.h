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
            "{\"command\":\"health\",\"reason\":\"missing local_cli command\"}");
    }
    if (command == "health") {
        return BuildLocalCliEnvelope(config, command, BuildLivenessResult(config), "null");
    }
    if (command == "chat-status") {
        CommandResult health = BuildHealthResult(config);
        CommandResult result;
        result.fields["local_chat_ready"] = GetFieldOrDefault(health, "local_chat_ready", "false");
        result.fields["local_chat_endpoint"] = GetFieldOrDefault(health, "local_chat_endpoint", "");
        result.fields["local_chat_detail"] = GetFieldOrDefault(health, "local_chat_detail", "");
        result.ok = result.fields["local_chat_ready"] == "true";
        result.exit_code = result.ok ? 0 : 50;
        if (!result.ok) {
            result.fields["error"] = "local chat is not ready";
        }
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"health\",\"reason\":\"chat status unavailable\"}");
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
        if (action_id == "check_remote_online") {
            return LocalCliResult(config, "health", "", "", "", "", "", "", "", "", false);
        }
        if (action_id == "check_local_chat") {
            return LocalCliResult(config, "chat-status", "", "", "", "", "", "", "", "", false);
        }
        if (action_id == "read_latest_log") {
            return LocalCliResult(config, "log-latest", "", "", "", "", "", "", "", "", false);
        }
        if (action_id == "get_git_diff") {
            return LocalCliResult(config, "diff", "", repo_root, "", "", "", "", "", "", false);
        }
        if (action_id == "read_test_result") {
            return LocalCliResult(config, "test-result", task_id, "", "", "", "", "", log_path, args_text, false);
        }
        result.ok = false;
        result.exit_code = 45;
        result.fields["error"] = "action_id is not in run-light allowlist";
        result.fields["action_id"] = action_id;
        result.fields["safe_action_allowlist"] =
            "check_remote_online,check_local_chat,read_latest_log,get_git_diff,read_test_result";
        return BuildLocalCliEnvelope(
            config,
            command,
            result,
            "{\"command\":\"health\",\"reason\":\"unsupported run-light action\"}");
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
        "Use lan_agent_write_text_file for generate/create/write/append text files; local_cli is only for whitelisted operations.";
    result.fields["supported_commands"] =
        "health,chat-status,task-latest,task,log-latest,diff,run-light,build-target,test-result,thread-report,mkdir";
    return BuildLocalCliEnvelope(
        config,
        command,
        result,
        "{\"command\":\"health\",\"reason\":\"unsupported command\"}");
}
