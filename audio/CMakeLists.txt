# audio — decode, playback, offline analysis. Stage 3 of docs/15-BUILD-ORDER.md.
# Enable with -DSPECTRA_ENABLE_AUDIO=ON.
#
# The FFmpeg backend is the only place in the project that may include libav*.
# A MediaCodec backend for Android becomes a sibling directory implementing the
# same decoder.hpp.

add_library(spectra_audio
    backends/ffmpeg/decoder_ffmpeg.cpp
    src/buffer.cpp
    src/dsp.cpp
    src/fft.cpp
)

add_library(spectra::audio ALIAS spectra_audio)

target_include_directories(spectra_audio
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(spectra_audio
    PUBLIC  spectra::core
    PRIVATE spectra::ffmpeg PFFFT::PFFFT spectra::compile_options
)

if(SPECTRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
