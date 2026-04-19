/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/core/IMultiModalCapture.h
 *  PURPOSE:     Multi-modal capture interface (RGB / segmentation / depth).
 *
 *  Designed for training-data collection where the sim is paused during each
 *  capture; captureMultiModalFrame() is a synchronous "all-or-nothing" call
 *  that captures all three modalities off the current backbuffer/seg/depth
 *  render targets and blocks until every artifact is on disk.
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <string>

struct IDirect3DDevice9;

// Modality identifier used by the start/stopVideoRecording API. Integer
// values are part of the ABI that Lua scripts depend on — do not renumber.
enum class EModality : int
{
    RGB          = 0,
    SEGMENTATION = 1,
    DEPTH        = 2,
};

class IMultiModalCapture
{
public:
    virtual ~IMultiModalCapture() {}

    // Lifecycle — owned by CGraphics; called at device create / invalidate.
    virtual bool Initialize(IDirect3DDevice9* pDevice, int width, int height) = 0;
    virtual void Shutdown() = 0;

    // Synchronous atomic capture of all three modalities from the current
    // backbuffer / seg RT / depth RT. Any `*Path` may be empty to skip the
    // image save for that modality (video submission still happens if that
    // modality's encoder is running and the corresponding saveToVideo is true).
    //
    // Blocks until every requested artifact is on disk. Returns false if
    // initialization is bad or any individual save fails; best-effort for the
    // rest (partial outputs are still written).
    virtual bool CaptureMultiModalFrame(const std::string& rgbPath,
                                        const std::string& segPath,
                                        const std::string& depthPath,
                                        bool saveRgbToVideo,
                                        bool saveSegToVideo,
                                        bool saveDepthToVideo,
                                        int  jpegQuality) = 0;

    // Persistent per-modality H.264 encoder. captureMultiModalFrame() feeds
    // the encoder when its corresponding saveXxxToVideo flag is true and the
    // encoder is running.
    virtual bool StartVideoRecording(int                modalityId,
                                     const std::string& videoPath,
                                     int                width,
                                     int                height,
                                     int                fps,
                                     int                bitrate) = 0;

    virtual bool StopVideoRecording(int modalityId) = 0;

    // Writes the accumulated texture-name → color map as JSON. Format matches
    // the server-side SegmentationCollector schema:
    //   { "texName": { "color": [r,g,b], "modelIds": [] }, ... }
    // modelIds is always empty from the native path (game doesn't expose a
    // per-texture model reverse-map here).
    virtual bool WriteMappingJson(const std::string& path) const = 0;

    // Rendering pipeline hook — reserved. No-op today.
    virtual void OnPresent(IDirect3DDevice9* pDevice) = 0;

    // Segmentation double-draw: when enabled, every game DrawPrimitive /
    // DrawIndexedPrimitive is followed by a second draw onto the seg RT with
    // a constant-color pixel shader whose color comes from TextureRegistry
    // keyed by the current sampler-0 texture pointer. Takes effect from the
    // next rendered frame. Disabled by default — cost is ~1-3 ms/frame when on.
    virtual void SetSegmentationEnabled(bool enabled) = 0;
    virtual bool IsSegmentationEnabled() const        = 0;

    // Called by the proxy device's DrawPrimitiveGuarded hooks. Fast-paths to
    // no-op when segmentation is disabled. pDevice is the RAW device the
    // proxy forwards to, not the proxy itself.
    virtual void EmitSegmentationDraw(IDirect3DDevice9* pDevice,
                                      unsigned int primitiveType,
                                      unsigned int startVertex,
                                      unsigned int primitiveCount) = 0;
    virtual void EmitSegmentationDrawIndexed(IDirect3DDevice9* pDevice,
                                             unsigned int primitiveType,
                                             int          baseVertexIndex,
                                             unsigned int minVertexIndex,
                                             unsigned int numVertices,
                                             unsigned int startIndex,
                                             unsigned int primitiveCount) = 0;
};
