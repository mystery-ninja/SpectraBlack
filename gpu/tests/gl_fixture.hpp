#pragma once

// A GL context shared by every test that needs one.
//
// CI runners have no display, so these tests report themselves skipped
// instead of failing. A developer with a GPU runs the full set, which is
// where the coverage that matters comes from.

#include <spectra/gpu/device.hpp>
#include <spectra/gpu/window.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <utility>

namespace spectra::gpu::testing {

struct Gl {
    std::unique_ptr<Window> window;
    std::unique_ptr<Device> device;
    std::string why;

    [[nodiscard]] bool ready() const { return device != nullptr; }
};

/// One context for the whole binary. Creating a window per test case is slow
/// enough to notice, and nothing here needs a fresh one.
inline Gl& gl() {
    static Gl shared = [] {
        Gl out;

        WindowOptions options;
        options.width = 64;
        options.height = 64;
        options.visible = false;
        options.vsync = false;
        options.title = "spectra gpu tests";

        auto window = Window::create(options);
        if (!window) {
            out.why = window.error();
            return out;
        }

        auto device = Device::create(Window::procAddressLoader());
        if (!device) {
            out.why = device.error();
            return out;
        }

        out.window = std::move(window.value());
        out.device = std::move(device.value());
        return out;
    }();
    return shared;
}

}  // namespace spectra::gpu::testing

/// Leaves the test case early when there is no GPU to run it on.
#define SPECTRA_REQUIRE_GL()                                                                       \
    do {                                                                                           \
        if (!::spectra::gpu::testing::gl().ready()) {                                              \
            MESSAGE("skipped, no GL context: " << ::spectra::gpu::testing::gl().why);              \
            return;                                                                                \
        }                                                                                          \
    } while (false)
