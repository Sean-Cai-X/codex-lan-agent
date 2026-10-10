$root = "D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent"
$sentinel = Join-Path $root "logs\network_rollback\audit_gateway_rollback.armed"
if (Test-Path $sentinel) {
  Remove-Item -Path $sentinel -Force
  Write-Host "CANCELED audit gateway rollback"
} else {
  Write-Host "No armed audit gateway rollback sentinel found"
}
