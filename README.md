## mtasa-blue-gest

<p align="center">
  <a href="https://github.com/ncudlenco/mtasa-blue-gest/releases"><img src="https://img.shields.io/github/v/release/ncudlenco/mtasa-blue-gest?include_prereleases&style=for-the-badge" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge" alt="GPLv3"></a>
  <a href="https://github.com/ncudlenco/mtasa-blue-gest/stargazers"><img src="https://img.shields.io/github/stars/ncudlenco/mtasa-blue-gest?style=for-the-badge&logo=github" alt="GitHub stars"></a>
  <a href="https://github.com/ncudlenco/mtasa-blue-gest/network/members"><img src="https://img.shields.io/github/forks/ncudlenco/mtasa-blue-gest?style=for-the-badge&logo=github" alt="GitHub forks"></a>
  <a href="https://github.com/ncudlenco/mtasa-blue-gest/watchers"><img src="https://img.shields.io/github/watchers/ncudlenco/mtasa-blue-gest?style=for-the-badge&logo=github" alt="GitHub watchers"></a>
</p>

> **mtasa-blue-gest** is a fork of [Multi Theft Auto: San Andreas](https://github.com/multitheftauto/mtasa-blue) with native client-side hooks for deterministic multi-modal frame capture — RGB video and still images, instance segmentation, and per-frame linear depth — extracted directly from the GTA San Andreas D3D9 rendering pipeline, with no screen-recorder or external capture tooling in the loop. It is the client-side companion to [**GEST-Engine** (`ncudlenco/mta-sim`)](https://github.com/ncudlenco/mta-sim), which drives these hooks over the MTA Lua scripting interface to produce multi-actor videos paired with dense frame-level ground-truth annotations.

This repository is a soft fork of `multitheftauto/mtasa-blue`: upstream MTA features and fixes are tracked on `master`, and the additions below are layered on top as dedicated C++ modules + Lua bindings. Nothing in the vanilla MTA user experience changes unless the bindings are explicitly called from a Lua resource.

## Publications

The GEST system driven by this client is described in:

> N. Cudlenco, M. Masala, M. Leordeanu. **[Tiny Paper] GEST-Engine: Controllable Multi-Actor Video Synthesis with Perfect Spatiotemporal Annotations.** *ICLR 2026, the 2nd Workshop on World Models: Understanding, Modelling and Scaling.* [OpenReview](https://openreview.net/forum?id=uUofPYVMZH)

The underlying GEST formalism — *Graphs of Events in Space and Time* — was introduced in:

> M. Masala, N. Cudlenco, T. Rebedea, M. Leordeanu. **Explaining Vision and Language Through Graphs of Events in Space and Time.** *ICCV 2023 Workshops (CLVL)*, pp. 2826–2831. [openaccess.thecvf.com](https://openaccess.thecvf.com/content/ICCV2023W/CLVL/html/Masala_Explaining_Vision_and_Language_Through_Graphs_of_Events_in_Space_ICCVW_2023_paper.html) · [arXiv:2309.08612](https://arxiv.org/abs/2309.08612)

The sample corpus of **398 procedurally generated multi-actor stories** (with engine-rendered videos, dense annotations, and VEO 3.1 / WAN 2.2 neural baselines) is publicly available on HuggingFace: [**nnc-001/gtasa-01**](https://huggingface.co/datasets/nnc-001/gtasa-01).

## What this fork adds

All extensions are layered on top of upstream; none of them alter vanilla MTA behaviour unless exercised by a Lua script through the new bindings.

- **`CMultiModalCapture`** ([`Client/core/Graphics/CMultiModalCapture.{h,cpp}`](Client/core/Graphics/CMultiModalCapture.h)) — single C++ class that owns private render targets for RGB, segmentation and depth and exposes one atomic per-frame capture entry point. Fire-and-forget: the D3D9 readback (~2–4 ms on the render thread) is the only synchronous cost; encoding and file I/O run on a worker pool. A drain barrier (`waitMultiModalPending`) is provided for end-of-session teardown so MP4 trailers are finalized against a stable frame set.
- **H.264 video encoder** ([`CVideoEncoder`](Client/core/Graphics/CVideoEncoder.h)) — Media Foundation based, one encoder per modality, with cached staging and `IMFMediaBuffer` instances across `AddFrame` calls. Input frames are gated by the configured FPS so sample timestamps stay monotonic regardless of how fast the simulation runs.
- **WIC image writer** ([`CModalityImageWriter`](Client/core/Graphics/CModalityImageWriter.h)) — PNG / indexed PNG / JPEG saves, with explicit BGRX→BGR repacking to avoid a driver-level WIC format-conversion bug that produced byte-shifted output on some machines.
- **Depth modality via INTZ** — a sampleable depth-stencil (NVIDIA's `INTZ` FourCC) is installed on the D3D9 device so a lightweight shader pass can linearize the game's own depth buffer into a grayscale PNG. Capability-gated; non-fatal on adapters without INTZ support.
- **Segmentation double-draw** — a second draw per GTA primitive, replayed onto a private seg RT with a per-texture hashed constant-colour pixel shader. Texture identity is resolved through MTA's wrapped `CD3DDUMMY*` tracking map so the mapping JSON uses real SA RenderWare texture names, not raw pointers. Instrumented end-to-end behind a Lua-toggled flag (`enableCaptureLogs`) that writes `[Seg/*]` / `[SegDraw]` lines to `seg_diag.log` in the client CWD.
- **Clean-capture mode** ([`SetCleanCaptureMode`](Client/sdk/core/CGraphicsInterface.h)) — mutes MTA's overlay / HUD / cursor / tonemap compositor so external window-capture tools see only the scene GTA rendered, with whatever per-texture shaders a Lua resource has applied via `engineApplyShaderToWorldTexture`.
- **Lua bindings** ([`CLuaMultiModalDefs`](Client/mods/deathmatch/logic/luadefs/CLuaMultiModalDefs.h)):

  | Binding | Purpose |
  |---|---|
  | `captureMultiModalFrame(rgbPath, segPath, depthPath, saveRgb, saveSeg, saveDepth, quality)` | One-call atomic capture. Paths may be empty to skip an individual modality. Returns after readback; save is async. |
  | `startVideoRecording(modalityId, path, w, h, fps, bitrate)` / `stopVideoRecording(modalityId)` | Per-modality persistent H.264 recorder. Modality IDs: 0=RGB, 1=Seg, 2=Depth. |
  | `writeMultiModalMapping(path)` | Writes `{textureName → {color: [r,g,b], modelIds: []}}` JSON matching the sv2l `SegmentationCollector` schema. |
  | `setMultiModalSegmentation(enabled)` | Arms / disarms the seg double-draw. Takes effect next frame. |
  | `setCleanCaptureMode(enabled)` | Toggles the overlay mute described above. |
  | `enableCaptureLogs(enabled)` | Toggles all `[Seg*]` diagnostic output to `seg_diag.log`. |
  | `waitMultiModalPending()` | Blocks until every queued save completes. Use at session end before `stopVideoRecording`. |

- **INTZ capability advertisement** and **OnPresent hook wiring** in the D3D9 proxy ([`CProxyDirect3D9`](Client/core/DXHook/CProxyDirect3D9.cpp), [`CDirect3DEvents9`](Client/core/DXHook/CDirect3DEvents9.cpp)).
- **Dev-loop helpers** — [`BUILD_CHECKLIST.md`](BUILD_CHECKLIST.md) is a reminder list for an end-to-end rebuild, and [`deploy-to-MTA.ps1`](deploy-to-MTA.ps1) is a small PowerShell helper some contributors use for iterating on individual DLLs without reinstalling the full client.

## Requirements

Same as upstream MTA for runtime, plus the toolchain required to rebuild the client:

- **Windows 10 / 11.** The capture modules use Windows-native APIs (D3D9, Media Foundation, WIC).
- **Grand Theft Auto: San Andreas PC v1.0** — MTA will not run against later patched releases, the Steam re-release, Mobile, or the Definitive Edition.
- **Multi Theft Auto: San Andreas 1.6** — see [multitheftauto.com](https://multitheftauto.com/). Only the **client** is patched by this fork; the server binary is unchanged.
- For building: **Visual Studio 2026** with the **v145 MSVC build tools** (`MSVC v145 — VS 2026 C++ x64/x86 build tools`), the **Microsoft DirectX SDK**, and optionally Git for Windows. Upstream's toolset pin is `v145`; VS 2022's `v143` will not work without a local toolset override.

## Build

The fork inherits upstream's build system unchanged.

```powershell
./win-create-projects.bat
# then open Build/MTASA.sln and build Release|Win32, OR:
./win-build.bat
./win-install-data.bat   # refreshes netc.dll etc. to the version this fork expects
```

See upstream's [*Compiling MTASA* wiki page](https://wiki.multitheftauto.com/wiki/Compiling_MTASA) for detailed instructions and troubleshooting, and [`BUILD_CHECKLIST.md`](BUILD_CHECKLIST.md) for a fork-specific rebuild reminder list.

## Install

1. Install MTA:SA 1.6 the normal way from [multitheftauto.com](https://multitheftauto.com/).
2. Grab the latest **`InstallFiles-win32`** build artifact from this fork's [GitHub Actions](https://github.com/ncudlenco/mtasa-blue-gest/actions) (pick the most recent green `master` run of the Build workflow, scroll to **Artifacts** at the bottom of the run page), or from a [release](https://github.com/ncudlenco/mtasa-blue-gest/releases) if one is tagged.
3. Unzip the archive over your MTA install directory. The archive is a staging tree that mirrors the MTA install layout, so extracting it overlays only the files that differ from stock MTA.

The server binary is unchanged; only the client side is patched. To roll back, reinstall stock MTA:SA 1.6 from multitheftauto.com.

For iterating on individual DLLs during development without re-unzipping the whole archive, some contributors use [`deploy-to-MTA.ps1`](deploy-to-MTA.ps1) as a convenience wrapper around the copy/backup/revert cycle. Regular end users don't need it.

## Using the capture bindings from a Lua resource

The bindings are exposed on the client. The companion [GEST-Engine](https://github.com/ncudlenco/mta-sim) resource drives them through its server-side [`MTAClientMultiModalAdapter`](https://github.com/ncudlenco/mta-sim/blob/master/src/features/artifact_collection/adapters/mta/server/MTAClientMultiModalAdapter.lua) and client-side [`ClientMultiModalHandler`](https://github.com/ncudlenco/mta-sim/blob/master/src/features/artifact_collection/adapters/mta/client/ClientMultiModalHandler.lua). Minimal standalone example:

```lua
-- client.lua (MTA client resource)
if not captureMultiModalFrame then
    outputDebugString("native multi-modal capture not available — wrong client build")
    return
end

setMultiModalSegmentation(true)     -- arm the seg double-draw
startVideoRecording(0, "out/raw.mp4", 1920, 1080, 30, 15000000)

addEventHandler("onClientRender", root, function()
    local n = frameId()              -- your own counter
    captureMultiModalFrame(
        string.format("out/frame_%04d_screenshot.jpg", n),
        string.format("out/frame_%04d_segmentation.png", n),
        string.format("out/frame_%04d_depth.png", n),
        true,   -- feed RGB into the H.264 encoder too
        false,  -- no seg video
        false,  -- no depth video
        95)     -- JPEG quality
end)

-- on teardown
waitMultiModalPending()              -- drain queued saves
stopVideoRecording(0)                -- finalize MP4 trailer
setMultiModalSegmentation(false)
writeMultiModalMapping("out/segmentation_mapping.json")
```

## Repository layout

```
Client/
├── core/
│   ├── DXHook/
│   │   ├── CDirect3DEvents9.{cpp,h}      # OnPresent + seg double-draw hook sites
│   │   └── CProxyDirect3D9.cpp           # INTZ capability advertisement
│   ├── Graphics/
│   │   ├── CMultiModalCapture.{cpp,h}    # Main capture orchestrator
│   │   ├── CModalityImageWriter.{cpp,h}  # WIC PNG / JPEG writer
│   │   ├── CVideoEncoder.{cpp,h}         # Media Foundation H.264
│   │   ├── CSaveWorkerPool.{cpp,h}       # packaged_task pool
│   │   ├── CD3D9To11Converter.{cpp,h}    # Shared-handle interop to MF
│   │   ├── TextureRegistry.{cpp,h}       # Per-texture color assignment
│   │   └── CGraphics.{cpp,h}             # Ownership + device invalidate/restore
│   └── ...
├── mods/deathmatch/logic/luadefs/
│   └── CLuaMultiModalDefs.{cpp,h}        # Lua bindings
├── sdk/core/
│   ├── IMultiModalCapture.h              # SDK interface
│   └── CGraphicsInterface.h              # SetCleanCaptureMode
└── ...                                   # Upstream mtasa-blue tree
BUILD_CHECKLIST.md
deploy-to-MTA.ps1
```

## Relationship to upstream MTA

This fork is a minimal, additive overlay: the C++ classes above are new files, and the hook sites in `CGraphics` / `CDirect3DEvents9` / `CProxyDirect3D9` are small, localized insertions next to existing extension points. Upstream merges land on `master` regularly. If you're looking for MTA itself — multiplayer, scripting, community servers, anti-cheat, Linux server builds — go to the canonical [multitheftauto/mtasa-blue](https://github.com/multitheftauto/mtasa-blue). This fork exists solely to make deterministic multi-modal capture available to GEST-Engine.

## Star History

<a href="https://www.star-history.com/#ncudlenco/mtasa-blue-gest&Date">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=ncudlenco/mtasa-blue-gest&type=Date&theme=dark" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=ncudlenco/mtasa-blue-gest&type=Date" />
   <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=ncudlenco/mtasa-blue-gest&type=Date" />
 </picture>
</a>

## Citation

If you use this system in your research, please cite the ICLR 2026 Tiny Paper:

```bibtex
@inproceedings{cudlenco2026tiny,
  title={[Tiny Paper] {GEST}-Engine: Controllable Multi-Actor Video Synthesis with Perfect Spatiotemporal Annotations},
  author={Nicolae Cudlenco and Mihai Masala and Marius Leordeanu},
  booktitle={ICLR 2026 the 2nd Workshop on World Models: Understanding, Modelling and Scaling},
  year={2026},
  url={https://openreview.net/forum?id=uUofPYVMZH}
}
```

## License and intellectual property notice

Unless otherwise specified, all source code in this repository is licensed under the GPLv3, matching upstream `multitheftauto/mtasa-blue`. See [`LICENSE`](LICENSE).

**Use of this system requires a licensed copy of Grand Theft Auto: San Andreas.** Rockstar Games / Take-Two Interactive own all in-game assets (3D models, textures, animations, environments) and this repository makes no claim to them. Nothing here distributes Rockstar / Take-Two intellectual property — users supply their own legitimate copy of the game. Users are responsible for complying with both Rockstar's EULA and the Multi Theft Auto terms of use. Research data derived from this system (e.g. the [GTASA-01 corpus on HuggingFace](https://huggingface.co/datasets/nnc-001/gtasa-01)) is released for non-commercial academic research only.

Grand Theft Auto and all related trademarks are © Rockstar North 1997–2026.

## Contact

For questions, bug reports, or collaboration inquiries: open an [issue](https://github.com/ncudlenco/mtasa-blue-gest/issues) or email `nicolae.cudlenco@gmail.com`.
