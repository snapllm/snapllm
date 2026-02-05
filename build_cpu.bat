@echo off
REM =============================================================================
REM SnapLLM CPU-Only Build Script (Windows)
REM =============================================================================
REM For systems without NVIDIA GPU or for lightweight deployment
REM Uses AVX2 SIMD optimizations for best CPU performance
REM =============================================================================

setlocal enabledelayedexpansion

echo.
echo ========================================
echo   SnapLLM CPU-Only Build (Windows)
echo ========================================
echo.

REM Check for CMake
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake not found. Please install CMake 3.18+
    exit /b 1
)

REM Create build directory
if not exist build_cpu mkdir build_cpu
cd build_cpu

REM Configure without CUDA
echo Configuring CMake (CPU only with AVX2)...
echo.

cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DSNAPLLM_CUDA=OFF ^
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
echo Building SnapLLM (CPU only)...
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
echo Output directory: build_cpu\bin\
echo.
echo Usage:
echo   build_cpu\bin\snapllm.exe --help
echo.

cd ..
endlocal