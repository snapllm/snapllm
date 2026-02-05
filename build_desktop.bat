@echo off
REM ============================================================
REM SnapLLM Desktop App Build Script (Windows)
REM Builds the Tauri GUI application with auto-dependency install
REM ============================================================

setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "CI_MODE=0"
if /I "%CI%"=="true" set "CI_MODE=1"
if /I "%CI%"=="1" set "CI_MODE=1"
if /I "%GITHUB_ACTIONS%"=="true" set "CI_MODE=1"
if /I "%TF_BUILD%"=="True" set "CI_MODE=1"
if /I "%SNAPLLM_CI%"=="1" set "CI_MODE=1"

set "NPM_CONFIG_FUND=false"
set "NPM_CONFIG_AUDIT=false"

call :header

REM Ensure SnapLLM desktop app is not running (can lock build output)
tasklist /FI "IMAGENAME eq SnapLLM.exe" | find /I "SnapLLM.exe" >nul 2>&1
if not errorlevel 1 (
    echo [WARN] SnapLLM.exe is currently running and can lock build output.
    if "%CI_MODE%"=="1" (
        echo [INFO] Stopping SnapLLM.exe for CI build...
        taskkill /F /IM SnapLLM.exe >nul 2>&1
    ) else (
        echo [ERROR] Please close SnapLLM.exe and re-run this script.
        call :maybe_pause
        exit /b 1
    )
)

REM Check for Node.js
where node >nul 2>&1
if errorlevel 1 goto :node_missing

REM Check for Rust
where rustc >nul 2>&1
if errorlevel 1 goto :rust_missing

REM Display versions
for /f "tokens=1" %%v in ('node -v') do set "NODE_VERSION=%%v"
for /f "tokens=1,2" %%v in ('rustc --version') do set "RUST_VERSION=%%v %%w"
echo [OK] Node.js: %NODE_VERSION%
echo [OK] Rust: %RUST_VERSION%

REM Navigate to desktop-app directory
pushd "%SCRIPT_DIR%desktop-app" || exit /b 1

REM Install npm dependencies
echo.
echo [INFO] Checking npm dependencies...
if not exist "node_modules" goto :install_deps

goto :after_deps

:install_deps
echo [INFO] Installing npm dependencies (this may take a few minutes)...
if "%CI_MODE%"=="1" goto :npm_ci
call npm install
if errorlevel 1 goto :npm_failed
goto :after_deps

:npm_ci
call npm ci
if errorlevel 1 goto :npm_failed
goto :after_deps

:after_deps
REM Install Tauri CLI if needed
call npx tauri --version >nul 2>&1
if errorlevel 1 goto :install_tauri

goto :build

:install_tauri
echo [INFO] Installing Tauri CLI...
call npm install --save-dev @tauri-apps/cli
if errorlevel 1 goto :tauri_failed

goto :build

:build
echo.
echo [INFO] Building SnapLLM Desktop App (Release)...
echo        This will take 1-2 minutes on subsequent builds...
echo.

call npm run tauri:build
if errorlevel 1 goto :build_failed

popd
call :footer
call :maybe_pause
exit /b 0

:node_missing
echo [ERROR] Node.js not found.
if "%CI_MODE%"=="1" exit /b 1
echo.
echo Installing Node.js via winget...
winget install OpenJS.NodeJS.LTS -e --silent --accept-package-agreements --accept-source-agreements
if errorlevel 1 goto :node_install_failed
echo [OK] Node.js installed. Please restart this script.
call :maybe_pause
exit /b 0

:node_install_failed
echo [ERROR] Failed to install Node.js automatically.
echo         Please install manually: https://nodejs.org/
call :maybe_pause
exit /b 1

:rust_missing
echo [ERROR] Rust not found.
if "%CI_MODE%"=="1" exit /b 1
echo.
echo Installing Rust via winget...
winget install Rustlang.Rustup -e --silent --accept-package-agreements --accept-source-agreements
if errorlevel 1 goto :rust_install_failed
echo [OK] Rust installed. Please restart your terminal and run this script again.
call :maybe_pause
exit /b 0

:rust_install_failed
echo [ERROR] Failed to install Rust automatically.
echo         Please install manually: https://rustup.rs/
call :maybe_pause
exit /b 1

:npm_failed
echo [ERROR] npm install failed
popd
call :maybe_pause
exit /b 1

:tauri_failed
echo [ERROR] Failed to install Tauri CLI
popd
call :maybe_pause
exit /b 1

:build_failed
echo.
echo [ERROR] Build failed
popd
call :maybe_pause
exit /b 1

:header
 echo.
 echo ============================================================
 echo   SnapLLM Desktop App Builder v1.0.0
 echo ============================================================
 echo.
 exit /b 0

:footer
 echo.
 echo ============================================================
 echo   Build Complete!
 echo ============================================================
 echo.
 echo Output locations:
 echo.
 echo   NSIS Installer (recommended):
 echo     src-tauri\target\release\bundle\nsis\SnapLLM_1.0.0_x64-setup.exe
 echo.
 echo   MSI Installer:
 echo     src-tauri\target\release\bundle\msi\SnapLLM_1.0.0_x64_en-US.msi
 echo.
 echo   Portable Executable:
 echo     src-tauri\target\release\SnapLLM.exe
 echo.
 echo ============================================================
 echo   IMPORTANT: Running the Desktop App
 echo ============================================================
 echo.
 echo   1. First, start the backend server:
 echo      Start_Server.bat
 echo      (or set SNAPLLM_SERVER_EXE to a specific snapllm.exe path)
 echo.
 echo   2. Then launch the desktop app:
 echo      - Run the NSIS installer, OR
 echo      - Run src-tauri\target\release\SnapLLM.exe directly
 echo.
 echo   The desktop UI connects to http://localhost:6930
 echo.
 exit /b 0

:maybe_pause
if "%CI_MODE%"=="0" pause
exit /b 0
