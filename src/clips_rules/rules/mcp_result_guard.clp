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
