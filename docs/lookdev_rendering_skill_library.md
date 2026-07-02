# HaoRender-GI Rendering Skill Library

This library turns mature rendering workflows into compact skills for the LookDev AI agent.

The agent should not invent a look from adjectives alone. It should choose one dominant skill, apply one or two modifiers, then map the result to HaoRender-GI's actual controls.

## Agent Decision Loop

1. Classify the target family: PBR product, soft game anime, hard cel, figure showcase, painterly, technical illustration, or readability-first stylization.
2. Pick exactly one dominant rendering skill.
3. Apply at most two modifiers: softer/harder, warmer/cooler, more/less outline, larger/smaller highlight, more/less rim.
4. Respect renderer limits. If a requested look needs edited normals, material roughness, matcap maps, SSS, transmission, or face-specific shadow masks that HaoRender-GI does not expose, approximate with available controls and say so in the assistant reply.
5. Avoid the "ugly stack": high rim + high outline + high specular + high ambient at the same time.

## Skill: UTS / MToon Soft Game Anime

Use for: "原神", "二游", "柔和", "温和", "日系角色", "轻卡通", "soft anime".

Mature reference idea:

- Unity Toon Shader separates base/shade colors, shading steps, feather, highlights, rim light, matcap, scene-light influence, and outline.
- MToon frames anime shading around lit color, shade color, toony thresholding, rim add, and outline mode.

HaoRender mapping:

- `shadingModel`: Phong
- `toon.enabled`: true
- `diffuseSteps`: 3-4
- `diffuseSoftness`: 0.07-0.13
- `shadowFloor`: 0.08-0.18
- `litFloor`: 0.42-0.58
- `rampBias`: -0.03-0.06
- `rampContrast`: 0.75-1.10
- `shadowMapStrength`: 0.20-0.50
- `shadowThreshold`: 0.40-0.50
- `shadowSoftness`: 0.09-0.16
- `shadowTint`: cool violet-blue or warm peach, never black
- `highlightThreshold`: 0.32-0.45
- `highlightSoftness`: 0.07-0.12
- `highlightStrength`: 0.45-0.75
- `specularStrength`: 0.22-0.45
- `smoothness`: 0.70-0.84
- `rimStrength`: 0.22-0.48
- `outline.widthPixels`: 1.0-2.0
- `outline.opacity`: 0.50-0.75
- `normalStrength`: 0.85-1.05
- `materialOverrideEnabled`: true when source textures are too dark or too noisy.
- `materialTextureStrength`: 0.70-0.92
- `materialLift`: 0.03-0.10
- `materialSaturation`: 0.90-1.15
- `materialContrast`: 0.75-1.05

Avoid:

- Hard black outlines, full-strength rim, mirror-like specular, high normal strength on faces.

Good follow-up:

- "你想更像游戏内柔和角色，还是更像手办宣传图那种更亮更干净的高光？"

## Skill: ArcSys Hard 2D Cel Approximation

Use for: "罪恶装备", "龙珠斗士Z", "硬赛璐璐", "2D感强", "动画截图", "sharp cel".

Mature reference idea:

- Arc System Works' Guilty Gear Xrd pipeline pursued a 2D fighting-game look inside a 3D framework. A large part of the look comes from art-directed modeling, normals, animation, and camera choices, not only shader sliders.

HaoRender mapping:

- `shadingModel`: Phong
- `toon.enabled`: true
- `hardSpecular`: true
- `primaryLightOnly`: true or very low fill
- `diffuseSteps`: 2-3
- `diffuseSoftness`: 0.00-0.04
- `shadowThreshold`: 0.46-0.58
- `shadowSoftness`: 0.00-0.05
- `highlightSoftness`: 0.00-0.04
- `rimStrength`: 0.18-0.42
- `outline.widthPixels`: 2.2-4.2
- `outline.opacity`: 0.78-1.00
- `ambientStrength`: 0.02-0.08

Avoid:

- Soft studio fill, high ambient, low-contrast gray shadows.

Limit note:

- HaoRender cannot yet edit normals or per-character lighting masks, so this is a shader-side approximation.

## Skill: Figure Showcase

Use for: "手办", "宣传图", "展示", "PVC", "更高级但还是二次元".

HaoRender mapping:

- `shadingModel`: Phong
- `toon.enabled`: true
- `diffuseSteps`: 3-4
- `diffuseSoftness`: 0.04-0.10
- `shadowSoftness`: 0.06-0.12
- `specularStrength`: 0.45-0.80
- `smoothness`: 0.80-0.94
- `shininess`: 72-120
- `highlightStrength`: 0.65-1.00
- `rimStrength`: 0.35-0.70
- `outline.widthPixels`: 0.8-1.6
- `outline.opacity`: 0.42-0.68
- `secondaryLightScale`: 0.24-0.42

Avoid:

- Thick outline plus mirror highlights. It usually reads as cheap plastic.

## Skill: Painterly / Hand-Painted

Use for: "厚涂", "手绘", "柔软体积", "插画感", "不那么技术".

HaoRender mapping:

- `toon.enabled`: true
- `diffuseSteps`: 4-6
- `diffuseSoftness`: 0.12-0.22
- `shadowSoftness`: 0.14-0.24
- `shadowTint`: desaturated colored shadow
- `highlightStrength`: 0.18-0.45
- `highlightSoftness`: 0.10-0.18
- `specularStrength`: 0.12-0.32
- `smoothness`: 0.35-0.65
- `rimStrength`: 0.10-0.32
- `outline.widthPixels`: 0.4-1.2
- `outline.opacity`: 0.20-0.48
- `ambientStrength`: 0.10-0.20

Avoid:

- Hard specular, high outline opacity, high normal strength.

## Skill: TF2 / Readability-First Stylization

Use for: "可读性", "轮廓清楚", "游戏角色清晰", "TF2", "商业插画".

Mature reference idea:

- Team Fortress 2 uses art and shading choices to convey shape quickly through rim highlights, luminance variation, and hue variation.

HaoRender mapping:

- `toon.enabled`: false or mild
- `ambientStrength`: 0.10-0.22
- `secondaryLightScale`: 0.20-0.45
- `specularStrength`: 0.32-0.65
- `smoothness`: 0.60-0.82
- `rimStrength`: 0.40-0.85
- `rimPower`: 1.4-3.2
- `outline.widthPixels`: 0.0-1.2

Avoid:

- Heavy black outlines when rim and value contrast can solve readability.

## Skill: Gooch Technical Illustration

Use for: "技术插画", "冷暖体积", "说明书", "结构清晰".

Mature reference idea:

- Gooch shading uses hue and luminance changes in midtones while preserving edge lines and highlights.

HaoRender mapping:

- `toon.enabled`: true
- `diffuseSteps`: 4-6
- `diffuseSoftness`: 0.08-0.18
- `shadowTint`: cool blue
- `highlightTint`: warm cream/yellow
- `ambientStrength`: 0.12-0.24
- `specularStrength`: 0.18-0.45
- `outline.widthPixels`: 1.0-2.4
- `outline.opacity`: 0.60-0.88

Avoid:

- Pure black shadows or photorealistic high-contrast lighting.

## Skill: PBR Product / Material Guardrails

Use for: "PBR", "写实", "材质检查", "产品", "金属", "粗糙度", "贴图".

Mature reference idea:

- Filament and Unreal-style PBR separate material identity into base color, metallic, roughness, reflectance, normal, AO, and emissive. Metallic is usually binary. Roughness controls reflection sharpness.

HaoRender mapping:

- Stay in `shadingModel`: PBR unless the user explicitly asks for Toon/Phong/anime/cel.
- Tune only exposed controls: exposure, normalStrength, IBL diffuse/specular, sky light, shadows, backface culling, channel indices.
- Do not claim to edit material roughness/base color/metallic factors unless those controls are added later.

Profiles:

- Neutral material check: exposure 1.00-1.18, normal 0.90-1.10, IBL diffuse 0.50-0.85, IBL specular 0.65-1.05, sky 0.12-0.32.
- Soft studio product: exposure 1.08-1.28, IBL diffuse 0.75-1.20, IBL specular 0.55-0.90, sky 0.25-0.55.
- Dark premium product: exposure 0.85-1.08, low diffuse/sky, stronger specular IBL, outline off.

## Sources

- Unity Toon Shader overview and parameter groups: https://docs.unity3d.com/ja/Packages/com.unity.toonshader%400.8/manual/index.html
- Unity Toon Shader shading steps and feather: https://docs.unity3d.com/ja/Packages/com.unity.toonshader%400.8/manual/ShadingStepAndFeather.html
- Unity Toon Shader highlight, rim, outline docs:
  - https://docs.unity3d.com/ja/Packages/com.unity.toonshader%400.9/manual/Highlight.html
  - https://docs.unity3d.com/ja/Packages/com.unity.toonshader%400.9/manual/Rimlight.html
  - https://docs.unity3d.com/ja/Packages/com.unity.toonshader%400.9/manual/Outline.html
- VRM MToon shader documentation: https://vrm.dev/en/univrm/shaders/shader_mtoon/
- Arc System Works Guilty Gear Xrd GDC talk/handout: https://www.arcsystemworks.com/guilty-gear-xrds-art-style-the-x-factor-between-2d-and-3d-talk-from-gdc-2015-is-now-available-online/
- Valve Team Fortress 2 illustrative rendering paper: https://steamcdn-a.akamaihd.net/apps/valve/2007/NPAR07_IllustrativeRenderingInTeamFortress2.pdf
- Gooch technical illustration paper: https://users.cs.northwestern.edu/~ago820/SIG98/abstract.html
- Filament material guide: https://google.github.io/filament/main/materials.html
