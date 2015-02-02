!define WINLOGON_KEY "SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon"

!macro installGina
    File "cred-connectors/gina/flexVDIGina.dll"
    Rename /REBOOTOK $INSTDIR\flexVDIGina.dll $SYSDIR\flexVDIGina.dll
    ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "OldGinaDLL"
    IfErrors 0 oldgina_present
        ReadRegStr $0 HKLM "${WINLOGON_KEY}" "GinaDLL"
        WriteRegStr HKLM "${UNINSTALL_KEY}" "OldGinaDLL" "$0"
    oldgina_present:
    WriteRegStr HKLM "${WINLOGON_KEY}" "GinaDLL" "flexVDIGina.dll"
    DetailPrint "GINA hook installed"
!macroend

!macro uninstallGina
    ReadRegStr $0 HKLM "${UNINSTALL_KEY}" "OldGinaDLL"
    WriteRegStr HKLM "${WINLOGON_KEY}" "GinaDLL" "$0"
    Delete /REBOOTOK $SYSDIR\flexVDIGina.dll
    DetailPrint "GINA hook removed"
!macroend