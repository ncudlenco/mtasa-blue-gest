/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/CMultiModalCapture.h
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <core/IMultiModalCapture.h>
#include <d3d9.h>
#include <d3d11.h>
#include <memory>
#include <string>
#include "TextureRegistry.h"
#include "CD3D9To11Converter.h"
#include "CVideoEncoder.h"
#include "CSaveWorkerPool.h"
#include <cstdint>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <unordered_set>
#include <vector>

class CMultiModalCapture : public IMultiModalCapture
{
public:
    CMultiModalCapture();
    ~CMultiModalCapture();

    // IMultiModalCapture
    bool Initialize(IDirect3DDevice9* pDevice, int width, int height) override;
    void Shutdown() override;

    bool CaptureMultiModalFrame(const std::string& rgbPath,
                                const std::string& segPath,
                                const std::string& depthPath,
                                bool saveRgbToVideo,
                                bool saveSegToVideo,
                                bool saveDepthToVideo,
                                int  jpegQuality) override;

    bool StartVideoRecording(int                modalityId,
                             const std::string& videoPath,
                             int                width,
                             int                height,
                             int                fps,
                             int                bitrate) override;
    bool StopVideoRecording(int modalityId) override;

    bool WriteMappingJson(const std::string& path) const override;

    void OnPresent(IDirect3DDevice9* pDevice) override;

    void SetSegmentationEnabled(bool enabled) override;
    bool IsSegmentationEnabled() const override { return m_bSegmentationEnabled; }

    void SetDiagLogsEnabled(bool enabled) override { m_bDiagLogsEnabled = enabled; }

    void WaitPendingCaptures() override;

    void EmitSegmentationDraw(IDirect3DDevice9* pDevice,
                              unsigned int primitiveType,
                              unsigned int startVertex,
                              unsigned int primitiveCount) override;
    void EmitSegmentationDrawIndexed(IDirect3DDevice9* pDevice,
                                     unsigned int primitiveType,
                                     int          baseVertexIndex,
                                     unsigned int minVertexIndex,
                                     unsigned int numVertices,
                                     unsigned int startIndex,
                                     unsigned int primitiveCount) override;

private:
    IDirect3DSurface9*      m_pRGBSurface;
    IDirect3DSurface9*      m_pSegmentationSurface;
    IDirect3DSurface9*      m_pSegmentationSnapshot;    // frame-boundary copy read by captureMultiModalFrame
    IDirect3DSurface9*      m_pSegDepthStencil;         // seg-owned DS; frontmost-wins per replay-pixel
    IDirect3DSurface9*      m_pDepthSurface;

    IDirect3DDevice9*       m_pDevice;

    ID3D11Device*           m_pD3D11Device;
    ID3D11DeviceContext*    m_pD3D11Context;
    CD3D9To11Converter      m_D3D9To11Converter;

    CTextureRegistry        m_TextureRegistry;

    CVideoEncoder           m_RGBVideoEncoder;
    CVideoEncoder           m_SegmentationVideoEncoder;
    CVideoEncoder           m_DepthVideoEncoder;

    IDirect3DPixelShader9*  m_pDepthVisualizationShader;
    IDirect3DVertexShader9* m_pFullscreenQuadVS;        // passthrough VS, used with the depth viz PS
    IDirect3DPixelShader9*  m_pConstantColorShader;     // Stage 7 seg double-draw
    IDirect3DVertexBuffer9* m_pFullscreenQuadVB;

    bool                    m_bSegmentationEnabled;     // arms the seg double-draw
    bool                    m_bSegSurfaceNeedsClear;    // reset each OnPresent, triggers clear at next first-draw of the new frame

    // Per-frame diagnostics for the segmentation replay path. Reset in
    // OnPresent after logging. Single-threaded access (render thread only).
    struct SSegFrameStats
    {
        int emitCalls        = 0;    // EmitSegmentationDraw[Indexed] entries
        int passedEnabled    = 0;    // ... that had m_bSegmentationEnabled true
        int passedScene      = 0;    // ... that passed IsInGtaSceneOnly
        int passedResources  = 0;    // ... that had all RTs / shader / DS
        int passedSizeGate   = 0;    // ... that passed IsDrawingToFullSizeRT
        int drawsFired       = 0;    // ... that actually ran EmitSegmentationCommon
        int uniqueColors     = 0;    // unique registry keys fired this frame
    };
    SSegFrameStats                     m_SegStats;
    std::unordered_set<uint32_t>       m_SegUniqueColorsThisFrame;
    std::map<uint64_t, int>            m_SegRTSizeBucket;           // (w<<32|h) -> count of Emit calls at that size
    bool                               m_bDiagLogsEnabled;          // gate for all [SegDiag] output + per-draw trace
    int                                m_SegPerDrawTraceRemaining;  // decremented per logged draw; reset in OnPresent
    uint64_t                           m_FrameIndex;                // incremented each OnPresent; stamped in log lines

    // INTZ depth-stencil (Stage 6). INTZ is a FourCC depth format that's
    // simultaneously a depth-stencil target and a sampleable texture, so the
    // game's normal render fills it and we read it in the visualization pass
    // without disrupting anything. Optional — skipped on adapters that don't
    // advertise INTZ support (older hardware / some virtual machines).
    IDirect3DTexture9*      m_pIntzTexture;
    IDirect3DSurface9*      m_pIntzSurface;             // level 0 of m_pIntzTexture
    IDirect3DSurface9*      m_pOriginalDepthStencil;    // saved, restored on Shutdown
    bool                    m_bIntzInstalled;

    std::unique_ptr<CSaveWorkerPool> m_pSaveWorkerPool;

    bool                    m_bInitialized;
    int                     m_iCaptureWidth;
    int                     m_iCaptureHeight;

    // Resource management
    bool CreateRenderTargets(int width, int height);
    void ReleaseRenderTargets();
    bool CreateShaders(IDirect3DDevice9* pDevice);
    void ReleaseShaders();
    bool CreateFullscreenQuad(IDirect3DDevice9* pDevice);
    void ReleaseFullscreenQuad();

    // Modality render paths (stubs filled by Stages 6/7).
    bool CaptureBackbuffer(IDirect3DDevice9* pDevice);
    bool RenderSegmentation(IDirect3DDevice9* pDevice);
    bool RenderDepthMap(IDirect3DDevice9* pDevice);

    CVideoEncoder* EncoderForModality(int modalityId);

    // Owning heap copy of one readback. Pattern mirrors CScreenGrabber:
    // readback (RT -> SYSTEMMEM) + memcpy into heap happens on the render
    // thread (D3D9 requires it); the heap buffer is then handed to a worker
    // so the render thread never waits on encode/write. No D3D9 resources
    // survive the ReadbackToHeap call.
    struct SReadbackBuffer
    {
        std::vector<uint8_t> pixels;        // tightly-packed BGRX bytes
        int                  pitch  = 0;    // stride in bytes (row alignment)
        UINT                 width  = 0;
        UINT                 height = 0;
        bool                 valid() const { return !pixels.empty() && pitch > 0; }
    };
    bool                ReadbackToHeap(IDirect3DSurface9* pRTSurface, SReadbackBuffer& out) const;

    // In-flight save tasks. Owned here so the capture can drain them at
    // session end via WaitPendingCaptures (also gets back-pressure so the
    // queue doesn't grow unbounded under sustained load).
    std::mutex                         m_PendingMutex;
    std::deque<std::future<void>>      m_PendingSaves;
    static constexpr size_t            kMaxPendingSaves = 32;
    void                DrainCompletedSaves();     // non-blocking cleanup
    void                ApplyBackPressure();       // blocks if queue full

    ID3D11Texture2D*    ConvertD3D9ToD3D11(IDirect3DSurface9* pSurface);
};
