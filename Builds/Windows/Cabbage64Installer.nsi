; Script generated by the HM NIS Edit Script Wizard.

; HM NIS Edit Wizard helper defines
!define PRODUCT_NAME "Cabbage64"
!define PRODUCT_VERSION ""
!define PRODUCT_PUBLISHER "Cabbage Audio"
!define PRODUCT_WEB_SITE "http://www.cabbageaudio.com"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\Cabbage64.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"

; MUI 1.67 compatible ------
!include "MUI.nsh"
!include "EnvVarUpdate.nsh"

; MUI Settings
!define MUI_ABORTWARNING
!define MUI_ICON "cabbage.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Welcome page
!insertmacro MUI_PAGE_WELCOME
; License page
!insertmacro MUI_PAGE_LICENSE "..\..\GNUGeneralPublicLicense.txt"
; Directory page
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_COMPONENTS

; Instfiles page
!insertmacro MUI_PAGE_INSTFILES
; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\Cabbage64.exe"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_INSTFILES

; Language files
!insertmacro MUI_LANGUAGE "English"

; MUI end ------


Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "Output\Cabbage64Setup.exe"
InstallDir "$PROGRAMFILES64\Cabbage64"
ShowInstDetails show
ShowUnInstDetails show

Section "Core components" SEC01
  SetOutPath "$INSTDIR"
  ${EnvVarUpdate} $0 "PATH" "P" "HKLM" "$INSTDIR"                            ; Prepend
  ${EnvVarUpdate} $0 "CABBAGE_OPCODE_PATH64" "A" "HKLM" "$INSTDIR"                            ; Prepend
  SetOverwrite ifnewer
  File "build64\Cabbage64.exe"
  File "build64\CabbageStudio64.exe"

  CreateDirectory "$SMPROGRAMS\Cabbage64"
  
  CreateShortCut "$SMPROGRAMS\Cabbage64\Cabbage64.lnk" "$INSTDIR\Cabbage64.exe"
  CreateShortCut "$DESKTOP\Cabbage64.lnk" "$INSTDIR\Cabbage64.exe"

  File "build64\CabbagePluginEffect.dat"
  File "build64\CabbagePluginSynth.dat"
  File "C:\Users\rory\sourcecode\cabbageaudio\fmod_csoundL64.dll"
  File "C:\Users\rory\sourcecode\cabbageaudio\msvcp140d.dll"
  File "build\opcodes.txt"
  File "build\IntroScreen.csd"
  File "build\cabbageEarphones.png"
  File "build\cabbage.png"
  
  ;CLI for Csound
  File "csoundCabbage.exe"

  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\ampmidid.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\buchla.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\cellular.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\cs_date.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\csladspa.dll"
  ;File "..\..\....\\csound64csound\mingw64\csound-mingw64\csnd6.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\csound64.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\doppler.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\exciter.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\fareygen.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\fractalnoise.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\framebuffer.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\mixer.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\padsynth.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\platerev.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\osc.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\rtpa.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\ftsamplebank.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\rtwinmm.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\scansyn.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\serial.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\signalflowgraph.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\stdutil.dll"
  File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\system_call.dll"
  File "C:\msys64\mingw64\bin\libwinpthread-1.dll"
  ;docs and examples
  SetOutPath "$INSTDIR\Docs\_book\"
  File /r "..\..\Docs\_book\*"
  SetOutPath "$INSTDIR\Examples\"
  File /r "..\..\Examples\*"
  SetOutPath "$INSTDIR\csoundDocs\"
  File /r  "..\..\..\csoundDocs\*"

SectionEnd

Section /o "Python opcodes" SEC02
 File "..\..\..\..\csound64\csound\mingw64\csound-mingw64\py.dll"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC01} "Installs Cabbage, Csound and all core components"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC02} "Installs Csound python opcodes, requires Python 2.7 to be pre-installed"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section -AdditionalIcons
  WriteIniStr "$INSTDIR\${PRODUCT_NAME}.url" "InternetShortcut" "URL" "${PRODUCT_WEB_SITE}"
  CreateShortCut "$SMPROGRAMS\Cabbage64\Website.lnk" "$INSTDIR\${PRODUCT_NAME}.url"
  CreateShortCut "$SMPROGRAMS\Cabbage64\Uninstall.lnk" "$INSTDIR\uninst.exe"
SectionEnd

Section -Post
  WriteUninstaller "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\Cabbage64.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName" "$(^Name)"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninst.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\Cabbage64.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
SectionEnd


Function un.onUninstSuccess
  HideWindow
  MessageBox MB_ICONINFORMATION|MB_OK "$(^Name) was successfully removed from your computer."
FunctionEnd

Function un.onInit
  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "Are you sure you want to completely remove $(^Name) and all of its components?" IDYES +2
  Abort
FunctionEnd

Section Uninstall
  ${un.EnvVarUpdate} $0 "PATH" "R" "HKLM" "$INSTDIR"
  ${un.EnvVarUpdate} $0 "CABBAGE_OPCODE_PATH64" "R" "HKLM" "$INSTDIR"
  
  Delete "$INSTDIR\${PRODUCT_NAME}.url"
  Delete "$INSTDIR\uninst.exe"
  Delete "$INSTDIR\py.dll"
  Delete "$INSTDIR\ampmidid.dll"
  Delete "$INSTDIR\buchla.dll"
  Delete "$INSTDIR\cellular.dll"
  Delete "$INSTDIR\cs_date.dll"
  Delete "$INSTDIR\csladspa.dll"
  Delete "$INSTDIR\csnd6.dll"
  Delete "$INSTDIR\doppler.dll"
  Delete "$INSTDIR\exciter.dll"
  Delete "$INSTDIR\fareygen.dll"
  Delete "$INSTDIR\fractalnoise.dll"
  Delete "$INSTDIR\framebuffer.dll"
  Delete "$INSTDIR\ipmidi.dll"
  Delete "$INSTDIR\libportaudio-2.dll"
  Delete "$INSTDIR\mixer.dll"
  Delete "$INSTDIR\padsynth.dll"
  Delete "$INSTDIR\platerev.dll"
  Delete "$INSTDIR\py.dll"
  Delete "$INSTDIR\rtpa.dll"
  Delete "$INSTDIR\rtwinmm.dll"
  Delete "$INSTDIR\scansyn.dll"
  Delete "$INSTDIR\serial.dll"
  Delete "$INSTDIR\signalflowgraph.dll"
  Delete "$INSTDIR\stdutil.dll"
  Delete "$INSTDIR\system_call.dll"
  Delete "$INSTDIR\cabbage.png"
  Delete "$INSTDIR\cabbageEarphones.png"
  Delete "$INSTDIR\opcodes.txt"
  Delete "$INSTDIR\CabbagePluginSynth.dat"
  Delete "$INSTDIR\CabbagePluginEffect.dat"
  Delete "$INSTDIR\Cabbage64.exe"
  Delete "$INSTDIR\libwinpthread-1.dll"
  Delete "$INSTDIR\libgcc_s_dw2-1.dll"
  Delete "$INSTDIR\libstdc++-6.dll"
  Delete "$INSTDIR\py.dll"
  Delete "$INSTDIR\osc.dll"
  Delete "$INSTDIR\csound64.dll"
  Delete "$INSTDIR\libsndfile-1.dll"
  Delete "$INSTDIR\libwinpthread-1.dll"

  Delete "$SMPROGRAMS\Cabbage64\Uninstall.lnk"
  Delete "$SMPROGRAMS\Cabbage64\Website.lnk"
  Delete "$DESKTOP\Cabbage64.lnk"
  Delete "$SMPROGRAMS\Cabbage64\Cabbage64.lnk"

  Delete "$INSTDIR\opcodes.txt"
  Delete "$INSTDIR\IntroScreen.csd"
  Delete "$INSTDIR\cabbageEarphones.png"
  Delete "$INSTDIR\cabbage.png"
  
  ;CLI for Csound
  ;File "$INSTDIR\csoundCabbage.exe"
  ;File "$INSTDIR\CabbageStudio64.exe"

  SetOutPath $SMPROGRAMS
  RMDir /r "$INSTDIR\csoundDocs"
  RMDir /r "$INSTDIR\Examples"
  RMDir /r "$INSTDIR\Docs"
  RMDir "$SMPROGRAMS\Cabbage64"
  RMDir "$INSTDIR"

  DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
  SetAutoClose true
SectionEnd
