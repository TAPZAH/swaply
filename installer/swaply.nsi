Unicode true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"

!define APP_NAME "Swaply"
!define APP_VERSION "0.987 beta"
!define APP_VERSION_FILE "0.987-beta"
!define APP_PUBLISHER "Tap3ah"
!define APP_REG "Software\${APP_NAME}"
!define APP_EXE "Swaply.exe"
!define APP_WINDOW_CLASS "Swaply.HiddenTrayWindow"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "..\dist\${APP_NAME}-${APP_VERSION_FILE}-setup.exe"
InstallDir "$LOCALAPPDATA\${APP_NAME}"
InstallDirRegKey HKCU "${APP_REG}" "InstallDir"
RequestExecutionLevel user
ShowInstDetails show
ShowUninstDetails show

!define MUI_ABORTWARNING
!define MUI_ICON "..\assets\icon.ico"
!define MUI_UNICON "..\assets\icon.ico"

!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "English"

LangString NAME_SecApp ${LANG_RUSSIAN} "Программа"
LangString NAME_SecApp ${LANG_ENGLISH} "Program"
LangString NAME_SecDesktop ${LANG_RUSSIAN} "Ярлык на рабочем столе"
LangString NAME_SecDesktop ${LANG_ENGLISH} "Desktop shortcut"
LangString DESC_SecApp ${LANG_RUSSIAN} "Файлы Swaply, словари и документация."
LangString DESC_SecApp ${LANG_ENGLISH} "Swaply files, dictionaries and documentation."
LangString DESC_SecDesktop ${LANG_RUSSIAN} "Создать ярлык Swaply на рабочем столе."
LangString DESC_SecDesktop ${LANG_ENGLISH} "Create a Swaply shortcut on the desktop."

VIProductVersion "0.987.0.0"
VIAddVersionKey /LANG=1049 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1049 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1049 "CompanyName" "${APP_PUBLISHER}"
VIAddVersionKey /LANG=1049 "FileDescription" "Установщик ${APP_NAME}"
VIAddVersionKey /LANG=1049 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1049 "LegalCopyright" "Copyright (C) 2026 Tap3ah"
VIAddVersionKey /LANG=1049 "Comments" "GNU GPL v3 or later"

; Politely close a running instance (its hidden window handles WM_CLOSE), then
; force-terminate as a last resort so the files can be replaced.
Function CloseRunningApp
  StrCpy $1 0
  CloseAppLoop:
    FindWindow $0 "${APP_WINDOW_CLASS}" ""
    StrCmp $0 0 CloseAppDone
    SendMessage $0 ${WM_CLOSE} 0 0
    Sleep 200
    IntOp $1 $1 + 1
    IntCmp $1 15 CloseAppKill CloseAppLoop CloseAppKill
  CloseAppKill:
    nsExec::ExecToLog 'taskkill /IM "${APP_EXE}" /F'
    Sleep 500
  CloseAppDone:
FunctionEnd

Function un.CloseRunningApp
  StrCpy $1 0
  unCloseAppLoop:
    FindWindow $0 "${APP_WINDOW_CLASS}" ""
    StrCmp $0 0 unCloseAppDone
    SendMessage $0 ${WM_CLOSE} 0 0
    Sleep 200
    IntOp $1 $1 + 1
    IntCmp $1 15 unCloseAppKill unCloseAppLoop unCloseAppKill
  unCloseAppKill:
    nsExec::ExecToLog 'taskkill /IM "${APP_EXE}" /F'
    Sleep 500
  unCloseAppDone:
FunctionEnd

Section "$(NAME_SecApp)" SecApp
  SectionIn RO
  Call CloseRunningApp

  SetOutPath "$INSTDIR"
  File "..\build\Release\${APP_EXE}"
  File "..\assets\default_config.json"
  File "..\LICENSE"
  File "..\README.md"
  File "..\THIRD_PARTY.md"
  File "..\AUTHORS"
  SetOutPath "$INSTDIR\dict"
  File "..\assets\dict\en.txt"
  File "..\assets\dict\ru.txt"
  File "..\assets\dict\ATTRIBUTION.txt"

  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\Удалить ${APP_NAME}.lnk" "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "${APP_REG}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayName" "${APP_NAME} ${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "EstimatedSize" "$0"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "$(NAME_SecDesktop)" SecDesktop
  CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} $(DESC_SecApp)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  Call un.CloseRunningApp

  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\default_config.json"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\THIRD_PARTY.md"
  Delete "$INSTDIR\AUTHORS"
  Delete "$INSTDIR\dict\en.txt"
  Delete "$INSTDIR\dict\ru.txt"
  Delete "$INSTDIR\dict\ATTRIBUTION.txt"
  RMDir "$INSTDIR\dict"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Удалить ${APP_NAME}.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"
  Delete "$DESKTOP\${APP_NAME}.lnk"

  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
  DeleteRegKey HKCU "${APP_REG}"
SectionEnd
