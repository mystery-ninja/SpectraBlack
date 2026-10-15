# app-desktop — the ImGui editor. Stage 5 of docs/15-BUILD-ORDER.md.
# Enable with -DSPECTRA_ENABLE_APP=ON.

add_executable(spectra src/main.cpp)

target_link_libraries(spectra
    PRIVATE
        spectra::core
        spectra::platform
        spectra::compile_options
)
