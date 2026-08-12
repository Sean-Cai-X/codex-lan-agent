#pragma once

std::string StableContentChecksum(const std::string & content);

struct FieldCatalogEntry {
    const char * id;
    const char * key;
    const char * default_value;
    bool required;
    bool is_public;
    bool write_to_log;
    bool redact;
};

struct ToolMeta {
    const char * tool_name;
    const char * provider_id;
    const char * capability_id;
    const char * default_summary;
    const char * public_fields;
    const char * log_safety;
};

struct ResultEnvelopeConfig {
    const char * schema_id;
    const char * catalog_version;
};

struct ResultFieldOverlay {
    std::vector<std::string> required_fields;
    std::unordered_map<std::string, std::string> default_fields;
    std::string public_fields;
    std::string log_safety;
};

const ResultEnvelopeConfig & GetDefaultResultEnvelopeConfig() {
    static const ResultEnvelopeConfig config = {
        "lan_result_envelope_v1",
        "field_catalog_v1"
    };
    return config;
}

const std::vector<FieldCatalogEntry> & GetFieldCatalog() {
    static const std::vector<FieldCatalogEntry> catalog = {
        {"ok", "ok", "", true, true, true, false},
        {"exit_code", "exit_code", "", true, true, true, false},
        {"tool_name", "tool_name", "", true, true, true, false},
        {"request_id", "request_id", "", false, true, true, false},
        {"trace_id", "trace_id", "", false, true, true, false},
        {"tool_call_id", "tool_call_id", "", false, true, true, false},
        {"schema_version", "schema_version", "", false, true, true, false},
        {"result_hash", "result_hash", "", false, true, true, false},
        {"status", "status", "", false, true, true, false},
        {"error_code", "error_code", "", false, true, true, false},
        {"error_message", "error_message", "", false, true, true, false},
        {"provider_id", "provider_id", "codex-lan-agent", false, true, true, false},
        {"capability_id", "capability_id", "", false, true, true, false},
        {"task_id", "task_id", "", false, true, true, false},
        {"session_id", "session_id", "", false, true, true, false},
        {"turn_id", "turn_id", "", false, true, true, false},
        {"error", "error", "", false, true, true, false},
        {"summary", "summary", "", false, true, true, false},
        {"result_ref", "result_ref", "", false, true, true, false},
        {"evidence_ref", "evidence_ref", "", false, true, true, false},
        {"log_path", "log_path", "", false, true, true, false},
        {"patch_id", "patch_id", "", false, true, true, false},
        {"write_verified", "write_verified", "", false, true, true, false},
        {"disk_write_completed", "disk_write_completed", "", false, true, true, false},
        {"request_type", "request_type", "", false, true, true, false},
        {"trigger", "trigger", "", false, true, true, false},
        {"risk", "risk", "", false, true, true, false},
        {"tags", "tags", "", false, true, true, false},
        {"clips_pre_call_tool_chain_template_id", "clips_pre_call_tool_chain_template_id", "", false, true, true, false},
        {"clips_pre_call_tool_chain_request_type", "clips_pre_call_tool_chain_request_type", "", false, true, true, false},
        {"clips_pre_call_tool_chain_risk", "clips_pre_call_tool_chain_risk", "", false, true, true, false},
        {"clips_pre_call_tool_chain_safety_class", "clips_pre_call_tool_chain_safety_class", "", false, true, true, false},
        {"clips_pre_call_tool_chain_execution_class", "clips_pre_call_tool_chain_execution_class", "", false, true, true, false},
        {"clips_pre_call_tool_chain_evidence_policy", "clips_pre_call_tool_chain_evidence_policy", "", false, true, true, false},
        {"clips_pre_call_tool_chain_clips_required", "clips_pre_call_tool_chain_clips_required", "", false, true, true, false},
        {"clips_pre_call_chain_template_id", "clips_pre_call_chain_template_id", "", false, true, true, false},
        {"clips_pre_call_chain_request_type", "clips_pre_call_chain_request_type", "", false, true, true, false},
        {"clips_pre_call_chain_risk", "clips_pre_call_chain_risk", "", false, true, true, false},
        {"clips_pre_call_chain_safety_class", "clips_pre_call_chain_safety_class", "", false, true, true, false},
        {"clips_pre_call_chain_execution_class", "clips_pre_call_chain_execution_class", "", false, true, true, false},
        {"clips_pre_call_chain_evidence_policy", "clips_pre_call_chain_evidence_policy", "", false, true, true, false},
        {"clips_pre_call_chain_clips_required", "clips_pre_call_chain_clips_required", "", false, true, true, false},
        {"clips_post_result_chain_template_id", "clips_post_result_chain_template_id", "", false, true, true, false},
        {"clips_post_result_chain_request_type", "clips_post_result_chain_request_type", "", false, true, true, false},
        {"clips_post_result_chain_risk", "clips_post_result_chain_risk", "", false, true, true, false},
        {"clips_post_result_chain_safety_class", "clips_post_result_chain_safety_class", "", false, true, true, false},
        {"clips_post_result_chain_execution_class", "clips_post_result_chain_execution_class", "", false, true, true, false},
        {"clips_post_result_chain_evidence_policy", "clips_post_result_chain_evidence_policy", "", false, true, true, false},
        {"clips_post_result_chain_clips_required", "clips_post_result_chain_clips_required", "", false, true, true, false}
    };
    return catalog;
}

const std::vector<ToolMeta> & GetToolMetaCatalog() {
    static const std::vector<ToolMeta> tools = {
        {
            "lan_agent_mcp_route",
            "codex-lan-agent",
            "mcp_gateway_route",
            "mcp gateway route returned",
            "ok,exit_code,tool_name,status,result,error,summary,next_action,mcp_route_mode,mcp_route_entry_tool,tool_use_decision,current_tool_chain_node,chain_state,tool_surface_policy,visible_tool_count,visible_tool_name,internal_execution_performed,routed_tool_name,routed_tool_surface,route_target,pre_guard_route_arguments_source,clips_pre_call_tool_decision,clips_pre_call_tool_verification,clips_pre_call_tool_reason_code,clips_pre_call_tool_matched_rule,clips_pre_call_tool_route_target,clips_pre_call_tool_route_arguments_json,clips_pre_call_tool_route_arguments_json_ref,clips_pre_call_tool_route_arguments_json_available,clips_pre_call_tool_route_arguments_json_transport,task_execution_in_mcp_required,forced_task_memory_execution,long_loop_budget_recommended,long_loop_freeze_tool_name,long_loop_budget_tool_name,primary_intent,directory_mutation_flow,directory_scope_active,directory_manifest_path,directory_current_file_index,directory_next_file_index,directory_total_code_file_count,directory_remaining_code_file_count,directory_scope_incomplete,directory_next_probe_call_json,flow_id,flow_task_list_required,flow_current_task_id,flow_next_task_id,flow_task_list_path,flow_task_list_md_path,remote_endpoint,remote_prefix,remote_tool_count,remote_tools_csv,local_proxy_tools_csv,bridge_mode,local_proxy_tool_name,remote_tool_name,remote_status_code,remote_status,remote_result,remote_summary,remote_error,remote_has_more,remote_next_call_json,remote_required_tool_name,remote_required_tool_arguments_json,source_file,dry_run,would_change,changed,format_apply_required,dry_run_only,directory_path,normalized_path,total_entries,file_count,code_file_count,remaining_code_file_count,directory_count,entry_labels_json,file_paths_json,file_names_json,directory_listing_complete,known_file_list_complete,batch_manifest_complete,batch_manifest_path,batch_total_files,batch_read_file_count,remaining_batch_file_count,batch_completion,content_read_completion,incomplete_scope,next_batch_file_path,next_tool_name,next_file_path,next_max_lines,analysis_allowed,analysis_blocked_reason,required_next_action_type,required_tool_name,required_tool_arguments_json,continue_required,auto_continue_required,semantic_model_clamp,assistant_response_allowed,completion_guard,task_memory_freeze_recovery,task_memory_freeze_recovered_from_short_call,goal_id,trace_id,terminal_state,completion_claim_allowed,final_answer_allowed,verification_ok,clean_chat_close_allowed,conversation_close_status,chat_context_reset_required,chat_context_reset_requested,chat_context_reset_acknowledged,host_chat_history_mutable_by_mcp,old_context_dropped,mcp_continuation_ready,new_chat_entry_tool_name,new_chat_entry_arguments_json,local_ai_mcp_guidance_version,local_ai_required_entry,local_ai_guidance_json,local_ai_context_bootstrap_json,accepted,conclusion,case_count,pass_count,fail_count,boundary_case_count,boundary_pass_count,boundary_fail_count,synthetic_flow_violation_count,conformance_pass,flow_conformance_pass,flow_violation_count,violation_count,completion_state,current_node,next_expected_node,boundary_cases_jsonl_path,boundary_summary_json_path,rule_candidates_md_path,flow_state_dashboard_html_path,violations_md_path,result_ref,evidence_ref,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_health",
            "codex-lan-agent",
            "runtime_health",
            "agent health returned",
            "ok,exit_code,status,workspace_root,log_root,generation_endpoint,generation_ready,local_chat_endpoint_primary,local_chat_endpoint_fallback,local_chat_endpoint_effective,local_chat_endpoint_source,local_chat_endpoint_policy,local_chat_ready,local_chat_detail,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_flow_task_list",
            "codex-lan-agent",
            "flow_task_list",
            "flow task list generated",
            "ok,exit_code,status,result,error,summary,flow_id,goal_id,trace_id,current_task_id,flow_task_count,flow_task_list_path,flow_task_list_md_path,result_ref,evidence_ref,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_runtime_overview",
            "codex-lan-agent",
            "runtime_overview",
            "runtime overview returned",
            "ok,exit_code,queue_depth,tool_config_exists,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_remote_session_overview",
            "codex-lan-agent",
            "remote_session_overview",
            "remote session overview returned",
            "ok,exit_code,status,status_code,total_sessions_count,session_semantic_projection_source,session_semantic_projection_mode,session_semantic_projection_visible_count,session_semantic_projection_missing_count,latest_session_id,latest_turn_id,latest_title,latest_summary,session_items_json,browser_section_id,browser_section_title,browser_http_get_path,browser_route_hint,browser_status,browser_primary_count,browser_payload_field,browser_card_json,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_overview",
            "codex-lan-agent",
            "task_overview",
            "task overview returned",
            "ok,exit_code,status,queue_depth,active_resource_lock_count,visible_count,total_task_count,tasks_json,latest_task_available,latest_task_id,latest_task_status,latest_task_type,latest_task_summary,latest_task_result_ref,latest_task_evidence_ref,latest_task_resolved_log_path,latest_task_json,task_retention_model,max_completed_history,browser_section_id,browser_section_title,browser_http_get_path,browser_route_hint,browser_status,browser_primary_count,browser_payload_field,browser_card_json,result,summary,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_event_overview",
            "codex-lan-agent",
            "event_overview",
            "event overview returned",
            "ok,exit_code,status,visible_count,matched_count,max_entries,offset,has_more,next_offset,command_name_filter,session_id_filter,task_id_filter,since_timestamp,latest_timestamp,latest_command_name,latest_status,latest_session_id,latest_turn_id,latest_result_ref,latest_evidence_ref,events_json,browser_section_id,browser_section_title,browser_http_get_path,browser_route_hint,browser_status,browser_primary_count,browser_payload_field,browser_card_json,result,summary,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_patch_overview",
            "codex-lan-agent",
            "patch_overview",
            "patch audit overview returned",
            "ok,exit_code,status,visible_count,matched_count,max_entries,offset,has_more,next_offset,patch_id_filter,trace_id_filter,file_path_filter,latest_patch_id,latest_trace_id,latest_file_path,latest_stage,latest_status,latest_log_path,patch_events_json,audit_event_path,browser_section_id,browser_section_title,browser_http_get_path,browser_route_hint,browser_status,browser_primary_count,browser_payload_field,browser_card_json,result,summary,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_mcp_overview",
            "codex-lan-agent",
            "mcp_overview",
            "mcp overview returned",
            "ok,exit_code,status,mcp_endpoint,mcp_transport,tool_count,tool_names_json,semantic_action_count,profile_count,tool_config_exists,tool_config_mode,local_chat_ready,local_ai_mcp_guidance_version,local_ai_required_entry,local_ai_common_file_operation_policy,local_ai_context_policy,local_ai_comment_cleanup_policy,local_ai_code_format_policy,local_ai_long_loop_policy,local_ai_completion_gate,local_ai_conversation_close_policy,local_ai_guidance_json,local_ai_context_bootstrap_json,browser_section_id,browser_section_title,browser_http_get_path,browser_route_hint,browser_status,browser_primary_count,browser_payload_field,browser_card_json,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_remote_mcp_overview",
            "codex-lan-agent",
            "remote_mcp_bridge",
            "remote MCP bridge overview returned",
            "ok,exit_code,status,result,error,summary,bridge_mode,remote_endpoint,remote_prefix,remote_tool_count,remote_tools_csv,local_proxy_tools_csv,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_rag_overview",
            "codex-lan-agent",
            "rag_overview",
            "rag overview returned",
            "ok,exit_code,status,enabled,ready,pending,chunk_count,clips_meta,capabilities,next_action,browser_section_id,browser_section_title,browser_http_get_path,browser_route_hint,browser_status,browser_primary_count,browser_payload_field,browser_card_json,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_browser_list_overview",
            "codex-lan-agent",
            "browser_list_overview",
            "browser list overview returned",
            "ok,exit_code,status,consumer_contract_version,default_section_id,default_route,overview_tools_json,consumer_http_paths_json,section_count,sections_json,section_contract_json,browser_section_id,browser_section_title,browser_http_get_path,browser_route_hint,browser_status,browser_primary_count,browser_payload_field,browser_card_json,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_rag_index_status",
            "codex-lan-agent",
            "rag_bridge_status",
            "rag bridge index status returned",
            "ok,exit_code,enabled,ready,status,pending,chunk_count,clips_meta,capabilities,next_action,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_rag_clips_meta",
            "codex-lan-agent",
            "rag_bridge_clips_meta",
            "rag bridge clips meta returned",
            "ok,exit_code,query,top_k,clips_meta,fact_schema_id,decision_schema_id,decision,verification,matched_rule,next_action,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_rag_clips_run",
            "codex-lan-agent",
            "rag_bridge_clips_run",
            "rag bridge clips run returned",
            "ok,exit_code,query,top_k,request_id,trace_id,query_id,rag_clips_run_contract,rag_clips_run_profile_dependency,backend,message,rules_dir,manifest_path,rule_set_id,store_refs,store_refs_present,store_refs_verified,store_refs_verification_summary,run_snapshot_path,run_snapshot_exists,fact_page_path,fact_page_exists,query_index_path,query_index_exists,clips_runs_log_path,clips_runs_log_exists,slice_index_refs_json,slice_index_ref_count,slice_index_verified_count,slice_index_missing_count,verified_slice_index_refs_json,missing_slice_index_refs_json,result_ref,evidence_ref,log_path,next_action,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_rag_storage_lookup",
            "codex-lan-agent",
            "rag_bridge_storage_lookup",
            "rag bridge storage lookup returned",
            "ok,exit_code,kind,id,slice_id,trace_id,query_id,node_id,edge_id,found,primary,related,storage_base_path,rocksdb_path,kv_snapshot_path,result_ref,evidence_ref,log_path,next_action,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_rag_review_observe",
            "codex-lan-agent",
            "rag_bridge_review_observe",
            "rag bridge review observation returned",
            "ok,exit_code,request_id,trace_id,query_id,module,task_case,observation_id,test_bucket,bucket_coverage,coverage_gap,coverage_status,review_scope,result_stage,best_params_ref,human_review_required,conclusion_id,review_store_path,review_index_path,review_trace_index_path,review_bucket_index_path,rocksdb_path,kv_snapshot_path,result_ref,evidence_ref,log_path,next_action,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_rag_storage_page",
            "codex-lan-agent",
            "rag_bridge_storage_page",
            "rag bridge storage page returned",
            "ok,exit_code,kind,trace_id,query_id,test_bucket,coverage_gap,result_stage,coverage_status,run_kind,fact_type,offset,limit,returned_count,total_count,has_more,next_offset,records,trace_lookup,key_prefix,result_ref,evidence_ref,log_path,next_action,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_remote_session_semantic_catalog",
            "codex-lan-agent",
            "remote_session_semantic_catalog",
            "remote session semantic catalog returned",
            "ok,exit_code,status,semantic_binding_mode,semantic_observability_mode,semantic_catalog_count,remote_dialog_semantic_list_count,callable_semantic_count,non_callable_semantic_count,mcp_tool_count,semantic_action_count,mounted_tool_count,display_projection_mode,all_mcp_tools_cataloged,all_semantic_actions_cataloged,all_catalog_entries_visible_in_dialog_list,catalog_is_single_source_of_truth,mounted_tools_json,semantic_catalog_json,remote_dialog_semantic_list_json,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_list_remote_sessions",
            "codex-lan-agent",
            "remote_session_list",
            "remote sessions listed",
            "ok,exit_code,status,status_code,session_semantic_projection_source,session_semantic_projection_mode,session_semantic_projection_visible_count,session_semantic_projection_missing_count,session_semantic_projection_list_json,body,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_get_remote_session",
            "codex-lan-agent",
            "remote_session_detail",
            "remote session loaded",
            "ok,exit_code,status,session_id,status_code,session_semantic_projection_ready,session_semantic_projection_source,session_semantic_projection_latest_turn_id,semantic_binding_mode,semantic_observability_mode,semantic_catalog_count,remote_dialog_semantic_list_count,callable_semantic_count,non_callable_semantic_count,mounted_tool_count,display_projection_mode,all_catalog_entries_visible_in_dialog_list,catalog_is_single_source_of_truth,available_tool_classes_json,semantic_catalog_json,remote_dialog_semantic_list_json,body,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_list_profiles",
            "codex-lan-agent",
            "profile_catalog",
            "profile catalog returned",
            "ok,exit_code,profile_count,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_profile_catalog",
            "codex-lan-agent",
            "profile_catalog",
            "profile catalog returned",
            "ok,exit_code,profile_count,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_run_cli_profile",
            "codex-lan-agent",
            "cli_profile_run",
            "cli profile executed",
            "ok,exit_code,status,profile,expected_marker,timed_out,stalled,process_exit_ok,process_id,started_at,finished_at,last_output_at,effective_timeout_sec,effective_stall_timeout_sec,profile_timeout_sec,profile_timeout_source,profile_stall_timeout_sec,profile_stall_timeout_source,execution_completion_reason,process_progress_signal,timeout_diagnostic,runtime_sec,quiet_sec_at_finish,heartbeat_count,process_output_observed,log_path,resource_key,semantic_outcome,verification_ok,result_ref,evidence_ref,resolved_log_path,summary,next_action,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_enqueue_cli_profile",
            "codex-lan-agent",
            "cli_profile_run",
            "cli profile task queued",
            "ok,exit_code,status,task_id,profile,timeout_sec_override,stall_timeout_sec_override,summary,next_action,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_run_local_chat",
            "codex-lan-agent",
            "local_chat_analysis",
            "local chat analysis returned",
            "ok,exit_code,status,scope,mode,question,question_effective,analysis_only,review_analysis_only,execution_capability,implicit_evidence_lookup,evidence_injection_template,evidence_injection_used,evidence_task_id,evidence_result_ref,evidence_evidence_ref,evidence_resolved_log_path,evidence_log_excerpt_chars,evidence_source_excerpt_chars,local_chat_endpoint_primary,local_chat_endpoint_fallback,local_chat_endpoint_effective,local_chat_endpoint_mode,local_chat_endpoint_policy,trusted_execution_evidence_required,execution_evidence_contract,real_execution_toolchain_json,fallback_mode,project_summary_contract,provider_recovery_hint,evidence_input_mode,summary,direct_answer,source_refs,evidence_lines,confidence,insufficient_context,next_action,result_ref,evidence_ref,log_path,body,result,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_enqueue_local_chat",
            "codex-lan-agent",
            "local_chat_analysis",
            "local chat task queued",
            "ok,exit_code,status,task_id,scope,analysis_only,execution_capability,evidence_injection_template,evidence_injection_used,trusted_execution_evidence_required,execution_evidence_contract,real_execution_toolchain_json,summary,next_action,provider_id,capability_id",
            "public"
        },
        {
            "rag.query",
            "codex-lan-agent",
            "rag_query_analysis",
            "rag query analysis returned",
            "ok,exit_code,status,mode,query,analysis_only,review_analysis_only,execution_capability,implicit_evidence_lookup,evidence_injection_template,evidence_injection_used,evidence_task_id,evidence_result_ref,evidence_evidence_ref,evidence_resolved_log_path,evidence_log_excerpt_chars,evidence_source_excerpt_chars,local_chat_endpoint_primary,local_chat_endpoint_fallback,local_chat_endpoint_effective,local_chat_endpoint_mode,local_chat_endpoint_policy,trusted_execution_evidence_required,execution_evidence_contract,real_execution_toolchain_json,fallback_mode,project_summary_contract,provider_recovery_hint,evidence_input_mode,summary,direct_answer,source_refs,evidence_lines,confidence,insufficient_context,next_action,result_ref,evidence_ref,log_path,body,result,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_execute_semantic_action",
            "codex-lan-agent",
            "semantic_action_execution_bridge",
            "semantic action executed through the real MCP bridge",
            "ok,exit_code,status,action_id,bridge_action_id,bridge_tool_name,bridge_tool_arguments_json,bridge_status,tool_call_ready,dry_run_injected,real_execution_bridge,execution_capability,trusted_execution_evidence_required,execution_evidence_contract,task_id,task_ref,result_ref,evidence_ref,resolved_log_path,patch_id,log_path,summary,next_action,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_discover_ctest_tests",
            "codex-lan-agent",
            "ctest_discovery",
            "ctest discovery returned",
            "ok,exit_code,build_dir,config,test_count,test_names,semantic_outcome,result_ref,evidence_ref",
            "public"
        },
        {
            "lan_agent_preflight_build_target",
            "codex-lan-agent",
            "build_preflight",
            "build preflight returned",
            "ok,exit_code,status,build_dir,target,config,preflight_scope,preflight_status,preflight_reason_code,preflight_ref,preflight_tool_name,preflight_contract,summary,next_action,encoding_warning,encoding_contract,raw_request_encoding_probe,raw_request_contains_non_ascii,encoding_probe_scope,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_preflight_run_ctest_target",
            "codex-lan-agent",
            "ctest_preflight",
            "ctest preflight returned",
            "ok,exit_code,status,build_dir,test_regex,config,preflight_scope,preflight_status,preflight_reason_code,preflight_ref,preflight_tool_name,preflight_contract,semantic_outcome,test_count,result_ref,evidence_ref,summary,next_action,encoding_warning,encoding_contract,raw_request_encoding_probe,raw_request_contains_non_ascii,encoding_probe_scope,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_build_target",
            "codex-lan-agent",
            "build_execution",
            "build target request handled",
            "ok,exit_code,status,task_id,build_dir,target,config,preflight_status,preflight_ref,build_target_stall_timeout_sec,build_target_stall_timeout_source,stall_timeout_sec_override,effective_stall_timeout_sec,effective_timeout_sec,semantic_outcome,result_ref,evidence_ref,resolved_log_path,summary,next_action,failure_mode,result_object,raw_request_encoding_probe,raw_request_contains_non_ascii,encoding_probe_scope,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_run_ctest_target",
            "codex-lan-agent",
            "ctest_execution",
            "ctest run request handled",
            "ok,exit_code,task_id,semantic_outcome,verification_ok,preflight_status,preflight_ref,preflight_ref_replay,preflight_ref_checksum,result_ref,evidence_ref,resolved_log_path,summary,next_action,failure_mode,result_object,raw_request_encoding_probe,raw_request_contains_non_ascii,encoding_probe_scope,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_get_task",
            "codex-lan-agent",
            "task_status",
            "task status returned",
            "ok,exit_code,status,task_id,task_type,summary,next_action,failure_mode,result_object,result_ref,evidence_ref,resolved_log_path,effective_timeout_sec,effective_stall_timeout_sec,execution_completion_reason,process_progress_signal,timeout_diagnostic,runtime_sec,quiet_sec_at_finish,heartbeat_count,process_output_observed,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_resolve_task_result",
            "codex-lan-agent",
            "task_result_reference",
            "task result reference resolved",
            "ok,exit_code,status,task_id,task_ref,task_log_ref,summary,next_action,failure_mode,result_object,result_ref,evidence_ref,resolved_result_ref,resolved_evidence_ref,resolved_log_path,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_clips_decide",
            "codex-lan-agent",
            "clips_rule_decision",
            "clips rule decision returned",
            "ok,exit_code,status,result,decision_result_status,decision_domain,target_tool_name,decision,target_verification,verification,reason_code,matched_rule,next_action,route_target,route_arguments_json_ref,route_arguments_json_available,route_arguments_json_transport",
            "public"
        },
        {
            "lan_agent_clips_chain_template",
            "codex-lan-agent",
            "clips_chain_template",
            "clips chain template returned",
            "ok,exit_code,status,target_tool_name,chain_phase,chain_template_id,request_type,risk,safety_class,execution_class,evidence_policy,clips_required,rule_namespace,input_fact,template_contract,next_action,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_mcp_flow_visualize",
            "codex-lan-agent",
            "mcp_flow_visualize",
            "MCP flow graph generated",
            "ok,exit_code,status,result,input_jsonl,out_dir,event_count,tool_call_count,tool_result_count,violation_count,completion_state,current_node,next_expected_node,flow_events_jsonl_path,flow_graph_dot_path,flow_graph_mermaid_path,flow_state_json_path,flow_state_graph_dot_path,flow_state_graph_mermaid_path,flow_state_dashboard_html_path,flow_report_md_path,violations_md_path,result_ref,script_stdout_ref,stderr_ref,summary,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_mcp_flow_analyze",
            "codex-lan-agent",
            "mcp_flow_analyze",
            "MCP flow analysis report generated",
            "ok,exit_code,status,result,input_jsonl,rule_root,out_dir,conclusion,event_count,tool_call_count,tool_result_count,violation_count,completion_state,current_node,next_expected_node,acceptance_summary_json_path,index_md_path,rules_report_md_path,rules_fact_graph_dot_path,flow_report_md_path,flow_graph_dot_path,flow_graph_mermaid_path,flow_state_json_path,flow_state_graph_dot_path,flow_state_graph_mermaid_path,flow_state_dashboard_html_path,violations_md_path,result_ref,script_stdout_ref,stderr_ref,summary,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_mcp_flow_export",
            "codex-lan-agent",
            "mcp_flow_export",
            "MCP flow HTML report exported",
            "ok,exit_code,status,result,input_jsonl,rule_root,out_dir,conclusion,event_count,tool_call_count,tool_result_count,violation_count,completion_state,current_node,next_expected_node,html_report_path,artifact_bundle_dir,index_md_path,flow_report_md_path,flow_graph_dot_path,flow_graph_mermaid_path,flow_state_json_path,flow_state_graph_dot_path,flow_state_graph_mermaid_path,flow_state_dashboard_html_path,violations_md_path,result_ref,script_stdout_ref,stderr_ref,summary,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_mcp_boundary_explore",
            "codex-lan-agent",
            "mcp_boundary_explore",
            "MCP boundary exploration completed",
            "ok,exit_code,status,result,out_dir,case_count,pass_count,fail_count,accepted,conclusion,boundary_cases_jsonl_path,boundary_summary_json_path,rule_candidates_md_path,synthetic_jsonl_path,synthetic_flow_status,synthetic_flow_completion_state,synthetic_flow_violation_count,synthetic_flow_state_dashboard_html_path,result_ref,evidence_ref,summary,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_mcp_flow_conformance_check",
            "codex-lan-agent",
            "mcp_flow_conformance_check",
            "MCP flow conformance checked",
            "ok,exit_code,status,result,input_jsonl,out_dir,conformance_pass,conclusion,event_count,tool_call_count,tool_result_count,violation_count,completion_state,current_node,next_expected_node,flow_events_jsonl_path,flow_graph_dot_path,flow_graph_mermaid_path,flow_state_json_path,flow_state_graph_dot_path,flow_state_graph_mermaid_path,flow_state_dashboard_html_path,flow_report_md_path,violations_md_path,result_ref,script_stdout_ref,stderr_ref,summary,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_mcp_guard_regression_acceptance",
            "codex-lan-agent",
            "mcp_guard_regression_acceptance",
            "MCP guard regression acceptance completed",
            "ok,exit_code,status,result,out_dir,input_jsonl,accepted,conclusion,boundary_case_count,boundary_pass_count,boundary_fail_count,synthetic_flow_violation_count,flow_conformance_pass,flow_violation_count,flow_state_dashboard_html_path,boundary_summary_json_path,boundary_cases_jsonl_path,rule_candidates_md_path,result_ref,evidence_ref,summary,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_freeze",
            "codex-lan-agent",
            "task_memory_write",
            "task memory freeze returned",
            "ok,exit_code,status,record_model,goal_id,trace_id,resume_context_path,completion_claim_allowed,terminal_state,task_done,continue_required,final_answer_allowed,verification_ok,clean_chat_close_allowed,conversation_close_status,chat_context_reset_required,chat_context_reset_requested,chat_context_reset_acknowledged,host_chat_history_mutable_by_mcp,old_context_dropped,mcp_continuation_ready,new_chat_entry_tool_name,new_chat_entry_arguments_json,new_chat_resume_instruction,required_tool_name,required_tool_arguments_json,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_append_step",
            "codex-lan-agent",
            "task_memory_write",
            "task memory append step returned",
            "ok,exit_code,status,record_model,goal_id,trace_id,step_index,step_ledger_path,resume_context_path,completion_claim_allowed,terminal_state,task_done,assistant_response_allowed,final_answer_allowed,verification_ok,must_continue_until,clean_chat_close_allowed,conversation_close_allowed,conversation_close_status,handoff_state,handoff_completion_claim,next_chat_status_check_required,next_chat_status_check_tool_name,next_chat_status_check_arguments_json,next_chat_must_verify_fields_json,new_chat_entry_tool_name,new_chat_entry_arguments_json,new_chat_resume_instruction,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_execute_continuation_budget",
            "codex-lan-agent",
            "task_memory_write",
            "task memory continuation budget returned",
            "ok,exit_code,status,record_model,goal_id,trace_id,budget_run_id,budget_status,execution_mode,dry_run,execute_requested,execution_deferred,max_steps,planned_step_count,executed_step_count,budget_exhausted,last_verified_step,last_tool,last_status,last_result_ref,last_summary,last_verification_ok,directory_scope_active,directory_manifest_path,directory_current_file_index,directory_next_file_index,directory_total_code_file_count,directory_remaining_code_file_count,directory_scope_incomplete,directory_next_probe_call_json,resume_context_path,budget_plan_path,step_ledger_path,completion_claim_allowed,terminal_state,task_done,continue_required,auto_continue_required,assistant_response_allowed,final_answer_allowed,verification_ok,must_continue_until,clean_chat_close_allowed,conversation_close_allowed,conversation_close_status,chat_context_reset_required,chat_context_reset_requested,chat_context_reset_acknowledged,host_chat_history_mutable_by_mcp,old_context_dropped,mcp_continuation_ready,handoff_state,handoff_completion_claim,next_chat_status_check_required,next_chat_status_check_tool_name,next_chat_status_check_arguments_json,next_chat_must_verify_fields_json,new_chat_entry_tool_name,new_chat_entry_arguments_json,new_chat_resume_instruction,budget_requires_frozen_resume_context,next_call_json,interaction_continuation_mode,internal_next_call_hidden,required_tool_name,required_tool_arguments_json,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_resume_and_execute",
            "codex-lan-agent",
            "task_memory_write",
            "task memory fresh-chat resume execution returned",
            "ok,exit_code,status,record_model,goal_id,trace_id,resume_execute_entry,budget_status,execution_mode,max_steps,executed_step_count,budget_exhausted,last_verified_step,last_tool,last_status,last_summary,resume_context_path,completion_claim_allowed,terminal_state,task_done,continue_required,final_answer_allowed,verification_ok,clean_chat_close_allowed,conversation_close_status,chat_context_reset_required,chat_context_reset_requested,chat_context_reset_acknowledged,host_chat_history_mutable_by_mcp,old_context_dropped,mcp_continuation_ready,new_chat_entry_tool_name,new_chat_entry_arguments_json,new_chat_resume_instruction,budget_requires_frozen_resume_context,resume_recovery_status,required_tool_name,required_tool_arguments_json,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_new_chat_round_selftest",
            "codex-lan-agent",
            "task_memory_write",
            "task memory new chat round selftest returned",
            "ok,exit_code,status,record_model,result,summary,goal_id,trace_id,mcp_conversation_id,mcp_round_id,new_chat_round_id,new_chat_round_mode,conversation_owner,round_owner,execution_owner,state_owner,llama_cpp_role,llama_cpp_execution_participation,remote_session_required,mcp_conversation_round_established,round_manifest_path,host_chat_history_mutable_by_mcp,chat_context_reset_required,chat_context_reset_acknowledged,old_context_dropped,mcp_context_independence_verified,fresh_entry_tool_name,fresh_entry_arguments_scope,freeze_status,freeze_resume_context_path,resume_status,resume_budget_status,executed_step_count,terminal_state,completion_claim_allowed,final_answer_allowed,verification_ok,comment_removed,selftest_pass,sample_path,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_build_kv_snapshot",
            "codex-lan-agent",
            "task_memory_write",
            "task memory kv snapshot returned",
            "ok,exit_code,status,record_model,goal_id,task_memory_root,kv_backend,kv_snapshot_dir,kv_index_path,kv_manifest_path,record_count,step_record_count,slice_record_count,budget_record_count,rocksdb_status,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_kv_lookup",
            "codex-lan-agent",
            "task_memory_read",
            "task memory kv lookup returned",
            "ok,exit_code,status,record_model,goal_id,lookup_key,prefix_match,kv_backend,kv_index_path,limit,offset,matched_count,returned_count,has_more,next_offset,matches_jsonl,include_value,value_ref,value_text,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_rocksdb_mirror",
            "codex-lan-agent",
            "task_memory_write",
            "task memory RocksDB native mirror returned",
            "ok,exit_code,status,record_model,goal_id,task_memory_root,source_of_truth,native_backend_role,rocksdb_status,rocksdb_path,rocksdb_manifest_path,kv_index_path,kv_manifest_path,kv_record_count,mirrored_count,mirror_complete,safe_to_replace_source_of_truth,compile_required,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_rocksdb_lookup",
            "codex-lan-agent",
            "task_memory_read",
            "task memory RocksDB native lookup returned",
            "ok,exit_code,status,record_model,goal_id,task_memory_root,lookup_key,prefix_match,kv_backend,source_of_truth,native_backend_role,rocksdb_status,rocksdb_path,limit,offset,matched_count,returned_count,has_more,next_offset,matches_jsonl,include_value,value_ref,value_text,compile_required,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_rocksdb_parity_check",
            "codex-lan-agent",
            "task_memory_read",
            "task memory RocksDB parity check returned",
            "ok,exit_code,status,record_model,goal_id,lookup_key,source_of_truth,native_backend_role,file_lookup_ok,rocksdb_lookup_ok,file_matched_count,rocksdb_matched_count,file_matches_hash,rocksdb_matches_hash,parity_ok,safe_to_replace_source_of_truth,rocksdb_status,semantic_outcome,next_action,result_ref,evidence_ref,file_error,rocksdb_error,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_migration_assess",
            "codex-lan-agent",
            "task_memory_read",
            "task memory migration assessment returned",
            "ok,exit_code,status,record_model,goal_id,task_memory_root,migration_stage,adaptation_decision,active_backend,source_of_truth,backend_order,file_object_ready,migration_bundle_ready,kv_snapshot_ready,kv_contract_ready,rocksdb_native_ready,rocksdb_status,rocksdb_path,rocksdb_manifest_path,rocksdb_mirrored_count,safe_to_enable_rocksdb_adapter,safe_to_replace_source_of_truth,ready_file_count,required_file_count,ready_migration_file_count,required_migration_file_count,missing_file_objects_csv,missing_migration_files_csv,kv_record_count,step_record_count,slice_record_count,budget_record_count,resume_context_path,step_ledger_path,slices_path,index_manifest_path,kv_index_path,kv_manifest_path,terminal_state,completion_claim_allowed,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_structure_manifest",
            "codex-lan-agent",
            "task_memory_write",
            "task memory structure manifest returned",
            "ok,exit_code,status,record_model,goal_id,structure_version,task_memory_root,memory_structure_path,source_of_truth,active_read_backend,write_backend,native_backend_role,read_backend_order,required_model_read,structure_ready,fresh_model_bootstrap_ready,backend_policy_ready,safe_to_replace_source_of_truth,parity_required_for_native_reads,migration_bundle_ready,kv_snapshot_ready,rocksdb_native_ready,rocksdb_status,rocksdb_manifest_path,rocksdb_path,task_memory_path,step_ledger_path,slices_path,index_manifest_path,resume_context_path,evidence_refs_dir,budget_runs_dir,kv_snapshot_dir,kv_index_path,kv_manifest_path,step_count,slice_count,budget_file_count,evidence_file_count,kv_record_count,rocksdb_mirrored_count,terminal_state,completion_claim_allowed,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_migration_acceptance",
            "codex-lan-agent",
            "task_memory_write",
            "task memory migration acceptance returned",
            "ok,exit_code,status,record_model,goal_id,trace_id,migration_acceptance_status,acceptance_status,semantic_outcome,sample_path,output_dir,summary_path,memory_structure_path,resume_context_path,kv_index_path,rocksdb_path,rocksdb_manifest_path,source_of_truth,active_read_backend,write_backend,safe_to_replace_source_of_truth,parity_required_for_native_reads,partial_budget_terminal_state,partial_budget_completion_claim_allowed,final_budget_terminal_state,kv_record_count,rocksdb_mirrored_count,validated_chain,completion_claim_allowed,final_answer_allowed,next_action,result_ref,evidence_ref,failed_stage,failed_field,expected_value,actual_value,failed_step_error,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_task_memory_resume_context",
            "codex-lan-agent",
            "task_memory_read",
            "task memory resume context returned",
            "ok,exit_code,status,record_model,goal_id,task_memory_root,resume_context_path,resume_context,step_ledger_path,slices_path,index_manifest_path,migration_handover_path,completion_claim_allowed,terminal_state,final_answer_allowed,verification_ok,clean_chat_close_allowed,conversation_close_allowed,conversation_close_status,handoff_completion_claim,next_chat_status_check_required,next_chat_status_check_tool_name,next_chat_status_check_arguments_json,next_chat_must_verify_fields_json,new_chat_entry_tool_name,new_chat_entry_arguments_json,project_flow_role,single_round_flow,slice_execution_policy,model_context_policy,current_tool,next_call_json,compact_summary,remaining_work,next_allowed_action,semantic_outcome,next_action,result_ref,evidence_ref,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_probe_text_file",
            "codex-lan-agent",
            "file_probe",
            "text file probe returned",
            "ok,exit_code,status,result,summary,error,error_code,error_message,failure_mode,file_path,current_file_path,normalized_path,primary_intent,normalized_primary_intent,directory_scope_active,directory_manifest_path,directory_current_file_index,directory_next_file_index,directory_total_code_file_count,directory_remaining_code_file_count,directory_scope_incomplete,directory_next_probe_call_json,file_bytes,total_bytes,total_lines,line_count,returned_lines,remaining_lines,probe_mode,probe_complete,probe_ref,probe_ready,analysis_allowed,file_length_class,content_payload_format,content_payload_scope,content_payload_boundary_safe,structured_body_read_mode,structured_body_helper_bypassed,pagination_basis,recommended_next_tool,recommended_read_max_lines,recommended_scan_mode,next_call_json,next_tool_name,next_action,task_completion,continue_required,auto_continue_required,read_complete,file_complete,has_more,result_ref,evidence_ref,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_read_text_file",
            "codex-lan-agent",
            "file_read",
            "paged file read returned",
            "ok,exit_code,status,result,summary,file_path,current_file_path,normalized_path,start_line,end_line,total_lines,remaining_lines,has_more,file_complete,read_complete,task_completion,continue_required,auto_continue_required,analysis_allowed,probe_ref,probe_ready,content_text,content_payload_format,content_payload_scope,content_payload_boundary_safe,structured_body_read_mode,structured_body_helper_bypassed,pagination_basis,start_byte_offset,next_byte_offset,returned_bytes,total_bytes,remaining_bytes,effective_page_byte_limit,next_start_line,next_call_json,result_ref,evidence_ref,semantic_model_clamp,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,terminal_state,completion_claim_allowed,must_continue_until,completion_guard,required_next_action_type,required_tool_name,required_tool_arguments_json,local_ai_guidance_enforced,local_ai_required_first_tool,local_ai_uncertain_route_tool,local_ai_completion_gate,clips_gate,clips_pre_call_tool_decision,clips_pre_call_tool_reason_code,clips_pre_call_tool_next_action,clips_pre_call_tool_matched_rule,verification_status,verification_ok,supervision_alarm,supervision_alarm_code,supervision_alarm_message,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_read_directory_files",
            "codex-lan-agent",
            "directory_file_read",
            "directory file batch read returned",
            "ok,exit_code,status,result,summary,directory_path,normalized_directory_path,file_extensions_csv,matched_file_count,current_file_index,current_file_path,next_file_index,next_file_path,start_line,end_line,total_lines,has_more,file_complete,read_complete,task_completion,continue_required,auto_continue_required,analysis_allowed,content_text,content_payload_format,content_payload_scope,content_payload_boundary_safe,pagination_basis,start_byte_offset,next_byte_offset,returned_bytes,total_bytes,remaining_bytes,effective_page_byte_limit,directory_complete,directory_listing_complete,known_file_list_complete,batch_manifest_complete,content_read_completion,incomplete_scope,continuation_status,next_start_line,next_batch_file_path,batch_completion,remaining_batch_file_count,batch_read_file_count,next_call_json,result_ref,evidence_ref,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_prepare_directory_analysis",
            "codex-lan-agent",
            "directory_analysis_bundle",
            "directory analysis bundle returned",
            "ok,exit_code,status,result,summary,directory_path,normalized_path,file_extensions_csv,matched_file_count,excerpt_file_count,truncated_file_count,omitted_file_count,total_excerpt_lines,max_excerpt_lines_per_file,max_total_excerpt_lines,max_excerpt_chars,source_excerpt_chars,content_text,content_payload_format,content_payload_scope,content_payload_boundary_safe,analysis_bundle_ref,analysis_bundle_contract,directory_listing_complete,known_file_list_complete,analysis_allowed,task_completion,batch_completion,result_ref,evidence_ref,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_scan_text_ranges",
            "codex-lan-agent",
            "text_range_scan",
            "text range scan returned",
            "ok,exit_code,status,result,summary,error,error_code,error_message,failure_mode,next_action,file_path,normalized_path,primary_intent,scan_mode,total_lines,total_range_count,returned_range_count,range_offset,max_ranges_per_call,next_range_offset,has_more,task_completion,step_completion,continue_required,auto_continue_required,analysis_allowed,single_step_required,operation_granularity,max_items_per_call,batch_mutation_allowed,window_batch_scope,effective_window_policy,probe_ref,probe_ready,scan_result_ref,result_ref,evidence_ref,cache_hit,semantic_model_clamp,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,terminal_state,completion_claim_allowed,completion_guard,required_next_action_type,required_tool_name,required_tool_arguments_json,local_ai_guidance_enforced,local_ai_required_first_tool,local_ai_uncertain_route_tool,local_ai_completion_gate,clips_gate,clips_pre_call_tool_decision,clips_pre_call_tool_reason_code,clips_pre_call_tool_next_action,clips_pre_call_tool_matched_rule,verification_status,verification_ok,supervision_alarm,supervision_alarm_code,supervision_alarm_message,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_delete_next_text_range_atomic",
            "codex-lan-agent",
            "file_write",
            "next text range delete returned",
            "ok,exit_code,status,result,summary,error,error_code,error_message,failure_mode,next_action,file_path,normalized_path,primary_intent,scan_mode,probe_ref,probe_ready,range_index,range_kind,range_start_line,range_end_line,delete_mode,deleted_text_bytes,total_lines_before,total_lines_after,total_range_count_before,total_range_count_after,has_more,write_applied,write_verified,disk_write_completed,verification_status,verification_ok,task_completion,continue_required,terminal_state,task_done,completion_claim_allowed,completion_guard,single_step_required,operation_granularity,max_items_per_call,batch_mutation_allowed,server_side_optimized_step,result_ref,evidence_ref,semantic_model_clamp,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,required_next_action_type,required_tool_name,required_tool_arguments_json,task_execution_in_mcp_required,forced_task_memory_execution,long_loop_budget_recommended,long_loop_freeze_tool_name,long_loop_budget_tool_name,long_loop_budget_precondition,long_loop_budget_policy,clips_continuation_required,clips_post_result_decision,clips_post_result_verification,clips_post_result_reason_code,trace_id,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_delete_text_range_window_atomic",
            "codex-lan-agent",
            "file_write",
            "bounded text range window delete returned",
            "ok,exit_code,status,result,summary,error,error_code,error_message,failure_mode,next_action,file_path,normalized_path,primary_intent,scan_mode,probe_ref,probe_ready,directory_scope_active,directory_manifest_path,directory_current_file_index,directory_next_file_index,directory_total_code_file_count,directory_remaining_code_file_count,directory_scope_incomplete,directory_next_probe_call_json,start_line,window_start_line,window_end_line,max_lines,max_lines_per_call,effective_window_policy,total_lines_before,total_lines_after,total_range_count_before,total_range_count_after,window_range_count_before,boundary_range_count,deleted_range_count,deleted_text_bytes,has_range_in_current_window_after,has_more,next_start_line,write_applied,write_verified,disk_write_completed,verification_status,verification_ok,task_completion,continue_required,terminal_state,task_done,completion_claim_allowed,completion_guard,window_step_required,operation_granularity,max_items_per_call,batch_mutation_allowed,window_batch_scope,server_side_optimized_step,result_ref,evidence_ref,semantic_model_clamp,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,required_next_action_type,required_tool_name,required_tool_arguments_json,task_execution_in_mcp_required,forced_task_memory_execution,long_loop_budget_recommended,long_loop_freeze_tool_name,long_loop_budget_tool_name,long_loop_budget_precondition,long_loop_budget_policy,clips_continuation_required,clips_post_result_decision,clips_post_result_verification,clips_post_result_reason_code,trace_id,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_prepare_edit_windows",
            "codex-lan-agent",
            "edit_window_bundle",
            "edit window bundle returned",
            "ok,exit_code,status,result,summary,error,error_code,error_message,failure_mode,next_action,file_path,normalized_path,primary_intent,scan_mode,total_lines,total_window_count,returned_window_count,window_offset,requested_max_windows_per_call,max_windows_per_call,context_before,context_after,max_window_chars,windows_json,has_more,has_more_after_current_step,next_window_offset,task_completion,step_completion,continue_required,auto_continue_required,analysis_allowed,single_step_required,operation_granularity,max_items_per_call,batch_mutation_allowed,window_batch_scope,effective_window_policy,probe_ref,probe_ready,content_text,content_payload_format,content_payload_scope,content_payload_boundary_safe,edit_window_bundle_ref,edit_window_contract,step_contract,next_call_json,result_ref,evidence_ref,semantic_model_clamp,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,terminal_state,completion_claim_allowed,must_continue_until,completion_guard,required_next_action_type,required_tool_name,required_tool_arguments_json,local_ai_guidance_enforced,local_ai_required_first_tool,local_ai_uncertain_route_tool,local_ai_completion_gate,clips_gate,clips_pre_call_tool_decision,clips_pre_call_tool_reason_code,clips_pre_call_tool_next_action,clips_pre_call_tool_matched_rule,verification_status,verification_ok,supervision_alarm,supervision_alarm_code,supervision_alarm_message,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_find_line_metadata",
            "codex-lan-agent",
            "line_metadata",
            "line metadata returned",
            "ok,exit_code,status,result,summary,file_path,normalized_path,line,show_preview,operation,line_hash,line_length,preview,content_text,content_payload_format,content_payload_scope,content_payload_boundary_safe,result_ref,evidence_ref,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_find_content_matches",
            "codex-lan-agent",
            "content_match_metadata",
            "content match metadata returned",
            "ok,exit_code,status,result,summary,file_path,normalized_path,anchor_text,query_text,show_preview,fuzzy_threshold,operation,match_type,match_count,is_unique,first_match_line,first_match_hash,first_match_preview,matches_json,message,content_text,content_payload_format,content_payload_scope,content_payload_boundary_safe,result_ref,evidence_ref,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_locate_text_lines",
            "codex-lan-agent",
            "locate_text_lines",
            "text locate metadata returned",
            "ok,exit_code,status,result,summary,file_path,normalized_path,anchor_text,show_preview,fuzzy_threshold,operation,match_type,match_count,is_unique,first_match_line,first_match_hash,first_match_preview,matches_json,message,content_text,content_payload_format,content_payload_scope,content_payload_boundary_safe,result_ref,evidence_ref,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_delete_line_atomic",
            "codex-lan-agent",
            "file_write",
            "line delete returned",
            "ok,exit_code,status,result,summary,file_path,normalized_path,line,expected_line_hash,operation,deleted_line_hash_before,new_hash,written_text_bytes,write_applied,write_verified,disk_write_completed,final_write_tool,content_text,content_payload_format,content_payload_scope,content_payload_boundary_safe,result_ref,evidence_ref,request_id,trace_id,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_delete_content_atomic",
            "codex-lan-agent",
            "file_write",
            "content delete returned",
            "ok,exit_code,status,result,summary,error,error_code,error_message,failure_mode,next_action,file_path,normalized_path,anchor_text,occurrence,expected_anchor_hash,operation,deleted_line,deleted_line_hash_before,old_hash,new_hash,before_anchor_occurrence_count,after_anchor_occurrence_count,written_text_bytes,write_applied,write_verified,disk_write_completed,final_write_tool,recovered_after_helper_failure,recovery_reason,helper_exit_code,helper_error,readback_error,verification_status,verification_ok,content_text,content_payload_format,content_payload_scope,content_payload_boundary_safe,result_ref,evidence_ref,request_id,trace_id,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_insert_after_anchor_atomic",
            "codex-lan-agent",
            "file_write",
            "anchor insert returned",
            "ok,exit_code,status,result,summary,file_path,normalized_path,anchor_text,occurrence,expected_anchor_hash,anchor_line,insert_start_line,insert_line_count,anchor_line_hash_before,new_hash,written_text_bytes,write_applied,write_verified,disk_write_completed,final_write_tool,content_transport,content_base64_bytes,content_text,result_ref,evidence_ref,request_id,trace_id,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_replace_line_range_atomic",
            "codex-lan-agent",
            "file_write",
            "line range replace returned",
            "ok,exit_code,status,result,summary,file_path,normalized_path,start_line,end_line,expected_range_hash,replacement_line_count,range_hash_before,new_hash,written_text_bytes,write_applied,write_verified,disk_write_completed,final_write_tool,content_transport,content_base64_bytes,content_text,result_ref,evidence_ref,request_id,trace_id,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_format_code_file",
            "codex-lan-agent",
            "code_format",
            "code format returned",
            "ok,exit_code,status,result,summary,error,source_file,normalized_path,dry_run,would_change,changed,format_apply_required,dry_run_only,style,fallback_style,formatter,formatter_path,formatter_source,old_hash,new_hash,formatted_hash,original_bytes,formatted_bytes,write_applied,write_verified,disk_write_completed,backup_path,log_path,required_next_action_type,required_tool_name,required_tool_arguments_json,continue_required,auto_continue_required,semantic_model_clamp,completion_guard,next_action,terminal_state,task_done,completion_claim_allowed,final_answer_allowed,verification_ok,result_ref,evidence_ref,request_id,trace_id,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_list_cxparser_flows",
            "codex-lan-agent",
            "cxparser_flow_catalog",
            "cxparser flow catalog returned",
            "ok,exit_code,status,flow_count,runtime_flow_count,runtime_binding_ready_count,flow_ids_json,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_validate_cxparser_flow",
            "codex-lan-agent",
            "cxparser_flow_validation",
            "cxparser flow validation returned",
            "ok,exit_code,status,flow_id,flow_registered,backend_kind,safety_class,entry_script,params_json,runtime_binding_available,runtime_binding_source,runtime_binding_entrypoint,runtime_binding_contract,cxparser_public_build_root,cxparser_public_build_contract,result,summary,error,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_run_cxparser_flow",
            "codex-lan-agent",
            "cxparser_flow_execution",
            "cxparser flow step returned",
            "ok,exit_code,status,result,summary,error,flow_id,test_statement,test_input_mode,flow_resolution,cxparser_status,cxparser_flow_registered,cxparser_backend_kind,backend_kind,cxparser_safety_class,flow_safety_class,cxparser_entry_script,runtime_binding_available,runtime_binding_source,runtime_binding_entrypoint,runtime_binding_contract,cxparser_public_build_root,cxparser_public_build_contract,effective_timeout_sec,effective_stall_timeout_sec,effective_timeout_scope,task_id,task_type,result_ref,evidence_ref,resolved_log_path,current_file_path,file_complete,has_more,read_complete,task_completion,continue_required,auto_continue_required,analysis_allowed,batch_completion,remaining_batch_file_count,continuation_status,next_file_index,next_start_line,next_file_path,next_batch_file_path,next_call_json,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_list_directory",
            "codex-lan-agent",
            "directory_list",
            "directory listing returned",
            "ok,exit_code,status,result,summary,directory_path,normalized_path,total_entries,file_count,code_file_count,remaining_code_file_count,directory_count,entry_labels_json,file_paths_json,file_names_json,response_preview_truncated,file_paths_total_count,file_names_total_count,directory_complete,directory_listing_complete,known_file_list_complete,batch_manifest_complete,batch_manifest_path,batch_total_files,batch_read_file_count,remaining_batch_file_count,batch_completion,content_read_completion,incomplete_scope,next_batch_file_path,next_tool_name,next_file_path,next_max_lines,next_call_json,required_tool_name,required_tool_arguments_json,task_completion,continue_required,auto_continue_required,analysis_allowed,analysis_blocked_reason,flow_id,flow_task_list_required,flow_current_task_id,flow_next_task_id,flow_task_list_path,flow_task_list_md_path,result_ref,evidence_ref,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_apply_single_file_patch",
            "codex-lan-agent",
            "single_file_patch",
            "single file patch apply returned",
            "ok,exit_code,patch_id,file_path,requested_file_path,normalized_path,applied_target_file,patch_execution_mode,write_applied,write_verified,disk_write_completed,final_write_tool,diff_hash,diff_changed_line_count,diff_inline_omitted,diff_inline_omitted_reason,diff_bytes,changed,old_hash,new_hash,applied_hash,applied_hash_match,diff_applied_effective,backup_path,log_path,result,summary,error,next_action,path_guard_decision,path_guard_reason,workspace_guard,verification_ok,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,pre_guard_status,pre_guard_reason_code,pre_guard_next_action,pre_guard_route_target,pre_guard_blocked,post_guard_status,post_guard_decision,post_guard_reason_code,post_guard_next_action,post_guard_result_valid,acceptance_status,acceptance_reason,acceptance_next_action_available,supervision_alarm,supervision_alarm_code,supervision_alarm_message,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_apply_diff_patch",
            "codex-lan-agent",
            "single_file_patch",
            "single file diff patch apply returned",
            "ok,exit_code,patch_id,file_path,requested_file_path,resolved_file_path,target_resolution_reason,target_resolution_used,target_resolution_required,auto_target_resolution_used,auto_target_resolution_reason,candidate_file_count,candidate_file_paths_json,touched_diff_paths_json,diff_old_path,diff_new_path,normalized_path,applied_target_file,patch_backend_primary,patch_backend_effective,git_apply_attempted,git_apply_completed,git_apply_fallback_reason,git_apply_check_exit_code,git_apply_exit_code,git_apply_check_log_path,git_apply_log_path,git_repo_root,git_patch_file_path,patch_execution_mode,diff_parse_status,diff_apply_mode,fuzzy_context_used,fallback_strategy,write_applied,write_verified,disk_write_completed,final_write_tool,diff_hash,input_diff_hash,diff_changed_line_count,diff_inline_omitted,diff_inline_omitted_reason,diff_bytes,changed,old_hash,new_hash,applied_hash,applied_hash_match,diff_applied_effective,diff_write_contract,backup_path,log_path,result,summary,error,next_action,path_guard_decision,path_guard_reason,workspace_guard,write_mode,patch_format,verification_ok,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,pre_guard_status,pre_guard_reason_code,pre_guard_next_action,pre_guard_route_target,pre_guard_blocked,post_guard_status,post_guard_decision,post_guard_reason_code,post_guard_next_action,post_guard_result_valid,acceptance_status,acceptance_reason,acceptance_next_action_available,supervision_alarm,supervision_alarm_code,supervision_alarm_message,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_write_text_file",
            "codex-lan-agent",
            "file_write",
            "text file write returned",
            "ok,exit_code,file_path,normalized_path,append,file_existed,content_transport,content_base64_bytes,written_text_bytes,old_bytes,final_bytes,old_hash,new_hash,applied_hash,applied_hash_match,write_applied,write_verified,disk_write_completed,log_path,result,next_action,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,pre_guard_status,post_guard_status,post_guard_decision,post_guard_reason_code,acceptance_status,acceptance_reason,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_revert_single_file_patch",
            "codex-lan-agent",
            "single_file_patch_revert",
            "single file patch revert returned",
            "ok,exit_code,patch_id,file_path,normalized_path,backup_path,backup_bytes,final_bytes,applied_hash,applied_hash_match,write_applied,write_verified,disk_write_completed,log_path,result,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,pre_guard_status,post_guard_status,post_guard_decision,post_guard_reason_code,acceptance_status,acceptance_reason,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_ensure_directory",
            "codex-lan-agent",
            "directory_write",
            "directory ensure returned",
            "ok,exit_code,directory_path,file_path,ensure_parent,normalized_path,resolved_from,existed_before,exists_after,log_path,result,result_ref,evidence_ref,summary,next_action,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,pre_guard_status,post_guard_status,post_guard_decision,post_guard_reason_code,acceptance_status,acceptance_reason,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_preview_patch",
            "codex-lan-agent",
            "single_file_patch",
            "single file patch preview returned",
            "ok,exit_code,patch_id,file_path,requested_file_path,normalized_path,diff_hash,result,summary,error,next_action,path_guard_decision,path_guard_reason,workspace_guard,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,pre_guard_status,pre_guard_reason_code,pre_guard_next_action,pre_guard_route_target,pre_guard_blocked,post_guard_status,post_guard_decision,post_guard_reason_code,post_guard_next_action,post_guard_result_valid,acceptance_status,acceptance_reason,acceptance_next_action_available,supervision_alarm,supervision_alarm_code,supervision_alarm_message,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_get_patch_audit_trail",
            "codex-lan-agent",
            "patch_audit_trail",
            "patch audit trail returned",
            "ok,exit_code,patch_id,event_count,audit_event_path,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_get_trace_audit_trail",
            "codex-lan-agent",
            "trace_audit_trail",
            "trace audit trail returned",
            "ok,exit_code,trace_id,event_count,source_count,available_source_count,matched_source_count,audit_event_path,mcp_trace_audit_event_path,audit_sources,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_get_supervision_status",
            "codex-lan-agent",
            "goal_supervision_status",
            "goal supervision status returned",
            "ok,exit_code,trace_id,goal_id,clips_first_decision,clips_first_next_tool,clips_first_reason,event_count,trace_context_found,completion_state,can_continue,interruption_reason,interruption_stage,supervision_query_status,supervision_lookup_ok,supervision_decision,supervision_explanation,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,pre_guard_status,pre_guard_reason_code,pre_guard_next_action,pre_guard_route_target,pre_guard_blocked,post_guard_status,post_guard_decision,post_guard_reason_code,post_guard_next_action,post_guard_result_valid,acceptance_status,acceptance_reason,acceptance_next_action_available,supervision_alarm,supervision_alarm_code,supervision_alarm_message,progress_target_count,progress_completed_count,progress_pending_count,progress_failed_count,progress_skipped_count,next_actions_count,next_action_0_tool_name,next_action_0_safety_class,next_action_0_params_json,next_action_0_reason,next_action_0_source_rule,next_action_0_trace_id,next_action_0_goal_id,next_action_0_params_hash,result,summary,provider_id,capability_id",
            "public"
        },
        {
            "lan_agent_verify_single_file_patch",
            "codex-lan-agent",
            "patch_verification_pipeline",
            "patch verification returned",
            "ok,exit_code,patch_id,file_path,expected_hash,actual_hash,verification_ok,semantic_outcome,repair_candidate_id,repair_candidate_reason,next_action,result,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,pre_guard_status,pre_guard_reason_code,pre_guard_next_action,pre_guard_route_target,pre_guard_blocked,post_guard_status,post_guard_decision,post_guard_reason_code,post_guard_next_action,post_guard_result_valid,acceptance_status,acceptance_reason,acceptance_next_action_available,supervision_alarm,supervision_alarm_code,supervision_alarm_message,provider_id,capability_id",
            "public"
        }
    };
    return tools;
}

const ToolMeta * FindToolMeta(const std::string & tool_name) {
    for (const ToolMeta & tool : GetToolMetaCatalog()) {
        if (tool_name == tool.tool_name) {
            return &tool;
        }
    }
    return nullptr;
}

std::string ResolveResultFieldConfigPath(const AgentConfig & config) {
    if (!config.result_fields_config_path.empty()) {
        return config.result_fields_config_path;
    }
    return config.config_dir.empty()
        ? std::string()
        : (std::filesystem::path(config.config_dir) / "result_fields.cfg").string();
}

std::vector<std::string> SplitCommaSeparatedFields(const std::string & value) {
    std::vector<std::string> fields;
    std::string current;
    std::istringstream input(value);
    while (std::getline(input, current, ',')) {
        const std::string trimmed = Trim(current);
        if (!trimmed.empty()) {
            fields.push_back(trimmed);
        }
    }
    return fields;
}

void ApplyOverlayLine(
    const std::string & key,
    const std::string & value,
    ResultFieldOverlay * overlay) {
    if (overlay == nullptr) {
        return;
    }
    if (key == "required_fields") {
        overlay->required_fields = SplitCommaSeparatedFields(value);
    } else if (key == "public_fields") {
        overlay->public_fields = value;
    } else if (key == "log_safety") {
        overlay->log_safety = value;
    } else if (key.rfind("default.", 0) == 0) {
        const std::string field_key = Trim(key.substr(std::string("default.").size()));
        if (!field_key.empty()) {
            overlay->default_fields[field_key] = value;
        }
    }
}

ResultFieldOverlay LoadResultFieldOverlay(
    const std::string & path,
    const std::string & tool_name) {
    ResultFieldOverlay overlay;
    if (path.empty() || !std::filesystem::exists(path)) {
        return overlay;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        return overlay;
    }

    const std::string tool_prefix = "tool." + tool_name + ".";
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        const std::size_t separator = trimmed.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string raw_key = Trim(trimmed.substr(0, separator));
        const std::string raw_value = Trim(trimmed.substr(separator + 1));
        if (raw_key.rfind("default.", 0) == 0) {
            ApplyOverlayLine(raw_key, raw_value, &overlay);
            continue;
        }
        if (raw_key.rfind("tool.default.", 0) == 0) {
            ApplyOverlayLine(raw_key.substr(std::string("tool.default.").size()), raw_value, &overlay);
            continue;
        }
        if (raw_key.rfind(tool_prefix, 0) == 0) {
            ApplyOverlayLine(raw_key.substr(tool_prefix.size()), raw_value, &overlay);
        }
    }
    return overlay;
}

std::string ResolveResultEnvelopeStatus(const CommandResult & result) {
    const std::string verification = GetFieldOrDefault(result, "verification", "");
    const std::string verification_ok = GetFieldOrDefault(result, "verification_ok", "");
    if (verification == "not_verified" || verification_ok == "false") {
        return "INVALID";
    }
    if (!result.ok || result.exit_code != 0) {
        return "FAILED";
    }
    return "SUCCESS";
}

std::string ResolveFailureModeField(const CommandResult & result) {
    if (GetFieldOrDefault(result, "status", "") == "SUCCESS") {
        return "none";
    }
    return FirstNonEmpty(
        GetFieldOrDefault(result, "failure_mode", ""),
        GetFieldOrDefault(result, "preflight_reason_code", ""),
        GetFieldOrDefault(result, "pre_guard_reason_code", ""),
        GetFieldOrDefault(result, "post_guard_reason_code", ""),
        GetFieldOrDefault(result, "invalid_conclusion_reason", ""),
        GetFieldOrDefault(result, "error_code", ""),
        GetFieldOrDefault(result, "error", ""),
        "command_failed");
}

std::string BuildResultObjectJson(const CommandResult & result) {
    const std::vector<std::pair<std::string, std::string>> fields = {
        {"result", GetFieldOrDefault(result, "result", "")},
        {"status", GetFieldOrDefault(result, "status", "")},
        {"summary", GetFieldOrDefault(result, "summary", "")},
        {"failure_mode", GetFieldOrDefault(result, "failure_mode", "")},
        {"task_id", GetFieldOrDefault(result, "task_id", "")},
        {"result_ref", GetFieldOrDefault(result, "result_ref", "")},
        {"evidence_ref", GetFieldOrDefault(result, "evidence_ref", "")},
        {"resolved_log_path", GetFieldOrDefault(result, "resolved_log_path", "")},
        {"log_path", GetFieldOrDefault(result, "log_path", "")},
        {"next_action", GetFieldOrDefault(result, "next_action", "")}
    };
    std::ostringstream output;
    output << "{";
    bool first = true;
    for (const auto & field : fields) {
        if (field.second.empty()) {
            continue;
        }
        if (!first) {
            output << ",";
        }
        output << "\"" << codex_lan_agent::JsonEscape(field.first) << "\":\""
               << codex_lan_agent::JsonEscape(field.second) << "\"";
        first = false;
    }
    output << "}";
    return output.str();
}

CommandResult BuildPublicResultProjection(const CommandResult & result) {
    CommandResult projected;
    projected.ok = result.ok;
    projected.exit_code = result.exit_code;

    std::vector<std::string> public_fields =
        SplitCommaSeparatedFields(GetFieldOrDefault(result, "public_fields", ""));
    if (public_fields.empty()) {
        for (const auto & entry : result.fields) {
            public_fields.push_back(entry.first);
        }
    }

    std::unordered_set<std::string> allowed(public_fields.begin(), public_fields.end());
    allowed.insert("ok");
    allowed.insert("exit_code");

    for (const std::string & key : public_fields) {
        const std::string value = GetFieldOrDefault(result, key, "");
        if (!value.empty()) {
            projected.fields[key] = value;
        }
    }

    std::vector<std::string> deferred_fields;
    for (const auto & entry : result.fields) {
        if (entry.first == "content" || entry.first == "public_fields") {
            continue;
        }
        if (allowed.find(entry.first) == allowed.end() && !entry.second.empty()) {
            deferred_fields.push_back(entry.first);
        }
    }
    std::sort(deferred_fields.begin(), deferred_fields.end());
    projected.fields["audit_field_count"] = std::to_string(deferred_fields.size());
    projected.fields["audit_details_ref"] = FirstNonEmpty(
        GetFieldOrDefault(result, "result_ref", ""),
        GetFieldOrDefault(result, "evidence_ref", ""),
        GetFieldOrDefault(result, "resolved_log_path", ""),
        GetFieldOrDefault(result, "log_path", ""));
    projected.fields["audit_read_hint"] =
        deferred_fields.empty()
            ? "no deferred audit fields"
            : "read audit_details_ref for the full result envelope and deferred audit fields";
    if (!deferred_fields.empty()) {
        projected.fields["audit_fields_deferred"] = "true";
    }

    return projected;
}

std::string BuildResultEnvelopeHash(const CommandResult & result) {
    std::vector<std::pair<std::string, std::string>> entries;
    entries.reserve(result.fields.size());
    for (const auto & entry : result.fields) {
        if (entry.first == "result_hash") {
            continue;
        }
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const auto & left, const auto & right) {
        return left.first < right.first;
    });
    std::ostringstream serialized;
    serialized << "ok=" << (result.ok ? "true" : "false")
               << ";exit_code=" << result.exit_code;
    for (const auto & entry : entries) {
        serialized << ";" << entry.first << "=" << entry.second;
    }
    return StableContentChecksum(serialized.str());
}

void FinalizeResultEnvelope(
    const AgentConfig & config,
    const std::string & tool_name,
    CommandResult * result) {
    if (result == nullptr) {
        return;
    }

    const ToolMeta * meta = FindToolMeta(tool_name);
    const ResultEnvelopeConfig & envelope = GetDefaultResultEnvelopeConfig();
    result->fields["ok"] = result->ok ? "true" : "false";
    result->fields["exit_code"] = std::to_string(result->exit_code);
    result->fields["result_schema_id"] = envelope.schema_id;
    result->fields["schema_version"] = envelope.schema_id;
    result->fields["field_catalog_version"] = envelope.catalog_version;
    result->fields["tool_name"] = tool_name;
    result->fields["field_catalog_mode"] = "embedded";
    if (GetFieldOrDefault(*result, "request_id", "").empty()) {
        result->fields["request_id"] = "req-" + BuildRequestTimestampToken();
    }
    if (GetFieldOrDefault(*result, "trace_id", "").empty()) {
        result->fields["trace_id"] =
            "trace-" + SanitizeDispatchToken(tool_name, "tool") + "-" + BuildRequestTimestampToken();
    }
    if (GetFieldOrDefault(*result, "tool_call_id", "").empty()) {
        result->fields["tool_call_id"] =
            "toolcall-" + SanitizeDispatchToken(tool_name, "tool") + "-" + BuildRequestTimestampToken();
    }

    const std::string field_config_path = ResolveResultFieldConfigPath(config);
    const ResultFieldOverlay overlay = LoadResultFieldOverlay(field_config_path, tool_name);
    result->fields["result_fields_config_path"] = field_config_path;
    result->fields["result_fields_config_exists"] =
        (!field_config_path.empty() && std::filesystem::exists(field_config_path)) ? "true" : "false";
    result->fields["result_fields_config_effect"] =
        result->fields["result_fields_config_exists"] == "true"
            ? "external config detected; embedded catalog with overlay defaults is active in this build"
            : "using embedded field catalog";
    if (result->fields["result_fields_config_exists"] == "true") {
        result->fields["field_catalog_mode"] = "embedded_with_external_overlay";
    }

    if (meta != nullptr) {
        if (GetFieldOrDefault(*result, "provider_id", "").empty()) {
            result->fields["provider_id"] = meta->provider_id;
        }
        if (GetFieldOrDefault(*result, "capability_id", "").empty()) {
            result->fields["capability_id"] = meta->capability_id;
        }
        if (GetFieldOrDefault(*result, "summary", "").empty()) {
            result->fields["summary"] = meta->default_summary;
        }
        result->fields["public_fields"] = overlay.public_fields.empty() ? meta->public_fields : overlay.public_fields;
        result->fields["log_safety"] = overlay.log_safety.empty() ? meta->log_safety : overlay.log_safety;
    } else {
        if (!overlay.public_fields.empty()) {
            result->fields["public_fields"] = overlay.public_fields;
        }
        if (!overlay.log_safety.empty()) {
            result->fields["log_safety"] = overlay.log_safety;
        }
    }

    for (const auto & item : overlay.default_fields) {
        if (GetFieldOrDefault(*result, item.first, "").empty()) {
            result->fields[item.first] = item.second;
        }
    }

    if (GetFieldOrDefault(*result, "evidence_ref", "").empty()) {
        result->fields["evidence_ref"] = GetFieldOrDefault(*result, "log_path", "");
    }
    if (GetFieldOrDefault(*result, "result_ref", "").empty()) {
        result->fields["result_ref"] = GetFieldOrDefault(*result, "log_path", "");
    }
    if (!result->ok && GetFieldOrDefault(*result, "summary", "").empty()) {
        result->fields["summary"] = GetFieldOrDefault(*result, "error", "command failed");
    }

    std::vector<std::string> required_fields = overlay.required_fields;
    if (required_fields.empty()) {
        for (const FieldCatalogEntry & field : GetFieldCatalog()) {
            if (field.required) {
                required_fields.push_back(field.key);
            }
        }
    }
    std::ostringstream required_fields_text;
    std::ostringstream missing_fields_text;
    bool first_required = true;
    bool first_missing = true;
    int missing_count = 0;
    for (const std::string & field_name : required_fields) {
        if (!first_required) {
            required_fields_text << ",";
        }
        required_fields_text << field_name;
        first_required = false;
        if (GetFieldOrDefault(*result, field_name, "").empty()) {
            if (!first_missing) {
                missing_fields_text << ",";
            }
            missing_fields_text << field_name;
            first_missing = false;
            ++missing_count;
        }
    }
    result->fields["required_result_fields"] = required_fields_text.str();
    result->fields["missing_required_result_fields"] = missing_fields_text.str();
    result->fields["result_field_contract_status"] = missing_count == 0 ? "complete" : "missing_required_fields";
    result->fields["required_result_field_count"] = std::to_string(required_fields.size());
    result->fields["missing_required_result_field_count"] = std::to_string(missing_count);
    result->fields["status"] = ResolveResultEnvelopeStatus(*result);
    if (GetFieldOrDefault(*result, "error_message", "").empty()) {
        result->fields["error_message"] = GetFieldOrDefault(*result, "error", "");
    }
    if (GetFieldOrDefault(*result, "error_code", "").empty()) {
        if (result->fields["status"] == "INVALID") {
            result->fields["error_code"] = "invalid_result";
        } else if (result->fields["status"] == "FAILED") {
            result->fields["error_code"] = "tool_exit_" + std::to_string(result->exit_code);
        }
    }
    if (GetFieldOrDefault(*result, "failure_mode", "").empty()) {
        result->fields["failure_mode"] = ResolveFailureModeField(*result);
    }
    if (GetFieldOrDefault(*result, "result_object", "").empty()) {
        result->fields["result_object"] = BuildResultObjectJson(*result);
    }
    result->fields["result_hash"] = BuildResultEnvelopeHash(*result);
    if (missing_count > 0 && GetFieldOrDefault(*result, "next_action", "").empty()) {
        result->fields["next_action"] = "populate missing_required_result_fields before browser projection or downstream consumption";
    }
}
