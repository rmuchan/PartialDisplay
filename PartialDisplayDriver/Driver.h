#pragma once

#define NOMINMAX
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>

#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <avrt.h>
#include <wrl.h>

#include <memory>
#include <vector>
#include <mutex>

namespace Microsoft::WRL::Wrappers
{
    // Adds a wrapper for thread handles to the existing set of WRL handle wrapper classes
    typedef HandleT<HandleTraits::HANDLENullTraits> Thread;
}

namespace PartialDisplay
{
    /// <summary>
    /// Manages the creation and lifetime of a Direct3D render device.
    /// </summary>
    struct Direct3DDevice
    {
        Direct3DDevice(LUID AdapterLuid);
        Direct3DDevice();
        HRESULT Init();

        LUID AdapterLuid;
        Microsoft::WRL::ComPtr<IDXGIFactory5> DxgiFactory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> DeviceContext;
    };

    /// <summary>
    /// Manages a thread that consumes buffers from an indirect display swap-chain object.
    /// </summary>
    class SwapChainProcessor
    {
    public:
        SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, std::shared_ptr<Direct3DDevice> Device, HANDLE NewFrameEvent);
        ~SwapChainProcessor();

        NTSTATUS FillRetrievalResponse(void* Buffer, size_t Size);

    private:
        static DWORD CALLBACK RunThread(LPVOID Argument);

        void Run();
        void RunCore();

        HRESULT ProcessResource(IDXGIResource* resource);

        IDDCX_SWAPCHAIN m_hSwapChain;
        std::shared_ptr<Direct3DDevice> m_Device;
        HANDLE m_hAvailableBufferEvent;
        Microsoft::WRL::Wrappers::Thread m_hThread;
        Microsoft::WRL::Wrappers::Event m_hTerminateEvent;

        UINT m_Width;
        UINT m_Height;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_CopyBuffer;
        std::mutex m_MutexMeta;
    };

    /// <summary>
    /// Provides a sample implementation of an indirect display driver.
    /// </summary>
    class IndirectDeviceContext
    {
    public:
        IndirectDeviceContext(_In_ WDFDEVICE WdfDevice);
        virtual ~IndirectDeviceContext();

        void InitAdapter();
        void FinishInit(UINT ConnectorIndex);

        IDDCX_MONITOR GetMonitorAt(UINT ConnectorIndex);

    protected:
        WDFDEVICE m_WdfDevice;
        IDDCX_ADAPTER m_Adapter;
        std::vector<IDDCX_MONITOR> m_Monitors;
    };

    class IndirectMonitorContext
    {
    public:
        IndirectMonitorContext(_In_ IDDCX_MONITOR Monitor);
        virtual ~IndirectMonitorContext();

        void AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent);
        void UnassignSwapChain();

        SwapChainProcessor* GetSwapChainProcessor() { return m_ProcessingThread.get(); }

    private:
        IDDCX_MONITOR m_Monitor;
        std::unique_ptr<SwapChainProcessor> m_ProcessingThread;
    };

    struct IndirectDeviceContextWrapper
    {
        IndirectDeviceContext* pContext;

        void Cleanup()
        {
            delete pContext;
            pContext = nullptr;
        }
    };

    struct IndirectMonitorContextWrapper
    {
        IndirectMonitorContext* pContext;

        void Cleanup()
        {
            delete pContext;
            pContext = nullptr;
        }
    };

    // This macro creates the methods for accessing an IndirectDeviceContextWrapper as a context for a WDF object
    WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContextWrapper);
    WDF_DECLARE_CONTEXT_TYPE(IndirectMonitorContextWrapper);
}