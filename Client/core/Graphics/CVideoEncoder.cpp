/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/CVideoEncoder.cpp
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CVideoEncoder.h"
#include <mferror.h>

// Helper function to create directory recursively
static void CreateDirectoryRecursive(const std::string& path)
{
    size_t pos = 0;
    do
    {
        pos = path.find_first_of("\\/", pos + 1);
        std::string subPath = path.substr(0, pos);
        if (!subPath.empty())
            CreateDirectoryA(subPath.c_str(), nullptr);
    } while (pos != std::string::npos);
}

// Media Foundation initialization state
static bool g_bMediaFoundationInitialized = false;

CVideoEncoder::CVideoEncoder()
    : m_pSinkWriter(nullptr)
    , m_dwStreamIndex(0)
    , m_iFrameCount(0)
    , m_iFPS(30)
    , m_iWidth(1920)
    , m_iHeight(1080)
    , m_iBitrate(5000000)
    , m_bInitialized(false)
    , m_pCachedDevice(nullptr)
    , m_pCachedContext(nullptr)
    , m_pCachedStagingTexture(nullptr)
    , m_uCachedStagingWidth(0)
    , m_uCachedStagingHeight(0)
    , m_CachedStagingFormat(DXGI_FORMAT_UNKNOWN)
{
}

CVideoEncoder::~CVideoEncoder()
{
    Stop();
    ReleaseCachedResources();
}

bool CVideoEncoder::Start(const std::string& path, int width, int height, int fps, int bitrate)
{
    if (m_bInitialized)
        return false;

    m_iWidth = width;
    m_iHeight = height;
    m_iFPS = fps;
    m_iBitrate = bitrate;

    if (!InitializeMediaFoundation())
        return false;

    if (!CreateSinkWriter(path))
        return false;

    if (!ConfigureVideoStream())
        return false;

    HRESULT hr = m_pSinkWriter->BeginWriting();
    if (FAILED(hr))
    {
        OutputDebugString("[CVideoEncoder] BeginWriting failed\n");
        return false;
    }

    m_bInitialized = true;
    m_iFrameCount = 0;
    return true;
}

bool CVideoEncoder::AddFrame(ID3D11Texture2D* gpuTexture)
{
    if (!m_bInitialized || !gpuTexture)
        return false;

    IMFSample* pSample = CreateSampleFromTexture(gpuTexture);
    if (!pSample)
        return false;

    // Set sample time and duration
    LONGLONG sampleTime = (m_iFrameCount * 10000000LL) / m_iFPS;
    LONGLONG sampleDuration = 10000000LL / m_iFPS;

    pSample->SetSampleTime(sampleTime);
    pSample->SetSampleDuration(sampleDuration);

    // Write sample
    HRESULT hr = m_pSinkWriter->WriteSample(m_dwStreamIndex, pSample);
    pSample->Release();

    if (FAILED(hr))
        return false;

    m_iFrameCount++;
    return true;
}

bool CVideoEncoder::Stop()
{
    if (!m_bInitialized)
        return true;

    if (m_pSinkWriter)
    {
        m_pSinkWriter->Finalize();
        m_pSinkWriter->Release();
        m_pSinkWriter = nullptr;
    }

    ReleaseCachedResources();

    m_bInitialized = false;
    return true;
}

bool CVideoEncoder::InitializeMediaFoundation()
{
    if (g_bMediaFoundationInitialized)
        return true;

    HRESULT hr = MFStartup(MF_VERSION);
    if (SUCCEEDED(hr))
    {
        g_bMediaFoundationInitialized = true;
        return true;
    }

    return false;
}

bool CVideoEncoder::CreateSinkWriter(const std::string& path)
{
    // Create parent directories if needed
    std::string dir = path.substr(0, path.find_last_of("\\/"));
    if (!dir.empty())
        CreateDirectoryRecursive(dir);

    // Convert to wide string
    int len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    wchar_t* wPath = new wchar_t[len];
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wPath, len);

    IMFAttributes* pAttributes = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr))
    {
        delete[] wPath;
        return false;
    }

    pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

    hr = MFCreateSinkWriterFromURL(wPath, nullptr, pAttributes, &m_pSinkWriter);

    pAttributes->Release();
    delete[] wPath;

    return SUCCEEDED(hr);
}

bool CVideoEncoder::ConfigureVideoStream()
{
    IMFMediaType* pOutputType = nullptr;
    IMFMediaType* pInputType = nullptr;

    HRESULT hr = MFCreateMediaType(&pOutputType);
    if (FAILED(hr))
        return false;

    // Configure output media type (H.264)
    pOutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pOutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    pOutputType->SetUINT32(MF_MT_AVG_BITRATE, m_iBitrate);
    pOutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(pOutputType, MF_MT_FRAME_SIZE, m_iWidth, m_iHeight);
    MFSetAttributeRatio(pOutputType, MF_MT_FRAME_RATE, m_iFPS, 1);
    MFSetAttributeRatio(pOutputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    hr = m_pSinkWriter->AddStream(pOutputType, &m_dwStreamIndex);
    pOutputType->Release();

    if (FAILED(hr))
        return false;

    // Configure input media type (RGB32)
    hr = MFCreateMediaType(&pInputType);
    if (FAILED(hr))
        return false;

    pInputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pInputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    pInputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(pInputType, MF_MT_FRAME_SIZE, m_iWidth, m_iHeight);
    MFSetAttributeRatio(pInputType, MF_MT_FRAME_RATE, m_iFPS, 1);
    MFSetAttributeRatio(pInputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    hr = m_pSinkWriter->SetInputMediaType(m_dwStreamIndex, pInputType, nullptr);
    pInputType->Release();

    return SUCCEEDED(hr);
}

bool CVideoEncoder::EnsureStagingResources(ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC& srcDesc)
{
    // Cache device+context on first use. The device is owned by the caller (CMultiModalCapture),
    // so we hold a ref and drop it in ReleaseCachedResources().
    if (m_pCachedDevice != pDevice)
    {
        ReleaseCachedResources();

        m_pCachedDevice = pDevice;
        m_pCachedDevice->AddRef();
        m_pCachedDevice->GetImmediateContext(&m_pCachedContext);
        if (!m_pCachedContext)
        {
            ReleaseCachedResources();
            return false;
        }
    }

    // Reuse staging texture if dimensions/format unchanged.
    if (m_pCachedStagingTexture
        && m_uCachedStagingWidth == srcDesc.Width
        && m_uCachedStagingHeight == srcDesc.Height
        && m_CachedStagingFormat == srcDesc.Format)
    {
        return true;
    }

    if (m_pCachedStagingTexture)
    {
        m_pCachedStagingTexture->Release();
        m_pCachedStagingTexture = nullptr;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    HRESULT hr = m_pCachedDevice->CreateTexture2D(&stagingDesc, nullptr, &m_pCachedStagingTexture);
    if (FAILED(hr))
    {
        OutputDebugString("[CVideoEncoder] Failed to create cached staging texture\n");
        return false;
    }

    m_uCachedStagingWidth = srcDesc.Width;
    m_uCachedStagingHeight = srcDesc.Height;
    m_CachedStagingFormat = srcDesc.Format;
    return true;
}

void CVideoEncoder::ReleaseCachedResources()
{
    if (m_pCachedStagingTexture)
    {
        m_pCachedStagingTexture->Release();
        m_pCachedStagingTexture = nullptr;
    }
    if (m_pCachedContext)
    {
        m_pCachedContext->Release();
        m_pCachedContext = nullptr;
    }
    if (m_pCachedDevice)
    {
        m_pCachedDevice->Release();
        m_pCachedDevice = nullptr;
    }
    m_uCachedStagingWidth = 0;
    m_uCachedStagingHeight = 0;
    m_CachedStagingFormat = DXGI_FORMAT_UNKNOWN;
}

IMFSample* CVideoEncoder::CreateSampleFromTexture(ID3D11Texture2D* texture)
{
    ID3D11Device* pDevice = nullptr;
    texture->GetDevice(&pDevice);
    if (!pDevice)
        return nullptr;

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    if (!EnsureStagingResources(pDevice, desc))
    {
        pDevice->Release();
        return nullptr;
    }
    pDevice->Release();            // EnsureStagingResources holds its own ref

    m_pCachedContext->CopyResource(m_pCachedStagingTexture, texture);

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_pCachedContext->Map(m_pCachedStagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
        return nullptr;

    IMFMediaBuffer* pBuffer = nullptr;
    DWORD bufferSize = m_iWidth * m_iHeight * 4;
    hr = MFCreateMemoryBuffer(bufferSize, &pBuffer);
    if (FAILED(hr))
    {
        m_pCachedContext->Unmap(m_pCachedStagingTexture, 0);
        return nullptr;
    }

    BYTE* pBufferData = nullptr;
    pBuffer->Lock(&pBufferData, nullptr, nullptr);

    // D3D11 is top-down, MF RGB32 is bottom-up; flip row-by-row.
    for (int y = 0; y < m_iHeight; y++)
    {
        memcpy(
            pBufferData + (y * m_iWidth * 4),
            (BYTE*)mapped.pData + ((m_iHeight - 1 - y) * mapped.RowPitch),
            m_iWidth * 4
        );
    }

    pBuffer->Unlock();
    pBuffer->SetCurrentLength(bufferSize);

    IMFSample* pSample = nullptr;
    hr = MFCreateSample(&pSample);
    if (FAILED(hr))
    {
        pBuffer->Release();
        m_pCachedContext->Unmap(m_pCachedStagingTexture, 0);
        return nullptr;
    }

    pSample->AddBuffer(pBuffer);
    pBuffer->Release();

    m_pCachedContext->Unmap(m_pCachedStagingTexture, 0);
    return pSample;
}
