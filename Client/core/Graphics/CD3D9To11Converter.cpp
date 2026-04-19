/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/CD3D9To11Converter.cpp
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CD3D9To11Converter.h"

CD3D9To11Converter::CD3D9To11Converter()
    : m_pD3D9Device(nullptr),
      m_pD3D11Device(nullptr),
      m_pD3D11Context(nullptr),
      m_bInitialized(false),
      m_pStagingSurface(nullptr),
      m_iStagingWidth(0),
      m_iStagingHeight(0)
{
}

CD3D9To11Converter::~CD3D9To11Converter()
{
    Shutdown();
}

bool CD3D9To11Converter::Initialize(IDirect3DDevice9* pD3D9Device, ID3D11Device* pD3D11Device)
{
    if (m_bInitialized)
        return true;

    if (!pD3D9Device || !pD3D11Device)
        return false;

    m_pD3D9Device = pD3D9Device;
    m_pD3D11Device = pD3D11Device;

    // Get D3D11 immediate context
    pD3D11Device->GetImmediateContext(&m_pD3D11Context);
    if (!m_pD3D11Context)
    {
        OutputDebugString("[CD3D9To11Converter] Failed to get D3D11 context\n");
        return false;
    }

    m_bInitialized = true;
    return true;
}

void CD3D9To11Converter::Shutdown()
{
    if (m_pStagingSurface)
    {
        m_pStagingSurface->Release();
        m_pStagingSurface = nullptr;
    }

    if (m_pD3D11Context)
    {
        m_pD3D11Context->Release();
        m_pD3D11Context = nullptr;
    }

    m_pD3D9Device = nullptr;
    m_pD3D11Device = nullptr;
    m_bInitialized = false;
}

bool CD3D9To11Converter::EnsureStagingSurface(int width, int height)
{
    // Reuse existing staging surface if dimensions match
    if (m_pStagingSurface && m_iStagingWidth == width && m_iStagingHeight == height)
        return true;

    // Release old staging surface
    if (m_pStagingSurface)
    {
        m_pStagingSurface->Release();
        m_pStagingSurface = nullptr;
    }

    // Create new staging surface (CPU-readable)
    HRESULT hr = m_pD3D9Device->CreateOffscreenPlainSurface(
        width, height,
        D3DFMT_X8R8G8B8,        // RGB format
        D3DPOOL_SYSTEMMEM,      // System memory (CPU-readable)
        &m_pStagingSurface,
        nullptr
    );

    if (FAILED(hr))
    {
        OutputDebugString("[CD3D9To11Converter] Failed to create staging surface\n");
        return false;
    }

    m_iStagingWidth = width;
    m_iStagingHeight = height;

    return true;
}

ID3D11Texture2D* CD3D9To11Converter::Convert(IDirect3DSurface9* pD3D9Surface)
{
    if (!m_bInitialized || !pD3D9Surface)
        return nullptr;

    // Get surface description
    D3DSURFACE_DESC desc;
    HRESULT hr = pD3D9Surface->GetDesc(&desc);
    if (FAILED(hr))
    {
        OutputDebugString("[CD3D9To11Converter] Failed to get surface description\n");
        return nullptr;
    }

    // Ensure staging surface exists
    if (!EnsureStagingSurface(desc.Width, desc.Height))
        return nullptr;

    // Copy GPU surface to CPU staging surface
    hr = m_pD3D9Device->GetRenderTargetData(pD3D9Surface, m_pStagingSurface);
    if (FAILED(hr))
    {
        OutputDebugString("[CD3D9To11Converter] Failed to copy surface data\n");
        return nullptr;
    }

    // Lock staging surface to access pixels
    D3DLOCKED_RECT lockedRect;
    hr = m_pStagingSurface->LockRect(&lockedRect, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr))
    {
        OutputDebugString("[CD3D9To11Converter] Failed to lock surface\n");
        return nullptr;
    }

    // Create D3D11 texture description
    D3D11_TEXTURE2D_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(texDesc));
    texDesc.Width = desc.Width;
    texDesc.Height = desc.Height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = D3D9FormatToDXGI(desc.Format);
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    // Create subresource data from locked pixels
    D3D11_SUBRESOURCE_DATA initData;
    ZeroMemory(&initData, sizeof(initData));
    initData.pSysMem = lockedRect.pBits;
    initData.SysMemPitch = lockedRect.Pitch;
    initData.SysMemSlicePitch = 0;

    // Create D3D11 texture
    ID3D11Texture2D* pD3D11Texture = nullptr;
    hr = m_pD3D11Device->CreateTexture2D(&texDesc, &initData, &pD3D11Texture);

    // Unlock staging surface
    m_pStagingSurface->UnlockRect();

    if (FAILED(hr))
    {
        OutputDebugString("[CD3D9To11Converter] Failed to create D3D11 texture\n");
        return nullptr;
    }

    return pD3D11Texture;
}

DXGI_FORMAT CD3D9To11Converter::D3D9FormatToDXGI(D3DFORMAT d3d9Format)
{
    switch (d3d9Format)
    {
        case D3DFMT_X8R8G8B8:
        case D3DFMT_A8R8G8B8:
            return DXGI_FORMAT_B8G8R8A8_UNORM;

        case D3DFMT_R5G6B5:
            return DXGI_FORMAT_B5G6R5_UNORM;

        case D3DFMT_X1R5G5B5:
        case D3DFMT_A1R5G5B5:
            return DXGI_FORMAT_B5G5R5A1_UNORM;

        case D3DFMT_A4R4G4B4:
            // DXGI_FORMAT_B4G4R4A4_UNORM doesn't exist in D3D11, use closest match
            return DXGI_FORMAT_B8G8R8A8_UNORM;

        case D3DFMT_R8G8B8:
            return DXGI_FORMAT_R8G8B8A8_UNORM;  // Closest match

        case D3DFMT_A8:
            return DXGI_FORMAT_A8_UNORM;

        default:
            return DXGI_FORMAT_B8G8R8A8_UNORM;  // Default fallback
    }
}
