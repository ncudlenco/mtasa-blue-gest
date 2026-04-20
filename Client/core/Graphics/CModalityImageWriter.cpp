/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/CModalityImageWriter.cpp
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CModalityImageWriter.h"
// wincodec.h on Win10 SDK references DXGI_JPEG_* types without pulling in dxgi headers;
// include dxgi1_3.h first to satisfy those forward refs.
#include <dxgi1_3.h>
#include <wincodec.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    IWICImagingFactory* g_pWICFactory = nullptr;
    std::once_flag      g_WICInitOnce;

    IWICImagingFactory* GetWICFactory()
    {
        std::call_once(g_WICInitOnce,
                       []()
                       {
                           // COM may already be initialized on this thread (game thread almost certainly is);
                           // multi-thread re-init is harmless and required for worker threads in Stage 5.
                           CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                           HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pWICFactory));
                           if (FAILED(hr))
                           {
                               OutputDebugStringA("[ModalityImageWriter] CoCreateInstance(WICImagingFactory) failed\n");
                               g_pWICFactory = nullptr;
                           }
                       });
        return g_pWICFactory;
    }

    void EnsureDirectory(const std::string& filePath)
    {
        size_t pos = 0;
        while ((pos = filePath.find_first_of("\\/", pos + 1)) != std::string::npos)
        {
            std::string sub = filePath.substr(0, pos);
            if (!sub.empty())
                CreateDirectoryA(sub.c_str(), nullptr);
        }
    }

    std::wstring Widen(const std::string& s)
    {
        int          len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(len ? len - 1 : 0, L'\0');
        if (len > 0)
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
        return w;
    }

    // Open a WIC file stream for writing. Caller owns the returned IWICStream.
    IWICStream* OpenWriteStream(IWICImagingFactory* pFactory, const std::string& filePath)
    {
        EnsureDirectory(filePath);

        IWICStream* pStream = nullptr;
        if (FAILED(pFactory->CreateStream(&pStream)))
            return nullptr;

        std::wstring wpath = Widen(filePath);
        if (FAILED(pStream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE)))
        {
            pStream->Release();
            return nullptr;
        }
        return pStream;
    }

    // Repack 4-byte BGRX source rows into tightly packed 3-byte BGR. WIC's PNG
    // and JPEG encoders natively accept 24bppBGR; passing them 32bppBGR and
    // relying on WIC's implicit conversion produces a byte-shifted output on
    // at least some driver/WIC combinations (manifests as a 4-pixel repeating
    // pattern). Explicit repack removes the ambiguity.
    static std::vector<uint8_t> PackBGRXToBGR(const uint8_t* src, int width, int height, int srcStride)
    {
        std::vector<uint8_t> out(static_cast<size_t>(width) * height * 3);
        for (int y = 0; y < height; ++y)
        {
            const uint8_t* r = src + y * srcStride;
            uint8_t*       w = &out[static_cast<size_t>(y) * width * 3];
            for (int x = 0; x < width; ++x)
            {
                w[3 * x + 0] = r[4 * x + 0];
                w[3 * x + 1] = r[4 * x + 1];
                w[3 * x + 2] = r[4 * x + 2];
            }
        }
        return out;
    }

    // Encode a single-frame image via WIC. `configureFrame` sets per-format state
    // (pixel format, palette, encoder options) and writes pixels.
    template <typename ConfigureFrameFn>
    bool EncodeSingleFrame(IWICImagingFactory* pFactory, REFGUID containerFormat, const std::string& filePath, ConfigureFrameFn configureFrame)
    {
        IWICStream* pStream = OpenWriteStream(pFactory, filePath);
        if (!pStream)
            return false;

        IWICBitmapEncoder* pEncoder = nullptr;
        HRESULT            hr = pFactory->CreateEncoder(containerFormat, nullptr, &pEncoder);
        if (FAILED(hr))
        {
            pStream->Release();
            return false;
        }

        hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
        if (FAILED(hr))
        {
            pEncoder->Release();
            pStream->Release();
            return false;
        }

        IWICBitmapFrameEncode* pFrame = nullptr;
        IPropertyBag2*         pFrameProps = nullptr;
        hr = pEncoder->CreateNewFrame(&pFrame, &pFrameProps);
        if (FAILED(hr))
        {
            pEncoder->Release();
            pStream->Release();
            return false;
        }

        bool ok = configureFrame(pFactory, pEncoder, pFrame, pFrameProps);

        if (ok)
            ok = SUCCEEDED(pFrame->Commit());
        if (ok)
            ok = SUCCEEDED(pEncoder->Commit());

        if (pFrameProps)
            pFrameProps->Release();
        pFrame->Release();
        pEncoder->Release();
        pStream->Release();
        return ok;
    }
}

namespace ModalityImageWriter
{

    bool SavePNG(const std::string& filePath, const uint8_t* pixels, int width, int height, int stride)
    {
        if (!pixels || width <= 0 || height <= 0 || stride < width * 4)
            return false;

        IWICImagingFactory* pFactory = GetWICFactory();
        if (!pFactory)
            return false;

        std::vector<uint8_t> bgr = PackBGRXToBGR(pixels, width, height, stride);
        const UINT           packedStride = static_cast<UINT>(width) * 3;

        return EncodeSingleFrame(pFactory, GUID_ContainerFormatPng, filePath,
                                 [&](IWICImagingFactory*, IWICBitmapEncoder*, IWICBitmapFrameEncode* pFrame, IPropertyBag2* pProps) -> bool
                                 {
                                     if (pProps)
                                         pFrame->Initialize(pProps);
                                     else
                                         pFrame->Initialize(nullptr);
                                     pFrame->SetSize(width, height);

                                     WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
                                     if (FAILED(pFrame->SetPixelFormat(&fmt)))
                                         return false;

                                     return SUCCEEDED(pFrame->WritePixels(height, packedStride, packedStride * height, bgr.data()));
                                 });
    }

    bool SaveJPEG(const std::string& filePath, const uint8_t* pixels, int width, int height, int stride, int quality)
    {
        if (!pixels || width <= 0 || height <= 0 || stride < width * 4)
            return false;

        IWICImagingFactory* pFactory = GetWICFactory();
        if (!pFactory)
            return false;

        const float q = (quality < 0 ? 0.0f : (quality > 100 ? 1.0f : quality / 100.0f));

        std::vector<uint8_t> bgr = PackBGRXToBGR(pixels, width, height, stride);
        const UINT           packedStride = static_cast<UINT>(width) * 3;

        return EncodeSingleFrame(pFactory, GUID_ContainerFormatJpeg, filePath,
                                 [&](IWICImagingFactory*, IWICBitmapEncoder*, IWICBitmapFrameEncode* pFrame, IPropertyBag2* pProps) -> bool
                                 {
                                     if (pProps)
                                     {
                                         PROPBAG2 opt = {};
                                         opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
                                         VARIANT v;
                                         VariantInit(&v);
                                         v.vt = VT_R4;
                                         v.fltVal = q;
                                         pProps->Write(1, &opt, &v);
                                         VariantClear(&v);
                                         pFrame->Initialize(pProps);
                                     }
                                     else
                                     {
                                         pFrame->Initialize(nullptr);
                                     }

                                     pFrame->SetSize(width, height);

                                     WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
                                     if (FAILED(pFrame->SetPixelFormat(&fmt)))
                                         return false;

                                     return SUCCEEDED(pFrame->WritePixels(height, packedStride, packedStride * height, bgr.data()));
                                 });
    }

    bool SaveIndexedPNG(const std::string& filePath, const uint8_t* pixels, int width, int height, int stride)
    {
        if (!pixels || width <= 0 || height <= 0 || stride < width * 4)
            return false;

        IWICImagingFactory* pFactory = GetWICFactory();
        if (!pFactory)
            return false;

        // Scan pixels, building palette. The D3DFMT_X8R8G8B8 memory layout is little-endian
        // BGRX, so reading as uint32_t gives 0xXXRRGGBB. Mask off the top byte to get a 24-bit key.
        std::unordered_map<uint32_t, uint8_t> colorToIndex;
        colorToIndex.reserve(64);
        std::vector<WICColor> palette;
        palette.reserve(64);

        for (int y = 0; y < height; y++)
        {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(pixels + y * stride);
            for (int x = 0; x < width; x++)
            {
                uint32_t key = row[x] & 0x00FFFFFFu;
                if (colorToIndex.find(key) == colorToIndex.end())
                {
                    if (palette.size() >= 256)
                    {
                        // Too many colors for 8bpp indexed — degrade to 32bpp PNG at the same path.
                        OutputDebugStringA("[ModalityImageWriter] >256 unique colors, falling back to RGB PNG\n");
                        return SavePNG(filePath, pixels, width, height, stride);
                    }
                    colorToIndex[key] = static_cast<uint8_t>(palette.size());
                    palette.push_back(0xFF000000u | key);  // WICColor is 0xAARRGGBB
                }
            }
        }

        // Pack pixel indices into a contiguous 8bpp buffer.
        std::vector<uint8_t> indexed(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; y++)
        {
            const uint32_t* src = reinterpret_cast<const uint32_t*>(pixels + y * stride);
            uint8_t*        dst = &indexed[static_cast<size_t>(y) * width];
            for (int x = 0; x < width; x++)
                dst[x] = colorToIndex[src[x] & 0x00FFFFFFu];
        }

        // Create WIC palette.
        IWICPalette* pWICPalette = nullptr;
        if (FAILED(pFactory->CreatePalette(&pWICPalette)))
            return false;
        HRESULT hr = pWICPalette->InitializeCustom(palette.data(), static_cast<UINT>(palette.size()));
        if (FAILED(hr))
        {
            pWICPalette->Release();
            return false;
        }

        bool ok = EncodeSingleFrame(pFactory, GUID_ContainerFormatPng, filePath,
                                    [&](IWICImagingFactory*, IWICBitmapEncoder* pEncoder, IWICBitmapFrameEncode* pFrame, IPropertyBag2* pProps) -> bool
                                    {
                                        if (pProps)
                                            pFrame->Initialize(pProps);
                                        else
                                            pFrame->Initialize(nullptr);
                                        pFrame->SetSize(width, height);

                                        WICPixelFormatGUID fmt = GUID_WICPixelFormat8bppIndexed;
                                        if (FAILED(pFrame->SetPixelFormat(&fmt)))
                                            return false;

                                        if (FAILED(pFrame->SetPalette(pWICPalette)))
                                            return false;

                                        UINT indexedStride = width;
                                        return SUCCEEDED(pFrame->WritePixels(height, indexedStride, indexedStride * height, const_cast<BYTE*>(indexed.data())));
                                    });

        pWICPalette->Release();
        return ok;
    }

}  // namespace ModalityImageWriter
