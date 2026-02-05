# External dependencies

# llama.cpp integration
# We'll use it as a git submodule
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp/CMakeLists.txt)
    message(STATUS "llama.cpp not found. Run: git submodule update --init --recursive")
    message(FATAL_ERROR "Please initialize git submodules")
endif()

# Configure llama.cpp build
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "Disable llama.cpp tests")
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "Disable llama.cpp examples")
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "Disable llama.cpp server")
set(LLAMA_ALL_WARNINGS OFF CACHE BOOL "Disable llama.cpp warnings")
set(LLAMA_FATAL_WARNINGS OFF CACHE BOOL "Disable warnings-as-errors in llama.cpp")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries")

# Add llama.cpp
add_subdirectory(llama.cpp EXCLUDE_FROM_ALL)

# Enable SIMD optimizations for llama.cpp/ggml
set(GGML_AVX2 ON CACHE BOOL "Enable AVX2 SIMD optimizations" FORCE)
set(GGML_FMA ON CACHE BOOL "Enable FMA (Fused Multiply-Add) instructions" FORCE)
set(GGML_AVX ON CACHE BOOL "Enable AVX SIMD optimizations" FORCE)

# ============================================================
# stable-diffusion.cpp integration for image generation
# ============================================================
option(SNAPLLM_ENABLE_DIFFUSION "Enable stable-diffusion.cpp for image generation" ON)

if(SNAPLLM_ENABLE_DIFFUSION)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/stable-diffusion.cpp/stable-diffusion.h)
        message(STATUS "stable-diffusion.cpp not found - diffusion support disabled")
        message(STATUS "To enable: git clone https://github.com/leejet/stable-diffusion.cpp external/stable-diffusion.cpp")
        set(SNAPLLM_ENABLE_DIFFUSION OFF CACHE BOOL "" FORCE)
    else()
        message(STATUS "stable-diffusion.cpp found - enabling diffusion support")

        # Configure stable-diffusion.cpp build options
        # Respect SnapLLM CUDA setting so CPU-only builds work in Docker/local dev.
        set(SD_BUILD_SHARED_LIBS OFF CACHE BOOL "Build as static library")
        set(SD_BUILD_EXAMPLES OFF CACHE BOOL "Disable examples")
        set(SD_CUDA ${SNAPLLM_CUDA} CACHE BOOL "Enable CUDA backend for SD" FORCE)
        set(SD_FLASH_ATTN ${SNAPLLM_CUDA} CACHE BOOL "Enable flash attention" FORCE)
        set(GGML_CUDA ${SNAPLLM_CUDA} CACHE BOOL "Enable CUDA for GGML in SD" FORCE)

        # Add stable-diffusion.cpp
        add_subdirectory(stable-diffusion.cpp EXCLUDE_FROM_ALL)

        # Export for parent
        set(SNAPLLM_HAS_DIFFUSION ON CACHE INTERNAL "Diffusion support available")
    endif()
endif()

# ============================================================
# STB libraries for image I/O
# ============================================================
add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stable-diffusion.cpp/thirdparty)

# ============================================================
# mtmd (MultiModal) library from llama.cpp for vision/audio support
# ============================================================
option(SNAPLLM_ENABLE_MULTIMODAL "Enable multimodal support (vision/audio)" ON)

if(SNAPLLM_ENABLE_MULTIMODAL)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp/tools/mtmd/mtmd.h)
        message(STATUS "mtmd found - enabling multimodal support")

        # Use llama.cpp's built-in mtmd library instead of building separately
        # This ensures ABI compatibility with the rest of llama.cpp
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp/tools/mtmd mtmd_build)

        set(SNAPLLM_HAS_MULTIMODAL ON CACHE INTERNAL "Multimodal support available")
    else()
        message(STATUS "mtmd not found - multimodal support disabled")
        set(SNAPLLM_ENABLE_MULTIMODAL OFF CACHE BOOL "" FORCE)
    endif()
endif()
