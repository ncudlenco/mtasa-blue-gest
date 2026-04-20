/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaMultiModalDefs.cpp
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaMultiModalDefs.h"
#include "CStaticFunctionDefinitions.h"
#include "CClientCommon.h"
#include <core/IMultiModalCapture.h>

void CLuaMultiModalDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"captureMultiModalFrame",    CaptureMultiModalFrame},
        {"startVideoRecording",       StartVideoRecording},
        {"stopVideoRecording",        StopVideoRecording},
        {"writeMultiModalMapping",    WriteMultiModalMapping},
        {"setMultiModalSegmentation", SetMultiModalSegmentation},
        {"setCleanCaptureMode",       SetCleanCaptureMode},
        {"enableCaptureLogs",         EnableCaptureLogs},
        {"waitMultiModalPending",     WaitMultiModalPending},
    };

    for (const auto& [name, func] : functions)
        CLuaCFunctions::AddFunction(name, func);
}

namespace
{
    IMultiModalCapture* GetMultiModalCapturePtr()
    {
        if (!g_pCore || !g_pCore->GetGraphics())
            return nullptr;
        return g_pCore->GetGraphics()->GetMultiModalCapture();
    }

    // Reads an optional string field from the stack slot, returns "" if nil/missing.
    std::string OptString(lua_State* luaVM, int index)
    {
        if (lua_isnoneornil(luaVM, index)) return std::string();
        if (!lua_isstring(luaVM, index))   return std::string();
        return std::string(lua_tostring(luaVM, index));
    }
}

//
// captureMultiModalFrame(rgbPath, segPath, depthPath,
//                        saveRgbToVideo, saveSegToVideo, saveDepthToVideo,
//                        jpegQuality)
//   -> boolean (true on success)
//
// Any of the paths may be "" to skip the image save for that modality.
// Blocks the calling thread until all enabled modality artifacts are on disk.
//
int CLuaMultiModalDefs::CaptureMultiModalFrame(lua_State* luaVM)
{
    IMultiModalCapture* pCapture = GetMultiModalCapturePtr();
    if (!pCapture)
    {
        m_pScriptDebugging->LogCustom(luaVM, "Multi-modal capture not available");
        lua_pushboolean(luaVM, false);
        return 1;
    }

    const std::string rgbPath         = OptString(luaVM, 1);
    const std::string segPath         = OptString(luaVM, 2);
    const std::string depthPath       = OptString(luaVM, 3);
    const bool        saveRgbToVideo  = lua_toboolean(luaVM, 4) != 0;
    const bool        saveSegToVideo  = lua_toboolean(luaVM, 5) != 0;
    const bool        saveDepthToVid  = lua_toboolean(luaVM, 6) != 0;
    const int         jpegQuality     = lua_isnumber(luaVM, 7) ? (int)lua_tointeger(luaVM, 7) : 95;

    bool ok = pCapture->CaptureMultiModalFrame(rgbPath, segPath, depthPath,
                                                saveRgbToVideo, saveSegToVideo, saveDepthToVid,
                                                jpegQuality);
    lua_pushboolean(luaVM, ok);
    return 1;
}

//
// startVideoRecording(modalityId, videoPath, width, height, fps, bitrate) -> boolean
//
int CLuaMultiModalDefs::StartVideoRecording(lua_State* luaVM)
{
    IMultiModalCapture* pCapture = GetMultiModalCapturePtr();
    if (!pCapture)
    {
        m_pScriptDebugging->LogCustom(luaVM, "Multi-modal capture not available");
        lua_pushboolean(luaVM, false);
        return 1;
    }

    if (!lua_isnumber(luaVM, 1) || !lua_isstring(luaVM, 2))
    {
        m_pScriptDebugging->LogCustom(luaVM, "startVideoRecording expects (modalityId, videoPath, width, height, fps, bitrate)");
        lua_pushboolean(luaVM, false);
        return 1;
    }

    const int         modalityId = (int)lua_tointeger(luaVM, 1);
    const std::string path       = lua_tostring(luaVM, 2);
    const int         width      = lua_isnumber(luaVM, 3) ? (int)lua_tointeger(luaVM, 3) : 1920;
    const int         height     = lua_isnumber(luaVM, 4) ? (int)lua_tointeger(luaVM, 4) : 1080;
    const int         fps        = lua_isnumber(luaVM, 5) ? (int)lua_tointeger(luaVM, 5) : 30;
    const int         bitrate    = lua_isnumber(luaVM, 6) ? (int)lua_tointeger(luaVM, 6) : 5000000;

    lua_pushboolean(luaVM, pCapture->StartVideoRecording(modalityId, path, width, height, fps, bitrate));
    return 1;
}

//
// stopVideoRecording(modalityId) -> boolean
//
int CLuaMultiModalDefs::StopVideoRecording(lua_State* luaVM)
{
    IMultiModalCapture* pCapture = GetMultiModalCapturePtr();
    if (!pCapture)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    if (!lua_isnumber(luaVM, 1))
    {
        m_pScriptDebugging->LogCustom(luaVM, "stopVideoRecording expects (modalityId)");
        lua_pushboolean(luaVM, false);
        return 1;
    }

    lua_pushboolean(luaVM, pCapture->StopVideoRecording((int)lua_tointeger(luaVM, 1)));
    return 1;
}

//
// writeMultiModalMapping(path) -> boolean
//
int CLuaMultiModalDefs::WriteMultiModalMapping(lua_State* luaVM)
{
    IMultiModalCapture* pCapture = GetMultiModalCapturePtr();
    if (!pCapture)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    if (!lua_isstring(luaVM, 1))
    {
        m_pScriptDebugging->LogCustom(luaVM, "writeMultiModalMapping expects (path)");
        lua_pushboolean(luaVM, false);
        return 1;
    }

    lua_pushboolean(luaVM, pCapture->WriteMappingJson(lua_tostring(luaVM, 1)));
    return 1;
}

//
// setMultiModalSegmentation(enabled) -> boolean (previous state)
//
// Arms / disarms the segmentation double-draw. When enabled, every game
// DrawPrimitive / DrawIndexedPrimitive is followed by a second draw to the
// seg RT using a constant-color PS. Takes effect from the next rendered frame.
//
int CLuaMultiModalDefs::SetMultiModalSegmentation(lua_State* luaVM)
{
    IMultiModalCapture* pCapture = GetMultiModalCapturePtr();
    if (!pCapture)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    const bool wasEnabled = pCapture->IsSegmentationEnabled();
    const bool enable     = lua_toboolean(luaVM, 1) != 0;
    pCapture->SetSegmentationEnabled(enable);

    lua_pushboolean(luaVM, wasEnabled);
    return 1;
}

//
// setCleanCaptureMode(enabled) -> boolean (previous state)
//
// When enabled, the next Present (and every subsequent one until disabled)
// skips MTA overlay stages and the borderless tone-map: no dxDraw queues,
// no HUD/GUI, no cursor, no tone mapping. External window-capture tools
// then see only the scene as GTA rendered it (with whatever per-texture
// shaders scripts have applied via engineApplyShaderToWorldTexture).
// Intended to bracket one or two frames per artifact capture — leaving it
// enabled hides the entire UI.
//
int CLuaMultiModalDefs::SetCleanCaptureMode(lua_State* luaVM)
{
    if (!g_pCore || !g_pCore->GetGraphics())
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    CGraphicsInterface* pGraphics = g_pCore->GetGraphics();
    const bool          wasEnabled = pGraphics->IsCleanCaptureMode();
    const bool          enable     = lua_toboolean(luaVM, 1) != 0;
    pGraphics->SetCleanCaptureMode(enable);

    lua_pushboolean(luaVM, wasEnabled);
    return 1;
}

//
// enableCaptureLogs(boolean)
//   -> boolean (true on success, false if the capture instance isn't ready)
//
// Toggles all [SegDiag] / [SegDraw] logging — per-frame counters, RT-size
// histogram, and the first-N-draws-per-frame detailed trace — to
// OutputDebugString plus the seg_diag.log file in the process's CWD.
// Default off; call once at startup to turn it on for a capture session.
//
int CLuaMultiModalDefs::EnableCaptureLogs(lua_State* luaVM)
{
    IMultiModalCapture* pCapture = GetMultiModalCapturePtr();
    if (!pCapture)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }
    const bool enable = lua_toboolean(luaVM, 1) != 0;
    pCapture->SetDiagLogsEnabled(enable);
    lua_pushboolean(luaVM, true);
    return 1;
}

//
// waitMultiModalPending()
//   -> boolean (true on success)
//
// Blocks until all previously-submitted fire-and-forget captureMultiModalFrame
// save tasks have finished writing to disk. Call at session end (stopCollection)
// so MP4 / PNG files are flushed before sv2l tears down the output tree.
//
int CLuaMultiModalDefs::WaitMultiModalPending(lua_State* luaVM)
{
    IMultiModalCapture* pCapture = GetMultiModalCapturePtr();
    if (!pCapture)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }
    pCapture->WaitPendingCaptures();
    lua_pushboolean(luaVM, true);
    return 1;
}
