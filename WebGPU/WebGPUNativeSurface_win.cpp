#include "WebGPUNativeSurface.h"

#if JUCE_WINDOWS

#include <juce_gui_extra/juce_gui_extra.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace webgpu
{
    namespace
    {
        LRESULT CALLBACK surfaceWndProc(HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
        {
            switch (msg)
            {
                case WM_NCHITTEST: return HTTRANSPARENT;
                case WM_ERASEBKGND: return 1;
                case WM_PAINT:
                {
                    PAINTSTRUCT ps;
                    BeginPaint(hwnd, &ps);
                    EndPaint(hwnd, &ps);
                    return 0;
                }
                default: break;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        HINSTANCE moduleInstance()
        {
            return static_cast<HINSTANCE>(juce::Process::getCurrentModuleInstanceHandle());
        }

        const wchar_t* windowClassName()
        {
            static const juce::String name = "WebGPUSurface_" + juce::String::toHexString(reinterpret_cast<juce::pointer_sized_int>(moduleInstance()));
            static const std::wstring wide = name.toWideCharPointer();
            static const bool registered = []
            {
                WNDCLASSEXW wc{};
                wc.cbSize = sizeof(wc);
                wc.style = CS_OWNDC;
                wc.lpfnWndProc = surfaceWndProc;
                wc.hInstance = moduleInstance();
                wc.hCursor = nullptr;
                wc.hbrBackground = nullptr;
                wc.lpszClassName = wide.c_str();
                return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
            }();
            return registered ? wide.c_str() : nullptr;
        }

        class ScopedDpiAwarenessOf final
        {
        public:
            explicit ScopedDpiAwarenessOf(HWND parent)
            {
                const auto user32 = GetModuleHandleW(L"user32.dll");
                if (user32 == nullptr)
                    return;

                using GetCtx = DpiContext(WINAPI*)(HWND);
                using SetCtx = DpiContext(WINAPI*)(DpiContext);
                const auto getWindowCtx = reinterpret_cast<GetCtx>(reinterpret_cast<void*>(GetProcAddress(user32, "GetWindowDpiAwarenessContext")));
                setThreadCtx = reinterpret_cast<SetCtx>(reinterpret_cast<void*>(GetProcAddress(user32, "SetThreadDpiAwarenessContext")));
                if (getWindowCtx != nullptr && setThreadCtx != nullptr)
                    previous = setThreadCtx(getWindowCtx(parent));
            }

            ~ScopedDpiAwarenessOf()
            {
                if (previous != nullptr)
                    setThreadCtx(previous);
            }

        private:
            using DpiContext = HANDLE;

            DpiContext(WINAPI* setThreadCtx)(DpiContext) = nullptr;
            DpiContext previous = nullptr;
        };
    }

    class HwndSurfaceView final : public NativeSurfaceView
    {
    public:
        HwndSurfaceView()
        {
            setInterceptsMouseClicks(false, false);
            hwndComponent.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(hwndComponent);
        }

        void resized() override { hwndComponent.setBounds(getLocalBounds()); }

        Surface createSurface(const WGPUInstance instance) override
        {
            auto* peer = getPeer();
            const auto* className = windowClassName();
            if (peer == nullptr || className == nullptr)
                return {};

            if (hwndComponent.getHWND() == nullptr)
            {
                const auto parent = static_cast<HWND>(peer->getNativeHandle());
                const ScopedDpiAwarenessOf dpi(parent);
                const auto hwnd = CreateWindowExW(WS_EX_NOPARENTNOTIFY, className, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                                  0, 0, 1, 1, parent, nullptr, moduleInstance(), nullptr);
                if (hwnd == nullptr)
                    return {};

                hwndComponent.setHWND(hwnd);
                hwndComponent.updateHWNDBounds();
            }

            WGPUSurfaceSourceWindowsHWND source = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
            source.hinstance = moduleInstance();
            source.hwnd = hwndComponent.getHWND();

            WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
            descriptor.nextInChain = &source.chain;
            return Surface(wgpuInstanceCreateSurface(instance, &descriptor));
        }

        juce::Point<int> getPixelSize() const override
        {
            RECT r{};
            if (const auto hwnd = static_cast<HWND>(hwndComponent.getHWND()); hwnd == nullptr || !GetClientRect(hwnd, &r))
                return {};
            return {static_cast<int>(r.right - r.left), static_cast<int>(r.bottom - r.top)};
        }

    private:
        juce::HWNDComponent hwndComponent;
    };

    std::unique_ptr<NativeSurfaceView> NativeSurfaceView::create()
    {
        return std::make_unique<HwndSurfaceView>();
    }
}

#endif
