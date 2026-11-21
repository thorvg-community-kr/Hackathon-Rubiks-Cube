#pragma once

#include <cstdint>

#include <thorvg.h>

#include "graphics/CanvasTheme.hpp"
#include "graphics/Color.hpp"
#include "graphics/RenderScene.hpp"
#include "render/Renderer.hpp"
#include "render/Renderers.hpp"

namespace rubiks::render {

/**
 * Draws a RenderScene with ThorVG, whatever ThorVG draws it into.
 *
 * Everything a frame is made of -- the ground, the shadow, the bodies, the
 * faces and their glints, the lifted pieces, the strokes -- is built here, on
 * ThorVG's common canvas, so every backend draws the same picture. What a
 * backend adds is only its canvas and the target it points it at, which
 * ThorVGBackend (ThorVGBackend.hpp) owns: a pixel buffer in memory
 * (ThorVGSoftwareRenderer.cpp), a WebGL context or a WebGPU surface on the
 * page (ThorVGGpuRenderer.cpp). The canvas contents are rebuilt from the
 * RenderScene on every frame, so no renderer holds scene state between
 * frames.
 */
class ThorVGRenderer : public Renderer {
public:
    ~ThorVGRenderer() override = default;

    ThorVGRenderer(const ThorVGRenderer&) = delete;
    ThorVGRenderer& operator=(const ThorVGRenderer&) = delete;

    /**
     * Points the canvas at a target of a new size, through the backend's
     * retarget(). A target the backend could not give leaves the renderer
     * unusable until it is destroyed and recreated; the only caller treats
     * any resize failure as fatal, so nothing rolls back. The scene arrives
     * with each frame, so a resize touches no scene state.
     */
    [[nodiscard]] bool resize(std::uint32_t width,
                              std::uint32_t height) noexcept final;

    [[nodiscard]] bool render(
        const graphics::RenderScene& scene) noexcept override;

    /**
     * Sets the opaque color every frame after this one is cleared to.
     *
     * Takes effect on the next render rather than immediately: the canvas
     * holds nothing between frames, so there is no painted background here
     * to go back and change.
     */
    void set_background(graphics::Color color) noexcept override;

    /** Zero: a backend drawing into memory says otherwise. */
    [[nodiscard]] std::uintptr_t pixel_buffer() const noexcept override;

    /** Zero: a backend drawing into memory says otherwise. */
    [[nodiscard]] std::uint32_t pixel_byte_length() const noexcept override;

protected:
    ThorVGRenderer() = default;

    /**
     * Points the backend's canvas at a first target of this size. False
     * leaves the renderer unusable.
     */
    [[nodiscard]] bool start(std::uint32_t width, std::uint32_t height) noexcept;

    /** False until there is a target, and after one that could not be kept. */
    [[nodiscard]] bool usable() const noexcept { return usable_; }

private:
    /** The canvas a frame is built on; the backend owns it. */
    [[nodiscard]] virtual tvg::Canvas& canvas() noexcept = 0;

    /**
     * Points the canvas at a target of this size: the one thing a backend
     * does differently. The size has been validated, and is recorded once
     * this answers true.
     */
    [[nodiscard]] virtual bool retarget(std::uint32_t width,
                                        std::uint32_t height) noexcept = 0;

    // The size the target has.
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool usable_ = false;

    /** Replaces the canvas contents with the background and the scene faces. */
    [[nodiscard]] bool rebuild_canvas(
        const graphics::RenderScene& scene) noexcept;

    /**
     * Composes the ground shadow out of ThorVG's own machinery.
     *
     * One shape holding every piece's polygon, filled non-zero so overlaps
     * are inside once and no darker; a gradient fill that is the fade, in the
     * shadow colour already multiplied into the ground's; a Gaussian blur on
     * the scene round it for the edge; a clip to the region; and the cube's
     * silhouette painted back in the ground colour over it, so no shadow
     * shows through a seam. The pipeline decided where the shadow lies; this
     * is only how it is made to look like one, in the one composition layer
     * the blur needs.
     */
    [[nodiscard]] bool draw_shadow(const graphics::RenderShadow& shadow) noexcept;

    /**
     * A storey of lifted pieces as one scene with a drop shadow.
     *
     * The one place the renderer uses ThorVG's DropShadow: a screen-space
     * shadow of a silhouette, which is wrong for a cube standing on a floor
     * and exactly right for a piece held up off a flat drawing.
     */
    [[nodiscard]] bool draw_group(const graphics::RenderGroup& group) noexcept;

    /** The glint on one face: a radial falloff screened onto its outline. */
    [[nodiscard]] bool draw_highlight(
        const graphics::RenderFace& face,
        const graphics::RenderHighlight& highlight) noexcept;

    // Dark by default, which is the ground the renderer has always had and
    // the one the native build with no UI in front of it still wants.
    graphics::Color background_ =
        graphics::canvas_background(graphics::CanvasTheme::Dark);
};

}  // namespace rubiks::render
