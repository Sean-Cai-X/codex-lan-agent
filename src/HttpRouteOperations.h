#pragma once

CommandResult BuildCompatToolRouteResult(
    const AgentConfig & config,
    const std::string & tool_name,
    const std::string & params_body) {
    const JsonRequestView params(params_body);
    CommandResult result;

    if (MaybeApplyClipsPreflightBlock(config, tool_name, params, &result)) {
        ApplyRequestRuleFields(tool_name, params, &result);
        LanResultBuilder(&result).Finalize(config, tool_name);
        ApplySupervisionEnvelope(&result);
        AppendMcpTraceAuditEvent(config, tool_name, result);
        AppendMcpSupervisionAlarmEvent(config, tool_name, result);
        return result;
    }

    if (TryHandleRegisteredMcpTool(config, tool_name, params, &result)) {
        ApplyRequestRuleFields(tool_name, params, &result);
        LanResultBuilder(&result).Finalize(config, tool_name);
        ApplyAiConclusionValidityGuards(&result);
        ApplyClipsResultGuard(config, tool_name, &result);
        ApplyClipsSemanticTraceContinuation(config, &result);
        ApplySupervisionEnvelope(&result);
        AppendMcpTraceAuditEvent(config, tool_name, result);
        AppendMcpSupervisionAlarmEvent(config, tool_name, result);
        return result;
    }

    if (tool_name == "lan_agent_list_cxparser_flows") {
        result = BuildCxParserFlowCatalogResult(&config);
    } else if (tool_name == "lan_agent_validate_cxparser_flow") {
        result = ValidateCxParserFlowResult(
            &config,
            ExtractJsonString(params_body, "flow_id"),
            ExtractJsonString(params_body, "params_json"));
    } else if (tool_name == "lan_agent_run_cxparser_flow") {
        result = RunCxParserFlowResult(
            config,
            ExtractJsonString(params_body, "flow_id"),
            params_body,
            ResolveEffectiveTraceIdForToolCall(tool_name, params_body),
            ExtractJsonString(params_body, "goal_id"));
    } else if (tool_name == "lan_agent_read_text_file"
               || tool_name == "lan_agent_list_directory"
               || tool_name == "lan_agent_read_directory_files") {
        result = ExecuteReadOnlyMcpToolForClipsContinuation(config, tool_name, params_body);
    } else {
        result.ok = false;
        result.exit_code = 404;
        result.fields["error"] = "tool not found";
        result.fields["tool_name"] = tool_name;
        return result;
    }

    ApplyRequestRuleFields(tool_name, params, &result);
    LanResultBuilder(&result).Finalize(config, tool_name);
    ApplyAiConclusionValidityGuards(&result);
    ApplyClipsResultGuard(config, tool_name, &result);
    ApplyClipsSemanticTraceContinuation(config, &result);
    ApplySupervisionEnvelope(&result);
    AppendMcpTraceAuditEvent(config, tool_name, result);
    AppendMcpSupervisionAlarmEvent(config, tool_name, result);
    return result;
}

CommandResult HandleHttpRoute(
    const AgentConfig & config,
    const HttpRequest & request) {
    if (request.method == "POST" && request.path == "/tools") {
        const std::string tool_name = FirstNonEmpty(
            ExtractJsonString(request.body, "tool"),
            ExtractJsonString(request.body, "name"));
        const std::string params_body = FirstNonEmpty(
            ExtractJsonObjectRaw(request.body, "arguments"),
            ExtractJsonObjectRaw(request.body, "params"),
            request.body);
        return BuildCompatToolRouteResult(config, tool_name, params_body);
    }
    if (request.method == "GET" && request.path == "/health") {
        return BuildHealthResult(config);
    }
    if (request.method == "GET" && request.path == "/runtime-overview") {
        return BuildRuntimeOverviewResult(config);
    }
    if (request.method == "GET" && request.path == "/overview/remote-sessions") {
        return BuildRemoteSessionOverviewResult(config);
    }
    if (request.method == "GET" && request.path == "/overview/tasks") {
        int max_entries = 20;
        const std::string max_entries_raw = GetQueryParamValue(request, "max_entries");
        if (!max_entries_raw.empty()) {
            const int parsed_max_entries = std::atoi(max_entries_raw.c_str());
            max_entries = parsed_max_entries > 0 ? parsed_max_entries : 1;
        }
        return BuildTaskOverviewResult(config, max_entries);
    }
    if (request.method == "GET" && request.path == "/overview/events") {
        int max_entries = 10;
        int offset = 0;
        const std::string max_entries_raw = GetQueryParamValue(request, "max_entries");
        if (!max_entries_raw.empty()) {
            const int parsed_max_entries = std::atoi(max_entries_raw.c_str());
            max_entries = parsed_max_entries > 0 ? parsed_max_entries : 1;
        }
        const std::string offset_raw = GetQueryParamValue(request, "offset");
        if (!offset_raw.empty()) {
            offset = std::max(0, std::atoi(offset_raw.c_str()));
        }
        return BuildEventOverviewResult(
            config,
            max_entries,
            offset,
            GetQueryParamValue(request, "include_auto") == "true",
            GetQueryParamValue(request, "include_noise") == "true",
            GetQueryParamValue(request, "command_name"),
            GetQueryParamValue(request, "session_id"),
            GetQueryParamValue(request, "task_id"),
            GetQueryParamValue(request, "since_timestamp"));
    }
    if (request.method == "GET" && request.path == "/overview/patches") {
        int max_entries = 20;
        int offset = 0;
        const std::string max_entries_raw = GetQueryParamValue(request, "max_entries");
        if (!max_entries_raw.empty()) {
            const int parsed_max_entries = std::atoi(max_entries_raw.c_str());
            max_entries = parsed_max_entries > 0 ? parsed_max_entries : 1;
        }
        const std::string offset_raw = GetQueryParamValue(request, "offset");
        if (!offset_raw.empty()) {
            offset = std::max(0, std::atoi(offset_raw.c_str()));
        }
        return BuildPatchOverviewResult(
            config,
            max_entries,
            offset,
            GetQueryParamValue(request, "patch_id"),
            GetQueryParamValue(request, "trace_id"),
            GetQueryParamValue(request, "file_path"));
    }
    if (request.method == "GET" && request.path == "/overview/mcp") {
        return BuildMcpOverviewResult(config);
    }
    if (request.method == "GET" && request.path == "/overview/rag") {
        return BuildRagOverviewResult(config);
    }
    if (request.method == "GET" &&
        (request.path == "/overview/browser-list" || request.path == "/browser-list-overview")) {
        int task_max_entries = 20;
        int event_max_entries = 10;
        int patch_max_entries = 20;
        const std::string task_max_entries_raw = GetQueryParamValue(request, "task_max_entries");
        const std::string event_max_entries_raw = GetQueryParamValue(request, "event_max_entries");
        const std::string patch_max_entries_raw = GetQueryParamValue(request, "patch_max_entries");
        if (!task_max_entries_raw.empty()) {
            const int parsed_task_max_entries = std::atoi(task_max_entries_raw.c_str());
            task_max_entries = parsed_task_max_entries > 0 ? parsed_task_max_entries : 1;
        }
        if (!event_max_entries_raw.empty()) {
            const int parsed_event_max_entries = std::atoi(event_max_entries_raw.c_str());
            event_max_entries = parsed_event_max_entries > 0 ? parsed_event_max_entries : 1;
        }
        if (!patch_max_entries_raw.empty()) {
            const int parsed_patch_max_entries = std::atoi(patch_max_entries_raw.c_str());
            patch_max_entries = parsed_patch_max_entries > 0 ? parsed_patch_max_entries : 1;
        }
        return BuildBrowserListOverviewResult(config, task_max_entries, event_max_entries, patch_max_entries);
    }
    if (request.method == "GET" && request.path == "/rag/index/status") {
        CommandResult result = BuildRagIndexStatusResult(config);
        LanResultBuilder(&result).Finalize(config, "lan_agent_rag_index_status");
        return result;
    }
    if (request.method == "GET" && request.path == "/healthz") {
        return BuildLivenessResult(config);
    }
    if (request.method == "GET" && request.path == "/profiles") {
        return BuildProfileListResult(config);
    }
    if (request.method == "GET" &&
        (request.path == "/semantic-action-map" ||
         request.path == "/tool-shortcuts" ||
         request.path == "/mcp-actions")) {
        return BuildSemanticActionMapResult(GetQueryParamValue(request, "action_id"));
    }
    if (request.method == "GET" && request.path == "/semantic-action-resolve") {
        return BuildSemanticActionResolveResult(
            GetQueryParamValue(request, "action_id"),
            GetQueryParamValue(request, "query"));
    }
    if (request.method == "GET" && request.path == "/semantic-action-validate") {
        return BuildSemanticActionValidateResult(
            GetQueryParamValue(request, "action_id"),
            GetQueryParamValue(request, "arguments_text"));
    }
    if (request.method == "GET" && request.path == "/semantic-action-prepare") {
        return BuildSemanticActionPrepareResult(
            GetQueryParamValue(request, "action_id"),
            GetQueryParamValue(request, "query"),
            GetQueryParamValue(request, "arguments_text"));
    }
    if (request.method == "GET" && request.path == "/semantic-action-tool-call") {
        return BuildSemanticActionToolCallResult(
            GetQueryParamValue(request, "action_id"),
            GetQueryParamValue(request, "query"),
            GetQueryParamValue(request, "arguments_text"),
            GetQueryParamValue(request, "prefer_dry_run") == "true");
    }
    if (request.method == "GET" && request.path == "/router-domain-map") {
        return BuildRouterDomainMapResult(GetQueryParamValue(request, "domain"));
    }
    if (request.method == "GET" && request.path == "/mcp-capability-registry") {
        return BuildMcpCapabilityRegistryResult(GetQueryParamValue(request, "capability_id"));
    }
    if (request.method == "GET" && request.path == "/rag-memory-slice-contract") {
        return BuildRagMemorySliceContractResult(GetQueryParamValue(request, "field_group"));
    }
    if (request.method == "GET" && request.path == "/check-build-dir") {
        const std::string build_dir = GetQueryParamValue(request, "build_dir");
        if (build_dir.empty()) {
            CommandResult result;
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "build_dir is required";
            return result;
        }
        return RunCliProfile(config, "check_build_dir", BuildCheckBuildDirArguments(build_dir));
    }
    if (request.method == "POST" && request.path == "/prepare-build-dir") {
        const std::string build_dir = ExtractJsonString(request.body, "build_dir");
        const bool create_if_missing = ExtractJsonBool(request.body, "create_if_missing", false);
        if (build_dir.empty()) {
            CommandResult result;
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "build_dir is required";
            return result;
        }
        return RunCliProfile(
            config,
            "prepare_build_dir",
            BuildPrepareBuildDirArguments(build_dir, config.workspace_root, create_if_missing));
    }
    if (request.method == "POST" && request.path == "/build-target") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        const std::string build_dir = ExtractJsonString(request.body, "build_dir");
        const std::string target = ExtractJsonString(request.body, "target");
        std::string config_name = ExtractJsonString(request.body, "config");
        const bool dry_run = ExtractJsonBool(request.body, "dry_run", false);
        const bool validate_args = ExtractJsonBool(request.body, "validate_args", false);
        const std::string stall_timeout_raw = ExtractJsonRawValue(request.body, "stall_timeout_sec");
        const int stall_timeout_sec = stall_timeout_raw.empty() ? -1 : std::atoi(stall_timeout_raw.c_str());
        if (config_name.empty()) {
            config_name = "Release";
        }
        if (build_dir.empty() || target.empty()) {
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "build_dir and target are required";
            return result;
        }
        if (dry_run || validate_args) {
            return BuildTargetDryRunResult(build_dir, target, config_name);
        }
        const std::string task_id = g_task_manager->EnqueueCliProfile(
            "build_target",
            BuildBuildTargetArguments(build_dir, config_name, target),
            -1,
            stall_timeout_sec >= 0 ? stall_timeout_sec : config.build_target_stall_timeout_sec);
        result = BuildQueuedTaskResult(task_id);
        result.fields["build_target_stall_timeout_sec"] =
            std::to_string(stall_timeout_sec >= 0 ? stall_timeout_sec : config.build_target_stall_timeout_sec);
        result.fields["build_target_stall_timeout_source"] =
            stall_timeout_sec >= 0 ? "request" : "config";
        return result;
    }
    if (request.method == "POST" && request.path == "/configure-project") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        const std::string project_root = ExtractJsonString(request.body, "project_root");
        const std::string build_dir = ExtractJsonString(request.body, "build_dir");
        std::string generator_kind = ExtractJsonString(request.body, "generator_kind");
        const std::string stall_timeout_raw = ExtractJsonRawValue(request.body, "stall_timeout_sec");
        const int stall_timeout_sec = stall_timeout_raw.empty()
            ? config.configure_project_stall_timeout_sec
            : std::atoi(stall_timeout_raw.c_str());
        std::string cmake_args;
        std::string cmake_args_list_raw;
        const std::vector<std::string> cmake_arg_values = CollectConfigureProjectCmakeArgs(
            request.body,
            &cmake_args,
            &cmake_args_list_raw);
        const std::string env_args = ExtractJsonString(request.body, "env");
        if (generator_kind.empty()) {
            generator_kind = "vs2022";
        }
        if (project_root.empty() || build_dir.empty()) {
            result.ok = false;
            result.exit_code = 400;
            result.fields["error"] = "project_root and build_dir are required";
            return result;
        }
        const std::string task_id = g_task_manager->EnqueueCliProfile(
            "configure_project",
            BuildConfigureProjectArguments(
                project_root,
                build_dir,
                generator_kind,
                cmake_arg_values,
                env_args),
            -1,
            std::max(0, stall_timeout_sec));
        result = BuildQueuedTaskResult(task_id);
        result.fields["generator_kind"] = generator_kind;
        result.fields["configure_project_stall_timeout_sec"] = std::to_string(std::max(0, stall_timeout_sec));
        result.fields["configure_project_stall_timeout_source"] = stall_timeout_raw.empty() ? "config" : "request";
        if (!cmake_arg_values.empty()) {
            result.fields["cmake_args"] = JoinConfigureProjectCmakeArgs(cmake_arg_values);
            result.fields["cmake_arg_count"] = std::to_string(cmake_arg_values.size());
        } else if (!cmake_args.empty()) {
            result.fields["cmake_args"] = cmake_args;
        }
        if (!cmake_args_list_raw.empty()) {
            result.fields["cmake_args_list"] = cmake_args_list_raw;
        }
        if (!env_args.empty()) {
            result.fields["env"] = env_args;
        }
        return result;
    }
    if (request.method == "POST" && request.path == "/run-ctest-target") {
        CommandResult result;
        const JsonRequestView params(request.body);
        TryHandleRegisteredMcpTool(config, "lan_agent_run_ctest_target", params, &result);
        ApplyRequestRuleFields("lan_agent_run_ctest_target", params, &result);
        LanResultBuilder(&result).Finalize(config, "lan_agent_run_ctest_target");
        return result;
    }
    if (request.method == "POST" && request.path == "/ctest/discover") {
        CommandResult result;
        const JsonRequestView params(request.body);
        TryHandleRegisteredMcpTool(config, "lan_agent_discover_ctest_tests", params, &result);
        ApplyRequestRuleFields("lan_agent_discover_ctest_tests", params, &result);
        LanResultBuilder(&result).Finalize(config, "lan_agent_discover_ctest_tests");
        return result;
    }
    if (request.method == "POST" && request.path == "/clips/decide") {
        CommandResult result;
        const JsonRequestView params(request.body);
        TryHandleRegisteredMcpTool(config, "lan_agent_clips_decide", params, &result);
        ApplyRequestRuleFields("lan_agent_clips_decide", params, &result);
        LanResultBuilder(&result).Finalize(config, "lan_agent_clips_decide");
        return result;
    }
    if (request.method == "POST" && request.path == "/rag/clips/meta") {
        CommandResult result;
        const JsonRequestView params(request.body);
        TryHandleRegisteredMcpTool(config, "lan_agent_rag_clips_meta", params, &result);
        ApplyRequestRuleFields("lan_agent_rag_clips_meta", params, &result);
        LanResultBuilder(&result).Finalize(config, "lan_agent_rag_clips_meta");
        return result;
    }
    if (request.method == "POST" && request.path == "/rag/clips/run") {
        CommandResult result;
        const JsonRequestView params(request.body);
        TryHandleRegisteredMcpTool(config, "lan_agent_rag_clips_run", params, &result);
        ApplyRequestRuleFields("lan_agent_rag_clips_run", params, &result);
        LanResultBuilder(&result).Finalize(config, "lan_agent_rag_clips_run");
        return result;
    }
    if (request.method == "POST" && request.path == "/rag/storage/lookup") {
        CommandResult result;
        const JsonRequestView params(request.body);
        TryHandleRegisteredMcpTool(config, "lan_agent_rag_storage_lookup", params, &result);
        ApplyRequestRuleFields("lan_agent_rag_storage_lookup", params, &result);
        LanResultBuilder(&result).Finalize(config, "lan_agent_rag_storage_lookup");
        return result;
    }
    if (request.method == "POST" && request.path == "/rag/review/observe") {
        CommandResult result;
        const JsonRequestView params(request.body);
        TryHandleRegisteredMcpTool(config, "lan_agent_rag_review_observe", params, &result);
        ApplyRequestRuleFields("lan_agent_rag_review_observe", params, &result);
        LanResultBuilder(&result).Finalize(config, "lan_agent_rag_review_observe");
        return result;
    }
    if (request.method == "POST" && request.path == "/rag/storage/page") {
        CommandResult result;
        const JsonRequestView params(request.body);
        TryHandleRegisteredMcpTool(config, "lan_agent_rag_storage_page", params, &result);
        ApplyRequestRuleFields("lan_agent_rag_storage_page", params, &result);
        LanResultBuilder(&result).Finalize(config, "lan_agent_rag_storage_page");
        return result;
    }
    if (request.method == "POST" && request.path == "/run-cli-profile") {
        const std::string profile = ExtractJsonString(request.body, "profile");
        const std::string args = ExtractJsonString(request.body, "args");
        const std::string timeout_raw = ExtractJsonRawValue(request.body, "timeout_sec");
        const std::string stall_timeout_raw = ExtractJsonRawValue(request.body, "stall_timeout_sec");
        return RunCliProfile(
            config,
            profile,
            args,
            std::string(),
            timeout_raw.empty() ? -1 : std::atoi(timeout_raw.c_str()),
            stall_timeout_raw.empty() ? -1 : std::atoi(stall_timeout_raw.c_str()));
    }
    if (request.method == "POST" && request.path == "/enqueue-cli-profile") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        const std::string timeout_raw = ExtractJsonRawValue(request.body, "timeout_sec");
        const std::string stall_timeout_raw = ExtractJsonRawValue(request.body, "stall_timeout_sec");
        result.fields["task_id"] = g_task_manager->EnqueueCliProfile(
            ExtractJsonString(request.body, "profile"),
            ExtractJsonString(request.body, "args"),
            timeout_raw.empty() ? -1 : std::atoi(timeout_raw.c_str()),
            stall_timeout_raw.empty() ? -1 : std::atoi(stall_timeout_raw.c_str()));
        result.fields["status"] = "queued";
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
        return result;
    }
    if (request.method == "POST" && request.path == "/run-case") {
        return RunCase(config, ExtractJsonString(request.body, "case_path"));
    }
    if (request.method == "POST" && request.path == "/enqueue-case") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        result.fields["task_id"] = g_task_manager->EnqueueCase(
            ExtractJsonString(request.body, "case_path"));
        result.fields["status"] = "queued";
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
        return result;
    }
    if (request.method == "POST" && request.path == "/run-rag-flow") {
        const std::string query = ExtractJsonString(request.body, "query");
        std::string mode = ExtractJsonString(request.body, "mode");
        if (mode.empty()) {
            mode = "review";
        }
        return RunRagFlow(config, query, mode);
    }
    if (request.method == "POST" && request.path == "/enqueue-rag-flow") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        result.fields["task_id"] = g_task_manager->EnqueueRagFlow(
            ExtractJsonString(request.body, "query"),
            ExtractJsonString(request.body, "mode"));
        result.fields["status"] = "queued";
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
        return result;
    }
    if (request.method == "POST" && request.path == "/run-local-chat") {
        std::string mode = ExtractJsonString(request.body, "mode");
        if (mode.empty()) {
            mode = "code_analysis";
        }
        const LocalChatEvidencePacket evidence = ExtractLocalChatEvidencePacket(request.body);
        return RunLocalChat(
            config,
            ExtractJsonString(request.body, "scope"),
            ExtractJsonString(request.body, "question"),
            mode,
            30000,
            &evidence);
    }
    if (request.method == "POST" && request.path == "/enqueue-local-chat") {
        CommandResult result;
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["error"] = "task manager is not active";
            return result;
        }
        result.fields["task_id"] = g_task_manager->EnqueueLocalChat(
            ExtractJsonString(request.body, "scope"),
            ExtractJsonString(request.body, "question"),
            ExtractJsonString(request.body, "mode"));
        result.fields["status"] = "queued";
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
        result.fields["analysis_only"] = "true";
        result.fields["execution_capability"] = "false";
        result.fields["evidence_injection_template"] =
            "task_id,result_ref,evidence_ref,resolved_log_path,log_excerpt,source_excerpt";
        result.fields["evidence_injection_used"] = "false";
        result.fields["evidence_injection_queue_policy"] =
            "queued local chat preserves analysis-only mode; use synchronous lan_agent_run_local_chat for evidence injection";
        return result;
    }
    if (request.method == "POST" && request.path == "/remote-session/new-turn") {
        std::string context_refs = ExtractJsonString(request.body, "context_refs");
        if (context_refs.empty()) {
            context_refs = ExtractJsonRawValue(request.body, "context_refs");
        }
        return RunRemoteSessionNewTurn(
            config,
            ExtractJsonString(request.body, "task_id"),
            std::string(),
            ExtractJsonString(request.body, "speaker_mode"),
            ExtractJsonString(request.body, "reasoning_level"),
            ExtractJsonString(request.body, "prompt_purpose"),
            context_refs,
            ExtractJsonString(request.body, "response_mode"),
            ExtractJsonString(request.body, "prompt_text"));
    }
    if (request.method == "POST" && request.path == "/remote-session/append-turn") {
        std::string context_refs = ExtractJsonString(request.body, "context_refs");
        if (context_refs.empty()) {
            context_refs = ExtractJsonRawValue(request.body, "context_refs");
        }
        return RunRemoteSessionAppendTurn(
            config,
            ExtractJsonString(request.body, "task_id"),
            ExtractJsonString(request.body, "session_id"),
            ExtractJsonString(request.body, "speaker_mode"),
            ExtractJsonString(request.body, "reasoning_level"),
            ExtractJsonString(request.body, "prompt_purpose"),
            context_refs,
            ExtractJsonString(request.body, "response_mode"),
            ExtractJsonString(request.body, "prompt_text"));
    }
    if (request.method == "GET" && request.path == "/remote-sessions") {
        return ListRemoteSessionsResult(config);
    }
    if (request.method == "POST" && request.path == "/remote-sessions/get") {
        return GetRemoteSessionResult(config, ExtractJsonString(request.body, "session_id"));
    }
    if (request.method == "POST" && request.path == "/remote-sessions/read-slice") {
        return ReadRemoteSessionSliceResult(config, ExtractJsonString(request.body, "session_id"));
    }
    if (request.method == "POST" && request.path == "/tail-text-file") {
        int max_lines = 120;
        const std::string max_lines_raw = ExtractJsonRawValue(request.body, "max_lines");
        if (!max_lines_raw.empty()) {
            const int parsed_max_lines = std::atoi(max_lines_raw.c_str());
            max_lines = parsed_max_lines > 0 ? parsed_max_lines : 1;
        }
        return TailTextFileResult(
            config,
            ExtractJsonString(request.body, "file_path"),
            max_lines);
    }
    if (request.method == "POST" && request.path == "/task-log") {
        int max_lines = 60;
        const std::string max_lines_raw = ExtractJsonRawValue(request.body, "max_lines");
        if (!max_lines_raw.empty()) {
            const int parsed_max_lines = std::atoi(max_lines_raw.c_str());
            max_lines = parsed_max_lines > 0 ? parsed_max_lines : 1;
        }
        return TaskLogTailResult(
            config,
            ExtractJsonString(request.body, "task_id"),
            max_lines);
    }
    if (request.method == "GET") {
        if (request.path.rfind("/remote-sessions/", 0) == 0
            && request.path.size() > std::string("/remote-sessions/").size()
            && request.path.size() > std::string("/slice").size()
            && request.path.substr(request.path.size() - std::string("/slice").size()) == "/slice") {
            const std::string prefix = "/remote-sessions/";
            const std::string session_id = request.path.substr(
                prefix.size(),
                request.path.size() - prefix.size() - std::string("/slice").size());
            return ReadRemoteSessionSliceResult(config, session_id);
        }
        if (request.path.rfind("/remote-sessions/", 0) == 0 && request.path.size() > std::string("/remote-sessions/").size()) {
            return GetRemoteSessionResult(config, request.path.substr(std::string("/remote-sessions/").size()));
        }
        const std::string task_id = ExtractTaskIdFromPath(request.path);
        if (!task_id.empty()) {
            if (g_task_manager == nullptr) {
                CommandResult result;
                result.ok = false;
                result.exit_code = 41;
                result.fields["error"] = "task manager is not active";
                return result;
            }
            return g_task_manager->GetTaskResult(task_id);
        }
    }

    CommandResult result;
    result.ok = false;
    result.exit_code = 404;
    result.fields["error"] = "route not found";
    result.fields["method"] = request.method;
    result.fields["path"] = request.path;
    return result;
}
