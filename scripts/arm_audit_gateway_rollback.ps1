param(
  [int]$DelaySeconds = 300
)

$ErrorActionPreference = "Stop"
$root = "D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent"
$sentinel = Join-Path $root "logs\network_rollback\audit_gateway_rollback.armed"
$rollback = Join-Path $root "scripts\audit_gateway_rollback.ps1"
$logDir = Split-Path -Parent $sentinel
$vergeDir = "C:\Users\Ums_Ai_Coder\AppData\Roaming\io.github.clash-verge-rev.clash-verge-rev"
$backupDir = Join-Path $logDir "clash_verge_backup"
if (-not (Test-Path $logDir)) {
  New-Item -ItemType Directory -Force -Path $logDir | Out-Null
}
if (-not (Test-Path $backupDir)) {
  New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
}
foreach ($name in @("clash-verge.yaml", "clash-verge-check.yaml", "verge.yaml", "profiles.yaml")) {
  $src = Join-Path $vergeDir $name
  if (Test-Path $src) {
    Copy-Item -Path $src -Destination (Join-Path $backupDir $name) -Force
  }
}

Set-Content -Path $sentinel -Value ("armed_at=" + (Get-Date -Format "yyyy-MM-dd HH:mm:ss")) -Encoding UTF8
$args = @(
  "-NoProfile",
  "-ExecutionPolicy", "Bypass",
  "-File", $rollback,
  "-DelaySeconds", [string]$DelaySeconds,
  "-SentinelPath", $sentinel
)
$p = Start-Process -FilePath "powershell.exe" -ArgumentList $args -WindowStyle Hidden -PassThru

Write-Host ("ARMED rollback_pid={0} delay_seconds={1}" -f $p.Id, $DelaySeconds)
Write-Host ("CANCEL command: powershell -NoProfile -ExecutionPolicy Bypass -File `"{0}\scripts\cancel_audit_gateway_rollback.ps1`"" -f $root)
Write-Host ("LOG: {0}" -f (Join-Path $logDir "audit_gateway_rollback.log"))
