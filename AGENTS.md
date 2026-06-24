## Goal
Build audio-reactive 3D/2D visual modules (MeshInstances3D, Syntetiscope, TriggerWaveEffect) with IVisualSource FBO output for MonitorModule compatibility.

## Constraints & Preferences
- Element grids (rotary/slider/button) are global/standalone, placed to the right of pattern slots
- Module is NOT user-resizable (content is fixed-size, resize adds dead space)
- Save format rev 6 uses global element bindings, not per-track; rev 7 adds page data
- MIDI bindings use mTrack=-1 + mSlot=-1 for standalone element cells, mSlot>=0 for pattern/scene
- 3D model support: OBJ only (no GLTF until nlohmann json is available)
- MeshInstances3D is a standalone module (kModuleCategory_Audio), not an EffectChain effect
- Pages: differ only in pattern data stored per slot (MIDI mapping, element grids, track assignments stay global)

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

### Syntetiscope (session 2026-06-24)
- Source/Syntetiscope.h/.cpp: new module (IAudioProcessor + IDrawableModule + IVisualSource), XY buffer visualization with multi-pass glow rendering, configurable scale/intensity/beam/decay/color/zoom, optional Lissajous overlay
- Source/ModuleFactory.cpp: REGISTER(Syntetiscope, syntetiscope, kModuleCategory_Audio)
- Source/CMakeLists.txt: added Syntetiscope.cpp/.h

### MeshInstances3D (session 2026-06-23)
- libs/CMakeLists.txt: added tinyobjloader subdirectory
- libs/tinyobjloader/CMakeLists.txt, libs/tinyobjloader/tiny_obj_loader.h: header-only OBJ loader setup
- Source/CMakeLists.txt: added syntetika::tinyobjloader link target and juce::juce_dsp module
- Source/CMakeLists.txt: added MeshInstances3D.cpp/.h to build
- Source/MeshInstances3D.h: full module declaration (IAudioProcessor + IDrawableModule, mesh data, GL handles, instance data, camera, audio analysis (FFT via juce::dsp), UI controls)
- Source/MeshInstances3D.cpp: OBJ loading (via tinyobjloader), OpenGL vertex buffers + VAO, GLSL 330 instancing shaders (vertex + fragment), orbit camera (azimuth/altitude/distance spherical coords), instance layouts (grid/circle/sphere/random), audio FFT analysis (Hann window, juce::dsp::FFT), amplitude envelope, audio-reactive scale/rotation/color/position, save/load state, layout XML config
- Source/ModuleFactory.cpp: registered MeshInstances3D (kModuleCategory_Audio, module name "meshinstances3d")

### IVisualSource refactor (session 2026-06-24)
- Syntetiscope: refactored DrawModule() to draw FBO as background then controls, added PostRender() + GetFBO() from IVisualSource
- MeshInstances3D: refactored DrawModule() to draw FBO as background then controls, added PostRender() + GetFBO() from IVisualSource, inline wireframe preview → PostRender() FBO
- Both modules: PostRender() renders glow/wireframe to VisualFBO, accessible via GetFBO() for MonitorModule/DisplayManager

### TriggerWaveEffect (session 2026-06-24)
- Source/TriggerWaveEffect.h: module declaration (IAudioProcessor + IDrawableModule + IVisualSource), beat detection members, waveform buffer, effect mode state
- Source/TriggerWaveEffect.cpp: audio pass-through (like Syntetiscope), energy-based beat detection (running RMS average, onset when local > avg * sensitivity), 4 effect modes (pulse/glitch/scanlines/all), HSB→RGB color conversion, FBO rendering via PostRender(), DrawModule() draws FBO then controls
- Source/ModuleFactory.cpp: REGISTER(TriggerWaveEffect, triggerwave, kModuleCategory_Audio)
- Source/CMakeLists.txt: added TriggerWaveEffect.cpp/.h

### GlShaderUtil (session 2026-06-23)
- Source/GlShaderUtil.h/.cpp: created shared utility namespace for GL shader boilerplate — CompileShader, LinkProgram, CompileAndLink (deletes intermediate shaders), DeleteShader/Program (safe null+type-check), GetUniformLocation (cached per program), ClearUniformCache
- Source/CMakeLists.txt: added GlShaderUtil.cpp/.h to build
- Source/ShaderModule.cpp: refactored CompileShader() to use GlShaderUtil::CompileAndLink, CleanupShader() to use GlShaderUtil::DeleteProgram, uniform lookups cached via GlShaderUtil::GetUniformLocation
- Source/MeshInstances3D.cpp: refactored SetupShaders() to use GlShaderUtil::CompileAndLink, removed private CompileShader() method, destructor uses GlShaderUtil::DeleteProgram, uniform lookups cached via GlShaderUtil::GetUniformLocation

## Known Issues
- tinygltf removed from build (missing nlohmann json.hpp dependency); only OBJ loading supported
- InstanceData struct uses float arrays, sizeof(InstanceData) must remain stable for GPU buffer upload
- OpenGL functions (glVertexAttribDivisor, glDrawElementsInstanced, VAO) require OpenGL 3.2+ (JUCE handles loading)
- FFT is mono (channel 0 only)
- Camera update via mCameraAzimuth/mCameraAltitude/mCameraDistance sliders only (no interactive orbit yet)
- TriggerWaveEffect beat detection is mono (channel 0 only)
- TriggerWaveEffect uses energy-based detection, no FFT/spectral flux analysis yet

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
- **Syntetiscope standalone module**: registered in ModuleFactory (kModuleCategory_Audio) like Lissajous; implements IAudioProcessor + IVisualSource for FBO visual output
- **Syntetiscope multi-pass glow**: 4-passes of thick semi-transparent lines (1x, 3x, 6x, 12x width) simulate Gaussian beam profile without GLSL; open path (ofEndShape(false)) avoids closing diagonal
- **Syntetiscope zoom as divisor**: effScale = scale / zoom — zoom counterbalances scale for fine adjustment
- **TriggerWaveEffect standalone module**: registered in ModuleFactory (kModuleCategory_Audio) like Lissajous/Syntetiscope; implements IAudioProcessor + IVisualSource for FBO visual output
- **TriggerWaveEffect FBO rendering**: DrawModule() draws FBO then controls; PostRender() renders beat-synced effects to VisualFBO, accessible via GetFBO() for MonitorModule/DisplayManager
- **Beat detection**: simple running-RMS energy-based onset detection (256-sample window), configurable sensitivity threshold, 50ms hold to prevent double-triggers

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
