@echo off
setlocal enabledelayedexpansion

:: ============================================================================
:: SnapLLM Release Packaging Script (Windows)
:: ============================================================================
:: This script creates a distributable release package with all necessary files.
::
:: Usage: package_release.bat [version]
:: Example: package_release.bat 1.0.0
:: ============================================================================

echo.
echo  ========================================
echo   SnapLLM Release Packager
echo  ========================================
echo.

:: Get version from argument or use default
set VERSION=%1
if "%VERSION%"=="" set VERSION=1.0.0

set RELEASE_NAME=snapllm-%VERSION%-windows-x64-cuda
set RELEASE_DIR=releases\%RELEASE_NAME%
set BUILD_DIR=build_gpu

:: Check if build exists
if not exist "%BUILD_DIR%\bin\snapllm.exe" (
    echo [ERROR] Build not found! Run build_gpu.bat first.
    echo.
    pause
    exit /b 1
)

echo [INFO] Creating release: %RELEASE_NAME%
echo.

:: Create release directory structure
echo [1/6] Creating directory structure...
if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%\bin"
mkdir "%RELEASE_DIR%\examples"
mkdir "%RELEASE_DIR%\docs"

:: Copy main executable
echo [2/6] Copying executable...
copy "%BUILD_DIR%\bin\snapllm.exe" "%RELEASE_DIR%\bin\" > nul

:: Copy CUDA DLLs
echo [3/6] Copying CUDA libraries...
copy "%BUILD_DIR%\bin\cublas64_12.dll" "%RELEASE_DIR%\bin\" > nul 2>&1
copy "%BUILD_DIR%\bin\cublasLt64_12.dll" "%RELEASE_DIR%\bin\" > nul 2>&1
copy "%BUILD_DIR%\bin\cudart64_12.dll" "%RELEASE_DIR%\bin\" > nul 2>&1

:: Copy documentation
echo [4/6] Copying documentation...
copy "README.md" "%RELEASE_DIR%\" > nul
copy "LICENSE" "%RELEASE_DIR%\" > nul
copy "QUICKSTART.md" "%RELEASE_DIR%\" > nul 2>&1
copy "docs\*.md" "%RELEASE_DIR%\docs\" > nul 2>&1

:: Copy examples
echo [5/6] Copying examples...
copy "examples\*.py" "%RELEASE_DIR%\examples\" > nul 2>&1
copy "examples\README.md" "%RELEASE_DIR%\examples\" > nul 2>&1

:: Create start scripts
echo [6/6] Creating start scripts...

:: Create run_server.bat
(
echo @echo off
echo echo Starting SnapLLM Server...
echo echo.
echo cd /d "%%~dp0"
echo bin\snapllm.exe --server --port 6930 %%*
echo pause
) > "%RELEASE_DIR%\run_server.bat"

:: Create run_server_with_model.bat
(
echo @echo off
echo echo.
echo echo  SnapLLM Server with Model
echo echo  =========================
echo echo.
echo set /p MODEL_PATH="Enter path to your .gguf model file: "
echo set /p MODEL_NAME="Enter a name for this model: "
echo echo.
echo echo Starting server with model: %%MODEL_NAME%%
echo cd /d "%%~dp0"
echo bin\snapllm.exe --server --port 6930 --load-model %%MODEL_NAME%% "%%MODEL_PATH%%"
echo pause
) > "%RELEASE_DIR%\run_server_with_model.bat"

:: Create VERSION file
echo %VERSION% > "%RELEASE_DIR%\VERSION"

:: Create zip archive
echo.
echo [INFO] Creating ZIP archive...
powershell -Command "Compress-Archive -Path '%RELEASE_DIR%\*' -DestinationPath 'releases\%RELEASE_NAME%.zip' -Force"

echo.
echo  ========================================
echo   Release Package Created Successfully!
echo  ========================================
echo.
echo   Location: releases\%RELEASE_NAME%\
echo   Archive:  releases\%RELEASE_NAME%.zip
echo.
echo   Contents:
echo   - bin\snapllm.exe (main executable)
echo   - bin\*.dll (CUDA libraries)
echo   - examples\ (Python examples)
echo   - docs\ (documentation)
echo   - run_server.bat (quick start)
echo   - README.md, LICENSE, QUICKSTART.md
echo.
echo   To use:
echo   1. Extract the ZIP
echo   2. Run run_server.bat
echo   3. Open http://localhost:6930
echo.

pause
