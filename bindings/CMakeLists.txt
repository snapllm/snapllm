# =============================================================================
# SnapLLM Python Bindings (pybind11)
# =============================================================================

# This CMakeLists is intended to be included from the top-level build:
#   cmake -DSNAPLLM_ENABLE_PYTHON_BINDINGS=ON ...

if(NOT TARGET snapllm)
    message(FATAL_ERROR "snapllm target not found. Configure from the repo root with -DSNAPLLM_ENABLE_PYTHON_BINDINGS=ON.")
endif()

# Find Python
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# Find or fetch pybind11
find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v2.11.1
    )
    FetchContent_MakeAvailable(pybind11)
endif()

# Create Python module
pybind11_add_module(snapllm_bindings
    snapllm_bindings.cpp
)

target_link_libraries(snapllm_bindings PRIVATE snapllm)

# Ensure headers from snapllm are visible (propagated via target_link_libraries)

# Output to a predictable location — override both the base and per-config
# properties so the module lands in python/ regardless of generator type.
set_target_properties(snapllm_bindings PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python
)
foreach(CFG Release Debug RelWithDebInfo MinSizeRel)
    string(TOUPPER ${CFG} CFG_UPPER)
    set_target_properties(snapllm_bindings PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} ${CMAKE_BINARY_DIR}/python
        RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} ${CMAKE_BINARY_DIR}/python
    )
endforeach()

message(STATUS "Python bindings output: ${CMAKE_BINARY_DIR}/python")
message(STATUS "Python version: ${Python3_VERSION}")
