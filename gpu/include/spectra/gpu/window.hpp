#pragma once

#include <spectra/core/result.hpp>
#include <spectra/gpu/device.hpp>

#include <memory>
#include <string>

namespace spectra::gpu {

struct WindowOptions {
    int width{1280};
    int height{720};
    std::string title{"Spectra Black"};

    /// Hidden windows still have a real context, which is how an offscreen
    /// render gets one on a desktop.
    bool visible{true};

    /// Vsync caps preview at the display rate. Export turns it off, or it
    /// would render no faster than the monitor refreshes.
    bool vsync{true};

    /// Asks the driver for a debug context, which is what makes
    /// `Device::takeErrors` report anything useful.
    bool debugContext{true};
};

/// A window and the GL 4.3 core context that goes with it.
///
/// It lives in `gpu/` rather than in the editor because the context, not the
/// window, is the thing being created here — export needs one without a
/// visible window, and Android will need one without GLFW at all.
class Window {
public:
    [[nodiscard]] static Result<std::unique_ptr<Window>> create(const WindowOptions& options = {});

    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    [[nodiscard]] bool shouldClose() const;
    void requestClose();

    void pollEvents();

    /// Swap buffers. The framebuffer must already hold sRGB-encoded values:
    /// the conversion is done explicitly by the final pass rather than by
    /// GL_FRAMEBUFFER_SRGB, because export has no swap chain and the two
    /// paths have to produce identical pixels. See docs/03-RENDER-ENGINE.md.
    void present();

    /// Size of the drawable area in pixels, which is not the window size on a
    /// scaled display.
    [[nodiscard]] int framebufferWidth() const;
    [[nodiscard]] int framebufferHeight() const;

    void setVsync(bool enabled);

    /// Feeds `Device::create`.
    [[nodiscard]] static ProcAddressLoader procAddressLoader();

    /// The `GLFWwindow*`, for the editor's ImGui backend. Nothing else should
    /// need it.
    [[nodiscard]] void* nativeHandle() const;

private:
    Window();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace spectra::gpu
