$ErrorActionPreference = "Stop"

$tracked = git ls-files
$forbiddenPatterns = @(
  '^(third_party|vendor|vendored|deps|external|AIbuild|logs|stage|out|dist|release)(/|\\)',
  '\.(exe|dll|lib|obj|o|a|pdb|ilk|exp|zip|7z|rar|tar|gz|xz|bz2|msi|cab|nupkg|png|jpg|jpeg|gif|webp|bmp|ico|tif|tiff|svg|pdf)$'
)

$violations = @()
foreach ($path in $tracked) {
  $normalized = $path -replace '\\', '/'
  foreach ($pattern in $forbiddenPatterns) {
    if ($normalized -match $pattern) {
      $violations += $path
      break
    }
  }
}

if ($violations.Count -gt 0) {
  Write-Host "Repository upload policy violation: local/generated artifacts are tracked."
  $violations | Sort-Object -Unique | ForEach-Object { Write-Host "FORBIDDEN_TRACKED_FILE $_" }
  throw "Do not upload local dll/exe/zip/package/image/build artifacts. Build release artifacts on CI only."
}

Write-Host "Repository upload policy OK: no tracked local dll/exe/zip/package/image/build artifacts."