# HAORENDER-AI Electron Desktop Shell

The new product UI is split from the C++ renderer/converter core.

## Runtime Shape

```text
Electron / React / Tailwind UI
  |
  | Electron IPC
  |
haorender-core.exe
  - --inspect-model
  - --convert-model
  - --render-preview
  - --list-export-formats
```

The first Electron version focuses on the model-conversion workflow:

1. Import FBX / GLB / GLTF / VRM / OBJ / DAE / STL / PLY.
2. Inspect mesh, material, skeleton, morph, expression, and animation payloads.
3. Generate a deterministic conversion plan from the inspected asset.
4. Convert through the C++ backend core.
5. Preview source and result side by side.
6. Show conversion status and output location.

## Build

```powershell
cmake --build build --config Release --target haorender-core -- /m
cd desktop
pnpm install
pnpm build
pnpm start
```

The Electron app resolves the backend in this order:

1. `HAORENDER_CORE_PATH`
2. `build/Release/haorender-core.exe`
3. `build/Debug/haorender-core.exe`
4. packaged resources

## Current Boundaries

- Three.js previews GLB / GLTF / VRM directly.
- Other formats use the C++ preview image as a fallback.
- High-quality OpenGL/DXR viewport embedding is intentionally deferred.
- Qt remains available as the legacy workbench while the Electron UI matures.
