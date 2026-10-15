#include <spectra/gpu/target_pool.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <vector>

using namespace spectra;
using namespace spectra::gpu;

namespace {

/// Hands out handles without a GPU, and records what it was asked for.
class FakeAllocator final : public TargetAllocator {
public:
    [[nodiscard]] Result<FramebufferHandle> createTarget(const TargetKey& key) override {
        if (failNext) {
            failNext = false;
            return Result<FramebufferHandle>::error("out of memory");
        }
        created.push_back(key);
        return Result<FramebufferHandle>::ok(FramebufferHandle{++nextId});
    }

    void destroyTarget(FramebufferHandle target) override { destroyed.push_back(target); }

    std::vector<TargetKey> created;
    std::vector<FramebufferHandle> destroyed;
    std::uint32_t nextId{0};
    bool failNext{false};
};

constexpr TargetKey kFullHd{.width = 1920, .height = 1080, .format = TextureFormat::RGBA16F};
constexpr TargetKey kHalf{.width = 960, .height = 540, .format = TextureFormat::RGBA16F};

}  // namespace

TEST_CASE("the first acquire allocates, and a matching one after release does not") {
    FakeAllocator allocator;
    RenderTargetPool pool(allocator);

    const FramebufferHandle first = pool.acquire(kFullHd);
    REQUIRE(first.valid());
    CHECK(pool.allocations() == 1);
    CHECK(pool.inUse() == 1);

    pool.release(first);
    CHECK(pool.inUse() == 0);
    CHECK(pool.available() == 1);

    const FramebufferHandle second = pool.acquire(kFullHd);
    CHECK(second == first);
    CHECK(pool.allocations() == 1);
}

TEST_CASE("targets held at the same time do not alias each other") {
    FakeAllocator allocator;
    RenderTargetPool pool(allocator);

    const FramebufferHandle a = pool.acquire(kFullHd);
    const FramebufferHandle b = pool.acquire(kFullHd);

    CHECK(a != b);
    CHECK(pool.allocations() == 2);
    CHECK(pool.inUse() == 2);
}

TEST_CASE("a different size is a different target") {
    FakeAllocator allocator;
    RenderTargetPool pool(allocator);

    pool.release(pool.acquire(kFullHd));
    const FramebufferHandle small = pool.acquire(kHalf);

    CHECK(small.valid());
    CHECK(pool.allocations() == 2);
    CHECK(allocator.created.back() == kHalf);
}

TEST_CASE("format and depth are part of what makes a target reusable") {
    FakeAllocator allocator;
    RenderTargetPool pool(allocator);

    pool.release(
        pool.acquire(TargetKey{.width = 64, .height = 64, .format = TextureFormat::RGBA16F}));

    const FramebufferHandle other =
        pool.acquire(TargetKey{.width = 64, .height = 64, .format = TextureFormat::RGBA8});
    CHECK(other.valid());
    CHECK(pool.allocations() == 2);

    const FramebufferHandle withDepth = pool.acquire(
        TargetKey{.width = 64, .height = 64, .format = TextureFormat::RGBA16F, .depth = true});
    CHECK(withDepth.valid());
    CHECK(pool.allocations() == 3);
}

TEST_CASE("a steady-state frame loop stops allocating once the pool is warm") {
    FakeAllocator allocator;
    RenderTargetPool pool(allocator);

    for (int frame = 0; frame < 120; ++frame) {
        const FramebufferHandle scene = pool.acquire(kFullHd);
        const FramebufferHandle glow = pool.acquire(kHalf);
        pool.release(glow);
        pool.release(scene);
        pool.endFrame();
    }

    // Two targets, allocated on the first frame and reused for the other 119.
    CHECK(pool.allocations() == 2);
    CHECK(allocator.destroyed.empty());
}

TEST_CASE("a target idle for long enough is given back to the driver") {
    FakeAllocator allocator;
    RenderTargetPool pool(allocator, 10);

    pool.release(pool.acquire(kFullHd));

    for (int frame = 0; frame < 10; ++frame) {
        pool.endFrame();
    }
    CHECK(allocator.destroyed.empty());  // Still inside the window.

    pool.endFrame();
    CHECK(allocator.destroyed.size() == 1);
    CHECK(pool.available() == 0);
}

TEST_CASE("a target still in use is never aged out from under its reader") {
    FakeAllocator allocator;
    RenderTargetPool pool(allocator, 2);

    const FramebufferHandle held = pool.acquire(kFullHd);
    for (int frame = 0; frame < 20; ++frame) {
        pool.endFrame();
    }

    CHECK(allocator.destroyed.empty());
    CHECK(pool.inUse() == 1);
    CHECK(held.valid());
}

TEST_CASE("releasing a handle the pool never gave out is ignored") {
    FakeAllocator allocator;
    RenderTargetPool pool(allocator);

    pool.release(FramebufferHandle{});
    pool.release(FramebufferHandle{12345});

    CHECK(pool.available() == 0);
    CHECK(pool.inUse() == 0);
}

TEST_CASE("a failed allocation yields a null handle and an explanation") {
    FakeAllocator allocator;
    RenderTargetPool pool(allocator);
    allocator.failNext = true;

    const FramebufferHandle target = pool.acquire(kFullHd);

    CHECK_FALSE(target.valid());
    CHECK(pool.lastError() == "out of memory");
    CHECK(pool.inUse() == 0);
}

TEST_CASE("clearing frees everything, checked out or not") {
    FakeAllocator allocator;
    RenderTargetPool pool(allocator);

    const FramebufferHandle held = pool.acquire(kFullHd);
    pool.release(pool.acquire(kHalf));
    CHECK(held.valid());

    pool.clear();

    CHECK(allocator.destroyed.size() == 2);
    CHECK(pool.inUse() == 0);
    CHECK(pool.available() == 0);
}
