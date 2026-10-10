param(
    [string]$InputDir = "D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\semantic_runs\semantic_monitor",
    [string]$OutputDir = "D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\src\semantic_dashboard\out",
    [string]$SqliteExe = "",
    [switch]$VerboseProgress
)

$ErrorActionPreference = "Stop"

function Write-Phase {
    param([string]$Name)
    if ($VerboseProgress) { Write-Host "phase: $Name" }
}

function ConvertTo-SqlText {
    param([AllowNull()][object]$Value)
    if ($null -eq $Value) { return "NULL" }
    $text = [string]$Value
    return "'" + $text.Replace("'", "''") + "'"
}

function ConvertTo-CompactJson {
    param([AllowNull()][object]$Value)
    if ($null -eq $Value) { return "" }
    return ($Value | ConvertTo-Json -Depth 32 -Compress)
}

function ConvertTo-SafeName {
    param([string]$Value)
    $safe = $Value -replace '[^A-Za-z0-9_.-]+', '_'
    $safe = $safe.Trim('_')
    if ([string]::IsNullOrWhiteSpace($safe)) { return "object" }
    return $safe
}

function Get-FileSha256 {
    param([string]$Path)
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Get-RelativePath {
    param([string]$Base, [string]$Path)
    $baseFull = [System.IO.Path]::GetFullPath($Base).TrimEnd('\') + '\'
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    return $pathFull.Substring($baseFull.Length).Replace('\', '/')
}

function Get-MarkdownTitle {
    param([string]$Content, [string]$Fallback)
    foreach ($line in ($Content -split "`r?`n")) {
        $trimmed = $line.Trim()
        if ($trimmed.StartsWith("#")) {
            $title = $trimmed.TrimStart('#').Trim()
            if (-not [string]::IsNullOrWhiteSpace($title)) { return $title }
        }
    }
    return $Fallback
}

function Get-Judgment {
    param([string]$Content)
    $lower = $Content.ToLowerInvariant()
    if ($lower.Contains("partial") -or $Content.Contains("⚠")) { return "partial" }
    if ($lower.Contains("aligned") -or $Content.Contains("✅")) { return "aligned" }
    if ($lower.Contains("missing")) { return "missing" }
    if ($lower.Contains("suspicious")) { return "suspicious" }
    return "unknown"
}

function Get-RiskLevel {
    param([string]$Content)
    $lower = $Content.ToLowerInvariant()
    if ($lower.Contains("critical") -or $Content.Contains("严重")) { return "critical" }
    if ($lower.Contains("high") -or $Content.Contains("高")) { return "high" }
    if ($lower.Contains("medium") -or $lower.Contains("partial") -or $Content.Contains("中")) { return "medium" }
    if ($lower.Contains("low") -or $Content.Contains("低")) { return "low" }
    return "unknown"
}

function Get-DocumentKind {
    param([string]$RelPath)
    $p = $RelPath.ToLowerInvariant()
    if ($p.StartsWith("trace_cards/")) { return "trace_card" }
    if ($p.StartsWith("lenses/")) { return "lens" }
    if ($p.StartsWith("increment/")) { return "increment" }
    if ($p.Contains("risk_queue")) { return "risk_queue" }
    if ($p.Contains("gap_queue")) { return "gap_queue" }
    if ($p.Contains("issues")) { return "issues" }
    if ($p.EndsWith(".json")) { return "json" }
    if ($p.EndsWith(".dot")) { return "graph_dot" }
    if ($p.EndsWith(".md")) { return "markdown" }
    return "object"
}

function Add-Sql {
    param([System.Collections.Generic.List[string]]$Sql, [string]$Line)
    [void]$Sql.Add($Line)
}

if (-not (Test-Path -LiteralPath $InputDir)) {
    throw "InputDir does not exist: $InputDir"
}

if ([string]::IsNullOrWhiteSpace($SqliteExe)) {
    $cmd = Get-Command sqlite3 -ErrorAction SilentlyContinue
    if ($cmd) {
        $SqliteExe = $cmd.Source
    } else {
        $known = "D:\TraeAccounts\work\ModularData\ai-agent\vm\tools\app\sqlite\sqlite3.exe"
        if (Test-Path -LiteralPath $known) { $SqliteExe = $known }
    }
}

Write-Phase "prepare-output"
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$objectsDir = Join-Path $OutputDir "objects"
$dashboardDir = Join-Path $OutputDir "dashboard"
if (Test-Path -LiteralPath $objectsDir) { Remove-Item -LiteralPath $objectsDir -Recurse -Force }
if (Test-Path -LiteralPath $dashboardDir) { Remove-Item -LiteralPath $dashboardDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $objectsDir,$dashboardDir | Out-Null

$dbPath = Join-Path $OutputDir "semantic_monitor.db"
$sqlPath = Join-Path $OutputDir "semantic_monitor.sql"
$dataJsonPath = Join-Path $OutputDir "dashboard_data.json"
$dataJsPath = Join-Path $dashboardDir "dashboard_data.js"
$htmlPath = Join-Path $dashboardDir "index.html"
$manifestPath = Join-Path $OutputDir "object_manifest.json"
foreach ($path in @($dbPath, $sqlPath, $dataJsonPath, $dataJsPath, $htmlPath, $manifestPath)) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
}

Write-Phase "copy-objects"
$files = Get-ChildItem -LiteralPath $InputDir -Recurse -File | Sort-Object FullName
$documents = New-Object System.Collections.Generic.List[object]
$manifest = New-Object System.Collections.Generic.List[object]
foreach ($file in $files) {
    $rel = Get-RelativePath -Base $InputDir -Path $file.FullName
    $sha = Get-FileSha256 -Path $file.FullName
    $docId = $sha.Substring(0, 16)
    $objName = $docId + "_" + (ConvertTo-SafeName $rel)
    $objPath = Join-Path $objectsDir $objName
    Copy-Item -LiteralPath $file.FullName -Destination $objPath -Force
    $content = ""
    if ($file.Extension -in @(".md", ".json", ".dot", ".txt")) {
        $content = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
    }
    $title = if ($file.Extension -eq ".md") { Get-MarkdownTitle -Content $content -Fallback $file.BaseName } else { $file.BaseName }
    $kind = Get-DocumentKind -RelPath $rel
    $doc = [ordered]@{
        document_id = $docId
        rel_path = $rel
        object_path = ("objects/" + $objName)
        kind = $kind
        size_bytes = $file.Length
        sha256 = $sha
        title = $title
        content = $content
    }
    $documents.Add([pscustomobject]$doc)
    $manifest.Add([pscustomobject]@{
        document_id = $docId
        rel_path = $rel
        object_path = ("objects/" + $objName)
        kind = $kind
        size_bytes = $file.Length
        sha256 = $sha
        title = $title
    })
}

Write-Phase "load-json"
$semantic = Get-Content -LiteralPath (Join-Path $InputDir "semantic_network.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$navigation = Get-Content -LiteralPath (Join-Path $InputDir "navigation_tree.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$artifacts = Get-Content -LiteralPath (Join-Path $InputDir "artifacts_index.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$ledger = Get-Content -LiteralPath (Join-Path $InputDir "increment\increment_ledger.json") -Raw -Encoding UTF8 | ConvertFrom-Json

$nodes = New-Object System.Collections.Generic.List[object]
foreach ($node in @($semantic.key_nodes)) {
    if ($node.node_id) {
        $nodes.Add([pscustomobject]@{
            node_id = $node.node_id
            layer = $node.layer
            semantic_name = $node.semantic_name
            parent_id = $node.parent_id
            child_count = [int]($node.child_count + 0)
            evidence_type = $node.evidence_type
            risk_level = Get-RiskLevel -Content (ConvertTo-CompactJson $node)
            round_id = if ($node.node_id -match 'frag_(\d+)') { $n=[int]$Matches[1]; if($n -le 1021){"round_1"} elseif($n -le 1023){"round_2"} elseif($n -le 1025){"round_3"} else {"round_4"} } else { "" }
            raw_json = ConvertTo-CompactJson $node
        })
    }
}
foreach ($node in @($navigation.navigation_tree.high_value_nodes)) {
    if ($node.node_id -and -not ($nodes | Where-Object { $_.node_id -eq $node.node_id })) {
        $nodes.Add([pscustomobject]@{
            node_id = $node.node_id
            layer = if ($node.node_id -match '^(L\d_[A-Z]+)') { $Matches[1] } else { "L4_ATOM" }
            semantic_name = $node.summary
            parent_id = ""
            child_count = 0
            evidence_type = $node.evidence_strength
            risk_level = Get-RiskLevel -Content (ConvertTo-CompactJson $node)
            round_id = if ($node.node_id -match 'frag_(\d+)') { $n=[int]$Matches[1]; if($n -le 1021){"round_1"} elseif($n -le 1023){"round_2"} elseif($n -le 1025){"round_3"} else {"round_4"} } else { "" }
            raw_json = ConvertTo-CompactJson $node
        })
    }
}

$rounds = New-Object System.Collections.Generic.List[object]
foreach ($round in @($ledger.increment_ledger.rounds)) {
    $roundId = "round_" + $round.round_id
    $deltaNodeCount = 0
    if ($round.delta_nodes -and $round.delta_nodes.total_added) { $deltaNodeCount = [int]$round.delta_nodes.total_added }
    $rounds.Add([pscustomobject]@{
        round_id = $roundId
        round_index = [int]$round.round_id
        round_type = $round.round_type
        input_source = ConvertTo-CompactJson $round.input_source
        base_artifact = $round.base_artifact
        output_summary = $round.output_summary
        added_fragment_count = [int]($round.added_fragment_count + 0)
        skipped_duplicate_fragment_count = [int]($round.skipped_duplicate_fragment_count + 0)
        delta_fragment_count = [int](($round.delta_fragments, $round.added_fragment_count | Where-Object { $_ })[0] + 0)
        delta_node_count = $deltaNodeCount
        risk_count = [int]($round.risk_count + 0)
        human_review_status = "pending"
        raw_json = ConvertTo-CompactJson $round
    })
}

$artifactRows = New-Object System.Collections.Generic.List[object]
foreach ($prop in $artifacts.PSObject.Properties) {
    $a = $prop.Value
    $fragmentCount = 0
    if ($null -ne $a.fragments) {
        $fragmentCount = [int]($a.fragments + 0)
    } elseif ($null -ne $a.fragment_count) {
        $fragmentCount = [int]($a.fragment_count + 0)
    }
    $artifactRows.Add([pscustomobject]@{
        artifact_id = $prop.Name
        artifact_path = $a.path
        artifact_type = $a.type
        source_file = $a.source_file
        status = $a.status
        node_count = [int]($a.node_count + 0)
        edge_count = [int]($a.edge_count + 0)
        fragment_count = $fragmentCount
        description = $a.description
        raw_json = ConvertTo-CompactJson $a
    })
}

$traceCards = New-Object System.Collections.Generic.List[object]
$lenses = New-Object System.Collections.Generic.List[object]
$issues = New-Object System.Collections.Generic.List[object]
foreach ($doc in $documents) {
    if ($doc.kind -eq "trace_card") {
        $traceCards.Add([pscustomobject]@{
            card_id = [System.IO.Path]::GetFileNameWithoutExtension($doc.rel_path)
            document_id = $doc.document_id
            title = $doc.title
            judgment = Get-Judgment -Content $doc.content
            risk_level = Get-RiskLevel -Content $doc.content
            introduced_round = if ($doc.content -match 'round\s*(\d+)') { "round_" + $Matches[1] } else { "" }
            last_touched_round = if (($doc.content | Select-String -Pattern 'round\s*(\d+)' -AllMatches).Matches.Count -gt 0) { $m=($doc.content | Select-String -Pattern 'round\s*(\d+)' -AllMatches).Matches; "round_" + $m[$m.Count-1].Groups[1].Value } else { "" }
            human_review_status = "pending"
            content = $doc.content
        })
    } elseif ($doc.kind -eq "lens") {
        $lensId = [System.IO.Path]::GetFileNameWithoutExtension($doc.rel_path)
        $lenses.Add([pscustomobject]@{
            lens_id = $lensId
            document_id = $doc.document_id
            title = $doc.title
            lens_type = $lensId.Replace("_lens", "")
            content = $doc.content
        })
    } elseif ($doc.kind -in @("issues", "risk_queue", "gap_queue")) {
        $issues.Add([pscustomobject]@{
            issue_id = [System.IO.Path]::GetFileNameWithoutExtension($doc.rel_path)
            document_id = $doc.document_id
            title = $doc.title
            severity = Get-RiskLevel -Content $doc.content
            status = "open"
            content = $doc.content
        })
    }
}

Write-Phase "build-sql"
$sql = New-Object System.Collections.Generic.List[string]
Add-Sql $sql "PRAGMA foreign_keys=OFF;"
Add-Sql $sql "BEGIN TRANSACTION;"
Add-Sql $sql @"
CREATE TABLE documents(document_id TEXT PRIMARY KEY, rel_path TEXT, object_path TEXT, kind TEXT, size_bytes INTEGER, sha256 TEXT, title TEXT, content TEXT);
CREATE TABLE nodes(node_id TEXT PRIMARY KEY, layer TEXT, semantic_name TEXT, parent_id TEXT, child_count INTEGER, evidence_type TEXT, risk_level TEXT, round_id TEXT, raw_json TEXT);
CREATE TABLE artifacts(artifact_id TEXT PRIMARY KEY, artifact_path TEXT, artifact_type TEXT, source_file TEXT, status TEXT, node_count INTEGER, edge_count INTEGER, fragment_count INTEGER, description TEXT, raw_json TEXT);
CREATE TABLE rounds(round_id TEXT PRIMARY KEY, round_index INTEGER, round_type TEXT, input_source TEXT, base_artifact TEXT, output_summary TEXT, added_fragment_count INTEGER, skipped_duplicate_fragment_count INTEGER, delta_fragment_count INTEGER, delta_node_count INTEGER, risk_count INTEGER, human_review_status TEXT, raw_json TEXT);
CREATE TABLE trace_cards(card_id TEXT PRIMARY KEY, document_id TEXT, title TEXT, judgment TEXT, risk_level TEXT, introduced_round TEXT, last_touched_round TEXT, human_review_status TEXT, content TEXT);
CREATE TABLE issues(issue_id TEXT PRIMARY KEY, document_id TEXT, title TEXT, severity TEXT, status TEXT, content TEXT);
CREATE TABLE lenses(lens_id TEXT PRIMARY KEY, document_id TEXT, title TEXT, lens_type TEXT, content TEXT);
CREATE TABLE review_decisions(review_id TEXT PRIMARY KEY, target_type TEXT, target_id TEXT, decision TEXT, notes TEXT, reviewer TEXT, updated_at TEXT);
"@
foreach ($d in $documents) {
    Add-Sql $sql ("INSERT INTO documents VALUES({0},{1},{2},{3},{4},{5},{6},{7});" -f (ConvertTo-SqlText $d.document_id),(ConvertTo-SqlText $d.rel_path),(ConvertTo-SqlText $d.object_path),(ConvertTo-SqlText $d.kind),$d.size_bytes,(ConvertTo-SqlText $d.sha256),(ConvertTo-SqlText $d.title),(ConvertTo-SqlText $d.content))
}
foreach ($n in $nodes) {
    Add-Sql $sql ("INSERT OR REPLACE INTO nodes VALUES({0},{1},{2},{3},{4},{5},{6},{7},{8});" -f (ConvertTo-SqlText $n.node_id),(ConvertTo-SqlText $n.layer),(ConvertTo-SqlText $n.semantic_name),(ConvertTo-SqlText $n.parent_id),$n.child_count,(ConvertTo-SqlText $n.evidence_type),(ConvertTo-SqlText $n.risk_level),(ConvertTo-SqlText $n.round_id),(ConvertTo-SqlText $n.raw_json))
}
foreach ($a in $artifactRows) {
    Add-Sql $sql ("INSERT OR REPLACE INTO artifacts VALUES({0},{1},{2},{3},{4},{5},{6},{7},{8},{9});" -f (ConvertTo-SqlText $a.artifact_id),(ConvertTo-SqlText $a.artifact_path),(ConvertTo-SqlText $a.artifact_type),(ConvertTo-SqlText $a.source_file),(ConvertTo-SqlText $a.status),$a.node_count,$a.edge_count,$a.fragment_count,(ConvertTo-SqlText $a.description),(ConvertTo-SqlText $a.raw_json))
}
foreach ($r in $rounds) {
    Add-Sql $sql ("INSERT OR REPLACE INTO rounds VALUES({0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12});" -f (ConvertTo-SqlText $r.round_id),$r.round_index,(ConvertTo-SqlText $r.round_type),(ConvertTo-SqlText $r.input_source),(ConvertTo-SqlText $r.base_artifact),(ConvertTo-SqlText $r.output_summary),$r.added_fragment_count,$r.skipped_duplicate_fragment_count,$r.delta_fragment_count,$r.delta_node_count,$r.risk_count,(ConvertTo-SqlText $r.human_review_status),(ConvertTo-SqlText $r.raw_json))
}
foreach ($c in $traceCards) {
    Add-Sql $sql ("INSERT OR REPLACE INTO trace_cards VALUES({0},{1},{2},{3},{4},{5},{6},{7},{8});" -f (ConvertTo-SqlText $c.card_id),(ConvertTo-SqlText $c.document_id),(ConvertTo-SqlText $c.title),(ConvertTo-SqlText $c.judgment),(ConvertTo-SqlText $c.risk_level),(ConvertTo-SqlText $c.introduced_round),(ConvertTo-SqlText $c.last_touched_round),(ConvertTo-SqlText $c.human_review_status),(ConvertTo-SqlText $c.content))
}
foreach ($i in $issues) {
    Add-Sql $sql ("INSERT OR REPLACE INTO issues VALUES({0},{1},{2},{3},{4},{5});" -f (ConvertTo-SqlText $i.issue_id),(ConvertTo-SqlText $i.document_id),(ConvertTo-SqlText $i.title),(ConvertTo-SqlText $i.severity),(ConvertTo-SqlText $i.status),(ConvertTo-SqlText $i.content))
}
foreach ($l in $lenses) {
    Add-Sql $sql ("INSERT OR REPLACE INTO lenses VALUES({0},{1},{2},{3},{4});" -f (ConvertTo-SqlText $l.lens_id),(ConvertTo-SqlText $l.document_id),(ConvertTo-SqlText $l.title),(ConvertTo-SqlText $l.lens_type),(ConvertTo-SqlText $l.content))
}
Add-Sql $sql "COMMIT;"
Write-Phase "write-sql"
Set-Content -LiteralPath $sqlPath -Value $sql -Encoding UTF8

if (-not [string]::IsNullOrWhiteSpace($SqliteExe) -and (Test-Path -LiteralPath $SqliteExe)) {
    Write-Phase "sqlite-import"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $SqliteExe
    $psi.Arguments = '"' + $dbPath + '"'
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()
    $proc.StandardInput.Write((Get-Content -LiteralPath $sqlPath -Raw -Encoding UTF8))
    $proc.StandardInput.Close()
    if (-not $proc.WaitForExit(30000)) {
        $proc.Kill()
        throw "sqlite import timed out"
    }
    if ($proc.ExitCode -ne 0) {
        $stderr = $proc.StandardError.ReadToEnd()
        throw "sqlite import failed with exit code $($proc.ExitCode): $stderr"
    }
}

Write-Phase "build-dashboard-data"
$layerMap = [ordered]@{}
foreach ($prop in $semantic.layers.PSObject.Properties) {
    $layerMap[$prop.Name] = $prop.Value
}
$dashboardData = [ordered]@{
    generated_at = (Get-Date).ToString("s")
    sqlite_db = $dbPath
    summary = [ordered]@{
        total_nodes = [int]$semantic.nodes_summary.total
        total_edges = [int]$semantic.edges_summary.total
        layers = $layerMap
        latest_artifact = $ledger.increment_ledger.latest_artifact
        document_count = $documents.Count
        object_count = $manifest.Count
    }
    nodes = $nodes
    rounds = $rounds
    artifacts = $artifactRows
    trace_cards = @($traceCards | ForEach-Object {
        [pscustomobject]@{
            card_id = $_.card_id
            document_id = $_.document_id
            title = $_.title
            judgment = $_.judgment
            risk_level = $_.risk_level
            introduced_round = $_.introduced_round
            last_touched_round = $_.last_touched_round
            human_review_status = $_.human_review_status
            content = if ($_.content.Length -gt 2500) { $_.content.Substring(0, 2500) + "`n...[truncated, see object store or SQLite]" } else { $_.content }
        }
    })
    issues = @($issues | ForEach-Object {
        [pscustomobject]@{
            issue_id = $_.issue_id
            document_id = $_.document_id
            title = $_.title
            severity = $_.severity
            status = $_.status
            content = if ($_.content.Length -gt 2500) { $_.content.Substring(0, 2500) + "`n...[truncated, see object store or SQLite]" } else { $_.content }
        }
    })
    lenses = @($lenses | ForEach-Object {
        [pscustomobject]@{
            lens_id = $_.lens_id
            document_id = $_.document_id
            title = $_.title
            lens_type = $_.lens_type
            content = if ($_.content.Length -gt 2500) { $_.content.Substring(0, 2500) + "`n...[truncated, see object store or SQLite]" } else { $_.content }
        }
    })
    documents = $manifest
}

Write-Phase "write-dashboard-data"
$json = $dashboardData | ConvertTo-Json -Depth 12
Set-Content -LiteralPath $dataJsonPath -Value $json -Encoding UTF8
Set-Content -LiteralPath $dataJsPath -Value ("window.SEMANTIC_DASHBOARD_DATA = " + $json + ";") -Encoding UTF8
Set-Content -LiteralPath $manifestPath -Value (@{ objects = $manifest } | ConvertTo-Json -Depth 8) -Encoding UTF8

Write-Phase "write-html"
$html = @'
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Semantic Monitor Dashboard</title>
<style>
:root{--bg:#f6f7f9;--panel:#fff;--ink:#18202a;--muted:#657386;--line:#dce1e8;--accent:#1f6feb;--risk:#b42318;--ok:#1a7f37;--warn:#9a6700}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 "Segoe UI",Arial,sans-serif}
header{height:64px;display:flex;align-items:center;gap:18px;padding:0 22px;border-bottom:1px solid var(--line);background:#fff;position:sticky;top:0;z-index:2}h1{font-size:18px;margin:0}
main{display:grid;grid-template-columns:260px minmax(420px,1fr) 360px;min-height:calc(100vh - 64px)}nav,aside{background:#fff;padding:16px;overflow:auto}nav{border-right:1px solid var(--line)}aside{border-left:1px solid var(--line)}section{padding:18px;overflow:auto}
button{width:100%;text-align:left;border:1px solid var(--line);background:#fff;border-radius:8px;padding:9px 10px;margin:4px 0;color:var(--ink);cursor:pointer}button.active,button:hover{border-color:var(--accent);background:#eef5ff}
input,select,textarea{width:100%;border:1px solid var(--line);border-radius:8px;padding:9px 10px;font:inherit;background:#fff}textarea{min-height:90px;resize:vertical}
.pill,.tag{display:inline-block;border:1px solid var(--line);border-radius:999px;padding:3px 8px;margin:2px;background:#f8fafc;color:var(--muted);font-size:12px}.tag{border-radius:6px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}.card{border:1px solid var(--line);background:var(--panel);border-radius:10px;padding:14px;margin-bottom:12px}.card h3{margin:0 0 8px;font-size:15px}
.muted{color:var(--muted)}.mono{font-family:Consolas,monospace;font-size:12px;word-break:break-all}.list{display:grid;gap:8px}.row{border:1px solid var(--line);border-radius:8px;background:#fff;padding:10px;cursor:pointer}.row:hover{border-color:var(--accent)}
.partial,.medium{color:var(--warn);font-weight:600}.aligned,.ok{color:var(--ok);font-weight:600}.high,.critical{color:var(--risk);font-weight:600}pre{white-space:pre-wrap;word-break:break-word;background:#f8fafc;border:1px solid var(--line);border-radius:8px;padding:12px;max-height:460px;overflow:auto}
@media(max-width:1100px){main{grid-template-columns:1fr}nav,aside{border:0;border-bottom:1px solid var(--line)}}
</style>
</head>
<body>
<header><h1>Semantic Monitor Dashboard</h1><div id="headerSummary"></div></header>
<main>
<nav><input id="search" placeholder="搜索 node / round / artifact / card"><div style="height:10px"></div>
<button data-view="overview" class="active">Overview</button><button data-view="navigation">Layer Navigation</button><button data-view="rounds">Increment Ledger</button><button data-view="risks">Risks & Gaps</button><button data-view="cards">Trace Cards</button><button data-view="artifacts">Artifacts</button><button data-view="lenses">Lenses</button><button data-view="objects">Object Store</button></nav>
<section id="content"></section>
<aside><div class="card"><h3>Review Decision</h3><div id="selectedTarget" class="muted">选择一个节点、卡片或 artifact</div><div style="height:10px"></div><select id="decision"><option>pending</option><option>accepted</option><option>rejected</option><option>needs_more_evidence</option></select><div style="height:8px"></div><textarea id="notes" placeholder="人工研判备注"></textarea><div style="height:8px"></div><button id="saveDecision">保存到本机浏览器</button></div><div class="card"><h3>Current Detail</h3><pre id="detail">No selection</pre></div></aside>
</main>
<script src="dashboard_data.js"></script>
<script>
const data=window.SEMANTIC_DASHBOARD_DATA;let view="overview",selected=null;const $=id=>document.getElementById(id);const esc=v=>String(v??"").replace(/[&<>]/g,ch=>({"&":"&amp;","<":"&lt;",">":"&gt;"}[ch]));
function setDetail(type,id,payload){selected={type,id,payload};$("selectedTarget").textContent=`${type}: ${id}`;$("detail").textContent=JSON.stringify(payload,null,2);const saved=JSON.parse(localStorage.getItem("semanticReview:"+type+":"+id)||"{}");$("decision").value=saved.decision||"pending";$("notes").value=saved.notes||""}
function filter(items,fields){const q=$("search").value.trim().toLowerCase();if(!q)return items;return items.filter(i=>fields.some(f=>String(i[f]??"").toLowerCase().includes(q))||JSON.stringify(i).toLowerCase().includes(q))}
function row(label,sub,type,id,extra=""){return `<div class="row" data-type="${esc(type)}" data-id="${esc(id)}"><b>${esc(label)}</b> ${extra}<div class="muted mono">${esc(sub)}</div></div>`}
function bind(map){document.querySelectorAll(".row[data-id]").forEach(el=>el.onclick=()=>setDetail(el.dataset.type,el.dataset.id,map[el.dataset.type+":"+el.dataset.id]||{}))}
function overview(){const s=data.summary,l=s.layers||{};$("content").innerHTML=`<div class="grid"><div class="card"><h3>Total Nodes</h3><div style="font-size:28px">${esc(s.total_nodes)}</div></div><div class="card"><h3>Total Edges</h3><div style="font-size:28px">${esc(s.total_edges)}</div></div><div class="card"><h3>Latest Artifact</h3><div class="mono">${esc(s.latest_artifact)}</div></div><div class="card"><h3>Objects</h3><div style="font-size:28px">${esc(s.object_count)}</div></div></div><div class="card"><h3>Layer Distribution</h3>${Object.entries(l).map(([k,v])=>`<span class="tag">${esc(k)}: ${esc(v)}</span>`).join("")}</div><div class="card"><h3>Next Round Entry</h3><pre>${esc(s.latest_artifact)} / summary.json</pre></div>`}
function listView(title,items,fields,type,idField,labelField,subFn,extraFn=()=>"",limit=1000){const arr=filter(items,fields).slice(0,limit),map={};$("content").innerHTML=`<div class="card"><h3>${title}</h3><div class="list">${arr.map(i=>{const id=i[idField];map[type+":"+id]=i;return row(i[labelField]||id,subFn(i),type,id,extraFn(i))}).join("")}</div></div>`;bind(map)}
function render(){document.querySelectorAll("nav button[data-view]").forEach(b=>b.classList.toggle("active",b.dataset.view===view));if(view==="overview")overview();else if(view==="navigation")listView("Layer Navigation",data.nodes,["node_id","layer","semantic_name","round_id"],"node","node_id","node_id",n=>`${n.layer||""} · ${n.semantic_name||""} · ${n.round_id||""}`,n=>`<span class="tag">${esc(n.child_count)} children</span>`);else if(view==="rounds")listView("Increment Ledger",data.rounds,["round_id","round_type","input_source","output_summary"],"round","round_id","round_id",r=>`${r.round_type||""} · ${r.output_summary||""}`,r=>`<span class="tag">+${esc(r.added_fragment_count)} fragments</span><span class="tag">${esc(r.delta_node_count)} nodes</span>`);else if(view==="risks")listView("Risks & Gaps",data.issues,["issue_id","title","severity","content"],"issue","issue_id","title",i=>(i.content||"").slice(0,180),i=>`<span class="tag ${esc(i.severity)}">${esc(i.severity)}</span>`);else if(view==="cards")listView("Trace Cards",data.trace_cards,["card_id","title","judgment","content"],"card","card_id","title",c=>`${c.judgment||""} · ${c.introduced_round||""} -> ${c.last_touched_round||""}`,c=>`<span class="tag ${esc(c.judgment)}">${esc(c.judgment)}</span>`);else if(view==="artifacts")listView("Artifacts",data.artifacts,["artifact_id","artifact_path","artifact_type","source_file","description"],"artifact","artifact_id","artifact_id",a=>a.artifact_path||"",a=>`<span class="tag">${esc(a.artifact_type||"semantic")}</span><span class="tag">${esc(a.status||"")}</span>`);else if(view==="lenses")listView("Lenses",data.lenses,["lens_id","title","lens_type","content"],"lens","lens_id","title",l=>(l.content||"").slice(0,180),l=>`<span class="tag">${esc(l.lens_type)}</span>`);else listView("Object Store",data.documents,["rel_path","kind","title"],"document","document_id","rel_path",d=>`${d.kind} · ${d.size_bytes} bytes · ${d.object_path}`,d=>`<span class="tag">${esc(d.kind)}</span>`)}
$("headerSummary").innerHTML=`<span class="pill">${esc(data.summary.total_nodes)} nodes</span><span class="pill">${esc(data.summary.total_edges)} edges</span><span class="pill">latest: ${esc(data.summary.latest_artifact)}</span>`;document.querySelectorAll("nav button[data-view]").forEach(b=>b.onclick=()=>{view=b.dataset.view;render()});$("search").oninput=render;$("saveDecision").onclick=()=>{if(!selected)return;localStorage.setItem("semanticReview:"+selected.type+":"+selected.id,JSON.stringify({decision:$("decision").value,notes:$("notes").value,updated_at:new Date().toISOString()}));alert("已保存本机审查状态")};render();
</script>
</body>
</html>
'@
Set-Content -LiteralPath $htmlPath -Value $html -Encoding UTF8

Write-Phase "done"
[pscustomobject]@{
    output_dir = $OutputDir
    sqlite_db = $dbPath
    sqlite_created = (Test-Path -LiteralPath $dbPath)
    sql_file = $sqlPath
    dashboard = $htmlPath
    objects = $manifest.Count
    documents = $documents.Count
    nodes = $nodes.Count
    artifacts = $artifactRows.Count
    rounds = $rounds.Count
    trace_cards = $traceCards.Count
    lenses = $lenses.Count
    issues = $issues.Count
} | ConvertTo-Json -Depth 4
