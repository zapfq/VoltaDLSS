cmake_minimum_required(VERSION 3.25)

project(VoltaDLSS LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

set(CMAKE_SYSTEM_VERSION 10.0.26100.0)

# Titan V / Volta = SM 7.0
set(CMAKE_CUDA_ARCHITECTURES 70)


# ---------------------------------------------------------------------------
# VoltaDLSS
# ---------------------------------------------------------------------------

add_executable(VoltaDLSS
    src/main.cpp
    src/quality.cpp
    src/tensor_benchmark.cu
    src/scaler.cu
    src/tensor_scaler.cu
    src/adaptive_scaler.cu
    src/easu_reference.cu
    src/easu_tensor.cu
)

target_include_directories(VoltaDLSS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(VoltaDLSS PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
)

set_source_files_properties(
    src/easu_tensor.cu
    PROPERTIES
    COMPILE_OPTIONS "-Xptxas=-v"
)


# ---------------------------------------------------------------------------
# VoltaDLSS Runtime
# ---------------------------------------------------------------------------

add_executable(VoltaDLSSRuntime
    src/runtime_main.cpp
    src/runtime.cpp
    src/capture.cpp
    src/presenter.cpp
    src/overlay.cpp
    src/telemetry.cpp
    src/quality.cpp

    src/scaler.cu
    src/easu_reference.cu
    src/easu_tensor.cu
)

target_include_directories(VoltaDLSSRuntime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/cppwinrt"
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/include"
)

set_target_properties(VoltaDLSSRuntime PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
)

target_link_libraries(VoltaDLSSRuntime PRIVATE
    user32
    gdi32
    d3d11
    dxgi
    d2d1
    dwrite
    windowsapp
)


# ---------------------------------------------------------------------------
# VoltaDLSS Throughput
# ---------------------------------------------------------------------------

add_executable(VoltaDLSSThroughput
    src/throughput_benchmark.cpp
    src/capture.cpp
    src/presenter.cpp
    src/easu_reference.cu
    src/easu_tensor.cu
)

target_include_directories(VoltaDLSSThroughput PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/cppwinrt"
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/include"
)

set_target_properties(VoltaDLSSThroughput PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
)

target_link_libraries(VoltaDLSSThroughput PRIVATE
    user32
    gdi32
    d3d11
    dxgi
    windowsapp
)

# NVIDIA CUPTI - CUDA 12.9
set(CUPTI_ROOT
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9/extras/CUPTI"
)

target_include_directories(VoltaDLSSRuntime PRIVATE
    "${CUPTI_ROOT}/include"
)

target_link_directories(VoltaDLSSRuntime PRIVATE
    "${CUPTI_ROOT}/lib64"
)

target_link_libraries(VoltaDLSSRuntime PRIVATE
    cupti
    )






# VoltaDLSS native launcher
add_subdirectory(launcher)

