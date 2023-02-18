#include "App.h"
#include "compiled/VertexShader.h"
#include "compiled/PixelShader.h"

using namespace std;
using namespace PartialDisplay;

Rendering::~Rendering()
{
    if (m_SwapChain != nullptr)
    {
        m_SwapChain->SetFullscreenState(false, nullptr);
    }
}

HRESULT Rendering::InitD3D(HWND hWnd, UINT WindowWidth, UINT WindowHeight)
{
    HRESULT hr;

    // create a struct to hold information about the swap chain
    DXGI_SWAP_CHAIN_DESC scd = {};

    // fill the swap chain description struct
    scd.BufferCount = 1;                                   // one back buffer
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;    // use 32-bit color
    scd.BufferDesc.Width = WindowWidth;                    // set the back buffer width
    scd.BufferDesc.Height = WindowHeight;                  // set the back buffer height
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;     // how swap chain is to be used
    scd.OutputWindow = hWnd;                               // the window to be used
    scd.SampleDesc.Count = 4;                              // how many multisamples
    scd.Windowed = TRUE;                                   // windowed/full-screen mode
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;    // allow full-screen switching

    // create a device, device context and swap chain using the information in the scd struct
    hr = D3D11CreateDeviceAndSwapChain(NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        NULL,
        NULL,
        NULL,
        D3D11_SDK_VERSION,
        &scd,
        &m_SwapChain,
        &m_Device,
        NULL,
        &m_DeviceContext);
    if (FAILED(hr)) { return hr; }

    hr = InitPipeline();
    if (FAILED(hr)) { return hr; }
    hr = InitGraphics();
    if (FAILED(hr)) { return hr; }
    hr = UpdateConfig(0, 0, WindowWidth, WindowHeight);
    return hr;
}

HRESULT Rendering::InitPipeline()
{
    HRESULT hr;

    // encapsulate both shaders into shader objects
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    hr = m_Device->CreateVertexShader(g_VertexShader, sizeof(g_VertexShader), nullptr, &VertexShader);
    if (FAILED(hr)) { return hr; }
    hr = m_Device->CreatePixelShader(g_PixelShader, sizeof(g_PixelShader), nullptr, &PixelShader);
    if (FAILED(hr)) { return hr; }

    // set the shader objects
    m_DeviceContext->VSSetShader(VertexShader.Get(), nullptr, 0);
    m_DeviceContext->PSSetShader(PixelShader.Get(), nullptr, 0);

    // create input layout
    D3D11_INPUT_ELEMENT_DESC ied[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    ComPtr<ID3D11InputLayout> InputLayout;
    hr = m_Device->CreateInputLayout(ied, ARRAYSIZE(ied), g_VertexShader, sizeof(g_VertexShader), &InputLayout);
    if (FAILED(hr)) { return hr; }
    m_DeviceContext->IASetInputLayout(InputLayout.Get());

    return S_OK;
}

HRESULT Rendering::InitGraphics()
{
    HRESULT hr;

    // create and select vertex buffer
    {
        float Vertices[][2] =
        {
            { -1, +1 },  // Left  Top
            { +1, +1 },  // Right Top
            { -1, -1 },  // Left  Bottom
            { +1, -1 },  // Right Bottom
        };

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.ByteWidth = sizeof(Vertices);
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = Vertices;

        ComPtr<ID3D11Buffer> VertexBuffer;
        hr = m_Device->CreateBuffer(&bd, &sd, &VertexBuffer);
        if (FAILED(hr)) { return hr; }

        UINT stride = sizeof(Vertices[0]);
        UINT offset = 0;
        m_DeviceContext->IASetVertexBuffers(0, 1, VertexBuffer.GetAddressOf(), &stride, &offset);
        m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    }

    // create and select sampler state
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MinLOD = 0;
        sd.MaxLOD = D3D11_FLOAT32_MAX;

        ComPtr<ID3D11SamplerState> SamplerState;
        hr = m_Device->CreateSamplerState(&sd, &SamplerState);
        if (FAILED(hr)) { return hr; }
        m_DeviceContext->PSSetSamplers(0, 1, SamplerState.GetAddressOf());
    }

    return S_OK;
}

HRESULT Rendering::UpdateConfig(UINT ScreenWidth, UINT ScreenHeight, UINT WindowWidth, UINT WindowHeight)
{
    struct DisplayConfig
    {
        float XOffset;
        float YOffset;
        float XScale;
        float YScale;
    };

    HRESULT hr;

    if (m_ConfigBuffer == nullptr)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(DisplayConfig);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = m_Device->CreateBuffer(&bd, nullptr, &m_ConfigBuffer);
        if (FAILED(hr)) { return hr; }
        m_DeviceContext->VSSetConstantBuffers(0, 1, m_ConfigBuffer.GetAddressOf());
    }

    if (!m_PreviousConfig.LoadOrUpdate(ScreenWidth, ScreenHeight, WindowWidth, WindowHeight))
    {
        return S_OK;
    }

    DisplayConfig config;
    float ScreenRatio = float(ScreenWidth) / ScreenHeight;
    float WindowRatio = float(WindowWidth) / WindowHeight;
    if (ScreenRatio < WindowRatio)
    {
        config.XOffset = 1 - ScreenRatio / WindowRatio;
        config.YOffset = 0;
        config.XScale = ScreenRatio / WindowRatio;
        config.YScale = 1;
    }
    else
    {
        config.XOffset = 0;
        config.YOffset = 0;
        config.XScale = 1;
        config.YScale = WindowRatio / ScreenRatio;
    }
    m_DeviceContext->UpdateSubresource(m_ConfigBuffer.Get(), 0, nullptr, &config, 0, 0);

    m_RenderTarget.Reset();
    hr = m_SwapChain->ResizeBuffers(
        2, WindowWidth, WindowHeight, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
    if (FAILED(hr)) { return hr; }

    // get the address of the back buffer
    ComPtr<ID3D11Texture2D> BackBuffer;
    hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &BackBuffer);
    if (FAILED(hr)) { return hr; }
    // use the back buffer address to create the render target
    hr = m_Device->CreateRenderTargetView(BackBuffer.Get(), nullptr, &m_RenderTarget);
    if (FAILED(hr)) { return hr; }
    // set the render target as the back buffer
    m_DeviceContext->OMSetRenderTargets(1, m_RenderTarget.GetAddressOf(), nullptr);

    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (FLOAT)WindowWidth;
    viewport.Height = (FLOAT)WindowHeight;
    m_DeviceContext->RSSetViewports(1, &viewport);
    return S_OK;
}

HRESULT Rendering::UpdateFrame(UINT ScreenWidth, UINT ScreenHeight, UINT pitch, const void* data)
{
    HRESULT hr;

    // check if the buffer can be reused
    if (ScreenWidth != m_PreviousConfig.m_ScreenWidth || ScreenHeight != m_PreviousConfig.m_ScreenHeight)
    {
        // create texture buffer
        D3D11_TEXTURE2D_DESC td;
        td.Width = ScreenWidth;
        td.Height = ScreenHeight;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.SampleDesc.Quality = 0;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        td.MiscFlags = 0;
        hr = m_Device->CreateTexture2D(&td, nullptr, &m_TextureBuffer);
        if (FAILED(hr)) { return hr; }

        // create and select texture view
        ComPtr<ID3D11ShaderResourceView> TextureView;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        srv.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        hr = m_Device->CreateShaderResourceView(m_TextureBuffer.Get(), &srv, &TextureView);
        if (FAILED(hr)) { return hr; }
        m_DeviceContext->PSSetShaderResources(0, 1, TextureView.GetAddressOf());

        // update display config
        UpdateConfig(ScreenWidth, ScreenHeight, 0, 0);
    }

    D3D11_MAPPED_SUBRESOURCE ms;
    hr = m_DeviceContext->Map(m_TextureBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    if (FAILED(hr)) { return hr; }
    memcpy(ms.pData, data, size_t(pitch) * ScreenHeight);
    m_DeviceContext->Unmap(m_TextureBuffer.Get(), 0);

    float color[] = { 0, 0, 0, 1 };
    m_DeviceContext->ClearRenderTargetView(m_RenderTarget.Get(), color);
    m_DeviceContext->Draw(4, 0);
    hr = m_SwapChain->Present(1, 0);
    return hr;
}

bool Rendering::PreviousConfig::LoadOrUpdate(UINT& rScreenWidth, UINT& rScreenHeight, UINT& rWindowWidth, UINT& rWindowHeight)
{
    bool updated = false;
#define UpdateSingle(v)  \
    do {                                       \
        if (r##v == 0 && m_##v != 0)           \
            r##v = m_##v;                      \
        else if (r##v != m_##v)                \
            m_##v = r##v, updated = true;      \
        else if (m_##v == 0)                   \
            m_##v = r##v = 1, updated = true;  \
    } while (false)

    UpdateSingle(ScreenWidth);
    UpdateSingle(ScreenHeight);
    UpdateSingle(WindowWidth);
    UpdateSingle(WindowHeight);
#undef UpdateSingle
    return updated;
}