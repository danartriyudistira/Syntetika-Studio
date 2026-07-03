## v1.1.1 — HRTF Overload Hotfix & Project Cleanup

### Critical Bug Fix
- **SpatialRender HRTF overload**: Nested inner `for (int s = 0; s < bufferSize; ++s)` loop at line 255 removed — was running inside the outer per-sample loop, causing `bufferSize×` (256-512×) amplification into binaural output. Now uses outer `s` index directly, correct 1 sample per iteration.
- **SpatialRender brace mismatch**: Missing `}` for mutex scope added — build was broken after HRTF refactoring.
- **Debug logging removed**: HRTF debug `printf` (peak level per 60 callbacks) cleaned up.

### Project Cleanup
- **Dead code removed**: `VisualNodeBase.cpp/.h`, `CodeEditorNode.h`, `VisualNode.h` — all unused.
- **Stale artifacts deleted**: `build2/` (270 MB stale Ninja/Multi-Config build dir), `build.log`, `dist/` (v1.0.0 installer), `ignore/`, `Syntetiscope.cpp/.h`.

### Full Changelog
https://github.com/danartriyudistira/Syntetika-Studio/compare/v1.1.0...v1.1.1
