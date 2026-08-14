; MCP pre-call allow/block/route rules.

(defrule route-mismatched-pending-continuation
  (declare (salience 96))
  (mcp_tool_request (tool_name ?tool)
                    (pending_continuation_active "true")
                    (continuation_takeover_allowed "true")
                    (pending_required_tool ?expected&:(neq ?expected ""))
                    (pending_required_arguments_json ?args&:(neq ?args "")))
  (test (neq ?expected ?tool))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "pending_continuation_mismatch")
    (route_target ?expected)
    (next_action "tool_call_only: a pending continuation already exists for this trace/goal; call its route_arguments_json exactly before any other tool")
    (matched_rule "route-mismatched-pending-continuation"))))

(defrule route-pending-continuation-stale
  (declare (salience 95))
  (mcp_tool_request (tool_name ?tool)
                    (trace_id ?trace)
                    (pending_continuation_active "true")
                    (continuation_takeover_allowed "true")
                    (pending_required_tool ?expected&:(neq ?expected ""))
                    (pending_required_arguments_json ?args&:(neq ?args ""))
                    (pending_trace_id ?pending_trace&:(neq ?pending_trace "")))
  (test (eq ?expected ?tool))
  (test (neq ?pending_trace ?trace))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "continuation_stale")
    (route_target ?expected)
    (next_action "tool_call_only: this is the expected tool but the saved continuation context is missing or changed; call route_arguments_json exactly")
    (matched_rule "route-pending-continuation-stale"))))

(defrule allow-single-file-patch-preview
  (declare (salience 80))
  (mcp_tool_request (tool_name "lan_agent_preview_patch")
                    (path_within_workspace "true")
                    (file_count "1"))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target "lan_agent_preview_patch")
    (decision "allow")
    (verification "verified")
    (matched_rule "allow-single-file-patch-preview"))))

(defrule block-single-file-patch-apply-without-explicit-intent
  (declare (salience 89))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_apply_single_file_patch")
                                          (eq ?tool "lan_agent_apply_diff_patch")
                                          (eq ?tool "lan_agent_revert_single_file_patch")))
                    (explicit_user_intent "false"))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "block")
    (verification "not_verified")
    (reason_code "missing_patch_intent")
    (next_action "provide a non-empty reason or repair_candidate intent before apply")
    (matched_rule "block-single-file-patch-apply-without-explicit-intent"))))

(defrule block-stepwise-file-tool-multi-item-request
  (declare (salience 88))
  (mcp_tool_request (tool_name ?tool)
                    (single_step_required "true")
                    (max_items_per_call ?count&:(neq ?count "")&:(neq ?count "1")))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "block")
    (verification "not_verified")
    (reason_code "multi_item_file_step_not_allowed")
    (next_action "redo this file operation with max_ranges_per_call=1 or max_windows_per_call=1; process one item, verify, then rescan")
    (matched_rule "block-stepwise-file-tool-multi-item-request"))))

(defrule block-broad-file-mutation-for-stepwise-editing-intent
  (declare (salience 88))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_write_text_file")
                                          (eq ?tool "lan_agent_apply_single_file_patch")
                                          (eq ?tool "lan_agent_apply_diff_patch")))
                    (primary_intent ?intent&:(or (eq ?intent "comment_cleanup")
                                                 (eq ?intent "text_cleaning")
                                                 (eq ?intent "localized_edit")
                                                 (eq ?intent "source_edit_planning")
                                                 (eq ?intent "remove_comments")
                                                 (eq ?intent "strip_comments"))))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "block")
    (verification "not_verified")
    (reason_code "bulk_file_mutation_not_allowed_for_stepwise_edit")
    (next_action "use the single-step loop: lan_agent_scan_text_ranges(max_ranges_per_call=1), lan_agent_prepare_edit_windows(max_windows_per_call=1), one atomic delete/replace, verify, then rescan")
    (matched_rule "block-broad-file-mutation-for-stepwise-editing-intent"))))

(defrule block-multi-file-patch-in-phase1
  (declare (salience 87))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_preview_patch")
                                          (eq ?tool "lan_agent_apply_single_file_patch")
                                          (eq ?tool "lan_agent_apply_diff_patch")
                                          (eq ?tool "lan_agent_revert_single_file_patch")))
                    (file_count ?count&:(neq ?count "1")))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "block")
    (verification "not_verified")
    (reason_code "multi_file_patch_not_allowed_phase1")
    (next_action "split the change into one single-file patch candidate")
    (matched_rule "block-multi-file-patch-in-phase1"))))

(defrule block-single-file-patch-without-revert-plan
  (declare (salience 86))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_apply_single_file_patch")
                                          (eq ?tool "lan_agent_apply_diff_patch")
                                          (eq ?tool "lan_agent_revert_single_file_patch")))
                    (requires_revert_plan "true")
                    (revert_plan_ready "false"))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "block")
    (verification "not_verified")
    (reason_code "missing_patch_revert_plan")
    (next_action "ensure backup/revert plan is available before apply")
    (matched_rule "block-single-file-patch-without-revert-plan"))))

(defrule block-high-risk-write-without-path
  (declare (salience 85))
  (mcp_tool_chain (request_type "file_mutation"))
  (mcp_tool_request (tool_name ?tool)
                    (file_path ""))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "block")
    (verification "not_verified")
    (reason_code "missing_file_path")
    (next_action "provide a concrete single target file_path before write or patch execution")
    (matched_rule "block-high-risk-write-without-path"))))

(defrule route-code-format-cleanup-to-clang-format
  (declare (salience 85))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_read_text_file")
                                          (eq ?tool "lan_agent_scan_text_ranges")
                                          (eq ?tool "lan_agent_prepare_edit_windows")
                                          (eq ?tool "lan_agent_delete_line_atomic")
                                          (eq ?tool "lan_agent_delete_content_atomic")
                                          (eq ?tool "lan_agent_delete_next_text_range_atomic")
                                          (eq ?tool "lan_agent_delete_text_range_window_atomic")
                                          (eq ?tool "lan_agent_write_text_file")
                                          (eq ?tool "lan_agent_apply_single_file_patch")
                                          (eq ?tool "lan_agent_apply_diff_patch")))
                    (primary_intent "code_format")
                    (file_path ?file_path&:(neq ?file_path "")))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "route")
    (verification "verified")
    (reason_code "code_format_cleanup_prefers_clang_format")
    (route_target "lan_agent_format_code_file")
    (next_action "use lan_agent_format_code_file dry_run=true first for whitespace/newline/code-format cleanup; do not scan comments or delete text ranges for formatting")
    (matched_rule "route-code-format-cleanup-to-clang-format"))))

(defrule route-file-text-operations-to-probe-first
  (declare (salience 84))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_find_line_metadata")
                                          (eq ?tool "lan_agent_find_content_matches")
                                          (eq ?tool "lan_agent_locate_text_lines")
                                          (eq ?tool "lan_agent_scan_text_ranges")
                                          (eq ?tool "lan_agent_prepare_edit_windows")
                                          (eq ?tool "lan_agent_delete_line_atomic")
                                          (eq ?tool "lan_agent_delete_content_atomic")
                                          (eq ?tool "lan_agent_delete_next_text_range_atomic")
                                          (eq ?tool "lan_agent_delete_text_range_window_atomic")
                                          (eq ?tool "lan_agent_insert_after_anchor_atomic")
                                          (eq ?tool "lan_agent_replace_line_range_atomic")
                                          (eq ?tool "lan_agent_apply_single_file_patch")
                                          (eq ?tool "lan_agent_apply_diff_patch")))
                    (file_path ?file_path&:(neq ?file_path ""))
                    (probe_required "true")
                    (probe_ready "false"))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "missing_probe_preflight")
    (route_target "lan_agent_probe_text_file")
    (next_action "call lan_agent_probe_text_file first and continue only with the emitted next_call_json/probe_ref chain before any scan, edit, or patch step")
    (matched_rule "route-file-text-operations-to-probe-first"))))

(defrule route-read-text-file-to-window-delete-for-comment-cleanup
  (declare (salience 84))
  (mcp_tool_request (tool_name "lan_agent_read_text_file")
                    (primary_intent "comment_cleanup")
                    (probe_ready "true"))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target "lan_agent_read_text_file")
    (decision "route")
    (verification "verified")
    (reason_code "comment_cleanup_prefers_window_delete")
    (route_target "lan_agent_delete_text_range_window_atomic")
    (next_action "call lan_agent_delete_text_range_window_atomic with max_lines=200; do not use read/scan/prepare for comment cleanup")
    (matched_rule "route-read-text-file-to-window-delete-for-comment-cleanup"))))

(defrule route-comment-cleanup-scaffold-to-window-delete
  (declare (salience 83))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_scan_text_ranges")
                                          (eq ?tool "lan_agent_prepare_edit_windows")))
                    (primary_intent "comment_cleanup")
                    (probe_ready "true"))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "route")
    (verification "verified")
    (reason_code "comment_cleanup_scaffold_prefers_bounded_window")
    (route_target "lan_agent_delete_text_range_window_atomic")
    (next_action "use lan_agent_delete_text_range_window_atomic with max_lines=200 as the default bounded comment cleanup step; reserve single-range delete for boundary-spanning leftovers")
    (matched_rule "route-comment-cleanup-scaffold-to-window-delete"))))

(defrule route-directory-content-read-to-window-delete-for-comment-cleanup
  (declare (salience 83))
  (mcp_tool_request (tool_name "lan_agent_read_directory_files")
                    (primary_intent "comment_cleanup")
                    (file_path ?file_path&:(neq ?file_path ""))
                    (probe_ready "true"))
  =>
  (assert (mcp_flow_expectation
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (current_tool "lan_agent_read_directory_files")
    (expected_next_tool "lan_agent_delete_text_range_window_atomic")
    (continuation_required "true")
    (manual_content_processing_allowed "false")
    (reason_code "comment_cleanup_must_not_read_directory_content_for_manual_edit")))
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target "lan_agent_read_directory_files")
    (decision "route")
    (verification "verified")
    (reason_code "comment_cleanup_must_use_window_delete")
    (route_target "lan_agent_delete_text_range_window_atomic")
    (next_action "tool_call_only: for directory comment cleanup, do not read file bodies for manual editing; call lan_agent_delete_text_range_window_atomic for the current file with max_lines=200 and continue by required_tool_arguments_json")
    (matched_rule "route-directory-content-read-to-window-delete-for-comment-cleanup"))))

(defrule block-directory-comment-cleanup-without-current-file
  (declare (salience 82))
  (mcp_tool_request (tool_name "lan_agent_read_directory_files")
                    (primary_intent "comment_cleanup")
                    (file_path ""))
  =>
  (assert (mcp_flow_expectation
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (current_tool "lan_agent_read_directory_files")
    (expected_next_tool "lan_agent_list_directory")
    (continuation_required "true")
    (manual_content_processing_allowed "false")
    (reason_code "directory_comment_cleanup_requires_file_manifest_first")))
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target "lan_agent_read_directory_files")
    (decision "route")
    (verification "not_verified")
    (reason_code "directory_comment_cleanup_requires_file_manifest")
    (route_target "lan_agent_list_directory")
    (next_action "tool_call_only: list the directory and select one concrete source file before any comment cleanup write; do not read all file bodies into chat context")
    (matched_rule "block-directory-comment-cleanup-without-current-file"))))

(defrule route-read-text-file-to-range-scan-for-editing-intent
  (declare (salience 82))
  (mcp_tool_request (tool_name "lan_agent_read_text_file")
                    (primary_intent ?intent&:(or (eq ?intent "text_cleaning")
                                                 (eq ?intent "localized_edit")
                                                 (eq ?intent "source_edit_planning")))
                    (probe_ready "true"))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target "lan_agent_read_text_file")
    (decision "route")
    (verification "verified")
    (reason_code "editing_intent_prefers_range_scan")
    (route_target "lan_agent_scan_text_ranges")
    (next_action "call lan_agent_scan_text_ranges with scan_mode=comments before localized edit or comment cleanup")
    (matched_rule "route-read-text-file-to-range-scan-for-editing-intent"))))

(defrule default-mcp-tool-allow
  (declare (salience -100))
  ?r <- (mcp_tool_request (tool_name ?tool))
  (not (clips_decision (domain "mcp_tool_guard")))
  =>
  (assert (clips_decision
    (domain "mcp_tool_guard")
    (target ?tool)
    (decision "allow")
    (verification "verified")
    (matched_rule "default-mcp-tool-allow"))))
