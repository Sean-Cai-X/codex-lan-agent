; CMM-specific fact template extensions for codex-lan-agent MCP guard integration.

(deftemplate cmm_project_state
  (slot repo_path (default ""))
  (slot normalized_project (default ""))
  (slot project_indexed (default "false"))
  (slot index_status (default "unknown"))
  (slot last_check_time (default ""))
  (slot ensure_action (default ""))
  (slot parent_project (default ""))
  (slot note (default "")))

(deftemplate cmm_search_request
  (slot repo_path (default ""))
  (slot project (default ""))
  (slot query (default ""))
  (slot path_filter (default ""))
  (slot file_pattern (default ""))
  (slot search_mode (default "semantic"))
  (slot requires_init (default "false")))

(deftemplate cmm_workflow_stage
  (slot stage (default "init"))
  (slot project_ready (default "false"))
  (slot search_ready (default "false"))
  (slot current_phase (default "pre_call"))
  (slot completed_steps (default ""))
  (slot pending_steps (default ""))
  (slot error_stage (default ""))
  (slot error_reason (default "")))