#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "WebGPUDevice.h"

#include <memory>

namespace webgpu
{
    class NativeSurfaceView : public juce::Component
    {
    public:
        [[nodiscard]] static std::unique_ptr<NativeSurfaceView> create();

        ~NativeSurfaceView() override = default;

        [[nodiscard]] virtual Surface createSurface(WGPUInstance instance) = 0;

        [[nodiscard]] virtual juce::Point<int> getPixelSize() const = 0;
    };
}
