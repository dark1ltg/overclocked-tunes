!ifndef STAGE
  !error "Pass /DSTAGE=path"
!endif
!ifndef OUT
  !define OUT "."
!endif

Name "OCR Catalog"
OutFile "${OUT}\OCRCatalog-setup.exe"
InstallDir "$PROGRAMFILES64\OCR Catalog"
RequestExecutionLevel admin
SetCompressor lzma

Page directory
Page instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  CreateShortCut "$SMPROGRAMS\OCR Catalog.lnk" "$INSTDIR\OCRCatalog.exe"
  CreateShortCut "$DESKTOP\OCR Catalog.lnk" "$INSTDIR\OCRCatalog.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OCRCatalog" "DisplayName" "OCR Catalog"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OCRCatalog" "Publisher" "OCR Catalog"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OCRCatalog" "DisplayVersion" "1.0.2"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OCRCatalog" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OCRCatalog" "DisplayIcon" "$INSTDIR\OCRCatalog.exe"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OCRCatalog" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OCRCatalog" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\OCR Catalog.lnk"
  Delete "$DESKTOP\OCR Catalog.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OCRCatalog"
  IfFileExists "$INSTDIR\OCRCatalog.exe" 0 skip_rm
  IfFileExists "$INSTDIR\Uninstall.exe" 0 skip_rm
  RMDir /r "$INSTDIR"
skip_rm:
SectionEnd
