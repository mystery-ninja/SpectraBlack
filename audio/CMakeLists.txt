# audio — decode, playback, offline analysis. Stage 3 of docs/15-BUILD-ORDER.md.
# Enable with -DSPECTRA_ENABLE_AUDIO=ON once there is something here.

add_library(spectra_audio INTERFACE)
add_library(spectra::audio ALIAS spectra_audio)

target_include_directories(spectra_audio
    INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
