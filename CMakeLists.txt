cmake_minimum_required(VERSION 3.25)

project(PerformanceMonitor
    VERSION 0.2.0
    DESCRIPTION "Lightweight Win32 CPU/GPU performance monitor"
    LANGUAGES CXX RC
)

if(NOT WIN32)
    message(FATAL_ERROR "PerformanceMonitor currently supports Windows only.")
endif()

set(PERFMON_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    resources/app.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc"
    @ONLY
)

add_executable(PerformanceMonitor WIN32
    src/main.cpp
    src/win32_headers.hpp
    src/text_util.hpp

    src/app/application.cpp
    src/app/application.hpp
    src/app/main_window.cpp
    src/app/main_window.hpp

    src/ui/ui_state.hpp
    src/ui/layout.cpp
    src/ui/layout.hpp
    src/ui/graph_renderer.cpp
    src/ui/graph_renderer.hpp
    src/ui/renderer.cpp
    src/ui/renderer.hpp

    src/nvml_loader.cpp
    src/nvml_loader.hpp

    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc"
)

set_property(
    TARGET PerformanceMonitor
    PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
)

set_target_properties(PerformanceMonitor PROPERTIES
    OUTPUT_NAME "PerformanceMonitor"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_compile_features(PerformanceMonitor PRIVATE cxx_std_20)

target_compile_definitions(PerformanceMonitor PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    STRICT
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(PerformanceMonitor PRIVATE
        /W4
        /permissive-
        /EHsc
        /utf-8
    )

    # Our app.rc already embeds RT_MANIFEST resource #1.
    # Prevent MSVC from generating another one.
    target_link_options(PerformanceMonitor PRIVATE
        /MANIFEST:NO
    )
endif()

# Native Windows graphics/windowing libraries. These are provided by Windows.
target_link_libraries(PerformanceMonitor PRIVATE
    d2d1
    dwrite
    dxgi
    ole32
    user32
    gdi32
)

# NVML is loaded dynamically at runtime, so we intentionally do NOT link
# nvml.lib. We only need nvml.h at compile time. The application still builds
# without the header and reports NVML as disabled.
find_package(CUDAToolkit QUIET)

if(CUDAToolkit_FOUND)
    target_include_directories(PerformanceMonitor PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(PerformanceMonitor PRIVATE PERFMON_HAS_NVML_HEADER=1)
    message(STATUS "NVML header enabled from CUDA Toolkit: ${CUDAToolkit_INCLUDE_DIRS}")
else()
    find_path(NVML_INCLUDE_DIR
        NAMES nvml.h
        HINTS
            "$ENV{CUDA_PATH}/include"
            "$ENV{CUDA_PATH_V13_0}/include"
            "$ENV{CUDA_PATH_V12_9}/include"
            "$ENV{CUDA_PATH_V12_8}/include"
            "$ENV{CUDA_PATH_V12_7}/include"
            "$ENV{CUDA_PATH_V12_6}/include"
    )

    if(NVML_INCLUDE_DIR)
        target_include_directories(PerformanceMonitor PRIVATE "${NVML_INCLUDE_DIR}")
        target_compile_definitions(PerformanceMonitor PRIVATE PERFMON_HAS_NVML_HEADER=1)
        message(STATUS "NVML header enabled from: ${NVML_INCLUDE_DIR}")
    else()
        target_compile_definitions(PerformanceMonitor PRIVATE PERFMON_HAS_NVML_HEADER=0)
        message(WARNING
            "nvml.h was not found. The project will still build, but NVML "
            "probing will be disabled. Install the NVIDIA CUDA Toolkit or set CUDA_PATH."
        )
    endif()
endif()

install(TARGETS PerformanceMonitor RUNTIME DESTINATION .)
