#include "rendering/opengl_rasterizer.h"

#include "core/math_utils.h"

#include <QOpenGLContext>
#include <QOpenGLShaderProgram>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <functional>
#include <numeric>

namespace haorendergi {
namespace {

constexpr int kMtoonShadeTextureUnit = 8;
constexpr int kMtoonMatcapTextureUnit = 9;
constexpr int kShadowTextureUnit = 10;

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsAnyToken(const std::string& value, std::initializer_list<const char*> tokens) {
    for (const char* token : tokens) {
        if (value.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const char* shadowVertexShaderSource() {
    return R"(#version 330 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uLightMatrix;
void main() {
    gl_Position = uLightMatrix * uModel * vec4(aPosition, 1.0);
}
)";
}

const char* shadowFragmentShaderSource() {
    return R"(#version 330 core
void main() {
}
)";
}

const char* mainVertexShaderSource() {
    return R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aTangent;
layout(location = 3) in vec3 aBitangent;
layout(location = 4) in vec2 aUv;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightMatrix;
uniform mat3 uNormalMatrix;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec3 vWorldTangent;
out vec3 vWorldBitangent;
out vec2 vUv;
out vec4 vShadowPosition;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize(uNormalMatrix * aNormal);
    vWorldTangent = normalize(uNormalMatrix * aTangent);
    vWorldBitangent = normalize(uNormalMatrix * aBitangent);
    vUv = aUv;
    vShadowPosition = uLightMatrix * worldPosition;
    gl_Position = uProjection * uView * worldPosition;
}
)";
}

std::string mainFragmentShaderSource() {
    return std::string(R"(#version 330 core
const float PI = 3.14159265359;

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec3 vWorldTangent;
in vec3 vWorldBitangent;
in vec2 vUv;
in vec4 vShadowPosition;

out vec4 fragColor;

uniform vec3 uCameraPosition;
uniform mat4 uView;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uBaseColorFactor;
uniform vec3 uEmissiveFactor;
uniform vec3 uMtoonShadeColorFactor;
uniform vec3 uMtoonRimColorFactor;
uniform vec3 uPhongAmbientColor;
uniform vec3 uPhongSpecularTint;
uniform vec3 uPhongRimTint;
uniform vec3 uPhongToonShadowTint;
uniform vec3 uPhongToonHighlightTint;
uniform float uMetallicFactor;
uniform float uRoughnessFactor;
uniform float uAoFactor;
uniform float uBaseAlphaFactor;
uniform float uAlphaCutoff;
uniform float uMtoonShadingShift;
uniform float uMtoonShadingToony;
uniform float uMtoonRimLift;
uniform float uMtoonRimFresnelPower;
uniform int uPbrIblEnabled;
uniform int uAlphaMode;
uniform int uMaterialUnlit;
uniform int uMaterialMtoon;
uniform int uHasBaseColorTexture;
uniform int uHasDiffuseTexture;
uniform int uHasNormalTexture;
uniform int uHasSpecularTexture;
uniform int uHasMetallicTexture;
uniform int uHasRoughnessTexture;
uniform int uHasAoTexture;
uniform int uHasEmissiveTexture;
uniform int uHasMtoonShadeTexture;
uniform int uHasMtoonMatcapTexture;
uniform int uMetallicChannel;
uniform int uRoughnessChannel;
uniform int uAoChannel;
uniform int uEmissiveChannel;
uniform float uExposure;
uniform float uNormalStrength;
uniform float uIblDiffuseStrength;
uniform float uIblSpecularStrength;
uniform float uSkyLightStrength;
uniform float uPhongDiffuseStrength;
uniform float uPhongAmbientStrength;
uniform float uPhongSpecularStrength;
uniform float uPhongSecondaryLightScale;
uniform float uPhongSmoothness;
uniform float uPhongSpecularMapWeight;
uniform float uPhongShininess;
uniform float uPhongRimStrength;
uniform float uPhongRimPower;
uniform float uPhongToonDiffuseSteps;
uniform float uPhongToonDiffuseSoftness;
uniform float uPhongToonShadowFloor;
uniform float uPhongToonLitFloor;
uniform float uPhongToonRampBias;
uniform float uPhongToonRampContrast;
uniform float uPhongToonShadowMapStrength;
uniform float uPhongToonShadowThreshold;
uniform float uPhongToonShadowSoftness;
uniform float uPhongToonHighlightThreshold;
uniform float uPhongToonHighlightSoftness;
uniform float uPhongToonHighlightStrength;
uniform float uPhongToonRimThreshold;
uniform float uPhongToonRimSoftness;
uniform float uPhongToonMaterialTextureStrength;
uniform float uPhongToonMaterialLift;
uniform float uPhongToonMaterialSaturation;
uniform float uPhongToonMaterialContrast;
uniform int uEnableShadows;
uniform int uShadingModel;
uniform int uPhongHardSpecular;
uniform int uPhongToonEnabled;
uniform int uPhongUseTonemap;
uniform int uPhongPrimaryLightOnly;
uniform int uPhongToonMaterialOverrideEnabled;
uniform sampler2D uBaseColorTexture;
uniform sampler2D uDiffuseTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uSpecularTexture;
uniform sampler2D uMetallicTexture;
uniform sampler2D uRoughnessTexture;
uniform sampler2D uAoTexture;
uniform sampler2D uEmissiveTexture;
uniform sampler2D uMtoonShadeTexture;
uniform sampler2D uMtoonMatcapTexture;
uniform sampler2D uShadowMap;

float channelValue(vec4 value, int channelIndex) {
    if (channelIndex == 1) {
        return value.g;
    }
    if (channelIndex == 2) {
        return value.b;
    }
    if (channelIndex == 3) {
        return value.a;
    }
    return value.r;
}

vec3 srgbToLinear(vec3 value) {
    return pow(value, vec3(2.2));
}

vec3 linearToSrgb(vec3 value) {
    return pow(max(value, vec3(0.0)), vec3(1.0 / 2.2));
}

)") + R"(
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = max(dot(N, H), 0.0);
    float ndoth2 = ndoth * ndoth;
    float denom = ndoth2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-5);
}

float geometrySchlickGGX(float ndotv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 1e-5);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float ggx1 = geometrySchlickGGX(max(dot(N, V), 0.0), roughness);
    float ggx2 = geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cos_theta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

float shadowVisibility(vec3 surface_normal, vec3 light_direction) {
    if (uEnableShadows == 0) {
        return 1.0;
    }
    vec3 projection = vShadowPosition.xyz / max(vShadowPosition.w, 1e-5);
    projection = projection * 0.5 + 0.5;
    if (projection.x <= 0.0 || projection.x >= 1.0 || projection.y <= 0.0 || projection.y >= 1.0 || projection.z <= 0.0 || projection.z >= 1.0) {
        return 1.0;
    }
    float bias = max(0.0006 * (1.0 - dot(surface_normal, normalize(-light_direction))), 0.00012);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float visible = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float sampleDepth = texture(uShadowMap, projection.xy + vec2(x, y) * texel).r;
            visible += (projection.z - bias) <= sampleDepth ? 1.0 : 0.0;
        }
    }
    return visible / 9.0;
}

vec3 resolveNormal() {
    vec3 N = normalize(vWorldNormal);
    if (uHasNormalTexture == 0) {
        return N;
    }
    vec3 T = vWorldTangent;
    if (dot(T, T) < 1e-5) {
        vec3 helper = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T = normalize(cross(helper, N));
    } else {
        T = normalize(T - N * dot(N, T));
    }
    vec3 B = vWorldBitangent;
    if (dot(B, B) < 1e-5) {
        B = normalize(cross(N, T));
    } else {
        B = normalize(B);
    }
    mat3 TBN = mat3(T, B, N);
    vec3 mapNormal = texture(uNormalTexture, vUv).xyz * 2.0 - 1.0;
    mapNormal.xy *= max(uNormalStrength, 0.0);
    return normalize(TBN * normalize(mapNormal));
}

float resolveSpecularMask() {
    if (uHasSpecularTexture == 0) {
        return 1.0;
    }
    vec3 specularSample = texture(uSpecularTexture, vUv).rgb;
    float sampledMask = clamp(dot(specularSample, vec3(0.3333333)), 0.0, 1.0);
    float weight = clamp(uPhongSpecularMapWeight, 0.0, 2.0);
    float blendedMask = mix(1.0, sampledMask, clamp(weight, 0.0, 1.0));
    if (weight > 1.0) {
        blendedMask *= weight;
    }
    return clamp(blendedMask, 0.0, 1.0);
}

float toonBandValue(float value, float bands, float softness) {
    float safeBands = max(bands, 2.0);
    float clampedSoftness = clamp(softness, 0.0, 0.49);
    float scaled = clamp(value, 0.0, 1.0) * safeBands;
    float bandIndex = floor(scaled);
    float blend = smoothstep(0.5 - clampedSoftness, 0.5 + clampedSoftness, fract(scaled));
    return clamp((bandIndex + blend) / max(safeBands - 1.0, 1.0), 0.0, 1.0);
}

float toonThreshold(float value, float threshold, float softness) {
    float safeSoftness = max(softness, 1e-4);
    return smoothstep(threshold - safeSoftness, threshold + safeSoftness, clamp(value, 0.0, 1.0));
}

vec3 applyToonMaterialOverride(vec3 baseColor) {
    if (uPhongToonMaterialOverrideEnabled == 0) {
        return baseColor;
    }
    float luma = dot(baseColor, vec3(0.2126, 0.7152, 0.0722));
    vec3 color = mix(vec3(luma), baseColor, clamp(uPhongToonMaterialSaturation, 0.0, 2.0));
    color = (color - vec3(0.5)) * clamp(uPhongToonMaterialContrast, 0.25, 2.5) + vec3(0.5);
    color = mix(vec3(luma), color, clamp(uPhongToonMaterialTextureStrength, 0.0, 1.0));
    color = max(color, vec3(clamp(uPhongToonMaterialLift, 0.0, 0.8)));
    return clamp(color, 0.0, 4.0);
}

float applyToonRamp(float ndotl) {
    float ramp = (clamp(ndotl, 0.0, 1.0) - 0.5) * clamp(uPhongToonRampContrast, 0.25, 2.5) + 0.5 + uPhongToonRampBias;
    return clamp(ramp, 0.0, 1.0);
}

vec3 buildAmbient(vec3 baseColor, float ao, float metallic, float roughness, vec3 ambientColor, float baseAmbientStrength) {
    vec3 ambient = baseColor * srgbToLinear(ambientColor) * ao * max(baseAmbientStrength, 0.0);
    if (uShadingModel == 1 && uPhongToonEnabled != 0) {
        ambient += srgbToLinear(ambientColor) * ao * max(baseAmbientStrength, 0.0) * 0.18;
    }
    if (uPbrIblEnabled != 0) {
        ambient += baseColor * ao * uIblDiffuseStrength * vec3(0.12, 0.14, 0.16);
        ambient += mix(vec3(0.04), baseColor, metallic) * uIblSpecularStrength * (1.0 - roughness) * 0.08;
    }
    ambient += baseColor * ao * uSkyLightStrength * vec3(0.18, 0.21, 0.24);
    return ambient;
}

)" + R"(
void accumulatePhongLight(vec3 baseColor,
                          vec3 N,
                          vec3 V,
                          vec3 L,
                          vec3 lightColor,
                          float shadow,
                          float attenuation,
                          float specularMask,
                          inout vec3 direct) {
    float ndotl = max(dot(N, L), 0.0);
    if (ndotl <= 0.0) {
        return;
    }

    bool toonEnabled = uPhongToonEnabled != 0;
    float diffuseTerm = ndotl;
    vec3 R = reflect(-L, N);
    float specPower = mix(4.0, max(uPhongShininess, 4.0), clamp(uPhongSmoothness, 0.0, 1.0));
    if (uPhongHardSpecular != 0) {
        specPower = max(specPower * 2.0, 72.0);
    }
    float rawSpecular = pow(max(dot(V, R), 0.0), specPower) * specularMask;
    float specular = rawSpecular * uPhongSpecularStrength;
    if (uPhongHardSpecular != 0) {
        rawSpecular = rawSpecular > 0.35 ? 1.0 : 0.0;
        specular = rawSpecular * uPhongSpecularStrength;
    }
    vec3 diffuseColor;
    vec3 specularColor;
    if (toonEnabled) {
        float toonNdotL = applyToonRamp(ndotl);
        float bandedDiffuse = toonBandValue(toonNdotL, uPhongToonDiffuseSteps, uPhongToonDiffuseSoftness);
        float litMask = toonThreshold(toonNdotL, uPhongToonShadowThreshold, uPhongToonShadowSoftness);
        vec3 toonBase = applyToonMaterialOverride(baseColor);
        float liftAmount = clamp(uPhongAmbientStrength * 1.4, 0.0, 0.28);
        toonBase = mix(toonBase, max(toonBase, vec3(0.02)), liftAmount);
        vec3 shadowFloorColor = srgbToLinear(uPhongToonShadowTint) * clamp(uPhongToonShadowFloor, 0.0, 0.8);
        vec3 shadowColor = max(toonBase * srgbToLinear(uPhongToonShadowTint), shadowFloorColor);
        vec3 litColor = toonBase * mix(clamp(uPhongToonLitFloor, 0.0, 1.0), 1.0, bandedDiffuse);
        diffuseColor = mix(shadowColor, litColor, litMask) * uPhongDiffuseStrength;

        float highlightMask = toonThreshold(rawSpecular, uPhongToonHighlightThreshold, uPhongToonHighlightSoftness);
        float highlightEnergy = uPhongToonHighlightStrength * clamp(0.45 + 0.55 * uPhongSpecularStrength, 0.0, 1.6);
        specularColor = srgbToLinear(uPhongToonHighlightTint) * highlightMask * highlightEnergy;
    } else {
        diffuseColor = baseColor * diffuseTerm * uPhongDiffuseStrength;
        specularColor = srgbToLinear(uPhongSpecularTint) * specular;
    }
    float shadowFactor = toonEnabled ? mix(1.0, shadow, clamp(uPhongToonShadowMapStrength, 0.0, 1.0)) : shadow;
    direct += (diffuseColor + specularColor) * lightColor * attenuation * shadowFactor;
}

vec3 buildRimLight(vec3 N, vec3 V, vec3 L, float shadow) {
    if (uPhongRimStrength <= 1e-4) {
        return vec3(0.0);
    }
    float rimFactor = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), max(uPhongRimPower, 0.25));
    if (uPhongToonEnabled != 0) {
        rimFactor = toonThreshold(rimFactor, uPhongToonRimThreshold, uPhongToonRimSoftness);
    }
    float lightWrap = clamp(0.35 + 0.65 * max(dot(N, L), 0.0), 0.0, 1.0);
    float shadowFactor = uPhongToonEnabled != 0 ? mix(1.0, shadow, clamp(uPhongToonShadowMapStrength, 0.0, 1.0) * 0.75) : shadow;
    return srgbToLinear(uPhongRimTint) * uSunColor * rimFactor * lightWrap * shadowFactor * uPhongRimStrength;
}

)" + R"(
vec3 buildMtoonColor(vec3 baseColor, vec3 emissive, vec3 N, vec3 V, vec3 L, float shadow) {
    vec3 shadeColor = baseColor * srgbToLinear(uMtoonShadeColorFactor);
    if (uHasMtoonShadeTexture != 0) {
        shadeColor *= srgbToLinear(texture(uMtoonShadeTexture, vUv).rgb);
    }

    float halfLambert = clamp(dot(N, L) * 0.5 + 0.5, 0.0, 1.0);
    float shifted = clamp(halfLambert + uMtoonShadingShift, 0.0, 1.0);
    float toony = clamp(uMtoonShadingToony, 0.0, 1.0);
    float threshold = 1.0 - toony;
    float lit = smoothstep(threshold - 0.08, threshold + 0.08, shifted);
    lit *= mix(1.0, shadow, 0.45);

    vec3 color = mix(shadeColor, baseColor, clamp(lit, 0.0, 1.0));

    if (uHasMtoonMatcapTexture != 0) {
        vec3 viewNormal = normalize((uView * vec4(N, 0.0)).xyz);
        vec2 matcapUv = clamp(viewNormal.xy * 0.5 + 0.5, 0.0, 1.0);
        vec3 matcap = srgbToLinear(texture(uMtoonMatcapTexture, matcapUv).rgb);
        color += baseColor * matcap * 0.18;
    }

    vec3 rimColor = srgbToLinear(uMtoonRimColorFactor);
    if (dot(rimColor, rimColor) > 1e-6) {
        float rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), max(uMtoonRimFresnelPower, 0.01));
        rim = clamp(rim + uMtoonRimLift, 0.0, 1.0);
        color += rimColor * rim;
    }
    return color + emissive;
}

void main() {
    vec3 baseColor = uBaseColorFactor;
    float alpha = clamp(uBaseAlphaFactor, 0.0, 1.0);
    if (uHasBaseColorTexture != 0) {
        vec4 baseSample = texture(uBaseColorTexture, vUv);
        baseColor *= srgbToLinear(baseSample.rgb);
        alpha *= baseSample.a;
    } else if (uHasDiffuseTexture != 0) {
        vec4 diffuseSample = texture(uDiffuseTexture, vUv);
        baseColor *= srgbToLinear(diffuseSample.rgb);
        alpha *= diffuseSample.a;
    }
    if (uAlphaMode == 1 && alpha < uAlphaCutoff) {
        discard;
    }
    if (uAlphaMode == 2 && alpha < 0.02) {
        discard;
    }

    vec3 emissive = uEmissiveFactor;
    if (uHasEmissiveTexture != 0) {
        vec4 emissiveSample = texture(uEmissiveTexture, vUv);
        emissive *= srgbToLinear(emissiveSample.rgb) * channelValue(emissiveSample, uEmissiveChannel);
    }

    float metallic = clamp(uMetallicFactor, 0.0, 1.0);
    if (uHasMetallicTexture != 0) {
        metallic *= channelValue(texture(uMetallicTexture, vUv), uMetallicChannel);
    }
    float roughness = clamp(uRoughnessFactor, 0.04, 1.0);
    if (uHasRoughnessTexture != 0) {
        roughness *= channelValue(texture(uRoughnessTexture, vUv), uRoughnessChannel);
    }
    roughness = clamp(roughness, 0.04, 1.0);
    float ao = clamp(uAoFactor, 0.0, 1.0);
    if (uHasAoTexture != 0) {
        ao *= channelValue(texture(uAoTexture, vUv), uAoChannel);
    }

    vec3 N = resolveNormal();
    vec3 V = normalize(uCameraPosition - vWorldPosition);
    vec3 L = normalize(-uSunDirection);
    vec3 H = normalize(V + L);
    float visibility = shadowVisibility(N, uSunDirection);
    float ndotl = max(dot(N, L), 0.0);
    float ndotv = max(dot(N, V), 0.0);
    float specularMask = resolveSpecularMask();

    vec3 color = vec3(0.0);
    if (uMaterialMtoon != 0) {
        color = buildMtoonColor(baseColor, emissive, N, V, L, visibility);
    } else if (uMaterialUnlit != 0) {
        color = baseColor + emissive;
    } else if (uShadingModel == 1) {
        color = buildAmbient(baseColor, ao, metallic, roughness, uPhongAmbientColor, uPhongAmbientStrength);
        vec3 direct = vec3(0.0);
        accumulatePhongLight(baseColor, N, V, L, uSunColor, visibility, 1.0, specularMask, direct);
        if (uPhongPrimaryLightOnly == 0 && uPhongSecondaryLightScale > 0.0) {
            vec3 fillL = normalize(vec3(0.45, 0.38, 0.80));
            vec3 fillColor = uSunColor * vec3(0.44, 0.48, 0.60);
            accumulatePhongLight(baseColor, N, V, fillL, fillColor, 1.0, uPhongSecondaryLightScale, specularMask, direct);
        }
        color += direct;
        color += buildRimLight(N, V, L, visibility);
    } else {
        color = buildAmbient(baseColor, ao, metallic, roughness, vec3(1.0), 0.015);
        vec3 f0 = mix(vec3(0.04), baseColor, metallic);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), f0);
        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3 numerator = NDF * G * F;
        float denominator = max(4.0 * ndotv * ndotl, 1e-4);
        vec3 specular = numerator / denominator;
        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        color += (kD * baseColor / PI + specular) * uSunColor * ndotl * visibility;
    }

    if (uMaterialUnlit == 0 && uMaterialMtoon == 0) {
        color += emissive;
    }
    color *= max(uExposure, 0.0);
    if (uMaterialUnlit == 0 && uMaterialMtoon == 0 && (uShadingModel == 0 || uPhongUseTonemap != 0)) {
        color = color / (color + vec3(1.0));
    }
    fragColor = vec4(linearToSrgb(color), alpha);
}
)";
}

const char* outlineVertexShaderSource() {
    return R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
uniform vec2 uViewportSize;
uniform float uOutlineWidthPixels;
uniform float uOutlineDepthBias;

void main() {
    vec3 worldPosition = (uModel * vec4(aPosition, 1.0)).xyz;
    vec4 clipPosition = uProjection * uView * vec4(worldPosition, 1.0);

    vec3 worldNormal = normalize(uNormalMatrix * aNormal);
    vec4 clipNormalEnd = uProjection * uView * vec4(worldPosition + worldNormal, 1.0);

    vec2 ndcPosition = clipPosition.xy / max(abs(clipPosition.w), 1e-5);
    vec2 ndcNormalEnd = clipNormalEnd.xy / max(abs(clipNormalEnd.w), 1e-5);
    vec2 direction = ndcNormalEnd - ndcPosition;
    float directionLength = length(direction);
    if (directionLength < 1e-5) {
        direction = vec2(0.0, 1.0);
    } else {
        direction /= directionLength;
    }

    vec2 pixelToNdc = vec2(2.0 / max(uViewportSize.x, 1.0), 2.0 / max(uViewportSize.y, 1.0));
    gl_Position = clipPosition;
    gl_Position.xy += direction * uOutlineWidthPixels * pixelToNdc * clipPosition.w;
    gl_Position.z += uOutlineDepthBias * clipPosition.w;
}
)";
}

const char* outlineFragmentShaderSource() {
    return R"(#version 330 core
out vec4 fragColor;

uniform vec4 uOutlineColor;

void main() {
    fragColor = uOutlineColor;
}
)";
}

void setMatrix4(QOpenGLFunctions_3_3_Core* gl, QOpenGLShaderProgram* program, const char* name, const Eigen::Matrix4f& matrix) {
    gl->glUniformMatrix4fv(program->uniformLocation(name), 1, GL_FALSE, matrix.data());
}

void setMatrix3(QOpenGLFunctions_3_3_Core* gl, QOpenGLShaderProgram* program, const char* name, const Eigen::Matrix3f& matrix) {
    gl->glUniformMatrix3fv(program->uniformLocation(name), 1, GL_FALSE, matrix.data());
}

} // namespace

OpenGLRasterizer::OpenGLRasterizer() = default;

OpenGLRasterizer::~OpenGLRasterizer() = default;

QString OpenGLRasterizer::backendName() const {
    return QStringLiteral("OpenGL Rasterizer");
}

bool OpenGLRasterizer::initialize(QString* error_message) {
    if (initialized_) {
        return true;
    }
    if (!QOpenGLContext::currentContext()) {
        if (error_message) {
            *error_message = QStringLiteral("No active OpenGL context.");
        }
        return false;
    }

    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    if (!buildPrograms(error_message)) {
        shutdown();
        return false;
    }
    createFallbackTextures();
    ensureShadowResources();
    initialized_ = true;
    return true;
}

void OpenGLRasterizer::shutdown() {
    if (!QOpenGLContext::currentContext()) {
        initialized_ = false;
        return;
    }
    destroyScene();
    destroyShadowResources();
    destroyFallbackTextures();
    destroyPrograms();
    initialized_ = false;
}

void OpenGLRasterizer::resize(int framebuffer_width, int framebuffer_height) {
    framebuffer_width_ = std::max(framebuffer_width, 1);
    framebuffer_height_ = std::max(framebuffer_height, 1);
}

std::vector<OpenGLRasterizer::GlVertex> OpenGLRasterizer::buildVertexBuffer(const MeshData& mesh) const {
    std::vector<GlVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const Vertex& source : mesh.vertices) {
        GlVertex vertex {};
        vertex.position[0] = source.position.x();
        vertex.position[1] = source.position.y();
        vertex.position[2] = source.position.z();
        vertex.normal[0] = source.normal.x();
        vertex.normal[1] = source.normal.y();
        vertex.normal[2] = source.normal.z();
        vertex.tangent[0] = source.tangent.x();
        vertex.tangent[1] = source.tangent.y();
        vertex.tangent[2] = source.tangent.z();
        vertex.bitangent[0] = source.bitangent.x();
        vertex.bitangent[1] = source.bitangent.y();
        vertex.bitangent[2] = source.bitangent.z();
        vertex.uv[0] = source.uv.x();
        vertex.uv[1] = source.uv.y();
        vertices.push_back(vertex);
    }
    return vertices;
}

std::uint64_t OpenGLRasterizer::staticMeshSignature(const MeshData& mesh) const {
    auto hash_combine = [](std::uint64_t seed, std::uint64_t value) {
        return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
    };
    auto hash_string = [](const std::string& value) {
        return static_cast<std::uint64_t>(std::hash<std::string>{}(value));
    };

    std::uint64_t signature = 1469598103934665603ull;
    signature = hash_combine(signature, static_cast<std::uint64_t>(mesh.vertices.size()));
    signature = hash_combine(signature, static_cast<std::uint64_t>(mesh.indices.size()));
    signature = hash_combine(signature, hash_string(mesh.name));
    signature = hash_combine(signature, hash_string(mesh.material.name));
    signature = hash_combine(signature, hash_string(mesh.material.base_color_texture.path));
    signature = hash_combine(signature, hash_string(mesh.material.diffuse_texture.path));
    signature = hash_combine(signature, hash_string(mesh.material.normal_texture.path));
    signature = hash_combine(signature, hash_string(mesh.material.emissive_texture.path));
    if (!mesh.indices.empty()) {
        signature = hash_combine(signature, mesh.indices.front());
        signature = hash_combine(signature, mesh.indices[mesh.indices.size() / 2]);
        signature = hash_combine(signature, mesh.indices.back());
    }
    return signature;
}

bool OpenGLRasterizer::tryUpdateDynamicVertices(const SceneModel& scene) {
    if (meshes_.size() != scene.meshes.size()) {
        return false;
    }

    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        const MeshData& mesh = scene.meshes[i];
        const GpuMesh& gpu_mesh = meshes_[i];
        if (gpu_mesh.vbo == 0 ||
            gpu_mesh.vertex_count != static_cast<int>(mesh.vertices.size()) ||
            gpu_mesh.index_count != static_cast<int>(mesh.indices.size()) ||
            gpu_mesh.static_signature != staticMeshSignature(mesh)) {
            return false;
        }
    }

    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        const std::vector<GlVertex> vertices = buildVertexBuffer(scene.meshes[i]);
        glBindBuffer(GL_ARRAY_BUFFER, meshes_[i].vbo);
        glBufferSubData(GL_ARRAY_BUFFER,
                        0,
                        static_cast<GLsizeiptr>(vertices.size() * sizeof(GlVertex)),
                        vertices.data());
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void OpenGLRasterizer::uploadScene(const SceneModel& scene) {
    if (!initialized_) {
        return;
    }

    if (tryUpdateDynamicVertices(scene)) {
        return;
    }

    destroyScene();
    meshes_.reserve(scene.meshes.size());
    for (const MeshData& mesh : scene.meshes) {
        const std::vector<GlVertex> vertices = buildVertexBuffer(mesh);

        GpuMesh gpu_mesh;
        glGenVertexArrays(1, &gpu_mesh.vao);
        glGenBuffers(1, &gpu_mesh.vbo);
        glGenBuffers(1, &gpu_mesh.ebo);

        glBindVertexArray(gpu_mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gpu_mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(GlVertex)),
                     vertices.data(),
                     scene.hasAnimations() ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)), mesh.indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GlVertex), reinterpret_cast<void*>(offsetof(GlVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GlVertex), reinterpret_cast<void*>(offsetof(GlVertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(GlVertex), reinterpret_cast<void*>(offsetof(GlVertex, tangent)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(GlVertex), reinterpret_cast<void*>(offsetof(GlVertex, bitangent)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(GlVertex), reinterpret_cast<void*>(offsetof(GlVertex, uv)));
        glBindVertexArray(0);

        gpu_mesh.vertex_count = static_cast<int>(mesh.vertices.size());
        gpu_mesh.index_count = static_cast<int>(mesh.indices.size());
        gpu_mesh.static_signature = staticMeshSignature(mesh);
        gpu_mesh.material.base_color_factor = mesh.material.base_color_factor;
        gpu_mesh.material.base_alpha_factor = mesh.material.base_alpha_factor;
        gpu_mesh.material.emissive_factor = mesh.material.emissive_factor;
        gpu_mesh.material.metallic_factor = mesh.material.metallic_factor;
        gpu_mesh.material.roughness_factor = mesh.material.roughness_factor;
        gpu_mesh.material.ao_factor = mesh.material.ao_factor;
        gpu_mesh.material.alpha_mode = mesh.material.alpha_mode;
        gpu_mesh.material.alpha_cutoff = mesh.material.alpha_cutoff;
        gpu_mesh.material.render_queue_offset = mesh.material.render_queue_offset;
        gpu_mesh.material.double_sided = mesh.material.double_sided;
        gpu_mesh.material.unlit = mesh.material.unlit;
        gpu_mesh.material.mtoon = mesh.material.mtoon;
        gpu_mesh.material.transparent_with_z_write = mesh.material.transparent_with_z_write;
        gpu_mesh.material.mtoon_shade_color_factor = mesh.material.mtoon_shade_color_factor;
        gpu_mesh.material.mtoon_rim_color_factor = mesh.material.mtoon_rim_color_factor;
        gpu_mesh.material.mtoon_shading_shift = mesh.material.mtoon_shading_shift;
        gpu_mesh.material.mtoon_shading_toony = mesh.material.mtoon_shading_toony;
        gpu_mesh.material.mtoon_rim_lift = mesh.material.mtoon_rim_lift;
        gpu_mesh.material.mtoon_rim_fresnel_power = mesh.material.mtoon_rim_fresnel_power;
        const std::string material_key = lowerCopy(mesh.name + " " + mesh.material.name);
        const bool eye_highlight_overlay = containsAnyToken(material_key, { "eyehighlight" });
        gpu_mesh.material.face_overlay = mesh.material.mtoon &&
                                         mesh.material.alpha_mode == 2 &&
                                         containsAnyToken(material_key,
                                                          { "eyeiris", "eyehighlight", "eyeline", "eyelash", "brow" });
        if (eye_highlight_overlay) {
            gpu_mesh.material.base_alpha_factor *= 0.45f;
        }
        const bool generate_texture_mipmaps = !gpu_mesh.material.face_overlay;

        if (mesh.material.base_color_texture.valid()) {
            gpu_mesh.material.base_color_texture = uploadTexture(mesh.material.base_color_texture.image, generate_texture_mipmaps);
            gpu_mesh.material.has_base_color_texture = true;
        }
        if (mesh.material.diffuse_texture.valid()) {
            gpu_mesh.material.diffuse_texture = uploadTexture(mesh.material.diffuse_texture.image, generate_texture_mipmaps);
            gpu_mesh.material.has_diffuse_texture = true;
        }
        if (mesh.material.normal_texture.valid()) {
            gpu_mesh.material.normal_texture = uploadTexture(mesh.material.normal_texture.image, generate_texture_mipmaps);
            gpu_mesh.material.has_normal_texture = true;
        }
        if (mesh.material.specular_texture.valid()) {
            gpu_mesh.material.specular_texture = uploadTexture(mesh.material.specular_texture.image, generate_texture_mipmaps);
            gpu_mesh.material.has_specular_texture = true;
        }
        if (mesh.material.metallic_texture.valid()) {
            gpu_mesh.material.metallic_texture = uploadTexture(mesh.material.metallic_texture.image, generate_texture_mipmaps);
            gpu_mesh.material.has_metallic_texture = true;
        }
        if (mesh.material.roughness_texture.valid()) {
            gpu_mesh.material.roughness_texture = uploadTexture(mesh.material.roughness_texture.image, generate_texture_mipmaps);
            gpu_mesh.material.has_roughness_texture = true;
        }
        if (mesh.material.ao_texture.valid()) {
            gpu_mesh.material.ao_texture = uploadTexture(mesh.material.ao_texture.image, generate_texture_mipmaps);
            gpu_mesh.material.has_ao_texture = true;
        }
        if (mesh.material.emissive_texture.valid()) {
            gpu_mesh.material.emissive_texture = uploadTexture(mesh.material.emissive_texture.image, generate_texture_mipmaps);
            gpu_mesh.material.has_emissive_texture = true;
        }
        if (mesh.material.mtoon_shade_multiply_texture.valid()) {
            gpu_mesh.material.mtoon_shade_texture = uploadTexture(mesh.material.mtoon_shade_multiply_texture.image, generate_texture_mipmaps);
            gpu_mesh.material.has_mtoon_shade_texture = true;
        }
        if (!gpu_mesh.material.face_overlay && mesh.material.mtoon_matcap_texture.valid()) {
            gpu_mesh.material.mtoon_matcap_texture = uploadTexture(mesh.material.mtoon_matcap_texture.image, generate_texture_mipmaps);
            gpu_mesh.material.has_mtoon_matcap_texture = true;
        }
        meshes_.push_back(gpu_mesh);
    }
}

RenderStats OpenGLRasterizer::render(const FrameRenderSettings& settings) {
    RenderStats stats;
    if (!initialized_) {
        stats.note = QStringLiteral("OpenGL backend is not initialized.");
        return stats;
    }

    GLint current_draw_framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &current_draw_framebuffer);
    target_framebuffer_ = static_cast<unsigned int>(std::max(current_draw_framebuffer, 0));

    const auto frame_start = std::chrono::high_resolution_clock::now();
    const QColor clear = settings.clear_color;
    glViewport(0, 0, framebuffer_width_, framebuffer_height_);
    glClearColor(clear.redF(), clear.greenF(), clear.blueF(), clear.alphaF());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!settings.scene || settings.scene->empty() || meshes_.empty()) {
        stats.note = QStringLiteral("No scene loaded.");
        return stats;
    }

    light_matrix_ = buildLightMatrix(settings);
    if (settings.look_dev.enable_shadows) {
        renderShadowPass(settings, stats);
    }
    renderMainPass(settings, stats);
    if (settings.look_dev.shading_model == ShadingModel::Phong && settings.look_dev.phong.outline.enabled) {
        renderOutlinePass(settings);
    }

    const auto frame_end = std::chrono::high_resolution_clock::now();
    stats.frame_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(frame_end - frame_start).count();
    return stats;
}

bool OpenGLRasterizer::buildPrograms(QString* error_message) {
    destroyPrograms();

    shadow_program_ = new QOpenGLShaderProgram();
    if (!shadow_program_->addShaderFromSourceCode(QOpenGLShader::Vertex, shadowVertexShaderSource()) ||
        !shadow_program_->addShaderFromSourceCode(QOpenGLShader::Fragment, shadowFragmentShaderSource()) ||
        !shadow_program_->link()) {
        if (error_message) {
            *error_message = shadow_program_->log();
        }
        return false;
    }

    main_program_ = new QOpenGLShaderProgram();
    const std::string fragment_shader = mainFragmentShaderSource();
    if (!main_program_->addShaderFromSourceCode(QOpenGLShader::Vertex, mainVertexShaderSource()) ||
        !main_program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment_shader.c_str()) ||
        !main_program_->link()) {
        if (error_message) {
            *error_message = main_program_->log();
        }
        return false;
    }

    outline_program_ = new QOpenGLShaderProgram();
    if (!outline_program_->addShaderFromSourceCode(QOpenGLShader::Vertex, outlineVertexShaderSource()) ||
        !outline_program_->addShaderFromSourceCode(QOpenGLShader::Fragment, outlineFragmentShaderSource()) ||
        !outline_program_->link()) {
        if (error_message) {
            *error_message = outline_program_->log();
        }
        return false;
    }

    return true;
}

void OpenGLRasterizer::destroyPrograms() {
    delete main_program_;
    delete outline_program_;
    delete shadow_program_;
    main_program_ = nullptr;
    outline_program_ = nullptr;
    shadow_program_ = nullptr;
}

void OpenGLRasterizer::destroyScene() {
    for (GpuMesh& mesh : meshes_) {
        for (unsigned int* texture : { &mesh.material.base_color_texture, &mesh.material.diffuse_texture, &mesh.material.normal_texture,
                                       &mesh.material.specular_texture, &mesh.material.metallic_texture, &mesh.material.roughness_texture, &mesh.material.ao_texture,
                                       &mesh.material.emissive_texture, &mesh.material.mtoon_shade_texture, &mesh.material.mtoon_matcap_texture }) {
            if (*texture != 0) {
                glDeleteTextures(1, texture);
                *texture = 0;
            }
        }
        if (mesh.ebo != 0) {
            glDeleteBuffers(1, &mesh.ebo);
            mesh.ebo = 0;
        }
        if (mesh.vbo != 0) {
            glDeleteBuffers(1, &mesh.vbo);
            mesh.vbo = 0;
        }
        if (mesh.vao != 0) {
            glDeleteVertexArrays(1, &mesh.vao);
            mesh.vao = 0;
        }
    }
    meshes_.clear();
}

void OpenGLRasterizer::createFallbackTextures() {
    fallback_white_ = createColorTexture(255, 255, 255, 255, true);
    fallback_black_ = createColorTexture(0, 0, 0, 255, false);
    fallback_normal_ = createColorTexture(128, 128, 255, 255, false);
}

void OpenGLRasterizer::destroyFallbackTextures() {
    for (unsigned int* texture : { &fallback_white_, &fallback_black_, &fallback_normal_ }) {
        if (*texture != 0) {
            glDeleteTextures(1, texture);
            *texture = 0;
        }
    }
}

void OpenGLRasterizer::ensureShadowResources() {
    if (shadow_fbo_ != 0 && shadow_depth_texture_ != 0) {
        return;
    }

    glGenFramebuffers(1, &shadow_fbo_);
    glGenTextures(1, &shadow_depth_texture_);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadow_resolution_, shadow_resolution_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border_color[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);

    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_depth_texture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLRasterizer::destroyShadowResources() {
    if (shadow_depth_texture_ != 0) {
        glDeleteTextures(1, &shadow_depth_texture_);
        shadow_depth_texture_ = 0;
    }
    if (shadow_fbo_ != 0) {
        glDeleteFramebuffers(1, &shadow_fbo_);
        shadow_fbo_ = 0;
    }
}

unsigned int OpenGLRasterizer::uploadTexture(const QImage& image, bool generate_mipmaps) {
    if (image.isNull()) {
        return 0;
    }
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, generate_mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // The raster shader does explicit sRGB -> linear conversion for color textures.
    // Keep the GL storage linear so albedo/emissive maps are not decoded twice.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba.width(), rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
    if (generate_mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

unsigned int OpenGLRasterizer::createColorTexture(unsigned char r, unsigned char g, unsigned char b, unsigned char a, bool /*srgb*/) {
    const std::array<unsigned char, 4> pixel = { r, g, b, a };
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void OpenGLRasterizer::renderShadowPass(const FrameRenderSettings& settings, RenderStats& stats) {
    const auto pass_start = std::chrono::high_resolution_clock::now();
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glViewport(0, 0, shadow_resolution_, shadow_resolution_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_FRONT);

    shadow_program_->bind();
    setMatrix4(this, shadow_program_, "uModel", settings.model_matrix);
    setMatrix4(this, shadow_program_, "uLightMatrix", light_matrix_);
    for (const GpuMesh& mesh : meshes_) {
        if (mesh.material.alpha_mode == 2) {
            continue;
        }
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    shadow_program_->release();

    glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
    glCullFace(settings.look_dev.enable_backface_culling ? GL_BACK : GL_BACK);
    const auto pass_end = std::chrono::high_resolution_clock::now();
    stats.shadow_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(pass_end - pass_start).count();
}

void OpenGLRasterizer::renderMainPass(const FrameRenderSettings& settings, RenderStats& stats) {
    const auto pass_start = std::chrono::high_resolution_clock::now();
    glViewport(0, 0, framebuffer_width_, framebuffer_height_);
    glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
    glClear(GL_DEPTH_BUFFER_BIT);
    if (settings.look_dev.enable_backface_culling) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
    else {
        glDisable(GL_CULL_FACE);
    }
    main_program_->bind();
    setMatrix4(this, main_program_, "uModel", settings.model_matrix);
    setMatrix4(this, main_program_, "uView", settings.view_matrix);
    setMatrix4(this, main_program_, "uProjection", settings.projection_matrix);
    setMatrix4(this, main_program_, "uLightMatrix", light_matrix_);
    Eigen::Matrix3f normal_matrix = settings.model_matrix.block<3, 3>(0, 0);
    if (std::abs(normal_matrix.determinant()) > 1e-8f) {
        normal_matrix = normal_matrix.inverse().transpose();
    }
    setMatrix3(this, main_program_, "uNormalMatrix", normal_matrix);

    main_program_->setUniformValue("uCameraPosition", settings.camera_position.x(), settings.camera_position.y(), settings.camera_position.z());
    main_program_->setUniformValue("uSunDirection", settings.sun_direction.x(), settings.sun_direction.y(), settings.sun_direction.z());
    main_program_->setUniformValue("uSunColor", settings.sun_color.x(), settings.sun_color.y(), settings.sun_color.z());
    main_program_->setUniformValue("uPbrIblEnabled", settings.look_dev.pbr.ibl_enabled ? 1 : 0);
    main_program_->setUniformValue("uMetallicChannel", settings.look_dev.pbr.metallic_channel);
    main_program_->setUniformValue("uRoughnessChannel", settings.look_dev.pbr.roughness_channel);
    main_program_->setUniformValue("uAoChannel", settings.look_dev.pbr.ao_channel);
    main_program_->setUniformValue("uEmissiveChannel", settings.look_dev.pbr.emissive_channel);
    main_program_->setUniformValue("uExposure", settings.look_dev.exposure);
    main_program_->setUniformValue("uNormalStrength", settings.look_dev.normal_strength);
    main_program_->setUniformValue("uIblDiffuseStrength", settings.look_dev.pbr.ibl_diffuse_strength);
    main_program_->setUniformValue("uIblSpecularStrength", settings.look_dev.pbr.ibl_specular_strength);
    main_program_->setUniformValue("uSkyLightStrength", settings.look_dev.pbr.sky_light_strength);
    main_program_->setUniformValue("uPhongDiffuseStrength", settings.look_dev.phong.diffuse_strength);
    main_program_->setUniformValue("uPhongAmbientStrength", settings.look_dev.phong.ambient_strength);
    main_program_->setUniformValue("uPhongAmbientColor",
                                   settings.look_dev.phong.ambient_color.redF(),
                                   settings.look_dev.phong.ambient_color.greenF(),
                                   settings.look_dev.phong.ambient_color.blueF());
    main_program_->setUniformValue("uPhongSpecularStrength", settings.look_dev.phong.specular_strength);
    main_program_->setUniformValue("uPhongSpecularTint",
                                   settings.look_dev.phong.specular_tint.redF(),
                                   settings.look_dev.phong.specular_tint.greenF(),
                                   settings.look_dev.phong.specular_tint.blueF());
    main_program_->setUniformValue("uPhongToonShadowTint",
                                   settings.look_dev.phong.toon.shadow_tint.redF(),
                                   settings.look_dev.phong.toon.shadow_tint.greenF(),
                                   settings.look_dev.phong.toon.shadow_tint.blueF());
    main_program_->setUniformValue("uPhongToonHighlightTint",
                                   settings.look_dev.phong.toon.highlight_tint.redF(),
                                   settings.look_dev.phong.toon.highlight_tint.greenF(),
                                   settings.look_dev.phong.toon.highlight_tint.blueF());
    main_program_->setUniformValue("uPhongSecondaryLightScale", settings.look_dev.phong.secondary_light_scale);
    main_program_->setUniformValue("uPhongSmoothness", settings.look_dev.phong.smoothness);
    main_program_->setUniformValue("uPhongSpecularMapWeight", settings.look_dev.phong.specular_map_weight);
    main_program_->setUniformValue("uPhongShininess", settings.look_dev.phong.shininess);
    main_program_->setUniformValue("uPhongRimStrength", settings.look_dev.phong.rim_strength);
    main_program_->setUniformValue("uPhongRimPower", settings.look_dev.phong.rim_power);
    main_program_->setUniformValue("uPhongToonDiffuseSteps", settings.look_dev.phong.toon.diffuse_steps);
    main_program_->setUniformValue("uPhongToonDiffuseSoftness", settings.look_dev.phong.toon.diffuse_softness);
    main_program_->setUniformValue("uPhongToonShadowFloor", settings.look_dev.phong.toon.shadow_floor);
    main_program_->setUniformValue("uPhongToonLitFloor", settings.look_dev.phong.toon.lit_floor);
    main_program_->setUniformValue("uPhongToonRampBias", settings.look_dev.phong.toon.ramp_bias);
    main_program_->setUniformValue("uPhongToonRampContrast", settings.look_dev.phong.toon.ramp_contrast);
    main_program_->setUniformValue("uPhongToonShadowMapStrength", settings.look_dev.phong.toon.shadow_map_strength);
    main_program_->setUniformValue("uPhongToonShadowThreshold", settings.look_dev.phong.toon.shadow_threshold);
    main_program_->setUniformValue("uPhongToonShadowSoftness", settings.look_dev.phong.toon.shadow_softness);
    main_program_->setUniformValue("uPhongToonHighlightThreshold", settings.look_dev.phong.toon.highlight_threshold);
    main_program_->setUniformValue("uPhongToonHighlightSoftness", settings.look_dev.phong.toon.highlight_softness);
    main_program_->setUniformValue("uPhongToonHighlightStrength", settings.look_dev.phong.toon.highlight_strength);
    main_program_->setUniformValue("uPhongToonRimThreshold", settings.look_dev.phong.toon.rim_threshold);
    main_program_->setUniformValue("uPhongToonRimSoftness", settings.look_dev.phong.toon.rim_softness);
    main_program_->setUniformValue("uPhongToonMaterialTextureStrength", settings.look_dev.phong.toon.material_texture_strength);
    main_program_->setUniformValue("uPhongToonMaterialLift", settings.look_dev.phong.toon.material_lift);
    main_program_->setUniformValue("uPhongToonMaterialSaturation", settings.look_dev.phong.toon.material_saturation);
    main_program_->setUniformValue("uPhongToonMaterialContrast", settings.look_dev.phong.toon.material_contrast);
    main_program_->setUniformValue("uPhongRimTint",
                                   settings.look_dev.phong.rim_tint.redF(),
                                   settings.look_dev.phong.rim_tint.greenF(),
                                   settings.look_dev.phong.rim_tint.blueF());
    main_program_->setUniformValue("uEnableShadows", settings.look_dev.enable_shadows ? 1 : 0);
    main_program_->setUniformValue("uShadingModel", static_cast<int>(settings.look_dev.shading_model));
    main_program_->setUniformValue("uPhongHardSpecular", settings.look_dev.phong.hard_specular ? 1 : 0);
    main_program_->setUniformValue("uPhongToonEnabled", settings.look_dev.phong.toon.enabled ? 1 : 0);
    main_program_->setUniformValue("uPhongUseTonemap", settings.look_dev.phong.use_tonemap ? 1 : 0);
    main_program_->setUniformValue("uPhongPrimaryLightOnly", settings.look_dev.phong.primary_light_only ? 1 : 0);
    main_program_->setUniformValue("uPhongToonMaterialOverrideEnabled", settings.look_dev.phong.toon.material_override_enabled ? 1 : 0);

    glActiveTexture(GL_TEXTURE0 + kShadowTextureUnit);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_texture_);
    main_program_->setUniformValue("uShadowMap", kShadowTextureUnit);

    const auto draw_mesh = [this, &settings](const GpuMesh& mesh) {
        const GpuMaterial& material = mesh.material;
        main_program_->setUniformValue("uBaseColorFactor", material.base_color_factor.x(), material.base_color_factor.y(), material.base_color_factor.z());
        main_program_->setUniformValue("uBaseAlphaFactor", material.base_alpha_factor);
        main_program_->setUniformValue("uEmissiveFactor", material.emissive_factor.x(), material.emissive_factor.y(), material.emissive_factor.z());
        main_program_->setUniformValue("uMetallicFactor", material.metallic_factor);
        main_program_->setUniformValue("uRoughnessFactor", material.roughness_factor);
        main_program_->setUniformValue("uAoFactor", material.ao_factor);
        main_program_->setUniformValue("uAlphaMode", material.alpha_mode);
        main_program_->setUniformValue("uAlphaCutoff", material.alpha_cutoff);
        main_program_->setUniformValue("uMaterialUnlit", material.unlit ? 1 : 0);
        main_program_->setUniformValue("uMaterialMtoon", material.mtoon ? 1 : 0);
        main_program_->setUniformValue("uMtoonShadeColorFactor",
                                       material.mtoon_shade_color_factor.x(),
                                       material.mtoon_shade_color_factor.y(),
                                       material.mtoon_shade_color_factor.z());
        main_program_->setUniformValue("uMtoonRimColorFactor",
                                       material.mtoon_rim_color_factor.x(),
                                       material.mtoon_rim_color_factor.y(),
                                       material.mtoon_rim_color_factor.z());
        main_program_->setUniformValue("uMtoonShadingShift", material.mtoon_shading_shift);
        main_program_->setUniformValue("uMtoonShadingToony", material.mtoon_shading_toony);
        main_program_->setUniformValue("uMtoonRimLift", material.mtoon_rim_lift);
        main_program_->setUniformValue("uMtoonRimFresnelPower", material.mtoon_rim_fresnel_power);

        const auto bindTexture = [this](int unit, unsigned int texture, unsigned int fallback, const char* uniform_name) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, texture != 0 ? texture : fallback);
            main_program_->setUniformValue(uniform_name, unit);
        };

        bindTexture(0, material.base_color_texture, fallback_white_, "uBaseColorTexture");
        bindTexture(1, material.diffuse_texture, fallback_white_, "uDiffuseTexture");
        bindTexture(2, material.normal_texture, fallback_normal_, "uNormalTexture");
        bindTexture(3, material.specular_texture, fallback_white_, "uSpecularTexture");
        bindTexture(4, material.metallic_texture, fallback_black_, "uMetallicTexture");
        bindTexture(5, material.roughness_texture, fallback_white_, "uRoughnessTexture");
        bindTexture(6, material.ao_texture, fallback_white_, "uAoTexture");
        bindTexture(7, material.emissive_texture, fallback_black_, "uEmissiveTexture");
        bindTexture(kMtoonShadeTextureUnit, material.mtoon_shade_texture, fallback_white_, "uMtoonShadeTexture");
        bindTexture(kMtoonMatcapTextureUnit, material.mtoon_matcap_texture, fallback_black_, "uMtoonMatcapTexture");

        main_program_->setUniformValue("uHasBaseColorTexture", material.has_base_color_texture ? 1 : 0);
        main_program_->setUniformValue("uHasDiffuseTexture", material.has_diffuse_texture ? 1 : 0);
        main_program_->setUniformValue("uHasNormalTexture", material.has_normal_texture ? 1 : 0);
        main_program_->setUniformValue("uHasSpecularTexture", material.has_specular_texture ? 1 : 0);
        main_program_->setUniformValue("uHasMetallicTexture", material.has_metallic_texture ? 1 : 0);
        main_program_->setUniformValue("uHasRoughnessTexture", material.has_roughness_texture ? 1 : 0);
        main_program_->setUniformValue("uHasAoTexture", material.has_ao_texture ? 1 : 0);
        main_program_->setUniformValue("uHasEmissiveTexture", material.has_emissive_texture ? 1 : 0);
        main_program_->setUniformValue("uHasMtoonShadeTexture", material.has_mtoon_shade_texture ? 1 : 0);
        main_program_->setUniformValue("uHasMtoonMatcapTexture", material.has_mtoon_matcap_texture ? 1 : 0);

        if (material.face_overlay) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-2.0f, -8.0f);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }

        if (material.double_sided || material.face_overlay || !settings.look_dev.enable_backface_culling) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
        if (material.face_overlay) {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    };

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    for (const GpuMesh& mesh : meshes_) {
        if (mesh.material.alpha_mode == 2) {
            continue;
        }
        draw_mesh(mesh);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    std::vector<int> transparent_indices(meshes_.size());
    std::iota(transparent_indices.begin(), transparent_indices.end(), 0);
    std::stable_sort(transparent_indices.begin(), transparent_indices.end(), [this](int a, int b) {
        return meshes_[a].material.render_queue_offset < meshes_[b].material.render_queue_offset;
    });
    for (int mesh_index : transparent_indices) {
        const GpuMesh& mesh = meshes_[mesh_index];
        if (mesh.material.alpha_mode != 2) {
            continue;
        }
        glDepthMask(mesh.material.transparent_with_z_write ? GL_TRUE : GL_FALSE);
        draw_mesh(mesh);
    }
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    glBindVertexArray(0);
    for (int unit = 0; unit <= kShadowTextureUnit; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glDisable(GL_BLEND);
    main_program_->release();

    const auto pass_end = std::chrono::high_resolution_clock::now();
    stats.main_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(pass_end - pass_start).count();
}

void OpenGLRasterizer::renderOutlinePass(const FrameRenderSettings& settings) {
    if (!outline_program_) {
        return;
    }

    const PhongOutlineSettings& outline = settings.look_dev.phong.outline;
    if (!outline.enabled || outline.width_pixels <= 0.0f || outline.opacity <= 0.0f) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
    glViewport(0, 0, framebuffer_width_, framebuffer_height_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    outline_program_->bind();
    setMatrix4(this, outline_program_, "uModel", settings.model_matrix);
    setMatrix4(this, outline_program_, "uView", settings.view_matrix);
    setMatrix4(this, outline_program_, "uProjection", settings.projection_matrix);
    Eigen::Matrix3f normal_matrix = settings.model_matrix.block<3, 3>(0, 0);
    if (std::abs(normal_matrix.determinant()) > 1e-8f) {
        normal_matrix = normal_matrix.inverse().transpose();
    }
    setMatrix3(this, outline_program_, "uNormalMatrix", normal_matrix);
    outline_program_->setUniformValue("uViewportSize", static_cast<float>(framebuffer_width_), static_cast<float>(framebuffer_height_));
    outline_program_->setUniformValue("uOutlineWidthPixels", outline.width_pixels);
    outline_program_->setUniformValue("uOutlineDepthBias", outline.depth_bias);
    outline_program_->setUniformValue("uOutlineColor",
                                      outline.color.redF(),
                                      outline.color.greenF(),
                                      outline.color.blueF(),
                                      outline.opacity);

    for (const GpuMesh& mesh : meshes_) {
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
    }

    glBindVertexArray(0);
    outline_program_->release();
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (settings.look_dev.enable_backface_culling) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

Eigen::Matrix4f OpenGLRasterizer::buildLightMatrix(const FrameRenderSettings& settings) const {
    Eigen::Vector3f direction = settings.sun_direction;
    if (direction.squaredNorm() < 1e-8f) {
        direction = Eigen::Vector3f(-0.45f, -1.0f, -0.25f);
    }
    direction.normalize();

    const Eigen::Vector3f center = settings.camera_target;
    const float radius = settings.scene ? std::max(settings.scene->bounds.radius(), 1.0f) : 3.0f;
    const Eigen::Vector3f eye = center - direction * (radius * 3.0f);
    const Eigen::Vector3f up = std::abs(direction.dot(Eigen::Vector3f::UnitY())) > 0.98f
                                   ? Eigen::Vector3f::UnitZ()
                                   : Eigen::Vector3f::UnitY();

    const Eigen::Matrix4f light_view = math::lookAt(eye, center, up);
    const Eigen::Matrix4f light_projection = math::orthographic(
        -radius * 1.8f,
         radius * 1.8f,
        -radius * 1.8f,
         radius * 1.8f,
         0.1f,
         radius * 8.0f);
    return light_projection * light_view;
}

} // namespace haorendergi
