# HAORENDER-AI Asset Converter

The Asset Converter is designed as a conversion workbench, not a fake "rename the extension" tool.

## Product Flow

1. Import FBX, GLB, GLTF, VRM, OBJ, DAE, STL, PLY or related character assets.
2. Inspect mesh, material, skeleton, morph, expression and animation payloads.
3. Choose a target format: GLB, GLTF, FBX, OBJ, DAE, STL, PLY, VRM or later PMX/MMD.
4. Generate a conversion plan with risks and manual QA points.
5. Execute the available backend.
6. Reload the result for before/after preview when a model is written.
7. Write `conversion_plan.json`, `conversion_report.md` and a reproducible backend task package.

## Current Backend Policy

- Same-format exports are copied as identity exports.
- Simple model formats are attempted in-process through Assimp Exporter first: GLB, GLTF, FBX, OBJ, DAE, STL, PLY and any other exporter exposed by the current Assimp build.
- VRM export is attempted in-process through the native VRM 1.0 writer.
- Blender scripts are generated only as optional developer fallback/debug artifacts.
- PMX export should become a native PMX binary writer. The Blender + `mmd_tools` path is only a temporary fallback.

## Native Simple Model Converter

The simple-format phase deliberately ignores MMD/PMX complexity and focuses on common interchange formats:

- Lists current exporter capability through `--list-export-formats`.
- Converts through `--convert-model <source> <output> [targetExtension]`.
- Infers target format from the output extension when `targetExtension` is omitted.
- Uses Assimp native export IDs such as `glb2`, `gltf2`, `fbx`, `obj`, `collada`, `stl`, and `ply`.
- Tries several Assimp import flag sets so simple assets can still convert when strict validation is too harsh.
- Accepts Assimp incomplete-flagged scenes when a root node exists. This is important for FBX 7700 animation/skeleton-only files.
- Refuses meshless sources for geometry-only formats such as OBJ, STL, PLY, 3MF and 3DS, because those outputs would be empty.
- Refuses meshless sources for VRM, because VRM needs an avatar mesh.

Smoke-tested paths:

- `GLB -> FBX`
- `GLB -> OBJ`
- `GLB -> DAE`
- `GLB -> STL`
- `GLB -> PLY`
- `GLB -> GLTF`
- `FBX -> GLB`
- `FBX -> OBJ`
- FBX 7700 animation-only `Idle.fbx` -> GLB

## Native VRM Writer V1

The first native VRM path writes a real GLB 2.0 container with the `VRMC_vrm` extension:

- Exports the source scene to GLB through Assimp Exporter.
- Patches the GLB JSON chunk in-process.
- Adds `extensionsUsed` / `extensionsRequired` entries for `VRMC_vrm`.
- Writes `specVersion: "1.0"`.
- Writes generated `meta` fields with placeholder author/license warning.
- Builds `humanoid.humanBones` from detected skeleton semantics.
- Rejects invalid avatars when required VRM humanoid bones are missing.
- Includes a numbered-chain fallback for assets using names such as `torso_joint_1`, `leg_joint_L_1`, `arm_joint_R_2`.

Still missing from the native VRM writer:

- Full MToon material extension write-back.
- Expression / blendshape preset export.
- Look-at / eye gaze export.
- Spring bones, colliders and secondary animation.
- Proper user-facing license/author metadata editing.

## Backend Task Package

Every Execute run writes these files into the output directory:

- `conversion_plan.json`: machine-readable source inventory, target format, risks and backend status.
- `conversion_report.md`: human-readable QA report for artists and technical artists.
- `haorender_asset_convert_blender.py`: optional developer fallback bridge script for FBX / GLB / VRM / PMX style conversion tasks.
- `run_conversion.ps1`: optional Windows fallback runner that calls Blender with the source path, target format and requested output path.
- `backend_instructions.md`: backend requirements and QA policy.
- `backend_status.json`: minimal status and dependency flags for automation.

This makes unsupported or partially-supported target formats still useful for development, but the product goal is not to ask artists to install Blender add-ons. Native writers should handle the normal user path.

## AI Role

AI should help with semantic planning, risk explanation, bone/material/morph mapping hints and report writing. It should not claim lossless conversion when the target ecosystem has missing metadata, different material models, or different humanoid/avatar rules.

## High-Risk Areas

- FBX rest pose and coordinate conventions
- VRM humanoid metadata, expressions and look-at
- MMD morphs, toon materials, physics, rigid bodies and constraints
- Animation embedding vs external animation formats such as VRMA/VMD
- Material translation between PBR, MToon and MMD toon workflows
