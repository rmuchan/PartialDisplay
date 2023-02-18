#include "Driver.h"
#include <iomanip>

using namespace std;
using namespace Microsoft::WRL;
using namespace PartialDisplay;

SwapChainProcessor::SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, shared_ptr<Direct3DDevice> Device, HANDLE NewFrameEvent)
    : m_hSwapChain(hSwapChain), m_Device(Device), m_hAvailableBufferEvent(NewFrameEvent), m_Width(0), m_Height(0)
{
    m_hTerminateEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));

    // Immediately create and run the swap-chain processing thread, passing 'this' as the thread parameter
    m_hThread.Attach(CreateThread(nullptr, 0, RunThread, this, 0, nullptr));
}

SwapChainProcessor::~SwapChainProcessor()
{
    // Alert the swap-chain processing thread to terminate
    SetEvent(m_hTerminateEvent.Get());

    if (m_hThread.Get())
    {
        // Wait for the thread to terminate
        WaitForSingleObject(m_hThread.Get(), INFINITE);
    }
}

DWORD CALLBACK SwapChainProcessor::RunThread(LPVOID Argument)
{
    reinterpret_cast<SwapChainProcessor*>(Argument)->Run();
    return 0;
}

void SwapChainProcessor::Run()
{
    // For improved performance, make use of the Multimedia Class Scheduler Service, which will intelligently
    // prioritize this thread for improved throughput in high CPU-load scenarios.
    DWORD AvTask = 0;
    HANDLE AvTaskHandle = AvSetMmThreadCharacteristicsW(L"Distribution", &AvTask);

    RunCore();

    // Always delete the swap-chain object when swap-chain processing loop terminates in order to kick the system to
    // provide a new swap-chain if necessary.
    WdfObjectDelete((WDFOBJECT)m_hSwapChain);
    m_hSwapChain = nullptr;

    AvRevertMmThreadCharacteristics(AvTaskHandle);
}

void SwapChainProcessor::RunCore()
{
    // Get the DXGI device interface
    ComPtr<IDXGIDevice> DxgiDevice;
    HRESULT hr = m_Device->Device.As(&DxgiDevice);
    if (FAILED(hr))
    {
        return;
    }

    IDARG_IN_SWAPCHAINSETDEVICE SetDevice = {};
    SetDevice.pDevice = DxgiDevice.Get();

    hr = IddCxSwapChainSetDevice(m_hSwapChain, &SetDevice);
    if (FAILED(hr))
    {
        return;
    }

    // Acquire and release buffers in a loop
    for (;;)
    {
        ComPtr<IDXGIResource> AcquiredBuffer;

        // Ask for the next buffer from the producer
        IDARG_OUT_RELEASEANDACQUIREBUFFER Buffer = {};
        hr = IddCxSwapChainReleaseAndAcquireBuffer(m_hSwapChain, &Buffer);

        // AcquireBuffer immediately returns STATUS_PENDING if no buffer is yet available
        if (hr == E_PENDING)
        {
            // We must wait for a new buffer
            HANDLE WaitHandles[] =
            {
                m_hAvailableBufferEvent,
                m_hTerminateEvent.Get()
            };
            DWORD WaitResult = WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles, FALSE, 16);
            if (WaitResult == WAIT_OBJECT_0 || WaitResult == WAIT_TIMEOUT)
            {
                // We have a new buffer, so try the AcquireBuffer again
                continue;
            }
            else if (WaitResult == WAIT_OBJECT_0 + 1)
            {
                // We need to terminate
                break;
            }
            else
            {
                // The wait was cancelled or something unexpected happened
                hr = HRESULT_FROM_WIN32(WaitResult);
                break;
            }
        }
        else if (SUCCEEDED(hr))
        {
            // We have new frame to process, the surface has a reference on it that the driver has to release
            AcquiredBuffer.Attach(Buffer.MetaData.pSurface);

            // ==============================
            // TODO: Process the frame here
            //
            // This is the most performance-critical section of code in an IddCx driver. It's important that whatever
            // is done with the acquired surface be finished as quickly as possible. This operation could be:
            //  * a GPU copy to another buffer surface for later processing (such as a staging surface for mapping to CPU memory)
            //  * a GPU encode operation
            //  * a GPU VPBlt to another surface
            //  * a GPU custom compute shader encode operation
            // ==============================
            ProcessResource(AcquiredBuffer.Get());

            // We have finished processing this frame hence we release the reference on it.
            // If the driver forgets to release the reference to the surface, it will be leaked which results in the
            // surfaces being left around after m_SwapChain is destroyed.
            // NOTE: Although in this sample we release reference to the surface here; the driver still
            // owns the Buffer.MetaData.pSurface surface until IddCxSwapChainReleaseAndAcquireBuffer returns
            // S_OK and gives us a new frame, a driver may want to use the surface in future to re-encode the desktop 
            // for better quality if there is no new frame for a while
            AcquiredBuffer.Reset();

            // Indicate to OS that we have finished inital processing of the frame, it is a hint that
            // OS could start preparing another frame
            hr = IddCxSwapChainFinishedProcessingFrame(m_hSwapChain);
            if (FAILED(hr))
            {
                break;
            }

            // ==============================
            // TODO: Report frame statistics once the asynchronous encode/send work is completed
            //
            // Drivers should report information about sub-frame timings, like encode time, send time, etc.
            // ==============================
            // IddCxSwapChainReportFrameStatistics(m_hSwapChain, ...);
        }
        else
        {
            // The swap-chain was likely abandoned (e.g. DXGI_ERROR_ACCESS_LOST), so exit the processing loop
            break;
        }
    }
}

HRESULT SwapChainProcessor::ProcessResource(IDXGIResource* resource)
{
    HRESULT hr;

    ComPtr<ID3D11Texture2D> texture;
    hr = resource->QueryInterface(texture.GetAddressOf());
    if (FAILED(hr))
    {
        return hr;
    }

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    ComPtr<ID3D11Texture2D> CopyBuffer;
    bool ShouldRecreate;
    {
        unique_lock<mutex> lock(m_MutexMeta);
        CopyBuffer = m_CopyBuffer;
        ShouldRecreate = desc.Width != m_Width || desc.Height != m_Height || CopyBuffer == nullptr;
    }

    if (ShouldRecreate)
    {
        D3D11_TEXTURE2D_DESC bufferDesc;
        bufferDesc.Width = desc.Width;
        bufferDesc.Height = desc.Height;
        bufferDesc.MipLevels = 1;
        bufferDesc.ArraySize = 1;
        bufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.SampleDesc.Quality = 0;
        bufferDesc.Usage = D3D11_USAGE_STAGING;
        bufferDesc.BindFlags = 0;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        bufferDesc.MiscFlags = 0;

        hr = m_Device->Device->CreateTexture2D(&bufferDesc, nullptr, &CopyBuffer);
        if (FAILED(hr))
        {
            return hr;
        }

        unique_lock<mutex> lock(m_MutexMeta);
        m_Width = desc.Width;
        m_Height = desc.Height;
        m_CopyBuffer = CopyBuffer;
    }

    m_Device->DeviceContext->CopyResource(CopyBuffer.Get(), texture.Get());
    return S_OK;
}

NTSTATUS SwapChainProcessor::FillRetrievalResponse(void* Buffer, size_t Size)
{
    if (Size < sizeof(UINT[3]))
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    ComPtr<ID3D11Texture2D> CopyBuffer;
    UINT Width, Height;
    {
        unique_lock<mutex> lockMeta(m_MutexMeta);
        CopyBuffer = m_CopyBuffer;
        Width = m_Width;
        Height = m_Height;
    }

    HRESULT hr;
    if (CopyBuffer == nullptr)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    ComPtr<IDXGISurface> surface;
    hr = CopyBuffer->QueryInterface(surface.GetAddressOf());
    if (FAILED(hr))
    {
        return STATUS_INTERNAL_ERROR;
    }

    DXGI_MAPPED_RECT mapped;
    hr = surface->Map(&mapped, DXGI_MAP_READ);
    if (FAILED(hr))
    {
        return STATUS_INTERNAL_ERROR;
    }

    UINT* meta = (UINT*)Buffer;
    meta[0] = Width;
    meta[1] = Height;
    meta[2] = mapped.Pitch;

    UINT required = mapped.Pitch * Height;
    if (Size >= sizeof(UINT[3]) + required)
    {
        memcpy((char*)Buffer + sizeof(UINT[3]), mapped.pBits, required);
        surface->Unmap();
        return sizeof(UINT[3]) + required;
    }
    else
    {
        surface->Unmap();
        return sizeof(UINT[3]);
    }
}