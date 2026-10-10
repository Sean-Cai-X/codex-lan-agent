param(
  [Parameter(Mandatory=$true)][string]$RuleRoot,
  [Parameter(Mandatory=$true)][string]$OutDir
)

$ErrorActionPreference = "Stop"

function Remove-ClipsComments {
  param([string]$Text)
  $lines = New-Object System.Collections.Generic.List[string]
  foreach ($line in ($Text -split "`r?`n")) {
    $inString = $false
    $escaped = $false
    $chars = New-Object System.Collections.Generic.List[char]
    foreach ($ch in $line.ToCharArray()) {
      if ($ch -eq '"' -and -not $escaped) {
        $inString = -not $inString
      }
      if ($ch -eq ';' -and -not $inString) {
        break
      }
      $chars.Add($ch)
      $escaped = ($ch -eq '\' -and -not $escaped)
    }
    $lines.Add((-join $chars))
  }
  return ($lines -join "`n")
}

function Get-TopLevelForms {
  param([string]$Text)
  $forms = New-Object System.Collections.Generic.List[string]
  $depth = 0
  $start = -1
  $inString = $false
  $escaped = $false
  for ($i = 0; $i -lt $Text.Length; $i++) {
    $ch = $Text[$i]
    if ($ch -eq '"' -and -not $escaped) {
      $inString = -not $inString
    }
    if ($inString) {
      $escaped = ($ch -eq '\' -and -not $escaped)
      continue
    }
    $escaped = $false
    if ($ch -eq '(') {
      if ($depth -eq 0) {
        $start = $i
      }
      $depth++
    } elseif ($ch -eq ')') {
      $depth--
      if ($depth -eq 0 -and $start -ge 0) {
        $forms.Add($Text.Substring($start, $i - $start + 1))
        $start = -1
      }
    }
  }
  return $forms
}

function Get-HeadSymbol {
  param([string]$Form)
  $match = [regex]::Match($Form, '^\(\s*([A-Za-z_][\w-]*)')
  if ($match.Success) { return $match.Groups[1].Value }
  return ""
}

function Get-FirstLevelFacts {
  param([string]$Text)
  $keywords = @("assert","bind","declare","if","not","or","and","test")
  $facts = New-Object System.Collections.Generic.HashSet[string]
  foreach ($form in (Get-TopLevelForms $Text)) {
    $symbol = Get-HeadSymbol $form
    if ($symbol -and -not $keywords.Contains($symbol)) {
      [void]$facts.Add($symbol)
    }
  }
  return @($facts | Sort-Object)
}

function Get-RegexValues {
  param([string]$Text, [string]$Pattern)
  $set = New-Object System.Collections.Generic.HashSet[string]
  foreach ($match in [regex]::Matches($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
    [void]$set.Add($match.Groups[1].Value)
  }
  return @($set | Sort-Object)
}

function ConvertTo-DotString {
  param([string]$Value)
  return (($Value -replace '\\','\\') -replace '"','\"')
}

$root = (Resolve-Path -LiteralPath $RuleRoot).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$templates = New-Object System.Collections.Generic.List[object]
$rules = New-Object System.Collections.Generic.List[object]

foreach ($file in (Get-ChildItem -LiteralPath $root -Recurse -File -Filter *.clp | Sort-Object FullName)) {
  $text = Remove-ClipsComments (Get-Content -Raw -LiteralPath $file.FullName)
  foreach ($form in (Get-TopLevelForms $text)) {
    $head = Get-HeadSymbol $form
    if ($head -eq "deftemplate") {
      $m = [regex]::Match($form, '^\(\s*deftemplate\s+([^\s()]+)', [System.Text.RegularExpressions.RegexOptions]::Singleline)
      if (-not $m.Success) { continue }
      $slots = Get-RegexValues $form '\(\s*slot\s+([^\s()]+)'
      $templates.Add([pscustomobject]@{
        name = $m.Groups[1].Value
        path = $file.FullName
        slots = $slots
      })
    } elseif ($head -eq "defrule") {
      $m = [regex]::Match($form, '^\(\s*defrule\s+([^\s()]+)\s*(.*)\)$', [System.Text.RegularExpressions.RegexOptions]::Singleline)
      if (-not $m.Success) { continue }
      $name = $m.Groups[1].Value
      $body = $m.Groups[2].Value
      $parts = $body -split '=>', 2
      $lhs = $parts[0]
      $rhs = ""
      if ($parts.Count -gt 1) { $rhs = $parts[1] }
      $salience = 0
      $sm = [regex]::Match($lhs, '\(\s*salience\s+(-?\d+)')
      if ($sm.Success) { $salience = [int]$sm.Groups[1].Value }
      $rules.Add([pscustomobject]@{
        name = $name
        path = $file.FullName
        salience = $salience
        lhs_facts = Get-FirstLevelFacts $lhs
        rhs_asserts = Get-RegexValues $rhs '\(assert\s+\(\s*([A-Za-z_][\w-]*)'
        decisions = Get-RegexValues $rhs '\(decision\s+"([^"]*)"'
        route_targets = Get-RegexValues $rhs '\(route_target\s+"([^"]*)"'
        reason_codes = Get-RegexValues $rhs '\(reason_code\s+"([^"]*)"'
      })
    }
  }
}

$flows = New-Object System.Collections.Generic.List[object]
foreach ($file in (Get-ChildItem -LiteralPath $root -Recurse -File -Filter *.flow.json | Sort-Object FullName)) {
  $json = Get-Content -Raw -LiteralPath $file.FullName | ConvertFrom-Json
  $states = @()
  foreach ($state in $json.states) { $states += $state.id }
  $flows.Add([pscustomobject]@{
    flow_id = $json.flow_id
    path = $file.FullName
    cpp_role = $json.cpp_role
    states = $states
    forbidden_transitions = $json.forbidden_transitions
  })
}

$edges = New-Object System.Collections.Generic.List[object]
foreach ($producer in $rules) {
  foreach ($consumer in $rules) {
    if ($producer.name -eq $consumer.name) { continue }
    foreach ($fact in $producer.rhs_asserts) {
      if ($consumer.lhs_facts -contains $fact) {
        $edges.Add([pscustomobject]@{ from = $producer.name; to = $consumer.name; fact = $fact })
      }
    }
  }
}

$conflicts = New-Object System.Collections.Generic.List[object]
for ($i = 0; $i -lt $rules.Count; $i++) {
  for ($j = $i + 1; $j -lt $rules.Count; $j++) {
    $left = $rules[$i]
    $right = $rules[$j]
    if ($left.salience -ne $right.salience) { continue }
    $overlap = @($left.lhs_facts | Where-Object { $right.lhs_facts -contains $_ })
    if ($overlap.Count -eq 0) { continue }
    $leftDecision = ($left.decisions -join ",")
    $rightDecision = ($right.decisions -join ",")
    $leftRoutes = ($left.route_targets -join ",")
    $rightRoutes = ($right.route_targets -join ",")
    if ($leftDecision -ne $rightDecision -or $leftRoutes -ne $rightRoutes) {
      $conflicts.Add([pscustomobject]@{
        left = $left.name
        right = $right.name
        salience = $left.salience
        overlap_facts = $overlap
        left_decision = $leftDecision
        right_decision = $rightDecision
      })
    }
  }
}

$payload = [pscustomobject]@{
  rule_root = $root
  templates = $templates
  rules = $rules
  flows = $flows
  dependency_edges = $edges
  conflict_candidates = $conflicts
}
$payload | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $OutDir "rules_parse.json") -Encoding UTF8

$dot = New-Object System.Collections.Generic.List[string]
$dot.Add("digraph clips_rules {")
$dot.Add("  rankdir=TB;")
$dot.Add('  node [shape=box, fontname="Microsoft YaHei", fontsize=10];')
foreach ($rule in $rules) {
  $label = "$($rule.name)\nsalience=$($rule.salience)"
  if ($rule.reason_codes.Count -gt 0) { $label += "\n" + (($rule.reason_codes | Select-Object -First 2) -join ",") }
  $dot.Add("  `"$(ConvertTo-DotString $rule.name)`" [label=`"$(ConvertTo-DotString $label)`"];")
}
foreach ($edge in $edges) {
  $dot.Add("  `"$(ConvertTo-DotString $edge.from)`" -> `"$(ConvertTo-DotString $edge.to)`" [label=`"$(ConvertTo-DotString $edge.fact)`"];")
}
$dot.Add("}")
$dot | Set-Content -LiteralPath (Join-Path $OutDir "rules_graph.dot") -Encoding UTF8

$factDot = New-Object System.Collections.Generic.List[string]
$factDot.Add("digraph clips_fact_rule_graph {")
$factDot.Add("  rankdir=LR;")
$factDot.Add('  node [fontname="Microsoft YaHei", fontsize=10];')
foreach ($template in $templates) {
  $factDot.Add("  `"fact:$(ConvertTo-DotString $template.name)`" [shape=ellipse, style=filled, fillcolor=`"#E8F1FF`", label=`"$(ConvertTo-DotString $template.name)`"];")
}
foreach ($rule in $rules) {
  $label = "$($rule.name)\nsalience=$($rule.salience)"
  $factDot.Add("  `"rule:$(ConvertTo-DotString $rule.name)`" [shape=box, style=filled, fillcolor=`"#FFF4D6`", label=`"$(ConvertTo-DotString $label)`"];")
  foreach ($fact in $rule.lhs_facts) {
    $factDot.Add("  `"fact:$(ConvertTo-DotString $fact)`" -> `"rule:$(ConvertTo-DotString $rule.name)`" [label=`"match`"];")
  }
  foreach ($fact in $rule.rhs_asserts) {
    $factDot.Add("  `"rule:$(ConvertTo-DotString $rule.name)`" -> `"fact:$(ConvertTo-DotString $fact)`" [label=`"assert`"];")
  }
}
$factDot.Add("}")
$factDot | Set-Content -LiteralPath (Join-Path $OutDir "rules_fact_graph.dot") -Encoding UTF8

$mmd = New-Object System.Collections.Generic.List[string]
$mmd.Add("flowchart TB")
foreach ($rule in $rules) {
  $safe = ($rule.name -replace '[^A-Za-z0-9_]', '_')
  $mmd.Add("  $safe[`"$($rule.name)<br/>salience=$($rule.salience)`"]")
}
foreach ($edge in $edges) {
  $from = ($edge.from -replace '[^A-Za-z0-9_]', '_')
  $to = ($edge.to -replace '[^A-Za-z0-9_]', '_')
  $mmd.Add("  $from -->|`"$($edge.fact)`"| $to")
}
$mmd | Set-Content -LiteralPath (Join-Path $OutDir "rules_graph.mmd") -Encoding UTF8

$factMmd = New-Object System.Collections.Generic.List[string]
$factMmd.Add("flowchart LR")
foreach ($template in $templates) {
  $safe = "fact_" + ($template.name -replace '[^A-Za-z0-9_]', '_')
  $factMmd.Add("  $safe((`"$($template.name)`"))")
}
foreach ($rule in $rules) {
  $safeRule = "rule_" + ($rule.name -replace '[^A-Za-z0-9_]', '_')
  $factMmd.Add("  $safeRule[`"$($rule.name)<br/>salience=$($rule.salience)`"]")
  foreach ($fact in $rule.lhs_facts) {
    $safeFact = "fact_" + ($fact -replace '[^A-Za-z0-9_]', '_')
    $factMmd.Add("  $safeFact -->|match| $safeRule")
  }
  foreach ($fact in $rule.rhs_asserts) {
    $safeFact = "fact_" + ($fact -replace '[^A-Za-z0-9_]', '_')
    $factMmd.Add("  $safeRule -->|assert| $safeFact")
  }
}
$factMmd | Set-Content -LiteralPath (Join-Path $OutDir "rules_fact_graph.mmd") -Encoding UTF8

foreach ($flow in $flows) {
  $flowDot = New-Object System.Collections.Generic.List[string]
  $flowMmd = New-Object System.Collections.Generic.List[string]
  $flowDot.Add("digraph mcp_flow {")
  $flowDot.Add("  rankdir=TB;")
  $flowDot.Add('  node [shape=box, fontname="Microsoft YaHei", fontsize=10];')
  $flowMmd.Add("flowchart TB")
  foreach ($state in $flow.states) {
    $safe = ($state -replace '[^A-Za-z0-9_]', '_')
    $flowDot.Add("  `"$(ConvertTo-DotString $state)`";")
    $flowMmd.Add("  $safe[`"$state`"]")
  }
  for ($i = 0; $i -lt ($flow.states.Count - 1); $i++) {
    $from = $flow.states[$i]
    $to = $flow.states[$i + 1]
    $flowDot.Add("  `"$(ConvertTo-DotString $from)`" -> `"$(ConvertTo-DotString $to)`";")
    $flowMmd.Add("  $($from -replace '[^A-Za-z0-9_]', '_') --> $($to -replace '[^A-Za-z0-9_]', '_')")
  }
  $flowDot.Add("}")
  $flowDot | Set-Content -LiteralPath (Join-Path $OutDir "$($flow.flow_id).dot") -Encoding UTF8
  $flowMmd | Set-Content -LiteralPath (Join-Path $OutDir "$($flow.flow_id).mmd") -Encoding UTF8
}

$incoming = @($edges | ForEach-Object { $_.to } | Select-Object -Unique)
$outgoing = @($edges | ForEach-Object { $_.from } | Select-Object -Unique)
$entryRules = @($rules | Where-Object { $incoming -notcontains $_.name })
$exitRules = @($rules | Where-Object { $outgoing -notcontains $_.name })

$report = New-Object System.Collections.Generic.List[string]
$report.Add("# CLIPS Rule Observability Report")
$report.Add("")
$report.Add("## Summary")
$report.Add("")
$report.Add("- Templates: $($templates.Count)")
$report.Add("- Rules: $($rules.Count)")
$report.Add("- Dependency edges: $($edges.Count)")
$factEdges = 0
foreach ($rule in $rules) { $factEdges += $rule.lhs_facts.Count + $rule.rhs_asserts.Count }
$report.Add("- Fact-rule graph edges: $factEdges")
$report.Add("- Flow scripts: $($flows.Count)")
$report.Add("- Entry rules: $($entryRules.Count)")
$report.Add("- Exit rules: $($exitRules.Count)")
$report.Add("- Same-salience conflict candidates: $($conflicts.Count)")
$report.Add("")
$report.Add("## Flow Scripts")
$report.Add("")
if ($flows.Count -gt 0) {
  $report.Add("| Flow | States | C++ Role |")
  $report.Add("|---|---:|---|")
  foreach ($flow in $flows) {
    $report.Add("| ``$($flow.flow_id)`` | $($flow.states.Count) | ``$($flow.cpp_role)`` |")
  }
} else {
  $report.Add("- None")
}
$report.Add("")
$report.Add("## Conflict Candidates")
$report.Add("")
if ($conflicts.Count -gt 0) {
  $report.Add("| Left | Right | Salience | Overlap |")
  $report.Add("|---|---|---:|---|")
  foreach ($conflict in ($conflicts | Select-Object -First 50)) {
    $report.Add("| ``$($conflict.left)`` | ``$($conflict.right)`` | $($conflict.salience) | ``$(($conflict.overlap_facts) -join ',')`` |")
  }
} else {
  $report.Add("- None")
}
$report.Add("")
$report.Add("## Key Continuation Rules")
$report.Add("")
foreach ($rule in $rules) {
  $reasons = ($rule.reason_codes -join ",")
  if ($reasons -match "incomplete|not_terminal") {
    $report.Add("- ``$($rule.name)``: facts ``$(($rule.lhs_facts) -join ',')``; reasons ``$reasons``")
  }
}
$report.Add("")
$report.Add("## Artifacts")
$report.Add("")
$report.Add("- ``rules_parse.json``")
$report.Add("- ``rules_graph.dot``")
$report.Add("- ``rules_graph.mmd``")
$report.Add("- ``rules_fact_graph.dot``")
$report.Add("- ``rules_fact_graph.mmd``")
$report.Add("- ``<flow_id>.dot``")
$report.Add("- ``<flow_id>.mmd``")
$report | Set-Content -LiteralPath (Join-Path $OutDir "rules_impact_report.md") -Encoding UTF8

[pscustomobject]@{
  status = "success"
  rule_root = $root
  out_dir = (Resolve-Path -LiteralPath $OutDir).Path
  template_count = $templates.Count
  rule_count = $rules.Count
  edge_count = $edges.Count
  flow_count = $flows.Count
  conflict_candidate_count = $conflicts.Count
} | ConvertTo-Json -Compress
