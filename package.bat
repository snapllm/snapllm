@echo off
REM =============================================================================
REM SnapLLM Deployment Package Script
REM =============================================================================
REM Creates a portable deployment package with all required files
REM =============================================================================

setlocal enabledelayedexpansion

echo.
echo ========================================
echo   SnapLLM Deployment Packager
echo ========================================
echo.

REM Check which build exists
if exist build_gpu\bin\snapllm.exe (
    set BUILD_TYPE=GPU
    set BUILD_DIR=build_gpu
    set PACKAGE_DIR=dist\SnapLLM-GPU
) else if exist build_cpu\bin\snapllm.exe (
    set BUILD_TYPE=CPU
    set BUILD_DIR=build_cpu
    set PACKAGE_DIR=dist\SnapLLM-CPU
) else (
    echo ERROR: No build found.
    echo Please run build_gpu.bat or build_cpu.bat first.
    exit /b 1
)

echo Found %BUILD_TYPE% build in %BUILD_DIR%
echo.

REM Create package directory structure
if exist %PACKAGE_DIR% (
    echo Removing old package...
    rmdir /s /q %PACKAGE_DIR%
)
mkdir %PACKAGE_DIR%
mkdir %PACKAGE_DIR%\bin

REM Copy executable
echo Copying snapllm.exe...
copy %BUILD_DIR%\bin\snapllm.exe %PACKAGE_DIR%\bin\ >nul

REM Copy DLLs
echo Copying DLLs...
if exist %BUILD_DIR%\bin\*.dll (
    copy %BUILD_DIR%\bin\*.dll %PACKAGE_DIR%\bin\ >nul
)

REM Copy CUDA DLLs if GPU build
if "%BUILD_TYPE%"=="GPU" (
    echo Copying CUDA runtime DLLs...
    for %%D in (cudart64_12.dll cublas64_12.dll cublasLt64_12.dll) do (
        if exist "%CUDA_PATH%\bin\%%D" (
            copy "%CUDA_PATH%\bin\%%D" %PACKAGE_DIR%\bin\ >nul
        )
    )
)

REM Copy README and LICENSE
if exist README.md copy README.md %PACKAGE_DIR%\ >nul
if exist LICENSE copy LICENSE %PACKAGE_DIR%\ >nul

REM Create launcher script
echo Creating launcher script...
(
echo @echo off
echo REM SnapLLM Launcher
echo set PATH=%%~dp0bin;%%PATH%%
echo %%~dp0bin\snapllm.exe %%*
) > %PACKAGE_DIR%\snapllm.bat

REM Create quick start guide
echo Creating quick start guide...
(
echo ================================================================================
echo  SnapLLM - Ultra-fast Multi-Model AI Inference Engine
echo ================================================================================
echo.
echo Build Type: %BUILD_TYPE%
echo.
echo QUICK START
echo -----------
echo   1. Place your .gguf models in a folder ^(e.g., D:\Models\^)
echo   2. Run: snapllm.bat --load-model mymodel D:\Models\model.gguf --prompt "Hello"
echo.
echo FEATURES
echo --------
echo   * Sub-millisecond model switching ^(<1ms^)
echo   * GPU acceleration with CUDA
echo   * Multi-model support with shared cache
echo   * Image generation ^(Stable Diffusion^)
echo   * Multimodal vision ^(Gemma3, Qwen-VL^)
echo.
echo EXAMPLES
echo --------
echo   # Text generation
echo   snapllm.bat --load-model llama D:\Models\llama-8b.gguf --prompt "What is AI?"
echo.
echo   # Multi-model switching
echo   snapllm.bat --load-model med D:\Models\medicine.gguf ^
echo               --load-model legal D:\Models\legal.gguf ^
echo               --multi-model-test
echo.
echo   # Image generation
echo   snapllm.bat --load-diffusion sd15 D:\Models\sd15.gguf ^
echo               --generate-image "A sunset" --output sunset.png
echo.
echo   # Vision analysis
echo   snapllm.bat --multimodal D:\Models\gemma3.gguf D:\Models\mmproj.gguf ^
echo               --image photo.jpg --vision-prompt "Describe this image"
echo.
echo For full usage: snapllm.bat --help
echo.
) > %PACKAGE_DIR%\QUICKSTART.txt

echo.
echo ========================================
echo   Package Complete!
echo ========================================
echo.
echo Package: %PACKAGE_DIR%
echo.
echo Contents:
echo   bin\
dir /b %PACKAGE_DIR%\bin 2>nul | findstr /v "^$"
echo.
echo   Other files:
dir /b %PACKAGE_DIR%\*.* 2>nul | findstr /v "^$"
echo.
echo To distribute:
echo   Copy the entire %PACKAGE_DIR% folder to target machine.
echo.

endlocal