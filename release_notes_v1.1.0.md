## v1.1.0 — Visual Modules & FPS Optimization

### New Visual Modules
| Module | Description |
|--------|-------------|
| LissajousOut | XY audio buffer visualization with configurable scale/intensity/line-width/decay/color/zoom, optional grid background |
| MeshInstances3D | Audio-reactive 3D OBJ model viewer with orbit camera, instance layouts, FFT audio analysis, SVG support |
| TriggerWaveEffect | Beat-synced visual effects with energy-based onset detection — 4 modes: Pulse, Glitch, Scanlines, All |
| ShaderModule | GLSL fragment shader editor with live preview, dynamic uniform sliders, input source support, Shadertoy conversion |

### Infrastructure
- IVisualSource: FBO output interface for module chaining (Source → Processor → Monitor)
- GlShaderUtil: Shared GL shader utility — compile/link/uniform caching
- VisualFBO PBO: Double-buffered Pixel Buffer Object for async GPU→CPU readback
- tinyobjloader: Header-only OBJ 3D model loader
- nanosvg: Header-only SVG loader

### Major Bug Fixes
- **PatternMatrix**: Full rewrite — 11 layout bugs fixed, pages system (up to 8), 32 max slots, save format rev 6→7
- **MonitorModule**: PBO async readback + fullscreen chrome fix
- **DisplayManager**: Own FBO output, memory leak fix, mutex for audio thread
- **LayerComposition**: Direct GPU texture Draw replaces CPU roundtrip, dynamic FBO size, blend modes
- **ImageSequencerModule**: Decode-on-demand eliminates memory leak
- **TrigMatrixFX**: Use-after-free fix (exprtk persistent vars), recursive mutex
- **ShaderModule**: Default white texture fallback, u_texture auto-bind

### FPS Optimizations
- MeshInstances3D: Member vectors replace heap alloc per audio callback
- ShaderModule: glTexParameteri at texture creation (not per frame)
- TrigMatrixFX: Dynamic grid step for mini previews

### Installer
Windows MSI installer included (WiX).

### Full Changelog
https://github.com/danartriyudistira/Syntetika-Studio/compare/v1.0.0...v1.1.0
