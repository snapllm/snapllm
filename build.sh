#!/bin/bash
# =============================================================================
# SnapLLM Build Script (Linux/macOS)
# =============================================================================
# Usage:
#   ./build.sh          # Build with GPU if available, else CPU
#   ./build.sh gpu      # Force GPU build (requires CUDA)
#   ./build.sh cpu      # Force CPU-only build
#   ./build.sh clean    # Clean build directories
# =============================================================================

set -e

MODE="${1:-auto}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo ""
echo "========================================"
echo "  SnapLLM Build Script"
echo "========================================"
echo ""

# Handle clean command
if [ "$MODE" == "clean" ]; then
    echo "Cleaning build directories..."
    rm -rf build_gpu build_cpu build
    echo "Done."
    exit 0
fi

# Detect CUDA
CUDA_AVAILABLE=false
if command -v nvcc &> /dev/null; then
    CUDA_AVAILABLE=true
    CUDA_VERSION=$(nvcc --version | grep "release" | awk '{print $5}' | cut -d',' -f1)
    echo -e "${GREEN}CUDA detected: $CUDA_VERSION${NC}"
else
    echo -e "${YELLOW}CUDA not found${NC}"
fi

# Determine build mode
if [ "$MODE" == "auto" ]; then
    if [ "$CUDA_AVAILABLE" == "true" ]; then
        MODE="gpu"
    else
        MODE="cpu"
    fi
fi

if [ "$MODE" == "gpu" ] && [ "$CUDA_AVAILABLE" != "true" ]; then
    echo -e "${RED}ERROR: GPU mode requested but CUDA not found.${NC}"
    echo "Install CUDA Toolkit or use: ./build.sh cpu"
    exit 1
fi

# Set build directory and options
if [ "$MODE" == "gpu" ]; then
    BUILD_DIR="build_gpu"
    CMAKE_OPTS="-DSNAPLLM_CUDA=ON"
    echo -e "Build mode: ${GREEN}GPU (CUDA)${NC}"
else
    BUILD_DIR="build_cpu"
    CMAKE_OPTS="-DSNAPLLM_CUDA=OFF"
    echo -e "Build mode: ${YELLOW}CPU only${NC}"
fi

# Common options
CMAKE_OPTS="$CMAKE_OPTS -DSNAPLLM_AVX2=ON"
CMAKE_OPTS="$CMAKE_OPTS -DSNAPLLM_OPENMP=ON"
CMAKE_OPTS="$CMAKE_OPTS -DSNAPLLM_ENABLE_DIFFUSION=ON"
CMAKE_OPTS="$CMAKE_OPTS -DSNAPLLM_ENABLE_MULTIMODAL=ON"
CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_BUILD_TYPE=Release"

echo ""

# Check for submodules
if [ ! -f "external/llama.cpp/CMakeLists.txt" ]; then
    echo "Initializing git submodules..."
    git submodule update --init --recursive
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "Configuring CMake..."
cmake .. $CMAKE_OPTS

# Detect number of cores
if [ "$(uname)" == "Darwin" ]; then
    NPROC=$(sysctl -n hw.ncpu)
else
    NPROC=$(nproc)
fi

# Build
echo ""
echo "Building SnapLLM with $NPROC parallel jobs..."
cmake --build . --config Release -j$NPROC

echo ""
echo "========================================"
echo -e "  ${GREEN}Build Complete!${NC}"
echo "========================================"
echo ""
echo "Output directory: $BUILD_DIR/bin/"
echo ""
echo "Usage:"
echo "  ./$BUILD_DIR/bin/snapllm --help"
echo ""
echo "Example:"
echo "  ./$BUILD_DIR/bin/snapllm --load-model medicine /path/to/model.gguf --prompt \"What is diabetes?\""
echo ""