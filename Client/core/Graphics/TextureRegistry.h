/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/TextureRegistry.h
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#pragma once

#include <map>
#include <string>
#include <d3d9.h>

///////////////////////////////////////////////////////////////////////////////
//
// TextureRegistry
//
// Automatically assigns unique colors to textures for segmentation mapping.
// Colors are generated deterministically using HSV color space for visual
// distinction. Exports texture-to-color mapping as JSON for ML training.
//
///////////////////////////////////////////////////////////////////////////////
class CTextureRegistry
{
public:
    CTextureRegistry();
    ~CTextureRegistry();

    // Register texture and get assigned unique color
    D3DCOLOR GetOrAssignColor(const std::string& textureName);

    // Export mapping to JSON file
    // Format: {"textureName": [R, G, B], ...}
    bool ExportMapping(const std::string& jsonPath);

    // Get current mapping (for in-memory access)
    const std::map<std::string, D3DCOLOR>& GetMapping() const { return m_TextureColors; }

    // Clear all registered textures
    void Clear();

    // Get number of registered textures
    size_t GetTextureCount() const { return m_TextureColors.size(); }

private:
    // Generate unique color from index using golden ratio HSV distribution
    D3DCOLOR GenerateUniqueColor(int index);

    // Convert HSV to RGB
    static void HSVtoRGB(float h, float s, float v, BYTE& r, BYTE& g, BYTE& b);

    // Texture name -> assigned color
    std::map<std::string, D3DCOLOR> m_TextureColors;

    // Next color index to assign
    int m_iNextColorIndex;
};
