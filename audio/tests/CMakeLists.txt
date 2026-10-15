add_executable(spectra_audio_tests
    test_buffer.cpp
    test_analysis.cpp
    test_cache_file.cpp
    test_decoder.cpp
    test_dsp.cpp
    test_fft.cpp
    test_main.cpp
    test_onsets.cpp
)

target_link_libraries(spectra_audio_tests
    PRIVATE
        spectra::audio
        doctest::doctest
        spectra::compile_options
)

# Fixtures for the compressed formats are transcoded by the ffmpeg binary at
# test time, so that producing them does not pull libav* into a second
# translation unit. Without it, those cases are skipped and WAV still covers
# the decode path end to end.
find_program(SPECTRA_FFMPEG_EXECUTABLE ffmpeg)
if(SPECTRA_FFMPEG_EXECUTABLE)
    target_compile_definitions(spectra_audio_tests
        PRIVATE SPECTRA_FFMPEG_EXECUTABLE="${SPECTRA_FFMPEG_EXECUTABLE}"
    )
else()
    message(STATUS "ffmpeg binary not found — MP3/FLAC/OGG decode tests will be skipped")
endif()

doctest_discover_tests(spectra_audio_tests
    TEST_PREFIX "audio."
)
