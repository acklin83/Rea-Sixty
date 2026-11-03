#include "WinUsbDriver.h"

#if defined(_WIN32)

#include "winusb_inf.h"   // generated: uf8::setup_bundle::kWinUsbInfBytes{Bytes,Size}

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>     // CoInitializeEx: ShellExecuteEx wants an apartment
#include <setupapi.h>    // driver-status readout (no elevation, no PowerShell)

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace winusb {

// Drop the embedded WinUSB INF + a PowerShell helper to %TEMP% and run
// the helper elevated under one UAC prompt. The helper:
//   1. Mints (or reuses) a self-signed CodeSigningCert in LocalMachine\My
//      named "Rea-Sixty Driver Signer".
//   2. Installs the cert into LocalMachine\Root + TrustedPublisher so
//      pnputil trusts CAT files signed by it.
//   3. Builds a Windows file catalog (New-FileCatalog) over the package
//      directory and Authenticode-signs it with the cert.
//   4. Adds our package to the driver store with "pnputil /add-driver".
//      We deliberately do NOT remove SSL's sslbus.inf any more: it is the
//      SHARED bus driver for the whole SSL family — UF8/UC1 AND UF1 +
//      O-Series tiles (VID_31E9 PID_0024..0028). Deleting it (as older
//      builds did) silently broke the UF1 in SSL 360° with a Code 28
//      "no driver installed". We coexist with sslbus instead.
//   5. Force-binds the UF8 (PID_0021) + UC1 (PID_0023) + UF1 (PID_0025)
//      interfaces to our WinUSB INF via newdev's
//      UpdateDriverForPlugAndPlayDevices with INSTALLFLAG_FORCE — the same
//      call Zadig uses. pnputil's /install uses rank-based selection and
//      refuses to swap a device that is already working on sslbus, so a
//      plain /install can't move a UF8 that SSL 360° already claimed; the
//      FORCE call overrides the current binding. After install SSL 360°
//      stops seeing the UF1 — intended: the program drives
//      the UF1 directly. Only the three device INTERFACES are rebound;
//      sslbus itself is left in place, so O-Series tiles (PID_0024/0026..
//      0028), which we never name, keep their sslbus binding.
//
// This wraps the full Zadig-equivalent flow into a single UAC prompt.
// The Settings "uninstall" button removes our INF + cert (one staged copy
// per pass); the UF8/UC1 then re-bind to sslbus on their next replug.
//
// Returns true if PowerShell was launched (final result is visible
// only via the device state after the user clicks through UAC).
namespace {

// ---- the job ------------------------------------------------------------
// The install takes seconds to tens of seconds (minting a cert, pnputil, a
// force-bind per hardware id). Waiting for it on the UI thread would freeze it for
// that whole time, so the wait happens on a worker and the UI polls these.
std::atomic<int> g_jobState{0};   // 0 = idle/never run, 1 = running, 2 = done
std::mutex       g_jobMu;
std::string      g_jobMsg;

void setJobMsg(std::string m)
{
    std::lock_guard<std::mutex> lk(g_jobMu);
    g_jobMsg = std::move(m);
}

// Launch `psPath` elevated and WAIT for it, so we can report what actually
// happened instead of "started". Three outcomes the user can act on:
//   cancelled  : ShellExecuteEx fails with ERROR_CANCELLED. Very common, and it
//                is NOT a failure; saying "failed" here sends people debugging
//                a driver problem they do not have.
//   exit != 0  : the script threw. Its transcript has the reason; name the path.
//   exit == 0  : installed.
bool runElevated(const char* psPath, const char* what, const char* logName, std::string* errOut)
{
    if (g_jobState.load() == 1) {
        if (errOut) *errOut = "a driver job is already running";
        return false;
    }
    char tmpDir[MAX_PATH] = {0};
    GetTempPathA(MAX_PATH, tmpDir);
    std::string logPath = std::string(tmpDir) + logName;
    std::string ps      = psPath;
    std::string label   = what;

    g_jobState.store(1);
    setJobMsg(std::string("Driver ") + label + " running. Follow the UAC "
              "prompt if it appears.");

    std::thread([ps, label, logPath]() {
        // ShellExecuteEx wants an initialised COM apartment on the calling
        // thread; the main thread has one, a fresh std::thread does not.
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        std::string args = "-NoProfile -ExecutionPolicy Bypass -File \"" + ps + "\"";

        SHELLEXECUTEINFOA sei{};
        sei.cbSize       = sizeof(sei);
        sei.fMask        = SEE_MASK_NOCLOSEPROCESS;   // we need the handle to wait on
        sei.lpVerb       = "runas";
        sei.lpFile       = "powershell.exe";
        sei.lpParameters = args.c_str();
        sei.nShow        = SW_HIDE;

        std::string msg;
        if (!ShellExecuteExA(&sei) || !sei.hProcess) {
            const DWORD e = GetLastError();
            msg = (e == ERROR_CANCELLED)
                ? ("Driver " + label + " cancelled at the Windows permission "
                   "prompt. Nothing was changed.")
                : ("Driver " + label + " could not start (error "
                   + std::to_string(static_cast<unsigned>(e)) + ").");
        } else {
            WaitForSingleObject(sei.hProcess, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(sei.hProcess, &code);
            CloseHandle(sei.hProcess);
            if (code == 0) {
                msg = (label == "install")
                    ? "Driver installed. Unplug and replug the devices. In "
                      "Device Manager they move to \"Universal Serial Bus "
                      "devices\" and are renamed to \"SSL ... (WinUSB)\"."
                    : "Driver uninstalled. Unplug and replug the devices. "
                      "Reinstall SSL 360\xC2\xB0 to restore its driver.";
            } else {
                msg = "Driver " + label + " FAILED (exit "
                    + std::to_string(static_cast<unsigned>(code))
                    + "). Details: " + logPath;
            }
        }
        setJobMsg(std::move(msg));
        g_jobState.store(2);
        if (SUCCEEDED(co)) CoUninitialize();
    }).detach();

    return true;
}

} // namespace

int jobState() { return g_jobState.load(); }

std::string jobMessage()
{
    std::lock_guard<std::mutex> lk(g_jobMu);
    return g_jobMsg;
}

// Back to idle once the UI has taken the result. Without this a UI would re-adopt
// the same verdict every frame and re-enumerate the USB tree with it.
void jobClear()
{
    int expected = 2;
    g_jobState.compare_exchange_strong(expected, 0);
}

// ---- what is each surface actually bound to, right now? -------------------
// SetupAPI, not PowerShell: this only READS, so it needs no elevation and no UAC,
// and it can refresh while a pane is open. Service "WinUSB" means ours and the
// device will be picked up; "SSLBUS" means SSL's driver still owns it and libusb
// cannot open it.
std::vector<DeviceState> status(const std::vector<Device>& wanted)
{
    std::vector<DeviceState> out;
    for (const Device& d : wanted) { DeviceState s; s.name = d.name; out.push_back(s); }
    HDEVINFO set = SetupDiGetClassDevsA(nullptr, "USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA dev{};
    dev.cbSize = sizeof(dev);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); ++i) {
        char hw[1024] = {0};
        if (!SetupDiGetDeviceRegistryPropertyA(set, &dev, SPDRP_HARDWAREID, nullptr,
                                               reinterpret_cast<PBYTE>(hw), sizeof(hw) - 2, nullptr))
            continue;
        // REG_MULTI_SZ: walk the NUL-separated ids.
        for (std::size_t w = 0; w < wanted.size(); ++w) {
            bool hit = false;
            for (const char* p = hw; *p; p += std::strlen(p) + 1)
                if (std::strstr(p, wanted[w].id)) { hit = true; break; }
            if (!hit || out[w].connected) continue;
            char svc[128] = {0}, mfg[256] = {0};
            SetupDiGetDeviceRegistryPropertyA(set, &dev, SPDRP_SERVICE, nullptr,
                                              reinterpret_cast<PBYTE>(svc), sizeof(svc) - 1, nullptr);
            SetupDiGetDeviceRegistryPropertyA(set, &dev, SPDRP_MFG, nullptr,
                                              reinterpret_cast<PBYTE>(mfg), sizeof(mfg) - 1, nullptr);
            out[w].connected = true;
            out[w].service   = svc;
            out[w].maker     = mfg;
            out[w].ready     = _stricmp(svc, "WinUSB") == 0;
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

bool install(const std::vector<Device>& devices, std::string* errOut)
{
    namespace sb = uf8::setup_bundle;

    char tmpDir[MAX_PATH] = {0};
    if (!GetTempPathA(MAX_PATH, tmpDir)) {
        if (errOut) *errOut = "GetTempPathA failed";
        return false;
    }

    // Working dir: %TEMP%\rea_sixty_winusb\. ONLY the INF lives in here —
    // New-FileCatalog hashes every file under -Path, so a catalog written into
    // this folder becomes an input to the next run's hashing and throws. That is
    // what locked out every user who pressed the button twice; see below.
    char workDir[MAX_PATH];
    std::snprintf(workDir, sizeof(workDir), "%srea_sixty_winusb", tmpDir);
    CreateDirectoryA(workDir, nullptr);  // OK if it already exists

    char infPath[MAX_PATH];
    std::snprintf(infPath, sizeof(infPath), "%s\\rea_sixty_winusb.inf", workDir);

    // Belt and braces: clear a catalog left by a build that still wrote it in
    // here, so upgrading to this fix repairs a machine that is already poisoned
    // instead of needing the user to empty %TEMP% by hand.
    {
        char staleCat[MAX_PATH];
        std::snprintf(staleCat, sizeof(staleCat), "%s\\rea_sixty_winusb.cat", workDir);
        DeleteFileA(staleCat);
    }

    if (FILE* f = std::fopen(infPath, "wb")) {
        std::fwrite(sb::kWinUsbInfBytesBytes, 1,
                    sb::kWinUsbInfBytesSize, f);
        std::fclose(f);
    } else {
        if (errOut) *errOut = std::string("could not write ") + infPath;
        return false;
    }

    char psPath[MAX_PATH];
    std::snprintf(psPath, sizeof(psPath),
             "%sssl_winusb_install.ps1", tmpDir);

    // PowerShell helper. ASCII-safe (no UTF8/BOM concerns when written
    // via fopen "wb"). Keep newlines as \n; PowerShell accepts them.
    static const char* kPsScript = R"PS($ErrorActionPreference = 'Stop'

# Everything this script does is transcribed. Before this, a throw closed the
# hidden PowerShell window with no trace, so neither the user nor we could tell
# whether the driver had installed — the whole reason creal's catalog failure
# went unseen for as long as it did. The transcript is the artifact he can paste.
$log = Join-Path $env:TEMP 'ssl_winusb_install.log'
try { Start-Transcript -Path $log -Force | Out-Null } catch { }
try {

$wd = Join-Path $env:TEMP 'rea_sixty_winusb'
$inf = Join-Path $wd 'rea_sixty_winusb.inf'
if (-not (Test-Path -LiteralPath $inf)) { throw "INF missing at $inf" }

# Step 1+2: reuse or mint cert, then trust it.
$subject = 'CN=Rea-Sixty Driver Signer'
$cert = Get-ChildItem Cert:\LocalMachine\My |
        Where-Object { $_.Subject -eq $subject } |
        Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert -Subject $subject `
        -KeyUsage DigitalSignature `
        -CertStoreLocation 'Cert:\LocalMachine\My' `
        -KeyExportPolicy Exportable -NotAfter ((Get-Date).AddYears(5))
    $certPath = Join-Path $env:TEMP 'rea_sixty_signer.cer'
    Export-Certificate -Cert $cert -FilePath $certPath -Force | Out-Null
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $certPath -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
}

# Step 3: catalog + signature.
# ⛔ The catalog MUST NOT live inside $wd. New-FileCatalog hashes every file
# under -Path, so a .cat written in there is hashed by the NEXT run and the
# cmdlet throws "Unable to create the hash for file ...rea_sixty_winusb.cat".
# With $ErrorActionPreference = 'Stop' the script dies right there, before
# pnputil ever runs, and the window closes before anyone can read it. First run
# looked fine because the folder held only the INF — so every SECOND and later
# attempt on a user's machine was a silent no-op. Forum user creal lost his UF1
# to this and nearly sold it (2026-08-13).
# The signed CAT is referenced by CatalogFile= in the INF, so pnputil needs it
# NEXT TO the INF at /add-driver time: build it outside, then copy it in, and
# remove it again so the folder is clean for the next run.
$cat = Join-Path $env:TEMP 'rea_sixty_winusb.cat'
Remove-Item -LiteralPath $cat -Force -ErrorAction SilentlyContinue
# Self-healing, and NOT a duplicate of the C++ staging cleanup: a machine
# poisoned by an older build has a .cat sitting in $wd already, and if the
# staging delete failed for any reason the very first hash here would throw
# again. Clearing it here means the script repairs itself no matter how it
# was launched.
Get-ChildItem -LiteralPath $wd -Filter *.cat -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
New-FileCatalog -Path $wd -CatalogFilePath $cat -CatalogVersion 2 | Out-Null
$sig = Set-AuthenticodeSignature -FilePath $cat -Certificate $cert -HashAlgorithm SHA256
if ($sig.Status -ne 'Valid') { throw "CAT signature failed: $($sig.Status)" }

# Step 4: add our package to the store. Do NOT delete SSL's sslbus.inf:
# it is the shared bus driver for the whole SSL family (UF8/UC1 AND UF1 +
# O-Series), and removing it breaks the UF1 in SSL 360 with Code 28.
# We coexist with it and override only our two device interfaces below.
# The INF's CatalogFile= names rea_sixty_winusb.cat as a sibling, so put the
# signed catalog beside it for the duration of the call and take it out again.
$catInWd = Join-Path $wd 'rea_sixty_winusb.cat'
Copy-Item -LiteralPath $cat -Destination $catInWd -Force
try   { pnputil /add-driver $inf | Out-Null }
finally {
    # ALWAYS, including on a throw: a catalog left behind here is exactly what
    # made the next run fail.
    Remove-Item -LiteralPath $catInWd -Force -ErrorAction SilentlyContinue
}

# Resolve the staged published name (oemNN.inf) of our just-added package.
# Walk enum-drivers line by line: "Published Name" precedes "Original Name"
# inside each block, so track the last Published Name and grab it when we
# reach our Original Name. (A lazy cross-entry regex mis-captures a foreign
# oem, e.g. the first Published Name in the listing.)
$ourInf = $inf
$pub = $null
foreach ($line in (pnputil /enum-drivers)) {
    if ($line -match 'Published Name:\s+(oem\d+\.inf)') { $pub = $matches[1] }
    elseif ($line -match 'Original Name:\s+rea_sixty_winusb\.inf') {
        if ($pub) { $ourInf = Join-Path $env:WINDIR ('INF\' + $pub) }
        break
    }
}

# Step 5: force-bind ONLY UF8 (PID_0021) + UC1 (PID_0023) to our WinUSB
# INF. pnputil /install uses rank-based selection and will not swap a
# device already working on sslbus, so go through newdev's
# UpdateDriverForPlugAndPlayDevices with INSTALLFLAG_FORCE (the mechanism
# Zadig uses) which overrides the current binding for a hardware id.
# UF8/UC1/UF1 are force-bound; O-Series ids are never named -> they stay on
# sslbus. sslbus is left installed, so O-Series keeps working; only the UF1
# interface moves to WinUSB (SSL 360 then stops seeing the UF1, as intended).
$nd = @'
using System;
using System.Runtime.InteropServices;
public static class RsNewDev {
  [DllImport("newdev.dll", CharSet=CharSet.Unicode, SetLastError=true)]
  public static extern bool UpdateDriverForPlugAndPlayDevices(
    IntPtr hwndParent, string HardwareId, string FullInfPath,
    uint InstallFlags, out bool bRebootRequired);
}
'@
Add-Type -TypeDefinition $nd
$INSTALLFLAG_FORCE = 0x1
# NOTE: only devices PRESENT right now get force-bound. A UF8/UC1 that is
# first plugged in AFTER this runs goes through normal PnP ranking; with
# sslbus in the store it may land on sslbus, so the user re-runs this with
# the device attached. (The old delete-sslbus path auto-bound late hotplugs
# but at the cost of breaking UF1 — this trade is deliberate.)
foreach ($hwid in @(@@HWIDS@@)) {
    $reboot = $false
    try {
        [void][RsNewDev]::UpdateDriverForPlugAndPlayDevices(
            [IntPtr]::Zero, $hwid, $ourInf, $INSTALLFLAG_FORCE, [ref]$reboot)
    } catch { }
    # An absent device is a benign no-op (returns FALSE, no swap); only a
    # currently-attached UF8/UC1/UF1 is rebound to WinUSB.
}

# Step 6: keep exactly ONE copy of our package in the driver store.
# `pnputil /add-driver` stages a NEW oemNN on every call, so each press of the
# install button left another copy behind — measured on the StoerPC 2026-08-14,
# four runs turned oem2 into oem2 + oem13. That is how users end up with the
# duplicates they cannot remove: the uninstall path resolves ONE oemNN, so the
# extras are unreachable from the UI and regedit is not a supported way out
# (forum user creal had exactly this, 2026-08-13).
#
# Delete only packages NO DEVICE IS USING: read each device's actual
# DriverInfPath and keep those. Plain /delete-driver, never /uninstall /force —
# an unused package deletes cleanly, and refusing to touch a bound one is the
# whole safety property here.
try {
    $inUse = @{}
    foreach ($devpid in @('PID_0021','PID_0023','PID_0025')) {
        Get-PnpDevice -EA SilentlyContinue |
            Where-Object { $_.InstanceId -like "USB\VID_31E9&$devpid*" } |
            ForEach-Object {
                $p = (Get-PnpDeviceProperty -InstanceId $_.InstanceId `
                        -KeyName DEVPKEY_Device_DriverInfPath -EA SilentlyContinue).Data
                if ($p) { $inUse[$p.ToLower()] = $true }
            }
    }
    $ours = @()
    $pub2 = $null
    foreach ($line in (pnputil /enum-drivers)) {
        if ($line -match 'Published Name:\s+(oem\d+\.inf)') { $pub2 = $matches[1] }
        elseif ($line -match 'Original Name:\s+rea_sixty_winusb\.inf') {
            if ($pub2) { $ours += $pub2 }
        }
    }
    # Never leave zero: if nothing is bound yet (no device attached), keep the
    # newest and drop the rest, so a device plugged in later still finds one.
    if ($ours.Count -gt 1 -and $inUse.Count -eq 0) {
        $keep = ($ours | Sort-Object { [int]($_ -replace '\D','') })[-1]
        $inUse[$keep.ToLower()] = $true
    }
    foreach ($o in $ours) {
        if (-not $inUse.ContainsKey($o.ToLower())) {
            pnputil /delete-driver $o 2>&1 | Out-Null
        }
    }
} catch { }

Write-Output 'REASIXTY_RESULT=OK'
}
catch {
    # The exit code is what the caller reports as installed / failed, so a throw
    # must not leave it at 0. The message goes into the transcript too.
    Write-Output ('REASIXTY_RESULT=FAIL ' + $_.Exception.Message)
    try { Stop-Transcript | Out-Null } catch { }
    exit 1
}
try { Stop-Transcript | Out-Null } catch { }
exit 0
)PS";

    // Only these get bound, whatever else is plugged in. ORC leaves the UC1 to SSL 360.
    std::string hwids;
    for (const Device& d : devices) {
        std::string id = d.id;   // "VID_31E9&PID_0025"
        if (!hwids.empty()) hwids += ",";
        hwids += "'USB\\" + id + "'";
    }
    std::string script = kPsScript;
    const std::string mark = "@@HWIDS@@";
    script.replace(script.find(mark), mark.size(), hwids);

    if (FILE* f = std::fopen(psPath, "wb")) {
        std::fwrite(script.data(), 1, script.size(), f);
        std::fclose(f);
    } else {
        if (errOut) *errOut = std::string("could not write ") + psPath;
        return false;
    }

    return runElevated(psPath, "install", "ssl_winusb_install.log", errOut);
}

bool uninstall(std::string* errOut)
{
    // PowerShell one-liner: find our rea_sixty_winusb.inf in the driver
    // store and remove it. Also delete the signing cert we created.
    static const char* kPsUninstall = R"PS($ErrorActionPreference = 'Stop'

$log = Join-Path $env:TEMP 'ssl_winusb_uninstall.log'
try { Start-Transcript -Path $log -Force | Out-Null } catch { }
try {

# Remove EVERY copy of our package, not just the first. The old regex matched a
# single oemNN, so a machine that had accumulated copies (one per press of the
# install button, before that was fixed) could never be cleaned from the UI and
# the user was left reaching for regedit, which Windows refuses.
$pub = $null
$ours = @()
foreach ($line in (pnputil /enum-drivers)) {
    if ($line -match 'Published Name:\s+(oem\d+\.inf)') { $pub = $matches[1] }
    elseif ($line -match 'Original Name:\s+rea_sixty_winusb\.inf') {
        if ($pub) { $ours += $pub }
    }
}
foreach ($o in $ours) { pnputil /delete-driver $o /uninstall 2>&1 | Out-Null }
Write-Output ("removed: " + ($ours -join ',') )

# Remove signing cert from all stores.
$subject = 'CN=Rea-Sixty Driver Signer'
foreach ($store in @('My','Root','TrustedPublisher')) {
    Get-ChildItem "Cert:\LocalMachine\$store" |
        Where-Object { $_.Subject -eq $subject } |
        Remove-Item -Force 2>$null
}

Write-Output 'REASIXTY_RESULT=OK'
}
catch {
    Write-Output ('REASIXTY_RESULT=FAIL ' + $_.Exception.Message)
    try { Stop-Transcript | Out-Null } catch { }
    exit 1
}
try { Stop-Transcript | Out-Null } catch { }
exit 0
)PS";

    char tmpDir[MAX_PATH] = {0};
    if (!GetTempPathA(MAX_PATH, tmpDir)) {
        if (errOut) *errOut = "GetTempPathA failed";
        return false;
    }
    char psPath[MAX_PATH];
    std::snprintf(psPath, sizeof(psPath), "%sssl_winusb_uninstall.ps1", tmpDir);

    if (FILE* f = std::fopen(psPath, "wb")) {
        std::fwrite(kPsUninstall, 1, std::strlen(kPsUninstall), f);
        std::fclose(f);
    } else {
        if (errOut) *errOut = std::string("could not write ") + psPath;
        return false;
    }

    return runElevated(psPath, "uninstall", "ssl_winusb_uninstall.log", errOut);
}

} // namespace winusb

#endif // _WIN32
