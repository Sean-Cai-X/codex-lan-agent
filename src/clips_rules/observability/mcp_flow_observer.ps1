param(
  [Parameter(Mandatory=$true)][string]$InputJsonl,
  [Parameter(Mandatory=$true)][string]$OutDir,
  [string]$FlowTemplate = ""
)

$ErrorActionPreference = "Stop"

function ConvertTo-DotString {
  param([string]$Value)
  if ($null -eq $Value) { return "" }
  return (($Value -replace '\\','\\') -replace '"','\"')
}

function ConvertTo-NodeId {
  param([int]$Index)
  return "n$Index"
}

function Parse-KeyValueContent {
  param([string]$Content)
  $fields = [ordered]@{}
  foreach ($line in ($Content -split "`r?`n")) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    $idx = $line.IndexOf("=")
    if ($idx -lt 1) { continue }
    $key = $line.Substring(0, $idx).Trim()
    $value = $line.Substring($idx + 1).Trim()
    if ($key) { $fields[$key] = $value }
  }
  return $fields
}

function Try-ParseJson {
  param([string]$Text)
  try {
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    return $Text | ConvertFrom-Json
  } catch {
    return $null
  }
}

function Get-ToolCallEffectiveName {
  param($ToolCall)
  $name = ""
  $argsText = ""
  if ($ToolCall.function) {
    $name = [string]$ToolCall.function.name
    $argsText = [string]$ToolCall.function.arguments
  }
  $args = Try-ParseJson $argsText
  $target = ""
  if ($args -and $args.target_tool_name) {
    $target = [string]$args.target_tool_name
  } elseif ($args -and $args.name) {
    $target = [string]$args.name
  }
  if ($target) { return $target }
  return $name
}

function Get-ToolCallArgsSummary {
  param($ToolCall)
  if (-not $ToolCall.function) { return "" }
  $argsText = [string]$ToolCall.function.arguments
  if ($argsText.Length -gt 240) {
    return $argsText.Substring(0, 240) + "..."
  }
  return $argsText
}

function Get-ToolCallArgsRaw {
  param($ToolCall)
  if (-not $ToolCall.function) { return "" }
  return [string]$ToolCall.function.arguments
}

function Get-EffectiveArgumentObject {
  param($ParsedArgs)
  if ($null -eq $ParsedArgs) { return $null }
  if ($ParsedArgs.arguments) { return $ParsedArgs.arguments }
  return $ParsedArgs
}

function Get-ObjectPropertyNames {
  param($Obj)
  if ($null -eq $Obj) { return @() }
  return @($Obj.PSObject.Properties | ForEach-Object { $_.Name })
}

function Test-RequiredArgumentsSatisfied {
  param([string]$RequiredArgsJson, [string]$ActualArgsJson)
  $required = Try-ParseJson $RequiredArgsJson
  $actual = Try-ParseJson $ActualArgsJson
  if ($null -eq $required -or $null -eq $actual) { return $false }

  # Accept the chat-facing gateway wrapper:
  # required: {"name":"inner_tool","arguments":{...}}
  # actual:   {"mode":"call","target_tool_name":"inner_tool","arguments_json":"{...}"}
  if ($required.name -and $required.arguments -and $actual.target_tool_name) {
    $requiredName = [string]$required.name
    $actualName = [string]$actual.target_tool_name
    if ($requiredName -eq $actualName -and $actual.arguments_json) {
      $actualInnerArgs = Try-ParseJson ([string]$actual.arguments_json)
      if ($actualInnerArgs) {
        $requiredArgs = $required.arguments
        $actualArgs = $actualInnerArgs
        foreach ($name in (Get-ObjectPropertyNames $requiredArgs)) {
          $requiredValue = [string]$requiredArgs.$name
          $actualHas = $false
          foreach ($actualArgName in (Get-ObjectPropertyNames $actualArgs)) {
            if ($actualArgName -eq $name) {
              $actualHas = $true
              $actualValue = [string]$actualArgs.$actualArgName
              if ($requiredValue -ne $actualValue) {
                return $false
              }
            }
          }
          if (-not $actualHas) { return $false }
        }
        return $true
      }
    }
  }

  $requiredArgs = Get-EffectiveArgumentObject $required
  $actualArgs = Get-EffectiveArgumentObject $actual
  if ($null -eq $requiredArgs -or $null -eq $actualArgs) { return $false }
  foreach ($name in (Get-ObjectPropertyNames $requiredArgs)) {
    $requiredValue = [string]$requiredArgs.$name
    $actualHas = $false
    foreach ($actualName in (Get-ObjectPropertyNames $actualArgs)) {
      if ($actualName -eq $name) {
        $actualHas = $true
        $actualValue = [string]$actualArgs.$actualName
        if ($requiredValue -ne $actualValue) {
          return $false
        }
      }
    }
    if (-not $actualHas) { return $false }
  }
  return $true
}

function Find-NextToolCall {
  param($Events, [int]$StartIndex)
  for ($i = $StartIndex + 1; $i -lt $Events.Count; $i++) {
    if ($Events[$i].node_type -eq "tool_call") { return $Events[$i] }
  }
  return $null
}

function New-Event {
  param(
    [int]$Index,
    [string]$NodeType,
    [string]$Label,
    [string]$Status,
    [string]$Role,
    [string]$ToolName,
    [string]$EffectiveToolName,
    [string]$Detail,
    [hashtable]$Fields,
    [string]$MessageId,
    [string]$ParentId,
    [string]$ToolCallId,
    [object]$Timestamp
  )
  return [pscustomobject]@{
    step_index = $Index
    node_id = ConvertTo-NodeId $Index
    node_type = $NodeType
    label = $Label
    status = $Status
    role = $Role
    tool_name = $ToolName
    effective_tool_name = $EffectiveToolName
    detail = $Detail
    fields = $Fields
    message_id = $MessageId
    parent_id = $ParentId
    tool_call_id = $ToolCallId
    timestamp = $Timestamp
  }
}

function Is-FalseLike {
  param([string]$Value)
  return $Value -eq "false" -or $Value -eq "False" -or $Value -eq "0"
}

function Is-NonTerminalResult {
  param($Event)
  if ($Event.node_type -ne "tool_result") { return $false }
  $f = $Event.fields
  if (Is-FalseLike ([string]$f["terminal_state"])) { return $true }
  if (Is-FalseLike ([string]$f["completion_claim_allowed"])) { return $true }
  if (Is-FalseLike ([string]$f["final_answer_allowed"])) { return $true }
  if (Is-FalseLike ([string]$f["verification_ok"])) { return $true }
  if ([string]$f["analysis_allowed"] -eq "false") { return $true }
  if ([string]$f["continue_required"] -eq "true") { return $true }
  if ([string]$f["has_more"] -eq "true") { return $true }
  if ([string]$f["batch_completion"] -eq "incomplete") { return $true }
  return $false
}

function Get-ResultFailure {
  param($Event)
  if ($Event.node_type -ne "tool_result") { return $false }
  $f = $Event.fields
  if ([string]$f["status"] -eq "failed") { return $true }
  if ([string]$f["ok"] -eq "false") { return $true }
  if ([string]$f["exit_code"] -and [string]$f["exit_code"] -ne "0") { return $true }
  return $false
}

function Resolve-FlowTemplatePath {
  param([string]$ExplicitPath)
  if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
    return (Resolve-Path -LiteralPath $ExplicitPath).Path
  }
  $scriptRoot = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
  return (Resolve-Path -LiteralPath (Join-Path $scriptRoot "..\flows\directory_comment_cleanup.flow.json")).Path
}

function Get-FlowNodeForTool {
  param([string]$ToolName, [string]$PreviousNodeId)
  if ($ToolName -eq "lan_agent_mcp_route") { return "route_request" }
  if ($ToolName -eq "lan_agent_list_directory") { return "list_directory" }
  if ($ToolName -eq "lan_agent_read_directory_files") { return "list_directory" }
  if ($ToolName -eq "lan_agent_delete_text_range_window_atomic") { return "delete_current_file_window" }
  if ($ToolName -eq "lan_agent_probe_text_file") {
    if ($PreviousNodeId -eq "delete_current_file_window") { return "probe_next_file" }
    return "probe_current_file"
  }
  if ($ToolName -eq "final_answer") { return "complete" }
  return ""
}

function Get-StateRank {
  param([string]$Status)
  switch ($Status) {
    "not_started" { return 0 }
    "running" { return 1 }
    "success" { return 2 }
    "needs_continue" { return 3 }
    "failed" { return 4 }
    "violated" { return 5 }
    default { return 0 }
  }
}

function Set-NodeStatus {
  param($StateMap, [string]$NodeId, [string]$Status)
  if ([string]::IsNullOrWhiteSpace($NodeId)) { return }
  if (-not $StateMap.Contains($NodeId)) { return }
  $current = [string]$StateMap[$NodeId].status
  if ((Get-StateRank $Status) -ge (Get-StateRank $current)) {
    $StateMap[$NodeId].status = $Status
  }
}

function ConvertTo-HtmlText {
  param([string]$Value)
  if ($null -eq $Value) { return "" }
  return (($Value -replace '&','&amp;') -replace '<','&lt;') -replace '>','&gt;'
}

function ConvertTo-MermaidLabel {
  param([string]$Value)
  if ($null -eq $Value) { return "" }
  return (($Value -replace '"',"'") -replace "`r?`n"," ")
}

function Find-NextSignificantEvent {
  param($Events, [int]$StartIndex)
  for ($i = $StartIndex + 1; $i -lt $Events.Count; $i++) {
    if ($Events[$i].node_type -in @("assistant_text","tool_call","tool_result","user_input")) {
      return $Events[$i]
    }
  }
  return $null
}

$inputPath = (Resolve-Path -LiteralPath $InputJsonl).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$events = New-Object System.Collections.Generic.List[object]
$violations = New-Object System.Collections.Generic.List[object]
$lineNo = 0

foreach ($line in (Get-Content -LiteralPath $inputPath)) {
  $lineNo++
  if ([string]::IsNullOrWhiteSpace($line)) { continue }
  try {
    $obj = $line | ConvertFrom-Json
  } catch {
    continue
  }

  if ($obj.type -eq "session") {
    $fields = [ordered]@{ session_id = [string]$obj.id; name = [string]$obj.name }
    $events.Add((New-Event $events.Count "session" "session" "info" "system" "" "" ([string]$obj.name) $fields "" "" "" $obj.lastModified))
    continue
  }

  if ($obj.type -ne "message" -or -not $obj.message) { continue }
  $msg = $obj.message
  $role = [string]$msg.role
  $content = [string]$msg.content
  $timestamp = $msg.timestamp
  $messageId = [string]$msg.id
  $parentId = [string]$msg.parent

  if ($role -eq "user") {
    $detail = $content
    if ($detail.Length -gt 300) { $detail = $detail.Substring(0, 300) + "..." }
    $events.Add((New-Event $events.Count "user_input" "user" "info" $role "" "" $detail ([ordered]@{}) $messageId $parentId "" $timestamp))
  } elseif ($role -eq "assistant") {
    if ($msg.toolCalls) {
      foreach ($call in $msg.toolCalls) {
        $toolName = [string]$call.function.name
        $effective = Get-ToolCallEffectiveName $call
        $fields = [ordered]@{
          raw_tool_name = $toolName
          effective_tool_name = $effective
          arguments = Get-ToolCallArgsSummary $call
          arguments_raw = Get-ToolCallArgsRaw $call
        }
        $events.Add((New-Event $events.Count "tool_call" $effective "call" $role $toolName $effective ([string]$fields["arguments"]) $fields $messageId $parentId ([string]$call.id) $timestamp))
      }
    }
    if (-not [string]::IsNullOrWhiteSpace($content)) {
      $detail = $content
      if ($detail.Length -gt 420) { $detail = $detail.Substring(0, 420) + "..." }
      $events.Add((New-Event $events.Count "assistant_text" "assistant text" "text" $role "" "" $detail ([ordered]@{ content = $detail }) $messageId $parentId "" $timestamp))
    }
  } elseif ($role -eq "tool") {
    $fields = Parse-KeyValueContent $content
    $status = [string]$fields["status"]
    if (-not $status) { $status = [string]$fields["result"] }
    if (-not $status) { $status = "tool_result" }
    $toolName = [string]$fields["tool_name"]
    if (-not $toolName) { $toolName = [string]$fields["routed_tool_name"] }
    if (-not $toolName) { $toolName = "tool_result" }
    $effective = [string]$fields["routed_tool_name"]
    if (-not $effective) { $effective = [string]$fields["required_tool_name"] }
    if (-not $effective) { $effective = $toolName }
    $summary = [string]$fields["summary"]
    if (-not $summary) { $summary = [string]$fields["next_action"] }
    if ($summary.Length -gt 360) { $summary = $summary.Substring(0, 360) + "..." }
    $events.Add((New-Event $events.Count "tool_result" $toolName $status $role $toolName $effective $summary $fields $messageId $parentId ([string]$msg.toolCallId) $timestamp))
  }
}

for ($i = 0; $i -lt $events.Count; $i++) {
  $event = $events[$i]
  if ($event.node_type -ne "tool_result") { continue }
  $fields = $event.fields
  $next = Find-NextSignificantEvent $events $i
  $requiredTool = [string]$fields["required_tool_name"]
  $requiredArgs = [string]$fields["required_tool_arguments_json"]
  $nonTerminal = Is-NonTerminalResult $event

  if ([string]$fields["tool_use_decision"] -eq "no_tool_resolved") {
    $violations.Add([pscustomobject]@{
      violation_class = "route_no_tool_resolved_not_executed"
      step_index = $event.step_index
      next_step_index = if ($next) { $next.step_index } else { -1 }
      reason = "The gateway route did not resolve an executable internal tool; no file operation was performed."
      expected = "explicit routable request with file_path/directory_path and primary_intent"
      observed = if ($next) { "$($next.node_type):$($next.detail)" } else { "end_of_trace" }
    })
  }

  if ($nonTerminal -and $next -and $next.node_type -eq "assistant_text") {
    $violations.Add([pscustomobject]@{
      violation_class = "non_terminal_assistant_text"
      step_index = $event.step_index
      next_step_index = $next.step_index
      reason = "Tool result is non-terminal, but the next significant event is assistant text instead of a tool call."
      expected = "tool_call_only"
      observed = $next.detail
    })
  }

  if ($requiredTool -and $requiredArgs -and $next) {
    $ok = $false
    if ($next.node_type -eq "tool_call") {
      if ($next.effective_tool_name -eq $requiredTool -or $next.tool_name -eq $requiredTool) {
        $ok = $true
      }
    }
    if (-not $ok) {
      $violations.Add([pscustomobject]@{
        violation_class = "required_tool_arguments_ignored"
        step_index = $event.step_index
        next_step_index = $next.step_index
        reason = "The result exposed required_tool_arguments_json, but the next significant event did not call the required tool."
        expected = $requiredTool
        observed = "$($next.node_type):$($next.effective_tool_name)"
      })
    }
  }

  if ($requiredTool -and $requiredArgs) {
    $nextToolCall = Find-NextToolCall $events $i
    if ($nextToolCall -and ($nextToolCall.effective_tool_name -eq $requiredTool -or $nextToolCall.tool_name -eq $requiredTool)) {
      $actualRawArgs = [string]$nextToolCall.fields["arguments_raw"]
      if (-not (Test-RequiredArgumentsSatisfied $requiredArgs $actualRawArgs)) {
        $violations.Add([pscustomobject]@{
          violation_class = "required_tool_arguments_mismatched"
          step_index = $event.step_index
          next_step_index = $nextToolCall.step_index
          reason = "The next tool call used the required tool name but did not preserve the required arguments."
          expected = $requiredArgs
          observed = $actualRawArgs
        })
      }
    }
  }

  if ([string]$fields["status"] -eq "failed" -or [string]$fields["ok"] -eq "false" -or [string]$fields["exit_code"] -ne "0" -and [string]$fields["exit_code"]) {
    $violations.Add([pscustomobject]@{
      violation_class = "tool_result_failed"
      step_index = $event.step_index
      next_step_index = if ($next) { $next.step_index } else { -1 }
      reason = "Tool result failed and must not be treated as accepted evidence."
      expected = "recover with required next tool or report blocked"
      observed = "status=$($fields["status"]); error=$($fields["error"]); error_message=$($fields["error_message"])"
    })
  }

  if ([string]$fields["analysis_allowed"] -eq "false" -and $next -and $next.node_type -eq "assistant_text") {
    $violations.Add([pscustomobject]@{
      violation_class = "analysis_text_before_allowed"
      step_index = $event.step_index
      next_step_index = $next.step_index
      reason = "analysis_allowed=false but assistant produced analysis/status text."
      expected = "continue required MCP call"
      observed = $next.detail
    })
  }

  if ([string]$fields["batch_completion"] -eq "incomplete" -and $next -and $next.node_type -eq "assistant_text") {
    $violations.Add([pscustomobject]@{
      violation_class = "directory_manifest_not_terminal"
      step_index = $event.step_index
      next_step_index = $next.step_index
      reason = "A batch or directory manifest was incomplete but the chain moved into assistant text."
      expected = "required_tool_arguments_json"
      observed = $next.detail
    })
  }

  if ($next -and $next.node_type -eq "assistant_text") {
    $text = [string]$next.detail
    if ($text -match "read the content|读取.*内容|remove comments|删除.*注释|手动|manual") {
      $violations.Add([pscustomobject]@{
        violation_class = "manual_content_processing_forbidden"
        step_index = $event.step_index
        next_step_index = $next.step_index
        reason = "Assistant text indicates manual content processing in a flow that should stay tool-driven."
        expected = "bounded atomic tool flow"
        observed = $text
      })
    }
  }
}

$resolvedFlowTemplate = Resolve-FlowTemplatePath $FlowTemplate
$flowTemplateObject = Get-Content -Raw -LiteralPath $resolvedFlowTemplate | ConvertFrom-Json
$stateMap = [ordered]@{}
$stateOrder = New-Object System.Collections.Generic.List[string]
foreach ($state in $flowTemplateObject.states) {
  $stateId = [string]$state.id
  $stateOrder.Add($stateId) | Out-Null
  $stateMap[$stateId] = [pscustomobject]@{
    id = $stateId
    tool = [string]$state.tool
    purpose = [string]$state.purpose
    status = "not_started"
    visit_count = 0
    first_step = $null
    last_step = $null
    last_event_type = ""
    last_event_status = ""
    last_detail = ""
    violations = @()
  }
}

$eventToFlowNode = @{}
$actualTransitions = New-Object System.Collections.Generic.List[object]
$previousFlowNode = ""
$lastVisitedFlowNode = ""
$lastNonTerminalNode = ""
$lastFailedNode = ""
$lastResultEvent = $null

foreach ($event in $events) {
  $mappedNode = ""
  if ($event.node_type -in @("tool_call","tool_result")) {
    $tool = [string]$event.effective_tool_name
    if (-not $tool) { $tool = [string]$event.tool_name }
    $mappedNode = Get-FlowNodeForTool $tool $previousFlowNode
  }

  if ([string]::IsNullOrWhiteSpace($mappedNode) -or -not $stateMap.Contains($mappedNode)) {
    continue
  }

  $eventToFlowNode[[string]$event.step_index] = $mappedNode
  $node = $stateMap[$mappedNode]
  $node.visit_count = [int]$node.visit_count + 1
  if ($null -eq $node.first_step) { $node.first_step = $event.step_index }
  $node.last_step = $event.step_index
  $node.last_event_type = [string]$event.node_type
  $node.last_event_status = [string]$event.status
  $node.last_detail = [string]$event.detail

  if ($event.node_type -eq "tool_call") {
    Set-NodeStatus $stateMap $mappedNode "running"
  } elseif ($event.node_type -eq "tool_result") {
    $lastResultEvent = $event
    if (Get-ResultFailure $event) {
      Set-NodeStatus $stateMap $mappedNode "failed"
      $lastFailedNode = $mappedNode
    } elseif (Is-NonTerminalResult $event) {
      Set-NodeStatus $stateMap $mappedNode "needs_continue"
      $lastNonTerminalNode = $mappedNode
    } else {
      Set-NodeStatus $stateMap $mappedNode "success"
    }
  } elseif ($event.node_type -eq "assistant_text") {
    Set-NodeStatus $stateMap $mappedNode "success"
  }

  if (-not [string]::IsNullOrWhiteSpace($previousFlowNode)) {
    $actualTransitions.Add([pscustomobject]@{
      from = $previousFlowNode
      to = $mappedNode
      step_index = $event.step_index
      event_type = [string]$event.node_type
      status = [string]$event.status
    }) | Out-Null
  }
  $previousFlowNode = $mappedNode
  $lastVisitedFlowNode = $mappedNode
}

foreach ($violation in $violations) {
  $violationNode = ""
  $key = [string]$violation.step_index
  if ($eventToFlowNode.ContainsKey($key)) {
    $violationNode = $eventToFlowNode[$key]
  } elseif (-not [string]::IsNullOrWhiteSpace($lastVisitedFlowNode)) {
    $violationNode = $lastVisitedFlowNode
  }
  if (-not [string]::IsNullOrWhiteSpace($violationNode) -and $stateMap.Contains($violationNode)) {
    Set-NodeStatus $stateMap $violationNode "violated"
    $node = $stateMap[$violationNode]
    $node.violations = @($node.violations) + @([pscustomobject]@{
      class = [string]$violation.violation_class
      step_index = $violation.step_index
      next_step_index = $violation.next_step_index
      reason = [string]$violation.reason
    })
  }
}

$nextExpectedNode = ""
if ($lastResultEvent) {
  $requiredToolFromLast = [string]$lastResultEvent.fields["required_tool_name"]
  if ($requiredToolFromLast) {
    $nextExpectedNode = Get-FlowNodeForTool $requiredToolFromLast $lastVisitedFlowNode
  }
}
if (-not $nextExpectedNode -and $lastVisitedFlowNode -and $stateMap.Contains($lastVisitedFlowNode)) {
  $templateNode = @($flowTemplateObject.states | Where-Object { [string]$_.id -eq $lastVisitedFlowNode } | Select-Object -First 1)
  if ($templateNode -and $templateNode.allowed_next) {
    $nextExpectedNode = [string]@($templateNode.allowed_next)[0]
  }
}

$currentNode = $lastVisitedFlowNode
if ($violations.Count -gt 0) {
  $latestViolation = $violations[$violations.Count - 1]
  $key = [string]$latestViolation.step_index
  if ($eventToFlowNode.ContainsKey($key)) { $currentNode = $eventToFlowNode[$key] }
} elseif ($lastFailedNode) {
  $currentNode = $lastFailedNode
} elseif ($lastNonTerminalNode) {
  $currentNode = $lastNonTerminalNode
}

$completionGateSatisfied = $false
if ($lastResultEvent) {
  $f = $lastResultEvent.fields
  $completionGateSatisfied = (
    [string]$f["terminal_state"] -eq "true" -and
    [string]$f["completion_claim_allowed"] -eq "true" -and
    [string]$f["final_answer_allowed"] -eq "true" -and
    [string]$f["verification_ok"] -eq "true"
  )
}
if ($completionGateSatisfied -and $violations.Count -eq 0) {
  Set-NodeStatus $stateMap "complete" "success"
}

$completionState = if ($violations.Count -gt 0) {
  "blocked_by_violation"
} elseif ($lastFailedNode) {
  "failed"
} elseif ($completionGateSatisfied) {
  "accepted"
} elseif ($lastNonTerminalNode) {
  "needs_continue"
} else {
  "not_complete"
}

$stateNodes = @($stateOrder | ForEach-Object { $stateMap[$_] })
$visitedNodes = @($stateNodes | Where-Object { [int]$_.visit_count -gt 0 } | ForEach-Object { $_.id })
$templateEdges = New-Object System.Collections.Generic.List[object]
foreach ($templateState in $flowTemplateObject.states) {
  $fromId = [string]$templateState.id
  foreach ($toId in @($templateState.allowed_next)) {
    if (-not [string]::IsNullOrWhiteSpace([string]$toId)) {
      $templateEdges.Add([pscustomobject]@{ from = $fromId; to = [string]$toId }) | Out-Null
    }
  }
}
$stateNodesArray = @($stateNodes | Where-Object { $null -ne $_ })
$visitedNodesArray = @($visitedNodes)
$templateEdgesArray = @($templateEdges.ToArray())
$actualTransitionsArray = @($actualTransitions.ToArray())
$stateProjection = [ordered]@{
  flow_id = [string]$flowTemplateObject.flow_id
  title = [string]$flowTemplateObject.title
  flow_template_path = $resolvedFlowTemplate
  input_jsonl = $inputPath
  completion_state = $completionState
  current_node = $currentNode
  next_expected_node = $nextExpectedNode
  completion_gate_satisfied = $completionGateSatisfied
  violation_count = $violations.Count
  visited_nodes = $visitedNodesArray
  nodes = $stateNodesArray
  template_edges = $templateEdgesArray
  actual_transitions = $actualTransitionsArray
}

$stateProjection | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutDir "flow_state.json") -Encoding UTF8

$stateColor = @{
  not_started = "#E5E7EB"
  running = "#93C5FD"
  success = "#86EFAC"
  needs_continue = "#FCD34D"
  failed = "#FCA5A5"
  violated = "#F87171"
}

$actualEdgeKeys = @{}
foreach ($transition in $actualTransitions) {
  $actualEdgeKeys["$($transition.from)->$($transition.to)"] = $true
}

$stateDot = New-Object System.Collections.Generic.List[string]
$stateDot.Add("digraph mcp_flow_state {")
$stateDot.Add("  rankdir=LR;")
$stateDot.Add('  node [fontname="Microsoft YaHei", fontsize=10, shape=box, style="rounded,filled"];')
foreach ($node in $stateNodes) {
  $fill = $stateColor[[string]$node.status]
  if (-not $fill) { $fill = "#E5E7EB" }
  $label = "$($node.id)\n$($node.status)\nvisits=$($node.visit_count)"
  if ($node.id -eq $currentNode) { $label += "\nCURRENT" }
  if ($node.id -eq $nextExpectedNode) { $label += "\nNEXT" }
  $penWidth = if ($node.id -eq $currentNode) { "3" } else { "1" }
  $stateDot.Add("  `"$($node.id)`" [fillcolor=`"$fill`", penwidth=$penWidth, label=`"$(ConvertTo-DotString $label)`"];")
}
foreach ($edge in $stateProjection.template_edges) {
  $key = "$($edge.from)->$($edge.to)"
  $color = if ($actualEdgeKeys.ContainsKey($key)) { "#2563EB" } else { "#9CA3AF" }
  $pen = if ($actualEdgeKeys.ContainsKey($key)) { "3" } else { "1" }
  $stateDot.Add("  `"$($edge.from)`" -> `"$($edge.to)`" [color=`"$color`", penwidth=$pen];")
}
$stateDot.Add("}")
$stateDot | Set-Content -LiteralPath (Join-Path $OutDir "flow_state_graph.dot") -Encoding UTF8

$stateMmd = New-Object System.Collections.Generic.List[string]
$stateMmd.Add("flowchart LR")
foreach ($node in $stateNodes) {
  $label = ConvertTo-MermaidLabel "$($node.id)<br/>$($node.status)<br/>visits=$($node.visit_count)"
  if ($node.id -eq $currentNode) { $label += "<br/>CURRENT" }
  if ($node.id -eq $nextExpectedNode) { $label += "<br/>NEXT" }
  $stateMmd.Add("  $($node.id)[`"$label`"]")
}
foreach ($edge in $stateProjection.template_edges) {
  $stateMmd.Add("  $($edge.from) --> $($edge.to)")
}
$stateMmd.Add("  classDef not_started fill:#E5E7EB,stroke:#6B7280,color:#111827;")
$stateMmd.Add("  classDef running fill:#93C5FD,stroke:#1D4ED8,color:#111827;")
$stateMmd.Add("  classDef success fill:#86EFAC,stroke:#15803D,color:#111827;")
$stateMmd.Add("  classDef needs_continue fill:#FCD34D,stroke:#B45309,color:#111827;")
$stateMmd.Add("  classDef failed fill:#FCA5A5,stroke:#B91C1C,color:#111827;")
$stateMmd.Add("  classDef violated fill:#F87171,stroke:#991B1B,color:#111827;")
foreach ($node in $stateNodes) {
  $stateMmd.Add("  class $($node.id) $($node.status);")
}
$stateMmd | Set-Content -LiteralPath (Join-Path $OutDir "flow_state_graph.mmd") -Encoding UTF8

$dashboard = New-Object System.Collections.Generic.List[string]
$dashboard.Add("<!doctype html><html><head><meta charset=`"utf-8`"><title>MCP Flow State Dashboard</title>")
$dashboard.Add("<style>body{font-family:Segoe UI,Arial,sans-serif;margin:0;background:#0f1115;color:#e5e7eb}.wrap{max-width:1280px;margin:0 auto;padding:24px}.top{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px}.metric{border:1px solid #2d333b;border-radius:8px;padding:12px;background:#161b22}.metric b{display:block;font-size:12px;color:#9ca3af}.metric span{font-size:18px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin-top:18px}.node{border:1px solid #2d333b;border-radius:8px;padding:12px;background:#161b22}.node h3{margin:0 0 8px;font-size:15px}.badge{display:inline-block;border-radius:999px;padding:2px 8px;font-size:12px;color:#111827}.not_started{background:#e5e7eb}.running{background:#93c5fd}.success{background:#86efac}.needs_continue{background:#fcd34d}.failed{background:#fca5a5}.violated{background:#f87171}.current{outline:2px solid #60a5fa}.next{box-shadow:0 0 0 2px #fbbf24 inset}pre{white-space:pre-wrap;background:#0b0d12;border:1px solid #2d333b;border-radius:8px;padding:12px;overflow:auto}.small{color:#9ca3af;font-size:12px}.violations li{margin:6px 0}</style></head><body><div class=`"wrap`">")
$dashboard.Add("<h1>MCP Flow State Dashboard</h1>")
$dashboard.Add("<div class=`"top`">")
$dashboard.Add("<div class=`"metric`"><b>Completion</b><span>$completionState</span></div>")
$dashboard.Add("<div class=`"metric`"><b>Current Node</b><span>$currentNode</span></div>")
$dashboard.Add("<div class=`"metric`"><b>Next Expected</b><span>$nextExpectedNode</span></div>")
$dashboard.Add("<div class=`"metric`"><b>Violations</b><span>$($violations.Count)</span></div>")
$dashboard.Add("</div>")
$dashboard.Add("<h2>Fixed Flow Nodes</h2><div class=`"grid`">")
foreach ($node in $stateNodes) {
  $classes = "node"
  if ($node.id -eq $currentNode) { $classes += " current" }
  if ($node.id -eq $nextExpectedNode) { $classes += " next" }
  $dashboard.Add("<section class=`"$classes`"><h3>$(ConvertTo-HtmlText $node.id)</h3><span class=`"badge $($node.status)`">$($node.status)</span><p class=`"small`">tool: $(ConvertTo-HtmlText $node.tool)</p><p>$(ConvertTo-HtmlText $node.purpose)</p><p class=`"small`">visits=$($node.visit_count); first=$($node.first_step); last=$($node.last_step)</p></section>")
}
$dashboard.Add("</div>")
$dashboard.Add("<h2>Violations</h2><ul class=`"violations`">")
if ($violations.Count -eq 0) {
  $dashboard.Add("<li>None</li>")
} else {
  foreach ($v in $violations) {
    $dashboard.Add("<li><b>$(ConvertTo-HtmlText $v.violation_class)</b> at step $($v.step_index): $(ConvertTo-HtmlText $v.reason)</li>")
  }
}
$dashboard.Add("</ul>")
$dashboard.Add("<h2>Mermaid State Graph</h2><pre>$(ConvertTo-HtmlText ($stateMmd -join "`n"))</pre>")
$dashboard.Add("<h2>Actual Transitions</h2><pre>$(ConvertTo-HtmlText (($actualTransitions | ConvertTo-Json -Depth 6)))</pre>")
$dashboard.Add("</div></body></html>")
$dashboard | Set-Content -LiteralPath (Join-Path $OutDir "flow_state_dashboard.html") -Encoding UTF8

$events | ForEach-Object { $_ | ConvertTo-Json -Depth 8 -Compress } |
  Set-Content -LiteralPath (Join-Path $OutDir "flow_events.jsonl") -Encoding UTF8

[pscustomobject]@{
  input_jsonl = $inputPath
  flow_template_path = $resolvedFlowTemplate
  event_count = $events.Count
  tool_call_count = @($events | Where-Object { $_.node_type -eq "tool_call" }).Count
  tool_result_count = @($events | Where-Object { $_.node_type -eq "tool_result" }).Count
  violation_count = $violations.Count
  completion_state = $completionState
  current_node = $currentNode
  next_expected_node = $nextExpectedNode
  flow_state_json_path = (Join-Path $OutDir "flow_state.json")
  flow_state_graph_dot_path = (Join-Path $OutDir "flow_state_graph.dot")
  flow_state_graph_mermaid_path = (Join-Path $OutDir "flow_state_graph.mmd")
  flow_state_dashboard_html_path = (Join-Path $OutDir "flow_state_dashboard.html")
  violations = $violations
} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutDir "flow_analysis.json") -Encoding UTF8

$dot = New-Object System.Collections.Generic.List[string]
$dot.Add("digraph mcp_flow {")
$dot.Add("  rankdir=TB;")
$dot.Add('  node [fontname="Microsoft YaHei", fontsize=10];')
foreach ($event in $events) {
  $shape = "box"
  $color = "#CFCFCF"
  if ($event.node_type -eq "user_input") { $shape = "oval"; $color = "#B3D9FF" }
  elseif ($event.node_type -eq "tool_call") { $shape = "box"; $color = "#D9B3FF" }
  elseif ($event.node_type -eq "tool_result") {
    $shape = "box"
    if (Is-NonTerminalResult $event) { $color = "#FFCC66" } else { $color = "#90EE90" }
  } elseif ($event.node_type -eq "assistant_text") { $shape = "note"; $color = "#F5F5F5" }
  elseif ($event.node_type -eq "session") { $shape = "folder"; $color = "#E8E8E8" }
  $label = "$($event.step_index): $($event.label)"
  if ($event.status) { $label += "\n$($event.status)" }
  if ($event.fields["matched_rule"]) { $label += "\nrule=$($event.fields["matched_rule"])" }
  if ($event.fields["reason_code"]) { $label += "\nreason=$($event.fields["reason_code"])" }
  $dot.Add("  `"$($event.node_id)`" [shape=$shape, style=filled, fillcolor=`"$color`", label=`"$(ConvertTo-DotString $label)`"];")
}
for ($i = 0; $i -lt ($events.Count - 1); $i++) {
  $dot.Add("  `"$($events[$i].node_id)`" -> `"$($events[$i + 1].node_id)`";")
}
foreach ($v in $violations) {
  $dot.Add("  `"$((ConvertTo-NodeId $v.step_index))`" -> `"$((ConvertTo-NodeId $v.next_step_index))`" [color=red, penwidth=2, label=`"$($v.violation_class)`"];")
}
$dot.Add("}")
$dot | Set-Content -LiteralPath (Join-Path $OutDir "flow_graph.dot") -Encoding UTF8

$mmd = New-Object System.Collections.Generic.List[string]
$mmd.Add("flowchart TB")
foreach ($event in $events) {
  $id = $event.node_id
  $label = "$($event.step_index): $($event.label)<br/>$($event.status)"
  if ($event.node_type -eq "user_input") {
    $mmd.Add("  $id([`"$label`"])")
  } elseif ($event.node_type -eq "assistant_text") {
    $mmd.Add("  $id[`"$label`"]")
  } else {
    $mmd.Add("  $id[`"$label`"]")
  }
}
for ($i = 0; $i -lt ($events.Count - 1); $i++) {
  $mmd.Add("  $($events[$i].node_id) --> $($events[$i + 1].node_id)")
}
foreach ($v in $violations) {
  $mmd.Add("  $((ConvertTo-NodeId $v.step_index)) -. `"$($v.violation_class)`" .-> $((ConvertTo-NodeId $v.next_step_index))")
}
$mmd | Set-Content -LiteralPath (Join-Path $OutDir "flow_graph.mmd") -Encoding UTF8

$report = New-Object System.Collections.Generic.List[string]
$report.Add("# MCP Flow Analysis Report")
$report.Add("")
$report.Add("## Summary")
$report.Add("")
$report.Add("- Input: ``$inputPath``")
$report.Add("- Events: $($events.Count)")
$report.Add("- Tool calls: $(@($events | Where-Object { $_.node_type -eq 'tool_call' }).Count)")
$report.Add("- Tool results: $(@($events | Where-Object { $_.node_type -eq 'tool_result' }).Count)")
$report.Add("- Violations: $($violations.Count)")
$report.Add("- Completion state: ``$completionState``")
$report.Add("- Current node: ``$currentNode``")
$report.Add("- Next expected node: ``$nextExpectedNode``")
$report.Add("")
$report.Add("## Fixed Flow State")
$report.Add("")
$report.Add("- Template: ``$resolvedFlowTemplate``")
$report.Add("- State JSON: ``$(Join-Path $OutDir "flow_state.json")``")
$report.Add("- State DOT: ``$(Join-Path $OutDir "flow_state_graph.dot")``")
$report.Add("- State Mermaid: ``$(Join-Path $OutDir "flow_state_graph.mmd")``")
$report.Add("- State Dashboard: ``$(Join-Path $OutDir "flow_state_dashboard.html")``")
$report.Add("")
$report.Add("## Violations")
$report.Add("")
if ($violations.Count -eq 0) {
  $report.Add("- None")
} else {
  $report.Add("| Class | Step | Next | Expected | Observed |")
  $report.Add("|---|---:|---:|---|---|")
  foreach ($v in $violations) {
    $obs = [string]$v.observed
    if ($obs.Length -gt 100) { $obs = $obs.Substring(0, 100) + "..." }
    $report.Add("| ``$($v.violation_class)`` | $($v.step_index) | $($v.next_step_index) | ``$($v.expected)`` | $obs |")
  }
}
$report.Add("")
$report.Add("## Tool Timeline")
$report.Add("")
$report.Add("| Step | Type | Tool | Status | Detail |")
$report.Add("|---:|---|---|---|---|")
foreach ($event in $events) {
  $detail = [string]$event.detail
  if ($detail.Length -gt 120) { $detail = $detail.Substring(0, 120) + "..." }
  $report.Add("| $($event.step_index) | ``$($event.node_type)`` | ``$($event.effective_tool_name)`` | ``$($event.status)`` | $detail |")
}
$report | Set-Content -LiteralPath (Join-Path $OutDir "flow_report.md") -Encoding UTF8

$violations | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutDir "violations.json") -Encoding UTF8
if ($violations.Count -gt 0) {
  $violations |
    ForEach-Object { "- ``$($_.violation_class)`` at step $($_.step_index) -> $($_.next_step_index): $($_.reason)" } |
    Set-Content -LiteralPath (Join-Path $OutDir "violations.md") -Encoding UTF8
} else {
  "No violations." | Set-Content -LiteralPath (Join-Path $OutDir "violations.md") -Encoding UTF8
}

[pscustomobject]@{
  status = "success"
  input_jsonl = $inputPath
  out_dir = (Resolve-Path -LiteralPath $OutDir).Path
  flow_template_path = $resolvedFlowTemplate
  event_count = $events.Count
  tool_call_count = @($events | Where-Object { $_.node_type -eq "tool_call" }).Count
  tool_result_count = @($events | Where-Object { $_.node_type -eq "tool_result" }).Count
  violation_count = $violations.Count
  completion_state = $completionState
  current_node = $currentNode
  next_expected_node = $nextExpectedNode
  flow_state_json_path = (Join-Path $OutDir "flow_state.json")
  flow_state_graph_dot_path = (Join-Path $OutDir "flow_state_graph.dot")
  flow_state_graph_mermaid_path = (Join-Path $OutDir "flow_state_graph.mmd")
  flow_state_dashboard_html_path = (Join-Path $OutDir "flow_state_dashboard.html")
} | ConvertTo-Json -Compress
