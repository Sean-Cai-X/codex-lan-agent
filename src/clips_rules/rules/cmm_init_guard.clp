; CMM (Codebase-Memory-MCP) initialization and search workflow guard rules.
; These rules enforce the correct sequence for CMM operations:
;   Stage 1: Prepare (index_repository)
;   Stage 2: Validate (ensure_indexed, index_status)
;   Stage 3: Search (search_code, search_graph)
;   Stage 4: Analyze (query_graph, get_architecture)

; ============================================================================
; Test rule: Always route lan_agent_cmm_search_code for debugging
; ============================================================================

(defrule test-cmm-search-route
  (declare (salience 10))
  (mcp_tool_request (tool_name "lan_agent_cmm_search_code"))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target "lan_agent_cmm_search_code")
    (decision "route")
    (verification "not_verified")
    (reason_code "test_cmm_search_route")
    (next_action "Test rule triggered - CLIPS rules are loading correctly")
    (route_target "lan_agent_cmm_index_status")
    (matched_rule "test-cmm-search-route"))))

; ============================================================================
; Stage 1: Block CMM search operations before initialization
; ============================================================================

(defrule block-cmm-search-before-ensure-indexed
  (declare (salience 95))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_search_code")
                                          (eq ?tool "lan_agent_cmm_search_graph")
                                          (eq ?tool "lan_agent_cmm_query_graph")
                                          (eq ?tool "lan_agent_cmm_get_code_snippet")
                                          (eq ?tool "lan_agent_cmm_trace_path")
                                          (eq ?tool "lan_agent_cmm_get_graph_schema")
                                          (eq ?tool "lan_agent_cmm_get_architecture")))
                    (file_path ?repo_path&:(neq ?repo_path "")))
  (not (cmm_project_state (repo_path ?repo_path)
                          (project_indexed "true")))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "cmm_project_not_indexed")
    (next_action "Step 1: Call lan_agent_cmm_index_status with repo_path to check indexing status. Step 2: If not indexed, call lan_agent_cmm_index_repository. Step 3: Re-run the original tool.")
    (route_target "lan_agent_cmm_index_status")
    (matched_rule "block-cmm-search-before-ensure-indexed"))))

; ============================================================================
; Stage 2: Route ensure_indexed as the first CMM step
; ============================================================================

(defrule route-cmm-ensure-indexed-as-first-step
  (declare (salience 93))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_search_code")
                                          (eq ?tool "lan_agent_cmm_search_graph")
                                          (eq ?tool "lan_agent_cmm_query_graph")
                                          (eq ?tool "lan_agent_cmm_get_code_snippet")
                                          (eq ?tool "lan_agent_cmm_trace_path")
                                          (eq ?tool "lan_agent_cmm_get_graph_schema")
                                          (eq ?tool "lan_agent_cmm_get_architecture")))
                    (file_path ?repo_path&:(neq ?repo_path ""))
                    (probe_ready "false"))
  (not (cmm_project_state (repo_path ?repo_path)
                          (project_indexed "true")))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "cmm_ensure_indexed_required")
    (next_action "Step 1: Call lan_agent_cmm_index_status with repo_path. Step 2: If not indexed, call lan_agent_cmm_index_repository. Step 3: Run the original tool with the resolved project name.")
    (route_target "lan_agent_cmm_index_status")
    (matched_rule "route-cmm-ensure-indexed-as-first-step"))))

; ============================================================================
; Stage 3: Allow ensure_indexed as a preparatory step
; ============================================================================

(defrule allow-cmm-ensure-indexed
  (declare (salience 92))
  (mcp_tool_request (tool_name "lan_agent_cmm_index_status"))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target "lan_agent_cmm_index_status")
    (decision "allow")
    (verification "verified")
    (matched_rule "allow-cmm-ensure-indexed"))))

; ============================================================================
; Stage 4: Update project state after ensure_indexed result
; ============================================================================

(defrule update-project-state-after-ensure-indexed
  (declare (salience 91))
  (mcp_tool_result (tool_name "lan_agent_cmm_index_status")
                   (status ?status&:(or (eq ?status "success")
                                        (eq ?status "ok")
                                        (eq ?status "pass"))))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target "lan_agent_cmm_index_status")
    (decision "allow")
    (verification "verified")
    (reason_code "cmm_project_state_updated")
    (next_action "Project state updated. You may now proceed with lan_agent_cmm_search_code using the normalized_project value.")
    (matched_rule "update-project-state-after-ensure-indexed"))))

; ============================================================================
; Stage 5: Block search_code if project parameter is missing
; ============================================================================

(defrule block-cmm-search-without-project-parameter
  (declare (salience 94))
  (mcp_tool_request (tool_name "lan_agent_cmm_search_code")
                    (primary_intent ?intent&:(or (eq ?intent "code_search")
                                                 (eq ?intent "find_algorithm")
                                                 (eq ?intent "semantic_search")))
                    (file_path ?repo_path&:(neq ?repo_path ""))
                    (probe_ready "true"))
  (not (cmm_project_state (repo_path ?repo_path)
                          (normalized_project ?proj&:(neq ?proj ""))))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target "lan_agent_cmm_search_code")
    (decision "route")
    (verification "not_verified")
    (reason_code "cmm_project_name_not_resolved")
    (next_action "Call lan_agent_cmm_index_status first to get the normalized_project name, then re-run lan_agent_cmm_search_code with the project parameter")
    (route_target "lan_agent_cmm_index_status")
    (matched_rule "block-cmm-search-without-project-parameter"))))

; ============================================================================
; Stage 6: Route re-indexing when project needs reset
; ============================================================================

(defrule route-cmm-re-indexing-when-needed
  (declare (salience 90))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_index_repository")
                                          (eq ?tool "lan_agent_cmm_detect_changes")))
                    (file_path ?repo_path&:(neq ?repo_path "")))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target ?tool)
    (decision "route")
    (verification "not_verified")
    (reason_code "cmm_reindexing_recommended")
    (next_action "If previous indexing is corrupted or out-of-date, call lan_agent_cmm_delete_project first, then lan_agent_cmm_index_repository to rebuild the index. Otherwise proceed with the current tool.")
    (route_target "lan_agent_cmm_delete_project")
    (matched_rule "route-cmm-re-indexing-when-needed"))))

; ============================================================================
; Stage 7: Allow delete_project for re-indexing preparation
; ============================================================================

(defrule allow-cmm-delete-project
  (declare (salience 89))
  (mcp_tool_request (tool_name "lan_agent_cmm_delete_project")
                    (primary_intent "reindex_preparation"))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target "lan_agent_cmm_delete_project")
    (decision "allow")
    (verification "verified")
    (matched_rule "allow-cmm-delete-project"))))

(defrule block-cmm-delete-project-without-intent
  (declare (salience 88))
  (mcp_tool_request (tool_name "lan_agent_cmm_delete_project")
                    (primary_intent ""))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target "lan_agent_cmm_delete_project")
    (decision "block")
    (verification "not_verified")
    (reason_code "missing_delete_intent")
    (next_action "Provide explicit primary_intent='reindex_preparation' before deleting a CMM project index")
    (matched_rule "block-cmm-delete-project-without-intent"))))

; ============================================================================
; Stage 8: Allow verified search on indexed project
; ============================================================================

(defrule allow-cmm-search-on-verified-project
  (declare (salience 85))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_search_code")
                                          (eq ?tool "lan_agent_cmm_search_graph")
                                          (eq ?tool "lan_agent_cmm_query_graph")
                                          (eq ?tool "lan_agent_cmm_get_code_snippet")
                                          (eq ?tool "lan_agent_cmm_trace_path")
                                          (eq ?tool "lan_agent_cmm_get_graph_schema")
                                          (eq ?tool "lan_agent_cmm_get_architecture")))
                    (file_path ?repo_path&:(neq ?repo_path "")))
  (cmm_project_state (repo_path ?repo_path)
                     (project_indexed "true"))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target ?tool)
    (decision "allow")
    (verification "verified")
    (matched_rule "allow-cmm-search-on-verified-project"))))

; ============================================================================
; Stage 9: Suggest path_filter to refine search scope
; ============================================================================

(defrule suggest-path-filter-for-subdirectory-search
  (declare (salience 82))
  (mcp_tool_request (tool_name "lan_agent_cmm_search_code")
                    (file_path ?repo_path&:(neq ?repo_path ""))
                    (primary_intent ?intent&:(or (eq ?intent "subdirectory_search")
                                                 (eq ?intent "focused_search"))))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target "lan_agent_cmm_search_code")
    (decision "allow")
    (verification "verified")
    (reason_code "cmm_search_path_filter_suggested")
    (next_action "Include path_filter and file_pattern parameters to narrow the search scope and improve accuracy")
    (matched_rule "suggest-path-filter-for-subdirectory-search"))))

; ============================================================================
; Stage 10: Workflow completion tracking
; ============================================================================

(defrule track-cmm-workflow-stage-transition
  (declare (salience 80))
  (mcp_tool_result (tool_name ?tool)
                   (status ?status&:(or (eq ?status "success")
                                        (eq ?status "ok")
                                        (eq ?status "pass"))))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target ?tool)
    (decision "allow")
    (verification "verified")
    (reason_code "cmm_workflow_step_completed")
    (next_action "Workflow step completed. Update cmm_project_state and cmm_workflow_stage facts to reflect progress")
    (matched_rule "track-cmm-workflow-stage-transition"))))

; ============================================================================
; Stage 11: Handle CMM errors gracefully
; ============================================================================

(defrule handle-cmm-project-not-found-error
  (declare (salience 75))
  (mcp_tool_result (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_search_code")
                                          (eq ?tool "lan_agent_cmm_search_graph")))
                   (error ?err&:(or (eq ?err "project not found or not indexed")
                                    (eq ?err "project_not_found")
                                    (eq ?err "not_indexed"))))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target ?tool)
    (decision "block")
    (verification "not_verified")
    (reason_code "cmm_project_missing_or_not_indexed")
    (next_action "Step 1: Call lan_agent_cmm_list_projects to check available projects. Step 2: If project missing, call lan_agent_cmm_index_repository to index it. Step 3: If project exists but not indexed, wait for indexing or call lan_agent_cmm_index_status")
    (matched_rule "handle-cmm-project-not-found-error"))))

(defrule handle-cmm-pattern-required-error
  (declare (salience 74))
  (mcp_tool_result (tool_name "lan_agent_cmm_search_code")
                   (error ?err&:(or (eq ?err "pattern is required")
                                    (eq ?err "missing_pattern")
                                    (eq ?err "query_required"))))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target "lan_agent_cmm_search_code")
    (decision "block")
    (verification "not_verified")
    (reason_code "cmm_search_pattern_missing")
    (next_action "Provide the search query via the 'query' parameter, which will be mapped to CMM's 'pattern' field")
    (matched_rule "handle-cmm-pattern-required-error"))))

; ============================================================================
; Stage 12: Default rules - allow CMM tools without special handling
; ============================================================================

(defrule default-cmm-init-allow
  (declare (salience -100))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_cmm_index_repository")
                                          (eq ?tool "lan_agent_cmm_list_projects")
                                          (eq ?tool "lan_agent_cmm_index_status")
                                          (eq ?tool "lan_agent_cmm_detect_changes")
                                          (eq ?tool "lan_agent_cmm_search_code")
                                          (eq ?tool "lan_agent_cmm_search_graph")
                                          (eq ?tool "lan_agent_cmm_query_graph")
                                          (eq ?tool "lan_agent_cmm_get_code_snippet")
                                          (eq ?tool "lan_agent_cmm_trace_path")
                                          (eq ?tool "lan_agent_cmm_get_graph_schema")
                                          (eq ?tool "lan_agent_cmm_get_architecture")
                                          (eq ?tool "lan_agent_cmm_delete_project"))))
  (not (clips_decision (domain "cmm_init_guard")))
  =>
  (assert (clips_decision
    (domain "cmm_init_guard")
    (target ?tool)
    (decision "allow")
    (verification "verified")
    (matched_rule "default-cmm-init-allow"))))