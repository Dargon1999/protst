@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
)
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\app.res src\app.rc
if errorlevel 1 ( echo RC_FAIL & exit /b 1 )
cl /nologo /O2 /EHsc /MT /utf-8 /W3 /DUNICODE /D_UNICODE /DNOMINMAX ^
  src\main.cpp src\memcore.cpp src\cleanfiles.cpp src\settings.cpp src\procs.cpp src\ui\ui.cpp ^
  build\app.res ^
  /link advapi32.lib shlwapi.lib shell32.lib psapi.lib gdiplus.lib user32.lib gdi32.lib /SUBSYSTEM:WINDOWS /OUT:build\RamBoostPro.exe
if errorlevel 1 ( echo BUILD_FAIL & exit /b 1 )
echo BUILD_OK