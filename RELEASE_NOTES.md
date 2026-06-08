# Release Notes

## HAORENDER-AI 2026-06-08 Demo Release

This release refreshes the Windows preview package and adds demo screenshots for the GitHub repository and release page.

### Highlights

- Rebuilt the portable Windows package as `HAORENDER-AI-20260608-win64.zip`.
- Added repository demo screenshots for LookDev, OpenGL ray trace preview, and retarget animation preview.
- Kept the project under the MIT License.

### Validation

- `HaoRender-GI` Release build: passed.
- `HaoRigBatchTest` Release build: passed.
- Package script completed successfully.

## HAORENDER-AI 2026-06-06 Preview Release

This release packages the current HAORENDER-AI preview as a Windows Qt desktop application. The executable target is still named `HaoRender-GI.exe` for this preview.

The project is released under the MIT License.

### Highlights

- Added LookDev AI workflow for prompt-driven Phong/Toon/PBR parameter generation.
- Added a local rendering skill library and style knowledge base for stronger LLM guidance.
- Added Toon controls including shadow/lit/ramp/material override parameters.
- Added style preset save/load for `.haostyle.json`.
- Added HaoRig AI workspace for target/source skeleton loading, AI-assisted mapping, preview, and export.
- Added embedded Rig preview viewport with shared raster rendering backend.
- Added VRM expression and eye debug controls, blink/look/Fcl_EYE morph sliders, and gaze preview.
- Improved VRM/MToon material handling for face/eye rendering.
- Improved FBX animation retargeting by computing rotation deltas against the first-frame reference pose, reducing Assimp FBX helper/rest-pose mismatch failures.
- Added retarget quality scoring for missing channels, wrist flips, shoulder collapse, foot floating, root motion, and eye side reversal.
- Added startup geometry clamping so the Qt window opens inside the current screen work area.

### Validation

- `HaoRender-GI` Release build: passed.
- `HaoRigBatchTest` Release build: passed.
- MAXIMO/SHE motion set: 40 FBX motions tested against four AiGril targets, 160 combinations total.
  - Load failures: 0
  - Retarget failures: 0
  - Main review signal: expected foot-floating warnings on capoeira/flip/tumbling-style motions.
- Larger downloaded motion set: 174 FBX sources tested against `AiGril.glb`.
  - Successful retarget previews: 166
  - Files without transform animation channels: 8
  - Clean OK cases: 117
  - Needs-review cases: 49
  - Wrist flip detections: 0
  - Eye reversal detections: 0

### Known Limitations

- Foot contact detection is conservative and currently flags intentional airborne or tumbling motions as `foot_floating`.
- Some `_face_P_` FBX files contain facial/morph data without transform animation channels and are rejected by the current retarget pipeline.
- HaoRig AI maps skeleton semantics and applies procedural retarget correction; it is not yet a full Blender/UE-grade retarget authoring stack.
- LookDev AI quality depends heavily on the current skill prompts and exposed rendering parameters; artist review is still required.
- The portable package does not bundle large local model/motion datasets.
