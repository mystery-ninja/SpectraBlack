add_executable(spectra_gpu_tests
    test_main.cpp
    test_shader_source.cpp
)

target_link_libraries(spectra_gpu_tests
    PRIVATE
        spectra::gpu
        doctest::doctest
        spectra::compile_options
)

doctest_discover_tests(spectra_gpu_tests
    TEST_PREFIX "gpu."
)
