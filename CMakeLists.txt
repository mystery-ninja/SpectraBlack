cmake_minimum_required(VERSION 3.24)

# C is enabled for the third-party code only: GLFW and the generated glad
# loader. Nothing of ours is written in it.
project(SpectraBlack
    VERSION     0.1.0
    DESCRIPTION "Audio-reactive motion graphics"
    LANGUAGES   CXX C
)

# ---------------------------------------------------------------------------
# Options
#
# The GPU and audio modules pull heavy third-party dependencies. They are off
# until Stages 2 and 3 of docs/15-BUILD-ORDER.md actually begin, so that a
# fresh clone builds and tests in seconds.
# ---------------------------------------------------------------------------
option(SPECTRA_ENABLE_GPU   "Build the GPU module (fetches GLFW)"        OFF)
option(SPECTRA_ENABLE_AUDIO "Build the audio module (requires FFmpeg)"   OFF)
option(SPECTRA_ENABLE_APP   "Build the desktop editor"                   OFF)
option(SPECTRA_BUILD_TESTS  "Build tests"                                ON)
option(SPECTRA_WARNINGS_AS_ERRORS "Treat warnings as errors"             OFF)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Shared compile settings, applied via an interface target rather than
# globally, so third-party code is not subjected to our warning levels.
# ---------------------------------------------------------------------------
add_library(spectra_compile_options INTERFACE)
add_library(spectra::compile_options ALIAS spectra_compile_options)

if(MSVC)
    target_compile_options(spectra_compile_options INTERFACE /W4 /permissive- /utf-8)
    if(SPECTRA_WARNINGS_AS_ERRORS)
        target_compile_options(spectra_compile_options INTERFACE /WX)
    endif()
else()
    target_compile_options(spectra_compile_options INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wdouble-promotion
    )
    if(SPECTRA_WARNINGS_AS_ERRORS)
        target_compile_options(spectra_compile_options INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# GLM — header-only math, used by core and gpu alike.
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# nlohmann/json — the project file format. Core, not a test-only dependency.
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

if(SPECTRA_BUILD_TESTS)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.5.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(doctest)

    enable_testing()
    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
endif()

if(SPECTRA_ENABLE_GPU)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    # GLFW builds a Wayland backend by default on Linux, which needs
    # wayland-scanner and the protocol XML. Rather than making every
    # contributor install those to get a window, build what the machine can
    # and say so. Under Wayland without them, X11 runs through XWayland.
    if(UNIX AND NOT APPLE)
        find_package(PkgConfig QUIET)
        find_program(SPECTRA_WAYLAND_SCANNER wayland-scanner)
        if(PkgConfig_FOUND AND SPECTRA_WAYLAND_SCANNER)
            pkg_check_modules(SPECTRA_WAYLAND_PROTOCOLS QUIET wayland-protocols>=1.15)
        endif()
        if(SPECTRA_WAYLAND_SCANNER AND SPECTRA_WAYLAND_PROTOCOLS_FOUND)
            set(GLFW_BUILD_WAYLAND ON  CACHE BOOL "" FORCE)
        else()
            message(STATUS "wayland-scanner or wayland-protocols missing — GLFW: X11 only")
            set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        endif()
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glfw)

    # glad 2 generates the loader at configure time, which needs Python with
    # Jinja2 (`pip install jinja2`, or `apt install python3-jinja2`). The
    # generated code has no runtime dependency on either.
    FetchContent_Declare(glad
        GIT_REPOSITORY https://github.com/Dav1dde/glad.git
        GIT_TAG        v2.0.8
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  cmake
    )
    FetchContent_MakeAvailable(glad)

    # REPRODUCIBLE pins the generated source so two machines produce identical
    # output, which keeps the loader out of build-difference investigations.
    glad_add_library(spectra_glad REPRODUCIBLE API gl:core=4.3)
endif()

# ---------------------------------------------------------------------------
# Modules
#
# Dependencies point downward only. core/ depends on nothing above it, and
# scripts/check-core-deps.sh enforces that in CI.
# ---------------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(platform)

if(SPECTRA_ENABLE_GPU)
    add_subdirectory(gpu)
endif()

if(SPECTRA_ENABLE_AUDIO)
    add_subdirectory(audio)
endif()

if(SPECTRA_ENABLE_APP)
    add_subdirectory(app-desktop)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "SpectraBlack ${PROJECT_VERSION}")
message(STATUS "  build type ....... ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler ......... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  gpu module ....... ${SPECTRA_ENABLE_GPU}")
message(STATUS "  audio module ..... ${SPECTRA_ENABLE_AUDIO}")
message(STATUS "  desktop app ...... ${SPECTRA_ENABLE_APP}")
message(STATUS "  tests ............ ${SPECTRA_BUILD_TESTS}")
message(STATUS "")
