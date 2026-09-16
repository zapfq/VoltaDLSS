add_executable(
    VoltaDLSSLauncher
    WIN32
    launcher.cpp
)

target_compile_features(
    VoltaDLSSLauncher
    PRIVATE
    cxx_std_17
)

target_link_libraries(
    VoltaDLSSLauncher
    PRIVATE
    user32
    gdi32
    comdlg32
    shell32
    advapi32
)

set_target_properties(
    VoltaDLSSLauncher
    PROPERTIES
    OUTPUT_NAME "VoltaDLSS"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)
