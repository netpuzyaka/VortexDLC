@echo off
chcp 65001 >nul
color 0A
echo ========================================
echo   Building Vortex DLL + BypassAgent
echo ========================================

set "IMGUI_DIR=D:\Projects\Macros\C++\IceCubeRustTool\IceCubeRustTool\imgui-master"
set "PROJECT_DIR=%~dp0"
set "SRC_DIR=%PROJECT_DIR%src"

if not exist "%IMGUI_DIR%\imgui.h" (
    echo [ERROR] imgui.h not found at %IMGUI_DIR%
    pause & exit /b 1
)

set "MSVC_DIR="
for /d %%i in ("D:\VisualStudio\VC\Tools\MSVC\*") do set "MSVC_DIR=%%i"
if "%MSVC_DIR%"=="" ( echo [ERROR] MSVC not found! & pause & exit /b 1 )

set "SDK_INC="
for /d %%i in ("C:\Program Files (x86)\Windows Kits\10\Include\*") do set "SDK_INC=%%i"
set "SDK_LIB="
for /d %%i in ("C:\Program Files (x86)\Windows Kits\10\Lib\*") do set "SDK_LIB=%%i"

set "PATH=%MSVC_DIR%\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%MSVC_DIR%\include;%SDK_INC%\ucrt;%SDK_INC%\um;%SDK_INC%\shared;%SDK_INC%\winrt;%IMGUI_DIR%;%IMGUI_DIR%\backends;%SRC_DIR%"
set "LIB=%MSVC_DIR%\lib\x64;%SDK_LIB%\ucrt\x64;%SDK_LIB%\um\x64"

where javac >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARNING] javac not found. Java agent will NOT be built.
    set "BUILD_AGENT=0"
) else (
    set "BUILD_AGENT=1"
)

echo.
echo [1/2] Compiling vortex.dll...
cl /LD /EHsc /O2 /std:c++17 /MD /W3 /DUNICODE /D_UNICODE ^
    "%SRC_DIR%\main.cpp" ^
    "%IMGUI_DIR%\imgui.cpp" ^
    "%IMGUI_DIR%\imgui_draw.cpp" ^
    "%IMGUI_DIR%\imgui_tables.cpp" ^
    "%IMGUI_DIR%\imgui_widgets.cpp" ^
    "%IMGUI_DIR%\backends\imgui_impl_dx11.cpp" ^
    "%IMGUI_DIR%\backends\imgui_impl_win32.cpp" ^
    /link /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib dwmapi.lib winmm.lib user32.lib gdi32.lib ole32.lib shell32.lib psapi.lib advapi32.lib gdiplus.lib ^
    /OUT:"%PROJECT_DIR%vortex.dll"

if %errorlevel% neq 0 ( echo [ERROR] DLL compilation failed! & pause & exit /b 1 )
echo [OK] vortex.dll built.

echo.
echo [2/2] Building BypassAgent.jar...
if "%BUILD_AGENT%"=="0" (
    echo [SKIP] Java not available.
) else if not exist "%SRC_DIR%\BypassAgent.java" (
    echo [SKIP] BypassAgent.java not found.
) else (
    cd /d "%SRC_DIR%"
    javac -encoding UTF-8 BypassAgent.java
    if %errorlevel% neq 0 (
        echo [ERROR] BypassAgent.java compilation failed!
        cd /d "%PROJECT_DIR%"
        pause & exit /b 1
    )
    echo Manifest-Version: 1.0 > manifest.txt
    echo Premain-Class: BypassAgent >> manifest.txt
    echo Agent-Class: BypassAgent >> manifest.txt
    echo Can-Retransform-Classes: true >> manifest.txt
    echo Can-Redefine-Classes: true >> manifest.txt
    jar cfm "%PROJECT_DIR%BypassAgent.jar" manifest.txt BypassAgent.class
    if %errorlevel% neq 0 (
        echo [ERROR] jar creation failed!
        cd /d "%PROJECT_DIR%"
        pause & exit /b 1
    )
    del manifest.txt BypassAgent.class 2>nul
    cd /d "%PROJECT_DIR%"
    echo [OK] BypassAgent.jar built.
)

echo.
echo ========================================
echo   BUILD COMPLETE!
echo   - vortex.dll         (internal DLL)
if exist "%PROJECT_DIR%BypassAgent.jar" echo   - BypassAgent.jar    (Java agent)
echo.
echo   Inject vortex.dll into javaw.exe
echo   Attach BypassAgent.jar via jattach
echo ========================================
pause