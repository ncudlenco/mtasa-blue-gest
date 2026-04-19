/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/core/Graphics/TextureRegistry.cpp
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "TextureRegistry.h"
#include <fstream>
#include <sstream>
#include <cmath>

CTextureRegistry::CTextureRegistry() : m_iNextColorIndex(0)
{
}

CTextureRegistry::~CTextureRegistry()
{
    Clear();
}

D3DCOLOR CTextureRegistry::GetOrAssignColor(const std::string& textureName)
{
    // Check if texture already has assigned color
    auto it = m_TextureColors.find(textureName);
    if (it != m_TextureColors.end())
    {
        return it->second;
    }

    // Generate new unique color
    D3DCOLOR color = GenerateUniqueColor(m_iNextColorIndex);
    m_TextureColors[textureName] = color;
    m_iNextColorIndex++;

    return color;
}

D3DCOLOR CTextureRegistry::GenerateUniqueColor(int index)
{
    // Use golden ratio (phi) for well-distributed hues in HSV space
    // This ensures maximum visual distinction between colors
    const float goldenRatio = 0.618033988749895f;

    float hue = fmod(index * goldenRatio, 1.0f);
    float saturation = 0.8f;  // High saturation for vivid colors
    float value = 0.9f;       // High value for brightness

    BYTE r, g, b;
    HSVtoRGB(hue, saturation, value, r, g, b);

    return D3DCOLOR_XRGB(r, g, b);
}

void CTextureRegistry::HSVtoRGB(float h, float s, float v, BYTE& r, BYTE& g, BYTE& b)
{
    float c = v * s;  // Chroma
    float h6 = h * 6.0f;
    float x = c * (1.0f - fabs(fmod(h6, 2.0f) - 1.0f));
    float m = v - c;

    float r1, g1, b1;

    if (h6 < 1.0f)
    {
        r1 = c; g1 = x; b1 = 0;
    }
    else if (h6 < 2.0f)
    {
        r1 = x; g1 = c; b1 = 0;
    }
    else if (h6 < 3.0f)
    {
        r1 = 0; g1 = c; b1 = x;
    }
    else if (h6 < 4.0f)
    {
        r1 = 0; g1 = x; b1 = c;
    }
    else if (h6 < 5.0f)
    {
        r1 = x; g1 = 0; b1 = c;
    }
    else
    {
        r1 = c; g1 = 0; b1 = x;
    }

    r = static_cast<BYTE>((r1 + m) * 255.0f);
    g = static_cast<BYTE>((g1 + m) * 255.0f);
    b = static_cast<BYTE>((b1 + m) * 255.0f);
}

bool CTextureRegistry::ExportMapping(const std::string& jsonPath)
{
    std::ofstream file(jsonPath);
    if (!file.is_open())
    {
        return false;
    }

    // Write JSON manually (simple format, no external dependencies)
    file << "{\n";

    size_t count = 0;
    for (const auto& pair : m_TextureColors)
    {
        const std::string& texName = pair.first;
        D3DCOLOR color = pair.second;

        BYTE r = (color >> 16) & 0xFF;
        BYTE g = (color >> 8) & 0xFF;
        BYTE b = color & 0xFF;

        file << "  \"" << texName << "\": [" << (int)r << ", " << (int)g << ", " << (int)b << "]";

        if (++count < m_TextureColors.size())
        {
            file << ",";
        }
        file << "\n";
    }

    file << "}\n";
    file.close();

    return true;
}

void CTextureRegistry::Clear()
{
    m_TextureColors.clear();
    m_iNextColorIndex = 0;
}
