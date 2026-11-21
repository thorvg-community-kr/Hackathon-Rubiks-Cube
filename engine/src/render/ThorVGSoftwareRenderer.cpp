#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

#include <thorvg.h>

#include "render/Renderers.hpp"
#include "render/ThorVGBackend.hpp"

namespace rubiks::render {
namespace {

/**
 * A pixel buffer in engine memory, which ThorVG's software rasterizer draws
 * into, in tvg::ColorSpace::ABGR8888S.
 *
 * The buffer stays valid from a successful point() until the next one or
 * destruction. Callers borrow it read-only through pixel_buffer() and
 * pixel_byte_length().
 */
class SoftwareTarget {
public:
    using Canvas = tvg::SwCanvas;

    SoftwareTarget() = default;
    ~SoftwareTarget() { delete[] buffer_; }
    SoftwareTarget(const SoftwareTarget&) = delete;
    SoftwareTarget& operator=(const SoftwareTarget&) = delete;

    /**
     * A new buffer of the size, and the canvas pointed at it. The old buffer
     * survives a failed allocation or a target ThorVG refuses, and is freed
     * once the canvas draws into the new one.
     */
    [[nodiscard]] bool point(tvg::SwCanvas& canvas, std::uint32_t width,
                             std::uint32_t height) noexcept
    {
        // -fno-exceptions: allocation failure must surface as nullptr.
        auto* next = new (std::nothrow)
            std::uint32_t[static_cast<std::size_t>(width) * height];
        if (!next) return false;

        // The stride is a pixel count per row, not a byte count.
        if (canvas.target(next, width, width, height, tvg::ColorSpace::ABGR8888S) !=
            tvg::Result::Success) {
            delete[] next;
            return false;
        }
        delete[] buffer_;
        buffer_ = next;
        byte_length_ = width * height * kBytesPerPixel;
        return true;
    }

    [[nodiscard]] std::uintptr_t pixel_buffer() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(buffer_);
    }

    [[nodiscard]] std::uint32_t pixel_byte_length() const noexcept
    {
        return byte_length_;
    }

private:
    std::uint32_t* buffer_ = nullptr;
    std::uint32_t byte_length_ = 0;
};

}  // namespace

std::unique_ptr<Renderer> create_software_renderer(std::uint32_t width,
                                                   std::uint32_t height) noexcept
{
    return ThorVGBackend<SoftwareTarget>::create(
        std::unique_ptr<SoftwareTarget>(new (std::nothrow) SoftwareTarget()), width,
        height);
}

}  // namespace rubiks::render
