#!/bin/bash

# ============================================================================
# SnapLLM Release Packaging Script (Linux)
# ============================================================================
# This script creates a distributable release package with all necessary files.
#
# Usage: ./package_release.sh [version]
# Example: ./package_release.sh 1.0.0
# ============================================================================

set -e

echo ""
echo "========================================"
echo " SnapLLM Release Packager"
echo "========================================"
echo ""

# Get version from argument or use default
VERSION="${1:-1.0.0}"
RELEASE_NAME="snapllm-${VERSION}-linux-x64-cuda"
RELEASE_DIR="releases/${RELEASE_NAME}"
BUILD_DIR="build_gpu"

# Check if build exists
if [ ! -f "${BUILD_DIR}/bin/snapllm" ]; then
    echo "[ERROR] Build not found! Run ./build.sh --cuda first."
    exit 1
fi

echo "[INFO] Creating release: ${RELEASE_NAME}"
echo ""

# Create release directory structure
echo "[1/6] Creating directory structure..."
rm -rf "${RELEASE_DIR}"
mkdir -p "${RELEASE_DIR}/bin"
mkdir -p "${RELEASE_DIR}/examples"
mkdir -p "${RELEASE_DIR}/docs"
mkdir -p "${RELEASE_DIR}/lib"

# Copy main executable
echo "[2/6] Copying executable..."
cp "${BUILD_DIR}/bin/snapllm" "${RELEASE_DIR}/bin/"
chmod +x "${RELEASE_DIR}/bin/snapllm"

# Copy shared libraries if they exist
echo "[3/6] Copying shared libraries..."
cp "${BUILD_DIR}/lib/"*.so* "${RELEASE_DIR}/lib/" 2>/dev/null || true

# Copy documentation
echo "[4/6] Copying documentation..."
cp README.md "${RELEASE_DIR}/" 2>/dev/null || true
cp LICENSE "${RELEASE_DIR}/" 2>/dev/null || true
cp QUICKSTART.md "${RELEASE_DIR}/" 2>/dev/null || true
cp docs/*.md "${RELEASE_DIR}/docs/" 2>/dev/null || true

# Copy examples
echo "[5/6] Copying examples..."
cp examples/*.py "${RELEASE_DIR}/examples/" 2>/dev/null || true
cp examples/README.md "${RELEASE_DIR}/examples/" 2>/dev/null || true

# Create start scripts
echo "[6/6] Creating start scripts..."

# Create run_server.sh
cat > "${RELEASE_DIR}/run_server.sh" << 'EOF'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${SCRIPT_DIR}/lib:${LD_LIBRARY_PATH}"
echo "Starting SnapLLM Server..."
exec "${SCRIPT_DIR}/bin/snapllm" --server --port 6930 "$@"
EOF
chmod +x "${RELEASE_DIR}/run_server.sh"

# Create run_server_with_model.sh
cat > "${RELEASE_DIR}/run_server_with_model.sh" << 'EOF'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${SCRIPT_DIR}/lib:${LD_LIBRARY_PATH}"

echo ""
echo " SnapLLM Server with Model"
echo " ========================="
echo ""
read -p "Enter path to your .gguf model file: " MODEL_PATH
read -p "Enter a name for this model: " MODEL_NAME
echo ""
echo "Starting server with model: ${MODEL_NAME}"
exec "${SCRIPT_DIR}/bin/snapllm" --server --port 6930 --load-model "${MODEL_NAME}" "${MODEL_PATH}"
EOF
chmod +x "${RELEASE_DIR}/run_server_with_model.sh"

# Create VERSION file
echo "${VERSION}" > "${RELEASE_DIR}/VERSION"

# Create tarball
echo ""
echo "[INFO] Creating tarball..."
cd releases
tar -czvf "${RELEASE_NAME}.tar.gz" "${RELEASE_NAME}"
cd ..

echo ""
echo "========================================"
echo " Release Package Created Successfully!"
echo "========================================"
echo ""
echo "  Location: ${RELEASE_DIR}/"
echo "  Archive:  releases/${RELEASE_NAME}.tar.gz"
echo ""
echo "  Contents:"
echo "  - bin/snapllm (main executable)"
echo "  - lib/*.so (shared libraries)"
echo "  - examples/ (Python examples)"
echo "  - docs/ (documentation)"
echo "  - run_server.sh (quick start)"
echo "  - README.md, LICENSE, QUICKSTART.md"
echo ""
echo "  To use:"
echo "  1. Extract: tar -xzf ${RELEASE_NAME}.tar.gz"
echo "  2. Run: cd ${RELEASE_NAME} && ./run_server.sh"
echo "  3. Open: http://localhost:6930"
echo ""
