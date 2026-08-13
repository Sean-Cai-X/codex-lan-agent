# MCP CLIPS Flow Observability

This directory defines the observability contract for MCP tool flows.

Boundary:

- C++ remains the atomic tool layer.
- CLIPS rules decide allow/block/route and completion gates.
- Flow JSON files describe standard tool sequences.
- Observation schemas describe how to render and diagnose tool chains.

Current first flow:

```text
directory_comment_cleanup_bounded_window_v1
```

It enforces:

- directory listing is only a manifest step;
- one concrete file is probed before mutation;
- comment deletion uses `lan_agent_delete_text_range_window_atomic`;
- the default window size is `max_lines=200`;
- `has_more=true` forbids final completion claims;
- final completion requires:

```text
terminal_state=true
completion_claim_allowed=true
final_answer_allowed=true
verification_ok=true
```

Expected observability outputs for future tooling:

```text
flow_events.jsonl
flow_graph.dot
flow_graph.mmd
flow_state.json
flow_state_graph.dot
flow_state_graph.mmd
flow_state_dashboard.html
flow_report.md
violations.md
```

Current offline observer:

```powershell
powershell -ExecutionPolicy Bypass -File .\src\clips_rules\observability\clips_observer.ps1 `
  -RuleRoot .\src\clips_rules `
  -OutDir .\logs\clips_observability\latest
```

Validated outputs:

```text
rules_parse.json
rules_graph.dot
rules_graph.mmd
rules_fact_graph.dot
rules_fact_graph.mmd
rules_impact_report.md
directory_comment_cleanup_bounded_window_v1.dot
directory_comment_cleanup_bounded_window_v1.mmd
```

MCP conversation flow observer:

```powershell
powershell -ExecutionPolicy Bypass -File .\src\clips_rules\observability\mcp_flow_observer.ps1 `
  -InputJsonl D:\2026-08-11_03-06-38_conv_59805d8e__d_codex_workdir_sea.jsonl `
  -OutDir .\logs\mcp_flow_observability\conv_59805d8e
```

Validated outputs:

```text
flow_events.jsonl
flow_analysis.json
flow_graph.dot
flow_graph.mmd
flow_state.json
flow_state_graph.dot
flow_state_graph.mmd
flow_state_dashboard.html
flow_report.md
violations.json
violations.md
```

`flow_graph.*` is the raw event timeline. `flow_state.*` is the fixed workflow
projection used for human monitoring: each node records status, visit count,
latest step, current node, next expected node, and completion state.

First validated failure classes:

```text
non_terminal_assistant_text
required_tool_arguments_ignored
required_tool_arguments_mismatched
analysis_text_before_allowed
directory_manifest_not_terminal
manual_content_processing_forbidden
tool_result_failed
```

One-command acceptance:

```powershell
powershell -ExecutionPolicy Bypass -File .\src\clips_rules\observability\run_mcp_observability_acceptance.ps1 `
  -RuleRoot .\src\clips_rules `
  -InputJsonl D:\2026-08-11_03-06-38_conv_59805d8e__d_codex_workdir_sea.jsonl `
  -OutRoot .\logs\mcp_observability_acceptance\conv_59805d8e
```

Acceptance conclusions:

```text
MCP_OBSERVABILITY_ACCEPTED
MCP_OBSERVABILITY_REPORT_READY_WITH_VIOLATIONS
MCP_OBSERVABILITY_INCOMPLETE
```
