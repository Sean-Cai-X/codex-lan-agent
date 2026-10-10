param(
  [int]$DelaySeconds = 300,
  [string]$SentinelPath = "D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\logs\network_rollback\audit_gateway_rollback.armed",
  [string]$ProxyServer = "127.0.0.1:7897",
  [string]$ClashVergePath = "C:\Program Files\Clash Verge\clash-verge.exe",
  [string]$ClashVergeDir = "C:\Users\Ums_Ai_Coder\AppData\Roaming\io.github.clash-verge-rev.clash-verge-rev",
  [string]$BackupDir = "D:\Codex-WorkDir\Sean_WorkDir\codex-lan-agent\logs\network_rollback\clash_verge_backup"
)

$ErrorActionPreference = "Continue"
$logDir = Split-Path -Parent $SentinelPath
if (-not (Test-Path $logDir)) {
  New-Item -ItemType Directory -Force -Path $logDir | Out-Null
}
$logPath = Join-Path $logDir "audit_gateway_rollback.log"

function Write-RollbackLog {
  param([string]$Message)
  $line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $Message
  Add-Content -Path $logPath -Value $line -Encoding UTF8
}

Write-RollbackLog "armed delay_seconds=$DelaySeconds proxy=$ProxyServer sentinel=$SentinelPath"
Start-Sleep -Seconds $DelaySeconds

if (-not (Test-Path $SentinelPath)) {
  Write-RollbackLog "canceled sentinel_missing"
  exit 0
}

try {
  if (Test-Path $BackupDir) {
    foreach ($name in @("clash-verge.yaml", "clash-verge-check.yaml", "verge.yaml", "profiles.yaml")) {
      $src = Join-Path $BackupDir $name
      $dst = Join-Path $ClashVergeDir $name
      if (Test-Path $src) {
        Copy-Item -Path $src -Destination $dst -Force
        Write-RollbackLog "clash_verge_config_restored file=$name"
      }
    }
  }

  $key = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings"
  Set-ItemProperty -Path $key -Name ProxyEnable -Type DWord -Value 1
  Set-ItemProperty -Path $key -Name ProxyServer -Type String -Value $ProxyServer
  Remove-ItemProperty -Path $key -Name AutoConfigURL -ErrorAction SilentlyContinue
  rundll32.exe inetcpl.cpl,ClearMyTracksByProcess 8 | Out-Null
  rundll32.exe user32.dll,UpdatePerUserSystemParameters | Out-Null
  netsh winhttp import proxy source=ie | Out-Null
  Write-RollbackLog "proxy_restored proxy=$ProxyServer"
} catch {
  Write-RollbackLog ("proxy_restore_error " + $_.Exception.Message)
}

try {
  Get-Process verge-mihomo,clash-verge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 2
  if (Test-Path $ClashVergePath) {
    Start-Process -FilePath $ClashVergePath -WindowStyle Hidden
    Write-RollbackLog "clash_verge_restarted path=$ClashVergePath"
  } else {
    Write-RollbackLog "clash_verge_missing path=$ClashVergePath"
  }
} catch {
  Write-RollbackLog ("clash_verge_start_error " + $_.Exception.Message)
}

Remove-Item -Path $SentinelPath -Force -ErrorAction SilentlyContinue
Write-RollbackLog "complete"
