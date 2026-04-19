/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/CD3D9To11Converter.h
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <d3d9.h>
#include <d3d11.h>

///////////////////////////////////////////////////////////////////////////////
//
// CD3D9To11Converter
//
// Utility class for converting D3D9 surfaces to D3D11 textures.
// This is needed for interoperability with the existing video encoder
// system which uses D3D11.
//
// Conversion strategy:
// 1. Create staging surface (D3D9, CPU-readable)
// 2. Copy GPU surface to staging
// 3. Map staging surface, copy pixels to CPU buffer
// 4. Create D3D11 texture from CPU buffer
//
///////////////////////////////////////////////////////////////////////////////
class CD3D9To11Converter
{
public:
    CD3D9To11Converter();
    ~CD3D9To11Converter();

    // Initialize converter with D3D9 and D3D11 devices
    bool Initialize(IDirect3DDevice9* pD3D9Device, ID3D11Device* pD3D11Device);

    // Cleanup resources
    void Shutdown();

    // Convert D3D9 surface to D3D11 texture
    // Returns new D3D11Texture2D (caller must Release)
    // Returns nullptr on failure
    ID3D11Texture2D* Convert(IDirect3DSurface9* pD3D9Surface);

    // Check if initialized
    bool IsInitialized() const { return m_bInitialized; }

private:
    IDirect3DDevice9*       m_pD3D9Device;
    ID3D11Device*           m_pD3D11Device;
    ID3D11DeviceContext*    m_pD3D11Context;
    bool                    m_bInitialized;

    // Cached staging surface (reused for conversions)
    IDirect3DSurface9*      m_pStagingSurface;
    int                     m_iStagingWidth;
    int                     m_iStagingHeight;

    // Helper: Create or resize staging surface
    bool EnsureStagingSurface(int width, int height);

    // Helper: Convert D3D9 format to DXGI format
    static DXGI_FORMAT D3D9FormatToDXGI(D3DFORMAT d3d9Format);
};
