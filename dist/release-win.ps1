# Rea-Sixty Windows release packer. Bundles the built DLL with its
# two runtime dependencies (libusb + hidapi) into a single zip that
# users can drop into REAPER manually, and that the ReaPack index
# points at per-file.
#
# Pre-reqs:
#   - extension\build\Release\reaper_rea-sixty.dll built (run the
#     project's existing build_rea.bat first)
#   - libusb-1.0.dll + hidapi.dll at the paths configured below, or
#     pass -LibusbDll / -HidapiDll overrides
#
# Run from the repo root:
#   pwsh -File dist\release-win.ps1
#
# Output: dist\rea-sixty-win-v<VERSION>.zip with all three DLLs at
# the top level. install-windows.md documents the install paths.
#
# No code signing - Windows users get a "Publisher unknown" SmartScreen
# warning on the WinUSB installer step regardless of how we sign the
# DLL itself (the warning is about the .CAT that the in-product
# installer mints, not our DLL). Signing the DLL would only suppress
# the irrelevant "downloaded from internet" Mark-of-the-Web warning;
# not worth a code-signing cert.

param(
    [string]$DllPath    = "extension\build\Release\reaper_rea-sixty.dll",
    [string]$LibusbDll  = "$env:LIBUSB_ROOT\VS2022\MS64\dll\libusb-1.0.dll",
    [string]$HidapiDll  = "$env:HIDAPI_ROOT\x64\hidapi.dll"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$DistDir  = Join-Path $RepoRoot "dist"

# Version from git tag (preferred) or short SHA fallback. Wrapped in
# try/catch so that the no-tags-yet case (`git describe` emits its
# error on stderr) doesn't trip $ErrorActionPreference = "Stop".
$Version = ""
try { $Version = (& git -C $RepoRoot describe --tags --abbrev=0 2>$null) } catch {}
if (-not $Version) {
    $Version = (& git -C $RepoRoot rev-parse --short HEAD)
}

$Stage   = Join-Path $DistDir "stage-win-$Version"
$ZipPath = Join-Path $DistDir "rea-sixty-win-$Version.zip"

Write-Host "==> Release stage: $Stage"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Path $Stage | Out-Null

$Sources = @(
    @{ Name = "reaper_rea-sixty.dll"; Src = (Join-Path $RepoRoot $DllPath) },
    @{ Name = "libusb-1.0.dll";       Src = $LibusbDll },
    @{ Name = "hidapi.dll";           Src = $HidapiDll }
)

foreach ($s in $Sources) {
    if (-not (Test-Path $s.Src)) {
        Write-Error "Missing: $($s.Src) - pass -DllPath / -LibusbDll / -HidapiDll override."
        exit 1
    }
    Copy-Item -Force $s.Src (Join-Path $Stage $s.Name)
}

Write-Host "==> Pack zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
Compress-Archive -Path (Join-Path $Stage "*.dll") -DestinationPath $ZipPath

Write-Host ""
Write-Host "==> Done. Artifact: $ZipPath"
Get-Item $ZipPath | Select-Object Name, Length, LastWriteTime
