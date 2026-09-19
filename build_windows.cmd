@echo off
setlocal
cd /d "%~dp0src"
where cl >nul 2>nul
if errorlevel 1 (
  echo Open "x64 Native Tools Command Prompt for VS" and run this file.
  echo Visual Studio Build Tools: Desktop development with C++ is required.
  pause
  exit /b 1
)
rc /nologo /fo app.res app.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /MT main.cpp app.res /Fe:..\EdgeCatTimer.exe /link /SUBSYSTEM:WINDOWS gdiplus.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib user32.lib gdi32.lib advapi32.lib
if errorlevel 1 exit /b 1
echo Built EdgeCatTimer.exe
endlocal
