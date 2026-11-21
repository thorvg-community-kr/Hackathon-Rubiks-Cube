#pragma once

#include <cstdint>
#include <memory>
#include <new>
#include <utility>

#include <thorvg.h>

#include "render/ThorVGRenderer.hpp"

namespace rubiks::render {

/**
 * A ThorVGRenderer drawing into one kind of target.
 *
 * `Target` is what the canvas draws into -- a pixel buffer, a WebGL context,
 * a WebGPU surface -- and says which ThorVG canvas draws into it:
 *
 *   using Canvas = tvg::SwCanvas;             // or GlCanvas, WgCanvas
 *   bool point(Canvas&, width, height);       // targets the canvas at a size
 *   std::uintptr_t pixel_buffer() const;      // zero for a target not in memory
 *   std::uint32_t pixel_byte_length() const;
 *
 * Both are owned here, the target declared first: members go in reverse, so
 * the canvas, which draws into the target, is always destroyed before it.
 */
template <typename Target>
class ThorVGBackend final : public ThorVGRenderer {
public:
    using Canvas = typename Target::Canvas;

    /**
     * Makes the target's kind of canvas and points it at the target.
     *
     * The canvas is made only now, once the target exists: ThorVG's GL
     * engine makes its buffers as its canvas is made, in the context the
     * target has made current.
     *
     * @return nullptr without a target, when ThorVG makes no canvas, or when
     *         the canvas cannot be pointed at the target at this size.
     */
    [[nodiscard]] static std::unique_ptr<ThorVGBackend> create(
        std::unique_ptr<Target> target, std::uint32_t width,
        std::uint32_t height) noexcept
    {
        if (!target) return nullptr;
        std::unique_ptr<ThorVGBackend> renderer(new (std::nothrow)
                                                    ThorVGBackend(std::move(target)));
        if (!renderer || !renderer->canvas_) return nullptr;
        if (!renderer->start(width, height)) return nullptr;
        return renderer;
    }

    [[nodiscard]] std::uintptr_t pixel_buffer() const noexcept override
    {
        return usable() ? target_->pixel_buffer() : 0;
    }

    [[nodiscard]] std::uint32_t pixel_byte_length() const noexcept override
    {
        return usable() ? target_->pixel_byte_length() : 0;
    }

private:
    explicit ThorVGBackend(std::unique_ptr<Target> target) noexcept
        : target_(std::move(target)), canvas_(Canvas::gen())
    {
    }

    [[nodiscard]] tvg::Canvas& canvas() noexcept override { return *canvas_; }

    [[nodiscard]] bool retarget(std::uint32_t width, std::uint32_t height) noexcept override
    {
        return target_->point(*canvas_, width, height);
    }

    // In this order: see the class comment.
    std::unique_ptr<Target> target_;
    std::unique_ptr<Canvas> canvas_;
};

}  // namespace rubiks::render
