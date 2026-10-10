@echo off
setlocal

set ROOT_DIR=%~dp0
set LOCAL_GO=%ROOT_DIR%.tools\go
set QT_MINGW=C:\Qt\Qt5.14.2\Tools\mingw730_64

if exist "%LOCAL_GO%\bin\go.exe" set "GOROOT=%LOCAL_GO%"
if exist "%LOCAL_GO%\bin\go.exe" set "PATH=%LOCAL_GO%\bin;%PATH%"

if exist "%QT_MINGW%\bin\gcc.exe" set "PATH=%QT_MINGW%\bin;%PATH%"

if "%GOPROXY%"=="" set GOPROXY=https://goproxy.cn,direct
set CGO_ENABLED=1
set GOOS=windows
set GOARCH=amd64

where go >nul 2>nul
if errorlevel 1 (
  echo [FAIL] go.exe not found in PATH. Install Go 1.22+ first.
  exit /b 1
)

echo [INFO] tidying Go modules...
go mod tidy
if errorlevel 1 exit /b 1

where fyne >nul 2>nul
if errorlevel 1 (
  echo [WARN] fyne CLI not found; falling back to go build.
  go build -trimpath -ldflags "-s -w -H=windowsgui" -o clash-native.exe .
  if errorlevel 1 exit /b 1
  echo [OK] build done: clash-native.exe
  exit /b 0
)

if exist icon.png (
  fyne package -os windows -name clash-native -icon icon.png --release
) else (
  fyne package -os windows -name clash-native --release
)
if errorlevel 1 exit /b 1

if not exist clash-native.exe (
  echo [WARN] fyne package did not create clash-native.exe; running go build fallback.
  go build -trimpath -ldflags "-s -w -H=windowsgui" -o clash-native.exe .
  if errorlevel 1 exit /b 1
)

echo [OK] build done: clash-native.exe
exit /b 0
