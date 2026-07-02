# HAORENDER-AI

HAORENDER-AI is a Windows rendering and asset-pipeline workbench focused on GPU rendering, model conversion, lightweight LookDev, toon/Phong/PBR parameter exploration, and AI-assisted humanoid animation retargeting.

The current UI direction is a modern Electron desktop shell: artists and technical artists work in dedicated pages for rendering, model conversion, skeleton mapping, resource inspection, before/after comparison, and delivery reports, while the C++ core stays behind the UI as a command-line backend.

The product is moving toward a split architecture:

- `haorender-core.exe`: C++ command-line/backend core for inspect, convert, and preview tasks.
- `desktop/`: Electron + React + Tailwind desktop shell for the modern product UI.
- `HaoRender-GI.exe`: legacy Qt/C++ workbench kept as a rendering and debugging surface during migration.

The current product direction is a small artist-facing tool rather than a general DCC package:

- Raster, OpenGL ray trace, and DXR preview modes
- Phong, Toon, and PBR LookDev controls
- LookDev AI prompt workflow with Doubao/OpenAI-compatible local configuration
- Style preset save/load through `.haostyle.json`
- VRM/glTF/FBX loading through Assimp and local glTF helpers
- VRM MToon material adaptation, expression controls, eye gaze preview
- Asset Converter workspace for FBX / GLB / VRM / PMX conversion planning, native GLB/VRM writing, backend task packages, QA reports, and preview
- HaoRig AI skeleton mapping and animation retarget preview
- Batch retarget quality testing for downloaded FBX/VRMA motion sets

## Demo Screenshots

![Electron model conversion workbench](docs/demo/electron-convert-workbench.png)

![Electron skeleton mapping workbench](docs/demo/electron-rig-workbench.png)

The redesigned Electron interface separates each domain into its own workflow:

- Model conversion is shown as an asset pipeline from source inspection to target format and delivery output.
- Skeleton mapping is shown as a retargeting workspace with target/source assets, humanoid chain checks, mapping quality, and mapping table.
- Reports collect the output artifact, resource summary, conversion status, execution log, and downstream risks.

## Build

The project is configured with CMake and Qt 5.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target HaoRender-GI -- /m
cmake --build build --config Release --target haorender-core -- /m
```

The local workstation build expects Qt, Eigen, Assimp, and DXC runtime paths to be available. See `CMakeLists.txt` for the current dependency hints and post-build runtime copy rules.

## AI Configuration

Do not commit real keys. Copy `llm.env.example` to `llm.env.local` and edit it locally:

```env
AI_PROVIDER=doubao
DOUBAO_API_KEY=<set locally, never commit a real key>
DOUBAO_BASE_URL=https://ark.cn-beijing.volces.com/api/v3
DOUBAO_MODEL=doubao-seed-2-0-pro-260215
AI_REQUEST_TIMEOUT_MS=90000
```

The app also checks environment variables and the user config path before falling back to local rule-based recommendations.

## Package

After a Release build:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1
```

The script creates a portable Windows zip under `dist/` containing the executable, Qt/Assimp/DXC runtime DLLs, Qt plugins, docs, style presets, and the safe AI env template.
The executable target is still named `HaoRender-GI.exe` in this preview build.

## Retarget QA

The batch test target validates mapping and retarget stability across local motion datasets:

```powershell
cmake --build build --config Release --target HaoRigBatchTest -- /m
build\Release\HaoRigBatchTest.exe --target "F:\AIGril\Resources\AiGril.glb" --source "path\to\motion.fbx"
```

Recent large local checks covered MAXIMO/SHE and harvested Mixamo-style motions. See `RELEASE_NOTES.md` for the current results and known limitations.

## Electron Desktop Shell

The modern desktop shell lives in `desktop/` and talks to `build/Release/haorender-core.exe` through Electron IPC.

```powershell
cd desktop
pnpm install
pnpm build
pnpm start
```

For development:

```powershell
cd desktop
pnpm desktop:dev
```

## Backend Core CLI

Native VRM smoke conversion:

```powershell
build\Release\haorender-core.exe --convert-model "path\to\character.glb" "out\character.vrm" vrm
```

Simple model interchange:

```powershell
build\Release\haorender-core.exe --list-export-formats
build\Release\haorender-core.exe --inspect-model "path\to\model.fbx"
build\Release\haorender-core.exe --render-preview "path\to\model.fbx" "out\preview.png"
build\Release\haorender-core.exe --convert-model "path\to\model.glb" "out\model.fbx"
build\Release\haorender-core.exe --convert-model "path\to\model.fbx" "out\model.glb"
```

## License

HAORENDER-AI is open source under the MIT License. See `LICENSE` for details.
