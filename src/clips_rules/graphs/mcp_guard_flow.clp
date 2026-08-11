; CLIPS guard flow graph metadata.
; This file is intentionally data-only. Runtime routing stays in rules/*.clp;
; observability tools can parse these facts to build DOT/Mermaid graphs.

(deftemplate mcp_guard_flow_edge
  (slot flow_id)
  (slot from)
  (slot to)
  (slot label (default ""))
  (slot edge_type (default "control"))
  (slot reason_code (default "")))

(deffacts directory-comment-cleanup-flow-edges
  (mcp_guard_flow_edge
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (from "user_input")
    (to "lan_agent_mcp_route")
    (label "intent: comment cleanup")
    (edge_type "control"))
  (mcp_guard_flow_edge
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (from "lan_agent_mcp_route")
    (to "lan_agent_list_directory")
    (label "build file manifest")
    (edge_type "control"))
  (mcp_guard_flow_edge
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (from "lan_agent_list_directory")
    (to "lan_agent_probe_text_file")
    (label "one concrete file")
    (edge_type "data"))
  (mcp_guard_flow_edge
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (from "lan_agent_probe_text_file")
    (to "lan_agent_delete_text_range_window_atomic")
    (label "probe_ref")
    (edge_type "data"))
  (mcp_guard_flow_edge
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (from "lan_agent_delete_text_range_window_atomic")
    (to "lan_agent_delete_text_range_window_atomic")
    (label "has_more=true")
    (edge_type "loop")
    (reason_code "text_range_delete_incomplete"))
  (mcp_guard_flow_edge
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (from "lan_agent_delete_text_range_window_atomic")
    (to "verification_gate")
    (label "has_more=false")
    (edge_type "control"))
  (mcp_guard_flow_edge
    (flow_id "directory_comment_cleanup_bounded_window_v1")
    (from "verification_gate")
    (to "final_answer")
    (label "all completion fields true")
    (edge_type "control")))
