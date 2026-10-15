#include <spectra/gpu/framegraph.hpp>

#include <algorithm>
#include <utility>

namespace spectra::gpu {

TextureHandle PassContext::input(PassId pass) const {
    if (graph == nullptr || inputs == nullptr) {
        return TextureHandle{};
    }

    const bool declared = std::find(inputs, inputs + inputCount, pass) != inputs + inputCount;
    if (!declared) {
        return TextureHandle{};
    }
    return device.framebufferTexture(graph->targetOf(pass));
}

PassId FrameGraph::addPass(PassDesc pass) {
    passes_.push_back(std::move(pass));
    return static_cast<PassId>(passes_.size() - 1);
}

void FrameGraph::clear() {
    passes_.clear();
    order_.clear();
    lastReader_.clear();
    targets_.clear();
}

Result<std::vector<PassId>> FrameGraph::compile() {
    using ResultType = Result<std::vector<PassId>>;

    order_.clear();
    lastReader_.assign(passes_.size(), 0);
    targets_.assign(passes_.size(), FramebufferHandle{});

    for (PassId id = 0; id < passes_.size(); ++id) {
        for (const PassId input : passes_[id].inputs) {
            if (input >= passes_.size()) {
                return ResultType::error("pass \"" + passes_[id].name +
                                         "\" reads a pass that does not exist");
            }
            if (input == id) {
                return ResultType::error("pass \"" + passes_[id].name + "\" reads itself");
            }
        }
    }

    // Depth-first topological sort. Iterative, because a deeply nested
    // composition tree would otherwise put the recursion depth in the user's
    // hands, and "open this project to crash the app" is not a feature.
    enum class Mark : std::uint8_t { Unvisited, InProgress, Done };
    std::vector<Mark> marks(passes_.size(), Mark::Unvisited);

    struct Frame {
        PassId pass{0};
        std::size_t nextInput{0};
    };

    std::vector<Frame> stack;

    for (PassId root = 0; root < passes_.size(); ++root) {
        if (marks[root] != Mark::Unvisited) {
            continue;
        }
        stack.push_back(Frame{.pass = root});
        marks[root] = Mark::InProgress;

        while (!stack.empty()) {
            Frame& top = stack.back();
            const PassDesc& branch = passes_[top.pass];

            if (top.nextInput < branch.inputs.size()) {
                const PassId next = branch.inputs[top.nextInput];
                ++top.nextInput;

                if (marks[next] == Mark::InProgress) {
                    return ResultType::error("pass \"" + passes_[next].name + "\" and pass \"" +
                                             branch.name + "\" depend on each other");
                }
                if (marks[next] == Mark::Unvisited) {
                    marks[next] = Mark::InProgress;
                    stack.push_back(Frame{.pass = next});
                }
                continue;
            }

            marks[top.pass] = Mark::Done;
            order_.push_back(top.pass);
            stack.pop_back();
        }
    }

    // A present pass goes last whatever the dependency order says, so the
    // screen is written after everything it composites.
    std::stable_partition(
        order_.begin(), order_.end(), [this](PassId id) { return !passes_[id].present; });

    for (PassId id = 0; id < passes_.size(); ++id) {
        lastReader_[id] = id;
    }
    for (std::size_t position = 0; position < order_.size(); ++position) {
        const PassId reader = order_[position];
        for (const PassId input : passes_[reader].inputs) {
            lastReader_[input] = reader;
        }
    }

    return ResultType::ok(order_);
}

PassId FrameGraph::lastReaderOf(PassId pass) const {
    return pass < lastReader_.size() ? lastReader_[pass] : pass;
}

FramebufferHandle FrameGraph::targetOf(PassId pass) const {
    return pass < targets_.size() ? targets_[pass] : FramebufferHandle{};
}

void FrameGraph::execute(Device& device, RenderTargetPool& pool, FramebufferHandle presentTarget) {
    for (const PassId id : order_) {
        const PassDesc& pass = passes_[id];

        FramebufferHandle target = presentTarget;
        if (pass.present) {
            // Never recorded in targets_: the present target belongs to the
            // caller, and returning it to the pool would hand the window's
            // framebuffer to the next pass that asks for one.
            device.bindFramebuffer(presentTarget);
            if (pass.width > 0 && pass.height > 0) {
                device.setViewport(
                    Viewport{.x = 0, .y = 0, .width = pass.width, .height = pass.height});
            }
        } else {
            targets_[id] = pool.acquire(TargetKey{.width = pass.width,
                                                  .height = pass.height,
                                                  .format = pass.format,
                                                  .depth = pass.depth});
            target = targets_[id];
            device.bindFramebuffer(target);
        }

        if (pass.execute) {
            const PassContext context{.device = device,
                                      .target = target,
                                      .width = pass.width,
                                      .height = pass.height,
                                      .graph = this,
                                      .inputs = pass.inputs.data(),
                                      .inputCount = pass.inputs.size()};
            pass.execute(context);
        }

        // Anything whose last reader was this pass is now free to be reused,
        // which is what keeps a six-effect chain down to two framebuffers.
        for (PassId candidate = 0; candidate < passes_.size(); ++candidate) {
            if (lastReader_[candidate] == id && candidate != id && targets_[candidate].valid()) {
                pool.release(targets_[candidate]);
                targets_[candidate] = FramebufferHandle{};
            }
        }
    }

    for (PassId id = 0; id < targets_.size(); ++id) {
        if (targets_[id].valid()) {
            pool.release(targets_[id]);
            targets_[id] = FramebufferHandle{};
        }
    }
}

}  // namespace spectra::gpu
