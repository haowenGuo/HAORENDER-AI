# HaoRender-GI LookDev Style Knowledge Base

This document is the style cookbook behind the LookDev AI agent. The app embeds a compressed version of this knowledge in `src/app/llm_lookdev_client.cpp`.

The deeper production-style skill library lives in [`lookdev_rendering_skill_library.md`](lookdev_rendering_skill_library.md). Use that file when adding new mature rendering references or mapping external shader workflows into HaoRender-GI controls.

The goal is not to make the LLM invent taste from empty words. The agent should translate an artist's request into concrete Phong, Toon, Outline, and PBR-assist settings, then keep asking focused questions until the visual target is clear.

## Research Notes

- The agent should pick one mature rendering skill first, then tune parameters. It should not combine every attractive effect at once.
- Unity Toon Shader separates toon style into shadow steps, feather/softness, highlights, rim light, normal-map behavior, scene-light influence, and outline controls.
- MToon frames anime rendering around lit color, shade color, toony thresholding, shadow receive control, rim additive light, and outline width/color.
- Arc System Works' Guilty Gear Xrd style depends on a full art-directed 2D-emulation pipeline, including model/normal/camera choices. HaoRender-GI can only approximate the shader-side subset for now.
- Valve's Team Fortress 2 paper emphasizes readability: silhouette clarity, rim highlights, luminance and hue variation, and controlled non-photorealistic shading.
- Gooch technical illustration uses cool-to-warm midtone hue shifts while preserving highlights and edge lines for shape readability.
- Filament and Unreal PBR docs are useful as guardrails: roughness/smoothness controls highlight blur, metallic is mostly binary, and specular should not be abused as a roughness substitute.

Sources:

- Valve, *Illustrative Rendering in Team Fortress 2*: <https://steamcdn-a.akamaihd.net/apps/valve/2007/NPAR07_IllustrativeRenderingInTeamFortress2.pdf>
- Unity Toon Shader, shading steps and feather: <https://docs.unity3d.com/ja/Packages/com.unity.toonshader%400.9/manual/ShadingStepAndFeather.html>
- Unity Toon Shader, rim light: <https://docs.unity3d.com/ja/Packages/com.unity.toonshader%400.9/manual/Rimlight.html>
- Unity Toon Shader, outline: <https://docs.unity3d.com/ja/Packages/com.unity.toonshader%400.9/manual/Outline.html>
- VRM / MToon shader documentation: <https://vrm.dev/en/univrm/shaders/shader_mtoon/>
- Gooch et al., *A Non-Photorealistic Lighting Model for Automatic Technical Illustration*: <https://users.cs.northwestern.edu/~ago820/SIG98/abstract.html>
- Google Filament PBR reference: <https://google.github.io/filament/main/filament.html>
- Unreal Engine physically based materials: <https://dev.epicgames.com/documentation/en-us/unreal-engine/physically-based-materials-in-unreal-engine>

## Parameter Vocabulary

Use these controls as the agent's "rendering words":

- `toon.diffuseSteps`: how many visible light/shadow bands exist.
- `toon.diffuseSoftness`: softness of the main toon band boundary.
- `toon.shadowFloor`: minimum colored brightness for shadow bands; prevents dark textures from becoming dead black.
- `toon.litFloor`: minimum brightness of lit toon bands; useful for game-anime readability.
- `toon.rampBias`: shifts the whole ramp toward shadow or light.
- `toon.rampContrast`: controls whether the ramp is soft/painterly or hard/graphic.
- `toon.shadowMapStrength`: how much real shadow-map occlusion is allowed to crush the toon ramp.
- `toon.shadowThreshold`: where the shadow band begins.
- `toon.shadowSoftness`: softness of the shadow edge.
- `toon.shadowTint`: art-directed shadow hue.
- `toon.highlightThreshold`: how selective the highlight region is.
- `toon.highlightSoftness`: highlight edge softness.
- `toon.highlightStrength`: intensity of stylized highlights.
- `toon.highlightTint`: highlight color.
- `rimStrength`: view-angle edge light strength.
- `rimPower`: how narrow the rim is; lower is broad, higher is tight.
- `toon.rimThreshold` and `toon.rimSoftness`: toon-shaped rim coverage and softness.
- `outline.widthPixels`: visual line width.
- `outline.opacity`: line strength.
- `outline.color`: silhouette line hue.
- `toon.materialOverrideEnabled`: enables stylized base-color remapping before toon lighting.
- `toon.materialTextureStrength`: how strongly source textures influence the toon base color.
- `toon.materialLift`: minimum albedo floor for dark materials.
- `toon.materialSaturation`: base-color saturation control.
- `toon.materialContrast`: base-color contrast control.
- `secondaryLightScale`: fill light amount.
- `ambientStrength`: non-directional lift; useful for soft anime and face readability.
- `specularStrength`, `smoothness`, `shininess`: highlight amount, gloss, and size.
- PBR assist fields: IBL and channel mapping are support controls, not the main style language for toon requests.

## PBR Mode Policy

When the current renderer state is PBR, the agent must stay in PBR unless the artist clearly asks for Toon, Phong, anime, cel, cartoon, outline, or NPR stylization.

Effective PBR controls currently exposed in HaoRender-GI:

- `exposure`
- `normalStrength`
- `enableShadows`
- `enableBackfaceCulling`
- `pbr.iblEnabled`
- `pbr.iblDiffuseStrength`
- `pbr.iblSpecularStrength`
- `pbr.skyLightStrength`
- packed texture channels: metallic, roughness, AO, emissive

Do not pretend that the current PBR panel can directly edit per-material base color, roughness factor, metallic factor, clearcoat, transmission, sheen, or anisotropy. If the user asks for those, provide the closest approximation and mention the missing control in `assistantReply`.

### PBR Neutral LookDev / 写实材质检查

Intent: stable material inspection without stylized artifacts.

Recommended:

- `preferredPipeline`: `Raster`
- `shadingModel`: `PBR`
- `toon.enabled`: `false`
- `outline.enabled`: `false`
- `exposure`: `1.00-1.18`
- `normalStrength`: `0.90-1.10`
- `enableShadows`: `true`
- `enableBackfaceCulling`: `true`
- `iblEnabled`: `true`
- `iblDiffuseStrength`: `0.50-0.85`
- `iblSpecularStrength`: `0.65-1.05`
- `skyLightStrength`: `0.12-0.32`
- Preserve channel indices unless the user asks about packed maps.

### PBR Soft Studio / 柔和棚拍

Intent: cleaner, softer product preview with less harsh material contrast.

Recommended:

- `shadingModel`: `PBR`
- `exposure`: `1.08-1.28`
- `normalStrength`: `0.85-1.05`
- `iblDiffuseStrength`: `0.75-1.20`
- `iblSpecularStrength`: `0.55-0.90`
- `skyLightStrength`: `0.25-0.55`

### PBR Contrast Product / 高级产品写实

Intent: stronger reflection separation and more premium contrast.

Recommended:

- `shadingModel`: `PBR`
- `exposure`: `0.92-1.12`
- `normalStrength`: `0.95-1.20`
- `iblDiffuseStrength`: `0.20-0.55`
- `iblSpecularStrength`: `0.95-1.45`
- `skyLightStrength`: `0.05-0.20`

Avoid in PBR:

- Enabling Toon/Outline unless explicitly requested.
- Using Phong rim/specular to solve material realism.
- Overdriving exposure and IBL specular at the same time.

## Toon / Phong Style Profiles

### Soft Anime / 二游柔和 / Genshin-like

Intent: soft readable character shading, colored shadows, clean small highlights, modest outline, gentle rim. This is the default answer for "柔和", "温和", "二游", "原神感", "日系角色", "清爽动画".

Recommended:

- `preferredPipeline`: `Raster`
- `shadingModel`: `Phong`
- `toon.enabled`: `true`
- `useTonemap`: `true`
- `hardSpecular`: `false`
- `primaryLightOnly`: `false`
- `exposure`: `1.03-1.18`
- `secondaryLightScale`: `0.18-0.34`
- `ambientStrength`: `0.05-0.12`
- `diffuseStrength`: `1.00-1.12`
- `specularStrength`: `0.22-0.50`
- `smoothness`: `0.72-0.88`
- `shininess`: `48-88`
- `diffuseSteps`: `3-4`
- `diffuseSoftness`: `0.06-0.12`
- `shadowThreshold`: `0.40-0.50`
- `shadowSoftness`: `0.08-0.16`
- `shadowTint`: cool blue/violet such as `{0.62, 0.68, 0.94}`, or warm peach for skin.
- `highlightThreshold`: `0.30-0.42`
- `highlightSoftness`: `0.06-0.12`
- `highlightStrength`: `0.45-0.85`
- `highlightTint`: cream white such as `{1.0, 0.97, 0.92}`
- `rimStrength`: `0.20-0.55`
- `rimPower`: `1.8-3.2`
- `rimTint`: pale blue/cream.
- `rimThreshold`: `0.25-0.38`
- `rimSoftness`: `0.07-0.14`
- `outline.widthPixels`: `1.0-2.4`
- `outline.opacity`: `0.55-0.82`
- `outline.color`: dark cool gray, not pure black.

Avoid:

- Hard specular, black shadow tint, outline opacity near 1.0, high rim and high specular at the same time.

Ask if vague:

- "你更想要影子像二游那样柔和过渡，还是像赛璐璐那样边界清晰？"

### Graphic Cel / TV Anime

Intent: crisp cel bands, graphic silhouette, readable animation frame.

Recommended:

- `hardSpecular`: `true`
- `diffuseSteps`: `2-3`
- `diffuseSoftness`: `0.00-0.04`
- `shadowThreshold`: `0.42-0.56`
- `shadowSoftness`: `0.00-0.05`
- `highlightThreshold`: `0.24-0.38`
- `highlightSoftness`: `0.00-0.04`
- `highlightStrength`: `0.30-0.75`
- `rimStrength`: `0.15-0.45`
- `rimPower`: `2.5-5.0`
- `outline.widthPixels`: `2.0-4.5`
- `outline.opacity`: `0.75-1.0`

Avoid:

- Too much fill light and high diffuse softness, because it erases cel clarity.

Ask if vague:

- "你希望更像动画截图的硬阴影，还是保留一点 3D 手办的柔和体积？"

### Figure Showcase / 手办宣传图

Intent: product-like character display with clean forms, controlled glossy accents, not fully realistic.

Recommended:

- `exposure`: `1.08-1.30`
- `secondaryLightScale`: `0.22-0.42`
- `ambientStrength`: `0.04-0.10`
- `specularStrength`: `0.45-0.85`
- `smoothness`: `0.82-0.96`
- `shininess`: `76-128`
- `diffuseSteps`: `3-4`
- `diffuseSoftness`: `0.04-0.10`
- `shadowSoftness`: `0.06-0.12`
- `highlightStrength`: `0.65-1.10`
- `rimStrength`: `0.30-0.70`
- `outline.widthPixels`: `0.8-1.8`
- `outline.opacity`: `0.45-0.75`

Avoid:

- Thick outline plus mirror-like highlight; it usually reads as cheap plastic.

Ask if vague:

- "你要偏商业棚拍的亮面手办，还是偏游戏内角色的柔和卡通？"

### Painterly / Hand-Painted

Intent: soft bands, warmer/cooler hue variation, lower line dominance, less mechanical specular.

Recommended:

- `hardSpecular`: `false`
- `diffuseSteps`: `4-6`
- `diffuseSoftness`: `0.10-0.20`
- `shadowSoftness`: `0.14-0.24`
- `shadowTint`: colored but desaturated.
- `highlightStrength`: `0.20-0.50`
- `highlightSoftness`: `0.10-0.18`
- `rimStrength`: `0.10-0.35`
- `outline.widthPixels`: `0.5-1.4`
- `outline.opacity`: `0.25-0.55`
- `ambientStrength`: `0.08-0.18`

Avoid:

- High outline opacity and hard high specular; those make the result look technical instead of painted.

Ask if vague:

- "你想要厚涂感的软边体积，还是保留清楚的动画阴影分层？"

### Illustrative Readability / TF2-like

Intent: clear game-readable shape with broad rim highlights, hue/luminance variation, and strong silhouette. This is not anime; it is graphic readability.

Recommended:

- `toon.enabled`: optional, often `false` or mild.
- `diffuseStrength`: `1.05-1.25`
- `ambientStrength`: `0.10-0.22`
- `secondaryLightScale`: `0.22-0.45`
- `specularStrength`: `0.35-0.75`
- `smoothness`: `0.65-0.85`
- `shininess`: `36-76`
- `rimStrength`: `0.35-0.85`
- `rimPower`: `1.5-3.5`
- `outline.widthPixels`: `0.0-1.2`; use rim more than black outline.

Avoid:

- Muddy shadows and low rim strength; shape readability is the point.

Ask if vague:

- "你更想靠 rim light 读轮廓，还是靠黑色描边读轮廓？"

### Gooch / Technical Illustration

Intent: cool-to-warm midtone shading that preserves edge lines and highlights.

Recommended:

- `toon.enabled`: `true`
- `diffuseSteps`: `4-6`
- `diffuseSoftness`: `0.08-0.18`
- `shadowTint`: cool blue `{0.45, 0.58, 0.95}`
- `highlightTint`: warm cream `{1.0, 0.88, 0.62}`
- `ambientStrength`: `0.12-0.24`
- `specularStrength`: `0.18-0.45`
- `outline.widthPixels`: `1.0-2.5`
- `outline.opacity`: `0.65-0.90`

Avoid:

- Fully black shadows; the style lives in midtones.

Ask if vague:

- "你是要技术插画那种冷暖体积，还是普通卡通角色的阴影？"

### Clay / Matte Review

Intent: neutral material review without noisy gloss; useful for checking form.

Recommended:

- `toon.enabled`: `false`
- `exposure`: `1.0-1.18`
- `ambientStrength`: `0.12-0.25`
- `secondaryLightScale`: `0.20-0.45`
- `specularStrength`: `0.05-0.18`
- `smoothness`: `0.20-0.45`
- `shininess`: `12-36`
- `rimStrength`: `0.08-0.22`
- `outline.enabled`: `false` or very subtle.

Avoid:

- Metallic/gloss cues; this is for form review.

### Dark Premium Product

Intent: dramatic product/hero shot with strong separation.

Recommended:

- `toon.enabled`: usually `false` unless stylized product.
- `exposure`: `0.85-1.10`
- `ambientStrength`: `0.00-0.06`
- `secondaryLightScale`: `0.08-0.20`
- `specularStrength`: `0.60-1.00`
- `smoothness`: `0.80-0.98`
- `shininess`: `80-128`
- `rimStrength`: `0.50-1.10`
- `rimPower`: `1.2-2.6`
- `outline.enabled`: usually `false`

Avoid:

- High ambient fill, which destroys premium contrast.

## Refinement Ladder

Use these when the user critiques the result:

- "太硬": lower `hardSpecular`; raise `diffuseSoftness`, `shadowSoftness`, `highlightSoftness`; lower outline opacity.
- "太糊": lower softness; reduce `ambientStrength`; increase `shadowThreshold` contrast; slightly increase outline opacity.
- "太黑": raise `exposure`, `ambientStrength`, `secondaryLightScale`; reduce `shadowThreshold`; lighten `shadowTint`.
- "太灰": raise `diffuseStrength`; make `shadowTint` more chromatic; reduce over-strong ambient.
- "太塑料": lower `specularStrength`; lower `smoothness`; reduce `highlightStrength`; warm/desaturate highlight tint.
- "不够二次元": enable Toon; use 3-step bands; reduce shadow softness; add controlled outline.
- "不够高级": reduce outline; lower ambient; use cleaner specular and stronger rim separation.
- "脸太脏": reduce shadow receive visually by lowering `shadowThreshold`/shadow darkness; use warm soft shadow tint; increase fill.
- "轮廓不清楚": raise `outline.widthPixels`/opacity or increase `rimStrength`, but avoid increasing both aggressively.
- "高光太大": raise `highlightThreshold`, lower `highlightStrength`, increase `shininess` slightly.
- "高光太碎": lower `normalStrength` or `specularMapWeight`; soften highlight.

## Agent Loop Policy

The agent should not ask endless questions. For each turn:

1. If the user gives a clear style name plus a modifier, generate one preset.
2. If only a vague adjective is given, ask exactly one question about the most decisive visual axis.
3. If the user gives critique after applying a preset, refine the current state instead of starting over.
4. The next question should target one of these axes:
   - hard cel vs soft anime bands
   - line/outline strength
   - highlight size and material feel
   - cool vs warm shadow hue
   - product/figure vs game-character read
   - clean face/skin priority vs full-body dramatic lighting

## Anti-Patterns

- Do not push every slider upward. Strong rim + strong outline + strong specular usually looks noisy.
- Do not use pure black shadows for soft anime, Genshin-like, or painterly requests.
- Do not generate PBR/GI-heavy presets for anime/toon prompts unless explicitly requested.
- Do not describe limitations without giving a usable parameter set.
- Do not claim true glass/transmission if the current renderer lacks transmission/refraction; make a glossy/rim approximation and state it in `assistantReply`.
