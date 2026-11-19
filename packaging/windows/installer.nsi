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
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\OCR Catalog.lnk"
  Delete "$DESKTOP\OCR Catalog.lnk"
  RMDir /r "$INSTDIR"
SectionEnd
