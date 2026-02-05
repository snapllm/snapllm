@echo off
REM ============================================================
REM SnapLLM Server Launcher
REM Starts the backend server and opens the Desktop UI
REM Developed by AroorA AI Lab
REM ============================================================

setlocal enabledelayedexpansion

title SnapLLM Server

REM Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"

REM Configuration
set "PORT=6930"
set "HOST=127.0.0.1"
set "EXE_PATH="

echo.
echo   ============================================================
echo     SnapLLM Server Launcher
echo     Developed by AroorA AI Lab
echo   ============================================================
echo.

REM Check for explicit override
if defined SNAPLLM_SERVER_EXE (
    if exist "%SNAPLLM_SERVER_EXE%" (
        set "EXE_PATH=%SNAPLLM_SERVER_EXE%"
        echo [OK] Using SNAPLLM_SERVER_EXE
    )
)

REM Prefer newest Codex GPU build if available
if not defined EXE_PATH (
    pushd "%SCRIPT_DIR%" >nul 2>&1
    for /f %%d in ('dir /b /ad /o-d build_codex_gpu* 2^>nul') do (
        if not defined EXE_PATH if exist "%%d\\bin\\snapllm.exe" (
            set "EXE_PATH=%SCRIPT_DIR%%%d\\bin\\snapllm.exe"
            echo [OK] Found Codex GPU build: %%d
        )
    )
    popd >nul 2>&1
)

REM Check if snapllm.exe exists in various locations
if not defined EXE_PATH if exist "%SCRIPT_DIR%build_gpu\bin\snapllm.exe" (
    set "EXE_PATH=%SCRIPT_DIR%build_gpu\bin\snapllm.exe"
    echo [OK] Found GPU build
)
if not defined EXE_PATH if exist "%SCRIPT_DIR%snapllm.exe" (
    set "EXE_PATH=%SCRIPT_DIR%snapllm.exe"
    echo [OK] Found snapllm.exe
)
if not defined EXE_PATH if exist "%SCRIPT_DIR%bin\snapllm.exe" (
    set "EXE_PATH=%SCRIPT_DIR%bin\snapllm.exe"
    echo [OK] Found bin\snapllm.exe
)
if not defined EXE_PATH if exist "%SCRIPT_DIR%build_cpu\bin\snapllm.exe" (
    set "EXE_PATH=%SCRIPT_DIR%build_cpu\bin\snapllm.exe"
    echo [OK] Found CPU build
)
if not defined EXE_PATH (
    echo [ERROR] snapllm.exe not found!
    echo.
    echo         Expected locations:
    echo           - build_codex_gpu*\bin\snapllm.exe
    echo           - build_gpu\bin\snapllm.exe
    echo           - snapllm.exe
    echo           - bin\snapllm.exe
    echo.
    echo         Please build the project first using build_gpu.bat
    pause
    exit /b 1
)

echo [INFO] Executable: %EXE_PATH%
echo [INFO] Server URL: http://localhost:%PORT%
echo.

REM Check if Desktop UI exists
set "UI_PATH="
if exist "%SCRIPT_DIR%desktop-app\src-tauri\target\release\SnapLLM.exe" (
    set "UI_PATH=%SCRIPT_DIR%desktop-app\src-tauri\target\release\SnapLLM.exe"
    echo [OK] Desktop UI found
)

REM Launch UI or browser after server starts (3 second delay)
if defined UI_PATH (
    echo [INFO] Launching Desktop UI in 3 seconds...
    start "" cmd /c "timeout /t 3 /nobreak >nul && start "" "%UI_PATH%""
) else (
    echo [INFO] Desktop UI not built. Opening browser instead...
    echo        (Run build_desktop.bat to build the Desktop UI)
    start "" cmd /c "timeout /t 3 /nobreak >nul && start http://localhost:%PORT%"
)

echo.
echo   ============================================================
echo     Starting SnapLLM Server...
echo   ============================================================
echo.
echo     API Endpoints:
echo       Health:     http://localhost:%PORT%/health
echo       Models:     http://localhost:%PORT%/v1/models
echo       Chat:       http://localhost:%PORT%/v1/chat/completions
echo       Messages:   http://localhost:%PORT%/v1/messages
echo       Config:     http://localhost:%PORT%/api/v1/config
echo.
echo     Press Ctrl+C to stop the server
echo   ============================================================
echo.

REM Start the server (this blocks until server stops)
"%EXE_PATH%" --server --host %HOST% --port %PORT%

echo.
echo [INFO] Server stopped.
pause
