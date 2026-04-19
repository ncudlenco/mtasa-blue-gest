/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/CMultiModalCapture.cpp
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CMultiModalCapture.h"
#include "CModalityImageWriter.h"
#include "../DXHook/CProxyDirect3DDevice9.h"
#include <game/CGame.h>
#include <game/CRenderWare.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <future>
#include <sstream>
#include <thread>
#include <vector>

// Scene-phase flags owned by CDirect3DEvents9. We gate the seg double-draw
// so it only fires during GTA's own scene render — MTA's overlay / GUI /
// tonemap passes issue their own draws that would otherwise paint the entire
// seg surface with MTA-internal textures.
extern std::atomic<bool> g_bInGTAScene;
extern std::atomic<bool> g_bInMTAScene;

// Forward declaration — definition lives further down near RenderDepthMap.
static bool TryInstallIntz(IDirect3DDevice9* pDevice, int width, int height,
                           IDirect3DTexture9*& outTexture,
                           IDirect3DSurface9*& outSurface,
                           IDirect3DSurface9*& outOriginalDS);

CMultiModalCapture::CMultiModalCapture()
    : m_pRGBSurface(nullptr),
      m_pSegmentationSurface(nullptr),
      m_pSegmentationSnapshot(nullptr),
      m_pSegDepthStencil(nullptr),
      m_pDepthSurface(nullptr),
      m_pBackBufferRef(nullptr),
      m_pDevice(nullptr),
      m_pD3D11Device(nullptr),
      m_pD3D11Context(nullptr),
      m_pDepthVisualizationShader(nullptr),
      m_pFullscreenQuadVS(nullptr),
      m_pConstantColorShader(nullptr),
      m_pFullscreenQuadVB(nullptr),
      m_bSegmentationEnabled(false),
      m_bSegSurfaceNeedsClear(true),
      m_pIntzTexture(nullptr),
      m_pIntzSurface(nullptr),
      m_pOriginalDepthStencil(nullptr),
      m_bIntzInstalled(false),
      m_bInitialized(false),
      m_iCaptureWidth(0),
      m_iCaptureHeight(0)
{
}

CMultiModalCapture::~CMultiModalCapture()
{
    Shutdown();
}

bool CMultiModalCapture::Initialize(IDirect3DDevice9* pDevice, int width, int height)
{
    if (m_bInitialized)
        return true;

    if (!pDevice)
        return false;

    m_pDevice        = pDevice;
    m_iCaptureWidth  = width;
    m_iCaptureHeight = height;

    if (!CreateRenderTargets(width, height))
    {
        Shutdown();
        return false;
    }

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   nullptr, 0, D3D11_SDK_VERSION,
                                   &m_pD3D11Device, &featureLevel, &m_pD3D11Context);
    if (FAILED(hr))
    {
        OutputDebugString("[CMultiModalCapture] D3D11CreateDevice failed\n");
        Shutdown();
        return false;
    }

    if (!m_D3D9To11Converter.Initialize(pDevice, m_pD3D11Device))
    {
        OutputDebugString("[CMultiModalCapture] D3D9To11Converter init failed\n");
        Shutdown();
        return false;
    }

    if (!CreateShaders(pDevice))
    {
        OutputDebugString("[CMultiModalCapture] CreateShaders failed\n");
        Shutdown();
        return false;
    }

    if (!CreateFullscreenQuad(pDevice))
    {
        OutputDebugString("[CMultiModalCapture] CreateFullscreenQuad failed\n");
        Shutdown();
        return false;
    }

    // INTZ depth-stencil install. Non-fatal on failure — depth modality just
    // stays blank on adapters that don't support it.
    m_bIntzInstalled = TryInstallIntz(pDevice, width, height,
                                       m_pIntzTexture, m_pIntzSurface, m_pOriginalDepthStencil);

    // One worker per modality is plenty: captureMultiModalFrame submits at
    // most three save tasks at a time and blocks until they all finish.
    int poolSize = std::min<int>(4, std::max<int>(2, static_cast<int>(std::thread::hardware_concurrency())));
    m_pSaveWorkerPool = std::make_unique<CSaveWorkerPool>(poolSize);

    m_bInitialized = true;
    return true;
}

void CMultiModalCapture::Shutdown()
{
    // Stop encoders first; Stop() is idempotent.
    m_RGBVideoEncoder.Stop();
    m_SegmentationVideoEncoder.Stop();
    m_DepthVideoEncoder.Stop();

    // Joins workers.
    m_pSaveWorkerPool.reset();

    // Restore original depth-stencil before releasing our INTZ so the device
    // doesn't end up with a dangling DS pointer.
    if (m_bIntzInstalled && m_pDevice)
    {
        m_pDevice->SetDepthStencilSurface(m_pOriginalDepthStencil);
        m_bIntzInstalled = false;
    }
    if (m_pOriginalDepthStencil) { m_pOriginalDepthStencil->Release(); m_pOriginalDepthStencil = nullptr; }
    if (m_pIntzSurface)          { m_pIntzSurface->Release();          m_pIntzSurface = nullptr; }
    if (m_pIntzTexture)          { m_pIntzTexture->Release();          m_pIntzTexture = nullptr; }

    ReleaseShaders();
    ReleaseFullscreenQuad();
    m_D3D9To11Converter.Shutdown();
    ReleaseRenderTargets();

    if (m_pD3D11Context) { m_pD3D11Context->Release(); m_pD3D11Context = nullptr; }
    if (m_pD3D11Device)  { m_pD3D11Device->Release();  m_pD3D11Device  = nullptr; }

    m_pDevice      = nullptr;
    m_bInitialized = false;
}

bool CMultiModalCapture::CreateRenderTargets(int width, int height)
{
    if (!m_pDevice)
        return false;

    HRESULT hr;

    hr = m_pDevice->CreateRenderTarget(width, height, D3DFMT_X8R8G8B8,
                                       D3DMULTISAMPLE_NONE, 0, FALSE, &m_pRGBSurface, nullptr);
    if (FAILED(hr)) { OutputDebugString("[CMultiModalCapture] RGB RT create failed\n"); return false; }

    hr = m_pDevice->CreateRenderTarget(width, height, D3DFMT_X8R8G8B8,
                                       D3DMULTISAMPLE_NONE, 0, FALSE, &m_pSegmentationSurface, nullptr);
    if (FAILED(hr)) { OutputDebugString("[CMultiModalCapture] Seg RT create failed\n"); ReleaseRenderTargets(); return false; }

    hr = m_pDevice->CreateRenderTarget(width, height, D3DFMT_X8R8G8B8,
                                       D3DMULTISAMPLE_NONE, 0, FALSE, &m_pSegmentationSnapshot, nullptr);
    if (FAILED(hr)) { OutputDebugString("[CMultiModalCapture] Seg snapshot RT create failed\n"); ReleaseRenderTargets(); return false; }

    hr = m_pDevice->CreateRenderTarget(width, height, D3DFMT_X8R8G8B8,
                                       D3DMULTISAMPLE_NONE, 0, FALSE, &m_pDepthSurface, nullptr);
    if (FAILED(hr)) { OutputDebugString("[CMultiModalCapture] Depth RT create failed\n"); ReleaseRenderTargets(); return false; }

    // Seg-owned depth-stencil. Must match the seg RT's dimensions; non-MSAA
    // matches the seg RT's non-MSAA config. Discard=TRUE — we reclear each
    // frame so the driver is free to throw away contents on RT swap.
    hr = m_pDevice->CreateDepthStencilSurface(width, height, D3DFMT_D24S8,
                                              D3DMULTISAMPLE_NONE, 0, TRUE,
                                              &m_pSegDepthStencil, nullptr);
    if (FAILED(hr)) { OutputDebugString("[CMultiModalCapture] Seg DS create failed\n"); ReleaseRenderTargets(); return false; }

    return true;
}

void CMultiModalCapture::ReleaseRenderTargets()
{
    if (m_pRGBSurface)            { m_pRGBSurface->Release();            m_pRGBSurface = nullptr; }
    if (m_pSegmentationSurface)   { m_pSegmentationSurface->Release();   m_pSegmentationSurface = nullptr; }
    if (m_pSegmentationSnapshot)  { m_pSegmentationSnapshot->Release();  m_pSegmentationSnapshot = nullptr; }
    if (m_pSegDepthStencil)       { m_pSegDepthStencil->Release();       m_pSegDepthStencil = nullptr; }
    if (m_pDepthSurface)          { m_pDepthSurface->Release();          m_pDepthSurface = nullptr; }
    if (m_pBackBufferRef)         { m_pBackBufferRef->Release();         m_pBackBufferRef = nullptr; }
}

// HLSL source for the depth visualization pass. Samples the bound INTZ texture
// at the quad's UV and writes the raw depth (0..1 in clip space) as grayscale
// into the destination RT. Linearization is left to the offline post-processor
// so this shader stays simple and model-agnostic.
// Passthrough VS for fullscreen quads. Vertices are already in clip space
// (Z=0.5) and carry an unmodified TEXCOORD0. A paired VS+PS ensures the
// driver interpolates UV per-pixel correctly, which is NOT reliable with
// XYZRHW+PS-only on some drivers (we observed a 4-pixel UV aliasing pattern
// on NVIDIA that corrupted the depth output).
static const char kFullscreenQuadVS[] =
    "struct VS_IN  { float4 pos : POSITION0; float2 uv : TEXCOORD0; };\n"
    "struct VS_OUT { float4 pos : POSITION0; float2 uv : TEXCOORD0; };\n"
    "VS_OUT main(VS_IN i) {\n"
    "    VS_OUT o;\n"
    "    o.pos = i.pos;\n"
    "    o.uv  = i.uv;\n"
    "    return o;\n"
    "}\n";

static const char kDepthVisPS[] =
    "sampler2D g_Depth : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    "    float z = tex2D(g_Depth, uv).x;\n"
    "    return float4(z, z, z, 1.0);\n"
    "}\n";

// Constant-color PS used by the segmentation double-draw. Outputs whatever
// color the CPU side pushed into ps constant c0 (alpha forced to 1.0 to keep
// blending predictable when the game has ALPHABLENDENABLE on).
static const char kConstantColorPS[] =
    "float4 g_Color : register(c0);\n"
    "float4 main() : COLOR0 {\n"
    "    return float4(g_Color.rgb, 1.0);\n"
    "}\n";

static bool CompilePixelShader(IDirect3DDevice9* pDevice, const char* source, size_t sourceLen,
                               IDirect3DPixelShader9** outShader, const char* label)
{
    ID3DXBuffer* pCode = nullptr;
    ID3DXBuffer* pErr  = nullptr;
    HRESULT hr = D3DXCompileShader(source, sourceLen, nullptr, nullptr, "main", "ps_2_0",
                                   0, &pCode, &pErr, nullptr);
    if (FAILED(hr))
    {
        if (pErr)
        {
            OutputDebugString("[CMultiModalCapture] ");
            OutputDebugString(label);
            OutputDebugString(" compile error: ");
            OutputDebugString(static_cast<const char*>(pErr->GetBufferPointer()));
            OutputDebugString("\n");
            pErr->Release();
        }
        return false;
    }
    if (pErr) pErr->Release();

    hr = pDevice->CreatePixelShader(static_cast<const DWORD*>(pCode->GetBufferPointer()), outShader);
    pCode->Release();
    return SUCCEEDED(hr);
}

static bool CompileVertexShader(IDirect3DDevice9* pDevice, const char* source, size_t sourceLen,
                                IDirect3DVertexShader9** outShader, const char* label)
{
    ID3DXBuffer* pCode = nullptr;
    ID3DXBuffer* pErr  = nullptr;
    HRESULT hr = D3DXCompileShader(source, sourceLen, nullptr, nullptr, "main", "vs_2_0",
                                   0, &pCode, &pErr, nullptr);
    if (FAILED(hr))
    {
        if (pErr)
        {
            OutputDebugString("[CMultiModalCapture] ");
            OutputDebugString(label);
            OutputDebugString(" compile error: ");
            OutputDebugString(static_cast<const char*>(pErr->GetBufferPointer()));
            OutputDebugString("\n");
            pErr->Release();
        }
        return false;
    }
    if (pErr) pErr->Release();

    hr = pDevice->CreateVertexShader(static_cast<const DWORD*>(pCode->GetBufferPointer()), outShader);
    pCode->Release();
    return SUCCEEDED(hr);
}

bool CMultiModalCapture::CreateShaders(IDirect3DDevice9* pDevice)
{
    if (!pDevice) return false;

    if (!CompileVertexShader(pDevice, kFullscreenQuadVS, sizeof(kFullscreenQuadVS) - 1,
                             &m_pFullscreenQuadVS, "Fullscreen quad VS"))
        return false;

    if (!CompilePixelShader(pDevice, kDepthVisPS, sizeof(kDepthVisPS) - 1,
                            &m_pDepthVisualizationShader, "Depth PS"))
        return false;

    if (!CompilePixelShader(pDevice, kConstantColorPS, sizeof(kConstantColorPS) - 1,
                            &m_pConstantColorShader, "Segmentation PS"))
        return false;

    return true;
}

void CMultiModalCapture::ReleaseShaders()
{
    if (m_pDepthVisualizationShader) { m_pDepthVisualizationShader->Release(); m_pDepthVisualizationShader = nullptr; }
    if (m_pFullscreenQuadVS)         { m_pFullscreenQuadVS->Release();         m_pFullscreenQuadVS = nullptr; }
    if (m_pConstantColorShader)      { m_pConstantColorShader->Release();      m_pConstantColorShader = nullptr; }
}

// XYZ clip-space vertices paired with the passthrough VS. This replaces the
// earlier XYZRHW+FFP approach which produced per-pixel UV aliasing on NVIDIA.
struct SFullscreenQuadVertex
{
    float x, y, z;
    float u, v;
};
static const DWORD kQuadFVF = D3DFVF_XYZ | D3DFVF_TEX1;

bool CMultiModalCapture::CreateFullscreenQuad(IDirect3DDevice9* pDevice)
{
    if (!pDevice) return false;

    // Clip-space fullscreen quad. TRIANGLESTRIP: TL -> TR -> BL -> BR.
    // Y is +1 at the top of the screen in D3D9 NDC; UV y=0 is top of texture.
    SFullscreenQuadVertex verts[4] = {
        { -1.0f,  1.0f, 0.5f, 0.0f, 0.0f },   // top-left
        {  1.0f,  1.0f, 0.5f, 1.0f, 0.0f },   // top-right
        { -1.0f, -1.0f, 0.5f, 0.0f, 1.0f },   // bottom-left
        {  1.0f, -1.0f, 0.5f, 1.0f, 1.0f },   // bottom-right
    };

    HRESULT hr = pDevice->CreateVertexBuffer(sizeof(verts), D3DUSAGE_WRITEONLY, kQuadFVF,
                                             D3DPOOL_MANAGED, &m_pFullscreenQuadVB, nullptr);
    if (FAILED(hr)) return false;

    void* pData = nullptr;
    if (FAILED(m_pFullscreenQuadVB->Lock(0, sizeof(verts), &pData, 0)))
    {
        ReleaseFullscreenQuad();
        return false;
    }
    memcpy(pData, verts, sizeof(verts));
    m_pFullscreenQuadVB->Unlock();
    return true;
}

void CMultiModalCapture::ReleaseFullscreenQuad()
{
    if (m_pFullscreenQuadVB) { m_pFullscreenQuadVB->Release(); m_pFullscreenQuadVB = nullptr; }
}

bool CMultiModalCapture::CaptureBackbuffer(IDirect3DDevice9* pDevice)
{
    if (!pDevice || !m_pRGBSurface) return false;

    IDirect3DSurface9* pBackBuffer = nullptr;
    HRESULT hr = pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
    if (FAILED(hr) || !pBackBuffer) return false;

    hr = pDevice->StretchRect(pBackBuffer, nullptr, m_pRGBSurface, nullptr, D3DTEXF_NONE);
    pBackBuffer->Release();
    return SUCCEEDED(hr);
}

bool CMultiModalCapture::RenderSegmentation(IDirect3DDevice9* pDevice)
{
    if (!pDevice || !m_pSegmentationSurface) return false;

    // When the double-draw hook is armed the seg RT has been filled during the
    // frame's rendering — don't touch it. Otherwise emit a deterministic black
    // so consumers can tell "not captured" from "captured and empty".
    if (m_bSegmentationEnabled)
        return true;

    IDirect3DSurface9* pOld = nullptr;
    pDevice->GetRenderTarget(0, &pOld);
    pDevice->SetRenderTarget(0, m_pSegmentationSurface);
    pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    pDevice->SetRenderTarget(0, pOld);
    if (pOld) pOld->Release();
    return true;
}

namespace
{
    constexpr D3DFORMAT INTZ_FORMAT = static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'));
}

// Try to install an INTZ depth-stencil on the device so the game's normal
// depth writes land in a texture we can sample in RenderDepthMap. Returns
// true iff m_pIntzSurface is now the active depth-stencil.
static bool TryInstallIntz(IDirect3DDevice9* pDevice,
                           int width, int height,
                           IDirect3DTexture9*& outTexture,
                           IDirect3DSurface9*& outSurface,
                           IDirect3DSurface9*& outOriginalDS)
{
    if (!pDevice) return false;

    IDirect3D9* pD3D = nullptr;
    if (FAILED(pDevice->GetDirect3D(&pD3D)) || !pD3D) return false;

    D3DDEVICE_CREATION_PARAMETERS params;
    if (FAILED(pDevice->GetCreationParameters(&params))) { pD3D->Release(); return false; }

    D3DDISPLAYMODE mode;
    if (FAILED(pD3D->GetAdapterDisplayMode(params.AdapterOrdinal, &mode))) { pD3D->Release(); return false; }

    HRESULT hr = pD3D->CheckDeviceFormat(params.AdapterOrdinal, params.DeviceType,
                                         mode.Format, D3DUSAGE_DEPTHSTENCIL,
                                         D3DRTYPE_TEXTURE, INTZ_FORMAT);
    pD3D->Release();
    if (FAILED(hr))
    {
        OutputDebugString("[CMultiModalCapture] INTZ not supported on this adapter — depth modality will be blank\n");
        return false;
    }

    IDirect3DTexture9* pTex = nullptr;
    hr = pDevice->CreateTexture(width, height, 1, D3DUSAGE_DEPTHSTENCIL,
                                INTZ_FORMAT, D3DPOOL_DEFAULT, &pTex, nullptr);
    if (FAILED(hr) || !pTex) return false;

    IDirect3DSurface9* pSurf = nullptr;
    hr = pTex->GetSurfaceLevel(0, &pSurf);
    if (FAILED(hr) || !pSurf) { pTex->Release(); return false; }

    // Save whatever auto depth-stencil D3D installed so we can put it back on
    // Shutdown (lets the device continue using its own DS if our INTZ gets
    // released before the device is torn down).
    pDevice->GetDepthStencilSurface(&outOriginalDS);

    hr = pDevice->SetDepthStencilSurface(pSurf);
    if (FAILED(hr))
    {
        if (outOriginalDS) { outOriginalDS->Release(); outOriginalDS = nullptr; }
        pSurf->Release();
        pTex->Release();
        return false;
    }

    outTexture = pTex;
    outSurface = pSurf;
    return true;
}

bool CMultiModalCapture::RenderDepthMap(IDirect3DDevice9* pDevice)
{
    if (!pDevice || !m_pDepthSurface) return false;

    // If INTZ isn't active or the shader didn't compile, emit a deterministic
    // "no data" frame rather than leaving the surface undefined.
    if (!m_bIntzInstalled || !m_pIntzTexture || !m_pDepthVisualizationShader || !m_pFullscreenQuadVB)
    {
        IDirect3DSurface9* pOld = nullptr;
        pDevice->GetRenderTarget(0, &pOld);
        pDevice->SetRenderTarget(0, m_pDepthSurface);
        pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 255, 255), 1.0f, 0);
        pDevice->SetRenderTarget(0, pOld);
        if (pOld) pOld->Release();
        return true;
    }

    // Preserve the slice of device state we touch. IDirect3DStateBlock9 with
    // D3DSBT_ALL captures shaders, RT, streams, samplers, FVF — exactly what we need.
    IDirect3DStateBlock9* pStateBlock = nullptr;
    if (FAILED(pDevice->CreateStateBlock(D3DSBT_ALL, &pStateBlock)))
        return false;

    pDevice->SetRenderTarget(0, m_pDepthSurface);
    // D3D9 forbids reading a resource that's currently the depth-stencil, so
    // detach the DS before binding the INTZ texture as sampler input. The
    // state block's Apply will restore our DS surface afterwards.
    pDevice->SetDepthStencilSurface(nullptr);

    pDevice->SetRenderState(D3DRS_ZENABLE,         FALSE);
    pDevice->SetRenderState(D3DRS_ZWRITEENABLE,    FALSE);
    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_ALPHATESTENABLE,  FALSE);
    pDevice->SetRenderState(D3DRS_CULLMODE,         D3DCULL_NONE);
    pDevice->SetRenderState(D3DRS_LIGHTING,         FALSE);
    pDevice->SetRenderState(D3DRS_FOGENABLE,        FALSE);
    pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);

    pDevice->SetPixelShader(m_pDepthVisualizationShader);
    pDevice->SetVertexShader(m_pFullscreenQuadVS);
    pDevice->SetFVF(kQuadFVF);

    pDevice->SetTexture(0, m_pIntzTexture);
    pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    pDevice->SetSamplerState(0, D3DSAMP_ADDRESSU,  D3DTADDRESS_CLAMP);
    pDevice->SetSamplerState(0, D3DSAMP_ADDRESSV,  D3DTADDRESS_CLAMP);

    pDevice->SetStreamSource(0, m_pFullscreenQuadVB, 0, sizeof(SFullscreenQuadVertex));
    pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);

    pStateBlock->Apply();
    pStateBlock->Release();
    return true;
}

CVideoEncoder* CMultiModalCapture::EncoderForModality(int modalityId)
{
    switch (static_cast<EModality>(modalityId))
    {
        case EModality::RGB:          return &m_RGBVideoEncoder;
        case EModality::SEGMENTATION: return &m_SegmentationVideoEncoder;
        case EModality::DEPTH:        return &m_DepthVideoEncoder;
    }
    return nullptr;
}

bool CMultiModalCapture::StartVideoRecording(int modalityId, const std::string& videoPath,
                                             int width, int height, int fps, int bitrate)
{
    if (!m_bInitialized) return false;
    CVideoEncoder* pEnc = EncoderForModality(modalityId);
    if (!pEnc) return false;
    if (pEnc->IsInitialized()) return true;     // already running — idempotent
    return pEnc->Start(videoPath, width, height, fps, bitrate);
}

bool CMultiModalCapture::StopVideoRecording(int modalityId)
{
    CVideoEncoder* pEnc = EncoderForModality(modalityId);
    if (!pEnc) return false;
    return pEnc->Stop();
}

bool CMultiModalCapture::ReadbackToSysMem(IDirect3DSurface9* pRTSurface, SReadbackSurface& out) const
{
    if (!pRTSurface || !m_pDevice) return false;

    D3DSURFACE_DESC desc;
    if (FAILED(pRTSurface->GetDesc(&desc))) return false;

    IDirect3DSurface9* pSysMem = nullptr;
    HRESULT hr = m_pDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                        D3DPOOL_SYSTEMMEM, &pSysMem, nullptr);
    if (FAILED(hr) || !pSysMem) return false;

    hr = m_pDevice->GetRenderTargetData(pRTSurface, pSysMem);
    if (FAILED(hr)) { pSysMem->Release(); return false; }

    D3DLOCKED_RECT locked;
    hr = pSysMem->LockRect(&locked, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr)) { pSysMem->Release(); return false; }

    out.pSurface    = pSysMem;
    out.lockedBits  = static_cast<const uint8_t*>(locked.pBits);
    out.lockedPitch = locked.Pitch;
    out.width       = desc.Width;
    out.height      = desc.Height;
    return true;
}

void CMultiModalCapture::ReleaseReadback(SReadbackSurface& s) const
{
    if (s.pSurface)
    {
        s.pSurface->UnlockRect();
        s.pSurface->Release();
        s.pSurface = nullptr;
    }
    s.lockedBits  = nullptr;
    s.lockedPitch = 0;
    s.width = s.height = 0;
}

bool CMultiModalCapture::CaptureMultiModalFrame(const std::string& rgbPath,
                                                const std::string& segPath,
                                                const std::string& depthPath,
                                                bool saveRgbToVideo,
                                                bool saveSegToVideo,
                                                bool saveDepthToVideo,
                                                int  jpegQuality)
{
    if (!m_bInitialized || !m_pDevice || !m_pSaveWorkerPool)
        return false;

    const bool wantRGB   = !rgbPath.empty()   || saveRgbToVideo;
    const bool wantSeg   = !segPath.empty()   || saveSegToVideo;
    const bool wantDepth = !depthPath.empty() || saveDepthToVideo;

    if (!wantRGB && !wantSeg && !wantDepth)
        return true;    // nothing requested, nothing to do

    // --- 1. Snapshot each requested modality's RT on the main thread (D3D9). ---
    // RGB is populated by OnPresent (before the backbuffer is discarded) — we
    // just read m_pRGBSurface here; CaptureBackbuffer would overwrite it with
    // post-Present garbage. Seg is populated in-flight by the draw-call hook.
    // Depth is converted on demand from the INTZ texture.
    if (wantSeg   && !RenderSegmentation(m_pDevice))   { OutputDebugString("[CMultiModalCapture] Seg snapshot failed\n");   return false; }
    if (wantDepth && !RenderDepthMap(m_pDevice))       { OutputDebugString("[CMultiModalCapture] Depth snapshot failed\n"); return false; }

    // --- 2. Submit to video encoders (main thread; MF is CPU and sync). ---
    if (wantRGB && saveRgbToVideo && m_RGBVideoEncoder.IsInitialized())
    {
        if (ID3D11Texture2D* tex = ConvertD3D9ToD3D11(m_pRGBSurface))
        {
            m_RGBVideoEncoder.AddFrame(tex);
            tex->Release();
        }
    }
    if (wantSeg && saveSegToVideo && m_SegmentationVideoEncoder.IsInitialized())
    {
        if (ID3D11Texture2D* tex = ConvertD3D9ToD3D11(m_pSegmentationSurface))
        {
            m_SegmentationVideoEncoder.AddFrame(tex);
            tex->Release();
        }
    }
    if (wantDepth && saveDepthToVideo && m_DepthVideoEncoder.IsInitialized())
    {
        if (ID3D11Texture2D* tex = ConvertD3D9ToD3D11(m_pDepthSurface))
        {
            m_DepthVideoEncoder.AddFrame(tex);
            tex->Release();
        }
    }

    // --- 3. Readback the three modalities to sysmem on the main thread
    //        (D3D9 readback must happen on the device thread). ---
    SReadbackSurface rgbRead{}, segRead{}, depthRead{};
    bool readbackOk = true;
    if (!rgbPath.empty())
        readbackOk = ReadbackToSysMem(m_pRGBSurface, rgbRead) && readbackOk;
    // Read the seg SNAPSHOT, not the live surface — the live one is already
    // being overwritten by the current frame's draws by the time we get here.
    if (!segPath.empty())
        readbackOk = ReadbackToSysMem(m_pSegmentationSnapshot, segRead) && readbackOk;
    if (!depthPath.empty())
        readbackOk = ReadbackToSysMem(m_pDepthSurface, depthRead) && readbackOk;

    if (!readbackOk)
    {
        ReleaseReadback(rgbRead);
        ReleaseReadback(segRead);
        ReleaseReadback(depthRead);
        return false;
    }

    // --- 4. Dispatch encode+write tasks to the worker pool. ---
    std::vector<std::future<void>> futures;
    std::atomic<int>               failures{0};

    auto submitSave = [&](const SReadbackSurface& rb, const std::string& path, bool indexed, bool jpeg)
    {
        if (path.empty() || !rb.lockedBits) return;

        futures.push_back(m_pSaveWorkerPool->Submit([&failures, rb, path, indexed, jpeg, jpegQuality]()
        {
            bool ok;
            if (jpeg)
                ok = ModalityImageWriter::SaveJPEG(path, rb.lockedBits, rb.width, rb.height, rb.lockedPitch, jpegQuality);
            else if (indexed)
                ok = ModalityImageWriter::SaveIndexedPNG(path, rb.lockedBits, rb.width, rb.height, rb.lockedPitch);
            else
                ok = ModalityImageWriter::SavePNG(path, rb.lockedBits, rb.width, rb.height, rb.lockedPitch);

            if (!ok)
                failures.fetch_add(1, std::memory_order_relaxed);
        }));
    };

    // RGB: path extension decides PNG vs JPEG. Keep it simple — if the path
    // ends with ".jpg"/".jpeg" use JPEG, otherwise PNG.
    auto isJpegPath = [](const std::string& p) -> bool
    {
        if (p.size() < 4) return false;
        std::string lower(p);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return lower.size() >= 4 && (lower.compare(lower.size() - 4, 4, ".jpg") == 0 ||
                                      (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".jpeg") == 0));
    };

    submitSave(rgbRead,   rgbPath,   /*indexed=*/false, /*jpeg=*/isJpegPath(rgbPath));
    submitSave(segRead,   segPath,   /*indexed=*/true,  /*jpeg=*/false);            // seg is always 8bpp PNG
    submitSave(depthRead, depthPath, /*indexed=*/false, /*jpeg=*/isJpegPath(depthPath));

    // --- 5. Block until all workers finish. ---
    for (std::future<void>& f : futures)
        f.wait();

    // --- 6. Release locked sysmem surfaces on the main thread. ---
    ReleaseReadback(rgbRead);
    ReleaseReadback(segRead);
    ReleaseReadback(depthRead);

    return failures.load(std::memory_order_relaxed) == 0;
}

void CMultiModalCapture::OnPresent(IDirect3DDevice9* pDevice)
{
    if (!m_bInitialized || !pDevice) return;

    // Snapshot the backbuffer — the ONLY reliable moment, before the DISCARD
    // swap chain invalidates its content at Present() time.
    // Also refresh the weak backbuffer pointer used by the seg replay gate.
    IDirect3DSurface9* pBackBuffer = nullptr;
    if (SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer)) && pBackBuffer)
    {
        if (m_pRGBSurface)
            pDevice->StretchRect(pBackBuffer, nullptr, m_pRGBSurface, nullptr, D3DTEXF_NONE);

        if (m_pBackBufferRef != pBackBuffer)
        {
            if (m_pBackBufferRef) m_pBackBufferRef->Release();
            pBackBuffer->AddRef();
            m_pBackBufferRef = pBackBuffer;
        }
        pBackBuffer->Release();
    }

    // Snapshot the seg surface too. captureMultiModalFrame fires from a Lua
    // event that runs AFTER Present, which is already inside frame N+1 — by
    // then the first Emit* of N+1 has cleared the live seg surface and only
    // the first few N+1 draws (usually just the sky) have accumulated.
    // Reading the snapshot here gives us the complete frame N content.
    if (m_pSegmentationSurface && m_pSegmentationSnapshot)
        pDevice->StretchRect(m_pSegmentationSurface, nullptr, m_pSegmentationSnapshot, nullptr, D3DTEXF_NONE);

    // Next-frame seg clear scheduled after the snapshot is safely made.
    m_bSegSurfaceNeedsClear = true;
}

void CMultiModalCapture::SetSegmentationEnabled(bool enabled)
{
    m_bSegmentationEnabled = enabled;
}

// Installs the seg-capture state (RT = seg surface, PS = const color, color
// constant filled from the current sampler-0 texture). Returns the state block
// to Apply() on the way out. Returns nullptr on any failure — caller emits no
// draw in that case.
static IDirect3DStateBlock9* SetupSegmentationState(IDirect3DDevice9* pDevice,
                                                    IDirect3DSurface9* pSegSurface,
                                                    IDirect3DSurface9* pSegDS,
                                                    IDirect3DPixelShader9* pConstPS,
                                                    CTextureRegistry& registry)
{
    if (!pDevice || !pSegSurface || !pSegDS || !pConstPS) return nullptr;

    IDirect3DStateBlock9* pBlock = nullptr;
    if (FAILED(pDevice->CreateStateBlock(D3DSBT_ALL, &pBlock)) || !pBlock)
        return nullptr;

    // Resolve sampler-0 texture to its SA RenderWare name via MTA's D3D-to-RW
    // map. The map is keyed on the *wrapped* CD3DDUMMY* that MTA tracks — the
    // pointer the game code sees before the proxy's SetTexture unwraps it via
    // GetRealTexture() (see CProxyDirect3DDevice9.cpp:SetTexture). Calling
    // pDevice->GetTexture(0) here would give us the post-unwrap raw pointer,
    // which is NOT what the tracking map is keyed on — every lookup misses and
    // every texture degrades to a pointer-keyed fallback. g_pDeviceState holds
    // the wrapped pointer MTA itself uses in GetAppliedShaderForD3DData.
    IDirect3DBaseTexture9* pTex =
        g_pDeviceState ? g_pDeviceState->TextureState[0].Texture : nullptr;

    std::string key;
    CGame* pGame = CCore::GetSingletonPtr() ? CCore::GetSingletonPtr()->GetGame() : nullptr;
    CRenderWare* pRW = pGame ? pGame->GetRenderWare() : nullptr;
    if (pRW)
    {
        const char* name = pRW->GetTextureName(reinterpret_cast<CD3DDUMMY*>(static_cast<void*>(pTex)));
        if (name && *name)
            key = name;
    }
    if (key.empty())
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "texture_0x%p", static_cast<void*>(pTex));
        key = buf;
    }
    D3DCOLOR color = registry.GetOrAssignColor(key);

    const float psConstant[4] = {
        ((color >> 16) & 0xFF) / 255.0f,
        ((color >>  8) & 0xFF) / 255.0f,
        ( color        & 0xFF) / 255.0f,
        1.0f
    };

    pDevice->SetRenderTarget(0, pSegSurface);
    pDevice->SetDepthStencilSurface(pSegDS);
    pDevice->SetPixelShader(pConstPS);
    pDevice->SetPixelShaderConstantF(0, psConstant, 1);

    // The caller gets exclusive access to the seg RT+DS for the duration of
    // this draw. Clearing is orchestrated by the caller via the needs-clear
    // flag so it only happens on the first Emit* of a new frame.

    // Our own DS means we can run a full frontmost-wins depth test against
    // the replays alone, independent of the game's in-flight depth buffer.
    // First-arriving replay wins a pixel; later replays overwrite only if
    // they're closer; behind replays fail the test and leave the pixel alone.
    pDevice->SetRenderState(D3DRS_ZENABLE,          D3DZB_TRUE);
    pDevice->SetRenderState(D3DRS_ZWRITEENABLE,     TRUE);
    pDevice->SetRenderState(D3DRS_ZFUNC,            D3DCMP_LESSEQUAL);
    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    return pBlock;
}

// D3DSBT_ALL's Apply() has been observed to leave RT0 and the depth-stencil
// pointing at *our* seg surface on some drivers. That leak cascades into
// subsequent game draws landing on seg surface instead of the backbuffer —
// the backbuffer is left with only its clear color (sky blue), and both RGB
// and seg outputs degenerate to "one-color sky". Explicit save/restore of RT0
// + DS around the state block eliminates the ambiguity.
template <typename DrawFn>
static void EmitSegmentationCommon(IDirect3DDevice9*      pDevice,
                                   IDirect3DSurface9*     pSegSurface,
                                   IDirect3DSurface9*     pSegDS,
                                   IDirect3DPixelShader9* pConstPS,
                                   CTextureRegistry&      registry,
                                   bool&                  needsClearFlag,
                                   DrawFn                 doDraw)
{
    IDirect3DSurface9* pSavedRT = nullptr;
    IDirect3DSurface9* pSavedDS = nullptr;
    pDevice->GetRenderTarget(0, &pSavedRT);
    pDevice->GetDepthStencilSurface(&pSavedDS);

    IDirect3DStateBlock9* pBlock = SetupSegmentationState(pDevice, pSegSurface, pSegDS, pConstPS, registry);
    if (!pBlock)
    {
        if (pSavedRT) pSavedRT->Release();
        if (pSavedDS) pSavedDS->Release();
        return;
    }

    if (needsClearFlag)
    {
        // Clear both color and depth: color → background sentinel, depth → 1.0
        // (far plane) so any real world-space geometry wins LESSEQUAL.
        pDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
        needsClearFlag = false;
    }

    doDraw(pDevice);

    pBlock->Apply();
    pBlock->Release();

    // Unconditional RT+DS restore. If Apply() already did its job this is a
    // no-op on the driver side (same surfaces). If it didn't, we just saved
    // the game from drawing to our seg surface for the rest of the frame.
    pDevice->SetRenderTarget(0, pSavedRT);
    pDevice->SetDepthStencilSurface(pSavedDS);
    if (pSavedRT) pSavedRT->Release();
    if (pSavedDS) pSavedDS->Release();
}

static bool IsInGtaSceneOnly()
{
    return g_bInGTAScene.load(std::memory_order_acquire)
        && !g_bInMTAScene.load(std::memory_order_acquire);
}

// Only replay draws whose current RT is the primary backbuffer. Reflection,
// water, shadow and similar off-screen passes target auxiliary RTs; their
// geometry is authored in NDC that fills those sub-targets, so replaying
// them onto our full-resolution seg RT would paint the whole seg surface
// one color. These passes don't contribute to the final screenshot anyway.
static bool IsDrawingToBackbuffer(IDirect3DDevice9* pDevice, IDirect3DSurface9* pBackBufferRef)
{
    if (!pBackBufferRef) return false;
    IDirect3DSurface9* pCurRT = nullptr;
    pDevice->GetRenderTarget(0, &pCurRT);
    bool match = (pCurRT == pBackBufferRef);
    if (pCurRT) pCurRT->Release();
    return match;
}

void CMultiModalCapture::EmitSegmentationDraw(IDirect3DDevice9* pDevice,
                                              unsigned int primitiveType,
                                              unsigned int startVertex,
                                              unsigned int primitiveCount)
{
    if (!m_bSegmentationEnabled) return;
    if (!IsInGtaSceneOnly()) return;
    if (!pDevice || !m_pSegmentationSurface || !m_pSegDepthStencil || !m_pConstantColorShader) return;
    if (!IsDrawingToBackbuffer(pDevice, m_pBackBufferRef)) return;

    EmitSegmentationCommon(pDevice, m_pSegmentationSurface, m_pSegDepthStencil,
                           m_pConstantColorShader,
                           m_TextureRegistry, m_bSegSurfaceNeedsClear,
                           [&](IDirect3DDevice9* d) {
        d->DrawPrimitive(static_cast<D3DPRIMITIVETYPE>(primitiveType), startVertex, primitiveCount);
    });
}

void CMultiModalCapture::EmitSegmentationDrawIndexed(IDirect3DDevice9* pDevice,
                                                     unsigned int primitiveType,
                                                     int          baseVertexIndex,
                                                     unsigned int minVertexIndex,
                                                     unsigned int numVertices,
                                                     unsigned int startIndex,
                                                     unsigned int primitiveCount)
{
    if (!m_bSegmentationEnabled) return;
    if (!IsInGtaSceneOnly()) return;
    if (!pDevice || !m_pSegmentationSurface || !m_pSegDepthStencil || !m_pConstantColorShader) return;
    if (!IsDrawingToBackbuffer(pDevice, m_pBackBufferRef)) return;

    EmitSegmentationCommon(pDevice, m_pSegmentationSurface, m_pSegDepthStencil,
                           m_pConstantColorShader,
                           m_TextureRegistry, m_bSegSurfaceNeedsClear,
                           [&](IDirect3DDevice9* d) {
        d->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(primitiveType),
                                baseVertexIndex, minVertexIndex, numVertices,
                                startIndex, primitiveCount);
    });
}

bool CMultiModalCapture::WriteMappingJson(const std::string& path) const
{
    if (path.empty()) return false;

    // Ensure parent directory exists.
    size_t pos = 0;
    while ((pos = path.find_first_of("\\/", pos + 1)) != std::string::npos)
        CreateDirectoryA(path.substr(0, pos).c_str(), nullptr);

    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;

    const auto& mapping = m_TextureRegistry.GetMapping();
    out << "{\n";
    bool first = true;
    for (const auto& kv : mapping)
    {
        const std::string& name  = kv.first;
        D3DCOLOR           color = kv.second;
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >>  8) & 0xFF;
        uint8_t b =  color        & 0xFF;

        if (!first) out << ",\n";
        first = false;

        // Minimal JSON string escape — texture names may contain backslashes or quotes.
        out << "  \"";
        for (char c : name)
        {
            switch (c)
            {
                case '\\': out << "\\\\"; break;
                case '"':  out << "\\\""; break;
                case '\n': out << "\\n";  break;
                case '\r': out << "\\r";  break;
                case '\t': out << "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04X", (unsigned)(unsigned char)c);
                        out << buf;
                    }
                    else
                        out << c;
                    break;
            }
        }
        out << "\": { \"color\": [" << (int)r << ", " << (int)g << ", " << (int)b << "], \"modelIds\": [] }";
    }
    out << "\n}\n";
    return true;
}

ID3D11Texture2D* CMultiModalCapture::ConvertD3D9ToD3D11(IDirect3DSurface9* pSurface)
{
    if (!pSurface) return nullptr;
    return m_D3D9To11Converter.Convert(pSurface);
}
