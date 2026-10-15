#include <spectra/gpu/target_pool.hpp>

#include <algorithm>
#include <utility>

namespace spectra::gpu {

Result<FramebufferHandle> DeviceTargetAllocator::createTarget(const TargetKey& key) {
    return device_->createFramebuffer(FramebufferDesc{.width = key.width,
                                                      .height = key.height,
                                                      .format = key.format,
                                                      .filter = TextureFilter::Linear,
                                                      .depth = key.depth,
                                                      .debugName = "pooled target"});
}

void DeviceTargetAllocator::destroyTarget(FramebufferHandle target) {
    device_->destroyFramebuffer(target);
}

RenderTargetPool::RenderTargetPool(TargetAllocator& allocator, int framesUntilRelease)
    : allocator_(&allocator), framesUntilRelease_(std::max(framesUntilRelease, 0)) {}

RenderTargetPool::~RenderTargetPool() {
    clear();
}

FramebufferHandle RenderTargetPool::acquire(const TargetKey& key) {
    const auto match = std::find_if(
        free_.begin(), free_.end(), [&key](const Entry& entry) { return entry.key == key; });
    if (match != free_.end()) {
        Entry entry = *match;
        free_.erase(match);
        live_.push_back(entry);
        return entry.target;
    }

    Result<FramebufferHandle> created = allocator_->createTarget(key);
    if (!created) {
        lastError_ = created.error();
        return FramebufferHandle{};
    }

    ++allocations_;
    live_.push_back(Entry{.key = key, .target = created.value()});
    return created.value();
}

void RenderTargetPool::release(FramebufferHandle target) {
    if (!target.valid()) {
        return;
    }
    const auto match = std::find_if(live_.begin(), live_.end(), [target](const Entry& entry) {
        return entry.target == target;
    });
    if (match == live_.end()) {
        return;
    }

    Entry entry = *match;
    entry.idleSince = frame_;
    live_.erase(match);
    free_.push_back(entry);
}

void RenderTargetPool::endFrame() {
    ++frame_;

    if (framesUntilRelease_ <= 0) {
        return;
    }
    const auto limit = static_cast<std::uint64_t>(framesUntilRelease_);

    const auto expired = std::partition(free_.begin(), free_.end(), [this, limit](const Entry& e) {
        return frame_ - e.idleSince <= limit;
    });
    for (auto at = expired; at != free_.end(); ++at) {
        allocator_->destroyTarget(at->target);
    }
    free_.erase(expired, free_.end());
}

void RenderTargetPool::clear() {
    for (const Entry& entry : free_) {
        allocator_->destroyTarget(entry.target);
    }
    for (const Entry& entry : live_) {
        allocator_->destroyTarget(entry.target);
    }
    free_.clear();
    live_.clear();
}

}  // namespace spectra::gpu
