#include <spectra/gpu/window.hpp>

#include <GLFW/glfw3.h>

#include <string>
#include <utility>

namespace spectra::gpu {
namespace {

/// GLFW is global state, so it is initialized on the first window and torn
/// down with the last. A reference count rather than a leak, because the
/// export path creates and destroys its own window around a render.
int gWindowCount = 0;
bool gInitialized = false;

std::string gLastError;

void errorCallback(int code, const char* description) {
    gLastError = std::string(description != nullptr ? description : "unknown") + " (GLFW " +
                 std::to_string(code) + ")";
}

[[nodiscard]] bool ensureInitialized(std::string& error) {
    if (gInitialized) {
        return true;
    }
    glfwSetErrorCallback(errorCallback);
    if (glfwInit() != GLFW_TRUE) {
        error = gLastError.empty() ? "could not initialize GLFW" : gLastError;
        return false;
    }
    gInitialized = true;
    return true;
}

}  // namespace

struct Window::Impl {
    GLFWwindow* handle{nullptr};
};

Window::Window() : impl_(std::make_unique<Impl>()) {}

Window::~Window() {
    if (impl_->handle != nullptr) {
        glfwDestroyWindow(impl_->handle);
        --gWindowCount;
    }
    if (gWindowCount == 0 && gInitialized) {
        glfwTerminate();
        gInitialized = false;
    }
}

Result<std::unique_ptr<Window>> Window::create(const WindowOptions& options) {
    using ResultType = Result<std::unique_ptr<Window>>;

    std::string error;
    if (!ensureInitialized(error)) {
        return ResultType::error(std::move(error));
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, options.debugContext ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, options.visible ? GLFW_TRUE : GLFW_FALSE);

    // The default framebuffer stays linear-unaware: the final pass writes
    // sRGB-encoded values itself so that preview and export agree.
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DEPTH_BITS, 0);
    glfwWindowHint(GLFW_STENCIL_BITS, 0);

    gLastError.clear();
    GLFWwindow* const handle =
        glfwCreateWindow(options.width, options.height, options.title.c_str(), nullptr, nullptr);
    if (handle == nullptr) {
        const std::string why =
            gLastError.empty() ? "no GL 4.3 core context available" : gLastError;
        if (gWindowCount == 0) {
            glfwTerminate();
            gInitialized = false;
        }
        return ResultType::error("could not create a window: " + why);
    }

    ++gWindowCount;

    std::unique_ptr<Window> window(new Window());
    window->impl_->handle = handle;

    glfwMakeContextCurrent(handle);
    window->setVsync(options.vsync);

    return ResultType::ok(std::move(window));
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(impl_->handle) == GLFW_TRUE;
}

void Window::requestClose() {
    glfwSetWindowShouldClose(impl_->handle, GLFW_TRUE);
}

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::present() {
    glfwSwapBuffers(impl_->handle);
}

int Window::framebufferWidth() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(impl_->handle, &width, &height);
    return width;
}

int Window::framebufferHeight() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(impl_->handle, &width, &height);
    return height;
}

void Window::setVsync(bool enabled) {
    glfwSwapInterval(enabled ? 1 : 0);
}

ProcAddressLoader Window::procAddressLoader() {
    return
        [](const char* name) -> void* { return reinterpret_cast<void*>(glfwGetProcAddress(name)); };
}

void* Window::nativeHandle() const {
    return impl_->handle;
}

}  // namespace spectra::gpu
