; Installateur Windows de RAW 2 (ABRASION) : plugin VST3, RAW2Preview, instruments et presets.
Target amd64-unicode
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "x64.nsh"

Name "RAW 2"
Caption "RAW 2 v9.1 - installation"
OutFile "RAW2-Setup-v9.1.exe"
InstallDir "$PROGRAMFILES64\ABRASION\RAW 2"
RequestExecutionLevel admin
BrandingText "ABRASION · RAW 2 v9.1"
VIProductVersion "9.1.0.0"
VIAddVersionKey "ProductName" "RAW 2"
VIAddVersionKey "CompanyName" "ABRASION"
VIAddVersionKey "FileDescription" "Installateur de RAW 2"
VIAddVersionKey "FileVersion" "9.1"
VIAddVersionKey "LegalCopyright" "ABRASION"

!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "RAW 2 v9.1"
!define MUI_WELCOMEPAGE_TEXT "Ce programme installe RAW 2, le synthé VST3 d'ABRASION :$\r$\n$\r$\n  - le plugin VST3 (C:\Program Files\Common Files\VST3)$\r$\n  - RAW 2 sans DAW (menu Démarrer)$\r$\n  - 23 vrais instruments et 34 presets dans Documents\Raw 2$\r$\n$\r$\nFerme ta DAW avant de continuer."
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\LISEZ-MOI.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Lire le mode d'emploi (faire des presets et des packs)"
!define MUI_FINISHPAGE_RUN "$INSTDIR\RAW2Preview.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Essayer RAW 2 sans DAW"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "French"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "RAW 2 demande Windows 64 bits."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "RAW 2 (plugin VST3 + appli sans DAW)" SecCore
  SectionIn RO
  SetShellVarContext all
  ; plugin VST3 : un dossier, remplacé en entier (une ancienne version ne reste pas mélangée)
  RMDir /r "$COMMONFILES64\VST3\RAW2.vst3"
  SetOutPath "$COMMONFILES64\VST3"
  File /r "stage\vst3\RAW2.vst3"
  SetOutPath "$INSTDIR"
  File "stage\app\RAW2Preview.exe"
  File "stage\LISEZ-MOI.txt"
  CreateDirectory "$SMPROGRAMS\RAW 2"
  CreateShortcut "$SMPROGRAMS\RAW 2\RAW 2 (sans DAW).lnk" "$INSTDIR\RAW2Preview.exe"
  CreateShortcut "$SMPROGRAMS\RAW 2\Mode d'emploi RAW 2.lnk" "$INSTDIR\LISEZ-MOI.txt"
  CreateShortcut "$SMPROGRAMS\RAW 2\Désinstaller RAW 2.lnk" "$INSTDIR\Desinstaller.exe"
  WriteUninstaller "$INSTDIR\Desinstaller.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RAW2" "DisplayName" "RAW 2 (ABRASION)"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RAW2" "DisplayVersion" "9.1"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RAW2" "Publisher" "ABRASION"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RAW2" "DisplayIcon" "$INSTDIR\RAW2Preview.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RAW2" "UninstallString" '"$INSTDIR\Desinstaller.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RAW2" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RAW2" "NoRepair" 1
  ; dossiers de l'utilisateur (Documents\Raw 2) : ses presets ne sont jamais touchés
  SetShellVarContext current
  CreateDirectory "$DOCUMENTS\Raw 2\Mes presets"
SectionEnd

Section "Instruments réels et pack RAW 2 · INSTRUMENTS (34 presets)" SecInst
  SetShellVarContext current
  SetOutPath "$DOCUMENTS\Raw 2"
  ; l'ancien pack (presets en accords) est remplacé par le nouveau
  RMDir /r "$DOCUMENTS\Raw 2\RAW 2 · INSTRUMENTS"
  File /r "stage\docs\Instruments"
  File /r "stage\docs\RAW 2 · INSTRUMENTS"
SectionEnd

Section "Grand Piano (90 Mo)" SecPiano
  SetShellVarContext current
  SetOutPath "$DOCUMENTS\Raw 2"
  File /r "stage\docs_piano\Instruments"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCore} "Le plugin VST3 (pour la DAW) et RAW 2 sans DAW. Obligatoire."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecInst} "Wurlitzer, CP80, cuivres, cordes, sax, basses G-funk, orgues… dans Documents\Raw 2\Instruments, et les 34 presets qui s'en servent."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecPiano} "Le piano à queue (Documents\Raw 2\Instruments\Grand Piano)."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all
  RMDir /r "$COMMONFILES64\VST3\RAW2.vst3"
  Delete "$INSTDIR\RAW2Preview.exe"
  Delete "$INSTDIR\LISEZ-MOI.txt"
  Delete "$INSTDIR\Desinstaller.exe"
  RMDir "$INSTDIR"
  RMDir "$PROGRAMFILES64\ABRASION"
  RMDir /r "$SMPROGRAMS\RAW 2"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\RAW2"
  ; Documents\Raw 2 (presets, packs, instruments) est gardé
SectionEnd
