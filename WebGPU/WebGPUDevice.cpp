#include "WebGPUDevice.h"

namespace webgpu
{
    Device::Device()
    {
       #if JUCE_DEBUG
        wgpuSetLogLevel(WGPULogLevel_Warn);
        wgpuSetLogCallback([](WGPULogLevel, const WGPUStringView message, void*)
        {
            DBG("wgpu: " << toString(message));
        }, nullptr);
       #endif

        instance.reset(wgpuCreateInstance(nullptr));
        if (!instance)
        {
            DBG("webgpu::Device: wgpuCreateInstance failed");
            return;
        }

        struct AdapterResult final
        {
            bool done = false;
            WGPUAdapter adapter = nullptr;
            juce::String message;
        } adapterResult;

        WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        options.powerPreference = WGPUPowerPreference_LowPower;

        WGPURequestAdapterCallbackInfo adapterCallback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
        adapterCallback.mode = WGPUCallbackMode_AllowProcessEvents;
        adapterCallback.callback = [](const WGPURequestAdapterStatus status, const WGPUAdapter a, const WGPUStringView message,
                                      void* userdata, void*)
        {
            auto& r = *static_cast<AdapterResult*>(userdata);
            if (status == WGPURequestAdapterStatus_Success)
                r.adapter = a;
            else
                r.message = toString(message);
            r.done = true;
        };
        adapterCallback.userdata1 = &adapterResult;

        wgpuInstanceRequestAdapter(instance.get(), &options, adapterCallback);
        waitFor(adapterResult.done);
        adapter.reset(adapterResult.adapter);
        if (!adapter)
        {
            DBG("webgpu::Device: no adapter: " << adapterResult.message);
            return;
        }

        struct DeviceResult final
        {
            bool done = false;
            WGPUDevice device = nullptr;
            juce::String message;
        } deviceResult;

        WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
        descriptor.label = toStringView("webgpu::Device");

        descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        descriptor.deviceLostCallbackInfo.callback = [](const WGPUDevice*, const WGPUDeviceLostReason reason,
                                                        const WGPUStringView message, void* userdata, void*)
        {
            static_cast<Device*>(userdata)->lost.store(true, std::memory_order_relaxed);
            if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled)
                DBG("webgpu::Device lost: " << toString(message));
            juce::ignoreUnused(reason, message);
        };
        descriptor.deviceLostCallbackInfo.userdata1 = this;

        descriptor.uncapturedErrorCallbackInfo.callback = [](const WGPUDevice*, WGPUErrorType, const WGPUStringView message,
                                                             void*, void*)
        {
            DBG("webgpu: uncaptured error: " << toString(message));
            juce::ignoreUnused(message);
        };

        WGPURequestDeviceCallbackInfo deviceCallback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
        deviceCallback.mode = WGPUCallbackMode_AllowProcessEvents;
        deviceCallback.callback = [](const WGPURequestDeviceStatus status, const WGPUDevice d, const WGPUStringView message,
                                     void* userdata, void*)
        {
            auto& r = *static_cast<DeviceResult*>(userdata);
            if (status == WGPURequestDeviceStatus_Success)
                r.device = d;
            else
                r.message = toString(message);
            r.done = true;
        };
        deviceCallback.userdata1 = &deviceResult;

        wgpuAdapterRequestDevice(adapter.get(), &descriptor, deviceCallback);
        waitFor(deviceResult.done);
        device.reset(deviceResult.device);
        if (!device)
        {
            DBG("webgpu::Device: no device: " << deviceResult.message);
            return;
        }

        queue.reset(wgpuDeviceGetQueue(device.get()));
    }

    Device::~Device()
    {
        if (device)
            waitIdle();

        queue.reset();
        device.reset();
        adapter.reset();
        instance.reset();
    }

    void Device::waitFor(const bool& done) const
    {
        for (int i = 0; i < 10000 && !done; ++i)
        {
            wgpuInstanceProcessEvents(instance.get());
            if (!done)
                juce::Thread::sleep(1);
        }
        jassert(done);
    }

    void Device::poll(const double frameTime) noexcept
    {
        if (!device || frameTime == lastPollTime)
            return;
        lastPollTime = frameTime;
        wgpuDevicePoll(device.get(), false, nullptr);
    }

    void Device::waitIdle() noexcept
    {
        if (device)
            wgpuDevicePoll(device.get(), true, nullptr);
    }

    void Device::pushErrorScope(const WGPUErrorFilter filter) noexcept
    {
        if (device)
            wgpuDevicePushErrorScope(device.get(), filter);
    }

    std::optional<juce::String> Device::popErrorScope()
    {
        if (!device)
            return juce::String("no device");

        struct Result final
        {
            bool done = false;
            std::optional<juce::String> error;
        } result;

        WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = [](const WGPUPopErrorScopeStatus status, const WGPUErrorType type, const WGPUStringView message,
                               void* userdata, void*)
        {
            auto& r = *static_cast<Result*>(userdata);
            if (status != WGPUPopErrorScopeStatus_Success)
                r.error = juce::String("popErrorScope failed");
            else if (type != WGPUErrorType_NoError)
                r.error = toString(message);
            r.done = true;
        };
        callback.userdata1 = &result;

        wgpuDevicePopErrorScope(device.get(), callback);
        waitFor(result.done);
        return result.error;
    }

    ShaderModule Device::createShaderModule(const std::string_view wgsl, const std::string_view label) const
    {
        if (!device)
            return {};

        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = toStringView(wgsl);

        WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        descriptor.nextInChain = &source.chain;
        descriptor.label = toStringView(label);

        return ShaderModule(wgpuDeviceCreateShaderModule(device.get(), &descriptor));
    }
}
