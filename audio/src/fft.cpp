#include <spectra/audio/fft.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#include <pffft/pffft.h>

namespace spectra::audio {
namespace {

/// PFFFT's documented constraint: N = 2^a * 3^b * 5^c with a >= 5.
bool hasSupportedFactors(int size) {
    if (size < 32) {
        return false;
    }
    int remaining = size;
    int twos = 0;
    while (remaining % 2 == 0) {
        remaining /= 2;
        ++twos;
    }
    while (remaining % 3 == 0) {
        remaining /= 3;
    }
    while (remaining % 5 == 0) {
        remaining /= 5;
    }
    return remaining == 1 && twos >= 5;
}

struct AlignedBuffer {
    explicit AlignedBuffer(std::size_t floats)
        : data(static_cast<float*>(pffft_aligned_malloc(floats * sizeof(float)))) {}

    ~AlignedBuffer() { pffft_aligned_free(data); }

    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    AlignedBuffer(AlignedBuffer&&) = delete;
    AlignedBuffer& operator=(AlignedBuffer&&) = delete;

    float* data{nullptr};
};

}  // namespace

struct RealFft::Impl {
    explicit Impl(int size)
        : setup(pffft_new_setup(size, PFFFT_REAL)), input(static_cast<std::size_t>(size)),
          output(static_cast<std::size_t>(size)), work(static_cast<std::size_t>(size)) {}

    ~Impl() {
        if (setup != nullptr) {
            pffft_destroy_setup(setup);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    PFFFT_Setup* setup{nullptr};
    AlignedBuffer input;
    AlignedBuffer output;

    // PFFFT falls back to the stack when handed a null work pointer, and its
    // own header warns that this crashes on worker threads for large N. We
    // run one of these per worker, so the scratch is always heap-allocated.
    AlignedBuffer work;
};

RealFft::RealFft(std::unique_ptr<Impl> impl, int size) : impl_(std::move(impl)), size_(size) {}

RealFft::~RealFft() = default;

RealFft::RealFft(RealFft&&) noexcept = default;

RealFft& RealFft::operator=(RealFft&&) noexcept = default;

bool RealFft::isSupportedSize(int size) {
    return hasSupportedFactors(size);
}

Result<RealFft> RealFft::create(int size) {
    if (!isSupportedSize(size)) {
        return Result<RealFft>::error("FFT size " + std::to_string(size) +
                                      " is not a multiple of 32 of the form 2^a*3^b*5^c");
    }

    auto impl = std::make_unique<Impl>(size);
    if (impl->setup == nullptr || impl->input.data == nullptr || impl->output.data == nullptr ||
        impl->work.data == nullptr) {
        return Result<RealFft>::error("out of memory allocating a " + std::to_string(size) +
                                      "-point FFT");
    }

    return Result<RealFft>::ok(RealFft(std::move(impl), size));
}

void RealFft::transform(std::span<const float> input, std::span<float> magnitudes) const {
    const auto size = static_cast<std::size_t>(size_);
    const auto bins = static_cast<std::size_t>(binCount());
    if (input.size() != size || magnitudes.size() != bins) {
        return;
    }

    std::copy(input.begin(), input.end(), impl_->input.data);
    pffft_transform_ordered(
        impl_->setup, impl_->input.data, impl_->output.data, impl_->work.data, PFFFT_FORWARD);

    // Both purely real bins are packed into the first complex slot, which is
    // the one piece of PFFFT's interface that will bite anyone who assumes
    // the usual layout.
    const float* spectrum = impl_->output.data;
    magnitudes[0] = std::abs(spectrum[0]);
    magnitudes[bins - 1] = std::abs(spectrum[1]);

    for (std::size_t bin = 1; bin + 1 < bins; ++bin) {
        const float real = spectrum[2 * bin];
        const float imaginary = spectrum[2 * bin + 1];
        magnitudes[bin] = std::sqrt(real * real + imaginary * imaginary);
    }
}

}  // namespace spectra::audio
