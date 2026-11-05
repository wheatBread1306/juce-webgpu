#include "WebGPUNativeSurface.h"

#if JUCE_MAC

#include <juce_gui_extra/juce_gui_extra.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cmath>

namespace webgpu
{
    class MetalSurfaceView final : public NativeSurfaceView
    {
    public:
        MetalSurfaceView()
        {
            setInterceptsMouseClicks(false, false);
            viewComponent.setInterceptsMouseClicks(false, false);

            view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
            layer = [CAMetalLayer layer];
            layer.opaque = YES;
            [view setLayer:layer];
            [view setWantsLayer:YES];

            viewComponent.setView(view);
            addAndMakeVisible(viewComponent);
        }

        ~MetalSurfaceView() override
        {
            viewComponent.setView(nullptr);
            [view release];
        }

        void resized() override { viewComponent.setBounds(getLocalBounds()); }

        Surface createSurface(const WGPUInstance instance) override
        {
            updateContentsScale();

            WGPUSurfaceSourceMetalLayer source = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
            source.layer = layer;

            WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
            descriptor.nextInChain = &source.chain;
            return Surface(wgpuInstanceCreateSurface(instance, &descriptor));
        }

        juce::Point<int> getPixelSize() const override
        {
            const auto scale = updateContentsScale();
            const auto bounds = [view bounds];
            return {static_cast<int>(std::lround(bounds.size.width * scale)),
                    static_cast<int>(std::lround(bounds.size.height * scale))};
        }

    private:
        CGFloat updateContentsScale() const
        {
            NSWindow* window = [view window];
            const CGFloat scale = window != nil ? [window backingScaleFactor] : 1.0;
            if (layer.contentsScale != scale)
                layer.contentsScale = scale;
            return scale;
        }

        juce::NSViewComponent viewComponent;
        NSView* view = nil;
        CAMetalLayer* layer = nil;
    };

    std::unique_ptr<NativeSurfaceView> NativeSurfaceView::create()
    {
        return std::make_unique<MetalSurfaceView>();
    }
}

#endif
