# CLIPS Rule Diagnostics Tool Plan

Future observability tools should analyze CLIPS rules without moving workflow decisions into C++ dispatch.

## Tool Set

| Tool | Output |
|---|---|
| `lan_agent_clips_rules_parse` | templates, rules, LHS facts, RHS asserts, salience JSON |
| `lan_agent_clips_rules_dependency_graph` | rule dependency JSON and Graphviz DOT |
| `lan_agent_clips_rules_simulation` | firing timeline and final `clips_decision` |
| `lan_agent_clips_rules_impact_report` | coverage, conflict, redundancy, dead-rule, impact report |
| `lan_agent_mcp_flow_visualize` | MCP flow DOT/Mermaid from JSONL or dialog slices |
| `lan_agent_mcp_flow_analyze` | violations, loops, retries, missing continuations |
| `lan_agent_mcp_flow_export` | HTML/DOT/Mermaid/Markdown bundle |

## Dependency Rule

If rule A asserts a fact matched by rule B, emit:

```text
rule-A -> rule-B [label=fact_name]
```

First-stage templates:

```text
mcp_tool_request
mcp_tool_result
mcp_tool_chain
clips_decision
mcp_flow_observation
mcp_flow_expectation
```

## Required Report Questions

1. Which rules fired for the current task?
2. Which rule selected the next tool?
3. Which rule blocked final completion?
4. Did the model ignore `required_tool_arguments_json`?
5. Are there conflicting rules with the same salience and overlapping LHS?
6. Are there dead facts or dead rules?
7. Can the current chain be rendered as DOT/Mermaid?
