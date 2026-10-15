# gpu — thin RHI over OpenGL. Stage 2 of docs/15-BUILD-ORDER.md.
# Enable with -DSPECTRA_ENABLE_GPU=ON.

# The GL backend is the only place in the project that may call GL directly.
# A second backend (GLES 3.2 for Android) becomes another directory here,
# implementing the same device.hpp, selected at configure time.
add_library(spectra_gpu
    backends/gl/device_gl.cpp
    src/shader_source.cpp
    src/types.cpp
    src/window.cpp
)

add_library(spectra::gpu ALIAS spectra_gpu)

target_include_directories(spectra_gpu
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(spectra_gpu
    PUBLIC  spectra::core glfw spectra_glad
    PRIVATE spectra::compile_options
)

if(SPECTRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
