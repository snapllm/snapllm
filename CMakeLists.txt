cmake_minimum_required(VERSION 3.18)

# =============================================================================
# SnapLLM - Ultra-fast Multi-Model AI Inference Engine
# =============================================================================
# Supports:
#   - Text LLM inference (llama.cpp backend)
#   - Image generation (stable-diffusion.cpp backend)
#   - Video generation (Wan2, CogVideoX)
#   - Multimodal vision (Gemma3, Qwen-VL, LLaVA)
#
# Build modes:
#   - GPU (CUDA): cmake -B build -DSNAPLLM_CUDA=ON
#   - CPU only:   cmake -B build -DSNAPLLM_CUDA=OFF
# =============================================================================

project(SnapLLM VERSION 1.1.0 LANGUAGES CXX C)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# =============================================================================
# Build Options
# =============================================================================
option(SNAPLLM_CUDA "Enable CUDA/GPU support" ON)
option(SNAPLLM_AVX2 "Enable AVX2 optimizations" ON)
option(SNAPLLM_OPENMP "Enable OpenMP for parallel processing" ON)
option(SNAPLLM_ENABLE_DIFFUSION "Enable image generation support" ON)
option(SNAPLLM_ENABLE_MULTIMODAL "Enable multimodal (vision/audio) support" ON)
option(SNAPLLM_ENABLE_PYTHON_BINDINGS "Enable Python bindings (pybind11)" OFF)

# Python bindings produce a shared library (.so) that links static libs,
# so every translation unit must be compiled with -fPIC.
if(SNAPLLM_ENABLE_PYTHON_BINDINGS)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# =============================================================================
# Configuration Summary
# =============================================================================
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  SnapLLM v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS "  Build Type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  CUDA/GPU:      ${SNAPLLM_CUDA}")
message(STATUS "  AVX2:          ${SNAPLLM_AVX2}")
message(STATUS "  OpenMP:        ${SNAPLLM_OPENMP}")
message(STATUS "  Diffusion:     ${SNAPLLM_ENABLE_DIFFUSION}")
message(STATUS "  Multimodal:    ${SNAPLLM_ENABLE_MULTIMODAL}")
message(STATUS "  Py Bindings:   ${SNAPLLM_ENABLE_PYTHON_BINDINGS}")
message(STATUS "========================================")
message(STATUS "")

# =============================================================================
# CUDA Configuration
# =============================================================================
if(SNAPLLM_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit QUIET)

    set(GGML_CUDA ON CACHE BOOL "Enable CUDA" FORCE)
    set(GGML_CUDA_F16 ON CACHE BOOL "Enable CUDA FP16" FORCE)

    if(CUDAToolkit_FOUND)
        message(STATUS "CUDA found: ${CUDAToolkit_VERSION}")
        get_filename_component(CUDA_BIN_DIR "${CUDAToolkit_INCLUDE_DIRS}/../bin" ABSOLUTE)
        message(STATUS "CUDA bin dir: ${CUDA_BIN_DIR}")
    else()
        message(STATUS "CUDA enabled but CUDAToolkit not found via find_package")
    endif()
else()
    set(GGML_CUDA OFF CACHE BOOL "Enable CUDA" FORCE)
    message(STATUS "CUDA disabled - CPU only mode")
endif()

# =============================================================================
# CPU Optimizations
# =============================================================================
if(SNAPLLM_AVX2)
    set(GGML_AVX2 ON CACHE BOOL "Enable AVX2" FORCE)
    set(GGML_FMA ON CACHE BOOL "Enable FMA" FORCE)
    set(GGML_AVX ON CACHE BOOL "Enable AVX" FORCE)
endif()

# =============================================================================
# OpenMP for Parallel Processing
# =============================================================================
if(SNAPLLM_OPENMP)
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        message(STATUS "OpenMP found - parallel processing enabled")
    endif()
endif()

# =============================================================================
# Output Directories - Unified for all configurations
# =============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# For multi-config generators (Visual Studio, Xcode)
foreach(CONFIG_TYPE Release Debug RelWithDebInfo MinSizeRel)
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib)
endforeach()

# =============================================================================
# Add Subdirectories
# =============================================================================
add_subdirectory(external)
add_subdirectory(src)

# Optional Python bindings
if(SNAPLLM_ENABLE_PYTHON_BINDINGS)
    add_subdirectory(bindings)
endif()
# MCB - Model Context Bucket (vPID L1+L2 as open protocol)
if(EXISTS "${CMAKE_SOURCE_DIR}/MCB/CMakeLists.txt")
    add_subdirectory(MCB)
else()
    message(STATUS "MCB subdirectory not found - skipping")
endif()

# =============================================================================
# Install Targets
# =============================================================================
include(GNUInstallDirs)

# Install the CLI executable
install(TARGETS snapllm_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT runtime
)

# Install the library
install(TARGETS snapllm
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT runtime
)

# Install headers
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/snapllm
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT development
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install MCB headers (if available)
if(EXISTS "${CMAKE_SOURCE_DIR}/MCB/include/mcb")
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/MCB/include/mcb
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT development
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )
endif()

# =============================================================================
# Post-Build: Copy CUDA DLLs (Windows)
# =============================================================================
if(WIN32 AND SNAPLLM_CUDA AND CUDAToolkit_FOUND)
    # List of CUDA runtime DLLs to copy
    set(CUDA_DLLS
        cudart64_12.dll
        cublas64_12.dll
        cublasLt64_12.dll
    )

    # Create custom target to copy CUDA DLLs
    add_custom_target(copy_cuda_dlls ALL
        COMMENT "Copying CUDA runtime DLLs to output directory..."
    )

    foreach(DLL ${CUDA_DLLS})
        if(EXISTS "${CUDA_BIN_DIR}/${DLL}")
            add_custom_command(TARGET copy_cuda_dlls POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CUDA_BIN_DIR}/${DLL}"
                    "${CMAKE_BINARY_DIR}/bin"
                COMMENT "Copying ${DLL}"
            )
        endif()
    endforeach()

    # Ensure DLLs are copied after CLI is built
    add_dependencies(copy_cuda_dlls snapllm_cli)

    # Install CUDA DLLs
    foreach(DLL ${CUDA_DLLS})
        if(EXISTS "${CUDA_BIN_DIR}/${DLL}")
            install(FILES "${CUDA_BIN_DIR}/${DLL}"
                DESTINATION ${CMAKE_INSTALL_BINDIR}
                COMPONENT runtime
            )
        endif()
    endforeach()
endif()

# =============================================================================
# CPack Configuration for Distribution
# =============================================================================
set(CPACK_PACKAGE_NAME "SnapLLM")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Ultra-fast Multi-Model AI Inference Engine")
set(CPACK_PACKAGE_VENDOR "SnapLLM")

if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif()
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

# =============================================================================
# Summary
# =============================================================================
message(STATUS "")
message(STATUS "SnapLLM configured successfully!")
message(STATUS "")
message(STATUS "Build commands:")
message(STATUS "  cmake --build . --config Release")
message(STATUS "")
message(STATUS "Install command:")
message(STATUS "  cmake --install . --prefix <install_dir>")
message(STATUS "")
