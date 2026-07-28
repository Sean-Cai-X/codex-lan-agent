; cxparser-driven build/test preflight rules.

(defrule block-build-without-preflight
  (declare (salience 70))
  (mcp_tool_request (tool_name ?tool&:(or (eq ?tool "lan_agent_build_target")
                                          (eq ?tool "lan_agent_run_ctest_target")))
                    (preflight_status ?status&:(or (eq ?status "missing")
                                                   (eq ?status "false")
                                                   (eq ?status "blocked"))))
  =>
  (assert (clips_decision
    (domain "cxparser_preflight_guard")
    (target ?tool)
    (decision "block")
    (verification "not_verified")
    (reason_code "missing_cxparser_preflight")
    (next_action "call lan_agent_preflight_build_target or lan_agent_preflight_run_ctest_target first, then pass the returned preflight_ref")
    (matched_rule "block-build-without-preflight"))))

(defrule default-preflight-allow
  (declare (salience -100))
  (mcp_tool_request (tool_name ?tool))
  (not (clips_decision (domain "cxparser_preflight_guard")))
  =>
  (assert (clips_decision
    (domain "cxparser_preflight_guard")
    (target ?tool)
    (decision "allow")
    (verification "verified")
    (matched_rule "default-preflight-allow"))))
