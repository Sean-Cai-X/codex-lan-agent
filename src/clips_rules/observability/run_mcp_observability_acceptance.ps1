param(
  [string]$RuleRoot = ".\src\clips_rules",
  [Parameter(Mandatory=$true)][string]$InputJsonl,
  [string]$OutRoot = ".\logs\mcp_observability_acceptance\latest"
)

$ErrorActionPreference = "Stop"

function Resolve-ExistingPath {
  param([string]$PathValue)
  return (Resolve-Path -LiteralPath $PathValue).Path
}

function Ensure-Directory {
  param([string]$PathValue)
  New-Item -ItemType Directory -Force -Path $PathValue | Out-Null
  return (Resolve-Path -LiteralPath $PathValue).Path
}

function Read-JsonFile {
  param([string]$PathValue)
  return Get-Content -Raw -LiteralPath $PathValue | ConvertFrom-Json
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$clipsObserver = Join-Path $scriptRoot "clips_observer.ps1"
$flowObserver = Join-Path $scriptRoot "mcp_flow_observer.ps1"

$resolvedRuleRoot = Resolve-ExistingPath $RuleRoot
$resolvedInputJsonl = Resolve-ExistingPath $InputJsonl
$resolvedOutRoot = Ensure-Directory $OutRoot

$rulesOut = Ensure-Directory (Join-Path $resolvedOutRoot "rules")
$flowOut = Ensure-Directory (Join-Path $resolvedOutRoot "flow")

$rulesJsonText = powershell -ExecutionPolicy Bypass -File $clipsObserver `
  -RuleRoot $resolvedRuleRoot `
  -OutDir $rulesOut
if ($LASTEXITCODE -ne 0) {
  throw "clips_observer failed with exit code $LASTEXITCODE"
}

$flowJsonText = powershell -ExecutionPolicy Bypass -File $flowObserver `
  -InputJsonl $resolvedInputJsonl `
  -OutDir $flowOut
if ($LASTEXITCODE -ne 0) {
  throw "mcp_flow_observer failed with exit code $LASTEXITCODE"
}

$rulesSummary = $rulesJsonText | ConvertFrom-Json
$flowSummary = $flowJsonText | ConvertFrom-Json
$flowAnalysis = Read-JsonFile (Join-Path $flowOut "flow_analysis.json")
$rulesParse = Read-JsonFile (Join-Path $rulesOut "rules_parse.json")

$requiredRuleArtifacts = @(
  "rules_parse.json",
  "rules_graph.dot",
  "rules_graph.mmd",
  "rules_fact_graph.dot",
  "rules_fact_graph.mmd",
  "rules_impact_report.md"
)
$requiredFlowArtifacts = @(
  "flow_events.jsonl",
  "flow_analysis.json",
  "flow_state.json",
  "flow_state_graph.dot",
  "flow_state_graph.mmd",
  "flow_state_dashboard.html",
  "flow_graph.dot",
  "flow_graph.mmd",
  "flow_report.md",
  "violations.json",
  "violations.md"
)

$missing = New-Object System.Collections.Generic.List[string]
foreach ($name in $requiredRuleArtifacts) {
  if (-not (Test-Path -LiteralPath (Join-Path $rulesOut $name))) {
    $missing.Add("rules/$name")
  }
}
foreach ($name in $requiredFlowArtifacts) {
  if (-not (Test-Path -LiteralPath (Join-Path $flowOut $name))) {
    $missing.Add("flow/$name")
  }
}

$violationCount = [int]$flowSummary.violation_count
$rulesReady = ($missing.Count -eq 0 -and [int]$rulesSummary.rule_count -gt 0)
$flowReady = ($missing.Count -eq 0 -and [int]$flowSummary.event_count -gt 0)
$accepted = ($rulesReady -and $flowReady -and $violationCount -eq 0)
$conclusion = if ($accepted) {
  "MCP_OBSERVABILITY_ACCEPTED"
} elseif ($rulesReady -and $flowReady) {
  "MCP_OBSERVABILITY_REPORT_READY_WITH_VIOLATIONS"
} else {
  "MCP_OBSERVABILITY_INCOMPLETE"
}

$summary = [pscustomobject]@{
  status = "success"
  conclusion = $conclusion
  accepted = $accepted
  rule_root = $resolvedRuleRoot
  input_jsonl = $resolvedInputJsonl
  out_root = $resolvedOutRoot
  rules = [pscustomobject]@{
    template_count = [int]$rulesSummary.template_count
    rule_count = [int]$rulesSummary.rule_count
    edge_count = [int]$rulesSummary.edge_count
    flow_count = [int]$rulesSummary.flow_count
    conflict_candidate_count = [int]$rulesSummary.conflict_candidate_count
  }
  flow = [pscustomobject]@{
    event_count = [int]$flowSummary.event_count
    tool_call_count = [int]$flowSummary.tool_call_count
    tool_result_count = [int]$flowSummary.tool_result_count
    violation_count = $violationCount
    completion_state = [string]$flowSummary.completion_state
    current_node = [string]$flowSummary.current_node
    next_expected_node = [string]$flowSummary.next_expected_node
  }
  missing_artifacts = @($missing)
  key_outputs = [pscustomobject]@{
    rules_report = (Join-Path $rulesOut "rules_impact_report.md")
    rules_fact_graph_dot = (Join-Path $rulesOut "rules_fact_graph.dot")
    flow_report = (Join-Path $flowOut "flow_report.md")
    flow_graph_dot = (Join-Path $flowOut "flow_graph.dot")
    flow_state = (Join-Path $flowOut "flow_state.json")
    flow_state_graph_dot = (Join-Path $flowOut "flow_state_graph.dot")
    flow_state_dashboard = (Join-Path $flowOut "flow_state_dashboard.html")
    violations = (Join-Path $flowOut "violations.md")
  }
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $resolvedOutRoot "acceptance_summary.json") -Encoding UTF8

$index = New-Object System.Collections.Generic.List[string]
$index.Add("# MCP Observability Acceptance")
$index.Add("")
$index.Add("## Conclusion")
$index.Add("")
$index.Add("- Code: ``$conclusion``")
$index.Add("- Accepted: ``$accepted``")
$index.Add("- Violations: $violationCount")
$index.Add("- Missing artifacts: $($missing.Count)")
$index.Add("")
$index.Add("## Inputs")
$index.Add("")
$index.Add("- Rule root: ``$resolvedRuleRoot``")
$index.Add("- Input JSONL: ``$resolvedInputJsonl``")
$index.Add("")
$index.Add("## Rule Observability")
$index.Add("")
$index.Add("- Templates: $($summary.rules.template_count)")
$index.Add("- Rules: $($summary.rules.rule_count)")
$index.Add("- Rule dependency edges: $($summary.rules.edge_count)")
$index.Add("- Flow scripts: $($summary.rules.flow_count)")
$index.Add("- Conflict candidates: $($summary.rules.conflict_candidate_count)")
$index.Add("")
$index.Add("Artifacts:")
$index.Add("")
foreach ($name in $requiredRuleArtifacts) {
  $index.Add("- ``rules/$name``")
}
$index.Add("")
$index.Add("## MCP Flow Observability")
$index.Add("")
$index.Add("- Events: $($summary.flow.event_count)")
$index.Add("- Tool calls: $($summary.flow.tool_call_count)")
$index.Add("- Tool results: $($summary.flow.tool_result_count)")
$index.Add("- Violations: $($summary.flow.violation_count)")
$index.Add("- Completion state: ``$($summary.flow.completion_state)``")
$index.Add("- Current node: ``$($summary.flow.current_node)``")
$index.Add("- Next expected node: ``$($summary.flow.next_expected_node)``")
$index.Add("")
$index.Add("Artifacts:")
$index.Add("")
foreach ($name in $requiredFlowArtifacts) {
  $index.Add("- ``flow/$name``")
}
$index.Add("")
$index.Add("## Violation Classes")
$index.Add("")
if ($flowAnalysis.violations -and $flowAnalysis.violations.Count -gt 0) {
  $classes = @($flowAnalysis.violations | ForEach-Object { $_.violation_class } | Select-Object -Unique)
  foreach ($class in $classes) {
    $count = @($flowAnalysis.violations | Where-Object { $_.violation_class -eq $class }).Count
    $index.Add("- ``$class``: $count")
  }
} else {
  $index.Add("- None")
}
$index.Add("")
$index.Add("## Next Decision")
$index.Add("")
if ($accepted) {
  $index.Add("The observed conversation followed the MCP flow gates. It can be used as a clean baseline.")
} else {
  $index.Add("Review ``flow/violations.md`` first. Fix CLIPS/flow guidance or client behavior before treating the conversation as accepted.")
}
$index | Set-Content -LiteralPath (Join-Path $resolvedOutRoot "index.md") -Encoding UTF8

$summary | ConvertTo-Json -Compress
