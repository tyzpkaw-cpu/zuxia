@echo off
setlocal
rem Compiles the installer binaries. Expects an MSVC 32-bit environment to be
rem active already -- make-setup.ps1 calls vcvars32.bat before invoking this,
rem and also generates payload.cab, payload_info.h and setup.u16.rc.
rem
rem 32-bit so that one binary covers x86 and x64 Windows, and /MT so it needs
rem no redistributable.
pushd "%~dp0"

if not exist payload.cab      echo ERROR: payload.cab missing - run make-setup.ps1 & goto :fail
if not exist payload_info.h   echo ERROR: payload_info.h missing - run make-setup.ps1 & goto :fail
if not exist setup.u16.rc     echo ERROR: setup.u16.rc missing - run make-setup.ps1 & goto :fail

set CLFLAGS=/nologo /W4 /EHsc /std:c++17 /MT /O2 /utf-8 /DUNICODE /D_UNICODE
set UAC=/MANIFESTUAC:"level='requireAdministrator' uiAccess='false'"
set LINKCOMMON=/SUBSYSTEM:WINDOWS,6.00 /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:app.manifest %UAC%
set LIBS=advapi32.lib shell32.lib user32.lib gdi32.lib ole32.lib

if not exist obj_uninst mkdir obj_uninst
if not exist obj_setup  mkdir obj_setup

echo [1/4] uninstaller resources
rc /nologo /fo uninst.res uninst.rc || goto :fail

echo [2/4] ZuxiaUninstall.exe
cl %CLFLAGS% /Fo:obj_uninst\ uninst.cpp uninst.res ^
   /link %LINKCOMMON% /OUT:ZuxiaUninstall.exe %LIBS% || goto :fail

echo [3/4] setup resources (embeds payload.cab + ZuxiaUninstall.exe)
rc /nologo /fo setup.res setup.u16.rc || goto :fail

echo [4/4] ZuxiaSetup.exe
cl %CLFLAGS% /Fo:obj_setup\ setup.cpp setup.res ^
   /link %LINKCOMMON% /OUT:ZuxiaSetup.exe %LIBS% setupapi.lib comctl32.lib || goto :fail

popd
echo BUILD OK
exit /b 0

:fail
popd
echo BUILD FAILED
exit /b 1
