#include "CapabilityRegistry.h"

const std::vector<SemanticActionSpec> & GetSemanticActionSpecs() {
    static const std::vector<SemanticActionSpec> actions = {
        // -----------------------------------------------------------------
        // Remote Session / Core Remote Control
        // -----------------------------------------------------------------
        {
            "check_remote_online",
            "Check whether codex_lan_agent is reachable and can answer lightweight health requests.",
            "lan_agent_health",
            "{}",
            "{\"status\":\"ok\"}",
            "{\"tool\":\"lan_agent_runtime_overview\",\"reason\":\"health unavailable\"}",
            "[\"status\",\"remote_timestamp\",\"queue_depth\",\"active_resource_lock_count\",\"last_request_time\"]",
            "low",
            "true",
            "none"
        },
        {
            "check_local_chat",
            "Check whether the local-chat gateway used by rag.query is ready. If not, use directory-scope or evidence-driven fallback summaries until the provider is restored.",
            "lan_agent_health",
            "{}",
            "{\"local_chat_ready\":\"true\"}",
            "{\"tool\":\"rag.basic_comm_smoke\",\"reason\":\"local chat readiness needs provider context\"}",
            "[\"local_chat_ready\",\"local_chat_endpoint\",\"local_chat_detail\"]",
            "low",
            "true",
            "none"
        },
        {
            "read_latest_log",
            "Discover recent agent logs and read the latest log tail without using the task queue.",
            "lan_agent_discover_logs",
            "{\"max_entries\":\"optional integer\",\"tail_lines\":\"optional integer\"}",
            "{\"latest_log_path\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_tail_text_file\",\"reason\":\"known log path available\"}",
            "[\"latest_log_path\",\"latest_log_name\",\"latest_log_tail\",\"log_count\",\"returned_count\"]",
            "low",
            "true",
            "none"
        },
        {
            "get_task_status",
            "Read one queued/running/completed task status by task_id.",
            "lan_agent_get_task",
            "{\"task_id\":\"required string\"}",
            "{\"status\":\"queued|running|succeeded|failed\"}",
            "{\"tool\":\"lan_agent_task_log\",\"reason\":\"task status needs log evidence\"}",
            "[\"task_id\",\"task_log_ref\",\"status\",\"summary\",\"next_action\",\"semantic_outcome\",\"result_log_path\",\"resolved_log_path\",\"result_ref\",\"evidence_ref\",\"expected_marker\"]",
            "low",
            "true",
            "none"
        },
        {
            "resolve_task_result_ref",
            "Resolve one task_id or task-log reference into actual result landing refs.",
            "lan_agent_resolve_task_result",
            "{\"task_id\":\"optional string\",\"task_ref\":\"optional string like task-log(task-...) or task:task-...\"}",
            "{\"result_ref\":\"non_empty when available\"}",
            "{\"tool\":\"lan_agent_get_task\",\"reason\":\"raw task status may be enough when refs are already visible\"}",
            "[\"task_id\",\"task_ref\",\"task_log_ref\",\"status\",\"summary\",\"resolved_log_path\",\"resolved_result_ref\",\"resolved_evidence_ref\",\"result_ref\",\"evidence_ref\"]",
            "low",
            "true",
            "none"
        },
        {
            "run_light_command",
            "Run a known lightweight CLI profile directly; avoid for build/test workloads.",
            "lan_agent_run_cli_profile",
            "{\"profile\":\"required string\",\"args\":\"optional string\",\"safe_profile_allowlist\":[\"check_build_dir\",\"run_script\",\"run_local_chat\"]}",
            "{\"exit_code\":\"0\",\"semantic_outcome\":\"not_failed\"}",
            "{\"tool\":\"lan_agent_enqueue_cli_profile\",\"reason\":\"command may be long-running\"}",
            "[\"profile\",\"exit_code\",\"log_path\",\"semantic_outcome\",\"expected_marker\"]",
            "medium",
            "false",
            "process"
        },
        {
            "build_target",
            "Queue a build_target profile for a build directory and target. Preferred flow: call lan_agent_preflight_build_target first and pass preflight_ref instead of hand-writing preflight_status=ready.",
            "lan_agent_build_target",
            "{\"build_dir\":\"required string\",\"target\":\"required string\",\"config\":\"optional string\",\"dry_run\":\"optional boolean\",\"validate_args\":\"optional boolean\",\"stall_timeout_sec\":\"optional integer; 0 disables stall watchdog\",\"preflight_ref\":\"optional string\",\"preflight_status\":\"optional string\"}",
            "{\"status\":\"queued\",\"final_status\":\"succeeded\",\"expected_marker_verified\":\"true\"}",
            "{\"tool\":\"lan_agent_task_log\",\"reason\":\"build failed or marker missing\"}",
            "[\"task_id\",\"status\",\"queue_depth\",\"summary\",\"next_action\",\"resource_key\"]",
            "high",
            "true",
            "queue_build_task"
        },
        {
            "preflight_build_target",
            "Run the explicit build preflight contract and return preflight_status, preflight_reason_code, and preflight_ref for lan_agent_build_target.",
            "lan_agent_preflight_build_target",
            "{\"build_dir\":\"required string\",\"target\":\"required string\",\"config\":\"optional string\"}",
            "{\"preflight_status\":\"ready|blocked\",\"preflight_ref\":\"non_empty when ready\"}",
            "{\"tool\":\"lan_agent_build_target\",\"reason\":\"queue the real build after preflight is ready\"}",
            "[\"build_dir\",\"target\",\"config\",\"preflight_status\",\"preflight_reason_code\",\"preflight_ref\",\"summary\",\"next_action\"]",
            "low",
            "true",
            "none"
        },
        {
            "get_git_diff",
            "Create a read-only snapshot diff from a Git repo or managed non-Git mirror snapshot repo.",
            "lan_agent_snapshot_diff",
            "{\"repo_root\":\"optional string\",\"non_git_strategy\":\"optional auto|mirror_repo|fail\",\"snapshot_action\":\"optional diff|refresh_baseline\"}",
            "{\"semantic_outcome\":\"snapshot_diff_ready|non_git_snapshot_diff_ready|non_git_snapshot_baseline_created\"}",
            "{\"tool\":\"lan_agent_preview_patch\",\"reason\":\"git root unavailable\"}",
            "[\"semantic_outcome\",\"log_path\",\"content\",\"expected_marker\",\"snapshot_repo_path\",\"snapshot_repository_mode\"]",
            "low",
            "true",
            "none"
        },
        {
            "basic_diff_review",
            "Review a small diff or fallback to remote snapshot diff through local-chat.",
            "rag.diff_review",
            "{\"diff_text\":\"optional string\"}",
            "{\"insufficient_context\":\"false\",\"output_text\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_snapshot_diff\",\"reason\":\"diff_text unavailable\"}",
            "[\"output_text\",\"source_refs\",\"evidence_lines\",\"confidence\",\"insufficient_context\",\"review_log_path\",\"risk_level\",\"fallback\"]",
            "medium",
            "true",
            "none"
        },
        {
            "read_test_result",
            "Read and classify a task log or test log into a semantic outcome.",
            "rag.log_classify",
            "{\"task_id\":\"optional string\",\"file_path\":\"optional string\",\"log_text\":\"optional string\"}",
            "{\"insufficient_context\":\"false\",\"semantic_outcome\":\"not_insufficient_context\"}",
            "{\"tool\":\"lan_agent_task_log\",\"reason\":\"task log tail needed\"}",
            "[\"semantic_outcome\",\"evidence_lines\",\"confidence\",\"insufficient_context\",\"log_path\",\"risk_level\"]",
            "low",
            "true",
            "none"
        },
        {
            "generate_thread_report",
            "Generate a compact standard thread report from current health and event state.",
            "lan_agent_runtime_overview",
            "{}",
            "{\"status\":\"ok\"}",
            "{\"tool\":\"lan_agent_tail_control_events\",\"reason\":\"recent event evidence needed\"}",
            "[\"module\",\"remote_entry\",\"action\",\"result\",\"next_action\",\"last_request_time\",\"last_request_entry\"]",
            "low",
            "true",
            "none"
        },
        {
            "rag.basic_comm.check",
            "Check RAG basic communication status.",
            "rag.basic_comm_smoke",
            "{}",
            "{\"result\":\"pass\",\"blocking_points\":\"none\"}",
            "{\"tool\":\"lan_agent_health\",\"reason\":\"basic smoke unavailable\"}",
            "[\"module\",\"remote_entry\",\"result\",\"blocking_points\",\"next_action\"]",
            "low",
            "true",
            "none"
        },
        {
            "inspect_rag_index_status",
            "Inspect the current RAG bridge readiness and whether clips_meta is explicitly advertised.",
            "lan_agent_rag_index_status",
            "{}",
            "{\"enabled\":\"true|false\",\"ready\":\"true|false\",\"clips_meta\":\"true|false\"}",
            "{\"tool\":\"lan_agent_rag_clips_meta\",\"reason\":\"inspect bridge fact metadata next\"}",
            "[\"enabled\",\"ready\",\"status\",\"chunk_count\",\"clips_meta\",\"clips_meta_supported\",\"capabilities\",\"next_action\"]",
            "low",
            "true",
            "none"
        },
        {
            "rag_clips_meta",
            "Query the upstream RAG clips/meta route and return CLIPS-ready fact_bundle plus provider metadata.",
            "lan_agent_rag_clips_meta",
            "{\"query\":\"required string\",\"top_k\":\"optional integer\"}",
            "{\"clips_meta\":\"true|false\",\"fact_bundle\":\"json object\",\"serialized_assertions\":\"json array\"}",
            "{\"tool\":\"lan_agent_rag_index_status\",\"reason\":\"inspect bridge capabilities when clips_meta is unavailable\"}",
            "[\"query\",\"top_k\",\"clips_meta\",\"fact_schema_id\",\"decision_schema_id\",\"fact_bundle\",\"serialized_assertions\",\"decision\",\"verification\",\"matched_rule\",\"next_action\"]",
            "medium",
            "true",
            "none"
        },
        {
            "rag_clips_run",
            "Run the upstream RAG CLIPS route directly and return verified store_refs for the snapshot, query index, and slice indexes.",
            "lan_agent_rag_clips_run",
            "{\"query\":\"required string\",\"top_k\":\"optional integer\",\"request_id\":\"optional string\",\"trace_id\":\"optional string\",\"query_id\":\"optional string\",\"timeout_ms\":\"optional integer\"}",
            "{\"request_id\":\"non_empty\",\"trace_id\":\"non_empty\",\"query_id\":\"non_empty\",\"store_refs_verified\":\"true|false\"}",
            "{\"tool\":\"lan_agent_rag_index_status\",\"reason\":\"inspect bridge readiness when rag/clips/run is unavailable or weak\"}",
            "[\"request_id\",\"trace_id\",\"query_id\",\"store_refs\",\"store_refs_present\",\"store_refs_verified\",\"store_refs_verification_summary\",\"run_snapshot_path\",\"query_index_path\",\"slice_index_ref_count\",\"slice_index_verified_count\",\"result_ref\",\"evidence_ref\",\"log_path\",\"next_action\"]",
            "medium",
            "true",
            "none"
        },
        {
            "rag_storage_lookup",
            "Read storage-backed RAG records directly by slice, trace, query, node, edge, or review observation id.",
            "lan_agent_rag_storage_lookup",
            "{\"kind\":\"required string\",\"id\":\"optional string\",\"slice_id\":\"optional string\",\"trace_id\":\"optional string\",\"query_id\":\"optional string\",\"node_id\":\"optional string\",\"edge_id\":\"optional string\",\"limit\":\"optional integer\",\"timeout_ms\":\"optional integer\"}",
            "{\"record_model\":\"rag_storage_lookup_response_v1\",\"ok\":\"true|false\"}",
            "{\"tool\":\"lan_agent_rag_storage_page\",\"reason\":\"page deeper clips facts after storage lookup\"}",
            "[\"kind\",\"id\",\"slice_id\",\"trace_id\",\"query_id\",\"node_id\",\"edge_id\",\"found\",\"primary\",\"related\",\"storage_base_path\",\"rocksdb_path\",\"kv_snapshot_path\",\"result_ref\",\"evidence_ref\",\"log_path\",\"next_action\"]",
            "low",
            "true",
            "none"
        },
        {
            "rag_review_observe",
            "Write one storage-backed RAG review observation card through the upstream review observation route.",
            "lan_agent_rag_review_observe",
            "{\"summary\":\"optional string\",\"scenario\":\"optional string\",\"train\":\"optional string\",\"request_id\":\"optional string\",\"trace_id\":\"optional string\",\"query_id\":\"optional string\",\"module\":\"optional string\",\"task_layer\":\"optional string\",\"task_case\":\"optional string\",\"dataset_bridge\":\"optional string\",\"test_bucket\":\"optional string\",\"test_flow\":\"optional string\",\"baseline_objective\":\"optional string\",\"best_objective\":\"optional string\",\"objective_delta\":\"optional number encoded as string\",\"comparison_status\":\"optional string\",\"comparison_magnitude\":\"optional string\",\"optimization_signal\":\"optional string\",\"risk_axis\":\"optional string\",\"bucket_coverage\":\"optional string\",\"coverage_gap\":\"optional string\",\"coverage_status\":\"optional string\",\"next_review_action\":\"optional string\",\"review_scope\":\"optional string\",\"result_stage\":\"optional string\",\"primary_review_ref\":\"optional string\",\"summary_ref\":\"optional string\",\"compare_ref\":\"optional string\",\"replay_ref\":\"optional string\",\"best_params_ref\":\"optional string\",\"human_review_required\":\"optional boolean\",\"conclusion_id\":\"optional string\",\"short_conclusion\":\"optional string\",\"why_it_matters\":\"optional string\",\"next_observation\":\"optional string\",\"source_refs\":\"optional raw json string array\",\"tags\":\"optional raw json string array\",\"timeout_ms\":\"optional integer\"}",
            "{\"record_model\":\"rag_review_observation_response_v1\",\"ok\":\"true|false\"}",
            "{\"tool\":\"lan_agent_rag_storage_page\",\"reason\":\"page review observation cards by trace, bucket, or gap after write\"}",
            "[\"request_id\",\"trace_id\",\"query_id\",\"module\",\"task_case\",\"observation_id\",\"test_bucket\",\"bucket_coverage\",\"coverage_gap\",\"coverage_status\",\"review_scope\",\"result_stage\",\"best_params_ref\",\"human_review_required\",\"conclusion_id\",\"review_store_path\",\"review_index_path\",\"review_trace_index_path\",\"review_bucket_index_path\",\"rocksdb_path\",\"kv_snapshot_path\",\"result_ref\",\"evidence_ref\",\"log_path\",\"next_action\"]",
            "medium",
            "true",
            "none"
        },
        {
            "rag_storage_page",
            "Page storage-backed clips facts or review observations directly from the upstream RAG store.",
            "lan_agent_rag_storage_page",
            "{\"kind\":\"required string\",\"trace_id\":\"optional string\",\"query_id\":\"optional string\",\"test_bucket\":\"optional string\",\"coverage_gap\":\"optional string\",\"result_stage\":\"optional string\",\"coverage_status\":\"optional string\",\"run_kind\":\"optional string\",\"fact_type\":\"optional string\",\"limit\":\"optional integer\",\"offset\":\"optional integer\",\"timeout_ms\":\"optional integer\"}",
            "{\"record_model\":\"rag_storage_page_response_v1\",\"ok\":\"true|false\",\"has_more\":\"true|false\"}",
            "{\"tool\":\"lan_agent_rag_storage_page\",\"reason\":\"continue with next_offset when has_more=true\"}",
            "[\"kind\",\"trace_id\",\"query_id\",\"test_bucket\",\"coverage_gap\",\"result_stage\",\"coverage_status\",\"run_kind\",\"fact_type\",\"offset\",\"limit\",\"returned_count\",\"total_count\",\"has_more\",\"next_offset\",\"records\",\"trace_lookup\",\"key_prefix\",\"result_ref\",\"evidence_ref\",\"log_path\",\"next_action\"]",
            "low",
            "true",
            "none"
        },
        {
            "run_rag_flow",
            "Run one remote RAG flow query through the current retrieval and generation bridge.",
            "lan_agent_run_rag_flow",
            "{\"query\":\"required string\",\"mode\":\"optional string\"}",
            "{\"status_code\":\"200\",\"direct_answer\":\"non_empty when available\"}",
            "{\"tool\":\"lan_agent_rag_index_status\",\"reason\":\"inspect bridge readiness when the flow result is weak or unavailable\"}",
            "[\"query\",\"mode\",\"direct_answer\",\"summary\",\"log_path\",\"analysis_only\",\"execution_capability\"]",
            "medium",
            "false",
            "remote_rag_query"
        },
        {
            "start_remote_session_turn",
            "Create one new browser-visible remote-session turn through the llama.cpp remote session entry.",
            "lan_agent_remote_session_new_turn",
            "{\"task_id\":\"required string\",\"speaker_mode\":\"required string\",\"reasoning_level\":\"required string\",\"prompt_purpose\":\"required string\",\"context_refs\":\"required string or JSON array\",\"response_mode\":\"required string\",\"prompt_text\":\"optional string\"}",
            "{\"session_id\":\"non_empty\",\"turn_id\":\"non_empty\",\"write_mode\":\"new\"}",
            "{\"tool\":\"lan_agent_get_remote_session\",\"reason\":\"verify the newly created browser-visible session turn\"}",
            "[\"task_id\",\"session_id\",\"turn_id\",\"write_mode\",\"direct_answer\",\"result_ref\",\"evidence_ref\",\"provider_id\",\"capability_id\"]",
            "medium",
            "false",
            "remote_session_write"
        },
        {
            "append_remote_session_turn",
            "Append one controlled turn into an existing browser-visible remote-session.",
            "lan_agent_remote_session_append_turn",
            "{\"task_id\":\"required string\",\"session_id\":\"required string\",\"speaker_mode\":\"required string\",\"reasoning_level\":\"required string\",\"prompt_purpose\":\"required string\",\"context_refs\":\"required string or JSON array\",\"response_mode\":\"required string\",\"prompt_text\":\"optional string\"}",
            "{\"session_id\":\"non_empty\",\"turn_id\":\"non_empty\",\"write_mode\":\"append\"}",
            "{\"tool\":\"lan_agent_get_remote_session\",\"reason\":\"verify the appended browser-visible session turn\"}",
            "[\"task_id\",\"session_id\",\"turn_id\",\"write_mode\",\"direct_answer\",\"result_ref\",\"evidence_ref\",\"provider_id\",\"capability_id\"]",
            "medium",
            "false",
            "remote_session_write"
        },
        {
            "get_remote_session",
            "Read one browser-visible remote-session conversation by session_id.",
            "lan_agent_get_remote_session",
            "{\"session_id\":\"required string\"}",
            "{\"session_id\":\"non_empty\",\"status\":\"ok|failed\"}",
            "{\"tool\":\"lan_agent_list_remote_sessions\",\"reason\":\"discover an available remote-session id first\"}",
            "[\"session_id\",\"body\",\"summary\",\"session_semantic_projection_ready\",\"session_semantic_projection_latest_turn_id\"]",
            "low",
            "true",
            "none"
        },
                {
            "probe_text_file",
            "Probe file length, byte size, and line count before reading or editing. Use this first when the file may be large or when a segmented edit is planned.",
            "lan_agent_probe_text_file",
            "{\"file_path\":\"required string\",\"primary_intent\":\"optional string\",\"trace_id\":\"optional string\"}",
            "{\"file_path\":\"non_empty\",\"probe_complete\":\"true\",\"recommended_next_tool\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_read_text_file\",\"reason\":\"probe the file first to learn its length before choosing read or edit mode\"}",
            "[\"file_path\",\"normalized_path\",\"file_bytes\",\"total_lines\",\"file_length_class\",\"probe_complete\",\"recommended_next_tool\",\"recommended_read_max_lines\",\"next_call_json\",\"result_ref\",\"evidence_ref\"]",
            "low",
            "true",
            "none"
        },
        {
            "read_document",
            "Read raw file content for inspection or verification. Probe the file first with lan_agent_probe_text_file when the target may be large or when a segmented edit is planned. Do not use this as the first step for comment cleanup, text cleaning, or localized source edits.",
            "lan_agent_read_text_file",
            "{\"file_path\":\"required string\",\"max_lines\":\"optional integer\",\"start_line\":\"optional integer\",\"start_byte_offset\":\"optional integer for structured-body chunk continuation\",\"primary_intent\":\"optional string\"}",
            "{\"file_path\":\"non_empty\",\"content\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_probe_text_file\",\"reason\":\"probe file length first before raw reads or localized edits\"}",
            "[\"file_path\",\"content\",\"line_count\",\"normalized_path\"]",
            "low",
            "true",
            "none"
        },
        {
            "read_directory_files",
            "Read matching files from one remote directory with a stable manifest and continuation state.",
            "lan_agent_read_directory_files",
            "{\"directory_path\":\"required string\",\"file_extensions_csv\":\"optional string such as .cpp,.h,.txt\",\"max_files\":\"optional integer\",\"max_lines_per_file\":\"optional integer\",\"start_line\":\"optional integer\",\"start_byte_offset\":\"optional integer for structured-body chunk continuation\"}",
            "{\"directory_path\":\"non_empty\",\"matched_file_count\":\"non_empty\",\"batch_completion\":\"complete|incomplete\",\"analysis_allowed\":\"true|false\"}",
            "{\"tool\":\"lan_agent_list_directory\",\"reason\":\"inspect directory shape when file type filter is unclear\"}",
            "[\"directory_path\",\"file_extensions_csv\",\"matched_file_count\",\"current_file_path\",\"remaining_batch_file_count\",\"file_paths_json\",\"file_names_json\",\"next_call_json\"]",
            "low",
            "true",
            "none"
        },
        {
            "prepare_directory_analysis",
            "Build one server-side directory analysis bundle for downstream AI overview using bounded file excerpts instead of manual paged traversal.",
            "lan_agent_prepare_directory_analysis",
            "{\"directory_path\":\"required string\",\"file_extensions_csv\":\"optional string such as .cpp,.h,.txt\",\"max_files\":\"optional integer\",\"max_excerpt_lines_per_file\":\"optional integer\",\"max_total_excerpt_lines\":\"optional integer\",\"max_excerpt_chars\":\"optional integer\",\"trace_id\":\"optional string\"}",
            "{\"directory_path\":\"non_empty\",\"analysis_bundle_ref\":\"non_empty\",\"analysis_allowed\":\"true\",\"batch_completion\":\"complete\"}",
            "{\"tool\":\"lan_agent_list_directory\",\"reason\":\"inspect directory shape first when the target path is uncertain\"}",
            "[\"directory_path\",\"matched_file_count\",\"excerpt_file_count\",\"truncated_file_count\",\"omitted_file_count\",\"total_excerpt_lines\",\"analysis_bundle_ref\",\"content_text\",\"result_ref\",\"evidence_ref\"]",
            "low",
            "true",
            "none"
        },
        {
            "scan_text_ranges",
            "Scan one text file server-side and return exactly one range per call. For comment cleanup, process that range, apply one atomic edit, verify, then rescan from offset 0.",
            "lan_agent_scan_text_ranges",
            "{\"file_path\":\"required string\",\"scan_mode\":\"optional string: comments|line_comments|block_comments; use comments for comment cleanup\",\"max_ranges_per_call\":\"must be 1; batch values are blocked\",\"range_offset\":\"optional integer\",\"primary_intent\":\"optional string\",\"trace_id\":\"optional string\"}",
            "{\"file_path\":\"non_empty\",\"ranges_json\":\"non_empty when matches exist\",\"single_step_required\":\"true\",\"operation_granularity\":\"single_text_range\",\"task_completion\":\"complete|single_item_ready\"}",
            "{\"tool\":\"lan_agent_read_text_file\",\"reason\":\"read raw content directly when a range scan is not the right primitive\"}",
            "[\"file_path\",\"scan_mode\",\"total_lines\",\"total_range_count\",\"returned_range_count\",\"ranges_json\",\"next_call_json\",\"scan_result_ref\",\"result_ref\",\"evidence_ref\",\"cache_hit\"]",
            "low",
            "true",
            "none"
        },
        {
            "prepare_edit_windows",
            "Build exactly one localized edit window from ranges_json so downstream patching stays single-step.",
            "lan_agent_prepare_edit_windows",
            "{\"file_path\":\"required string\",\"ranges_json\":\"required JSON array string\",\"context_before\":\"optional integer\",\"context_after\":\"optional integer\",\"max_windows_per_call\":\"must be 1; batch values are blocked\",\"window_offset\":\"optional integer\",\"max_window_chars\":\"optional integer\",\"trace_id\":\"optional string\"}",
            "{\"file_path\":\"non_empty\",\"windows_json\":\"non_empty when windows are produced\",\"single_step_required\":\"true\",\"operation_granularity\":\"single_edit_window\",\"task_completion\":\"complete|single_item_ready\"}",
            "{\"tool\":\"lan_agent_scan_text_ranges\",\"reason\":\"derive edit windows from detected ranges first\"}",
            "[\"file_path\",\"window_count\",\"returned_window_count\",\"windows_json\",\"next_call_json\",\"edit_window_bundle_ref\",\"result_ref\",\"evidence_ref\"]",
            "low",
            "true",
            "none"
        },
        {
            "find_line_metadata",
            "Read one exact line server-side and return only line metadata such as line hash, length, and optional preview without exposing full file content.",
            "lan_agent_find_line_metadata",
            "{\"file_path\":\"required string\",\"line\":\"required integer or line_number\",\"show_preview\":\"optional boolean\"}",
            "{\"file_path\":\"non_empty\",\"line_hash\":\"non_empty\",\"line_length\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_probe_text_file\",\"reason\":\"probe file length first when the target path is uncertain or very large\"}",
            "[\"file_path\",\"normalized_path\",\"line\",\"line_hash\",\"line_length\",\"result_ref\",\"evidence_ref\"]",
            "low",
            "true",
            "none"
        },
        {
            "find_content_matches",
            "Locate content server-side and return only match metadata such as line numbers, hashes, match count, and optional preview without exposing full file content.",
            "lan_agent_find_content_matches",
            "{\"file_path\":\"required string\",\"anchor_text\":\"required string or query_text/text/anchor\",\"show_preview\":\"optional boolean\",\"fuzzy_threshold\":\"optional integer\"}",
            "{\"file_path\":\"non_empty\",\"match_count\":\"non_empty\",\"matches_json\":\"json array\",\"is_unique\":\"true|false\"}",
            "{\"tool\":\"lan_agent_probe_text_file\",\"reason\":\"probe file length first when the target path is uncertain or very large\"}",
            "[\"file_path\",\"normalized_path\",\"match_type\",\"match_count\",\"is_unique\",\"first_match_line\",\"first_match_hash\",\"matches_json\",\"result_ref\",\"evidence_ref\"]",
            "low",
            "true",
            "none"
        },
        {
            "locate_text_lines",
            "Locate anchor text server-side and return only line metadata such as count, line numbers, hashes, preview, and uniqueness without exposing full file content.",
            "lan_agent_locate_text_lines",
            "{\"file_path\":\"required string\",\"anchor_text\":\"required string\",\"show_preview\":\"optional boolean\",\"fuzzy_threshold\":\"optional integer\"}",
            "{\"file_path\":\"non_empty\",\"match_count\":\"non_empty\",\"matches_json\":\"json array\",\"is_unique\":\"true|false\"}",
            "{\"tool\":\"lan_agent_probe_text_file\",\"reason\":\"probe file length first when the target path is uncertain or very large\"}",
            "[\"file_path\",\"normalized_path\",\"match_type\",\"match_count\",\"is_unique\",\"first_match_line\",\"first_match_hash\",\"matches_json\",\"result_ref\",\"evidence_ref\"]",
            "low",
            "true",
            "none"
        },
        {
            "delete_line_atomic",
            "Delete one exact line atomically using server-side line-number resolution, optional line hash verification, and atomic write.",
            "lan_agent_delete_line_atomic",
            "{\"file_path\":\"required string\",\"line\":\"required integer or line_number\",\"expected_line_hash\":\"optional string\",\"request_id\":\"optional string\",\"trace_id\":\"optional string\"}",
            "{\"file_path\":\"non_empty\",\"line\":\"non_empty\",\"new_hash\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_find_line_metadata\",\"reason\":\"capture line hash before atomic delete\"}",
            "[\"file_path\",\"normalized_path\",\"line\",\"deleted_line_hash_before\",\"new_hash\",\"result_ref\",\"evidence_ref\"]",
            "high",
            "false",
            "write_file"
        },
        {
            "delete_content_atomic",
            "Delete one matched content line atomically using server-side content matching, optional line hash verification, and atomic write.",
            "lan_agent_delete_content_atomic",
            "{\"file_path\":\"required string\",\"anchor_text\":\"required string or query_text/text/anchor\",\"occurrence\":\"optional integer\",\"expected_anchor_hash\":\"optional string\",\"probe_ref\":\"recommended after lan_agent_probe_text_file\",\"probe_ready\":\"optional boolean\",\"request_id\":\"optional string\",\"trace_id\":\"optional string\"}",
            "{\"file_path\":\"non_empty\",\"deleted_line\":\"non_empty\",\"new_hash\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_find_content_matches\",\"reason\":\"confirm content match uniqueness and capture line hash before atomic delete\"}",
            "[\"file_path\",\"normalized_path\",\"anchor_text\",\"occurrence\",\"deleted_line\",\"deleted_line_hash_before\",\"new_hash\",\"result_ref\",\"evidence_ref\"]",
            "high",
            "false",
            "write_file"
        },
        {
            "insert_after_anchor_atomic",
            "Insert one code/text block after the Nth anchor match using server-side anchor resolution, optional anchor hash verification, and atomic write.",
            "lan_agent_insert_after_anchor_atomic",
            "{\"file_path\":\"required string\",\"anchor_text\":\"required string\",\"line_roi\":\"required string or line_roi_base64\",\"occurrence\":\"optional integer\",\"expected_anchor_hash\":\"optional string\",\"request_id\":\"optional string\",\"trace_id\":\"optional string\"}",
            "{\"file_path\":\"non_empty\",\"insert_start_line\":\"non_empty\",\"insert_line_count\":\"non_empty\",\"new_hash\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_locate_text_lines\",\"reason\":\"confirm anchor uniqueness and capture anchor hash before atomic insert\"}",
            "[\"file_path\",\"normalized_path\",\"anchor_text\",\"occurrence\",\"anchor_line\",\"insert_start_line\",\"insert_line_count\",\"anchor_line_hash_before\",\"new_hash\",\"result_ref\",\"evidence_ref\"]",
            "high",
            "false",
            "write_file"
        },
        {
            "replace_line_range_atomic",
            "Replace one line range using server-side hash verification and atomic write without returning source file content to the model.",
            "lan_agent_replace_line_range_atomic",
            "{\"file_path\":\"required string\",\"start_line\":\"required integer\",\"end_line\":\"required integer\",\"line_roi\":\"required string or line_roi_base64\",\"expected_range_hash\":\"optional string\",\"request_id\":\"optional string\",\"trace_id\":\"optional string\"}",
            "{\"file_path\":\"non_empty\",\"start_line\":\"non_empty\",\"end_line\":\"non_empty\",\"new_hash\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_locate_text_lines\",\"reason\":\"use metadata-only locate or prior MCP evidence to determine the target range and hash before replacement\"}",
            "[\"file_path\",\"normalized_path\",\"start_line\",\"end_line\",\"replacement_line_count\",\"range_hash_before\",\"new_hash\",\"result_ref\",\"evidence_ref\"]",
            "high",
            "false",
            "write_file"
        },
        {
            "run_cxparser_flow",
            "Run one cxparser-backed test through the public cxparser_ext_cxscript_cli entry, including --script, --script-dir, or --kind --layer --module --case forms.",
            "lan_agent_run_cxparser_flow",
            "{\"test_statement\":\"optional string\",\"flow_id\":\"optional string; legacy runtime flows are mapped to cxparser_ext_cxscript_cli\",\"params_json\":\"optional JSON string\",\"args\":\"optional raw cli args\",\"script\":\"optional path\",\"script_dir\":\"optional directory\",\"kind\":\"optional string\",\"layer\":\"optional string\",\"module\":\"optional string\",\"case\":\"optional string\",\"trace_id\":\"optional string\",\"goal_id\":\"optional string\"}",
            "{\"status\":\"success|needs_continue|failed\",\"supervision_status\":\"closed_loop_continue|closed_loop_complete|alarm\"}",
            "{\"tool\":\"lan_agent_list_cxparser_flows\",\"reason\":\"discover explicit flow ids only when direct cxscript_cli fields are insufficient\"}",
            "[\"flow_id\",\"public_entry_flow_id\",\"public_entry_contract\",\"cxparser_public_entry\",\"cxparser_public_build_root\",\"cxparser_public_build_contract\",\"cxparser_runtime_arguments\",\"status\",\"supervision_status\",\"task_id\",\"result_ref\",\"evidence_ref\",\"next_call_json\",\"assistant_response_allowed\",\"final_answer_allowed\",\"facts_json\"]",
            "low",
            "true",
            "none"
        },
        {
            "write_document",
            "Create, overwrite, or append exact text content to one remote document on the local filesystem. Do not use for comment cleanup, text cleaning, or localized source edits; use one scan/window/atomic edit step instead.",
            "lan_agent_write_text_file",
            "{\"file_path\":\"required string\",\"content\":\"optional string\",\"content_base64\":\"optional base64 string\",\"append\":\"optional boolean\"}",
            "{\"result\":\"created|overwritten|appended\",\"log_path\":\"non_empty\",\"write_verified\":\"true\"}",
            "{\"tool\":\"lan_agent_preview_patch\",\"reason\":\"preview full-file replacement first when replacing existing content\"}",
            "[\"file_path\",\"normalized_path\",\"log_path\",\"result\",\"written_text_bytes\",\"applied_hash\",\"write_verified\",\"content_transport\"]",
            "medium",
            "false",
            "write_file"
        },
        {
            "ensure_directory",
            "Create a directory or ensure the parent directory for a file_path exists before remote file sync or patch/write operations.",
            "lan_agent_ensure_directory",
            "{\"directory_path\":\"optional string\",\"file_path\":\"optional string\",\"ensure_parent\":\"optional boolean\"}",
            "{\"result\":\"created|already_exists\",\"normalized_path\":\"non_empty\",\"exists_after\":\"true\"}",
            "{\"tool\":\"lan_agent_write_text_file\",\"reason\":\"directory is ready; continue with file write\"}",
            "[\"directory_path\",\"file_path\",\"normalized_path\",\"existed_before\",\"exists_after\",\"log_path\",\"result\"]",
            "low",
            "false",
            "filesystem"
        },
        {
            "refactor_file",
            "Preview and apply one high-risk single-file replacement on the local filesystem with explicit trace fields, hash checks, backup, and compact diff responses. Do not use for comment cleanup, text cleaning, or localized source edits.",
            "lan_agent_apply_single_file_patch",
            "{\"file_path\":\"required string\",\"new_content\":\"optional string\",\"new_content_base64\":\"optional base64 string\",\"old_hash\":\"optional string\",\"request_id\":\"optional string\",\"trace_id\":\"optional string\",\"patch_id\":\"optional string\",\"reason\":\"optional string\",\"allow_empty_content\":\"optional boolean default false\"}",
            "{\"result\":\"applied|created\",\"patch_id\":\"non_empty\",\"diff_hash\":\"non_empty\",\"backup_path\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_preview_patch\",\"reason\":\"preview patch before apply and collect diff evidence\"}",
            "[\"request_id\",\"trace_id\",\"patch_id\",\"file_path\",\"normalized_path\",\"old_hash\",\"new_hash\",\"diff_hash\",\"patch_audit_id\",\"backup_path\",\"log_path\",\"result\"]",
            "high",
            "false",
            "write_file",
            "true",
            "true",
            "true",
            "true"
        },
        {
            "apply_diff_patch",
            "Apply one git-style unified diff to a single local file. Do not use for comment cleanup, text cleaning, or localized source edits; use scan_text_ranges, prepare_edit_windows, and one atomic edit per step.",
            "lan_agent_apply_diff_patch",
            "{\"file_path\":\"required string\",\"diff_text\":\"required string\",\"old_hash\":\"optional string\",\"request_id\":\"optional string\",\"trace_id\":\"optional string\",\"patch_id\":\"optional string\",\"reason\":\"optional string\",\"allow_empty_content\":\"optional boolean default false\"}",
            "{\"result\":\"diff_applied\",\"patch_id\":\"non_empty\",\"diff_hash\":\"non_empty\",\"backup_path\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_preview_patch\",\"reason\":\"preview a full-file replacement when additional human review is needed\"}",
            "[\"request_id\",\"trace_id\",\"patch_id\",\"file_path\",\"normalized_path\",\"old_hash\",\"new_hash\",\"diff_hash\",\"input_diff_hash\",\"patch_audit_id\",\"backup_path\",\"log_path\",\"result\",\"write_mode\",\"patch_format\"]",
            "high",
            "false",
            "write_file",
            "true",
            "true",
            "true",
            "true"
        },
        {
            "inspect_patch_audit",
            "Read the stored patch audit trail for one patch_id and replay its preview/apply/verify/revert chain.",
            "lan_agent_get_patch_audit_trail",
            "{\"patch_id\":\"required string\"}",
            "{\"result\":\"patch_audit_found\",\"event_count\":\"non_zero\"}",
            "{\"tool\":\"lan_agent_preview_patch\",\"reason\":\"no audit trail exists yet for this patch_id\"}",
            "[\"patch_id\",\"event_count\",\"events_jsonl\",\"audit_event_path\",\"result\",\"summary\"]",
            "low",
            "true",
            "none"
        },
        {
            "inspect_trace_audit",
            "Read replayable audit events for one trace_id across supported audit sources.",
            "lan_agent_get_trace_audit_trail",
            "{\"trace_id\":\"required string\"}",
            "{\"result\":\"trace_audit_found\",\"event_count\":\"non_zero\"}",
            "{\"tool\":\"lan_agent_get_patch_audit_trail\",\"reason\":\"inspect a specific patch_id when trace replay is empty\"}",
            "[\"trace_id\",\"event_count\",\"source_count\",\"available_source_count\",\"matched_source_count\",\"events_jsonl\",\"audit_event_path\",\"mcp_trace_audit_event_path\",\"audit_sources\",\"result\",\"summary\"]",
            "low",
            "true",
            "none"
        },
        {
            "get_supervision_status",
            "Read the latest machine-readable supervision envelope for one trace_id and optional goal_id.",
            "lan_agent_get_supervision_status",
            "{\"trace_id\":\"required string\",\"goal_id\":\"optional string\"}",
            "{\"result\":\"supervision_status_found\",\"supervision_status\":\"closed_loop_continue|closed_loop_complete|alarm\"}",
            "{\"tool\":\"lan_agent_get_trace_audit_trail\",\"reason\":\"inspect the full trace audit stream when the latest supervision state needs replay context\"}",
            "[\"trace_id\",\"goal_id\",\"trace_context_found\",\"completion_state\",\"can_continue\",\"interruption_reason\",\"interruption_stage\",\"supervision_query_status\",\"supervision_lookup_ok\",\"supervision_decision\",\"supervision_explanation\",\"supervision_status\",\"goal_status\",\"assistant_response_allowed\",\"final_answer_allowed\",\"pre_guard_status\",\"post_guard_status\",\"acceptance_status\",\"acceptance_reason\",\"supervision_alarm_code\",\"progress_pending_count\",\"next_action_0_tool_name\",\"next_action_0_params_json\",\"next_action_0_safety_class\"]",
            "low",
            "true",
            "none"
        },
        {
            "verify_patch_result",
            "Verify one patch result by patch_id and file_path using hash and optional text assertions, and emit a repair candidate when verification fails.",
            "lan_agent_verify_single_file_patch",
            "{\"patch_id\":\"required string\",\"file_path\":\"required string\",\"expected_hash\":\"optional string\",\"contains_text\":\"optional string\",\"forbidden_text\":\"optional string\",\"request_id\":\"optional string\",\"trace_id\":\"optional string\",\"reason\":\"optional string\"}",
            "{\"result\":\"patch_verify_passed\",\"verification_ok\":\"true\"}",
            "{\"tool\":\"lan_agent_get_patch_audit_trail\",\"reason\":\"inspect patch history before generating a repair candidate\"}",
            "[\"patch_id\",\"file_path\",\"expected_hash\",\"actual_hash\",\"verification_ok\",\"semantic_outcome\",\"repair_candidate_id\",\"repair_candidate_reason\",\"next_action\"]",
            "low",
            "true",
            "none"
        },
        {
            "configure_project",
            "Queue one remote CMake configure for a project root and build directory.",
            "lan_agent_configure_project",
            "{\"project_root\":\"required string\",\"build_dir\":\"required string\",\"generator_kind\":\"optional string\",\"cmake_args\":\"optional string\",\"cmake_args_list\":\"optional string array\",\"env\":\"optional string\",\"stall_timeout_sec\":\"optional integer; 0 disables stall watchdog\"}",
            "{\"status\":\"queued|succeeded\",\"expected_marker_verified\":\"true\"}",
            "{\"tool\":\"lan_agent_task_log\",\"reason\":\"configure evidence needed\"}",
            "[\"task_id\",\"status\",\"generator_kind\",\"cmake_args\",\"cmake_args_list\",\"cmake_arg_count\",\"env\",\"configure_project_stall_timeout_sec\",\"configure_project_stall_timeout_source\",\"next_action\"]",
            "high",
            "true",
            "queue_configure_task"
        },
        {
            "run_project_tests",
            "Queue one remote ctest regex run for a build directory. Preferred flow: call lan_agent_preflight_run_ctest_target first and pass preflight_ref instead of hand-writing preflight_status=ready.",
            "lan_agent_run_ctest_target",
            "{\"build_dir\":\"required unless replayed from preflight_ref\",\"test_regex\":\"required unless replayed from preflight_ref\",\"config\":\"optional string; defaults to Release when omitted and may be replayed from preflight_ref\",\"preflight_ref\":\"optional string; preferred, and can replay build_dir/config/test_regex from preflight output\",\"preflight_status\":\"optional string\"}",
            "{\"status\":\"queued|succeeded\",\"semantic_outcome\":\"ctest_tests_passed\"}",
            "{\"tool\":\"lan_agent_task_log\",\"reason\":\"ctest evidence needed\"}",
            "[\"task_id\",\"status\",\"semantic_outcome\",\"expected_marker\",\"next_action\"]",
            "high",
            "false",
            "queue_test_task"
        },
        {
            "preflight_run_project_tests",
            "Run the explicit ctest preflight contract, including discovery, and return preflight_status, preflight_reason_code, and preflight_ref for lan_agent_run_ctest_target.",
            "lan_agent_preflight_run_ctest_target",
            "{\"build_dir\":\"required string\",\"test_regex\":\"required string\",\"config\":\"optional string\"}",
            "{\"preflight_status\":\"ready|blocked\",\"preflight_ref\":\"non_empty when ready\",\"semantic_outcome\":\"tests_discovered|no_tests_found|build_dir_missing|not_configured\"}",
            "{\"tool\":\"lan_agent_run_ctest_target\",\"reason\":\"queue the real test run after preflight is ready\"}",
            "[\"build_dir\",\"config\",\"test_regex\",\"preflight_status\",\"preflight_reason_code\",\"preflight_ref\",\"semantic_outcome\",\"test_count\",\"summary\",\"next_action\"]",
            "low",
            "true",
            "read_only_test_discovery"
        },
        {
            "discover_project_tests",
            "Read-only discovery of registered CTest tests for a build directory before queueing a test run.",
            "lan_agent_discover_ctest_tests",
            "{\"build_dir\":\"required string\",\"config\":\"optional string\",\"test_regex\":\"optional string\",\"start_index\":\"optional integer\",\"max_entries\":\"optional integer\"}",
            "{\"semantic_outcome\":\"tests_discovered|no_tests_found|build_dir_missing|not_configured\",\"test_count\":\"integer\",\"test_names\":\"json array\"}",
            "{\"tool\":\"lan_agent_configure_project\",\"reason\":\"build_dir must be configured before CTest discovery can succeed\"}",
            "[\"build_dir\",\"config\",\"semantic_outcome\",\"test_count\",\"test_names\",\"result_ref\",\"evidence_ref\",\"next_action\"]",
            "medium",
            "true",
            "read_only_test_discovery"
        },
        {
            "clips_decide",
            "Run one CLIPS rule decision over MCP request facts, MCP result facts, slice ingest facts, or cxparser preflight facts without executing the guarded tool.",
            "lan_agent_clips_decide",
            "{\"decision_domain\":\"mcp_tool_guard|mcp_result_guard|slice_ingest_guard|cxparser_preflight_guard\",\"tool_name\":\"optional string\",\"task_id\":\"optional string\",\"session_id\":\"optional string\",\"turn_id\":\"optional string\",\"provider_id\":\"optional string\",\"capability_id\":\"optional string\",\"build_dir\":\"optional string\",\"project_root\":\"optional string\",\"test_regex\":\"optional string\",\"preflight_status\":\"optional string\",\"cxparser_preflight_status\":\"optional string\",\"dedup_status\":\"optional string\",\"canonical_slice_id\":\"optional string\",\"dup_of\":\"optional string\",\"route_hint\":\"optional string\",\"source_type\":\"optional string\",\"file_path\":\"optional string\",\"probe_ref\":\"optional string\",\"probe_ready\":\"optional bool\",\"patch_id\":\"optional string\",\"request_id\":\"optional string\",\"trace_id\":\"optional string\",\"old_hash\":\"optional string\",\"reasoning_level\":\"optional string\",\"primary_intent\":\"optional string\",\"reason\":\"optional string\",\"repair_candidate_id\":\"optional string\",\"business_user_text\":\"optional string\",\"business_assistant_text\":\"optional string\",\"slice_summary\":\"optional string\",\"direct_answer\":\"optional string\",\"summary\":\"optional string\",\"assistant_text\":\"optional string\",\"error\":\"optional string\",\"ai_conclusion_valid\":\"optional bool\"}",
            "{\"decision\":\"allow|block|route\",\"verification\":\"verified|not_verified\",\"matched_rule\":\"non_empty\",\"decision_schema_id\":\"clips_decision_schema_v1\"}",
            "{\"tool\":\"lan_agent_runtime_overview\",\"reason\":\"CLIPS rule root or embedded rule load failed\"}",
            "[\"decision_domain\",\"decision\",\"verification\",\"reason_code\",\"matched_rule\",\"next_action\",\"route_target\",\"clips_explicit_fact_schema_id\",\"clips_explicit_decision_schema_id\",\"clips_explicit_engine_status\"]",
            "medium",
            "true",
            "none"
        },
        {
            "clips_chain_template",
            "Return the standard mcp_tool_chain CLIPS fact projection for one MCP tool so tool-specific rules can be added without recompiling the agent.",
            "lan_agent_clips_chain_template",
            "{\"tool_name\":\"required string\",\"chain_phase\":\"optional pre_call|post_result\",\"task_id\":\"optional string\",\"result_ref\":\"optional string\",\"evidence_ref\":\"optional string\"}",
            "{\"chain_template_id\":\"mcp_tool_chain_v1\",\"input_fact\":\"mcp_tool_chain fact\",\"evidence_policy\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_clips_decide\",\"reason\":\"run the generated fact through a concrete CLIPS decision domain\"}",
            "[\"target_tool_name\",\"chain_phase\",\"chain_template_id\",\"request_type\",\"risk\",\"safety_class\",\"execution_class\",\"evidence_policy\",\"input_fact\",\"template_contract\",\"next_action\"]",
            "low",
            "true",
            "none"
        },
        {
            "inspect_rag_index_status",
            "Read upstream RAG bridge status through codex-lan-agent and explicitly surface whether clips_meta is supported or absent.",
            "lan_agent_rag_index_status",
            "{}",
            "{\"enabled\":\"true|false\",\"ready\":\"true|false\",\"clips_meta\":\"true|false\",\"capabilities\":\"json array\"}",
            "{\"tool\":\"lan_agent_rag_clips_meta\",\"reason\":\"inspect the CLIPS-ready RAG metadata contract next\"}",
            "[\"enabled\",\"ready\",\"status\",\"chunk_count\",\"clips_meta\",\"clips_meta_supported\",\"capabilities\",\"next_action\"]",
            "low",
            "true",
            "none"
        },
        {
            "rag_clips_meta",
            "Read CLIPS-ready RAG metadata through codex-lan-agent and return fact_bundle plus serialized_assertions or a structured unsupported result.",
            "lan_agent_rag_clips_meta",
            "{\"query\":\"required string\",\"top_k\":\"optional integer\"}",
            "{\"clips_meta\":\"true|false\",\"fact_bundle\":\"json object\",\"serialized_assertions\":\"json array\"}",
            "{\"tool\":\"lan_agent_rag_index_status\",\"reason\":\"inspect bridge capabilities when clips meta is unavailable\"}",
            "[\"query\",\"top_k\",\"clips_meta\",\"fact_schema_id\",\"decision_schema_id\",\"fact_bundle\",\"serialized_assertions\",\"decision\",\"verification\",\"matched_rule\",\"next_action\"]",
            "medium",
            "true",
            "none"
        },
        // -----------------------------------------------------------------
        // Dialog Slice
        // -----------------------------------------------------------------
        {
            "record_dialog_slice",
            "Write one dialog slice record only. Persist a dialog turn as JSONL for later retrieval, but do not analyze or ingest memory here.",
            "lan_agent_record_dialog_slice",
            "{\"slice_id\":\"optional string\",\"slice_version\":\"optional string default rag_memory_slice_v1\",\"slice_type\":\"optional string default dialog_slice\",\"task_id\":\"optional string\",\"session_id\":\"required string\",\"turn_id\":\"required string\",\"provider_id\":\"optional string\",\"capability_id\":\"optional string\",\"user_text\":\"required string\",\"assistant_text\":\"required string\",\"slice_summary\":\"optional string\",\"reasoning_level\":\"optional string\",\"primary_intent\":\"optional string\",\"confidence\":\"optional string\",\"result_ref\":\"optional string\",\"evidence_ref\":\"optional string\",\"audit_ref\":\"optional string\",\"source_type\":\"optional string\",\"write_mode\":\"optional string\",\"vector_payload\":\"optional string\",\"dedup_hash\":\"optional string\",\"tags\":\"optional string\"}",
            "{\"result\":\"recorded\",\"slice_path\":\"non_empty\",\"session_id\":\"non_empty\",\"turn_id\":\"non_empty\"}",
            "{\"tool\":\"lan_agent_tail_control_events\",\"reason\":\"verify dialog slice write event\"}",
            "[\"slice_path\",\"slice_id\",\"slice_version\",\"slice_type\",\"task_id\",\"session_id\",\"turn_id\",\"provider_id\",\"capability_id\",\"result_ref\",\"evidence_ref\",\"audit_ref\",\"source_type\",\"write_mode\",\"canonical_slice_id\",\"dedup_status\",\"slice_refs\",\"storage_refs\",\"bytes\",\"checksum\",\"result\"]",
            "low",
            "false",
            "append_slice"
        },
        {
            "analyze_dialog_slices",
            "Read and summarize stored dialog slices only. This tool does not write slices and does not ingest vector memory.",
            "lan_agent_analyze_dialog_slices",
            "{\"session_id\":\"optional string\",\"max_entries\":\"optional integer\"}",
            "{\"slice_file_count\":\"non_zero\",\"result\":\"analyzed\"}",
            "{\"tool\":\"lan_agent_list_directory\",\"reason\":\"dialog slice folder may be empty\"}",
            "[\"analysis_root\",\"slice_file_count\",\"session_id\",\"latest_slice_path\",\"result\",\"summary\",\"latest_slice_session_id\",\"latest_slice_turn_id\",\"latest_slice_summary\"]",
            "low",
            "true",
            "none"
        }
    };
    return actions;
}

const std::vector<McpCapabilitySpec> & GetMcpCapabilitySpecs() {
    static const std::vector<McpCapabilitySpec> specs = {
        // -----------------------------------------------------------------
        // Remote Session
        // -----------------------------------------------------------------
        {
            "remote_session_turn",
            "llama_cpp_b8851_remote_session",
            "llama.cpp-b8851",
            "task_id,session_id?,speaker_mode,reasoning_level,prompt_purpose,context_refs,response_mode,prompt_text",
            "session_id,turn_id,write_mode,direct_answer,evidence,next_action,confidence,result_ref,evidence_ref,audit_ref,codex_request_id,agent_dispatch_id,timings,provider_id,capability_id,slice_id,slice_version,dedup_hash,canonical_slice_id,dedup_status,slice_refs,storage_refs",
            "remote_session_store + WebUI mirror",
            "structuredContent + event list item",
            "conversation_display_and_handoff",
            "Use for browser-visible AI turns and manual handoff continuity."
        },
        // -----------------------------------------------------------------
        // Ventriloquy
        // -----------------------------------------------------------------
        {
            "ventriloquist_reply",
            "codex_lan_agent_ventriloquist",
            "codex-lan-agent -> llama.cpp-b8851",
            "task_id,session_id?,speaker_mode,reasoning_level,prompt_purpose,context_refs,response_mode,prompt_text",
            "direct_answer,evidence,next_action,confidence,session_id,turn_id,write_mode,result_ref,evidence_ref,audit_ref,dispatch_mode,dispatch_fallback,provider_id,capability_id,slice_id,slice_version,dedup_hash,canonical_slice_id,dedup_status,slice_refs,storage_refs",
            "execution_binding + remote_control_events",
            "structuredContent",
            "controlled_codex_proxy_reply",
            "Compatibility front door; prefers remote-session and falls back to local chat."
        },
        // -----------------------------------------------------------------
        // RAG Recall
        // -----------------------------------------------------------------
        {
            "rag_error_recall",
            "llama_cpp_b8212_faiss",
            "llama.cpp-b8212 + Faiss",
            "query,error_signature,scope_modules,top_k",
            "matched_errors,matched_solutions,confidence,evidence_refs,next_action,provider_id,capability_id,slice_refs,storage_refs",
            "Faiss vector store",
            "structured JSON",
            "semantic_error_retrieval",
            "Stage-1 target: new errors recall similar verified historical errors and solutions."
        },
        {
            "rag_memory_lookup",
            "llama_cpp_b8212_sqlite_vss",
            "llama.cpp-b8212 + sqlite-vss",
            "query,task_id?,session_id?,memory_scope,top_k",
            "memory_hits,evidence_refs,confidence,next_action,provider_id,capability_id,slice_refs,storage_refs",
            "sqlite-vss local persistence",
            "structured JSON",
            "lightweight_persistent_memory",
            "Use for small local memory and recent verified slices."
        },
        {
            "rag_recall_writeback",
            "codex_lan_agent_rag_bridge",
            "codex-lan-agent -> llama.cpp-b8212 -> llama.cpp-b8851",
            "query,capability,task_id?,session_id?,top_k,writeback_mode",
            "provider_hits,provider_refs,writeback_session_id,writeback_turn_id,direct_answer,evidence,next_action,confidence,result_ref,evidence_ref,audit_ref,provider_id,capability_id,slice_id,slice_version,dedup_hash,canonical_slice_id,dedup_status,slice_refs,storage_refs",
            "Faiss/sqlite-vss read + remote_session_store write",
            "structuredContent + remote_session_turn",
            "retrieval_to_conversation_writeback",
            "Stage-1 bridge: b8212 retrieval results write into b8851 remote-session and become browser-visible."
        },
        {
            "ctest_discovery",
            "codex_lan_agent_ctest_discovery",
            "codex-lan-agent",
            "build_dir,config?,test_regex?,start_index?,max_entries?",
            "ok,build_dir,config,test_count,test_names,log_path,evidence_ref,result_ref,semantic_outcome,process_exit_ok,verification_ok",
            "build_dir + ctest logs",
            "structured JSON",
            "browser_visible_test_inventory",
            "Read-only source of truth for browser-side test lists and ctest preflight checks."
        },
        {
            "clips_rule_decision",
            "codex_lan_agent_clips_guard",
            "codex-lan-agent + CLIPS",
            "decision_domain,tool_name?,task_id?,session_id?,turn_id?,provider_id?,capability_id?,build_dir?,project_root?,test_regex?,preflight_status?,dedup_status?,canonical_slice_id?,dup_of?,business_user_text?,business_assistant_text?,slice_summary?,direct_answer?,summary?,assistant_text?,error?,ai_conclusion_valid?",
            "decision,verification,next_action,reason_code,matched_rule,route_target,fact_schema_id,decision_schema_id,engine_status,loaded_from_files,loaded_files",
            "CLIPS templates/rules under src/clips_rules plus embedded fallback",
            "structured JSON",
            "rule_based_mcp_guard_layer",
            "Phase-1 decision layer for MCP pre-call allow/block/route and post-result verified/not_verified judgments."
        },
        {
            "rag_bridge_status",
            "codex_lan_agent_rag_bridge_status",
            "codex-lan-agent -> llama.cpp-b8212/b8851 rag bridge",
            "none",
            "enabled,ready,status,pending,chunk_count,clips_meta,clips_meta_supported,capabilities,next_action,provider_id,capability_id",
            "upstream /rag/index/status proxied through codex-lan-agent",
            "structured JSON",
            "bridge_capability_advertisement",
            "Read-only bridge status surface so callers do not need to guess whether clips_meta exists."
        },
        {
            "rag_bridge_clips_meta",
            "codex_lan_agent_rag_bridge_clips_meta",
            "codex-lan-agent -> upstream /rag/clips/meta",
            "query,top_k",
            "clips_meta,fact_schema_id,decision_schema_id,fact_bundle,serialized_assertions,decision,verification,matched_rule,next_action,provider_id,capability_id",
            "upstream /rag/clips/meta proxied through codex-lan-agent",
            "structured JSON",
            "clips_ready_rag_meta",
            "Bridge-level CLIPS-ready retrieval metadata surface with explicit fallback when upstream route is missing."
        },
        {
            "rag_bridge_clips_run",
            "codex_lan_agent_rag_bridge_clips_run",
            "codex-lan-agent -> upstream /rag/clips/run",
            "query,top_k,request_id?,trace_id?,query_id?,timeout_ms?",
            "request_id,trace_id,query_id,store_refs,store_refs_present,store_refs_verified,run_snapshot_path,query_index_path,slice_index_refs_json,result_ref,evidence_ref,log_path,next_action,provider_id,capability_id",
            "upstream /rag/clips/run proxied through codex-lan-agent plus local store_refs verification",
            "structured JSON",
            "verified_clips_run_bridge",
            "Stable MCP bridge for direct CLIPS runs with auditable request ids and filesystem-level store_refs verification."
        },
        {
            "rag_bridge_storage_lookup",
            "codex_lan_agent_rag_bridge_storage_lookup",
            "codex-lan-agent -> upstream /rag/storage/lookup",
            "kind,id?,slice_id?,trace_id?,query_id?,node_id?,edge_id?,limit?,timeout_ms?",
            "kind,found,primary,related,storage_base_path,rocksdb_path,kv_snapshot_path,result_ref,evidence_ref,log_path,next_action,provider_id,capability_id",
            "upstream /rag/storage/lookup proxied through codex-lan-agent",
            "structured JSON",
            "storage_lookup_bridge",
            "Direct bridge for stable storage-backed lookup without outer scripting."
        },
        {
            "rag_bridge_storage_page",
            "codex_lan_agent_rag_bridge_storage_page",
            "codex-lan-agent -> upstream /rag/storage/page",
            "kind,trace_id?,query_id?,test_bucket?,coverage_gap?,result_stage?,coverage_status?,run_kind?,fact_type?,limit?,offset?,timeout_ms?",
            "kind,trace_id,query_id,test_bucket,coverage_gap,result_stage,coverage_status,run_kind,fact_type,offset,limit,returned_count,total_count,has_more,next_offset,records,trace_lookup,key_prefix,result_ref,evidence_ref,log_path,next_action,provider_id,capability_id",
            "upstream /rag/storage/page proxied through codex-lan-agent",
            "structured JSON",
            "storage_page_bridge",
            "Direct bridge for paged storage-backed fact reads with continuation fields."
        },
        // -----------------------------------------------------------------
        // Memory Slice
        // -----------------------------------------------------------------
        {
            "rag_memory_slice_store",
            "codex_lan_agent_slice_glue",
            "codex-lan-agent + RAG glue",
            "rag_memory_slice_v1 = slice_id,slice_version,slice_type,task_id,session_id,turn_id,provider_id,capability_id,user_text,assistant_text,slice_summary,reasoning_level,primary_intent,confidence,result_ref,evidence_ref,audit_ref,source_type,write_mode,vector_payload,dedup_hash,slice_refs,storage_refs",
            "slice_id,slice_version,status,storage_ref,provider_id,capability_id,audit_ref,slice_refs,storage_refs",
            "sqlite-vss first, RocksDB later",
            "structured JSON",
            "slice_ingest_protocol",
            "Ingest-only protocol. Owns the memory-slice contract and storage handoff, but not dialog writing, retrieval listing, or analytical summarization."
        },
        // -----------------------------------------------------------------
        // Dialog Slice
        // -----------------------------------------------------------------
        {
            "dialog_slice_store",
            "codex_lan_agent_dialog_slice",
            "codex-lan-agent dialog slice writer",
            "slice_id,slice_version,slice_type,task_id,session_id,turn_id,provider_id,capability_id,user_text,assistant_text,slice_summary,reasoning_level,primary_intent,confidence,result_ref,evidence_ref,audit_ref,source_type,write_mode,vector_payload,dedup_hash",
            "slice_path,slice_id,slice_version,session_id,turn_id,audit_ref,canonical_slice_id,dedup_status,slice_refs,storage_refs,bytes,checksum,result",
            "dialog_slices JSONL store",
            "structured JSON",
            "dialog_slice_write_only",
            "Write-only dialog slice protocol for record_dialog_slice. Do not add retrieval, memory ingest, or analytical summarization here."
        },
        {
            "dialog_slice_analysis",
            "codex_lan_agent_dialog_slice_reader",
            "codex-lan-agent dialog slice analyzer",
            "session_id?,max_entries",
            "analysis_root,slice_file_count,latest_slice_path,summary,result",
            "dialog_slices JSONL store",
            "structured JSON",
            "dialog_slice_read_and_summarize",
            "Read-and-summary protocol for analyze_dialog_slices only. Do not make this capability write slices or perform vector ingest."
        },
        {
            "remote_session_task_list",
            "codex_lan_agent_remote_session_reader",
            "codex-lan-agent remote-session task list",
            "session_id?,task_group_id?,runner?,max_entries?",
            "latest_session_id,latest_task_group_id,latest_task_id,latest_runner,latest_task_log_ref,latest_result_ref,latest_evidence_ref,latest_resolved_log_path,latest_resolved_result_ref,latest_resolved_evidence_ref,visible_count,result,summary",
            "8095 remote-session list + task manager",
            "structured JSON",
            "remote_session_task_listing",
            "Use this as the unified task list interface for remote AI chats. Task discovery comes from 8095; file content reads still go to 18080."
        },
        {
            "remote_session_task_refs",
            "codex_lan_agent_remote_session_reader",
            "codex-lan-agent remote-session task ref resolver",
            "session_id,task_group_id?,task_id?,runner?",
            "session_id,turn_id,task_id,task_group_id,runner,audit_ref,task_log_ref,result_ref,evidence_ref,resolved_log_path,resolved_result_ref,resolved_evidence_ref,task_status,result,summary",
            "8095 remote-session store + local task manager",
            "structured JSON",
            "remote_session_task_result_resolution",
            "Use the 8095 remote-session supervision/task surface to resolve task refs first, then read actual file content from 18080. Do not infer tests task results from recent events."
        },
        {
            "remote_task_result_refs",
            "codex_lan_agent_remote_event_reader",
            "codex-lan-agent remote task result ref query",
            "session_id?,task_group?,runner?,command_name?,max_entries?",
            "latest_task_id,latest_task_log_ref,latest_result_ref,latest_evidence_ref,latest_resolved_log_path,latest_resolved_result_ref,latest_resolved_evidence_ref,visible_count,matched_count,result,summary",
            "remote_control_events.jsonl + task manager",
            "structured JSON",
            "remote_task_result_resolution",
            "Read-only query surface for mapping remote events to actual task/log/result landing refs without changing runner semantics."
        },
        {
            "single_file_patch",
            "codex_lan_agent_patch_pipeline",
            "codex-lan-agent",
            "file_path,new_content,old_hash?,request_id?,trace_id?,patch_id?,reason?,allow_empty_content?",
            "request_id,trace_id,patch_id,file_path,normalized_path,old_hash,new_hash,diff_hash,patch_audit_id,backup_path,log_path,result",
            "workspace_root + patch backup in log_root",
            "structured JSON",
            "surgical_single_file_patch",
            "High-risk write capability. Preview, approval, revert plan, and post-verify are required before treating apply as complete."
        },
        {
            "patch_audit_trail",
            "codex_lan_agent_patch_audit",
            "codex-lan-agent",
            "patch_id",
            "patch_id,event_count,events_jsonl,audit_event_path,result,summary",
            "patch_audit_events.jsonl",
            "structured JSON",
            "patch_chain_replay",
            "Read-only replay surface for preview/apply/verify/revert audit events of one patch_id."
        },
        {
            "trace_audit_trail",
            "codex_lan_agent_trace_audit",
            "codex-lan-agent",
            "trace_id",
            "trace_id,event_count,source_count,available_source_count,matched_source_count,events_jsonl,audit_event_path,mcp_trace_audit_event_path,audit_sources,result,summary",
            "patch_audit_events.jsonl,mcp_trace_audit_events.jsonl",
            "structured JSON",
            "trace_replay",
            "Read-only replay surface for all currently supported audit events sharing one trace_id."
        },
        {
            "goal_supervision_status",
            "codex_lan_agent_goal_supervision",
            "codex-lan-agent",
            "trace_id,goal_id?",
            "trace_id,goal_id,event_count,clips_first_decision,clips_first_next_tool,clips_first_reason,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,pre_guard_status,post_guard_status,acceptance_status,acceptance_reason,supervision_alarm,supervision_alarm_code,supervision_alarm_message,progress_target_count,progress_completed_count,progress_pending_count,progress_failed_count,progress_skipped_count,next_actions_count,next_action_0_tool_name,next_action_0_safety_class,next_action_0_reason,result,summary",
            "mcp_trace_audit_events.jsonl",
            "structured JSON",
            "goal_acceptance_and_supervision",
            "Read-only acceptance surface for service-side closed-loop executors. Use this to decide continue, complete, or alarm without trusting model text."
        },
        {
            "cxparser_flow_execution",
            "codex_lan_agent_cxparser_flow",
            "codex-lan-agent",
            "test_statement?,flow_id?,params_json?,arguments_text?,trace_id?,goal_id?",
            "status,flow_id,test_statement,cxparser_status,cxparser_backend_kind,runtime_binding_available,runtime_binding_source,runtime_binding_entrypoint,runtime_binding_contract,cxparser_public_build_root,cxparser_public_build_contract,task_id,result_ref,evidence_ref,resolved_log_path,supervision_status,goal_status,assistant_response_allowed,final_answer_allowed,next_call_json,next_actions_count,facts_json,candidate_next_action_json,result,summary",
            "embedded cxparser flow manifest + cxparser runtime adapter or queued task bridge + CLIPS result guard",
            "structured JSON",
            "scriptable_tool_flow_supervision",
            "Stable MCP-compatible entry for cxparser-backed testing. Prefer direct test_statement input and treat cxparser as the source-of-truth test pipeline instead of rebuilding outer MCP orchestration."
        },
        {
            "patch_verification_pipeline",
            "codex_lan_agent_patch_verify",
            "codex-lan-agent",
            "patch_id,file_path,expected_hash?,contains_text?,forbidden_text?,request_id?,trace_id?,reason?",
            "patch_id,file_path,expected_hash,actual_hash,verification_ok,semantic_outcome,repair_candidate_id,repair_candidate_reason,next_action,result",
            "workspace readback + patch_audit_events.jsonl",
            "structured JSON",
            "patch_result_verification",
            "Read-only verification surface for patch readback with structured repair candidates."
        },
        // -----------------------------------------------------------------
        // Deferred Storage Layers
        // -----------------------------------------------------------------
        {
            "slice_dedup_metadata",
            "rocksdb_deferred",
            "RocksDB (deferred)",
            "slice_id,hash,task_id,session_id,strategy_key",
            "dedup_status,canonical_slice_id,metadata_ref,provider_id,capability_id,slice_refs,storage_refs",
            "RocksDB",
            "structured JSON",
            "dedup_hash_and_metadata",
            "Deferred layer for hash, dedup, and task/session/strategy metadata."
        },
        {
            "long_term_experience_search",
            "milvus_deferred",
            "Milvus (deferred)",
            "query,capability,top_k,filters",
            "experience_hits,provider_refs,confidence,next_action,provider_id,capability_id,slice_refs,storage_refs",
            "Milvus vector store",
            "structured JSON",
            "large_scale_vector_experience",
            "Deferred large-scale experience recall service behind MCP."
        }
    };
    return specs;
}
