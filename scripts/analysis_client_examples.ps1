param(
    [string]$AgentBaseUrl = "http://127.0.0.1:18080",
    [string]$SourceFile = "D:\Codex-WorkDir\Sean_WorkDir\cxvisionai\cxvision_repo\cximage\FastMatch.cpp",
    [string]$ProjectRoot = "D:\Codex-WorkDir\Sean_WorkDir\cxvisionai",
    [string]$FocusSymbol = "center_x",
    [string]$OutputDir = "",
    [int]$TimeoutSec = 180
)

$ErrorActionPreference = "Stop"

function Invoke-LanMcpTool {
    param(
        [string]$Name,
        [hashtable]$Arguments,
        [int]$CallTimeoutSec = 120
    )

    $body = @{
        jsonrpc = "2.0"
        id = $Name
        method = "tools/call"
        params = @{
            name = $Name
            arguments = $Arguments
        }
    } | ConvertTo-Json -Depth 18 -Compress

    Invoke-RestMethod `
        -Uri "$AgentBaseUrl/mcp" `
        -Method Post `
        -ContentType "application/json; charset=utf-8" `
        -Body $body `
        -TimeoutSec $CallTimeoutSec
}

function Invoke-LanMcpToolsList {
    $body = '{"jsonrpc":"2.0","id":"tools-list","method":"tools/list","params":{}}'
    Invoke-RestMethod `
        -Uri "$AgentBaseUrl/mcp" `
        -Method Post `
        -ContentType "application/json; charset=utf-8" `
        -Body $body `
        -TimeoutSec 45
}

function Assert-Ok {
    param(
        [object]$Response,
        [string]$Label
    )
    if ($null -eq $Response.result -or $null -eq $Response.result.structuredContent) {
        throw "$Label did not return structuredContent"
    }
    $content = $Response.result.structuredContent
    if ($content.status -ne "success") {
        $reason = [string]$content.preflight_reason_code
        $errorText = [string]$content.error
        throw "$Label failed: status=$($content.status), reason=$reason, error=$errorText"
    }
    return $content
}

if (-not (Test-Path -LiteralPath $SourceFile)) {
    throw "SourceFile does not exist: $SourceFile"
}
if (-not [string]::IsNullOrWhiteSpace($ProjectRoot) -and -not (Test-Path -LiteralPath $ProjectRoot)) {
    throw "ProjectRoot does not exist: $ProjectRoot"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $env:TEMP "lan_agent_analysis_client_bundle"
}
if (Test-Path -LiteralPath $OutputDir) {
    $outputLeaf = Split-Path -Leaf $OutputDir
    if ($outputLeaf -notlike "lan_agent*") {
        throw "Refusing to delete non-example OutputDir: $OutputDir"
    }
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}

Write-Host "=== tools/list ==="
$tools = Invoke-LanMcpToolsList
$toolNames = $tools.result.tools.name
foreach ($name in @(
    "lan_agent_run_clang_ast_parser",
    "lan_agent_build_dfg",
    "lan_agent_query_dfg_artifact",
    "lan_agent_build_program_slice"
)) {
    if ($toolNames -notcontains $name) {
        throw "Missing MCP tool: $name"
    }
}
Write-Host "tools ok"

$common = @{
    source_file = $SourceFile.Replace("\", "/")
    project_root = $ProjectRoot.Replace("\", "/")
}

Write-Host "`n=== AST parse ==="
$ast = Assert-Ok (Invoke-LanMcpTool "lan_agent_run_clang_ast_parser" $common $TimeoutSec) "AST parse"
Write-Host $ast.summary
Write-Host "compile_db_mode=$($ast.compile_db_mode)"
Write-Host "resolved_compile_db_dir=$($ast.resolved_compile_db_dir)"

Write-Host "`n=== DFG build ==="
$dfgArgs = $common.Clone()
$dfgArgs.include_dot = $false
$dfgArgs.focus_symbol = $FocusSymbol
$dfgArgs.neighborhood_depth = 2
$dfgArgs.neighborhood_direction = "both"
$dfgArgs.max_nodes = 80
$dfgArgs.offset_edges = 0
$dfgArgs.max_edges = 120
$dfgArgs.max_interprocedural_bindings = 256
$dfgArgs.output_dir = $OutputDir
$dfg = Assert-Ok (Invoke-LanMcpTool "lan_agent_build_dfg" $dfgArgs $TimeoutSec) "DFG build"
Write-Host $dfg.summary
Write-Host "analysis_level=$($dfg.analysis_level)"
Write-Host "dfg_precision=$($dfg.dfg_precision)"
Write-Host "ast_readwrite_ref_count=$($dfg.ast_readwrite_ref_count)"
Write-Host "interprocedural_binding_count=$($dfg.interprocedural_binding_count)"
Write-Host "interprocedural_bindings_truncated=$($dfg.interprocedural_bindings_truncated)"

Write-Host "`n=== DFG artifact query ==="
$summaryPath = Join-Path $OutputDir "summary.json"
if (-not (Test-Path -LiteralPath $summaryPath)) {
    throw "DFG summary artifact was not written: $summaryPath"
}
$query = Assert-Ok (Invoke-LanMcpTool "lan_agent_query_dfg_artifact" @{
    artifact_summary_path = $summaryPath
    include_dot = $false
    focus_symbol = $FocusSymbol
    neighborhood_depth = 2
    neighborhood_direction = "both"
    max_nodes = 60
    max_edges = 80
} 60) "DFG artifact query"
Write-Host $query.summary
Write-Host "artifact_parser=$($query.artifact_parser)"
Write-Host "artifact_json_path_resolved_from=$($query.artifact_json_path_resolved_from)"

Write-Host "`n=== Program Slice ==="
$sliceArgs = $common.Clone()
$sliceArgs.symbol = $FocusSymbol
$sliceArgs.direction = "backward"
$sliceArgs.max_depth = 8
$sliceArgs.include_dot = $false
$sliceArgs.max_nodes = 40
$sliceArgs.max_edges = 80
$sliceArgs.max_interprocedural_bindings = 256
$slice = Assert-Ok (Invoke-LanMcpTool "lan_agent_build_program_slice" $sliceArgs $TimeoutSec) "Program Slice"
Write-Host $slice.summary
Write-Host "slice_precision=$($slice.slice_precision)"
Write-Host "source_line_count=$($slice.slice_line_count)"

Write-Host "`n=== Path metadata smoke ==="
$pathArgs = $sliceArgs.Clone()
$pathArgs.include_path_metadata = $true
$pathArgs.max_nodes = 40
$pathArgs.max_edges = 80
$path = Assert-Ok (Invoke-LanMcpTool "lan_agent_build_program_slice" $pathArgs $TimeoutSec) "Program Slice path metadata"
Write-Host "path_sensitive_status=$($path.path_sensitive_status)"
Write-Host "path_sensitive_precision=$($path.path_sensitive_precision)"
Write-Host "path_condition_candidate_count=$($path.path_condition_candidate_count)"

Write-Host "`nanalysis client example passed"
