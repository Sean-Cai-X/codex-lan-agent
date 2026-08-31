param(
    [string]$BaseUrl = "http://127.0.0.1:18080"
)

$ErrorActionPreference = "Continue"
$allPassed = $true

function Invoke-AgentTool {
    param([hashtable]$Payload)
    $json = $Payload | ConvertTo-Json -Depth 20 -Compress
    $tmp = Join-Path $env:TEMP "codex_lan_agent_semantic_gate_request.json"
    try {
        [System.IO.File]::WriteAllText($tmp, $json, [System.Text.Encoding]::UTF8)
        $raw = curl.exe -s -X POST -H "Content-Type: application/json; charset=utf-8" --data-binary "@$tmp" "$BaseUrl/tools"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "curl.exe failed: exit=$LASTEXITCODE"
            return $null
        }
        return $raw | ConvertFrom-Json
    } catch {
        Write-Host "HTTP request failed: $($_.Exception.Message)"
        return $null
    } finally {
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
    }
}

function Assert-Text {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Needle
    )
    if ($Text -like "*$Needle*") {
        Write-Host "PASS $Name"
    } else {
        Write-Host "FAIL $Name missing: $Needle"
        $script:allPassed = $false
    }
}

Write-Host "=== Semantic Gate A: complex goal must return to LLM decomposition ==="
$a = Invoke-AgentTool @{
    name = "lan_agent_mcp_route"
    arguments = @{
        mode = "call"
        target_tool_name = "lan_agent_accept_intent"
        arguments = @{
            user_intent = "推进解决optfile问题，代码在D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\optfile，编译后替换optfile.exe"
            prefer_local_model = $true
        }
    }
}
$aText = $a | ConvertTo-Json -Depth 20 -Compress
Assert-Text "A.acceptance" $aText "delegate_to_llm"
Assert-Text "A.reason" $aText "complex_goal_requires_llm_decomposition"
Assert-Text "A.gate" $aText "complex_goal_delegate_to_llm"

Write-Host "=== Semantic Gate B: atomic semantic task may go to local model when available ==="
$b = Invoke-AgentTool @{
    name = "lan_agent_mcp_route"
    arguments = @{
        mode = "call"
        target_tool_name = "lan_agent_accept_intent"
        arguments = @{
            user_intent = "将这段错误日志归纳根因：main.cpp:10: error: missing semicolon"
            semantic_mode = "error_diagnose"
            semantic_atomic = $true
            prefer_local_model = $true
            timeout_ms = 12000
        }
    }
}
$bText = $b | ConvertTo-Json -Depth 20 -Compress
if ($bText -like "*local_model_available*true*") {
    Assert-Text "B.semantic" $bText "local_semantic_atomic"
    Assert-Text "B.next_tool" $bText "lan_agent_semantic_reduce"
} else {
    Assert-Text "B.fallback" $bText "original_mcp_route_unchanged"
}

Write-Host "=== Semantic Gate C: direct semantic_reduce rejects complex goal ==="
$c = Invoke-AgentTool @{
    name = "lan_agent_mcp_route"
    arguments = @{
        mode = "call"
        target_tool_name = "lan_agent_semantic_reduce"
        arguments = @{
            task_complexity = "complex"
            question = "推进解决optfile问题，先分析并落地修复，生成后替换exe"
            timeout_ms = 12000
        }
    }
}
$cText = $c | ConvertTo-Json -Depth 20 -Compress
Assert-Text "C.reject" $cText "semantic_reduce_rejected_complex_goal"
Assert-Text "C.no_local_call" $cText "local_model_called"
Assert-Text "C.boundary" $cText "not a complex-goal decomposer"

if ($allPassed) {
    Write-Host "SEMANTIC_GATE PASS"
} else {
    Write-Host "SEMANTIC_GATE FAIL"
}