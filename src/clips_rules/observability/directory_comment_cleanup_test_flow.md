# Directory Comment Cleanup Test Flow

Use this semantic test with another local AI connected to MCP.

## Input

```text
Delete comments in D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo\cximage.
Use MCP tools only. Do not manually edit file bodies in chat.
```

## Expected Chain

```text
lan_agent_mcp_route
  -> lan_agent_list_directory
  -> lan_agent_probe_text_file
  -> lan_agent_delete_text_range_window_atomic
  -> lan_agent_delete_text_range_window_atomic ...
  -> next file probe/delete ...
  -> final answer only after completion gate
```

## Required Checks

Each non-terminal result must expose or preserve:

```text
matched_rule
reason_code
required_tool_name
required_tool_arguments_json
next_call_json
analysis_allowed
batch_completion
has_more
terminal_state
completion_claim_allowed
final_answer_allowed
verification_ok
result_ref
evidence_ref
```

## Failure Classes

```text
required_tool_arguments_ignored
manual_content_processing_forbidden
non_terminal_completion_claim
directory_manifest_not_terminal
text_range_delete_incomplete
context_growth_unbounded
```

## Pass Conditions

Basic:

```text
CLIPS_FLOW_ROUTE_PASS
```

The flow is routed away from directory body reads and into bounded window deletion.

Full:

```text
DIRECTORY_COMMENT_CLEANUP_FLOW_ACCEPTED
```

All files are complete and the final result has:

```text
terminal_state=true
completion_claim_allowed=true
final_answer_allowed=true
verification_ok=true
```
