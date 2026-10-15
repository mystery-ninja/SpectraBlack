add_executable(spectra_core_tests
    test_main.cpp
    test_version.cpp
)

target_link_libraries(spectra_core_tests
    PRIVATE
        spectra::core
        doctest::doctest
        spectra::compile_options
)

doctest_discover_tests(spectra_core_tests
    TEST_PREFIX "core."
)
