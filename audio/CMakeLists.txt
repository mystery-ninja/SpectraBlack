# audio — decode, playback, offline analysis. Stage 3 of docs/15-BUILD-ORDER.md.
# Enable with -DSPECTRA_ENABLE_AUDIO=ON.
#
# backends/ffmpeg/ is the only place that may include libav*, and
# backends/miniaudio/ the only place that may include miniaudio. A MediaCodec
# backend for Android becomes a sibling directory implementing the same
# decoder.hpp and playback.hpp.

add_library(spectra_audio
    backends/ffmpeg/decoder_ffmpeg.cpp
    backends/miniaudio/player_miniaudio.cpp
    src/analysis.cpp
    src/buffer.cpp
    src/cache_file.cpp
    src/dsp.cpp
    src/fft.cpp
    src/onsets.cpp
    src/playback.cpp
)

add_library(spectra::audio ALIAS spectra_audio)

target_include_directories(spectra_audio
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(spectra_audio
    PUBLIC  spectra::core
    PRIVATE spectra::ffmpeg PFFFT::PFFFT spectra::miniaudio spectra::compile_options
)

if(SPECTRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
