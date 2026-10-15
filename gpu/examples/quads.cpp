// Stage 2's exit criterion, as a runnable program: textured quads
// compositing with the six blend modes, in linear light, through the frame
// graph, at the display's refresh rate.
//
// There is no scene model here and no audio — those are Stages 1 and 3, and
// Stage 4 is where the three meet. This exists to prove the renderer works
// and to be the thing you run when a GL change looks suspicious.

#include <spectra/gpu/device.hpp>
#include <spectra/gpu/framegraph.hpp>
#include <spectra/gpu/image.hpp>
#include <spectra/gpu/quad_renderer.hpp>
#include <spectra/gpu/target_pool.hpp>
#include <spectra/gpu/window.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace spectra;
using namespace spectra::gpu;

namespace {

/// A checkerboard, so there is something to texture with when no image was
/// passed on the command line.
[[nodiscard]] Image checkerboard(int size, int squares) {
    Image image;
    image.width = size;
    image.height = size;
    image.pixels.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);

    const int step = size / squares;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool light = ((x / step) + (y / step)) % 2 == 0;
            const auto at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                             static_cast<std::size_t>(x)) *
                            4;
            image.pixels[at + 0] = light ? 230 : 40;
            image.pixels[at + 1] = light ? 230 : 40;
            image.pixels[at + 2] = light ? 230 : 60;
            image.pixels[at + 3] = 255;
        }
    }
    return image;
}

}  // namespace

int main(int argc, char** argv) {
    WindowOptions options;
    options.title = "Spectra Black — gpu demo";
    options.width = 1280;
    options.height = 720;

    auto window = Window::create(options);
    if (!window) {
        std::fprintf(stderr, "%s\n", window.error().c_str());
        return 1;
    }

    auto device = Device::create(Window::procAddressLoader());
    if (!device) {
        std::fprintf(stderr, "%s\n", device.error().c_str());
        return 1;
    }

    std::printf("renderer ........... %s\n", device.value()->info().renderer.c_str());
    std::printf("version ............ %s\n", device.value()->info().version.c_str());
    std::printf("persistent mapping . %s\n",
                device.value()->info().persistentMapping ? "yes" : "no");

    auto quads = QuadRenderer::create(*device.value());
    if (!quads) {
        std::fprintf(stderr, "%s\n", quads.error().c_str());
        return 1;
    }

    Image source = checkerboard(256, 8);
    if (argc > 1) {
        auto loaded = loadImage(argv[1]);
        if (!loaded) {
            std::fprintf(stderr, "%s\n", loaded.error().c_str());
            return 1;
        }
        source = std::move(loaded.value());
    }

    auto texture = uploadTexture(*device.value(), source);
    if (!texture) {
        std::fprintf(stderr, "%s\n", texture.error().c_str());
        return 1;
    }

    DeviceTargetAllocator allocator(*device.value());
    RenderTargetPool pool(allocator);

    const BlendMode modes[] = {BlendMode::Alpha,
                               BlendMode::PreMulAlpha,
                               BlendMode::Add,
                               BlendMode::AddAlpha,
                               BlendMode::Screen,
                               BlendMode::Over};

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto lastReport = start;
    int frames = 0;

    while (!window.value()->shouldClose()) {
        window.value()->pollEvents();
        device.value()->beginFrame();

        const int width = window.value()->framebufferWidth();
        const int height = window.value()->framebufferHeight();
        if (width <= 0 || height <= 0) {
            continue;  // Minimized.
        }

        const float seconds = std::chrono::duration<float>(Clock::now() - start).count();

        FrameGraph graph;

        // One offscreen pass in linear RGBA16F, then a present pass that
        // encodes to sRGB. Exactly the shape the export path will use.
        PassDesc scene{
            .name = "scene", .width = width, .height = height, .format = TextureFormat::RGBA16F};
        scene.execute = [&](const PassContext& context) {
            quads.value()->begin(context.width, context.height);
            context.device.clear(Color{0.02F, 0.02F, 0.03F, 1.0F});

            Quad backdrop;
            backdrop.texture = texture.value();
            backdrop.color = Color{0.35F, 0.35F, 0.4F, 1.0F};
            backdrop.blend = BlendMode::None;
            quads.value()->drawFullscreen(backdrop.texture, BlendMode::None, backdrop.color);

            // One quad per blend mode, rotating, overlapping its neighbours
            // so the differences between the modes are visible.
            for (std::size_t i = 0; i < std::size(modes); ++i) {
                const float phase = seconds + static_cast<float>(i) * 0.6F;

                Quad quad;
                quad.position =
                    Vec2{0.12F + 0.152F * static_cast<float>(i), 0.5F + 0.12F * std::sin(phase)};
                quad.size = Vec2{0.34F, 0.34F};
                quad.rotation = phase * 24.0F;
                quad.texture = texture.value();
                quad.blend = modes[i];
                quad.color = Color{0.5F + 0.5F * std::sin(phase),
                                   0.5F + 0.5F * std::sin(phase + 2.1F),
                                   0.5F + 0.5F * std::sin(phase + 4.2F),
                                   0.75F};
                quads.value()->draw(quad);
            }
        };
        const PassId scenePass = graph.addPass(std::move(scene));

        PassDesc present{.name = "present", .width = width, .height = height};
        present.present = true;
        present.inputs = {scenePass};
        present.execute = [&](const PassContext& context) {
            quads.value()->present(context.input(scenePass), context.width, context.height);
        };
        graph.addPass(std::move(present));

        const auto compiled = graph.compile();
        if (!compiled) {
            std::fprintf(stderr, "%s\n", compiled.error().c_str());
            return 1;
        }

        graph.execute(*device.value(), pool);
        pool.endFrame();

        if (const std::string errors = device.value()->takeErrors(); !errors.empty()) {
            std::fprintf(stderr, "%s\n", errors.c_str());
        }

        window.value()->present();
        ++frames;

        // Steady-state allocation is the Stage 2 exit criterion, so the demo
        // reports it rather than leaving it to be assumed.
        const auto now = Clock::now();
        if (now - lastReport >= std::chrono::seconds(1)) {
            const float elapsed = std::chrono::duration<float>(now - lastReport).count();
            std::printf("%6.1f fps   %u draws   %zu targets   %zu allocations\n",
                        static_cast<double>(static_cast<float>(frames) / elapsed),
                        device.value()->stats().drawCalls,
                        pool.inUse() + pool.available(),
                        pool.allocations());
            std::fflush(stdout);
            frames = 0;
            lastReport = now;
        }
    }

    pool.clear();
    return 0;
}
