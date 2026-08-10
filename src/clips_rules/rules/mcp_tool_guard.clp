; MCP pre-call allow/block/route rules.

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
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_read_text_file")
                                          (eq ?tool "lan_agent_find_line_metadata")
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
                                          (eq ?tool "lan_agent_write_text_file")
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
    (next_action "call lan_agent_probe_text_file first and continue only with the emitted next_call_json/probe_ref chain before any text read, scan, write, or patch step")
    (matched_rule "route-file-text-operations-to-probe-first"))))

(defrule route-read-text-file-to-window-delete-for-comment-cleanup
  (declare (salience 84))
  (mcp_tool_request (tool_name "lan_agent_read_text_file")
                    (primary_intent ?intent&:(or (eq ?intent "comment_cleanup")
                                                 (eq ?intent "remove_comments")
                                                 (eq ?intent "strip_comments")
                                                 (eq ?intent "删除注释")
                                                 (eq ?intent "清理注释")
                                                 (eq ?intent "去除注释")
                                                 (eq ?intent "移除注释")
                                                 (eq ?intent "删注释")))
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
                    (primary_intent ?intent&:(or (eq ?intent "comment_cleanup")
                                                 (eq ?intent "remove_comments")
                                                 (eq ?intent "strip_comments")
                                                 (eq ?intent "delete_comments")
                                                 (eq ?intent "删除注释")
                                                 (eq ?intent "清理注释")
                                                 (eq ?intent "去除注释")
                                                 (eq ?intent "移除注释")
                                                 (eq ?intent "删注释")))
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
