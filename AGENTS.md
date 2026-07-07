## Goal
Build audio-reactive 3D/2D visual modules (MeshInstances3D, LissajousOut, TriggerWaveEffect) with IVisualSource FBO output for MonitorModule compatibility.

## Constraints & Preferences
- Element grids (rotary/slider/button) are global/standalone, placed to the right of pattern slots
- Module is NOT user-resizable (content is fixed-size, resize adds dead space)
- Save format rev 6 uses global element bindings, not per-track; rev 7 adds page data
- MIDI bindings use mTrack=-1 + mSlot=-1 for standalone element cells, mSlot>=0 for pattern/scene
- 3D model support: OBJ only (no GLTF until nlohmann json is available)
- MeshInstances3D is a standalone module (kModuleCategory_Audio), not an EffectChain effect
- Pages: differ only in pattern data stored per slot (MIDI mapping, element grids, track assignments stay global)
- DJPlayer: Serato DJ clone, fokus pada BPM sync + CUE saja (warp dihapus)
- DJPlayer: scrollY = nudge, Shift+scrollY = zoom, scrollX = zoom
- DJPlayer: click-drag always scrubs
- DJPlayer: mSampleBPM = detected BPM, immutable
- DJPlayer: pre-roll = 2 seconds (kPreRollSeconds = 2.0f)

## Performance Decisions (session 2026-07-08)
- **ShaderModule**: dihapus — heavy GPU (256M fragment/frame pada shader Julia), tidak esensial
- **SpatialMonitor**: dihapus — CPU berat (bubble sort, 60+ drawing primitif kepala 3D), redundan dengan EclipSpatialRender::PostRender
- **DJPlayer warp**: dihapus — warp-aware rendering O(P × M × B) terlalu berat, disederhanakan ke BPM sync + CUE
- **EclipSpatialRender**: dipertahankan, optimasi nanti (bubble sort → std::sort)
- **FFTtoAdditive**: dibiarkan (130K ops/buffer masih tolerable)
- **MeshInstances3D**: dibiarkan, optimasi nanti (double vertex projection di Process + PostRender)
- **BandVocoder**: dibiarkan

## Done
### Previous work
- FormantFilterEffect: implemented with 4 parallel formant bands (commit 2e6be38)
- Lissajous: full XY oscilloscope module (commit 7783e76)
- ImageSequencerModule: replaced FPS slider with BPM + frames/beat, added dual mode (Sync FPB / Free FPS) (commit f22efbc)
- PatternMatrix: all 11 original layout bugs (dangling pointers, div-by-zero, OOB load, etc.) fixed
- PatternMatrix: header refactored — element grids moved from TrackInfo to PatternMatrix class, TrackInfo simplified
- PatternMatrix: layout helpers rewritten for right-side element area (GetElementAreaStartX, GetElementGridRows, GetElementTotalCols, GetElementAreaWidth)
- PatternMatrix: DrawModule rewritten — elements drawn to the right of pattern slots (R/S/B column bands), bottom bar uses actual module width from GetModuleDimensions
- PatternMatrix: HitTestElementGrid updated for right-side element coordinates (fix: was using mNumTracks instead of mNumSlots)
- PatternMatrix: OnClicked updated for new layout, cancel cell learn logic simplified
- PatternMatrix: ApplyBinding updated — per-track bindings unsupported (rev 6+), standalone element cells handled with mTrack==-1
- PatternMatrix: SaveState/LoadState for rev 6 — global element data, no per-track data; rev ≤5 skips old per-track data
- PatternMatrix: IsResizable() → false, mWidth/mHeight/mPreDockWidth/mPreDockHeight removed, CheckboxUpdated simplified, MouseMoved/Resize no-ops, OnClicked resize guard removed
- PatternMatrix: kMaxSlots 8→32, NoteStepSequencer NSS_NUM_PATTERNS 8→32, StepSequencer kNumPatternSlots 8→32
- PatternMatrix: pages system — PageData struct (pattern blobs per track+slot + currentPattern + color) , page tabs in bottom bar with colors, "+" button to add pages (max 8), SwitchPage/SnapshotPage/RestorePage, slot colors use page color, rev 7 save/load
- PatternMatrix: pages store only per-slot pattern data (MIDI mapping, element grids, track assignments remain global)

### LissajousOut (session 2026-06-24)
- Source/LissajousOut.h/.cpp: new module (IAudioProcessor + IDrawableModule + IVisualSource), XY buffer visualization with configurable scale/intensity/line-width/decay/color/zoom, optional grid background
- Source/ModuleFactory.cpp: REGISTER(LissajousOut, lissajousout, kModuleCategory_Audio)
- Source/CMakeLists.txt: added LissajousOut.cpp/.h

### MeshInstances3D (session 2026-06-23)
- libs/CMakeLists.txt: added tinyobjloader subdirectory
- libs/tinyobjloader/CMakeLists.txt, libs/tinyobjloader/tiny_obj_loader.h: header-only OBJ loader setup
- Source/CMakeLists.txt: added syntetika::tinyobjloader link target and juce::juce_dsp module
- Source/CMakeLists.txt: added MeshInstances3D.cpp/.h to build
- Source/MeshInstances3D.h: full module declaration (IAudioProcessor + IDrawableModule, mesh data, GL handles, instance data, camera, audio analysis (FFT via juce::dsp), UI controls)
- Source/MeshInstances3D.cpp: OBJ loading (via tinyobjloader), OpenGL vertex buffers + VAO, GLSL 330 instancing shaders (vertex + fragment), orbit camera (azimuth/altitude/distance spherical coords), instance layouts (grid/circle/sphere/random), audio FFT analysis (Hann window, juce::dsp::FFT), amplitude envelope, audio-reactive scale/rotation/color/position, save/load state, layout XML config
- Source/ModuleFactory.cpp: registered MeshInstances3D (kModuleCategory_Audio, module name "meshinstances3d")

### IVisualSource refactor (session 2026-06-24)
- LissajousOut: refactored DrawModule() to draw FBO as background then controls, added PostRender() + GetFBO() from IVisualSource
- MeshInstances3D: refactored DrawModule() to draw FBO as background then controls, added PostRender() + GetFBO() from IVisualSource, inline wireframe preview → PostRender() FBO
- Both modules: PostRender() renders glow/wireframe to VisualFBO, accessible via GetFBO() for MonitorModule/DisplayManager

### TriggerWaveEffect (session 2026-06-24)
- Source/TriggerWaveEffect.h: module declaration (IAudioProcessor + IDrawableModule + IVisualSource), beat detection members, waveform buffer, effect mode state
- Source/TriggerWaveEffect.cpp: audio pass-through (like LissajousOut), energy-based beat detection (running RMS average, onset when local > avg * sensitivity), 4 effect modes (pulse/glitch/scanlines/all), HSB→RGB color conversion, FBO rendering via PostRender(), DrawModule() draws FBO then controls
- Source/ModuleFactory.cpp: REGISTER(TriggerWaveEffect, triggerwave, kModuleCategory_Audio)
- Source/CMakeLists.txt: added TriggerWaveEffect.cpp/.h

### GlShaderUtil (session 2026-06-23)
- Source/GlShaderUtil.h/.cpp: created shared utility namespace for GL shader boilerplate — CompileShader, LinkProgram, CompileAndLink (deletes intermediate shaders), DeleteShader/Program (safe null+type-check), GetUniformLocation (cached per program), ClearUniformCache
- Source/CMakeLists.txt: added GlShaderUtil.cpp/.h to build
- Source/MeshInstances3D.cpp: refactored SetupShaders() to use GlShaderUtil::CompileAndLink, removed private CompileShader() method, destructor uses GlShaderUtil::DeleteProgram, uniform lookups cached via GlShaderUtil::GetUniformLocation

### MeshInstances3D noise gate (session 2026-07-02)
- Added per-buffer RMS noise gate: `mGateEnabled` (checkbox), `mGateThreshold` (slider 0-0.1, default 0.005)
- Process(): computes `inputRMS = sqrt(sum(s²)/N)` from input buffer; if RMS < threshold, output zeroed via `gateMul`
- Gate logic: `gateOpen = !mGateEnabled || inputRMS >= mGateThreshold`
- When gate ON and inData null (no cable), inputRMS stays 0 -> gate closed -> silent
- When gate OFF, inputRMS forced to 1.0 -> always open
- `mLastInputRMS` / `mLastGateOpen` stored per-callback for status bar display
- SaveState rev 9: saves mGateEnabled, mGateThreshold
- LoadState rev >=9: loads gate params; older revs use defaults (enabled, threshold=0.005)
- Bugfix: else block was setting inputRMS=1.0 for both `!mGateEnabled` AND `!inData`; fixed with nested if

### EclipSpatialSource + EclipSpatialRender rename & HRTF (session 2026-07-06)
- Rename: EclipsaInput → EclipSpatialSource, EclipsaManager → EclipSpatialRender
- Old files deleted, CMakeLists.txt + ModuleFactory.cpp updated, all references migrated
- HRTF binaural added to EclipSpatialRender: ITD (Woodworth-Schlosser delay model) + ILD (contralateral attenuation), per-object HRTFState (256-sample delay lines), quality modes (ITD/ITD+ILD/Full)
- HRTF UI: checkbox enable, quality dropdown, head radius slider
- Save state rev 3: HRTF params saved/loaded
- Two spatial systems coexist: EclipSpatialSource→EclipSpatialRender (animasi, occlusion, FBO, HRTF, layout presets) + SpatialSource→SpatialRender (HRTF, VBAP, multi-bus, per-source routing)

### SpatialDataSource + SpatialRender getter (session 2026-07-06)
- SpatialDataSource.h: shared struct SpatialSourceInfo (x, y, z, volume, colorHue, occlusion, name), SpatialRoomInfo (room dimensions + listener), SpatialSpeakerInfo (position + channel)
- SpatialRender: added GetNumSpatialSources() + GetSpatialSourceInfo() + GetSpatialRoomInfo() + GetSpeakerInfo() public getters (thread-safe via mutex)
- EclipSpatialRender: added GetNumSpatialSources() + GetSpatialSourceInfo() + GetSpatialRoomInfo() + GetNumSpeakers() + GetSpeakerInfo() wrappers

### ShaderModule + SpatialMonitor removal (session 2026-07-08)
- ShaderModule.h/.cpp deleted, CMakeLists.txt + ModuleFactory.cpp updated
- SpatialMonitor.h/.cpp deleted, CMakeLists.txt + ModuleFactory.cpp updated
- Reason: performance bottlenecks (ShaderModule=heavy GPU, SpatialMonitor=heavy CPU + redundant)

### DJPlayer warp removal (session 2026-07-08)
- WarpMarkers, SampleToBeat/BeatToSample, warp-aware rendering removed
- DJPlayer simplified: BPM sync + CUE only
- Reason: warp-aware O(P × M × B) per-pixel rendering too heavy (~3-5M CPU ops/frame)

### VideoPlayerModule foleys integration (session 2026-07-07)
- foleys_video_engine compiled as single-translation-unit (CMakeLists.txt fix)
- FFmpeg 4.x → 8.x API migration (ch_layout, swr_alloc_set_opts2, duration, etc.)
- avresample.lib pragma removed
- FOLEYS_USE_OPENGL=0 (OpenGL view not needed)
- JUCE API fixes: AudioSourceChannelInfo reference, Optional<PositionInfo> getPosition()
- Bugfix: SetPosition + loop resets mPlayStartTime formula (gTime - mPlayhead/mSpeed)
- VideoPlayerModule builds with foleys video engine

## Known Issues
- tinygltf removed from build (missing nlohmann json.hpp dependency); only OBJ loading supported
- InstanceData struct uses float arrays, sizeof(InstanceData) must remain stable for GPU buffer upload
- OpenGL functions (glVertexAttribDivisor, glDrawElementsInstanced, VAO) require OpenGL 3.2+ (JUCE handles loading)
- FFT is mono (channel 0 only)
- Camera update via mCameraAzimuth/mCameraAltitude/mCameraDistance sliders only (no interactive orbit yet)
- TriggerWaveEffect beat detection is mono (channel 0 only)
- TriggerWaveEffect uses energy-based detection, no FFT/spectral flux analysis yet
- VideoDrumSampler still uses ffmpeg.exe subprocess (foleys migration pending)

## Key Decisions
- **Elements on the right**: element area occupies a vertical strip to the right of pattern slots, spanning the same Y range as tracks; each grid type (rotary/slider/button) occupies a column band with R/S/B header labels
- **Module not resizable**: content (cells, track rows) is fixed-size; allowing user resize only created dead space (canvas grows but content does not)
- **Exact content dimensions**: GetModuleDimensions returns the precise content width/height, no MAX with stored user size; when docked, width is at least window width
- **Resize no-op preserved**: IsResizable() returns false, but Resize() override exists as empty function to prevent base-class assert if called accidentally
- **32 max slots**: requires also raising sequencer pattern limits (NSS_NUM_PATTERNS, kNumPatternSlots) to prevent out-of-bounds crashes
- **Pages contain only pattern data**: per-slot pattern blobs + current pattern + color; MIDI mapping, element grids, and track assignments are global (user confirmed "mapping midi tetap")
- **MeshInstances3D standalone module**: registered in ModuleFactory (kModuleCategory_Audio) like Lissajous, not in EffectFactory; uses IAudioProcessor (not IAudioEffect)
- **OBJ-only 3D loading**: tinyobjloader is header-only with no external dependencies; tinygltf dropped due to missing nlohmann json dependency
- **OBJ index expansion**: each face vertex is duplicated (positions/normals pushed separately for each index), then sequential indices are generated; wastes memory but correct
- **LissajousOut standalone module**: registered in ModuleFactory (kModuleCategory_Audio) like Lissajous; implements IAudioProcessor + IVisualSource for FBO visual output
- **LissajousOut grid background**: optional dark grid + crosshair background, configurable via checkbox
- **LissajousOut zoom as divisor**: effScale = scale / zoom — zoom counterbalances scale for fine adjustment
- **TriggerWaveEffect standalone module**: registered in ModuleFactory (kModuleCategory_Audio) like Lissajous/LissajousOut; implements IAudioProcessor + IVisualSource for FBO visual output
- **TriggerWaveEffect FBO rendering**: DrawModule() draws FBO then controls; PostRender() renders beat-synced effects to VisualFBO, accessible via GetFBO() for MonitorModule/DisplayManager
- **Beat detection**: simple running-RMS energy-based onset detection (256-sample window), configurable sensitivity threshold, 50ms hold to prevent double-triggers
- **DJPlayer BPM sync + CUE only**: warp markers removed, DJPlayer simplified to BPM sync and cue points
- **ShaderModule deleted**: too GPU-heavy (256M fragmen/frame pada shader Julia)
- **SpatialMonitor deleted**: CPU-heavy + redundan dengan EclipSpatialRender::PostRender
- **foleys_video_engine OBJ-only**: only FFmpeg format registered (no OpenGL rendering)

## Relevant Files
- Source/PatternMatrix.h: class declaration, global grids, rev 6→7, PageData struct, kMaxSlots=32
- Source/PatternMatrix.cpp: pages implementation, slot colors per page, SnapshotPage/RestorePage with pattern blobs
- Source/NoteStepSequencer.h: NSS_NUM_PATTERNS=32
- Source/StepSequencer.h: kNumPatternSlots=32
- Source/MeshInstances3D.h: module declaration (IAudioProcessor, GL handles, camera, instances)
- Source/MeshInstances3D.cpp: OBJ loading, GL shaders, instancing, audio FFT, camera, UI
- Source/CMakeLists.txt: added MeshInstances3D source files, GlShaderUtil source files, tinyobjloader link, juce_dsp module
- Source/TriggerWaveEffect.h: module declaration (IAudioProcessor + IDrawableModule + IVisualSource, beat detection, waveform buffer, effect modes)
- Source/TriggerWaveEffect.cpp: energy-based beat detection, 4 visual effect modes (pulse/glitch/scanlines/all), FBO rendering via PostRender(), rainbow HSB→RGB conversion
- Source/GlShaderUtil.h: utility namespace — CompileShader, LinkProgram, CompileAndLink, DeleteShader, DeleteProgram, GetUniformLocation (cached)
- Source/GlShaderUtil.cpp: implementation
- libs/CMakeLists.txt: added tinyobjloader subdirectory
- libs/tinyobjloader/CMakeLists.txt, libs/tinyobjloader/tiny_obj_loader.h: header-only OBJ loader
- Source/DJPlayer.h: DJPlayer class — BPM sync + CUE, warp removed
- Source/DJPlayer.cpp: simplified DJPlayer (BPM sync + CUE only)
- libs/foleys_video_engine/CMakeLists.txt: foleys static library, single-translation-unit, FOLEYS_USE_OPENGL=0, FFmpeg 8.x APIs
- Source/VideoPlayerModule.h/.cpp: foleys-based video player (no ffmpeg.exe subprocess)
