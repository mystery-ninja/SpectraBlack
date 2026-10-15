# gpu — thin RHI over OpenGL. Stage 2 of docs/15-BUILD-ORDER.md.
# Enable with -DSPECTRA_ENABLE_GPU=ON once there is something here.

add_library(spectra_gpu INTERFACE)
add_library(spectra::gpu ALIAS spectra_gpu)

target_include_directories(spectra_gpu
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(spectra_gpu INTERFACE glfw glm::glm)
