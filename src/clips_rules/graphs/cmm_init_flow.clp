; CMM initialization and analysis workflow graph definition.
; This describes the state machine and transitions for CMM operations.

(deftemplate cmm_state_machine
  (slot current_state (default "init"))
  (slot next_state (default ""))
  (slot transition_trigger (default ""))
  (slot required_tool (default ""))
  (slot guard_condition (default ""))
  (slot action (default "")))

; ============================================================================
; State definitions and transitions
; ============================================================================

; State: init -> validate
(defrule transition-init-to-validate
  (declare (salience 100))
  (cmm_state_machine (current_state "init"))
  =>
  (assert (cmm_state_machine
    (current_state "init")
    (next_state "validate")
    (transition_trigger "cmm_search_requested")
    (required_tool "lan_agent_cmm_index_status")
    (guard_condition "project_name_resolved=false")
    (action "Ensure the CMM project is indexed and resolve the normalized project name")))

; State: validate -> index (if not indexed)
(defrule transition-validate-to-index
  (declare (salience 99))
  (cmm_state_machine (current_state "validate"))
  =>
  (assert (cmm_state_machine
    (current_state "validate")
    (next_state "index")
    (transition_trigger "ensure_indexed_returns_indexed=false")
    (required_tool "lan_agent_cmm_index_repository")
    (guard_condition "project_indexed=false")
    (action "Index the repository or delete and re-index if corrupted")))

; State: validate -> ready (if already indexed)
(defrule transition-validate-to-ready
  (declare (salience 98))
  (cmm_state_machine (current_state "validate"))
  =>
  (assert (cmm_state_machine
    (current_state "validate")
    (next_state "ready")
    (transition_trigger "ensure_indexed_returns_indexed=true")
    (required_tool "lan_agent_cmm_index_status")
    (guard_condition "project_indexed=true")
    (action "Verify index status is complete and ready for search")))

; State: index -> validate (after indexing)
(defrule transition-index-to-validate
  (declare (salience 97))
  (cmm_state_machine (current_state "index"))
  =>
  (assert (cmm_state_machine
    (current_state "index")
    (next_state "validate")
    (transition_trigger "index_repository_completed")
    (required_tool "lan_agent_cmm_index_status")
    (guard_condition "index_complete=true")
    (action "Re-validate the project after indexing to confirm readiness")))

; State: ready -> search
(defrule transition-ready-to-search
  (declare (salience 96))
  (cmm_state_machine (current_state "ready"))
  =>
  (assert (cmm_state_machine
    (current_state "ready")
    (next_state "search")
    (transition_trigger "search_code_requested")
    (required_tool "lan_agent_cmm_search_code")
    (guard_condition "project_ready=true AND query_present=true")
    (action "Execute code search with resolved project name and query")))

; State: search -> analyze
(defrule transition-search-to-analyze
  (declare (salience 95))
  (cmm_state_machine (current_state "search"))
  =>
  (assert (cmm_state_machine
    (current_state "search")
    (next_state "analyze")
    (transition_trigger "search_completed")
    (required_tool "lan_agent_cmm_query_graph")
    (guard_condition "search_results_present=true")
    (action "Analyze search results with graph queries and architecture views")))

; State: any -> error (on failure)
(defrule transition-any-to-error
  (declare (salience 94))
  (cmm_state_machine (current_state ?state&:(or (eq ?state "init")
                                                  (eq ?state "validate")
                                                  (eq ?state "index")
                                                  (eq ?state "ready")
                                                  (eq ?state "search")
                                                  (eq ?state "analyze"))))
  =>
  (assert (cmm_state_machine
    (current_state ?state)
    (next_state "error")
    (transition_trigger "tool_call_failed")
    (required_tool "lan_agent_cmm_list_projects")
    (guard_condition "error_code!=none")
    (action "Check available projects, diagnose the failure, and consider re-indexing")))

; State: error -> init (reset and retry)
(defrule transition-error-to-init
  (declare (salience 93))
  (cmm_state_machine (current_state "error"))
  =>
  (assert (cmm_state_machine
    (current_state "error")
    (next_state "init")
    (transition_trigger "user_acknowledges_error")
    (required_tool "lan_agent_cmm_delete_project")
    (guard_condition "user_intent=retry")
    (action "Optionally delete and re-index the project, then restart the workflow")))