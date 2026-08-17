; cxparser-driven build/test preflight rules.

(defrule block-ctest-without-preflight
  (declare (salience 70))
  (mcp_tool_request (tool_name ?tool&:(eq ?tool "lan_agent_run_ctest_target"))
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
    (matched_rule "block-ctest-without-preflight"))))

(defrule allow-build-target-auto-preflight
  (declare (salience 80))
  (mcp_tool_request (tool_name "lan_agent_build_target")
                    (build_dir ?build_dir&:(neq ?build_dir ""))
                    (preflight_status ?status&:(or (eq ?status "missing")
                                                   (eq ?status "false"))))
  =>
  (assert (clips_decision
    (domain "cxparser_preflight_guard")
    (target "lan_agent_build_target")
    (decision "allow")
    (verification "verified")
    (reason_code "auto_preflight_allowed")
    (next_action "lan_agent_build_target will perform internal auto-preflight before executing")
    (matched_rule "allow-build-target-auto-preflight"))))

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
