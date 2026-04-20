/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/CModalityImageWriter.h
 *
 *  WIC-based PNG / indexed-PNG / JPEG writer for multi-modal capture.
 *  Inputs are raw 32bpp BGRX pixels (D3DFMT_X8R8G8B8 memory layout).
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <string>

namespace ModalityImageWriter
{
    bool SavePNG(const std::string& filePath, const uint8_t* pixels, int width, int height, int stride);

    bool SaveJPEG(const std::string& filePath, const uint8_t* pixels, int width, int height, int stride, int quality);

    // Scans pixels to build a palette of unique colors. If >256 unique colors are found,
    // silently falls back to a 32bpp PNG at the same path (still returns true on success).
    bool SaveIndexedPNG(const std::string& filePath, const uint8_t* pixels, int width, int height, int stride);
}
