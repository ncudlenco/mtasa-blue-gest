/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaMultiModalDefs.h
 *
 *  Multi Theft Auto is available from https://multitheftauto.com/
 *
 *****************************************************************************/

#pragma once
#include "CLuaDefs.h"

class CLuaMultiModalDefs : public CLuaDefs
{
public:
    static void LoadFunctions();

    LUA_DECLARE(CaptureMultiModalFrame);
    LUA_DECLARE(StartVideoRecording);
    LUA_DECLARE(StopVideoRecording);
    LUA_DECLARE(WriteMultiModalMapping);
    LUA_DECLARE(SetMultiModalSegmentation);
    LUA_DECLARE(SetCleanCaptureMode);
    LUA_DECLARE(EnableCaptureLogs);
    LUA_DECLARE(WaitMultiModalPending);
};
