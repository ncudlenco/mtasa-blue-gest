# Multi-Modal Capture - Build Checklist

## Pre-Build Verification ✅

### Files Created (8 implementation files)
- [x] `Client/core/Graphics/CMultiModalCapture.h` (6.6 KB)
- [x] `Client/core/Graphics/CMultiModalCapture.cpp` (25.4 KB)
- [x] `Client/core/Graphics/TextureRegistry.h` (1.9 KB)
- [x] `Client/core/Graphics/TextureRegistry.cpp` (3.3 KB)
- [x] `Client/core/Graphics/CD3D9To11Converter.h` (2.2 KB)
- [x] `Client/core/Graphics/CD3D9To11Converter.cpp` (5.7 KB)
- [x] `Client/mods/deathmatch/logic/luadefs/CLuaMultiModalDefs.h` (748 bytes)
- [x] `Client/mods/deathmatch/logic/luadefs/CLuaMultiModalDefs.cpp` (7.1 KB)

### Integration Points
- [x] `Client/core/premake5.lua` - Added `d3d11` library (line 48)
- [x] `Client/core/Graphics/CGraphics.h` - Added member and getter (lines 172, 248)
- [x] `Client/core/Graphics/CGraphics.cpp` - Added initialization/cleanup
- [x] `Client/core/DXHook/CDirect3DEvents9.cpp` - Added OnPresent hook (line 26, 671)
- [x] `Client/mods/deathmatch/logic/lua/CLuaManager.cpp` - Registered Lua functions (line 16, 287)

### Documentation
- [x] `MULTIMODAL_CAPTURE_COMPLETE.md` - Complete implementation guide
- [x] `MULTIMODAL_API_REFERENCE.md` - API reference and examples
- [x] `IMPLEMENTATION_SUMMARY.md` - Implementation status
- [x] `BUILD_CHECKLIST.md` - This file

## Build Steps

### 1. Regenerate Project Files

```bash
cd "Z:\More games\GTA San Andreas\mtasa-blue"
premake5 vs2022
```

**Expected Output:**
- Updated `.vcxproj` files with new source files
- Updated library dependencies (d3d11.lib)

### 2. Open Visual Studio Solution

```bash
# Open the generated solution
start Build/MTASA.sln
```

**Or manually:**
- Open Visual Studio 2022
- File → Open → Project/Solution
- Navigate to `Build/MTASA.sln`

### 3. Build Configuration

**Recommended build settings:**
- Configuration: `Debug` (for initial testing)
- Platform: `x86` or `x64` (depending on your setup)
- Target: `Client\core` project

### 4. Compile

**Build the core project:**
1. In Solution Explorer, right-click `Client\core`
2. Select "Build"

**Expected compilation:**
- CMultiModalCapture.cpp → CMultiModalCapture.obj
- TextureRegistry.cpp → TextureRegistry.obj
- CD3D9To11Converter.cpp → CD3D9To11Converter.obj
- CLuaMultiModalDefs.cpp → CLuaMultiModalDefs.obj
- All existing files should compile without errors

### 5. Link

**Expected output:**
- Core.dll (or similar, depending on MTA build config)
- All new objects linked successfully

## Common Build Issues

### Issue 1: "Cannot open include file: 'd3d11.h'"

**Solution:**
- Install DirectX SDK (June 2010)
- Or use Windows 10+ SDK which includes DirectX headers
- Add SDK include path to project settings

### Issue 2: "Unresolved external symbol D3D11CreateDevice"

**Solution:**
- Verify `d3d11.lib` is in library list (check premake5.lua line 48)
- Regenerate project files: `premake5 vs2022`
- Clean and rebuild

### Issue 3: "Unresolved external symbol D3DXSaveSurfaceToFileW"

**Solution:**
- Verify `d3dx9.lib` is linked (should already be in existing MTA build)
- Install DirectX SDK if missing

### Issue 4: "'StdInc.h': No such file or directory"

**Solution:**
- Ensure you're building from the correct project context
- StdInc.h should be in the same directory or include path
- Check existing MTA files use the same include pattern

### Issue 5: Lua-related errors in CLuaMultiModalDefs.cpp

**Solution:**
- Verify Lua headers are in include path
- Check other CLua*.cpp files for include patterns
- Ensure Lua library is linked (should already be configured)

### Issue 6: "CLuaCFunctions::AddFunction not found"

**Solution:**
- Verify CLuaCFunctions is declared and available
- Check other luadefs files for usage patterns
- Ensure proper include order

## Post-Build Verification

### 1. Check Build Output

```bash
# Verify DLL/EXE was created
ls -la Build/Debug/Core.dll  # or similar output file
```

### 2. Runtime Test (Basic)

**Launch MTA and test Lua API:**

```lua
-- In MTA Lua console
print("Testing multi-modal API...")

-- Test 1: Functions exist
print("beginMultiModalCapture:", type(beginMultiModalCapture))
print("endMultiModalCapture:", type(endMultiModalCapture))
print("setMultiModalCallback:", type(setMultiModalCallback))
print("isMultiModalCapturing:", type(isMultiModalCapturing))

-- Expected output:
-- beginMultiModalCapture: function
-- endMultiModalCapture: function
-- setMultiModalCallback: function
-- isMultiModalCapturing: function
```

### 3. Functional Test

**Test capture session:**

```lua
-- Set callback
setMultiModalCallback(function(frame, mapping)
    print("Captured frame " .. frame)
    print("Mapping: " .. mapping)
end)

-- Start capture
local success = beginMultiModalCapture({
    outputPath = "test_capture/",
    rgbEnabled = true,
    rgbImageFormat = "png",
    rgbImageFPS = 1  -- 1 FPS for testing
})

print("Capture started:", success)
print("Is capturing:", isMultiModalCapturing())

-- Wait a few seconds...
-- You should see "Captured frame 0", "Captured frame 1", etc.

-- Stop capture
endMultiModalCapture()
print("Is capturing:", isMultiModalCapturing())  -- Should be false
```

### 4. File Output Test

**Check that files were created:**

```bash
# Navigate to MTA directory
cd "test_capture/"

# List created files
ls -la

# Expected files:
# frame_0000_rgb.png
# frame_0000_mapping.json
# frame_0001_rgb.png
# frame_0001_mapping.json
# ...
```

### 5. Performance Test

**Monitor frame rate impact:**

1. Check baseline FPS (no capture): _____
2. Start RGB capture at 30 FPS: _____
3. Enable all modalities at 10 FPS: _____
4. Expected: <5% FPS drop for RGB only, <15% for all modalities

## Success Criteria

### Build Success
- [ ] All .cpp files compile without errors
- [ ] All .obj files link successfully
- [ ] No unresolved external symbols
- [ ] No missing header errors
- [ ] Output DLL/EXE created

### Runtime Success
- [ ] MTA launches without crashes
- [ ] Lua functions are accessible
- [ ] `beginMultiModalCapture()` returns true
- [ ] Callback fires when frames are captured
- [ ] Files are created in output directory
- [ ] `endMultiModalCapture()` works correctly
- [ ] No memory leaks (use profiler if available)
- [ ] No rendering artifacts during capture
- [ ] Game remains playable during capture

### Quality Checks
- [ ] RGB images match backbuffer
- [ ] Segmentation images show colored regions
- [ ] Depth images show grayscale depth
- [ ] Mapping JSON contains texture entries
- [ ] No crashes during extended captures (>1000 frames)
- [ ] State properly restored after capture

## Debugging Tips

### Enable Debug Output

In CMultiModalCapture.cpp, debug messages use `OutputDebugString()`.

**View debug output:**
- Use Visual Studio debugger output window
- Or use DebugView (sysinternals)

**Add more debug output if needed:**
```cpp
OutputDebugString("[CMultiModalCapture] Your debug message here\n");
```

### Memory Leak Detection

**Enable Visual Studio memory leak detection:**
```cpp
// Add to CMultiModalCapture.cpp top
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

// Add to shutdown or destructor
_CrtDumpMemoryLeaks();
```

### Profiling

**Use Visual Studio Profiler:**
1. Debug → Performance Profiler
2. Select "CPU Usage"
3. Start capture session in MTA
4. Stop profiler after 30 seconds
5. Analyze hotspots

**Expected:**
- D3DXSaveSurfaceToFile should be the bottleneck
- Shader operations should be minimal
- Texture mapping should be negligible

## Rollback Plan

If build fails or runtime issues occur:

### 1. Revert Integration Changes

```bash
# Revert modified files
git checkout Client/core/premake5.lua
git checkout Client/core/Graphics/CGraphics.h
git checkout Client/core/Graphics/CGraphics.cpp
git checkout Client/core/DXHook/CDirect3DEvents9.cpp
git checkout Client/mods/deathmatch/logic/lua/CLuaManager.cpp
```

### 2. Remove New Files

```bash
# Remove implementation files
rm Client/core/Graphics/CMultiModalCapture.*
rm Client/core/Graphics/TextureRegistry.*
rm Client/core/Graphics/CD3D9To11Converter.*
rm Client/mods/deathmatch/logic/luadefs/CLuaMultiModalDefs.*
```

### 3. Rebuild

```bash
# Regenerate project without new files
premake5 vs2022

# Build
# Should return to working state
```

## Next Steps After Successful Build

1. **Performance Optimization**
   - Profile capture overhead
   - Optimize disk I/O (async processing)
   - Reduce memory allocations

2. **Enhanced Segmentation**
   - Implement second render pass
   - Hook SetTexture() for active texture tracking
   - Custom pixel shader for constant color output

3. **Video Encoding**
   - Integrate with AsyncFrameProcessor
   - H.264/H.265 encoding using D3D11 textures
   - Multi-modal video output

4. **Production Testing**
   - Extended capture sessions (>10,000 frames)
   - Different game scenarios
   - Various graphics settings
   - Stress testing (high FPS, all modalities)

5. **ML Integration**
   - Python data loader for captured frames
   - PyTorch/TensorFlow integration
   - Real-time training pipeline

## Support

If you encounter issues not covered here:

1. Check `OutputDebugString` messages
2. Review error logs
3. Compare with existing MTA code patterns
4. Verify SDK installations (DirectX, Windows SDK)
5. Check compiler/linker settings match existing projects

## Completion

When all checks pass:
- ✅ Build successful
- ✅ Runtime tests pass
- ✅ Files generated correctly
- ✅ Performance acceptable
- ✅ No crashes or leaks

**Status: Ready for production use** 🎉

---

**Build Date:** _________________

**Built By:** _________________

**Notes:** _________________
