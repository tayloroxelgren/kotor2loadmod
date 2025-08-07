@echo off
echo Building KOTOR 2 Proxy (32-bit)...

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
    pause
    exit /b 1
)

REM Compile the proxy DLL in 32-bit
echo Building dinput8.dll (32-bit)...
cl /LD /O2 /MD /DWIN32 /EHsc dinput8.cpp ^
    buffer.obj hook.obj trampoline.obj hde32.obj hde64.obj ^
    /I"minhook\include" ^
    /link /MACHINE:X86 /DEF:dinput8.def /OUT:dinput8.dll

set BUILD_RESULT=%ERRORLEVEL%

REM Cleanup
del *.obj 2>nul

if %BUILD_RESULT% == 0 (
    echo.
    echo SUCCESS! dinput8.dll created
    echo Copy to KOTOR 2 directory and test
) else (
    echo.
    echo BUILD FAILED!
)