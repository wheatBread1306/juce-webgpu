#include "WebGPUContext.h"
#include "WebGPUNativeSurface.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>

namespace webgpu
{
#if !JUCE_WINDOWS && !JUCE_MAC
    std::unique_ptr<NativeSurfaceView> NativeSurfaceView::create() { return nullptr; }
#endif

    namespace
    {
        constexpr auto imageFormat = WGPUTextureFormat_BGRA8Unorm;

        [[nodiscard]] uint32_t alignedBytesPerRow(const int width) noexcept
        {
            return (static_cast<uint32_t>(width) * 4u + 255u) & ~255u;
        }

        [[nodiscard]] bool isSrgb(const WGPUTextureFormat f) noexcept
        {
            return f == WGPUTextureFormat_BGRA8UnormSrgb || f == WGPUTextureFormat_RGBA8UnormSrgb;
        }
    }

    struct Context::Session final
    {
        explicit Session(Device& d) : device(d) {}

        Device& device;
        WGPUTextureFormat format = imageFormat;
        bool native = false;

        std::unique_ptr<NativeSurfaceView> view;
        Surface surface;
        WGPUPresentMode presentMode = WGPUPresentMode_Fifo;
        WGPUCompositeAlphaMode alphaMode = WGPUCompositeAlphaMode_Auto;
        juce::Point<int> surfaceSize;

        enum class SlotState : int { idle, pending, ready };

        struct Readback final
        {
            Buffer buffer;
            std::atomic<SlotState> state{SlotState::idle};
            std::uint64_t frame = 0;
        };

        std::array<Readback, 3> readbacks;
        Texture texture;
        TextureView textureView;
        juce::Point<int> imageSize;
        uint32_t bytesPerRow = 0;
        juce::Image image;
        std::uint64_t frameCounter = 0, shownFrame = 0;

        juce::VBlankAttachment vblank;
        double lastRenderTime = -1.0e9;

        void releaseImageTargets() noexcept
        {
            device.waitIdle();
            for (auto& r : readbacks)
            {
                if (r.state.load() == SlotState::ready)
                    wgpuBufferUnmap(r.buffer.get());
                r.buffer.reset();
                r.state.store(SlotState::idle);
            }
            textureView.reset();
            texture.reset();
            imageSize = {};
        }

        ~Session()
        {
            vblank = {};
            if (!native)
                releaseImageTargets();
            surface.reset();
            view.reset();
        }
    };

    Context::Context() = default;

    Context::~Context()
    {
        detach();
    }

    void Context::setRenderer(Renderer* r)
    {
        if (renderer == r)
            return;
        stop();
        renderer = r;
        updateActive();
    }

    void Context::setPresentation(const Presentation p)
    {
        if (presentation == p)
            return;
        presentation = p;
        restart();
    }

    void Context::setComponentPaintingEnabled(const bool shouldPaintComponents)
    {
        if (paintComponents == shouldPaintComponents)
            return;
        paintComponents = shouldPaintComponents;
        restart();
    }

    void Context::attachTo(juce::Component& component)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        if (target == &component)
            return;

        detach();
        target = &component;
        target->addComponentListener(this);
        startTimerHz(4);
        updateActive();
    }

    void Context::detach()
    {
        JUCE_ASSERT_MESSAGE_THREAD
        stopTimer();
        cancelPendingUpdate();
        pendingStop = pendingFallback = false;
        stop(false);
        if (target != nullptr)
            target->removeComponentListener(this);
        target = nullptr;
        device.reset();
    }

    bool Context::isUsingNativeSurface() const noexcept
    {
        return session != nullptr && session->native;
    }

    Device* Context::getDevice() const noexcept
    {
        return session != nullptr ? &session->device : nullptr;
    }

    bool Context::shouldBeActive() const
    {
        if (target == nullptr || renderer == nullptr || !target->isShowing() || target->getWidth() <= 0 || target->getHeight() <= 0)
            return false;
        const auto* peer = target->getPeer();
        return peer != nullptr && !peer->isMinimised();
    }

    void Context::updateActive()
    {
        if (const bool want = shouldBeActive(); want != isActive())
        {
            if (want)
                start();
            else
                stop();
        }
    }

    void Context::restart()
    {
        if (isActive())
        {
            stop();
            updateActive();
        }
    }

    void Context::start()
    {
        jassert(session == nullptr && target != nullptr && renderer != nullptr);

        if (!device)
            device.emplace();
        auto& dev = device->get();
        if (!dev.isValid())
            return;

        auto s = std::make_unique<Session>(dev);

        const bool wantNative = presentation == Presentation::nativeSurface
                                || (presentation == Presentation::automatic && !paintComponents);
        if (wantNative)
        {
            if (auto view = NativeSurfaceView::create())
            {
                target->addAndMakeVisible(*view);
                view->setBounds(target->getLocalBounds());

                if (auto surface = view->createSurface(dev.getInstance()))
                {
                    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
                    if (wgpuSurfaceGetCapabilities(surface.get(), dev.getAdapter(), &caps) == WGPUStatus_Success && caps.formatCount > 0)
                    {
                        const auto formats = std::span(caps.formats, caps.formatCount);
                        auto format = formats[0];
                        for (const auto f : formats)
                            if (f == WGPUTextureFormat_BGRA8Unorm || f == WGPUTextureFormat_RGBA8Unorm)
                            {
                                format = f;
                                break;
                            }
                        if (isSrgb(format))
                            DBG("webgpu::Context: only an sRGB surface format is available; colours will differ");

                        const auto modes = std::span(caps.presentModes, caps.presentModeCount);
                        s->presentMode = std::ranges::find(modes, WGPUPresentMode_Mailbox) != modes.end() ? WGPUPresentMode_Mailbox : WGPUPresentMode_Fifo;

                        const auto alphas = std::span(caps.alphaModes, caps.alphaModeCount);
                        s->alphaMode = std::ranges::find(alphas, WGPUCompositeAlphaMode_Opaque) != alphas.end() ? WGPUCompositeAlphaMode_Opaque : WGPUCompositeAlphaMode_Auto;

                        s->format = format;
                        s->surface = std::move(surface);
                        s->view = std::move(view);
                        s->native = true;
                    }
                    wgpuSurfaceCapabilitiesFreeMembers(caps);
                }
            }

            if (!s->native)
                DBG("webgpu::Context: native surface unavailable, falling back to image presentation");
        }

        session = std::move(s);
        renderer->webgpuContextCreated(dev, session->format);

        needsRender = true;
        session->vblank = juce::VBlankAttachment(target, [this](const double t) { onVBlank(t); });
    }

    void Context::stop(const bool repaintTarget)
    {
        if (session == nullptr)
            return;

        session->vblank = {};
        if (renderer != nullptr)
            renderer->webgpuContextClosing();
        session.reset();

        if (repaintTarget && target != nullptr)
            target->repaint();
    }

    void Context::handleAsyncUpdate()
    {
        if (pendingFallback)
        {
            pendingFallback = false;
            presentation = Presentation::image;
            restart();
        }
        if (pendingStop)
        {
            pendingStop = false;
            stop();
        }
    }

    void Context::componentMovedOrResized(juce::Component&, bool, const bool wasResized)
    {
        if (!wasResized)
            return;

        if (session != nullptr && session->view != nullptr)
            session->view->setBounds(target->getLocalBounds());
        needsRender = true;
        updateActive();
    }

    void Context::componentVisibilityChanged(juce::Component&) { updateActive(); }
    void Context::componentParentHierarchyChanged(juce::Component&) { updateActive(); }

    void Context::componentBeingDeleted(juce::Component&)
    {
        detach();
    }

    void Context::onVBlank(const double time)
    {
        if (session == nullptr)
            return;

        if (pendingStop || pendingFallback)
            return;

        session->device.poll(time);
        if (!session->device.isValid())
        {
            pendingStop = true;
            triggerAsyncUpdate();
            return;
        }

        if (!session->native)
            collectImage();

        if (!continuous && !needsRender)
            return;
        if (maxFrameRate > 0.0 && time - session->lastRenderTime < 1.0 / maxFrameRate - 0.002)
            return;
        if (!target->isShowing())
            return;

        if (session->native ? renderNative(time) : renderImage(time))
        {
            session->lastRenderTime = time;
            needsRender = false;
        }
    }

    bool Context::renderNative(const double time)
    {
        auto& s = *session;
        auto& dev = s.device;

        const auto size = s.view->getPixelSize();
        if (size.x <= 0 || size.y <= 0)
            return false;

        if (size != s.surfaceSize)
        {
            WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
            config.device = dev.getDevice();
            config.format = s.format;
            config.usage = WGPUTextureUsage_RenderAttachment;
            config.width = static_cast<uint32_t>(size.x);
            config.height = static_cast<uint32_t>(size.y);
            config.alphaMode = s.alphaMode;
            config.presentMode = s.presentMode;
            wgpuSurfaceConfigure(s.surface.get(), &config);
            s.surfaceSize = size;
        }

        WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(s.surface.get(), &surfaceTexture);
        Texture texture(surfaceTexture.texture);

        switch (surfaceTexture.status)
        {
            case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
                break;
            case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
                s.surfaceSize = {};
                break;
            case WGPUSurfaceGetCurrentTextureStatus_Timeout:
                return false;
            case WGPUSurfaceGetCurrentTextureStatus_Outdated:
            case WGPUSurfaceGetCurrentTextureStatus_Lost:
                s.surfaceSize = {};
                return false;
            default:
                DBG("webgpu::Context: surface error, falling back to image presentation");
                pendingFallback = true;
                triggerAsyncUpdate();
                return false;
        }

        TextureView view(wgpuTextureCreateView(texture.get(), nullptr));
        CommandEncoder encoder(wgpuDeviceCreateCommandEncoder(dev.getDevice(), nullptr));

        const Frame frame{dev, encoder.get(), view.get(), s.format, size.x, size.y,
                          static_cast<float>(size.x) / static_cast<float>(std::max(target->getWidth(), 1)), time};
        renderer->renderWebGPU(frame);

        CommandBuffer commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
        const auto commandBuffer = commands.get();
        wgpuQueueSubmit(dev.getQueue(), 1, &commandBuffer);
        wgpuSurfacePresent(s.surface.get());
        return true;
    }

    bool Context::renderImage(const double time)
    {
        auto& s = *session;
        auto& dev = s.device;

        const auto scale = juce::Component::getApproximateScaleFactorForComponent(target) * renderScale;
        const juce::Point<int> size{static_cast<int>(std::lround(static_cast<float>(target->getWidth()) * scale)),
                               static_cast<int>(std::lround(static_cast<float>(target->getHeight()) * scale))};
        if (size.x <= 0 || size.y <= 0)
            return false;

        if (size != s.imageSize)
        {
            s.releaseImageTargets();

            WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
            td.label = toStringView("webgpu::Context target");
            td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
            td.dimension = WGPUTextureDimension_2D;
            td.size = {static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y), 1};
            td.format = imageFormat;
            s.texture.reset(wgpuDeviceCreateTexture(dev.getDevice(), &td));
            s.textureView.reset(wgpuTextureCreateView(s.texture.get(), nullptr));

            s.bytesPerRow = alignedBytesPerRow(size.x);
            WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
            bd.label = toStringView("webgpu::Context readback");
            bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
            bd.size = static_cast<uint64_t>(s.bytesPerRow) * static_cast<uint64_t>(size.y);
            for (auto& r : s.readbacks)
                r.buffer.reset(wgpuDeviceCreateBuffer(dev.getDevice(), &bd));

            s.imageSize = size;
        }

        const auto slot = std::ranges::find_if(s.readbacks, [](const Session::Readback& r)
        {
            return r.state.load(std::memory_order_acquire) == Session::SlotState::idle;
        });
        if (slot == s.readbacks.end())
            return false;

        CommandEncoder encoder(wgpuDeviceCreateCommandEncoder(dev.getDevice(), nullptr));

        const Frame frame{dev, encoder.get(), s.textureView.get(), imageFormat, size.x, size.y, scale, time};
        renderer->renderWebGPU(frame);

        WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        src.texture = s.texture.get();
        src.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dst.buffer = slot->buffer.get();
        dst.layout.bytesPerRow = s.bytesPerRow;
        dst.layout.rowsPerImage = static_cast<uint32_t>(size.y);
        const WGPUExtent3D extent{static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y), 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder.get(), &src, &dst, &extent);

        CommandBuffer commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
        const auto commandBuffer = commands.get();
        wgpuQueueSubmit(dev.getQueue(), 1, &commandBuffer);

        slot->frame = ++s.frameCounter;
        slot->state.store(Session::SlotState::pending, std::memory_order_release);

        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.callback = [](const WGPUMapAsyncStatus status, WGPUStringView, void* userdata, void*)
        {
            static_cast<Session::Readback*>(userdata)->state.store(status == WGPUMapAsyncStatus_Success ? Session::SlotState::ready
                                                                                                         : Session::SlotState::idle,
                                                                   std::memory_order_release);
        };
        callback.userdata1 = &*slot;
        wgpuBufferMapAsync(slot->buffer.get(), WGPUMapMode_Read, 0, static_cast<size_t>(s.bytesPerRow) * static_cast<size_t>(size.y), callback);
        return true;
    }

    void Context::collectImage()
    {
        auto& s = *session;

        Session::Readback* newest = nullptr;
        for (auto& r : s.readbacks)
        {
            if (r.state.load(std::memory_order_acquire) != Session::SlotState::ready)
                continue;
            if (newest == nullptr || r.frame > newest->frame)
            {
                if (newest != nullptr)
                {
                    wgpuBufferUnmap(newest->buffer.get());
                    newest->state.store(Session::SlotState::idle, std::memory_order_release);
                }
                newest = &r;
            }
            else
            {
                wgpuBufferUnmap(r.buffer.get());
                r.state.store(Session::SlotState::idle, std::memory_order_release);
            }
        }

        if (newest == nullptr)
            return;

        const auto w = s.imageSize.x, h = s.imageSize.y;
        if (newest->frame > s.shownFrame && w > 0 && h > 0)
        {
            const auto size = static_cast<size_t>(s.bytesPerRow) * static_cast<size_t>(h);
            if (const auto* src = static_cast<const juce::uint8*>(wgpuBufferGetConstMappedRange(newest->buffer.get(), 0, size)))
            {
                if (!s.image.isValid() || s.image.getWidth() != w || s.image.getHeight() != h)
                    s.image = juce::Image(juce::Image::ARGB, w, h, false);

                {
                    const juce::Image::BitmapData bitmap(s.image, juce::Image::BitmapData::writeOnly);
                    const auto rowBytes = static_cast<size_t>(w) * 4u;
                    if (bitmap.pixelStride == 4)
                        for (int y = 0; y < h; ++y)
                            std::memcpy(bitmap.getLinePointer(y), src + static_cast<size_t>(y) * s.bytesPerRow, rowBytes);
                }

                s.shownFrame = newest->frame;
                target->repaint();
            }
        }

        wgpuBufferUnmap(newest->buffer.get());
        newest->state.store(Session::SlotState::idle, std::memory_order_release);
    }

    void Context::paintImage(juce::Graphics& g, const juce::Rectangle<float> area) const
    {
        if (session == nullptr || session->native || !session->image.isValid())
            return;
        g.drawImage(session->image, area, juce::RectanglePlacement::stretchToFit);
    }
}
