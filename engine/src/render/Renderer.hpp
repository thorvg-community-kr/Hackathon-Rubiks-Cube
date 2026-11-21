#pragma once

#include <cstdint>

#include "graphics/Color.hpp"
#include "graphics/RenderScene.hpp"

namespace rubiks::render {

/**
 * Rendering boundary owned by the application.
 *
 * Implementations own their render target: a pixel buffer in memory, or a
 * canvas of the page drawn with the GPU. The application never observes how
 * the target is allocated or what its pixel format is through this
 * interface, and it borrows a pixel buffer's address and byte length only to
 * pass them on to its own caller, which copies the frame out of it. What it
 * gives a renderer is a finished 2D RenderScene rather than anything 3D.
 */
class Renderer {
public:
    virtual ~Renderer() = default;

    /**
     * Resizes the render target.
     *
     * @return true when the target uses the new size afterwards.
     */
    [[nodiscard]] virtual bool resize(std::uint32_t width,
                                      std::uint32_t height) noexcept = 0;

    /**
     * Renders one frame into the render target.
     *
     * Faces are drawn in the order given, which the pipeline has already
     * sorted back to front.
     *
     * @return true when the frame fully reached the target.
     */
    [[nodiscard]] virtual bool render(
        const graphics::RenderScene& scene) noexcept = 0;

    /**
     * Sets the opaque background the next frames are cleared to.
     *
     * On the boundary rather than on one backend, because the application
     * owns its renderer through this interface and could not reach a method
     * a concrete class kept to itself.
     *
     * The renderer is told a color rather than a theme: a theme is the
     * language of presentation, and a renderer is the tool that paints a
     * target.
     */
    virtual void set_background(graphics::Color color) noexcept = 0;

    /**
     * Returns the address of the CPU-visible pixel buffer, or zero for a
     * target that is not in memory -- a canvas the GPU draws on, which shows
     * its frame without being copied.
     */
    [[nodiscard]] virtual std::uintptr_t pixel_buffer() const noexcept = 0;

    /** Returns the byte length of the CPU-visible pixel buffer; zero without one. */
    [[nodiscard]] virtual std::uint32_t pixel_byte_length() const noexcept = 0;
};

}  // namespace rubiks::render
