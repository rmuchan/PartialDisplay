#pragma once

#include <Windows.h>
#include <cfgmgr32.h>
#include <swdevice.h>
#include <shellapi.h>
#include <wrl.h>
#include <d3d11.h>

#include <memory>
#include <optional>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HandleT;

namespace PartialDisplay::Helper
{
    struct HSWDEVICE_Traits
    {
        using Type = HSWDEVICE;

        inline static bool Close(_In_ Type h) noexcept
        {
            ::SwDeviceClose(h);
            return true;
        }

        inline static Type GetInvalidValue() noexcept
        {
            return nullptr;
        }
    };

    struct HMENU_Traits
    {
        using Type = HMENU;

        inline static bool Close(_In_ Type h) noexcept
        {
            return ::DestroyMenu(h);
        }

        inline static Type GetInvalidValue() noexcept
        {
            return nullptr;
        }
    };
}

namespace PartialDisplay
{
    using unique_handle = HandleT<Microsoft::WRL::Wrappers::HandleTraits::HANDLETraits>;

    struct MonitorData
    {
        constexpr static size_t HeaderLength = sizeof(UINT[3]);
        std::vector<char> Buffer;

        MonitorData() : Buffer(HeaderLength, 0) {}
        UINT GetWidth() { return reinterpret_cast<UINT*>(Buffer.data())[0]; }
        UINT GetHeight() { return reinterpret_cast<UINT*>(Buffer.data())[1]; }
        UINT GetPitch() { return reinterpret_cast<UINT*>(Buffer.data())[2]; }
        const char* GetData() { return Buffer.data() + HeaderLength; }
    };

    class Ioctl
    {
    public:
        MonitorData m_Monitor;

        bool CreateDevice();
        bool GetDeviceFileName();
        bool TryOpenHandle();
        bool RefreshMonitorData();

    private:
        HandleT<Helper::HSWDEVICE_Traits> m_hSwDevice;
        std::wstring m_DeviceFileName;
        unique_handle m_hDevice;
        WCHAR m_DeviceInstanceId[MAX_DEVICE_ID_LEN + 1];

        static void SwDeviceCreationCallback(HSWDEVICE hSwDevice, HRESULT CreateResult, PVOID pContext, PCWSTR pszDeviceInstanceId);
    };

    class Rendering
    {
    public:
        ~Rendering();
        HRESULT InitD3D(HWND hWnd, UINT WindowWidth, UINT WindowHeight);
        HRESULT OnSize(UINT WindowWidth, UINT WindowHeight) { return UpdateConfig(0, 0, WindowWidth, WindowHeight); }
        HRESULT UpdateFrame(UINT ScreenWidth, UINT ScreenHeight, UINT pitch, const void* data);

    private:
        struct PreviousConfig
        {
            UINT m_ScreenWidth = 0;
            UINT m_ScreenHeight = 0;
            UINT m_WindowWidth = 0;
            UINT m_WindowHeight = 0;
            bool LoadOrUpdate(UINT& ScreenWidth, UINT& ScreenHeight, UINT& WindowWidth, UINT& WindowHeight);
        };

        ComPtr<ID3D11Device> m_Device;
        ComPtr<ID3D11DeviceContext> m_DeviceContext;
        ComPtr<IDXGISwapChain> m_SwapChain;
        ComPtr<ID3D11RenderTargetView> m_RenderTarget;
        ComPtr<ID3D11Buffer> m_ConfigBuffer;
        ComPtr<ID3D11Texture2D> m_TextureBuffer;

        PreviousConfig m_PreviousConfig;

        HRESULT InitPipeline();
        HRESULT InitGraphics();
        HRESULT UpdateConfig(UINT ScreenWidth, UINT ScreenHeight, UINT WindowWidth, UINT WindowHeight);
    };

    class Window
    {
    public:
        Window();
        ~Window();

        static std::shared_ptr<Window> CreateMyWindow(HINSTANCE hInstance, HMONITOR hMonitor);
        int MainLoop();
        bool UpdateFrame(Ioctl& ioctl);

    private:
        HWND m_hWnd;
        std::optional<Rendering> m_Rendering;

        bool InitMyWindow(HINSTANCE hInstance, HMONITOR hMonitor);
        bool CreateMyTray(HWND hWnd, bool create);
        static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
        bool HandleTrayMessage(WPARAM wParam, LPARAM lParam);
    };
}