# gpu — thin RHI over OpenGL. Stage 2 of docs/15-BUILD-ORDER.md.
# Enable with -DSPECTRA_ENABLE_GPU=ON.

# The GL backend is the only place in the project that may call GL directly.
# A second backend (GLES 3.2 for Android) becomes another directory here,
# implementing the same device.hpp, selected at configure time.
add_library(spectra_gpu
    backends/gl/device_gl.cpp
    src/framegraph.cpp
    src/image.cpp
    src/quad_renderer.cpp
    src/shader_source.cpp
    src/target_pool.cpp
    src/types.cpp
    src/window.cpp
)

add_library(spectra::gpu ALIAS spectra_gpu)

target_include_directories(spectra_gpu
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# SYSTEM: stb is a header-only library compiled into one of our translation
# units, and our warning levels are not its problem.
target_include_directories(spectra_gpu SYSTEM PRIVATE ${stb_SOURCE_DIR})

target_link_libraries(spectra_gpu
    PUBLIC  spectra::core glfw spectra_glad
    PRIVATE spectra::compile_options
)

# The demo from docs/15-BUILD-ORDER.md Stage 2. Not a test: it needs a real
# display, and its value is in being looked at.
add_executable(spectra_gpu_demo examples/quads.cpp)

target_link_libraries(spectra_gpu_demo
    PRIVATE spectra::gpu spectra::compile_options
)

if(SPECTRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
