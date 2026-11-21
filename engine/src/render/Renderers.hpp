#pragma once

#include <cstdint>
#include <memory>

#include "render/Renderer.hpp"

/**
 * What the application asks of the render layer, with no ThorVG in sight:
 * starting ThorVG, the sizes a target may have, and renderers to draw with --
 * in memory, or on a canvas of the page with the GPU. Which ThorVG canvas
 * draws into what stays behind this header, in render/.
 */
namespace rubiks::render {

/** Largest accepted width or height in pixels. */
inline constexpr std::uint32_t kMaxDimension = 8192;

/** Bytes in one pixel of a target, whatever holds it. */
inline constexpr std::uint32_t kBytesPerPixel = 4;

/**
 * Validates dimensions against zero, kMaxDimension, and the byte size of a
 * buffer that big overflowing. Every renderer is held to it, and so is the
 * application boundary.
 */
[[nodiscard]] bool valid_dimensions(std::uint32_t width, std::uint32_t height) noexcept;

/** Starts ThorVG, before any renderer is made. */
[[nodiscard]] bool start_thorvg() noexcept;

/** Stops ThorVG, once every renderer is gone. */
void stop_thorvg() noexcept;

/**
 * ThorVG's software rasterizer, drawing into a pixel buffer it owns: the
 * renderer every build has -- the native one, the tests', and the page's
 * when it can have no GPU. The buffer stays valid until the next resize()
 * or destruction.
 *
 * @return nullptr when validation, allocation, or ThorVG setup fails.
 */
[[nodiscard]] std::unique_ptr<Renderer> create_software_renderer(
    std::uint32_t width, std::uint32_t height) noexcept;

/** The GPU APIs ThorVG can draw a page's canvas with. */
enum class GpuBackend : std::uint32_t {
    WebGl = 1,
    WebGpu = 2,
};

/** How far asking for a WebGPU device has got. */
enum class GpuDeviceStatus : std::uint32_t {
    Ready = 0,
    Failed = 1,
    Pending = 2,
};

/**
 * Asks the browser for a WebGPU device the first time, and says how far
 * that has got every time.
 *
 * The browser answers asynchronously, so a caller asks again until the
 * answer is Ready or Failed. One device serves every canvas. An adapter that
 * is a GPU emulated in software -- SwiftShader, in Chromium -- is refused:
 * ThorVG 1.1.4's WebGPU engine draws nothing right on one, and WebGL2 on the
 * same machine does. A device the browser later loses is Failed from then
 * on. A build with no WebGPU, the native one included, always answers Failed.
 */
[[nodiscard]] GpuDeviceStatus webgpu_device() noexcept;

/**
 * A GPU renderer for a canvas, or why there is none.
 *
 * A canvas gives a page one kind of context, ever. Everything that can fail
 * is tried before the context is taken, so a refusal normally leaves the
 * canvas free for another backend; `canvas_lost` says the rare refusal came
 * after, and the canvas can be drawn with nothing else.
 */
struct GpuRendererAttempt {
    std::unique_ptr<Renderer> renderer;
    bool canvas_lost = false;
};

/**
 * A renderer that draws straight onto a canvas of the page with the GPU.
 *
 * `selector` names the canvas the way Emscripten finds one: a CSS selector,
 * or a key the page put in Module.specialHTMLTargets. The renderer holds a
 * WebGL2 context on it, or a WebGPU surface (once webgpu_device() is Ready),
 * for as long as it lives, and logs to the console if the browser loses it.
 * resize() points ThorVG at the canvas's new size: the page sizes the
 * canvas's drawing buffer for WebGL, and WebGPU's surface configuration sets
 * it to the same size. Nothing is drawn into memory, so pixel_buffer() is
 * zero and the page copies nothing out.
 *
 * No renderer when this build has no such backend, when the page has no such
 * canvas, or when the browser refuses the context or surface.
 */
[[nodiscard]] GpuRendererAttempt create_gpu_renderer(GpuBackend backend,
                                                     const char* selector,
                                                     std::uint32_t width,
                                                     std::uint32_t height) noexcept;

}  // namespace rubiks::render
