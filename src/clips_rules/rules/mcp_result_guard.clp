; MCP post-result verification rules.

(defrule invalid-direct-answer-json-fragment
  (declare (salience 50))
  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_run_local_chat")
                                         (eq ?tool "lan_agent_ventriloquist_reply")
                                         (eq ?tool "lan_agent_remote_session_new_turn")
                                         (eq ?tool "lan_agent_remote_session_append_turn")
                                         (eq ?tool "rag.query")))
                   (direct_answer "{"))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "allow")
    (verification "not_verified")
    (reason_code "bad_direct_answer_fragment")
    (next_action "re-run with a concrete tool-backed answer and non-fragment business reply")
    (matched_rule "invalid-direct-answer-json-fragment"))))

(defrule invalid-direct-answer-empty
  (declare (salience 45))
  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_run_local_chat")
                                         (eq ?tool "lan_agent_ventriloquist_reply")
                                         (eq ?tool "lan_agent_remote_session_new_turn")
                                         (eq ?tool "lan_agent_remote_session_append_turn")
                                         (eq ?tool "rag.query")))
                   (direct_answer ""))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "allow")
    (verification "not_verified")
    (reason_code "empty_direct_answer")
    (next_action "return a non-empty direct_answer or route through fallback evidence collection")
    (matched_rule "invalid-direct-answer-empty"))))

(defrule invalid-direct-answer-label-token
  (declare (salience 44))
  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_run_local_chat")
                                         (eq ?tool "lan_agent_ventriloquist_reply")
                                         (eq ?tool "lan_agent_remote_session_new_turn")
                                         (eq ?tool "lan_agent_remote_session_append_turn")
                                         (eq ?tool "rag.query")))
                   (direct_answer "direct_answer"))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "allow")
    (verification "not_verified")
    (reason_code "bad_direct_answer_label_token")
    (next_action "re-run with the business answer text instead of the direct_answer label")
    (matched_rule "invalid-direct-answer-label-token"))))

(defrule invalid-ai-conclusion-flag
  (declare (salience 40))
  (mcp_tool_result (tool_name ?tool) (ai_conclusion_valid "false"))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "allow")
    (verification "not_verified")
    (reason_code "ai_conclusion_invalid")
    (next_action "attach raw tool evidence before returning this conclusion")
    (matched_rule "invalid-ai-conclusion-flag"))))

(defrule analysis-only-chat-claimed-execution-without-evidence
  (declare (salience 41))
  (mcp_tool_chain (request_type "analysis_review")
                  (chain_phase "post_result"))
  (mcp_tool_result (tool_name ?tool)
                   (ai_conclusion_valid "false")
                   (result_ref "")
                   (evidence_ref "")
                   (task_id ""))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "allow")
    (verification "not_verified")
    (reason_code "analysis_only_execution_claim_without_evidence")
    (next_action "use real MCP execution tools for edits/builds/tests and return task_id, result_ref, and evidence_ref")
    (matched_rule "analysis-only-chat-claimed-execution-without-evidence"))))

(defrule execution-task-result-missing-traceable-ref
  (declare (salience 39))
  (mcp_tool_chain (execution_class "execute")
                  (chain_phase "post_result"))
  (mcp_tool_result (tool_name ?tool)
                   (task_id "")
                   (result_ref "")
                   (evidence_ref "")
                   (log_path "")
                   (error ""))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "allow")
    (verification "not_verified")
    (reason_code "execution_result_missing_traceable_ref")
    (next_action "return task_id, result_ref, or evidence_ref for execution-class tools before treating the result as trusted evidence")
    (matched_rule "execution-task-result-missing-traceable-ref"))))

(defrule audited-write-result-missing-proof
  (declare (salience 38))
  (mcp_tool_chain (request_type "file_mutation")
                  (chain_phase "post_result"))
  (mcp_tool_result (tool_name ?tool)
                   (result_ref "")
                   (evidence_ref "")
                   (log_path "")
                   (error ""))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "allow")
    (verification "not_verified")
    (reason_code "write_result_missing_audit_ref")
    (next_action "return patch_id plus result_ref/evidence_ref/log_path before downstream consumers trust this write")
    (matched_rule "audited-write-result-missing-proof"))))

(defrule text-range-delete-result-still-pending-by-has-more
  (declare (salience 49))
  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_delete_text_range_window_atomic")
                                         (eq ?tool "lan_agent_delete_next_text_range_atomic")))
                   (has_more "true"))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "text_range_delete_incomplete")
    (next_action "tool_call_only: deletion is not complete; call the same delete tool with next_start_line/probe_ref until has_more=false before any final completion claim")
    (route_target ?tool)
    (matched_rule "text-range-delete-result-still-pending-by-has-more"))))

(defrule text-range-delete-result-still-pending-by-continuation
  (declare (salience 48))
  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_delete_text_range_window_atomic")
                                         (eq ?tool "lan_agent_delete_next_text_range_atomic")))
                   (continue_required "true"))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "text_range_delete_continue_required")
    (next_action "tool_call_only: continue the delete loop; current write was verified but the overall comment deletion task is not complete")
    (route_target ?tool)
    (matched_rule "text-range-delete-result-still-pending-by-continuation"))))

(defrule non-terminal-result-forbids-final-answer
  (declare (salience 47))
  (mcp_tool_result (tool_name ?tool)
                   (terminal_state "false")
                   (completion_claim_allowed "false"))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "non_terminal_result_forbids_final_answer")
    (next_action "tool_call_only: result is non-terminal; continue with next_call_json or move the continuation into task_memory budget runner before any completion claim")
    (route_target ?tool)
    (matched_rule "non-terminal-result-forbids-final-answer"))))

(defrule flow-task-list-required-forbids-final-answer
  (declare (salience 54))
  (mcp_tool_result (tool_name ?tool)
                   (flow_task_list_required "true")
                   (flow_current_task_id ?current&:(neq ?current "T6"))
                   (completion_claim_allowed "false"))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "flow_task_list_not_terminal")
    (next_action "tool_call_only: fixed flow task list is not at T6; execute the declared required_tool_arguments_json before any completion claim")
    (route_target ?tool)
    (matched_rule "flow-task-list-required-forbids-final-answer"))))

(defrule route-result-without-resolved-tool-forbids-final-answer
  (declare (salience 53))
  (mcp_tool_result (tool_name "lan_agent_mcp_route")
                   (tool_use_decision "no_tool_resolved"))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target "lan_agent_mcp_route")
    (decision "route")
    (verification "not_verified")
    (reason_code "route_no_tool_resolved_not_terminal")
    (next_action "tool_call_only: route did not resolve an executable internal tool; provide concrete file_path/directory_path and primary_intent, or restart from lan_agent_mcp_route with an explicit routeable request before any completion claim")
    (route_target "lan_agent_mcp_route")
    (matched_rule "route-result-without-resolved-tool-forbids-final-answer"))))

(defrule directory-list-result-requires-declared-continuation
  (declare (salience 52))
  (mcp_tool_result (tool_name "lan_agent_list_directory")
                   (analysis_allowed "false")
                   (batch_completion "incomplete"))
  =>
  (assert (mcp_flow_observation
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (phase "post_result")
    (tool_name "lan_agent_list_directory")
    (status "needs_continue")
    (expected_next_tool "required_tool_arguments_json")
    (terminal_allowed "false")
    (completion_allowed "false")
    (reason_code "directory_manifest_not_terminal")
    (matched_rule "directory-list-result-requires-declared-continuation")))
  (assert (mcp_flow_expectation
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (current_tool "lan_agent_list_directory")
    (expected_next_tool "required_tool_arguments_json")
    (continuation_required "true")
    (manual_content_processing_allowed "false")
    (reason_code "directory_manifest_must_continue_by_required_tool_arguments")))
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target "lan_agent_list_directory")
    (decision "route")
    (verification "not_verified")
    (reason_code "directory_manifest_not_terminal")
    (next_action "tool_call_only: directory listing is an intermediate manifest; call required_tool_arguments_json exactly. Do not summarize, do not read file bodies manually, and do not claim completion.")
    (route_target "lan_agent_list_directory")
    (matched_rule "directory-list-result-requires-declared-continuation"))))

(defrule directory-read-result-requires-declared-continuation
  (declare (salience 51))
  (mcp_tool_result (tool_name "lan_agent_read_directory_files")
                   (analysis_allowed "false")
                   (batch_completion "incomplete"))
  =>
  (assert (mcp_flow_observation
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (phase "post_result")
    (tool_name "lan_agent_read_directory_files")
    (status "needs_continue")
    (expected_next_tool "required_tool_arguments_json")
    (terminal_allowed "false")
    (completion_allowed "false")
    (reason_code "directory_content_batch_not_terminal")
    (matched_rule "directory-read-result-requires-declared-continuation")))
  (assert (mcp_flow_expectation
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (current_tool "lan_agent_read_directory_files")
    (expected_next_tool "required_tool_arguments_json")
    (continuation_required "true")
    (manual_content_processing_allowed "false")
    (reason_code "directory_content_batch_must_continue_by_required_tool_arguments")))
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target "lan_agent_read_directory_files")
    (decision "route")
    (verification "not_verified")
    (reason_code "directory_content_batch_not_terminal")
    (next_action "tool_call_only: file content batch is not a completion state; call required_tool_arguments_json exactly or freeze/resume the task. Manual editing in chat is forbidden for this flow.")
    (route_target "lan_agent_read_directory_files")
    (matched_rule "directory-read-result-requires-declared-continuation"))))

(defrule directory-scope-next-file-requires-probe
  (declare (salience 56))
  (mcp_tool_result (tool_name ?tool)
                   (directory_scope_active "true")
                   (directory_scope_incomplete "true")
                   (directory_next_probe_call_json ?next&:(neq ?next "")))
  =>
  (assert (mcp_flow_observation
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (phase "post_result")
    (tool_name ?tool)
    (status "needs_continue")
    (expected_next_tool "lan_agent_probe_text_file")
    (terminal_allowed "false")
    (completion_allowed "false")
    (reason_code "directory_scope_next_file_required")
    (matched_rule "directory-scope-next-file-requires-probe")))
  (assert (mcp_flow_expectation
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (current_tool ?tool)
    (expected_next_tool "lan_agent_probe_text_file")
    (continuation_required "true")
    (manual_content_processing_allowed "false")
    (reason_code "directory_scope_next_file_required")))
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "directory_scope_next_file_required")
    (next_action "tool_call_only: directory cleanup still has unprocessed files; call directory_next_probe_call_json for the next file before any completion claim")
    (route_target "lan_agent_probe_text_file")
    (matched_rule "directory-scope-next-file-requires-probe"))))

(defrule directory-scope-remaining-files-forbid-terminal
  (declare (salience 55))
  (mcp_tool_result (tool_name ?tool)
                   (directory_scope_active "true")
                   (terminal_state "true")
                   (directory_remaining_code_file_count ?remaining&:(and (neq ?remaining "") (neq ?remaining "0"))))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "directory_scope_remaining_files_forbid_terminal")
    (next_action "tool_call_only: directory-scope operation cannot be terminal while remaining files are recorded")
    (route_target "lan_agent_probe_text_file")
    (matched_rule "directory-scope-remaining-files-forbid-terminal"))))

(defrule declared-next-call-json-requires-continuation
  (declare (salience 43))
  (mcp_tool_result (tool_name ?tool)
                   (next_call_json ?next&:(neq ?next "")))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "declared_next_call_requires_continuation")
    (next_action "tool_call_only: result declared next_call_json; execute that continuation before any completion claim")
    (matched_rule "declared-next-call-json-requires-continuation"))))

(defrule final-answer-disallowed-by-result
  (declare (salience 46))
  (mcp_tool_result (tool_name ?tool)
                   (final_answer_allowed "false"))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "final_answer_disallowed_by_result")
    (next_action "tool_call_only: the tool result explicitly disallows final answer; execute the required MCP continuation or use task_memory_execute_continuation_budget")
    (route_target ?tool)
    (matched_rule "final-answer-disallowed-by-result"))))

(defrule invalid-result-missing-hash
  (declare (salience 35))
  (mcp_tool_result (tool_name ?tool) (result_hash ""))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "allow")
    (verification "not_verified")
    (reason_code "result_hash_missing")
    (next_action "finalize result envelope with result_hash before downstream consumption")
    (matched_rule "invalid-result-missing-hash"))))

(defrule invalid-result-missing-schema
  (declare (salience 34))
  (mcp_tool_result (tool_name ?tool) (schema_version ""))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "allow")
    (verification "not_verified")
    (reason_code "schema_version_missing")
      (next_action "finalize result envelope with schema_version before downstream consumption")
      (matched_rule "invalid-result-missing-schema"))))

(defrule incomplete-read-result-requires-continuation
  (declare (salience 33))
(mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_read_text_file")
                                         (eq ?tool "lan_agent_list_directory")
                                         (eq ?tool "lan_agent_read_directory_files")
                                         (eq ?tool "lan_agent_run_cxparser_flow")))
                   (task_completion "incomplete"))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "read_chain_incomplete")
    (next_action "tool_call_only: continue the declared next_call_json chain until task_completion=complete and analysis_allowed=true")
    (route_target ?tool)
    (matched_rule "incomplete-read-result-requires-continuation"))))

(defrule directory-batch-read-still-pending
  (declare (salience 48))
(mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_read_text_file")
                                         (eq ?tool "lan_agent_list_directory")
                                         (eq ?tool "lan_agent_read_directory_files")
                                         (eq ?tool "lan_agent_run_cxparser_flow")
                                         (eq ?tool "lan_agent_final_answer")))
                   (analysis_allowed "false")
                   (batch_completion "incomplete")
                   (remaining_batch_file_count ?count&:(neq ?count "0")))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "directory_batch_pending")
    (next_action "tool_call_only: directory file list is complete; call the required tool before forming a final conclusion")
    (route_target ?tool)
    (matched_rule "directory-batch-read-still-pending"))))

(defrule default-mcp-result-verified
  (declare (salience -100))
  (mcp_tool_result (tool_name ?tool))
  (not (clips_decision (domain "mcp_result_guard")))
  =>
  (assert (clips_decision
    (domain "mcp_result_guard")
    (target ?tool)
    (decision "allow")
    (verification "verified")
    (matched_rule "default-mcp-result-verified"))))
