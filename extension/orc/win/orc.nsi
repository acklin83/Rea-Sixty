; ORC's Windows installer (docs/orc-windows-plan.md, section 13). Built by
; .github/workflows/orc-windows.yml:
;   makensis /DVERSION=<x.y.z> /DSTAGE=<folder> /DOUT=<installer> orc.nsi
; /DOUT names the installer it writes.
;
; Everything ORC needs as an administrator happens here, once: Program Files, the
; firewall rule, the WebView2 Runtime. ⛔ Not the USB driver: it takes the surfaces
; away from SSL 360, so that is a button in ORC's settings.

Unicode true
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "x64.nsh"

Name "ORC ${VERSION}"
OutFile "${OUT}"
InstallDir "$PROGRAMFILES64\ORC"
RequestExecutionLevel admin
BrandingText "ORC ${VERSION}"

!define KEY_UNINSTALL "Software\Microsoft\Windows\CurrentVersion\Uninstall\ORC"
!define KEY_WEBVIEW2 "Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
!define MUTEX "Local\ch.stoersender.orc"

!define MUI_ICON "ORC.ico"
!define MUI_UNICON "ORC.ico"
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION StartOrc
!define MUI_FINISHPAGE_RUN_TEXT "Start ORC"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; The installer runs elevated and ORC must not: through Explorer it starts as the user.
Function StartOrc
    Exec '"$WINDIR\explorer.exe" "$INSTDIR\ORC.exe"'
FunctionEnd

; ORC keeps the USB devices and its files open: it has to be gone first.
!macro WaitForOrcToQuit
    retry:
    System::Call 'kernel32::OpenMutexW(i 0x100000, i 0, w "${MUTEX}") p .R0'
    ${If} $R0 P<> 0
        System::Call 'kernel32::CloseHandle(p $R0)'
        MessageBox MB_RETRYCANCEL|MB_ICONINFORMATION "ORC is running. Quit it from its icon in the taskbar, then press Retry." IDRETRY retry
        Abort
    ${EndIf}
!macroend

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "ORC needs 64-bit Windows."
        Abort
    ${EndIf}
    SetRegView 64
    !insertmacro WaitForOrcToQuit
FunctionEnd

Function un.onInit
    SetRegView 64
    !insertmacro WaitForOrcToQuit
FunctionEnd

Section
    SetOutPath "$INSTDIR"
    File "${STAGE}\ORC.exe"
    File "${STAGE}\libusb-1.0.dll"
    File "${STAGE}\vcruntime140.dll"
    File /r "${STAGE}\moreme"
    File /r "${STAGE}\settings"
    File /r "${STAGE}\Licenses"
    WriteUninstaller "$INSTDIR\Uninstall ORC.exe"

    CreateShortcut "$SMPROGRAMS\ORC.lnk" "$INSTDIR\ORC.exe"

    ; ⇨ MoreMe's page for the phones. Without an allow rule Windows asks at the first
    ; listen, and a non-admin or a Cancel leaves a block rule behind: MoreMe would stay
    ; deaf without a word. All profiles, since Windows makes an unknown network Public
    ; (Frank 03.11.2026). It only counts while MoreMe runs, and MoreMe has the PIN.
    nsExec::Exec 'netsh advfirewall firewall delete rule name="ORC"'
    Pop $0
    nsExec::Exec 'netsh advfirewall firewall add rule name="ORC" dir=in action=allow program="$INSTDIR\ORC.exe" enable=yes profile=any'
    Pop $0

    ; The settings window. Windows 11 has it, a few Windows 10 machines do not.
    ReadRegStr $0 HKLM "SOFTWARE\WOW6432Node\${KEY_WEBVIEW2}" "pv"
    ReadRegStr $1 HKCU "Software\${KEY_WEBVIEW2}" "pv"
    ${If} $0 == ""
    ${OrIf} $0 == "0.0.0.0"
        ${If} $1 == ""
        ${OrIf} $1 == "0.0.0.0"
            DetailPrint "Installing the Microsoft Edge WebView2 Runtime"
            SetOutPath "$PLUGINSDIR"
            File "${STAGE}\MicrosoftEdgeWebview2Setup.exe"
            ExecWait '"$PLUGINSDIR\MicrosoftEdgeWebview2Setup.exe" /silent /install'
            SetOutPath "$INSTDIR"
        ${EndIf}
    ${EndIf}

    WriteRegStr HKLM "${KEY_UNINSTALL}" "DisplayName" "ORC"
    WriteRegStr HKLM "${KEY_UNINSTALL}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${KEY_UNINSTALL}" "Publisher" "St$örsender Studio"
    WriteRegStr HKLM "${KEY_UNINSTALL}" "DisplayIcon" "$INSTDIR\ORC.exe"
    WriteRegStr HKLM "${KEY_UNINSTALL}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${KEY_UNINSTALL}" "UninstallString" '"$INSTDIR\Uninstall ORC.exe"'
    WriteRegDWORD HKLM "${KEY_UNINSTALL}" "NoModify" 1
    WriteRegDWORD HKLM "${KEY_UNINSTALL}" "NoRepair" 1
SectionEnd

; ORC's own files in %APPDATA%\ORC stay, as on the Mac, and so does the USB driver:
; Rea-Sixty may be using it.
Section "Uninstall"
    nsExec::Exec 'netsh advfirewall firewall delete rule name="ORC"'
    Pop $0
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "ORC"
    DeleteRegKey HKLM "${KEY_UNINSTALL}"
    Delete "$SMPROGRAMS\ORC.lnk"
    Delete "$INSTDIR\ORC.exe"
    Delete "$INSTDIR\libusb-1.0.dll"
    Delete "$INSTDIR\vcruntime140.dll"
    RMDir /r "$INSTDIR\moreme"
    RMDir /r "$INSTDIR\settings"
    RMDir /r "$INSTDIR\Licenses"
    Delete "$INSTDIR\Uninstall ORC.exe"
    RMDir "$INSTDIR"
SectionEnd
