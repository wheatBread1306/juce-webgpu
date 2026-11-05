# juce-webgpu

[日本語版](./README-ja.md)

This is a small CMake library for rendering WebGPU content in a JUCE
`Component`. It uses [wgpu-native](https://github.com/gfx-rs/wgpu-native) as
the rendering backend.

## Features

- Render into a JUCE component through `webgpu::Context`.
- `webgpu::Renderer` callbacks are invoked on JUCE's message thread.
- Choose between a native surface (a child `HWND` on Windows or a
  `CAMetalLayer` on macOS) and offscreen rendering with readback to a JUCE
  `Image`.
- With `automatic`, `image` is selected when JUCE child components must be
  painted; otherwise `nativeSurface` is preferred.
- `webgpu::Device` is shared in-process through `juce::SharedResourcePointer`.
- wgpu-native is downloaded with CMake `FetchContent` and linked statically,
  so no DLL or dylib has to be shipped with the plugin.

## Requirements

- CMake 3.28 or later
- C++20 or later
- JUCE (tested with 9.0.0). The consuming target must link
  `juce::juce_gui_extra`, because the native surfaces are built on
  `juce::HWNDComponent` (Windows) and `juce::NSViewComponent` (macOS).

`juce_webgpu` is an `INTERFACE` library: its sources are compiled as part of
the consuming target, with that target's JUCE configuration and definitions.
It does not link JUCE by itself, so JUCE must be linked by the consumer.
Do not turn it into a `STATIC` library that links JUCE modules; the JUCE
module sources would be compiled twice.

## Supported platforms

| Platform | Native surface | Architecture | Status |
| --- | --- | --- | --- |
| Windows | Child `HWND` | x86_64, ARM64 | Tested on x86_64; ARM64 untested |
| macOS | `CAMetalLayer` | x86_64, arm64 | Builds as universal binary; runtime untested |
| Linux | None (`image` only) | x86_64, aarch64 | Experimental; untested |

On Linux, `NativeSurfaceView::create()` returns `nullptr`, so `automatic`
falls back to `image` presentation.

## Integration

### CMake

The following example adds this repository with `FetchContent`. Pin
`GIT_TAG` to a release tag or a commit hash rather than a branch, so builds
stay reproducible (the wgpu-native C API changes between major releases).

```cmake
include(FetchContent)

FetchContent_Declare(
    juce_webgpu
    GIT_REPOSITORY https://github.com/wheatBread1306/juce-webgpu.git
    GIT_TAG        v0.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(juce_webgpu)

target_link_libraries(MyPlugin
    PRIVATE
        juce::juce_gui_extra
        juce_webgpu::juce_webgpu
)
```

On macOS, enable Objective-C++ in the consuming top-level CMake project:

```cmake
if(APPLE)
    enable_language(OBJCXX)
endif()
```

The wgpu-native release can be changed with:

```sh
cmake -DJUCE_WEBGPU_WGPU_TAG=v29.0.1.1 ...
```

### Local development

To work on a local checkout while keeping the `FetchContent` setup, point
FetchContent at it instead of downloading:

```sh
cmake -DFETCHCONTENT_SOURCE_DIR_JUCE_WEBGPU=path/to/juce-webgpu ...
```

Alternatively, add it as a subdirectory:

```cmake
add_subdirectory(path/to/juce-webgpu)

target_link_libraries(MyPlugin
    PRIVATE
        juce::juce_gui_extra
        juce_webgpu::juce_webgpu
)
```

### Include

```cpp
#include <WebGPU/WebGPUContext.h>
```

## Quick start

Implement `webgpu::Renderer`, assign it to a `webgpu::Context`, and attach the
context to a visible JUCE component. Create device-dependent resources in
`webgpuContextCreated()`, encode commands into `frame.encoder` targeting
`frame.target` in `renderWebGPU()`, and release those resources in
`webgpuContextClosing()`.

The example below draws a single triangle.

```cpp
class MyRenderer final : public webgpu::Renderer
{
public:
    void webgpuContextCreated(webgpu::Device& device,
                              WGPUTextureFormat format) override
    {
        const auto module = device.createShaderModule(shaderSource, "Triangle");

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = format;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = module.get();
        fragment.entryPoint = webgpu::toStringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.vertex.module = module.get();
        desc.vertex.entryPoint = webgpu::toStringView("vs_main");
        desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        desc.fragment = &fragment;

        pipeline.reset(wgpuDeviceCreateRenderPipeline(device.getDevice(), &desc));
    }

    void renderWebGPU(const webgpu::Frame& frame) override
    {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = frame.target;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = {0.08, 0.10, 0.14, 1.0};

        WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        desc.colorAttachmentCount = 1;
        desc.colorAttachments = &color;

        const webgpu::RenderPassEncoder pass(
            wgpuCommandEncoderBeginRenderPass(frame.encoder, &desc));

        if (pipeline)
        {
            wgpuRenderPassEncoderSetPipeline(pass.get(), pipeline.get());
            wgpuRenderPassEncoderDraw(pass.get(), 3, 1, 0, 0);
        }

        wgpuRenderPassEncoderEnd(pass.get());
    }

    void webgpuContextClosing() override
    {
        pipeline.reset();
    }

private:
    webgpu::RenderPipeline pipeline;

    static constexpr std::string_view shaderSource = R"(
@vertex
fn vs_main(@builtin(vertex_index) index : u32) -> @builtin(position) vec4f {
    var positions = array<vec2f, 3>(
        vec2f(0.0, 0.6),
        vec2f(-0.6, -0.6),
        vec2f(0.6, -0.6)
    );
    return vec4f(positions[index], 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
    return vec4f(1.0, 0.55, 0.2, 1.0);
}
)";
};

class MyComponent final : public juce::Component
{
public:
    MyComponent()
    {
        context.setRenderer(&renderer);
        context.setPresentation(webgpu::Context::Presentation::automatic);
        context.attachTo(*this);
    }

    ~MyComponent() override
    {
        context.detach();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black);
        context.paintImage(g, getLocalBounds().toFloat());
    }

private:
    MyRenderer renderer;
    webgpu::Context context;
};
```

To catch WGSL or pipeline validation errors, wrap resource creation in
`device.pushErrorScope()` / `device.popErrorScope()`; invalid shaders still
return a non-null handle, and the error is reported through the scope.

Call `paintImage()` from the component's `paint()` method when using
`image` presentation. Native presentation draws directly into a child native
view, so JUCE child components in the same area appear underneath it.

The context starts and stops rendering automatically as the target becomes
visible or hidden. Call `context.detach()` before destroying the target or
renderer.

## Main settings

```cpp
context.setPresentation(webgpu::Context::Presentation::nativeSurface);
context.setComponentPaintingEnabled(true);
context.setContinuousRepainting(false);
context.setMaximumFrameRate(60.0);
context.setRenderScale(0.5f);
context.triggerRepaint();
```

- `setPresentation()` accepts `automatic`, `nativeSurface`, or `image`.
- `setComponentPaintingEnabled(true)` makes `automatic` choose `image`.
- With `setContinuousRepainting(false)`, rendering occurs only after
  `triggerRepaint()`.
- `setMaximumFrameRate(0.0)` means no limit; `setRenderScale()` is clamped to
  `0.05` through `4.0`.
- A context is active only while its target is visible, non-zero-sized, and not
  minimized.
- Use `Context` and `Renderer` from JUCE's message thread.

## Notes

- Handles in `Frame`, including `encoder` and `target`, are valid only during
  the `renderWebGPU()` callback.
- `renderWebGPU()` is expected to clear the complete render target with
  `WGPULoadOp_Clear`.
- `Frame::scale` is the physical-pixel scale relative to logical pixels.
- The adapter is requested with `WGPUPowerPreference_LowPower`, so on systems
  with both integrated and discrete GPUs the integrated GPU is normally used.
- If `webgpu::Device` cannot be created, no rendering is performed and the
  caller's background remains visible.
- If a native surface cannot be used, the context falls back to `image`.
- `image` presentation reads pixels back from the GPU to the CPU, so it is more
  expensive than native presentation and normally introduces one frame of
  latency.

## Testing and feedback

Testing and feedback on Windows, macOS, and Linux are welcome. Reports from
different GPUs, architectures, and JUCE host environments are especially
valuable. When opening an issue, please include the OS, CPU/GPU (including
which GPU was selected on multi-GPU systems), JUCE version, build
environment, reproduction steps, and any relevant error messages where
possible.

## License

The original source code in this project is provided under the MIT License.
See [LICENSE](./LICENSE) for the full text.

JUCE is not relicensed by this project. JUCE is provided under either the
AGPLv3 or the JUCE Licence; products using JUCE must comply with the applicable
JUCE licensing terms.

wgpu-native and other external dependencies remain subject to their own
license terms. Review the licenses of all dependencies before redistribution.
