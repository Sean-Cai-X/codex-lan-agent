param(
    [string] $Endpoint = "http://127.0.0.1:18080/mcp",
    [string] $OutputRoot = "D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\temp"
)

$ErrorActionPreference = "Stop"

$stamp = Get-Date -Format "yyyyMMddHHmmss"
$goalId = "task-memory-migration-acceptance-$stamp"
$traceId = "$goalId-trace"
$outDir = Join-Path $OutputRoot "task_memory_migration_acceptance_$stamp"
$sample = Join-Path $outDir "acceptance_delete_comments.cpp"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Set-Content -LiteralPath $sample -Encoding UTF8 -Value @(
    "int keep0 = 0;"
    "// acceptance delete one"
    "int keep1 = 1;"
    "// acceptance delete two"
    "int keep2 = 2;"
)

function Invoke-McpTool {
    param(
        [string] $Name,
        [hashtable] $Arguments,
        [string] $OutName
    )
    $body = @{
        jsonrpc = "2.0"
        id = $OutName
        method = "tools/call"
        params = @{
            name = $Name
            arguments = $Arguments
        }
    } | ConvertTo-Json -Depth 70
    $response = Invoke-RestMethod -UseBasicParsing -Uri $Endpoint -Method Post -ContentType "application/json" -Body $body
    $json = $response | ConvertTo-Json -Depth 100
    Set-Content -LiteralPath (Join-Path $outDir "$OutName.json") -Value $json -Encoding UTF8
    return $json
}

function Assert-Match {
    param(
        [string] $Text,
        [string] $Pattern,
        [string] $Message
    )
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

$toolsBody = @{
    jsonrpc = "2.0"
    id = "tools-list"
    method = "tools/list"
    params = @{}
} | ConvertTo-Json -Depth 10
$tools = Invoke-RestMethod -UseBasicParsing -Uri $Endpoint -Method Post -ContentType "application/json" -Body $toolsBody
$toolsJson = $tools | ConvertTo-Json -Depth 100
Set-Content -LiteralPath (Join-Path $outDir "tools-list.json") -Value $toolsJson -Encoding UTF8
foreach ($toolName in @(
    "lan_agent_task_memory_freeze",
    "lan_agent_task_memory_append_step",
    "lan_agent_task_memory_resume_context",
    "lan_agent_task_memory_execute_continuation_budget",
    "lan_agent_task_memory_build_kv_snapshot",
    "lan_agent_task_memory_kv_lookup",
    "lan_agent_task_memory_rocksdb_mirror",
    "lan_agent_task_memory_rocksdb_lookup",
    "lan_agent_task_memory_rocksdb_parity_check",
    "lan_agent_task_memory_migration_assess",
    "lan_agent_task_memory_structure_manifest",
    "lan_agent_delete_next_text_range_atomic"
)) {
    Assert-Match $toolsJson $toolName "tools/list missing $toolName"
}

$nextCall = @{
    name = "lan_agent_delete_next_text_range_atomic"
    arguments = @{
        file_path = $sample
        scan_mode = "comments"
        primary_intent = "delete_comments"
        trace_id = $traceId
        probe_ref = $sample
        probe_ready = $true
    }
} | ConvertTo-Json -Depth 20 -Compress

Invoke-McpTool "lan_agent_task_memory_freeze" @{
    goal_id = $goalId
    trace_id = $traceId
    current_goal = "task memory migration acceptance"
    current_scope = "fresh model bootstrap + budget runner + KV/RocksDB mirror"
    current_file = $sample
    terminal_state = $false
    completion_claim_allowed = $false
    completed_step_count = 0
    current_tool = "lan_agent_delete_next_text_range_atomic"
    next_call_json = $nextCall
    compact_summary = "acceptance starts with two comment cleanup continuations"
    remaining_work = "execute budget runner, build KV snapshot, mirror to RocksDB, verify parity, materialize memory_structure"
    key_slices_jsonl = "{`"slice_id`":`"slice-task-memory-acceptance`",`"slice_type`":`"acceptance_chain`",`"summary`":`"task memory migration acceptance slice`",`"trace_id`":`"$traceId`"}"
} "01_freeze" | Out-Null

$resumeBefore = Invoke-McpTool "lan_agent_task_memory_resume_context" @{
    goal_id = $goalId
} "02_resume_before_budget"
Assert-Match $resumeBefore '"terminal_state".*false' "resume before budget did not preserve terminal_state=false"

$budgetPartial = Invoke-McpTool "lan_agent_task_memory_execute_continuation_budget" @{
    goal_id = $goalId
    trace_id = $traceId
    max_steps = 1
    dry_run = $false
    execute = $true
} "03_budget_partial"
Assert-Match $budgetPartial '"executed_step_count".*"1"' "partial budget did not execute one step"
Assert-Match $budgetPartial '"outcome".*"PARTIAL"' "partial budget did not report PARTIAL"
Assert-Match $budgetPartial '"terminal_state".*"false"' "partial budget should remain nonterminal"
Assert-Match $budgetPartial '"completion_claim_allowed".*"false"' "partial budget incorrectly allowed completion"
Assert-Match $budgetPartial '"final_answer_allowed".*"false"' "partial budget incorrectly allowed a final answer"

$budgetFinal = Invoke-McpTool "lan_agent_task_memory_execute_continuation_budget" @{
    goal_id = $goalId
    trace_id = $traceId
    max_steps = 8
    dry_run = $false
    execute = $true
} "04_budget_final"
Assert-Match $budgetFinal '"terminal_state".*"true"' "final budget did not reach terminal_state=true"
Assert-Match $budgetFinal '"completion_claim_allowed".*"true"' "final budget did not allow completion"

$sampleContent = Get-Content -LiteralPath $sample -Raw
if ($sampleContent -match "// acceptance delete") {
    throw "budget runner did not delete all sample comments"
}

$snapshot = Invoke-McpTool "lan_agent_task_memory_build_kv_snapshot" @{
    goal_id = $goalId
} "05_kv_snapshot"
Assert-Match $snapshot '"record_count"' "KV snapshot did not return record_count"

$lookupLatest = Invoke-McpTool "lan_agent_task_memory_kv_lookup" @{
    goal_id = $goalId
    kind = "latest"
    include_value = $true
} "06_kv_lookup_latest"
Assert-Match $lookupLatest '"matched_count".*"1"' "file KV latest lookup failed"

$mirror = Invoke-McpTool "lan_agent_task_memory_rocksdb_mirror" @{
    goal_id = $goalId
} "07_rocksdb_mirror"
Assert-Match $mirror '"rocksdb_status".*"enabled"' "RocksDB mirror did not enable"
Assert-Match $mirror '"mirror_complete".*"true"' "RocksDB mirror did not complete"
Assert-Match $mirror '"source_of_truth".*"file_object_store"' "mirror did not preserve file_object_store source of truth"
Assert-Match $mirror '"safe_to_replace_source_of_truth".*"false"' "mirror incorrectly allowed source replacement"

$rocksLookupLatest = Invoke-McpTool "lan_agent_task_memory_rocksdb_lookup" @{
    goal_id = $goalId
    kind = "latest"
    include_value = $true
} "08_rocksdb_lookup_latest"
Assert-Match $rocksLookupLatest '"kv_backend".*"rocksdb_native_mirror"' "RocksDB latest lookup did not use native mirror backend"
Assert-Match $rocksLookupLatest '"matched_count".*"1"' "RocksDB latest lookup failed"

$parityLatest = Invoke-McpTool "lan_agent_task_memory_rocksdb_parity_check" @{
    goal_id = $goalId
    kind = "latest"
    include_value = $false
} "09_rocksdb_parity_latest"
Assert-Match $parityLatest '"parity_ok".*"true"' "RocksDB latest parity failed"
Assert-Match $parityLatest '"safe_to_replace_source_of_truth".*"false"' "parity incorrectly allowed source replacement"

$assess = Invoke-McpTool "lan_agent_task_memory_migration_assess" @{
    goal_id = $goalId
} "10_migration_assess"
Assert-Match $assess 'ROCKSDB_NATIVE_MIRROR_READY' "migration assessment did not report native mirror ready"
Assert-Match $assess '"active_backend".*"rocksdb_native_mirror"' "migration assessment did not expose native mirror active backend"
Assert-Match $assess '"source_of_truth".*"file_object_store"' "migration assessment did not preserve file source of truth"
Assert-Match $assess '"safe_to_replace_source_of_truth".*"false"' "migration assessment incorrectly allowed source replacement"

$structure = Invoke-McpTool "lan_agent_task_memory_structure_manifest" @{
    goal_id = $goalId
} "11_memory_structure"
foreach ($pattern in @(
    '"structure_ready".*"true"',
    '"fresh_model_bootstrap_ready".*"true"',
    '"backend_policy_ready".*"true"',
    '"active_read_backend".*"rocksdb_native_mirror"',
    '"write_backend".*"file_object_store"',
    '"source_of_truth".*"file_object_store"',
    '"safe_to_replace_source_of_truth".*"false"',
    '"parity_required_for_native_reads".*"true"',
    '"required_model_read".*"latest_resume_context.json"'
)) {
    Assert-Match $structure $pattern "memory_structure output missing expected pattern: $pattern"
}

$structurePath = "D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\logs\task_memory\$goalId\memory_structure.json"
if (!(Test-Path -LiteralPath $structurePath)) {
    throw "memory_structure.json not found"
}
$structureText = Get-Content -LiteralPath $structurePath -Raw
foreach ($pattern in @(
    '"fresh_model_bootstrap"',
    '"first_read":"latest_resume_context.json"',
    '"second_read":"memory_structure.json"',
    '"query_read":"lan_agent_task_memory_rocksdb_lookup"',
    '"backend_policy"',
    '"query_contract"',
    '"native_lookup_tool":"lan_agent_task_memory_rocksdb_lookup"',
    '"native_parity_tool":"lan_agent_task_memory_rocksdb_parity_check"',
    '"full_history_read":"forbidden_by_default"'
)) {
    if ($structureText -notmatch [regex]::Escape($pattern)) {
        throw "memory_structure.json missing $pattern"
    }
}

$summaryPath = Join-Path $outDir "acceptance_summary.json"
$summary = @{
    status = "TASK_MEMORY_MIGRATION_ACCEPTANCE_PASS"
    goal_id = $goalId
    trace_id = $traceId
    output_dir = $outDir
    memory_structure_path = $structurePath
    source_of_truth = "file_object_store"
    active_read_backend = "rocksdb_native_mirror"
    write_backend = "file_object_store"
    safe_to_replace_source_of_truth = $false
    parity_required_for_native_reads = $true
} | ConvertTo-Json -Depth 20
Set-Content -LiteralPath $summaryPath -Value $summary -Encoding UTF8

Write-Output "TASK_MEMORY_MIGRATION_ACCEPTANCE_PASS"
Write-Output "goal_id=$goalId"
Write-Output "trace_id=$traceId"
Write-Output "out_dir=$outDir"
Write-Output "memory_structure_path=$structurePath"
Write-Output "summary_path=$summaryPath"
