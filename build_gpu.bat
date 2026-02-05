@echo off
REM =============================================================================
REM SnapLLM GPU Build Script (Windows + CUDA)
REM =============================================================================
REM Requirements:
REM   - Visual Studio 2022 with C++ workload
REM   - CUDA Toolkit 12.x
REM   - CMake 3.18+
REM   - Ninja (recommended) or MSBuild
REM =============================================================================

setlocal enabledelayedexpansion

echo.
echo ========================================
echo   SnapLLM GPU Build (Windows + CUDA)
echo ========================================
echo.

REM Check for CUDA
where nvcc >nul 2>&1
if errorlevel 1 (
    echo ERROR: CUDA not found in PATH.
    echo Please install CUDA Toolkit and add it to PATH.
    echo Download from: https://developer.nvidia.com/cuda-downloads
    exit /b 1
)

REM Show CUDA version
echo CUDA version:
nvcc --version | findstr "release"
echo.

REM Check for CMake
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake not found. Please install CMake 3.18+
    exit /b 1
)

REM Create build directory
if not exist build_gpu mkdir build_gpu
cd build_gpu

REM Configure with CMake
echo Configuring CMake with CUDA support...
echo.

cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DSNAPLLM_CUDA=ON ^
    -DSNAPLLM_AVX2=ON ^
    -DSNAPLLM_OPENMP=ON ^
    -DSNAPLLM_ENABLE_DIFFUSION=ON ^
    -DSNAPLLM_ENABLE_MULTIMODAL=ON ^
    -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo.
    echo ERROR: CMake configuration failed
    cd ..
    exit /b 1
)

REM Build
echo.
echo Building SnapLLM with GPU support...
echo This may take several minutes...
echo.

cmake --build . --config Release -j %NUMBER_OF_PROCESSORS%

if errorlevel 1 (
    echo.
    echo ERROR: Build failed
    cd ..
    exit /b 1
)

echo.
echo ========================================
echo   Build Complete!
echo ========================================
echo.
echo Output directory: build_gpu\bin\
echo.
echo Files:
if exist bin\snapllm.exe (
    echo   - snapllm.exe          (CLI executable^)
    dir /b bin\*.dll 2>nul | findstr /v "^$" && echo   - *.dll                (Runtime libraries^)
)
echo.
echo Usage:
echo   build_gpu\bin\snapllm.exe --help
echo.
echo Example:
echo   build_gpu\bin\snapllm.exe --load-model medicine "D:\Models\medicine-llm.Q8_0.gguf" --prompt "What is diabetes?"
echo.

cd ..
endlocal