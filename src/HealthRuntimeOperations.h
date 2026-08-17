#pragma once

struct RuntimeEndpointHealthSnapshot {
    bool generation_ready = false;
    bool embedding_ready = false;
    bool local_chat_ready = false;
    std::string generation_detail = "not configured";
    std::string embedding_detail = "not configured";
    std::string local_chat_detail = "not configured";
    std::string effective_embedding_endpoint;
    std::string effective_local_chat_endpoint;
    std::string embedding_endpoint_source = "unreachable";
    std::string local_chat_endpoint_source = "unreachable";
};
RuntimeEndpointHealthSnapshot ResolveRuntimeEndpointHealth(
    const AgentConfig & config,
    bool * cache_hit,
    long long * probe_duration_ms) {
    static std::mutex cache_mutex;
    static bool cache_valid = false;
    static std::string cache_key;
    static RuntimeEndpointHealthSnapshot cached;
    static std::chrono::steady_clock::time_point cache_expires_at;

    const std::string current_key = config.generation_endpoint + "\n"
        + config.embedding_endpoint + "\n"
        + config.local_chat_endpoint;
    const auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if (cache_valid && cache_key == current_key && now < cache_expires_at) {
            if (cache_hit != nullptr) {
                *cache_hit = true;
            }
            if (probe_duration_ms != nullptr) {
                *probe_duration_ms = 0;
            }
            return cached;
        }
    }

    if (cache_hit != nullptr) {
        *cache_hit = false;
    }
    const auto probe_started = std::chrono::steady_clock::now();
    constexpr int kHealthProbeTimeoutMs = 500;

    // Network probes intentionally run without the cache mutex. A slow or unreachable
    // optional endpoint must not serialize every health request behind one probe.
    RuntimeEndpointHealthSnapshot refreshed;
    refreshed.generation_ready = !config.generation_endpoint.empty()
        && codex_lan_agent::CheckTcpEndpoint(
            config.generation_endpoint,
            kHealthProbeTimeoutMs,
            &refreshed.generation_detail);
    refreshed.embedding_ready = ResolveReachableEndpoint(
        config.embedding_endpoint,
        DeriveEmbeddingFallbackEndpoint(config),
        kHealthProbeTimeoutMs,
        &refreshed.effective_embedding_endpoint,
        &refreshed.embedding_detail,
        &refreshed.embedding_endpoint_source);
    refreshed.local_chat_ready = ResolveReachableEndpoint(
        config.local_chat_endpoint,
        DeriveLocalChatFallbackEndpoint(config),
        kHealthProbeTimeoutMs,
        &refreshed.effective_local_chat_endpoint,
        &refreshed.local_chat_detail,
        &refreshed.local_chat_endpoint_source);

    const auto probe_finished = std::chrono::steady_clock::now();
    if (probe_duration_ms != nullptr) {
        *probe_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            probe_finished - probe_started).count();
    }

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cached = refreshed;
        cache_key = current_key;
        cache_valid = true;
        cache_expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    }
    return refreshed;
}

CommandResult BuildHealthResult(const AgentConfig & config) {
    std::filesystem::create_directories(config.log_root);

    bool endpoint_probe_cache_hit = false;
    long long endpoint_probe_duration_ms = 0;
    const RuntimeEndpointHealthSnapshot endpoint_health = ResolveRuntimeEndpointHealth(
        config,
        &endpoint_probe_cache_hit,
        &endpoint_probe_duration_ms);
    const bool generation_ready = endpoint_health.generation_ready;
    const bool embedding_ready = endpoint_health.embedding_ready;
    const bool local_chat_ready = endpoint_health.local_chat_ready;
    const std::string & generation_detail = endpoint_health.generation_detail;
    const std::string & embedding_detail = endpoint_health.embedding_detail;
    const std::string & local_chat_detail = endpoint_health.local_chat_detail;
    const std::string & effective_embedding_endpoint = endpoint_health.effective_embedding_endpoint;
    const std::string & effective_local_chat_endpoint = endpoint_health.effective_local_chat_endpoint;
    const std::string & embedding_endpoint_source = endpoint_health.embedding_endpoint_source;
    const std::string & local_chat_endpoint_source = endpoint_health.local_chat_endpoint_source;
    CommandResult result;
    result.fields["status"] = "ok";
    result.fields["platform"] = CurrentPlatformName();
    result.fields["listen_host"] = config.listen_host;
    result.fields["listen_port"] = std::to_string(config.listen_port);
    result.fields["workspace_root"] = config.workspace_root;
    result.fields["log_root"] = config.log_root;
    result.fields["data_root"] = config.data_root;
    result.fields["dialog_slices_root"] = BuildDialogSlicesDir(config);
    result.fields["session_dispatch_root"] = BuildSessionDispatchDir(config);
    result.fields["remote_session_slices_root"] = config.remote_session_slices_root;
    result.fields["storage_root_mode"] = "config_resolved_absolute";
    result.fields["remote_timestamp"] = IsoTimestampNow();
    result.fields["observed_at"] = result.fields["remote_timestamp"];
    result.fields["health_endpoint_probe_cache_hit"] = endpoint_probe_cache_hit ? "true" : "false";
    result.fields["health_endpoint_probe_cache_ttl_ms"] = "5000";
    result.fields["health_endpoint_probe_duration_ms"] = std::to_string(endpoint_probe_duration_ms);
    result.fields["remote_control_events_path"] = BuildRemoteControlEventsPath(config);
    result.fields["generation_endpoint"] = config.generation_endpoint;
    result.fields["generation_ready"] = generation_ready ? "true" : "false";
    result.fields["generation_detail"] = generation_detail;
    result.fields["embedding_endpoint"] = config.embedding_endpoint;
    result.fields["embedding_endpoint_effective"] = effective_embedding_endpoint;
    result.fields["embedding_endpoint_source"] = embedding_endpoint_source;
    result.fields["embedding_ready"] = embedding_ready ? "true" : "false";
    result.fields["embedding_detail"] = embedding_detail;
    result.fields["embedding_recovery_hint"] = embedding_ready
        ? "embedding endpoint is reachable"
        : "restore embedding endpoint service or keep vectorization-dependent flows in degraded mode";
    result.fields["local_chat_endpoint"] = config.local_chat_endpoint;
    result.fields["local_chat_endpoint_primary"] = config.local_chat_endpoint;
    result.fields["local_chat_endpoint_fallback"] = DeriveLocalChatFallbackEndpoint(config);
    result.fields["local_chat_endpoint_effective"] = effective_local_chat_endpoint;
    result.fields["local_chat_endpoint_source"] = local_chat_endpoint_source;
    result.fields["local_chat_endpoint_policy"] = "supported_fallback_until_primary_restored";
    result.fields["local_chat_ready"] = local_chat_ready ? "true" : "false";
    result.fields["local_chat_detail"] = local_chat_detail;
    result.fields["local_chat_recovery_hint"] = local_chat_ready
        ? "local chat analysis endpoint is reachable; generation fallback is supported when primary is absent"
        : "restore local_chat endpoint service; if immediate project summary is needed, use rag.query or lan_agent_run_local_chat with a directory scope or source_excerpt/result_ref/evidence_ref so deterministic fallback can answer";
    result.fields["tool_config_path"] = config.tool_config_path;
    result.fields["tool_config_exists"] = (!config.tool_config_path.empty()
        && std::filesystem::exists(config.tool_config_path)) ? "true" : "false";
    result.fields["tool_config_loaded"] = (!config.tool_config_path.empty() && !config.profiles.empty()) ? "true" : "false";
    result.fields["tool_config_profile_count"] = std::to_string(config.profiles.size());
    result.fields["tool_config_load_mode"] = config.tool_config_path.empty()
        ? "missing"
        : ((!config.profiles.empty() && std::filesystem::exists(config.tool_config_path)) ? "file" : "path_only");
    result.fields["result_fields_config_source"] = config.result_fields_config_path.empty()
        ? "config_dir_default"
        : "explicit";
    result.fields["profile_count"] = std::to_string(config.profiles.size());
    if (g_task_manager != nullptr) {
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
    }
    const std::vector<std::string> active_resource_keys = SnapshotActiveResourceKeys();
    result.fields["active_resource_lock_count"] = std::to_string(active_resource_keys.size());
    for (std::size_t index = 0; index < active_resource_keys.size(); ++index) {
        result.fields["active_resource_lock_" + std::to_string(index)] = active_resource_keys[index];
    }
    const auto last_event = SnapshotLastRemoteControlEvent();
    const auto copy_last_field = [&result, &last_event](const std::string & from, const std::string & to) {
        const auto it = last_event.find(from);
        result.fields[to] = it == last_event.end() ? "" : it->second;
    };
    copy_last_field("timestamp", "last_request_time");
    copy_last_field("entry_name", "last_request_entry");
    copy_last_field("method", "last_request_method");
    copy_last_field("status", "last_request_status");
    copy_last_field("duration_ms", "last_request_duration_ms");
    copy_last_field("source_thread", "last_request_thread");
    copy_last_field("source_label", "last_request_source_label");
    copy_last_field("takeover_relation", "last_request_takeover_relation");
    copy_last_field("task_group", "last_request_task_group");
    copy_last_field("task_id", "last_request_task_id");
    copy_last_field("command_name", "last_request_command_name");
    copy_last_field("request_type", "last_request_type");
    copy_last_field("interaction_kind", "last_request_interaction_kind");
    copy_last_field("session_id", "last_request_session_id");
    copy_last_field("turn_id", "last_request_turn_id");
    copy_last_field("write_mode", "last_request_write_mode");
    copy_last_field("result_ref", "last_request_result_ref");
    copy_last_field("evidence_ref", "last_request_evidence_ref");
    copy_last_field("trigger", "last_request_trigger");
    return result;
}

CommandResult BuildLivenessResult(const AgentConfig & config) {
    CommandResult result;
    result.fields["status"] = "ok";
    result.fields["platform"] = CurrentPlatformName();
    result.fields["listen_host"] = config.listen_host;
    result.fields["listen_port"] = std::to_string(config.listen_port);
    result.fields["workspace_root"] = config.workspace_root;
    result.fields["remote_timestamp"] = IsoTimestampNow();
    result.fields["observed_at"] = result.fields["remote_timestamp"];
    result.fields["remote_control_events_path"] = BuildRemoteControlEventsPath(config);
    result.fields["local_chat_endpoint"] = config.local_chat_endpoint;
    result.fields["profile_count"] = std::to_string(config.profiles.size());
    if (g_task_manager != nullptr) {
        result.fields["queue_depth"] = std::to_string(g_task_manager->QueueDepth());
    }
    result.fields["active_resource_lock_count"] = std::to_string(SnapshotActiveResourceKeys().size());
    const auto last_event = SnapshotLastRemoteControlEvent();
    const auto copy_last_field = [&result, &last_event](const std::string & from, const std::string & to) {
        const auto it = last_event.find(from);
        result.fields[to] = it == last_event.end() ? "" : it->second;
    };
    copy_last_field("timestamp", "last_request_time");
    copy_last_field("entry_name", "last_request_entry");
    copy_last_field("method", "last_request_method");
    copy_last_field("status", "last_request_status");
    copy_last_field("duration_ms", "last_request_duration_ms");
    copy_last_field("source_thread", "last_request_thread");
    copy_last_field("source_label", "last_request_source_label");
    copy_last_field("takeover_relation", "last_request_takeover_relation");
    copy_last_field("task_group", "last_request_task_group");
    copy_last_field("task_id", "last_request_task_id");
    copy_last_field("command_name", "last_request_command_name");
    copy_last_field("request_type", "last_request_type");
    copy_last_field("interaction_kind", "last_request_interaction_kind");
    copy_last_field("session_id", "last_request_session_id");
    copy_last_field("turn_id", "last_request_turn_id");
    copy_last_field("write_mode", "last_request_write_mode");
    copy_last_field("result_ref", "last_request_result_ref");
    copy_last_field("evidence_ref", "last_request_evidence_ref");
    copy_last_field("trigger", "last_request_trigger");
    return result;
}

CommandResult BuildRuntimeOverviewResult(const AgentConfig & config) {
    CommandResult result = BuildHealthResult(config);
    result.fields["overview"] = "runtime_overview";
    result.fields["platform"] = CurrentPlatformName();

    int profile_index = 0;
    for (const auto & entry : config.profiles) {
        result.fields["profile_" + std::to_string(profile_index)] = entry.first;
        ++profile_index;
    }
    result.fields["profile_count"] = std::to_string(profile_index);
    result.fields["async_local_chat_enabled"] = config.local_chat_endpoint.empty() ? "false" : "true";
    result.fields["async_queue_enabled"] = g_task_manager != nullptr ? "true" : "false";
    const int queue_depth = std::atoi(GetFieldOrDefault(result, "queue_depth", "0").c_str());
    result.fields["queue_depth_congestion_threshold_warning"] = "3";
    result.fields["queue_depth_congestion_threshold_busy"] = "6";
    result.fields["queue_depth_interpretation"] =
        queue_depth >= 6 ? "congested"
        : (queue_depth >= 3 ? "busy" : "normal");
    result.fields["queue_depth_guidance"] =
        "0-2 normal; 3-5 busy; >=6 congested and likely to delay queued work";

    const auto active_resource_locks = SnapshotActiveResourceLockDetails();
    result.fields["active_resource_lock_count"] = std::to_string(active_resource_locks.size());
    for (std::size_t index = 0; index < active_resource_locks.size(); ++index) {
        result.fields["active_resource_lock_" + std::to_string(index)] = std::get<0>(active_resource_locks[index]);
        result.fields["active_resource_lock_owner_" + std::to_string(index)] = std::get<1>(active_resource_locks[index]);
        result.fields["active_resource_lock_acquired_at_" + std::to_string(index)] = std::get<2>(active_resource_locks[index]);
    }
    result.fields["active_resource_lock_guidance"] =
        active_resource_locks.empty()
            ? "no active resource locks"
            : "inspect active_resource_lock_owner_* and active_resource_lock_acquired_at_* when queueing or lock contention occurs";

    const bool tool_config_exists = GetFieldOrDefault(result, "tool_config_exists", "false") == "true";
    const bool profiles_available = profile_index > 0;
    const bool embedding_ready = GetFieldOrDefault(result, "embedding_ready", "false") == "true";
    const bool local_chat_ready = GetFieldOrDefault(result, "local_chat_ready", "false") == "true";
    result.fields["tool_config_mode"] =
        tool_config_exists ? "configured"
        : (profiles_available ? "degraded_but_profiles_present" : "profile_actions_likely_unavailable");
    result.fields["tool_config_effect"] =
        tool_config_exists ? "profile-driven actions should run normally"
        : (profiles_available
            ? "tool config file is missing but in-memory profiles still exist; some actions may keep running until restart"
            : "profile-driven actions will likely fallback or fail because no tool config profiles are loaded");
    const std::string result_fields_config_path = ResolveResultFieldConfigPath(config);
    result.fields["result_fields_config_path"] = result_fields_config_path;
    result.fields["result_fields_config_exists"] =
        (!result_fields_config_path.empty() && std::filesystem::exists(result_fields_config_path)) ? "true" : "false";
    result.fields["result_fields_catalog_version"] = GetDefaultResultEnvelopeConfig().catalog_version;
    result.fields["result_fields_catalog_mode"] = "embedded";
    result.fields["result_fields_catalog_field_count"] = std::to_string(GetFieldCatalog().size());
    result.fields["result_fields_tool_meta_count"] = std::to_string(GetToolMetaCatalog().size());
    result.fields["tool_config_unstable_capabilities"] =
        tool_config_exists
            ? ""
            : "lan_agent_check_build_dir,lan_agent_prepare_build_dir,lan_agent_build_target,lan_agent_configure_project,lan_agent_run_ctest_target";
    result.fields["tool_config_stable_capabilities"] =
        "lan_agent_discover_ctest_tests,lan_agent_read_text_file,lan_agent_list_directory,lan_agent_tail_text_file";
    if (!embedding_ready) {
        result.fields["embedding_degraded_capabilities"] =
            "rag_memory_slice_store,dialog_slice_analysis_vector_recall,embedding_backed_retrieval";
    }
    if (!local_chat_ready) {
        result.fields["local_chat_degraded_capabilities"] =
            "lan_agent_run_local_chat,lan_agent_ventriloquist_reply,llama.observer_smoke,rag.query_local_analysis";
    }
    if (!tool_config_exists) {
        result.fields["next_action"] = profiles_available
            ? "restore tool_config_path on disk and keep current process alive if existing profiles are still needed"
            : "restore tool_config_path or reload profiles before using profile-driven build/test actions";
        result.fields["tool_config_recovery_hint"] =
            "discover and read-only tools can still run, but profile-driven build/test actions may become unstable after restart until tool_config_path is restored";
    } else if (!embedding_ready && !local_chat_ready) {
        result.fields["next_action"] =
            "restore embedding_endpoint and local_chat_endpoint services before vectorization, memory recall, or local-chat analysis validation";
    } else if (!embedding_ready) {
        result.fields["next_action"] =
            "restore embedding_endpoint before validating vectorization, recall, or memory-slice ingest";
    } else if (!local_chat_ready) {
        result.fields["next_action"] =
            "restore local_chat_endpoint before validating local-chat analysis or ventriloquy flows; meanwhile use rag.query with directory scope or evidence refs for deterministic fallback summaries";
    }
    return result;
}

CommandResult BuildRagBasicCommSmokeResult(const AgentConfig & config) {
    CommandResult health = BuildHealthResult(config);
    CommandResult result;
    result.fields["module"] = "RAG-integration-thread";
    result.fields["remote_entry"] = "192.168.9.100:18080";
    result.fields["action"] = "rag_basic_comm_smoke";
    result.fields["owner_thread"] = "RAG-integration-thread";
    result.fields["healthz_status"] = GetFieldOrDefault(health, "status", "unknown");
    result.fields["healthz_outcome"] = ComputeCommandOutcome(health);
    result.fields["queue_depth"] = GetFieldOrDefault(health, "queue_depth", "0");
    result.fields["profile_count"] = GetFieldOrDefault(health, "profile_count", "0");
    result.fields["generation_ready"] = GetFieldOrDefault(health, "generation_ready", "false");
    result.fields["embedding_ready"] = GetFieldOrDefault(health, "embedding_ready", "false");
    result.fields["local_chat_ready"] = GetFieldOrDefault(health, "local_chat_ready", "false");
    result.fields["generation_endpoint"] = config.generation_endpoint;
    result.fields["embedding_endpoint"] = config.embedding_endpoint;
    result.fields["local_chat_endpoint"] = config.local_chat_endpoint;
    result.fields["embedding_smoke_status"] =
        result.fields["embedding_ready"] == "true" ? "service_ready" : "service_unavailable";
    result.fields["embedding_provider_id"] = "configured_embedding_endpoint";
    result.fields["embedding_service_ready"] = result.fields["embedding_ready"];
    result.fields["ingest_filter"] =
        "canonical_status=canonical && vector_ready=true && vector_skip_reason=empty";
    result.fields["ingest_ready"] = "false";
    result.fields["ingest_ready_reason"] =
        "requires_slice_passing_ingest_filter; embedding_service_ready_alone_is_not_sufficient";
    result.fields["ingest_candidate_required_fields"] =
        "canonical_status,vector_ready,vector_skip_reason,canonical_slice_id,provider_id,capability_id,storage_refs";

    std::filesystem::path latest_log_path;
    std::filesystem::file_time_type latest_write_time{};
    bool found_log = false;
    std::error_code ec;
    for (const auto & entry : std::filesystem::directory_iterator(config.log_root, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path candidate = entry.path();
        if (candidate.extension() != ".log") {
            continue;
        }
        const auto write_time = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (!found_log || write_time > latest_write_time) {
            found_log = true;
            latest_write_time = write_time;
            latest_log_path = candidate;
        }
    }

    if (found_log) {
        result.fields["latest_log_path"] = latest_log_path.string();
        result.fields["latest_log_name"] = latest_log_path.filename().string();
        result.fields["latest_log_status"] = "found";
        const auto system_now = std::chrono::system_clock::now();
        const auto file_now = std::filesystem::file_time_type::clock::now();
        const auto system_write_time =
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                latest_write_time - file_now + system_now);
        const std::time_t write_time = std::chrono::system_clock::to_time_t(system_write_time);
        result.fields["latest_log_time"] = std::to_string(static_cast<long long>(write_time));
        CommandResult tail = TailTextFileResult(config, latest_log_path.string(), 20);
        result.fields["latest_log_tail"] = GetFieldOrDefault(tail, "content", "");
    } else {
        result.fields["latest_log_path"] = "";
        result.fields["latest_log_name"] = "";
        result.fields["latest_log_time"] = "";
        result.fields["latest_log_status"] = "not_found";
        result.fields["latest_log_tail"] = "";
    }

    std::vector<std::string> blocking_points;
    if (result.fields["generation_ready"] != "true") {
        blocking_points.push_back("generation_not_ready");
    }
    if (result.fields["embedding_ready"] != "true") {
        blocking_points.push_back("embedding_not_ready");
    }
    if (result.fields["local_chat_ready"] != "true") {
        blocking_points.push_back("local_chat_not_ready");
    }

    if (blocking_points.empty()) {
        result.fields["risk_level"] = "low";
        result.fields["blocking_points"] = "none";
        result.fields["result_summary"] = "basic communication smoke passed";
        result.fields["result"] = "pass";
        result.fields["next_action"] = "continue rag migration smoke";
    } else {
        std::ostringstream blocking;
        for (std::size_t index = 0; index < blocking_points.size(); ++index) {
            if (index > 0) {
                blocking << ",";
            }
            blocking << blocking_points[index];
        }
        result.ok = false;
        result.exit_code = 44;
        result.fields["risk_level"] = "medium";
        result.fields["blocking_points"] = blocking.str();
        result.fields["result_summary"] = "basic communication smoke has blocking points";
        result.fields["result"] = "fail";
        result.fields["next_action"] =
            "restore blocked provider then rerun smoke; if the current goal is project-directory overview only, use rag.query with directory scope or source_excerpt/result_ref/evidence_ref for deterministic fallback";
    }

    return result;
}

CommandResult BuildLlamaObserverSmokeResult(
    const AgentConfig & config,
    bool probe,
    const std::string & question) {
    CommandResult health = BuildHealthResult(config);
    CommandResult result;
    result.fields["module"] = "intranet_migration";
    result.fields["action"] = "llama_observer_smoke";
    result.fields["observer_role"] = "trace_codex_mcp_llama_chain_without_bypassing_agent";
    result.fields["chain"] = "CODEX->local_MCP->codex_lan_agent->local_chat->llama_cpp";
    result.fields["mcp_entry"] = config.listen_host + ":" + std::to_string(config.listen_port) + "/mcp";
    result.fields["local_chat_endpoint"] = config.local_chat_endpoint;
    result.fields["generation_endpoint"] = config.generation_endpoint;
    result.fields["embedding_endpoint"] = config.embedding_endpoint;
    result.fields["local_chat_ready"] = GetFieldOrDefault(health, "local_chat_ready", "false");
    result.fields["generation_ready"] = GetFieldOrDefault(health, "generation_ready", "false");
    result.fields["embedding_ready"] = GetFieldOrDefault(health, "embedding_ready", "false");
    result.fields["remote_control_events_path"] = BuildRemoteControlEventsPath(config);
    result.fields["experience_card_path"] = BuildExperienceCardsPath(config);
    result.fields["recommended_entry_tool"] = "rag.query";
    result.fields["recommended_observer_tool"] = "llama.observer_smoke";
    result.fields["recommended_trace_tool"] = "lan_agent_tail_control_events";
    result.fields["probe_requested"] = probe ? "true" : "false";
    result.fields["probe_question"] = question;
    result.fields["success_rule"] =
        "local_chat_ready=true and MCP event contains command_name=llama.observer_smoke or rag.query";
    result.fields["fallback"] =
        "{\"tool\":\"rag.basic_comm_smoke\",\"reason\":\"observer smoke unavailable or provider blocked\"}";

    std::vector<std::string> blocking_points;
    if (result.fields["local_chat_ready"] != "true") {
        blocking_points.push_back("local_chat_not_ready");
    }
    if (result.fields["generation_ready"] != "true") {
        blocking_points.push_back("generation_not_ready");
    }
    if (result.fields["embedding_ready"] != "true") {
        blocking_points.push_back("embedding_not_ready");
    }

    if (probe) {
        const std::string resolved_question = question.empty()
            ? "Reply with exactly: llama_observer_ok"
            : question;
        CommandResult probe_result = RunLocalChat(
            config,
            "llama_observer",
            resolved_question,
            "observer_smoke",
            15000);
        result.fields["probe_ok"] = probe_result.ok ? "true" : "false";
        result.fields["probe_status_code"] = GetFieldOrDefault(probe_result, "status_code", "");
        result.fields["probe_log_path"] = GetFieldOrDefault(probe_result, "log_path", "");
        result.fields["probe_output_text"] = ExtractOutputTextFallback(probe_result);
        result.fields["probe_semantic_outcome"] = GetFieldOrDefault(
            probe_result,
            "semantic_outcome",
            ComputeCommandOutcome(probe_result));
        if (!probe_result.ok) {
            blocking_points.push_back("local_chat_probe_failed");
        }
    } else {
        result.fields["probe_ok"] = "not_run";
        result.fields["probe_log_path"] = "";
        result.fields["probe_output_text"] = "";
        result.fields["next_action"] = "rerun llama.observer_smoke with probe=true for one controlled local_chat call";
    }

    if (blocking_points.empty()) {
        result.fields["risk_level"] = probe ? "low" : "medium";
        result.fields["blocking_points"] = "none";
        result.fields["result"] = probe ? "pass" : "ready_without_probe";
        result.fields["result_summary"] = probe
            ? "CODEX to local MCP to local llama path is observable"
            : "observer chain is ready; probe was not executed";
        if (probe) {
            result.fields["next_action"] = "tail remote_control_events and verify rag.query or observer call ordering";
        }
    } else {
        result.ok = false;
        result.exit_code = 44;
        result.fields["risk_level"] = "medium";
        std::ostringstream blocking;
        for (std::size_t index = 0; index < blocking_points.size(); ++index) {
            if (index > 0) {
                blocking << ",";
            }
            blocking << blocking_points[index];
        }
        result.fields["blocking_points"] = blocking.str();
        result.fields["result"] = "fail";
        result.fields["result_summary"] = "observer chain has blocking points";
        result.fields["next_action"] = "restore blocked local endpoint then rerun observer smoke";
    }
    return result;
}