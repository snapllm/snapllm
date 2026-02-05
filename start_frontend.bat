@echo off
REM =============================================================================
REM SnapLLM Desktop App Startup Script (Windows)
REM =============================================================================
REM Starts the React/Vite frontend development server
REM =============================================================================

setlocal enabledelayedexpansion

echo.
echo ========================================
echo   SnapLLM Desktop App
echo ========================================
echo.

REM Check for Node.js
where node >nul 2>&1
if errorlevel 1 (
    echo ERROR: Node.js not found. Please install Node.js 18+
    exit /b 1
)

REM Change to desktop-app directory
cd /d "%~dp0desktop-app"

REM Install dependencies if needed
if not exist node_modules (
    echo Installing dependencies...
    npm install
)

echo.
echo Starting SnapLLM Desktop App on http://localhost:5173
echo Make sure the API server is running on http://localhost:6930
echo Press Ctrl+C to stop
echo.

REM Start the dev server
npm run dev

endlocal
