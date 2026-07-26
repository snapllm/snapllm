# =============================================================================
# SnapLLM Source Build Configuration
# =============================================================================

# Core source files
set(SNAPLLM_SOURCES
    vpid_workspace.cpp
    vpid_tensor_cache.cpp
    dequant_cache.cpp
    vpid_hot_cache.cpp
    vpid_bridge.cpp
    model_manager.cpp
    request_router.cpp
    daemon_manager.cpp
    context_manager.cpp         # vPID L2 - KV Cache persistence
    file_cache_store.cpp        # vPID L2 - File-based cache storage
    kv_cache_extractor.cpp      # vPID L2 - KV extraction from llama.cpp
    tiered_memory_allocator.cpp # vPID L2 - GPU/CPU tiered memory allocation
    auto_tiering.cpp            # vPID L2 - Automatic tier promotion/demotion
    compression.cpp             # vPID L2 - LZ4/ZSTD compression for KV cache
    model_context_registry.cpp  # vPID L2 - Auto model-context association
    prefetch_engine.cpp
    validation.cpp
    workspace_metadata.cpp
    unified_model_manager.cpp
    server_limits.cpp
    server.cpp
    ${CMAKE_SOURCE_DIR}/external/llama.cpp/examples/gguf-hash/deps/sha256/sha256.c
)

# Security policy helpers are isolated so their unit tests do not need the
# model runtime or third-party inference libraries.
add_library(snapllm_server_security OBJECT server_security.cpp)
target_include_directories(snapllm_server_security PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(snapllm_server_security PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# Add diffusion support if enabled
if(SNAPLLM_HAS_DIFFUSION)
    list(APPEND SNAPLLM_SOURCES diffusion_bridge.cpp)
    message(STATUS "Adding diffusion_bridge.cpp to build")
endif()

# Add multimodal support if enabled
if(SNAPLLM_HAS_MULTIMODAL)
    list(APPEND SNAPLLM_SOURCES multimodal_bridge.cpp)
    message(STATUS "Adding multimodal_bridge.cpp to build")
endif()

# =============================================================================
# Create SnapLLM Library
# =============================================================================
add_library(snapllm STATIC ${SNAPLLM_SOURCES})
set_target_properties(snapllm PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(snapllm PRIVATE
    ${CMAKE_SOURCE_DIR}/external/llama.cpp/examples/gguf-hash/deps/sha256
    ${CMAKE_SOURCE_DIR}/external/llama.cpp/examples/gguf-hash/deps
)

# Link core dependencies
target_link_libraries(snapllm
    PUBLIC
        llama
    PRIVATE
        snapllm_server_security
)

# Add diffusion library if enabled
if(SNAPLLM_HAS_DIFFUSION)
    target_link_libraries(snapllm PUBLIC stable-diffusion stb_image)
    target_compile_definitions(snapllm PUBLIC SNAPLLM_HAS_DIFFUSION=1)
endif()

# Add multimodal library if enabled
if(SNAPLLM_HAS_MULTIMODAL)
    target_link_libraries(snapllm PUBLIC mtmd)
    target_compile_definitions(snapllm PUBLIC SNAPLLM_HAS_MULTIMODAL=1)
endif()

# OpenMP support
if(OpenMP_CXX_FOUND)
    target_link_libraries(snapllm PUBLIC OpenMP::OpenMP_CXX)
endif()

# Platform-specific libraries
if(WIN32)
    # Windows socket library for HTTP server
    target_link_libraries(snapllm PUBLIC ws2_32)
else()
    target_link_libraries(snapllm PRIVATE pthread)
endif()

# =============================================================================
# Include Directories
# =============================================================================
target_include_directories(snapllm
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/external/llama.cpp/include
        ${CMAKE_SOURCE_DIR}/external/llama.cpp/ggml/include
        ${CMAKE_SOURCE_DIR}/external/llama.cpp/ggml/src
        ${CMAKE_SOURCE_DIR}/external/llama.cpp/src
        ${CMAKE_SOURCE_DIR}/external/llama.cpp/vendor
        ${CMAKE_SOURCE_DIR}/external/nlohmann
)

# Add stable-diffusion.cpp includes if enabled
if(SNAPLLM_HAS_DIFFUSION)
    target_include_directories(snapllm
        PUBLIC
            ${CMAKE_SOURCE_DIR}/external/stable-diffusion.cpp
            ${CMAKE_SOURCE_DIR}/external/stable-diffusion.cpp/thirdparty
    )
endif()

# Add mtmd includes if enabled
if(SNAPLLM_HAS_MULTIMODAL)
    target_include_directories(snapllm
        PUBLIC
            ${CMAKE_SOURCE_DIR}/external/llama.cpp/tools/mtmd
            ${CMAKE_SOURCE_DIR}/external/llama.cpp/vendor/stb
    )
endif()

# =============================================================================
# Compile Options
# =============================================================================
target_compile_options(snapllm PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4100 /wd4244 /wd4267>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# Suppress specific warnings that come from external dependencies
if(MSVC)
    target_compile_definitions(snapllm PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
    )
endif()

# =============================================================================
# CLI Executable
# =============================================================================

# Windows resource file for application icon and version info
if(WIN32)
    set(SNAPLLM_RC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/snapllm.rc)
    if(EXISTS ${SNAPLLM_RC_FILE})
        message(STATUS "Including Windows resource file: ${SNAPLLM_RC_FILE}")
        add_executable(snapllm_cli main.cpp ${SNAPLLM_RC_FILE})
    else()
        message(STATUS "Windows resource file not found, building without icon")
        add_executable(snapllm_cli main.cpp)
    endif()
else()
    add_executable(snapllm_cli main.cpp)
endif()

target_link_libraries(snapllm_cli PRIVATE snapllm)
set_target_properties(snapllm_cli PROPERTIES
    OUTPUT_NAME snapllm
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Ensure output goes to unified bin directory for all configs
foreach(CONFIG_TYPE Release Debug RelWithDebInfo MinSizeRel)
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set_target_properties(snapllm_cli PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin
    )
endforeach()

# =============================================================================
# Tests
# =============================================================================
if(BUILD_TESTING)
    add_executable(snapllm_server_security_test
        ${CMAKE_SOURCE_DIR}/tests/server_security_test.cpp
    )
    target_link_libraries(snapllm_server_security_test PRIVATE snapllm_server_security)
    target_compile_options(snapllm_server_security_test PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )
    add_test(NAME server_security COMMAND snapllm_server_security_test)

    add_executable(snapllm_context_persistence_test
        ${CMAKE_SOURCE_DIR}/tests/context_persistence_test.cpp
    )
    target_include_directories(snapllm_context_persistence_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(snapllm_context_persistence_test PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )
    add_test(NAME context_persistence COMMAND snapllm_context_persistence_test)

    add_executable(snapllm_server_limits_test
        ${CMAKE_SOURCE_DIR}/tests/server_limits_test.cpp
        server_limits.cpp
    )
    target_link_libraries(snapllm_server_limits_test PRIVATE snapllm_server_security)
    target_include_directories(snapllm_server_limits_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(snapllm_server_limits_test PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )
    add_test(NAME server_limits COMMAND snapllm_server_limits_test)

    add_executable(snapllm_context_lifecycle_test
        ${CMAKE_SOURCE_DIR}/tests/context_lifecycle_test.cpp
    )
    target_link_libraries(snapllm_context_lifecycle_test PRIVATE snapllm)
    target_include_directories(snapllm_context_lifecycle_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(snapllm_context_lifecycle_test PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )
    add_test(NAME context_lifecycle COMMAND snapllm_context_lifecycle_test)

    add_executable(snapllm_model_lifecycle_test
        ${CMAKE_SOURCE_DIR}/tests/model_lifecycle_test.cpp
    )
    target_include_directories(snapllm_model_lifecycle_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_options(snapllm_model_lifecycle_test PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )
    add_test(NAME model_lifecycle COMMAND snapllm_model_lifecycle_test)

    add_executable(snapllm_workspace_metadata_security_test
        ${CMAKE_SOURCE_DIR}/tests/workspace_metadata_security_test.cpp
        workspace_metadata.cpp
    )
    target_include_directories(snapllm_workspace_metadata_security_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/external/nlohmann
    )
    target_compile_definitions(
        snapllm_workspace_metadata_security_test
        PRIVATE SNAPLLM_WORKSPACE_TEST_HOOKS=1
    )
    target_compile_options(snapllm_workspace_metadata_security_test PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )
    add_test(NAME workspace_metadata_security COMMAND snapllm_workspace_metadata_security_test)

    add_executable(snapllm_request_router_test
        ${CMAKE_SOURCE_DIR}/tests/request_router_test.cpp
    )
    target_link_libraries(snapllm_request_router_test PRIVATE snapllm)
    target_include_directories(snapllm_request_router_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME request_router COMMAND snapllm_request_router_test)

    add_executable(snapllm_benchmark_throughput
        ${CMAKE_SOURCE_DIR}/tests/benchmark_throughput.cpp
    )
    target_include_directories(snapllm_benchmark_throughput PRIVATE
        ${CMAKE_SOURCE_DIR}/external/llama.cpp/vendor/cpp-httplib
    )
endif()

# =============================================================================
# Summary
# =============================================================================
message(STATUS "SnapLLM library sources: ${SNAPLLM_SOURCES}")
