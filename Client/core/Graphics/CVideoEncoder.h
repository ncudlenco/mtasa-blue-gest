/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/CVideoEncoder.h
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <string>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <d3d11.h>

///////////////////////////////////////////////////////////////////////////////
//
// CVideoEncoder
//
// Encodes frames to H.264 video using Windows Media Foundation
// GPU-accelerated encoding with D3D11 texture input
//
///////////////////////////////////////////////////////////////////////////////
class CVideoEncoder
{
public:
    CVideoEncoder();
    ~CVideoEncoder();

    /// Start video recording session
    /// @param path Output MP4 file path
    /// @param width Frame width
    /// @param height Frame height
    /// @param fps Frames per second
    /// @param bitrate Video bitrate in bps
    /// @return Success
    bool Start(const std::string& path, int width, int height, int fps, int bitrate);

    /// Add frame from GPU texture
    /// @param gpuTexture D3D11 texture containing frame data
    /// @return Success
    bool AddFrame(ID3D11Texture2D* gpuTexture);

    /// Stop recording and finalize MP4 file
    /// @return Success
    bool Stop();

    /// Check if encoder is active
    bool IsInitialized() const { return m_bInitialized; }

private:
    IMFSinkWriter*  m_pSinkWriter;
    DWORD           m_dwStreamIndex;
    LONGLONG        m_iFrameCount;
    int             m_iFPS;
    int             m_iWidth;
    int             m_iHeight;
    int             m_iBitrate;
    bool            m_bInitialized;

    // Cached staging resources reused across AddFrame() calls.
    // Invalidated if source texture dimensions/format change.
    ID3D11Device*           m_pCachedDevice;
    ID3D11DeviceContext*    m_pCachedContext;
    ID3D11Texture2D*        m_pCachedStagingTexture;
    UINT                    m_uCachedStagingWidth;
    UINT                    m_uCachedStagingHeight;
    DXGI_FORMAT             m_CachedStagingFormat;

    bool        InitializeMediaFoundation();
    bool        CreateSinkWriter(const std::string& path);
    bool        ConfigureVideoStream();
    IMFSample*  CreateSampleFromTexture(ID3D11Texture2D* texture);
    bool        EnsureStagingResources(ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC& srcDesc);
    void        ReleaseCachedResources();
};
