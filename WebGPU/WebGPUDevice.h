#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <atomic>
#include <optional>
#include <string_view>
#include <utility>

namespace webgpu
{
    template <typename T, auto Release>
    class Handle final
    {
    public:
        Handle() noexcept = default;
        explicit Handle(T h) noexcept : handle(h) {}
        ~Handle() { reset(); }

        Handle(Handle&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
        Handle& operator=(Handle&& other) noexcept
        {
            if (this != &other)
                reset(std::exchange(other.handle, nullptr));
            return *this;
        }

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        void reset(T h = nullptr) noexcept
        {
            if (handle != nullptr)
                Release(handle);
            handle = h;
        }

        [[nodiscard]] T get() const noexcept { return handle; }
        [[nodiscard]] T release() noexcept { return std::exchange(handle, nullptr); }
        explicit operator bool() const noexcept { return handle != nullptr; }

    private:
        T handle = nullptr;
    };

    using InstanceHandle = Handle<WGPUInstance, wgpuInstanceRelease>;
    using AdapterHandle = Handle<WGPUAdapter, wgpuAdapterRelease>;
    using DeviceHandle = Handle<WGPUDevice, wgpuDeviceRelease>;
    using QueueHandle = Handle<WGPUQueue, wgpuQueueRelease>;
    using Surface = Handle<WGPUSurface, wgpuSurfaceRelease>;
    using Buffer = Handle<WGPUBuffer, wgpuBufferRelease>;
    using Texture = Handle<WGPUTexture, wgpuTextureRelease>;
    using TextureView = Handle<WGPUTextureView, wgpuTextureViewRelease>;
    using Sampler = Handle<WGPUSampler, wgpuSamplerRelease>;
    using ShaderModule = Handle<WGPUShaderModule, wgpuShaderModuleRelease>;
    using BindGroupLayout = Handle<WGPUBindGroupLayout, wgpuBindGroupLayoutRelease>;
    using BindGroup = Handle<WGPUBindGroup, wgpuBindGroupRelease>;
    using PipelineLayout = Handle<WGPUPipelineLayout, wgpuPipelineLayoutRelease>;
    using RenderPipeline = Handle<WGPURenderPipeline, wgpuRenderPipelineRelease>;
    using ComputePipeline = Handle<WGPUComputePipeline, wgpuComputePipelineRelease>;
    using CommandEncoder = Handle<WGPUCommandEncoder, wgpuCommandEncoderRelease>;
    using CommandBuffer = Handle<WGPUCommandBuffer, wgpuCommandBufferRelease>;
    using RenderPassEncoder = Handle<WGPURenderPassEncoder, wgpuRenderPassEncoderRelease>;

    [[nodiscard]] inline WGPUStringView toStringView(const std::string_view s) noexcept
    {
        return {s.data(), s.size()};
    }

    [[nodiscard]] inline juce::String toString(const WGPUStringView s)
    {
        if (s.data == nullptr)
            return {};
        return juce::String::fromUTF8(s.data, s.length == WGPU_STRLEN ? -1 : static_cast<int>(s.length));
    }

    [[nodiscard]] inline WGPUColor toColor(const juce::Colour c) noexcept
    {
        return {c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), c.getFloatAlpha()};
    }

    class Device final
    {
    public:
        Device();
        ~Device();

        [[nodiscard]] bool isValid() const noexcept { return device && !lost.load(std::memory_order_relaxed); }

        [[nodiscard]] WGPUInstance getInstance() const noexcept { return instance.get(); }
        [[nodiscard]] WGPUAdapter getAdapter() const noexcept { return adapter.get(); }
        [[nodiscard]] WGPUDevice getDevice() const noexcept { return device.get(); }
        [[nodiscard]] WGPUQueue getQueue() const noexcept { return queue.get(); }

        void poll(double frameTime) noexcept;

        void waitIdle() noexcept;

        void pushErrorScope(WGPUErrorFilter filter = WGPUErrorFilter_Validation) noexcept;
        [[nodiscard]] std::optional<juce::String> popErrorScope();

        [[nodiscard]] ShaderModule createShaderModule(std::string_view wgsl, std::string_view label = {}) const;

    private:
        void waitFor(const bool& done) const;

        InstanceHandle instance;
        AdapterHandle adapter;
        DeviceHandle device;
        QueueHandle queue;
        std::atomic<bool> lost{false};
        double lastPollTime = -1.0;

        JUCE_DECLARE_NON_COPYABLE(Device)
    };
}
