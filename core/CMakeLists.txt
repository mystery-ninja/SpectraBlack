# core — the scene graph, property system, keyframes, drivers, evaluator.
#
# This module must not depend on OpenGL, the OS, the UI, or file I/O.
# scripts/check-core-deps.sh enforces that in CI. See docs/01-ARCHITECTURE.md.

add_library(spectra_core
    src/version.cpp
)
add_library(spectra::core ALIAS spectra_core)

target_include_directories(spectra_core
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(spectra_core
    PUBLIC  glm::glm
    PRIVATE spectra::compile_options
)

target_compile_definitions(spectra_core PUBLIC
    SPECTRA_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SPECTRA_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SPECTRA_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(SPECTRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
