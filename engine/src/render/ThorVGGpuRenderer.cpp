#include <cstdint>
#include <new>
#include <utility>

#include "render/Renderers.hpp"
#include "render/ThorVGBackend.hpp"

// The GPU backends exist only in the WebAssembly build: that is the one
// ThorVG's GL and WebGPU engines are compiled into (engine/meson.build), and
// the one with a page whose canvases they draw on.
#ifdef __EMSCRIPTEN__
#include <cstring>
#include <string>

#include <emscripten/console.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>
#endif

namespace rubiks::render {

#ifdef __EMSCRIPTEN__
namespace {

/** Writes a message the browser handed over to the console, as an error. */
void console_error(const char* prefix, WGPUStringView message) noexcept
{
    std::string text(prefix);
    if (message.data != nullptr) {
        text.append(message.data, message.length == WGPU_STRLEN
                                      ? std::strlen(message.data)
                                      : message.length);
    }
    emscripten_console_error(text.c_str());
}

/**
 * The one WebGPU device every canvas is drawn with, and how far asking the
 * browser for it has got. The browser answers through callbacks that may
 * run at any later point, so each step leaves its result here for the next.
 */
struct WebGpuDevice {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    GpuDeviceStatus status = GpuDeviceStatus::Failed;
    bool asked = false;
};

WebGpuDevice& webgpu() noexcept
{
    static WebGpuDevice shared;
    return shared;
}

/**
 * What the device reports and nobody caught: the only place a WebGPU
 * validation error would otherwise show is a later "invalid due to a
 * previous error" with the cause left out.
 */
void on_device_error(WGPUDevice const*, WGPUErrorType, WGPUStringView message,
                     void*, void*) noexcept
{
    console_error("WebGPU: ", message);
}

/** A device the browser takes back draws nothing from then on. */
void on_device_lost(WGPUDevice const*, WGPUDeviceLostReason reason,
                    WGPUStringView message, void*, void*) noexcept
{
    // Losing it at the page's own teardown is no news.
    if (reason == WGPUDeviceLostReason_Destroyed) return;
    webgpu().status = GpuDeviceStatus::Failed;
    console_error("WebGPU device lost: ", message);
}

void on_device(WGPURequestDeviceStatus status, WGPUDevice device,
               WGPUStringView, void*, void*) noexcept
{
    auto& shared = webgpu();
    if (status != WGPURequestDeviceStatus_Success || device == nullptr) {
        shared.status = GpuDeviceStatus::Failed;
        return;
    }
    shared.device = device;
    shared.status = GpuDeviceStatus::Ready;
}

void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                WGPUStringView, void*, void*) noexcept
{
    auto& shared = webgpu();
    if (status != WGPURequestAdapterStatus_Success || adapter == nullptr) {
        shared.status = GpuDeviceStatus::Failed;
        return;
    }

    // A fallback adapter, which the browser reports as a CPU one, is a GPU
    // emulated in software; see webgpu_device().
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    const bool emulated = wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success &&
                          info.adapterType == WGPUAdapterType_CPU;
    wgpuAdapterInfoFreeMembers(info);
    if (emulated) {
        wgpuAdapterRelease(adapter);
        shared.status = GpuDeviceStatus::Failed;
        return;
    }
    shared.adapter = adapter;

    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo.callback = on_device_error;
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    descriptor.deviceLostCallbackInfo.callback = on_device_lost;
    WGPURequestDeviceCallbackInfo callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = on_device;
    wgpuAdapterRequestDevice(adapter, &descriptor, callback);
}

/** A WebGL context the browser takes back leaves its canvas blank. */
bool on_context_lost(int, const void*, void*) noexcept
{
    emscripten_console_error("WebGL context lost: the canvas is drawn no more.");
    return false;
}

/** A target on a canvas of the page: the frame shows there, nothing is in memory. */
class PageCanvasTarget {
public:
    PageCanvasTarget() = default;
    PageCanvasTarget(const PageCanvasTarget&) = delete;
    PageCanvasTarget& operator=(const PageCanvasTarget&) = delete;

    [[nodiscard]] std::uintptr_t pixel_buffer() const noexcept { return 0; }
    [[nodiscard]] std::uint32_t pixel_byte_length() const noexcept { return 0; }
};

/** A WebGL2 context on one canvas, which ThorVG's GL engine draws into. */
struct GlTarget : PageCanvasTarget {
    using Canvas = tvg::GlCanvas;

    ~GlTarget()
    {
        if (context > 0) emscripten_webgl_destroy_context(context);
    }

    /** Points the canvas at the context's default framebuffer. */
    [[nodiscard]] bool point(tvg::GlCanvas& canvas, std::uint32_t width,
                             std::uint32_t height) const noexcept
    {
        return canvas.target(nullptr, nullptr, reinterpret_cast<void*>(context), 0, width,
                             height, tvg::ColorSpace::ABGR8888S) == tvg::Result::Success;
    }

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
};

/** A WebGPU surface on one canvas, which ThorVG's WebGPU engine draws into. */
struct WgTarget : PageCanvasTarget {
    using Canvas = tvg::WgCanvas;

    ~WgTarget()
    {
        if (surface != nullptr) wgpuSurfaceRelease(surface);
    }

    /**
     * Points the canvas at the surface, which ThorVG configures at the size.
     *
     * The overload without the adapter: given one, ThorVG 1.1.4 asks the
     * surface for its capabilities with a WGPUSurfaceCapabilities it leaves
     * uninitialized, and Emscripten's WebGPU port aborts on the garbage in
     * its nextInChain. Without one ThorVG skips the question and takes the
     * premultiplied alpha mode a web surface has anyway.
     */
    [[nodiscard]] bool point(tvg::WgCanvas& canvas, std::uint32_t width,
                             std::uint32_t height) const noexcept
    {
        const auto& shared = webgpu();
        return canvas.target(shared.device, shared.instance, surface, width, height,
                             tvg::ColorSpace::ABGR8888, 0) == tvg::Result::Success;
    }

    WGPUSurface surface = nullptr;
};

/**
 * A renderer drawing with WebGL2 on `selector`'s canvas.
 *
 * The attributes are the ones ThorVG's own web canvas asks for: its GL engine
 * keeps its depth and stencil in its own targets, and draws the frame's
 * ground itself, alpha and all.
 */
GpuRendererAttempt create_gl_renderer(const char* selector, std::uint32_t width,
                                      std::uint32_t height) noexcept
{
    GpuRendererAttempt attempt;
    if (!valid_dimensions(width, height)) return attempt;
    std::unique_ptr<GlTarget> target(new (std::nothrow) GlTarget());
    if (!target) return attempt;

    EmscriptenWebGLContextAttributes attributes;
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.alpha = true;
    attributes.depth = false;
    attributes.stencil = false;
    attributes.premultipliedAlpha = true;
    attributes.failIfMajorPerformanceCaveat = false;
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;
    attributes.enableExtensionsByDefault = true;
    target->context = emscripten_webgl_create_context(selector, &attributes);
    if (target->context <= 0) return attempt;
    emscripten_set_webglcontextlost_callback(selector, nullptr, false, on_context_lost);

    // The canvas has its context from here, so any failure loses it.
    attempt.canvas_lost = true;
    if (emscripten_webgl_make_context_current(target->context) !=
        EMSCRIPTEN_RESULT_SUCCESS) {
        return attempt;
    }
    attempt.renderer = ThorVGBackend<GlTarget>::create(std::move(target), width, height);
    attempt.canvas_lost = !attempt.renderer;
    return attempt;
}

/** A renderer drawing with WebGPU on `selector`'s canvas, once the device is Ready. */
GpuRendererAttempt create_webgpu_renderer(const char* selector, std::uint32_t width,
                                          std::uint32_t height) noexcept
{
    GpuRendererAttempt attempt;
    if (!valid_dimensions(width, height)) return attempt;
    if (webgpu().status != GpuDeviceStatus::Ready) return attempt;
    std::unique_ptr<WgTarget> target(new (std::nothrow) WgTarget());
    if (!target) return attempt;

    // Making the surface takes the canvas's WebGPU context.
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source =
        WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    source.selector = WGPUStringView{selector, WGPU_STRLEN};
    WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &source.chain;
    target->surface = wgpuInstanceCreateSurface(webgpu().instance, &descriptor);
    if (target->surface == nullptr) return attempt;

    attempt.renderer = ThorVGBackend<WgTarget>::create(std::move(target), width, height);
    attempt.canvas_lost = !attempt.renderer;
    return attempt;
}

}  // namespace

GpuDeviceStatus webgpu_device() noexcept
{
    auto& shared = webgpu();
    if (shared.asked) return shared.status;
    shared.asked = true;

    shared.instance = wgpuCreateInstance(nullptr);
    if (shared.instance == nullptr) {
        shared.status = GpuDeviceStatus::Failed;
        return shared.status;
    }
    shared.status = GpuDeviceStatus::Pending;

    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.callback = on_adapter;
    wgpuInstanceRequestAdapter(shared.instance, &options, callback);
    return shared.status;
}

GpuRendererAttempt create_gpu_renderer(GpuBackend backend, const char* selector,
                                       std::uint32_t width, std::uint32_t height) noexcept
{
    if (selector == nullptr) return {};
    switch (backend) {
        case GpuBackend::WebGl:
            return create_gl_renderer(selector, width, height);
        case GpuBackend::WebGpu:
            return create_webgpu_renderer(selector, width, height);
    }
    return {};
}

#else

GpuDeviceStatus webgpu_device() noexcept
{
    return GpuDeviceStatus::Failed;
}

GpuRendererAttempt create_gpu_renderer(GpuBackend, const char*, std::uint32_t,
                                       std::uint32_t) noexcept
{
    return {};
}

#endif

}  // namespace rubiks::render
