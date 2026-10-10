$ErrorActionPreference = "Stop"

$BaseUrl = if ($env:CODEX_LAN_AGENT_BASE_URL) { $env:CODEX_LAN_AGENT_BASE_URL } else { "http://127.0.0.1:18080" }
$ProjectDir = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

function Invoke-AgentTool {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable] $Arguments
    )

    $payload = @{
        name = "lan_agent_mcp_route"
        arguments = $Arguments
    } | ConvertTo-Json -Depth 12 -Compress

    $tmp = Join-Path $env:TEMP ("lan_agent_accept_fast_" + [guid]::NewGuid().ToString("N") + ".json")
    [System.IO.File]::WriteAllText($tmp, $payload, [System.Text.UTF8Encoding]::new($false))
    try {
        $raw = & curl.exe -s -X POST -H "Content-Type: application/json" --data-binary "@$tmp" "$BaseUrl/tools"
        return $raw | ConvertFrom-Json
    }
    finally {
        Remove-Item -LiteralPath $tmp -ErrorAction SilentlyContinue
    }
}

function Invoke-AcceptIntent {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable] $IntentArgs
    )

    return Invoke-AgentTool @{
        mode = "call"
        target_tool_name = "lan_agent_accept_intent"
        arguments = $IntentArgs
        model_profile = "large-llm"
    }
}

$allPassed = $true

Write-Host "=== Accept Fast Path A: direct health auto-executes ==="
$health = Invoke-AcceptIntent @{
    user_intent = "health status"
    auto_execute_direct = $true
    output_mode = "compact"
    max_stdout_chars = 1200
}
if ($health.executor_decision -eq "direct_mcp" -and
    $health.execution_performed -eq "true" -and
    $health.executed_tool_name -eq "lan_agent_health" -and
    $health.executed_ok -eq "true") {
    Write-Host "PASS A.health"
}
else {
    Write-Host "FAIL A.health"
    $allPassed = $false
}

Write-Host "=== Accept Fast Path B: read_file auto-executes with compact content ==="
$read = Invoke-AcceptIntent @{
    user_intent = "read file"
    file_path = (Join-Path $ProjectDir "AGENTS.md")
    auto_execute_direct = $true
    output_mode = "compact"
    max_stdout_chars = 800
    max_lines = 20
}
if ($read.executor_decision -eq "direct_mcp" -and
    $read.execution_performed -eq "true" -and
    $read.executed_tool_name -eq "lan_agent_read_text_file" -and
    [string]$read.content_text -ne "") {
    Write-Host "PASS B.read_file"
}
else {
    Write-Host "FAIL B.read_file"
    $allPassed = $false
}

Write-Host "=== Accept Fast Path C: search_text auto-executes ==="
$search = Invoke-AcceptIntent @{
    user_intent = "search text"
    directory_path = (Join-Path $ProjectDir "src")
    query_text = "BuildIntentAcceptanceResult"
    auto_execute_direct = $true
    output_mode = "compact"
    max_stdout_chars = 1200
}
if ($search.executor_decision -eq "direct_mcp" -and
    $search.execution_performed -eq "true" -and
    $search.executed_tool_name -eq "lan_agent_search_text" -and
    [string]$search.content_text -match "McpToolDispatch.h") {
    Write-Host "PASS C.search_text"
}
else {
    Write-Host "FAIL C.search_text"
    $allPassed = $false
}

Write-Host "=== Accept Fast Path D: complex goals do not auto-execute ==="
$complex = Invoke-AcceptIntent @{
    user_intent = "complex goal plan and execute refactor across multiple files"
    auto_execute_direct = $true
    output_mode = "compact"
}
if ($complex.executor_decision -eq "delegate_to_llm" -and
    $complex.execution_performed -eq "false" -and
    $complex.llm_decomposition_required -eq "true") {
    Write-Host "PASS D.complex_no_execute"
}
else {
    Write-Host "FAIL D.complex_no_execute"
    $allPassed = $false
}

Write-Host "=== Accept Fast Path E: MCP tools/list exposes guidance ==="
$toolsPayload = '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}'
$tmpTools = Join-Path $env:TEMP ("lan_agent_tools_list_" + [guid]::NewGuid().ToString("N") + ".json")
[System.IO.File]::WriteAllText($tmpTools, $toolsPayload, [System.Text.UTF8Encoding]::new($false))
try {
    $toolsRaw = & curl.exe -s -X POST -H "Content-Type: application/json" --data-binary "@$tmpTools" "$BaseUrl/mcp"
}
finally {
    Remove-Item -LiteralPath $tmpTools -ErrorAction SilentlyContinue
}
if ($toolsRaw -match "auto_execute_direct" -and $toolsRaw -match "executor_decision") {
    Write-Host "PASS E.schema_guidance"
}
else {
    Write-Host "FAIL E.schema_guidance"
    $allPassed = $false
}

if ($allPassed) {
    Write-Host "ACCEPT_FAST_PATH PASS"
}
else {
    Write-Host "ACCEPT_FAST_PATH FAIL"
    exit 1
}
