param(
  [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
  [string]$BuildDir = "",
  [string]$StageDir = "",
  [string]$LlvmRoot = $env:CODEX_LLVM_ROOT,
  [string]$RocksDbRoot = "",
  [string]$GnExe = "",
  [string]$GnUrl = "https://chrome-infra-packages.appspot.com/dl/gn/gn/windows-amd64/+/latest",
  [string]$TargetCpu = "x64",
  [switch]$SkipGnDownload
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($GnUrl)) { $GnUrl = "https://chrome-infra-packages.appspot.com/dl/gn/gn/windows-amd64/+/latest" }
function Fail($Message) { Write-Error $Message; exit 1 }
function FullPath([string]$p) { if ([string]::IsNullOrWhiteSpace($p)) { return "" }; return [System.IO.Path]::GetFullPath($p) }
function SlashPath([string]$p) { return $p.Replace([char]92, '/') }

function Download-Gn([string]$Destination, [string]$Url) {
  New-Item -ItemType Directory -Force -Path (Split-Path $Destination) | Out-Null
  $zipPath = Join-Path (Split-Path $Destination) "gn.zip"
  Write-Host "Downloading GN: $Url"
  Invoke-WebRequest -Uri $Url -OutFile $zipPath
  if (Test-Path $Destination) { Remove-Item $Destination -Force }
  Expand-Archive -Path $zipPath -DestinationPath (Split-Path $Destination) -Force
  if (-not (Test-Path $Destination)) { Fail "GN archive did not contain gn.exe" }
}

$RepoRoot = FullPath $RepoRoot
if (-not (Test-Path (Join-Path $RepoRoot "src\main.cpp"))) { Fail "Invalid RepoRoot: $RepoRoot" }
if ([string]::IsNullOrWhiteSpace($LlvmRoot)) { Fail "LlvmRoot is required. Set CODEX_LLVM_ROOT or pass -LlvmRoot." }
$LlvmRoot = FullPath $LlvmRoot
if (-not (Test-Path (Join-Path $LlvmRoot "include\clang\AST\ASTConsumer.h"))) { Fail "Missing LLVM clang headers under $LlvmRoot" }
if (-not (Test-Path (Join-Path $LlvmRoot "lib\clangTooling.lib"))) { Fail "Missing clangTooling.lib under $LlvmRoot\lib" }

if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $RepoRoot "build_gn_clangcl_$TargetCpu" }
if ([string]::IsNullOrWhiteSpace($StageDir)) { $StageDir = Join-Path $BuildDir "stage\codex_lan_agent-windows-$TargetCpu-clangcl-gn" }
$BuildDir = FullPath $BuildDir
$StageDir = FullPath $StageDir

$clang = (Get-Command clang-cl.exe -ErrorAction SilentlyContinue).Source
if (-not $clang) { Fail "clang-cl.exe is not in PATH. Run from a VS/LLVM/IntelLLVM compiler environment." }
Write-Host "clang-cl: $clang"

if ([string]::IsNullOrWhiteSpace($GnExe)) {
  $gnBase = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
  $GnExe = Join-Path $gnBase "codex-lan-agent-gn\gn.exe"
}
$GnExe = FullPath $GnExe
if (-not (Test-Path $GnExe)) {
  if ($SkipGnDownload) { Fail "GN not found at $GnExe and -SkipGnDownload was specified" }
  Download-Gn -Destination $GnExe -Url $GnUrl
}
& $GnExe --version
if ($LASTEXITCODE -ne 0) { Fail "gn --version failed" }

$cppSources = @(
  "src/AgentConfig.cpp", "src/CapabilityRegistry.cpp", "src/CmmBridge.cpp",
  "src/CmmToolResults.cpp", "src/ClangIndexerAdapter.cpp", "src/SemanticGridOperations.cpp",
  "src/ClangAstVisitor.cpp", "src/ClangAstParser.cpp", "src/CfGBuilder.cpp",
  "src/GraphSerialization.cpp", "src/ProcessRunner.cpp", "src/HttpClient.cpp",
  "src/TaskMemoryRocksDbOperations.cpp", "src/main.cpp"
)
$clipsSources = Get-ChildItem (Join-Path $RepoRoot "src\clips_core") -Filter "*.c" |
  Where-Object { $_.Name -ne "clips_standalone_main.c" } |
  Sort-Object Name | ForEach-Object { "src/clips_core/$($_.Name)" }
$allSources = @($cppSources + $clipsSources)

$llvmLibs = Get-ChildItem (Join-Path $LlvmRoot "lib") -Filter "*.lib" | Sort-Object Name | ForEach-Object { SlashPath $_.FullName }
if ($llvmLibs.Count -eq 0) { Fail "No LLVM .lib files found under $LlvmRoot\lib" }
$defines = @("_CRT_SECURE_NO_WARNINGS", "WIN32_LEAN_AND_MEAN", "NOMINMAX", "CODEX_LAN_AGENT_HAS_CLANG_AST=1")
$includeDirs = @("src")
$cflags = @("/MD", "/O2", "/DNDEBUG", "/I" + (SlashPath (Join-Path $LlvmRoot "include")))
$libs = @($llvmLibs + @("ws2_32.lib", "winhttp.lib", "version.lib"))

if (-not [string]::IsNullOrWhiteSpace($RocksDbRoot)) {
  $RocksDbRoot = FullPath $RocksDbRoot
  $rocksHeader = Join-Path $RocksDbRoot "include\rocksdb\db.h"
  $rocksLib = Join-Path $RocksDbRoot "lib\rocksdb.lib"
  if (-not (Test-Path $rocksHeader)) { Fail "Missing RocksDB header: $rocksHeader" }
  if (-not (Test-Path $rocksLib)) { Fail "Missing RocksDB lib: $rocksLib" }
  $defines += "CODEX_LAN_AGENT_WITH_ROCKSDB=1"
  $cflags += "/I" + (SlashPath (Join-Path $RocksDbRoot "include"))
  $libs += @((SlashPath $rocksLib), "shlwapi.lib", "rpcrt4.lib", "iphlpapi.lib")
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$dotGn = Join-Path $RepoRoot ".gn"
$buildGn = Join-Path $RepoRoot "BUILD.gn"
$bootstrapDir = Join-Path $RepoRoot ".gn_bootstrap_codex"
$dotGnBackup = "$dotGn.codex_gn_backup"
$buildGnBackup = "$buildGn.codex_gn_backup"
$hadDotGn = Test-Path $dotGn
$hadBuildGn = Test-Path $buildGn

try {
  if ($hadDotGn) { Move-Item -Force $dotGn $dotGnBackup }
  if ($hadBuildGn) { Move-Item -Force $buildGn $buildGnBackup }
  if (Test-Path $bootstrapDir) { Remove-Item -Recurse -Force $bootstrapDir }
  New-Item -ItemType Directory -Force -Path (Join-Path $bootstrapDir "toolchain") | Out-Null

  @"
buildconfig = "//.gn_bootstrap_codex/BUILDCONFIG.gn"
default_args = {
  is_debug = false
  target_cpu = "$TargetCpu"
}
"@ | Set-Content -Path $dotGn -Encoding ASCII

  'set_default_toolchain("//.gn_bootstrap_codex/toolchain:clangcl")' | Set-Content -Path (Join-Path $bootstrapDir "BUILDCONFIG.gn") -Encoding ASCII

  @'
toolchain("clangcl") {
  tool("cc") {
    command = "clang-cl.exe /nologo /showIncludes /c {{defines}} {{include_dirs}} {{cflags}} {{cflags_c}} /Fo{{output}} {{source}}"
    depsformat = "msvc"
    outputs = [ "{{source_out_dir}}/{{target_output_name}}.{{source_name_part}}.obj" ]
  }
  tool("cxx") {
    command = "clang-cl.exe /nologo /showIncludes /c {{defines}} {{include_dirs}} {{cflags}} {{cflags_cc}} /Fo{{output}} {{source}}"
    depsformat = "msvc"
    outputs = [ "{{source_out_dir}}/{{target_output_name}}.{{source_name_part}}.obj" ]
  }
  tool("link") {
    command = "clang-cl.exe /nologo @{{output}}.rsp /Fe{{output}} /link {{ldflags}}"
    rspfile = "{{output}}.rsp"
    rspfile_content = "{{inputs}}"
    outputs = [ "{{root_out_dir}}/{{target_output_name}}.exe" ]
  }
  tool("stamp") {
    command = "cmd /c type nul > {{output}}"
  }
  tool("copy") {
    command = "cmd /c copy /Y {{source}} {{output}} > nul"
  }
}
'@ | Set-Content -Path (Join-Path $bootstrapDir "toolchain\BUILD.gn") -Encoding ASCII

  $sourcesGn = ($allSources | ForEach-Object { "    `"//$_`"," }) -join "`n"
  $definesGn = ($defines | ForEach-Object { "    `"$_`"," }) -join "`n"
  $includesGn = ($includeDirs | ForEach-Object { "    `"$_`"," }) -join "`n"
  $cflagsGn = ($cflags | ForEach-Object { "    `"$_`"," }) -join "`n"
  $libsGn = ($libs | ForEach-Object { "    `"$_`"," }) -join "`n"
  @"
executable("codex_lan_agent") {
  sources = [
$sourcesGn
  ]
  defines = [
$definesGn
  ]
  include_dirs = [
$includesGn
  ]
  cflags = [
$cflagsGn
  ]
  cflags_c = [ "/std:c17" ]
  cflags_cc = [ "/std:c++20", "/EHsc" ]
  ldflags = [
    "/INCREMENTAL:NO",
    "/SUBSYSTEM:CONSOLE",
$libsGn
  ]
}
"@ | Set-Content -Path $buildGn -Encoding ASCII

  @"
is_debug = false
target_cpu = "$TargetCpu"
"@ | Set-Content -Path (Join-Path $BuildDir "args.gn") -Encoding ASCII
  $gnOut = SlashPath $BuildDir
  $gnRoot = SlashPath $RepoRoot
  & $GnExe @("gen", $gnOut, "--root=$gnRoot")
  if ($LASTEXITCODE -ne 0) { Fail "gn gen failed" }
  ninja -C $BuildDir codex_lan_agent
  if ($LASTEXITCODE -ne 0) { Fail "ninja build failed" }

  $exe = Join-Path $BuildDir "codex_lan_agent.exe"
  if (-not (Test-Path $exe)) { Fail "Expected exe missing: $exe" }
  if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
  New-Item -ItemType Directory -Force -Path (Join-Path $StageDir "bin") | Out-Null
  Copy-Item -Force $exe (Join-Path $StageDir "bin\codex_lan_agent.exe")
  # Do not bundle local compiler runtime DLLs; CI release builds must use network-built toolchains.
  foreach ($name in @("codex_lan_agent.cfg", "README.md", "LICENSE")) {
    $p = Join-Path $RepoRoot $name
    if (Test-Path $p) { Copy-Item -Force $p $StageDir }
  }
  $zip = Join-Path $BuildDir "codex_lan_agent-windows-$TargetCpu-clangcl-gn.zip"
  if (Test-Path $zip) { Remove-Item $zip -Force }
  Compress-Archive -Path (Join-Path $StageDir "*") -DestinationPath $zip
  $item = Get-Item $zip
  Write-Host "GN clang-cl release package: $zip ($([math]::Round($item.Length/1MB, 2)) MB)"
}
finally {
  Remove-Item -Force $dotGn -ErrorAction SilentlyContinue
  Remove-Item -Force $buildGn -ErrorAction SilentlyContinue
  Remove-Item -Recurse -Force $bootstrapDir -ErrorAction SilentlyContinue
  if ($hadDotGn -and (Test-Path $dotGnBackup)) { Move-Item -Force $dotGnBackup $dotGn }
  if ($hadBuildGn -and (Test-Path $buildGnBackup)) { Move-Item -Force $buildGnBackup $buildGn }
}