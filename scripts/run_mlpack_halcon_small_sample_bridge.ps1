$ErrorActionPreference = "Stop"

$workspaceRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$bridgeScript = Join-Path $workspaceRoot "local_test\public_datasets\build_mlpack_halcon_small_sample_feature_csv.ps1"

if (!(Test-Path -LiteralPath $bridgeScript)) {
    throw "bridge script not found: $bridgeScript"
}

& $bridgeScript -Rebuild
