#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "WebGPUDevice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <optional>

namespace webgpu
{
    class NativeSurfaceView;

    struct Frame final
    {
        Device& device;
        WGPUCommandEncoder encoder;
        WGPUTextureView target;
        WGPUTextureFormat format;
        int width = 0, height = 0;
        float scale = 1.0f;
        double time = 0.0;
    };

    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual void webgpuContextCreated(Device& device, WGPUTextureFormat format) = 0;

        virtual void renderWebGPU(const Frame& frame) = 0;

        virtual void webgpuContextClosing() = 0;
    };

    class Context final : private juce::ComponentListener, private juce::Timer, private juce::AsyncUpdater
    {
    public:
        enum class Presentation { automatic, nativeSurface, image };

        Context();
        ~Context() override;

        void setRenderer(Renderer* r);
        void setPresentation(Presentation p);
        void setComponentPaintingEnabled(bool shouldPaintComponents);

        void setContinuousRepainting(bool shouldContinuouslyRepaint) noexcept { continuous = shouldContinuouslyRepaint; }

        void setRenderScale(float scale) noexcept { renderScale = std::clamp(scale, 0.05f, 4.0f); }

        void setMaximumFrameRate(double fps) noexcept { maxFrameRate = std::max(fps, 0.0); }

        void attachTo(juce::Component& component);
        void detach();

        [[nodiscard]] bool isAttached() const noexcept { return target != nullptr; }
        [[nodiscard]] bool isActive() const noexcept { return session != nullptr; }
        [[nodiscard]] bool isUsingNativeSurface() const noexcept;

        void triggerRepaint() noexcept { needsRender = true; }

        void paintImage(juce::Graphics& g, juce::Rectangle<float> area) const;

        [[nodiscard]] Device* getDevice() const noexcept;

    private:
        struct Session;

        void componentMovedOrResized(juce::Component&, bool wasMoved, bool wasResized) override;
        void componentVisibilityChanged(juce::Component&) override;
        void componentParentHierarchyChanged(juce::Component&) override;
        void componentBeingDeleted(juce::Component&) override;
        void timerCallback() override { updateActive(); }
        void handleAsyncUpdate() override;

        [[nodiscard]] bool shouldBeActive() const;
        void updateActive();
        void start();
        void stop(bool repaintTarget = true);
        void restart();

        void onVBlank(double time);
        bool renderNative(double time);
        bool renderImage(double time);
        void collectImage();

        juce::Component* target = nullptr;
        Renderer* renderer = nullptr;
        Presentation presentation = Presentation::automatic;
        bool paintComponents = false;
        bool continuous = true;
        float renderScale = 1.0f;
        double maxFrameRate = 0.0;
        bool needsRender = true;
        bool pendingStop = false;
        bool pendingFallback = false;

        std::optional<juce::SharedResourcePointer<Device>> device;
        std::unique_ptr<Session> session;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Context)
    };
}
