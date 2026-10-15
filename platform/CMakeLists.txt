# platform — files, threads, clock, logging, dialogs.
#
# The only module allowed to contain OS-specific code. Everything above it
# talks to the OS through this interface.

add_library(spectra_platform
    src/clock.cpp
)
add_library(spectra::platform ALIAS spectra_platform)

target_include_directories(spectra_platform
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(spectra_platform
    PRIVATE spectra::compile_options
)

if(UNIX AND NOT ANDROID)
    target_sources(spectra_platform PRIVATE src/linux/platform_linux.cpp)
elseif(WIN32)
    target_sources(spectra_platform PRIVATE src/windows/platform_windows.cpp)
endif()
