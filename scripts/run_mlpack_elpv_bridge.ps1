param(
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"

$workspaceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bridgeScript = Join-Path $workspaceRoot "local_test\public_datasets\build_mlpack_elpv_feature_csv.ps1"
$handoffRoot = Join-Path $workspaceRoot "local_test\mlpack_baseline_thread\ELPV-Classification-Handoff"

if (!(Test-Path -LiteralPath $bridgeScript)) {
    throw "bridge script not found: $bridgeScript"
}

if (!(Test-Path -LiteralPath $handoffRoot)) {
    throw "handoff root not found: $handoffRoot"
}

$bridgeArgs = @(
    "-HandoffRoot"
    ('"{0}"' -f $handoffRoot)
)

if ($Rebuild) {
    $bridgeArgs += "-Rebuild"
}

$command = "& `"$bridgeScript`" $($bridgeArgs -join ' ')"
Invoke-Expression $command
