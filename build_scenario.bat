@echo off
REM Build wrapper for Git Bash / non-VC-console shells: sets up the 32-bit MSVC
REM environment, prints the active A/B scenario, then runs the normal build.bat.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if %ERRORLEVEL% neq 0 (
    echo vcvars32.bat failed - MSVC 2022 not found at the expected path.
    exit /b 1
)

findstr /R /C:"#define AB_SCENARIO_A 0" /C:"#define AB_SCENARIO_B 1" /C:"#define AB_SCENARIO  " dinput8.cpp >nul
echo ---- A/B scenario in dinput8.cpp ----
findstr /C:"#define AB_SCENARIO_A" /C:"#define AB_SCENARIO_B" /C:"#define AB_SCENARIO AB" dinput8.cpp

call build.bat
