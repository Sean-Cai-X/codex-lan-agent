@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "CORE_EXE=%ROOT_DIR%clash-meta.exe"
set "APP_EXE=%ROOT_DIR%clash-native.exe"
set "CONFIG_FILE=%ROOT_DIR%config.yaml"
set "STDOUT_LOG=%ROOT_DIR%clash-meta.stdout.log"
set "STDERR_LOG=%ROOT_DIR%clash-meta.stderr.log"
set "API_URL=http://127.0.0.1:9090/version"

if not exist "%CORE_EXE%" (
  echo [FAIL] Missing "%CORE_EXE%"
  echo Copy mihomo.exe or clash-meta.exe here and rename it to clash-meta.exe.
  pause
  exit /b 1
)

if not exist "%CONFIG_FILE%" (
  echo [FAIL] Missing "%CONFIG_FILE%"
  echo Create config.yaml from your subscription/config and include:
  echo   mixed-port: 7890
  echo   external-controller: 127.0.0.1:9090
  echo   external-ui: ui
  pause
  exit /b 1
)

if not exist "%APP_EXE%" (
  echo [FAIL] Missing "%APP_EXE%"
  echo Run build.bat first.
  pause
  exit /b 1
)

echo [INFO] stopping old project-local clash-meta.exe if present...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$root=(Resolve-Path '%ROOT_DIR%').Path.TrimEnd('\');" ^
  "Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'clash-meta.exe' -and $_.ExecutablePath -like ($root + '*') } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"

echo [INFO] starting clash-meta.exe...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$root=(Resolve-Path '%ROOT_DIR%').Path;" ^
  "$args=@('-d',$root,'-f',(Join-Path $root 'config.yaml'));" ^
  "Start-Process -FilePath (Join-Path $root 'clash-meta.exe') -ArgumentList $args -WorkingDirectory $root -RedirectStandardOutput (Join-Path $root 'clash-meta.stdout.log') -RedirectStandardError (Join-Path $root 'clash-meta.stderr.log') -WindowStyle Hidden"

echo [INFO] waiting for API %API_URL% ...
set "READY=0"
for /l %%i in (1,1,20) do (
  curl.exe -s "%API_URL%" | findstr /c:"version" >nul
  if not errorlevel 1 (
    set "READY=1"
    goto :api_ready
  )
  timeout /t 1 /nobreak >nul
)

:api_ready
if not "%READY%"=="1" (
  echo [FAIL] API did not become ready.
  echo Check logs:
  echo   %STDOUT_LOG%
  echo   %STDERR_LOG%
  pause
  exit /b 1
)

echo [OK] API ready at http://127.0.0.1:9090/
echo [OK] Settings page: http://127.0.0.1:9090/ui/

tasklist /FI "IMAGENAME eq clash-native.exe" | findstr /i "clash-native.exe" >nul
if errorlevel 1 (
  echo [INFO] starting clash-native.exe...
  start "" "%APP_EXE%"
) else (
  echo [INFO] clash-native.exe is already running.
)

echo [DONE] Clash Native is ready.
pause
