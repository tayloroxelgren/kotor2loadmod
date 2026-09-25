@echo off
setlocal
REM Usage: build.bat [-release]
REM   (no argument)  development build: logging and diagnostics on
REM   -release       shipping build: /DLOGGING_ENABLED=0, no log file, no diagnostics
REM Run it from any shell; it sets up the 32-bit MSVC environment itself if needed.
cd /d "%~dp0"

set DEFS=
set MODE=development
:parse
if "%~1"=="" goto parsed
if /i "%~1"=="-release" (set DEFS=/DLOGGING_ENABLED=0& set MODE=release& shift & goto parse)
if /i "%~1"=="--release" (set DEFS=/DLOGGING_ENABLED=0& set MODE=release& shift & goto parse)
if /i "%~1"=="/release" (set DEFS=/DLOGGING_ENABLED=0& set MODE=release& shift & goto parse)
echo Unknown argument: %~1
echo Usage: build.bat [-release]
exit /b 1
:parsed

REM vcvars32 sets VSCMD_ARG_TGT_ARCH; anything other than x86 (or no environment at
REM all) means the 32-bit compiler is not set up yet.
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
    if errorlevel 1 (
        echo vcvars32.bat failed - MSVC 2022 not found at the expected path.
        echo Edit the vcvars32 line in build.bat, or run from an x86 Native Tools Command Prompt.
        exit /b 1
    )
)

echo ---- A/B scenario in dinput8.cpp ----
findstr /C:"#define AB_SCENARIO_A" /C:"#define AB_SCENARIO_B" /C:"#define AB_SCENARIO AB" dinput8.cpp
echo Building KOTOR 2 Proxy (32-bit, %MODE%)...

REM Compile MinHook in 32-bit
echo Compiling MinHook (32-bit)...
cl /c /O2 /MD /DNDEBUG /DWIN32 /I"minhook\include" ^
    minhook\src\buffer.c ^
    minhook\src\hook.c ^
    minhook\src\trampoline.c ^
    minhook\src\hde\hde32.c ^
    minhook\src\hde\hde64.c

if %ERRORLEVEL% neq 0 (
    echo MinHook compilation failed!
    del *.obj 2>nul
    exit /b 1
)

REM Compile the proxy DLL in 32-bit
echo Building dinput8.dll (32-bit, %MODE%)...
cl /LD /O2 /MD /DWIN32 /EHsc %DEFS% dinput8.cpp ^
    buffer.obj hook.obj trampoline.obj hde32.obj hde64.obj ^
    /I"minhook\include" ^
    /link /MACHINE:X86 /DEF:dinput8.def /OUT:dinput8.dll user32.lib

set BUILD_RESULT=%ERRORLEVEL%

REM Cleanup
del *.obj 2>nul

if %BUILD_RESULT% == 0 (
    echo.
    echo SUCCESS! dinput8.dll created ^(%MODE%^)
    echo Copy to KOTOR 2 directory and test
    exit /b 0
) else (
    echo.
    echo BUILD FAILED!
    exit /b 1
)
