#include "rendering/dxr_ray_tracer.h"

#include <QElapsedTimer>
#include <QColor>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QVector2D>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef WGL_ACCESS_READ_ONLY_NV
#define WGL_ACCESS_READ_ONLY_NV 0x0000
#endif
#ifndef WGL_ACCESS_READ_WRITE_NV
#define WGL_ACCESS_READ_WRITE_NV 0x0001
#endif
#endif

namespace haorendergi {
namespace {

#ifdef _WIN32

constexpr wchar_t kRayGenShader[] = L"RayGen";
constexpr wchar_t kMissShader[] = L"Miss";
constexpr wchar_t kClosestHitShader[] = L"ClosestHit";
constexpr wchar_t kShadowMissShader[] = L"ShadowMiss";
constexpr wchar_t kShadowClosestHitShader[] = L"ShadowClosestHit";
constexpr wchar_t kHitGroup[] = L"HitGroup";
constexpr wchar_t kShadowHitGroup[] = L"ShadowHitGroup";

QString hresultMessage(const QString& label, HRESULT hr) {
    return QStringLiteral("%1 (HRESULT 0x%2)")
        .arg(label)
        .arg(static_cast<qulonglong>(static_cast<unsigned long>(hr)), 8, 16, QLatin1Char('0'));
}

bool failIfFailed(HRESULT hr, const QString& label, QString* error_message) {
    if (SUCCEEDED(hr)) {
        return false;
    }
    if (error_message) {
        *error_message = hresultMessage(label, hr);
    }
    return true;
}

D3D12_RESOURCE_BARRIER transitionBarrier(ID3D12Resource* resource,
                                         D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

D3D12_RESOURCE_BARRIER uavBarrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    return barrier;
}

const char* glVertexShaderSource() {
    return R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 p;
    if (gl_VertexID == 0) {
        p = vec2(-1.0, -1.0);
    } else if (gl_VertexID == 1) {
        p = vec2(3.0, -1.0);
    } else {
        p = vec2(-1.0, 3.0);
    }
    vUv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    gl_Position = vec4(p, 0.0, 1.0);
})";
}

const char* glFragmentShaderSource() {
    return R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uFrame;
uniform vec2 uTexelSize;
uniform float uDenoiseStrength;

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    float strength = clamp(uDenoiseStrength, 0.0, 1.0);
    vec3 center = texture(uFrame, vUv).rgb;
    float centerLum = luminance(center);

    vec3 sum = center * 1.8;
    float weightSum = 1.8;
    float sigmaLum = mix(0.035, 0.20, strength);
    float sigmaColor = mix(0.055, 0.26, strength);

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }
            vec2 o = vec2(float(x), float(y));
            vec3 sampleColor = texture(uFrame, vUv + o * uTexelSize).rgb;
            float lumDiff = abs(luminance(sampleColor) - centerLum);
            float colorDiff = length(sampleColor - center);
            float spatial = exp(-dot(o, o) * 0.18);
            float edge = exp(-(lumDiff * lumDiff) / max(2.0 * sigmaLum * sigmaLum, 1e-5)) *
                         exp(-(colorDiff * colorDiff) / max(2.0 * sigmaColor * sigmaColor, 1e-5));
            float w = spatial * edge;
            sum += sampleColor * w;
            weightSum += w;
        }
    }

    vec3 filtered = sum / max(weightSum, 1e-4);
    fragColor = vec4(mix(center, filtered, strength), 1.0);
})";
}

const char* dxrShaderSource() {
    return R"(
RaytracingAccelerationStructure gScene : register(t0);
struct VertexData {
    float3 position;
    float3 normal;
    float3 tangent;
    float2 uv;
    uint materialIndex;
    uint padding;
};

struct MaterialData {
    float4 baseColor;
    float4 emissive;
    float4 params;
    float4 baseColorRect;
    float4 normalRect;
    float4 metallicRect;
    float4 roughnessRect;
    float4 aoRect;
    float4 emissiveRect;
};

StructuredBuffer<VertexData> gVertices : register(t1);
StructuredBuffer<uint> gIndices : register(t2);
StructuredBuffer<MaterialData> gMaterials : register(t3);
StructuredBuffer<uint> gPrimitiveMaterials : register(t4);
Texture2D<float4> gBaseColorAtlas : register(t5);
Texture2D<float4> gNormalAtlas : register(t6);
Texture2D<float4> gMetallicAtlas : register(t7);
Texture2D<float4> gRoughnessAtlas : register(t8);
Texture2D<float4> gAoAtlas : register(t9);
Texture2D<float4> gEmissiveAtlas : register(t10);
RWTexture2D<float4> gOutput : register(u0);
RWTexture2D<float4> gAccumulation : register(u1);
RWTexture2D<float2> gMoments : register(u2);
SamplerState gLinearSampler : register(s0);

cbuffer FrameConstants : register(b0) {
    float4 gCameraPosition;
    float4 gCameraForward;
    float4 gCameraRight;
    float4 gCameraUp;
    float4 gFrameParams;       // fovY, aspect, frameIndex, maxBounces
    float4 gRenderParams;      // spp, maxNEEBounces, enableNEE, viewMode
    float4 gSunDirection;
    float4 gSunColor;
    float4 gAreaLightCorner;
    float4 gAreaLightEdgeU;
    float4 gAreaLightEdgeV;
    float4 gAreaLightEmissive;
};

struct RayPayload {
    float3 hitPoint;
    float hitT;
    float3 normal;
    uint materialIndex;
    float3 tangent;
    uint frontFace;
    float2 uv;
    uint hit;
    uint padding;
};

struct ShadowPayload {
    uint visible;
};

uint WangHash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float RandomFloat01(inout uint state) {
    state = WangHash(state);
    return float(state) / 4294967296.0;
}

float3 MakeTangent(float3 n) {
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    return normalize(cross(up, n));
}

float3 ToWorld(float3 localDir, float3 n) {
    float3 t = MakeTangent(n);
    float3 b = cross(n, t);
    return normalize(t * localDir.x + b * localDir.y + n * localDir.z);
}

float3 SampleCosineHemisphere(float3 n, inout uint rng) {
    float u1 = RandomFloat01(rng);
    float u2 = RandomFloat01(rng);
    float r = sqrt(u1);
    float phi = 6.28318530718 * u2;
    float3 localDir = float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
    return ToWorld(localDir, n);
}

float3 FresnelSchlick(float cosTheta, float3 f0) {
    float f = pow(1.0 - saturate(cosTheta), 5.0);
    return f0 + (1.0 - f0) * f;
}

bool HasTextureFlag(MaterialData material, float bit) {
    return fmod(floor(material.params.w / bit), 2.0) >= 1.0;
}

float2 AtlasUv(float4 rect, float2 uv) {
    return rect.xy + frac(uv) * rect.zw;
}

float DistributionGGX(float3 n, float3 h, float roughness) {
    float a = max(roughness, 0.035);
    a *= a;
    float a2 = a * a;
    float ndh = max(dot(n, h), 0.0);
    float denom = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denom * denom, 1e-5);
}

float GeometrySchlickGGX(float ndv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return ndv / max(ndv * (1.0 - k) + k, 1e-5);
}

float GeometrySmith(float3 n, float3 v, float3 l, float roughness) {
    return GeometrySchlickGGX(max(dot(n, v), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(n, l), 0.0), roughness);
}

float3 SampleBaseColor(MaterialData material, float2 uv) {
    float3 base = material.baseColor.rgb;
    if (HasTextureFlag(material, 1.0)) {
        base *= gBaseColorAtlas.SampleLevel(gLinearSampler, AtlasUv(material.baseColorRect, uv), 0.0).rgb;
    }
    return max(base, 0.0);
}

float SampleMetallic(MaterialData material, float2 uv) {
    float metallic = saturate(material.params.x);
    if (HasTextureFlag(material, 4.0)) {
        metallic *= gMetallicAtlas.SampleLevel(gLinearSampler, AtlasUv(material.metallicRect, uv), 0.0).b;
    }
    return saturate(metallic);
}

float SampleRoughness(MaterialData material, float2 uv) {
    float roughness = clamp(material.params.y, 0.035, 1.0);
    if (HasTextureFlag(material, 8.0)) {
        roughness *= gRoughnessAtlas.SampleLevel(gLinearSampler, AtlasUv(material.roughnessRect, uv), 0.0).g;
    }
    return clamp(roughness, 0.035, 1.0);
}

float SampleAo(MaterialData material, float2 uv) {
    float ao = saturate(material.emissive.w);
    if (HasTextureFlag(material, 16.0)) {
        ao *= gAoAtlas.SampleLevel(gLinearSampler, AtlasUv(material.aoRect, uv), 0.0).r;
    }
    return saturate(ao);
}

float3 SampleEmissive(MaterialData material, float2 uv) {
    float3 emissive = max(material.emissive.rgb, 0.0);
    if (HasTextureFlag(material, 32.0)) {
        emissive *= gEmissiveAtlas.SampleLevel(gLinearSampler, AtlasUv(material.emissiveRect, uv), 0.0).rgb;
    }
    return emissive;
}

float3 SampleShadingNormal(MaterialData material, float2 uv, float3 geometricNormal, float3 tangent) {
    float3 n = normalize(geometricNormal);
    if (!HasTextureFlag(material, 2.0)) {
        return n;
    }

    float3 t = tangent - n * dot(n, tangent);
    if (dot(t, t) < 1e-5) {
        t = MakeTangent(n);
    } else {
        t = normalize(t);
    }
    float3 b = normalize(cross(n, t));
    float3 mapNormal = gNormalAtlas.SampleLevel(gLinearSampler, AtlasUv(material.normalRect, uv), 0.0).xyz * 2.0 - 1.0;
    return normalize(t * mapNormal.x + b * mapNormal.y + n * mapNormal.z);
}

float3 EvalPbrBrdf(MaterialData material, float3 albedo, float metallic, float roughness, float3 n, float3 v, float3 l) {
    float ndl = max(dot(n, l), 0.0);
    float ndv = max(dot(n, v), 0.0);
    if (ndl <= 0.0 || ndv <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }

    float3 h = normalize(v + l);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 f = FresnelSchlick(max(dot(h, v), 0.0), f0);
    float d = DistributionGGX(n, h, roughness);
    float g = GeometrySmith(n, v, l, roughness);
    float3 specular = d * g * f / max(4.0 * ndv * ndl, 1e-5);
    float3 kd = (1.0 - f) * (1.0 - metallic);
    float3 diffuse = kd * albedo / 3.14159265;
    return diffuse + specular;
}

float3 GetSkyColor(float3 dir) {
    float t = saturate(dir.y * 0.5 + 0.5);
    float3 horizon = float3(0.34, 0.42, 0.52);
    float3 zenith = float3(0.035, 0.055, 0.085);
    float3 sky = lerp(horizon, zenith, pow(t, 1.2));
    float sun = max(dot(dir, normalize(-gSunDirection.xyz)), 0.0);
    sky += gSunColor.rgb * pow(sun, 256.0) * 4.0;
    return sky;
}

RayPayload TraceClosest(float3 origin, float3 direction) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = 1000.0;

    RayPayload payload;
    payload.hitPoint = float3(0.0, 0.0, 0.0);
    payload.hitT = 1000.0;
    payload.normal = float3(0.0, 1.0, 0.0);
    payload.materialIndex = 0;
    payload.tangent = float3(1.0, 0.0, 0.0);
    payload.uv = float2(0.0, 0.0);
    payload.hit = 0;
    payload.frontFace = 1;
    TraceRay(gScene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 2, 0, ray, payload);
    return payload;
}

bool TraceOcclusion(float3 origin, float3 direction, float maxT) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.002;
    ray.TMax = max(maxT, 0.003);

    ShadowPayload payload;
    payload.visible = 0;
    TraceRay(gScene,
             RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
             0xFF,
             1,
             2,
             1,
             ray,
             payload);
    return payload.visible == 0;
}

)"
R"(

float AreaLightPdf(float3 p, float3 lightDir) {
    float3 lightNormal = normalize(cross(gAreaLightEdgeU.xyz, gAreaLightEdgeV.xyz));
    float area = max(length(cross(gAreaLightEdgeU.xyz, gAreaLightEdgeV.xyz)), 1e-4);
    float denom = dot(lightDir, lightNormal);
    if (abs(denom) <= 1e-5) {
        return 0.0;
    }
    float planeT = dot(gAreaLightCorner.xyz - p, lightNormal) / denom;
    if (planeT <= 0.001) {
        return 0.0;
    }
    float3 hit = p + lightDir * planeT;
    float3 rel = hit - gAreaLightCorner.xyz;
    float uu = dot(rel, gAreaLightEdgeU.xyz) / max(dot(gAreaLightEdgeU.xyz, gAreaLightEdgeU.xyz), 1e-6);
    float vv = dot(rel, gAreaLightEdgeV.xyz) / max(dot(gAreaLightEdgeV.xyz, gAreaLightEdgeV.xyz), 1e-6);
    if (uu < 0.0 || uu > 1.0 || vv < 0.0 || vv > 1.0) {
        return 0.0;
    }
    float cosLight = max(dot(lightNormal, -lightDir), 0.0);
    return planeT * planeT / max(cosLight * area, 1e-5);
}

float PowerHeuristic(float a, float b) {
    float aa = a * a;
    float bb = b * b;
    return aa / max(aa + bb, 1e-6);
}

float3 SampleAreaLight(float3 p, float3 n, float3 v, MaterialData material, float3 albedo, float metallic, float roughness, inout uint rng) {
    float u = RandomFloat01(rng);
    float w = RandomFloat01(rng);
    float3 lightPos = gAreaLightCorner.xyz + gAreaLightEdgeU.xyz * u + gAreaLightEdgeV.xyz * w;
    float3 lightNormal = normalize(cross(gAreaLightEdgeU.xyz, gAreaLightEdgeV.xyz));
    float area = max(length(cross(gAreaLightEdgeU.xyz, gAreaLightEdgeV.xyz)), 1e-4);
    float3 toLight = lightPos - p;
    float dist2 = max(dot(toLight, toLight), 1e-4);
    float dist = sqrt(dist2);
    float3 l = toLight / dist;
    float cosSurface = max(dot(n, l), 0.0);
    float cosLight = max(dot(lightNormal, -l), 0.0);
    if (cosSurface <= 0.0 || cosLight <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }
    if (TraceOcclusion(p + n * 0.003, l, dist - 0.006)) {
        return float3(0.0, 0.0, 0.0);
    }
    float pdf = dist2 / max(cosLight * area, 1e-5);
    float bsdfPdf = max(cosSurface, 0.0) / 3.14159265;
    float mis = PowerHeuristic(pdf, bsdfPdf);
    return gAreaLightEmissive.rgb * EvalPbrBrdf(material, albedo, metallic, roughness, n, v, l) * cosSurface * mis / max(pdf, 1e-5);
}

float3 SampleSun(float3 p, float3 n, float3 v, MaterialData material, float3 albedo, float metallic, float roughness) {
    float3 l = normalize(-gSunDirection.xyz);
    float cosSurface = max(dot(n, l), 0.0);
    if (cosSurface <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }
    if (TraceOcclusion(p + n * 0.003, l, 1000.0)) {
        return float3(0.0, 0.0, 0.0);
    }
    return gSunColor.rgb * EvalPbrBrdf(material, albedo, metallic, roughness, n, v, l) * cosSurface;
}

float3 SampleBsdfDirection(MaterialData material, float3 albedo, float metallic, float roughness, float3 n, float3 v, bool frontFace, inout uint rng, out float3 weight, out bool deltaBounce) {
    deltaBounce = false;
    float materialType = material.params.z;
    if (materialType > 1.5) {
        deltaBounce = true;
        float ior = 1.5;
        float3 incident = -v;
        float etaRatio = frontFace ? (1.0 / ior) : ior;
        float cosTheta = min(dot(-incident, n), 1.0);
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        float r0 = (1.0 - ior) / (1.0 + ior);
        r0 *= r0;
        float fresnel = r0 + (1.0 - r0) * pow(1.0 - saturate(cosTheta), 5.0);
        bool cannotRefract = etaRatio * sinTheta > 1.0;
        if (cannotRefract || RandomFloat01(rng) < fresnel) {
            weight = lerp(float3(1.0, 1.0, 1.0), albedo, 0.18);
            return normalize(reflect(incident, n));
        }
        weight = lerp(float3(1.0, 1.0, 1.0), albedo, 0.08);
        return normalize(refract(incident, n, etaRatio));
    }

    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float specChance = saturate(0.18 + metallic * 0.68 + (1.0 - roughness) * 0.18);
    if (RandomFloat01(rng) < specChance) {
        float3 perfect = reflect(-v, n);
        float3 jitter = SampleCosineHemisphere(perfect, rng);
        float3 dir = normalize(lerp(perfect, jitter, roughness * roughness));
        weight = FresnelSchlick(max(dot(n, v), 0.0), f0) / max(specChance, 1e-3);
        return dir;
    }

    float3 diffuseDir = SampleCosineHemisphere(n, rng);
    weight = albedo * (1.0 - metallic) / max(1.0 - specChance, 1e-3);
    return diffuseDir;
}

float3 TracePath(float3 origin, float3 direction, inout uint rng) {
    float3 radiance = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);
    int maxBounces = clamp((int)gFrameParams.w, 1, 20);
    int maxNeeBounces = clamp((int)gRenderParams.y, 0, 8);
    bool lastBounceDelta = true;

    [loop]
    for (int bounce = 0; bounce < 20; ++bounce) {
        if (bounce >= maxBounces) {
            break;
        }

        RayPayload hit = TraceClosest(origin, direction);
        if (hit.hit == 0) {
            radiance += throughput * GetSkyColor(direction);
            break;
        }

        MaterialData material = gMaterials[hit.materialIndex];
        float3 albedo = SampleBaseColor(material, hit.uv);
        float3 emissive = SampleEmissive(material, hit.uv);
        if (dot(emissive, emissive) > 0.0) {
            if (bounce == 0 || lastBounceDelta) {
                radiance += throughput * emissive;
            }
            break;
        }

        float3 n = SampleShadingNormal(material, hit.uv, hit.normal, hit.tangent);
        float3 v = normalize(-direction);
        float materialType = material.params.z;
        float metallic = SampleMetallic(material, hit.uv);
        float roughness = SampleRoughness(material, hit.uv);
        float ao = SampleAo(material, hit.uv);
        albedo *= lerp(0.35, 1.0, ao);

        if (gRenderParams.z > 0.5 && bounce < maxNeeBounces && materialType < 1.5) {
            radiance += throughput * SampleSun(hit.hitPoint, n, v, material, albedo, metallic, roughness);
            radiance += throughput * SampleAreaLight(hit.hitPoint, n, v, material, albedo, metallic, roughness, rng);
        }

        float3 bsdfWeight;
        bool deltaBounce;
        float3 nextDir = SampleBsdfDirection(material, albedo, metallic, roughness, n, v, hit.frontFace != 0, rng, bsdfWeight, deltaBounce);
        if (materialType < 1.5 && dot(nextDir, n) <= 0.0) {
            nextDir = reflect(direction, n);
        }

        throughput *= bsdfWeight;
        throughput = min(throughput, float3(8.0, 8.0, 8.0));
        origin = hit.hitPoint + nextDir * 0.003;
        direction = normalize(nextDir);
        lastBounceDelta = deltaBounce;

        if (bounce >= 3) {
            float p = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.92);
            if (RandomFloat01(rng) > p) {
                break;
            }
            throughput /= p;
        }
    }
    return radiance;
}

float3 Tonemap(float3 color) {
    color = max(color, 0.0);
    color = color / (color + 1.0);
    return pow(color, 1.0 / 2.2);
}

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[shader("raygeneration")]
void RayGen() {
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;
    float2 uv = (float2(pixel) + 0.5) / float2(dims);
    float2 screen = uv * 2.0 - 1.0;
    screen.y = -screen.y;
    float aspect = gFrameParams.y;
    float tanHalfFov = tan(gFrameParams.x * 0.5);
    float3 primaryDir = normalize(gCameraForward.xyz +
                                  gCameraRight.xyz * screen.x * aspect * tanHalfFov +
                                  gCameraUp.xyz * screen.y * tanHalfFov);
    int viewMode = (int)gRenderParams.w;
    if (viewMode != 0) {
        RayPayload hit = TraceClosest(gCameraPosition.xyz, primaryDir);
        float3 debugColor = GetSkyColor(primaryDir);
        if (hit.hit != 0) {
            MaterialData material = gMaterials[hit.materialIndex];
            if (viewMode == 1) {
                debugColor = float3(1.0, 1.0, 1.0);
            } else if (viewMode == 2) {
                debugColor = normalize(hit.normal) * 0.5 + 0.5;
            } else {
                debugColor = SampleBaseColor(material, hit.uv);
            }
        }
        gAccumulation[pixel] = float4(debugColor, 1.0);
        gMoments[pixel] = float2(Luminance(debugColor), 0.0);
        gOutput[pixel] = float4(Tonemap(debugColor), 1.0);
        return;
    }

    uint rng = (pixel.x * 1973u + pixel.y * 9277u + (uint)gFrameParams.z * 26699u) | 1u;
    float4 previous = gAccumulation[pixel];
    float previousCount = gFrameParams.z < 0.5 ? 0.0 : max(previous.a, 0.0);
    float2 previousMoments = gFrameParams.z < 0.5 ? float2(0.0, 0.0) : gMoments[pixel];
    float previousVariance = previousCount > 1.0 ? previousMoments.y / max(previousCount - 1.0, 1.0) : 1.0;

    int spp = clamp((int)gRenderParams.x, 1, 16);
    if (previousCount > 8.0 && viewMode == 0) {
        if (previousVariance > 0.0015) {
            spp = min(16, spp * 2);
        } else if (previousVariance < 0.00008 && previousCount > 48.0) {
            spp = max(1, (spp + 1) / 2);
        }
    }

    float3 frameColor = float3(0.0, 0.0, 0.0);
    [loop]
    for (int s = 0; s < 16; ++s) {
        if (s >= spp) {
            break;
        }
        float2 jitter = float2(RandomFloat01(rng), RandomFloat01(rng)) - 0.5;
        float2 sampleScreen = ((float2(pixel) + 0.5 + jitter) / float2(dims)) * 2.0 - 1.0;
        sampleScreen.y = -sampleScreen.y;
        float3 dir = normalize(gCameraForward.xyz +
                               gCameraRight.xyz * sampleScreen.x * aspect * tanHalfFov +
                               gCameraUp.xyz * sampleScreen.y * tanHalfFov);
        frameColor += TracePath(gCameraPosition.xyz, dir, rng);
    }
    frameColor /= max((float)spp, 1.0);

    float newCount = previousCount + 1.0;
    float3 accumulated = (previous.rgb * previousCount + frameColor) / newCount;
    float frameLuminance = Luminance(frameColor);
    float delta = frameLuminance - previousMoments.x;
    float meanLuminance = previousMoments.x + delta / newCount;
    float m2 = previousMoments.y + delta * (frameLuminance - meanLuminance);
    gMoments[pixel] = float2(meanLuminance, m2);
    gAccumulation[pixel] = float4(accumulated, newCount);
    gOutput[pixel] = float4(Tonemap(accumulated), 1.0);
}

[shader("miss")]
void Miss(inout RayPayload payload) {
    payload.hit = 0;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    uint primitive = PrimitiveIndex();
    uint i0 = gIndices[primitive * 3 + 0];
    uint i1 = gIndices[primitive * 3 + 1];
    uint i2 = gIndices[primitive * 3 + 2];
    VertexData v0 = gVertices[i0];
    VertexData v1 = gVertices[i1];
    VertexData v2 = gVertices[i2];
    float3 p0 = v0.position;
    float3 p1 = v1.position;
    float3 p2 = v2.position;

    float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y,
                         attr.barycentrics.x,
                         attr.barycentrics.y);
    float3 normal = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);
    float3 tangent = normalize(v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z);
    if (dot(normal, normal) < 1e-4) {
        normal = normalize(cross(p1 - p0, p2 - p0));
    }
    if (dot(tangent, tangent) < 1e-4) {
        tangent = MakeTangent(normal);
    } else {
        tangent = normalize(tangent - normal * dot(normal, tangent));
    }
    bool frontFace = dot(normal, WorldRayDirection()) < 0.0;
    if (!frontFace) {
        normal = -normal;
        tangent = -tangent;
    }

    payload.hit = 1;
    payload.hitT = RayTCurrent();
    payload.hitPoint = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    payload.normal = normal;
    payload.materialIndex = gPrimitiveMaterials[primitive];
    payload.tangent = tangent;
    payload.uv = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;
    payload.frontFace = frontFace ? 1 : 0;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload) {
    payload.visible = 1;
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.visible = 0;
}
)";
}

#endif

} // namespace

DxrRayTracer::DxrRayTracer() = default;

DxrRayTracer::~DxrRayTracer() = default;

QString DxrRayTracer::backendName() const {
#ifdef _WIN32
    if (raytracing_tier_ == D3D12_RAYTRACING_TIER_1_1) {
        return QStringLiteral("DXR Hardware RT Tier 1.1");
    }
    if (raytracing_tier_ == D3D12_RAYTRACING_TIER_1_0) {
        return QStringLiteral("DXR Hardware RT Tier 1.0");
    }
#endif
    return QStringLiteral("DXR Hardware RT");
}

bool DxrRayTracer::initialize(QString* error_message) {
#ifndef _WIN32
    if (error_message) {
        *error_message = QStringLiteral("DXR requires Windows and Direct3D 12.");
    }
    return false;
#else
    if (!QOpenGLContext::currentContext()) {
        if (error_message) {
            *error_message = QStringLiteral("DXR display bridge requires an active OpenGL context.");
        }
        return false;
    }

    initializeOpenGLFunctions();
    if (!buildGlDisplay(error_message)) {
        return false;
    }
    if (!initializeD3D(error_message)) {
        return false;
    }
    if (!initializeDxr(error_message)) {
        return false;
    }
    initialized_ = true;
    output_dirty_ = true;
    return true;
#endif
}

void DxrRayTracer::shutdown() {
#ifdef _WIN32
    if (initialized_) {
        QString ignored;
        waitForGpu(&ignored);
    }
    releaseDxrResources();
#endif
    destroyGlDisplay();
    initialized_ = false;
}

void DxrRayTracer::resize(int framebuffer_width, int framebuffer_height) {
    framebuffer_width = std::max(1, framebuffer_width);
    framebuffer_height = std::max(1, framebuffer_height);
    if (framebuffer_width_ == framebuffer_width && framebuffer_height_ == framebuffer_height) {
        return;
    }
    framebuffer_width_ = framebuffer_width;
    framebuffer_height_ = framebuffer_height;
    output_dirty_ = true;
}

void DxrRayTracer::uploadScene(const SceneModel& scene) {
#ifdef _WIN32
    cached_scene_ = scene;
    has_cached_scene_ = !scene.empty();
    scene_error_.clear();
    if (!initialized_ || !device_) {
        output_dirty_ = true;
        return;
    }

    if (scene_buffers_are_cornell_) {
        output_dirty_ = true;
        return;
    }

    std::vector<DxrVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> primitive_materials;
    std::vector<DxrMaterial> materials;
    DxrTextureAtlases texture_atlases;
    flattenScene(scene, &vertices, &indices, &primitive_materials, &materials, &texture_atlases);
    if (vertices.empty() || indices.empty()) {
        QString error_message;
        if (!buildFallbackAccelerationStructures(&error_message)) {
            scene_error_ = error_message;
        }
    } else {
        QString error_message;
        if (!buildAccelerationStructures(vertices, indices, primitive_materials, materials, texture_atlases, &error_message)) {
            scene_error_ = error_message;
        }
    }
    if (scene_error_.isEmpty()) {
        QString descriptor_error;
        if (!buildDescriptorHeap(&descriptor_error)) {
            scene_error_ = descriptor_error;
        }
    }
    scene_buffers_are_cornell_ = false;
    resetAccumulation();
#endif
    output_dirty_ = true;
}

RenderStats DxrRayTracer::render(const FrameRenderSettings& settings) {
    RenderStats stats;
    QElapsedTimer timer;
    timer.start();

    if (!initialized_) {
        stats.note = QStringLiteral("DXR backend is not initialized.");
        return stats;
    }

#ifdef _WIN32
    if (!scene_error_.isEmpty()) {
        stats.note = scene_error_;
        return stats;
    }

    QString error_message;
    if (!ensureSceneMode(settings, &error_message)) {
        stats.note = error_message;
        return stats;
    }

    if (output_dirty_) {
        if (!buildOutputResources(&error_message) ||
            !buildDescriptorHeap(&error_message)) {
            stats.note = error_message;
            return stats;
        }
        output_dirty_ = false;
    }

    const std::uint64_t signature = frameSignature(settings);
    if (signature != last_frame_signature_) {
        resetAccumulation();
        last_frame_signature_ = signature;
    }
    const float denoise_convergence =
        std::clamp(static_cast<float>(accumulation_frame_index_) / 256.0f, 0.0f, 1.0f);
    display_denoise_strength_ = settings.look_dev.ray_trace.enable_photon_cache
        ? 0.55f * (1.0f - denoise_convergence * 0.45f)
        : 0.0f;

    QElapsedTimer dispatch_timer;
    dispatch_timer.start();
    if (!dispatchDxr(settings, &error_message)) {
        stats.note = error_message;
        return stats;
    }
    stats.main_ms = static_cast<double>(dispatch_timer.nsecsElapsed()) / 1000000.0;
    if (!drawGpuInteropImage(&error_message)) {
        stats.note = error_message;
        return stats;
    }
    stats.frame_ms = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    if (accumulation_frame_index_ < std::numeric_limits<std::uint64_t>::max() - 1ULL) {
        ++accumulation_frame_index_;
    }
    stats.note = QStringLiteral("DXR PT: %1 tris, %2 mats, %3 spp, %4 bounces, accumulation %5 frames, GPU present: %6.")
        .arg(scene_triangle_count_)
        .arg(scene_material_count_)
        .arg(settings.look_dev.ray_trace.samples_per_frame)
        .arg(settings.look_dev.ray_trace.max_bounces)
        .arg(static_cast<qulonglong>(accumulation_frame_index_))
        .arg(gpu_present_mode_.isEmpty() ? QStringLiteral("DXR/OpenGL") : gpu_present_mode_);
#else
    stats.note = QStringLiteral("DXR requires Windows and Direct3D 12.");
#endif
    return stats;
}

#ifdef _WIN32

bool DxrRayTracer::initializeD3D(QString* error_message) {
#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<ID3D12Debug> debug_controller;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
        debug_controller->EnableDebugLayer();
    }
#endif

    UINT factory_flags = 0;
    if (failIfFailed(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&dxgi_factory_)),
                     QStringLiteral("CreateDXGIFactory2 failed"),
                     error_message)) {
        return false;
    }

    const auto* gl_vendor_bytes = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const auto* gl_renderer_bytes = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    gl_vendor_ = QString::fromLatin1(gl_vendor_bytes ? gl_vendor_bytes : "");
    gl_renderer_ = QString::fromLatin1(gl_renderer_bytes ? gl_renderer_bytes : "");
    const QString gl_identity = QStringLiteral("%1 %2").arg(gl_vendor_, gl_renderer_).toLower();
    QString preferred_vendor;
    if (gl_identity.contains(QStringLiteral("nvidia"))) {
        preferred_vendor = QStringLiteral("nvidia");
    } else if (gl_identity.contains(QStringLiteral("intel"))) {
        preferred_vendor = QStringLiteral("intel");
    } else if (gl_identity.contains(QStringLiteral("amd")) ||
               gl_identity.contains(QStringLiteral("radeon")) ||
               gl_identity.contains(QStringLiteral("advanced micro"))) {
        preferred_vendor = QStringLiteral("amd");
    }

    int best_score = std::numeric_limits<int>::min();
    for (UINT adapter_index = 0;; ++adapter_index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        HRESULT enum_hr = dxgi_factory_->EnumAdapterByGpuPreference(
            adapter_index,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&candidate));
        if (enum_hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(enum_hr)) {
            continue;
        }

        DXGI_ADAPTER_DESC1 desc = {};
        candidate->GetDesc1(&desc);
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        Microsoft::WRL::ComPtr<ID3D12Device5> candidate_device;
        if (FAILED(D3D12CreateDevice(candidate.Get(),
                                     D3D_FEATURE_LEVEL_12_1,
                                     IID_PPV_ARGS(&candidate_device)))) {
            continue;
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 candidate_options5 = {};
        if (FAILED(candidate_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                                         &candidate_options5,
                                                         sizeof(candidate_options5))) ||
            candidate_options5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
            continue;
        }

        const QString adapter_name = QString::fromWCharArray(desc.Description);
        const QString adapter_lower = adapter_name.toLower();
        int score = 1000 - static_cast<int>(adapter_index);
        if (!preferred_vendor.isEmpty()) {
            bool vendor_match = false;
            if (preferred_vendor == QStringLiteral("nvidia")) {
                vendor_match = adapter_lower.contains(QStringLiteral("nvidia"));
            } else if (preferred_vendor == QStringLiteral("intel")) {
                vendor_match = adapter_lower.contains(QStringLiteral("intel"));
            } else if (preferred_vendor == QStringLiteral("amd")) {
                vendor_match = adapter_lower.contains(QStringLiteral("amd")) ||
                               adapter_lower.contains(QStringLiteral("radeon")) ||
                               adapter_lower.contains(QStringLiteral("advanced micro"));
            }
            score += vendor_match ? 10000 : -10000;
        }

        if (!device_ || score > best_score) {
            best_score = score;
            adapter_ = candidate;
            device_ = candidate_device;
            dxgi_adapter_name_ = adapter_name;
        }
    }

    if (!device_) {
        if (error_message) {
            *error_message = QStringLiteral("No Direct3D 12 feature level 12.1 hardware adapter was found.");
        }
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (failIfFailed(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)),
                     QStringLiteral("D3D12 raytracing feature query failed"),
                     error_message)) {
        return false;
    }
    raytracing_tier_ = options5.RaytracingTier;
    if (raytracing_tier_ == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
        if (error_message) {
            *error_message = QStringLiteral("This adapter does not expose a DXR raytracing tier.");
        }
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (failIfFailed(device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_)),
                     QStringLiteral("CreateCommandQueue failed"),
                     error_message) ||
        failIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator_)),
                     QStringLiteral("CreateCommandAllocator failed"),
                     error_message) ||
        failIfFailed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator_.Get(), nullptr, IID_PPV_ARGS(&command_list_)),
                     QStringLiteral("CreateCommandList failed"),
                     error_message)) {
        return false;
    }
    command_list_->Close();

    if (failIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
                     QStringLiteral("CreateFence failed"),
                     error_message)) {
        return false;
    }
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) {
        if (error_message) {
            *error_message = QStringLiteral("CreateEvent for D3D12 fence failed.");
        }
        return false;
    }
    return true;
}

bool DxrRayTracer::initializeDxr(QString* error_message) {
    return buildRootSignature(error_message) &&
        compileRayLibrary(error_message) &&
        buildStateObject(error_message) &&
        buildFallbackAccelerationStructures(error_message) &&
        buildOutputResources(error_message) &&
        buildDescriptorHeap(error_message) &&
        buildShaderTable(error_message);
}

bool DxrRayTracer::buildGlDisplay(QString* error_message) {
    display_program_ = new QOpenGLShaderProgram();
    if (!display_program_->addShaderFromSourceCode(QOpenGLShader::Vertex, glVertexShaderSource()) ||
        !display_program_->addShaderFromSourceCode(QOpenGLShader::Fragment, glFragmentShaderSource()) ||
        !display_program_->link()) {
        if (error_message) {
            *error_message = QStringLiteral("DXR OpenGL display shader failed: %1").arg(display_program_->log());
        }
        delete display_program_;
        display_program_ = nullptr;
        return false;
    }

    glGenVertexArrays(1, &fullscreen_vao_);
    glGenTextures(1, &gl_frame_texture_);
    glBindTexture(GL_TEXTURE_2D, gl_frame_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool DxrRayTracer::buildRootSignature(QString* error_message) {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 11;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;

    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 3;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 11;

    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    parameters[0].DescriptorTable.pDescriptorRanges = ranges;

    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.RegisterSpace = 0;
    parameters[1].Constants.Num32BitValues = sizeof(DxrFrameConstants) / sizeof(std::uint32_t);

    D3D12_ROOT_SIGNATURE_DESC signature_desc = {};
    signature_desc.NumParameters = static_cast<UINT>(std::size(parameters));
    signature_desc.pParameters = parameters;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    signature_desc.NumStaticSamplers = 1;
    signature_desc.pStaticSamplers = &sampler;
    signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> signature_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error_blob;
    HRESULT hr = D3D12SerializeRootSignature(&signature_desc,
                                             D3D_ROOT_SIGNATURE_VERSION_1,
                                             &signature_blob,
                                             &error_blob);
    if (FAILED(hr)) {
        if (error_message) {
            const char* details = error_blob ? static_cast<const char*>(error_blob->GetBufferPointer()) : "";
            *error_message = QStringLiteral("D3D12SerializeRootSignature failed: %1").arg(QString::fromLocal8Bit(details));
        }
        return false;
    }

    return !failIfFailed(device_->CreateRootSignature(0,
                                                       signature_blob->GetBufferPointer(),
                                                       signature_blob->GetBufferSize(),
                                                       IID_PPV_ARGS(&global_root_signature_)),
                         QStringLiteral("CreateRootSignature failed"),
                         error_message);
}

bool DxrRayTracer::compileRayLibrary(QString* error_message) {
    Microsoft::WRL::ComPtr<IDxcLibrary> library;
    Microsoft::WRL::ComPtr<IDxcCompiler> compiler;
    if (failIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library)),
                     QStringLiteral("DxcCreateInstance(DxcLibrary) failed"),
                     error_message) ||
        failIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)),
                     QStringLiteral("DxcCreateInstance(DxcCompiler) failed"),
                     error_message)) {
        return false;
    }

    const char* source = dxrShaderSource();
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> source_blob;
    if (failIfFailed(library->CreateBlobWithEncodingOnHeapCopy(source,
                                                               static_cast<UINT32>(std::strlen(source)),
                                                               DXC_CP_UTF8,
                                                               &source_blob),
                     QStringLiteral("DXC source blob creation failed"),
                     error_message)) {
        return false;
    }

    const wchar_t* arguments[] = {
        L"-Qstrip_debug",
        L"-Qstrip_reflect"
    };

    Microsoft::WRL::ComPtr<IDxcOperationResult> result;
    if (failIfFailed(compiler->Compile(source_blob.Get(),
                                       L"haorender_gi_minimal_dxr.hlsl",
                                       L"",
                                       L"lib_6_3",
                                       arguments,
                                       static_cast<UINT32>(std::size(arguments)),
                                       nullptr,
                                       0,
                                       nullptr,
                                       &result),
                     QStringLiteral("DXC Compile failed"),
                     error_message)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IDxcBlobEncoding> errors;
    result->GetErrorBuffer(&errors);
    HRESULT status = S_OK;
    result->GetStatus(&status);
    if (FAILED(status)) {
        if (error_message) {
            *error_message = QStringLiteral("DXR shader compile failed: %1")
                .arg(errors && errors->GetBufferSize() > 0
                    ? QString::fromUtf8(static_cast<const char*>(errors->GetBufferPointer()), static_cast<int>(errors->GetBufferSize()))
                    : hresultMessage(QStringLiteral("unknown DXC error"), status));
        }
        return false;
    }

    return !failIfFailed(result->GetResult(&ray_library_),
                         QStringLiteral("DXC object extraction failed"),
                         error_message);
}

bool DxrRayTracer::buildStateObject(QString* error_message) {
    D3D12_EXPORT_DESC exports[5] = {};
    exports[0].Name = kRayGenShader;
    exports[1].Name = kMissShader;
    exports[2].Name = kClosestHitShader;
    exports[3].Name = kShadowMissShader;
    exports[4].Name = kShadowClosestHitShader;

    D3D12_DXIL_LIBRARY_DESC dxil_library_desc = {};
    dxil_library_desc.DXILLibrary.pShaderBytecode = ray_library_->GetBufferPointer();
    dxil_library_desc.DXILLibrary.BytecodeLength = ray_library_->GetBufferSize();
    dxil_library_desc.NumExports = static_cast<UINT>(std::size(exports));
    dxil_library_desc.pExports = exports;

    D3D12_HIT_GROUP_DESC hit_group_descs[2] = {};
    hit_group_descs[0].HitGroupExport = kHitGroup;
    hit_group_descs[0].ClosestHitShaderImport = kClosestHitShader;
    hit_group_descs[0].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hit_group_descs[1].HitGroupExport = kShadowHitGroup;
    hit_group_descs[1].ClosestHitShaderImport = kShadowClosestHitShader;
    hit_group_descs[1].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

    D3D12_RAYTRACING_SHADER_CONFIG shader_config = {};
    shader_config.MaxPayloadSizeInBytes = 64;
    shader_config.MaxAttributeSizeInBytes = sizeof(float) * 2;

    D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature = {};
    global_root_signature.pGlobalRootSignature = global_root_signature_.Get();

    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = {};
    pipeline_config.MaxTraceRecursionDepth = 1;

    D3D12_STATE_SUBOBJECT subobjects[6] = {};
    subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[0].pDesc = &dxil_library_desc;
    subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[1].pDesc = &hit_group_descs[0];
    subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[2].pDesc = &hit_group_descs[1];
    subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[3].pDesc = &shader_config;
    subobjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[4].pDesc = &global_root_signature;
    subobjects[5].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[5].pDesc = &pipeline_config;

    D3D12_STATE_OBJECT_DESC state_object_desc = {};
    state_object_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    state_object_desc.NumSubobjects = static_cast<UINT>(std::size(subobjects));
    state_object_desc.pSubobjects = subobjects;

    if (failIfFailed(device_->CreateStateObject(&state_object_desc, IID_PPV_ARGS(&state_object_)),
                     QStringLiteral("CreateStateObject(DXR RTPSO) failed"),
                     error_message) ||
        failIfFailed(state_object_->QueryInterface(IID_PPV_ARGS(&state_object_properties_)),
                     QStringLiteral("Query DXR state object properties failed"),
                     error_message)) {
        return false;
    }
    return true;
}

bool DxrRayTracer::buildFallbackAccelerationStructures(QString* error_message) {
    const std::vector<DxrVertex> vertices = {
        { -1.0f, -0.85f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0 },
        {  1.0f, -0.85f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0 },
        {  0.0f,  0.95f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0 }
    };
    const std::vector<std::uint32_t> indices = { 0, 1, 2 };
    const std::vector<std::uint32_t> primitive_materials = { 0 };
    std::vector<DxrMaterial> materials(1);
    materials[0].base_color[0] = 0.85f;
    materials[0].base_color[1] = 0.65f;
    materials[0].base_color[2] = 0.25f;
    DxrTextureAtlases atlases;
    return buildAccelerationStructures(vertices, indices, primitive_materials, materials, atlases, error_message);
}

bool DxrRayTracer::buildCornellScene(QString* error_message) {
    std::vector<DxrVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> primitive_materials;
    std::vector<DxrMaterial> materials;

    const auto add_material = [&materials](const Eigen::Vector3f& base_color,
                                           float metallic,
                                           float roughness,
                                           float material_type,
                                           const Eigen::Vector3f& emissive = Eigen::Vector3f::Zero()) {
        DxrMaterial material;
        material.base_color[0] = base_color.x();
        material.base_color[1] = base_color.y();
        material.base_color[2] = base_color.z();
        material.base_color[3] = 1.0f;
        material.emissive[0] = emissive.x();
        material.emissive[1] = emissive.y();
        material.emissive[2] = emissive.z();
        material.emissive[3] = 1.0f;
        material.params[0] = metallic;
        material.params[1] = roughness;
        material.params[2] = material_type;
        material.params[3] = 0.0f;
        materials.push_back(material);
        return static_cast<std::uint32_t>(materials.size() - 1);
    };

    const std::uint32_t white = add_material(Eigen::Vector3f(0.78f, 0.76f, 0.70f), 0.0f, 0.72f, 0.0f);
    const std::uint32_t red = add_material(Eigen::Vector3f(0.78f, 0.12f, 0.08f), 0.0f, 0.78f, 0.0f);
    const std::uint32_t green = add_material(Eigen::Vector3f(0.10f, 0.52f, 0.18f), 0.0f, 0.78f, 0.0f);
    const std::uint32_t light = add_material(Eigen::Vector3f(1.0f, 0.92f, 0.72f), 0.0f, 0.4f, 0.0f, Eigen::Vector3f(16.0f, 14.5f, 10.5f));
    const std::uint32_t metal = add_material(Eigen::Vector3f(0.95f, 0.78f, 0.48f), 1.0f, 0.16f, 1.0f);
    const std::uint32_t glass = add_material(Eigen::Vector3f(0.82f, 0.94f, 1.0f), 0.0f, 0.02f, 2.0f);

    const auto add_vertex = [&vertices](const Eigen::Vector3f& p,
                                        const Eigen::Vector3f& n,
                                        const Eigen::Vector2f& uv,
                                        std::uint32_t material_index) {
        Eigen::Vector3f helper = std::abs(n.y()) < 0.95f ? Eigen::Vector3f(0.0f, 1.0f, 0.0f) : Eigen::Vector3f(1.0f, 0.0f, 0.0f);
        Eigen::Vector3f tangent = helper.cross(n);
        if (tangent.norm() < 1e-5f) {
            tangent = Eigen::Vector3f(1.0f, 0.0f, 0.0f);
        } else {
            tangent.normalize();
        }
        vertices.push_back({
            p.x(), p.y(), p.z(),
            n.x(), n.y(), n.z(),
            tangent.x(), tangent.y(), tangent.z(),
            uv.x(), uv.y(),
            material_index
        });
        return static_cast<std::uint32_t>(vertices.size() - 1);
    };

    const auto add_quad = [&](const Eigen::Vector3f& a,
                              const Eigen::Vector3f& b,
                              const Eigen::Vector3f& c,
                              const Eigen::Vector3f& d,
                              const Eigen::Vector3f& normal,
                              std::uint32_t material_index) {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        add_vertex(a, normal, Eigen::Vector2f(0.0f, 0.0f), material_index);
        add_vertex(b, normal, Eigen::Vector2f(1.0f, 0.0f), material_index);
        add_vertex(c, normal, Eigen::Vector2f(1.0f, 1.0f), material_index);
        add_vertex(d, normal, Eigen::Vector2f(0.0f, 1.0f), material_index);
        indices.insert(indices.end(), { base + 0, base + 1, base + 2, base + 0, base + 2, base + 3 });
        primitive_materials.push_back(material_index);
        primitive_materials.push_back(material_index);
    };

    constexpr float x0 = -1.0f;
    constexpr float x1 = 1.0f;
    constexpr float y0 = -1.0f;
    constexpr float y1 = 1.0f;
    constexpr float z0 = -1.2f;
    constexpr float z1 = 1.05f;

    add_quad(Eigen::Vector3f(x0, y0, z1), Eigen::Vector3f(x1, y0, z1), Eigen::Vector3f(x1, y0, z0), Eigen::Vector3f(x0, y0, z0), Eigen::Vector3f(0.0f, 1.0f, 0.0f), white);
    add_quad(Eigen::Vector3f(x0, y1, z0), Eigen::Vector3f(x1, y1, z0), Eigen::Vector3f(x1, y1, z1), Eigen::Vector3f(x0, y1, z1), Eigen::Vector3f(0.0f, -1.0f, 0.0f), white);
    add_quad(Eigen::Vector3f(x0, y0, z0), Eigen::Vector3f(x1, y0, z0), Eigen::Vector3f(x1, y1, z0), Eigen::Vector3f(x0, y1, z0), Eigen::Vector3f(0.0f, 0.0f, 1.0f), white);
    add_quad(Eigen::Vector3f(x0, y0, z1), Eigen::Vector3f(x0, y0, z0), Eigen::Vector3f(x0, y1, z0), Eigen::Vector3f(x0, y1, z1), Eigen::Vector3f(1.0f, 0.0f, 0.0f), red);
    add_quad(Eigen::Vector3f(x1, y0, z0), Eigen::Vector3f(x1, y0, z1), Eigen::Vector3f(x1, y1, z1), Eigen::Vector3f(x1, y1, z0), Eigen::Vector3f(-1.0f, 0.0f, 0.0f), green);
    add_quad(Eigen::Vector3f(-0.34f, 0.985f, -0.72f), Eigen::Vector3f(0.34f, 0.985f, -0.72f), Eigen::Vector3f(0.34f, 0.985f, -0.04f), Eigen::Vector3f(-0.34f, 0.985f, -0.04f), Eigen::Vector3f(0.0f, -1.0f, 0.0f), light);

    const auto add_sphere = [&](const Eigen::Vector3f& center,
                                float radius,
                                std::uint32_t material_index,
                                int rings,
                                int segments) {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        for (int y = 0; y <= rings; ++y) {
            const float v = static_cast<float>(y) / static_cast<float>(rings);
            const float theta = v * 3.1415926535f;
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);
            for (int x = 0; x <= segments; ++x) {
                const float u = static_cast<float>(x) / static_cast<float>(segments);
                const float phi = u * 6.283185307f;
                Eigen::Vector3f normal(std::cos(phi) * sin_theta, cos_theta, std::sin(phi) * sin_theta);
                if (normal.norm() < 1e-5f) {
                    normal = Eigen::Vector3f(0.0f, cos_theta >= 0.0f ? 1.0f : -1.0f, 0.0f);
                } else {
                    normal.normalize();
                }
                const Eigen::Vector3f p = center + normal * radius;
                add_vertex(p, normal, Eigen::Vector2f(u, v), material_index);
            }
        }

        for (int y = 0; y < rings; ++y) {
            for (int x = 0; x < segments; ++x) {
                const std::uint32_t i0 = base + static_cast<std::uint32_t>(y * (segments + 1) + x);
                const std::uint32_t i1 = i0 + 1;
                const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(segments + 1);
                const std::uint32_t i3 = i2 + 1;
                indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });
                primitive_materials.push_back(material_index);
                primitive_materials.push_back(material_index);
            }
        }
    };

    add_sphere(Eigen::Vector3f(-0.42f, -0.66f, -0.43f), 0.34f, metal, 32, 48);
    add_sphere(Eigen::Vector3f(0.42f, -0.69f, 0.08f), 0.31f, glass, 32, 48);

    DxrTextureAtlases atlases;
    const bool ok = buildAccelerationStructures(vertices, indices, primitive_materials, materials, atlases, error_message);
    if (ok) {
        scene_buffers_are_cornell_ = true;
        resetAccumulation();
    }
    return ok;
}

bool DxrRayTracer::rebuildLoadedScene(QString* error_message) {
    if (!has_cached_scene_ || cached_scene_.empty()) {
        return buildFallbackAccelerationStructures(error_message);
    }

    std::vector<DxrVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> primitive_materials;
    std::vector<DxrMaterial> materials;
    DxrTextureAtlases texture_atlases;
    flattenScene(cached_scene_, &vertices, &indices, &primitive_materials, &materials, &texture_atlases);
    const bool ok = buildAccelerationStructures(vertices, indices, primitive_materials, materials, texture_atlases, error_message);
    if (ok) {
        scene_buffers_are_cornell_ = false;
        resetAccumulation();
    }
    return ok;
}

void DxrRayTracer::flattenScene(const SceneModel& scene,
                                std::vector<DxrVertex>* vertices,
                                std::vector<std::uint32_t>* indices,
                                std::vector<std::uint32_t>* primitive_materials,
                                std::vector<DxrMaterial>* materials,
                                DxrTextureAtlases* texture_atlases) const {
    vertices->clear();
    indices->clear();
    primitive_materials->clear();
    materials->clear();
    *texture_atlases = DxrTextureAtlases{};
    if (scene.empty()) {
        return;
    }

    Eigen::Matrix4f model_matrix = Eigen::Matrix4f::Identity();
    if (scene.bounds.valid()) {
        const Eigen::Vector3f center = scene.bounds.center();
        const Eigen::Vector3f extent = scene.bounds.extent();
        const float max_extent = std::max({ extent.x(), extent.y(), extent.z(), 1e-4f });
        const float scale = 2.0f / max_extent;
        model_matrix(0, 0) = scale;
        model_matrix(1, 1) = scale;
        model_matrix(2, 2) = scale;
        model_matrix(0, 3) = -center.x() * scale;
        model_matrix(1, 3) = -center.y() * scale;
        model_matrix(2, 3) = -center.z() * scale;
    }
    const Eigen::Matrix3f normal_matrix = model_matrix.block<3, 3>(0, 0).inverse().transpose();

    std::uint32_t vertex_offset = 0;
    struct PendingTexture {
        std::uint32_t material_index = 0;
        QImage image;
        float* rect = nullptr;
        float flag = 0.0f;
    };
    std::vector<PendingTexture> pending_base_color_textures;
    std::vector<PendingTexture> pending_normal_textures;
    std::vector<PendingTexture> pending_metallic_textures;
    std::vector<PendingTexture> pending_roughness_textures;
    std::vector<PendingTexture> pending_ao_textures;
    std::vector<PendingTexture> pending_emissive_textures;

    for (const MeshData& mesh : scene.meshes) {
        if (mesh.vertices.empty() || mesh.indices.size() < 3) {
            continue;
        }
        if (vertices->size() + mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
            break;
        }

        DxrMaterial material;
        material.base_color[0] = mesh.material.base_color_factor.x();
        material.base_color[1] = mesh.material.base_color_factor.y();
        material.base_color[2] = mesh.material.base_color_factor.z();
        material.base_color[3] = 1.0f;
        material.emissive[0] = mesh.material.emissive_factor.x();
        material.emissive[1] = mesh.material.emissive_factor.y();
        material.emissive[2] = mesh.material.emissive_factor.z();
        material.emissive[3] = std::clamp(mesh.material.ao_factor, 0.0f, 1.0f);
        material.params[0] = std::clamp(mesh.material.metallic_factor, 0.0f, 1.0f);
        material.params[1] = std::clamp(mesh.material.roughness_factor, 0.035f, 1.0f);
        material.params[2] = 0.0f;
        material.params[3] = 0.0f;

        const TextureData* color_texture = nullptr;
        if (mesh.material.base_color_texture.valid()) {
            color_texture = &mesh.material.base_color_texture;
        } else if (mesh.material.diffuse_texture.valid()) {
            color_texture = &mesh.material.diffuse_texture;
        }

        const std::uint32_t material_index = static_cast<std::uint32_t>(materials->size());
        if (color_texture) {
            material.params[3] += 1.0f;
            pending_base_color_textures.push_back({ material_index, color_texture->image.convertToFormat(QImage::Format_RGBA8888), material.base_color_rect, 1.0f });
        }
        if (mesh.material.normal_texture.valid()) {
            material.params[3] += 2.0f;
            pending_normal_textures.push_back({ material_index, mesh.material.normal_texture.image.convertToFormat(QImage::Format_RGBA8888), material.normal_rect, 2.0f });
        }
        if (mesh.material.metallic_texture.valid()) {
            material.params[3] += 4.0f;
            pending_metallic_textures.push_back({ material_index, mesh.material.metallic_texture.image.convertToFormat(QImage::Format_RGBA8888), material.metallic_rect, 4.0f });
        }
        if (mesh.material.roughness_texture.valid()) {
            material.params[3] += 8.0f;
            pending_roughness_textures.push_back({ material_index, mesh.material.roughness_texture.image.convertToFormat(QImage::Format_RGBA8888), material.roughness_rect, 8.0f });
        }
        if (mesh.material.ao_texture.valid()) {
            material.params[3] += 16.0f;
            pending_ao_textures.push_back({ material_index, mesh.material.ao_texture.image.convertToFormat(QImage::Format_RGBA8888), material.ao_rect, 16.0f });
        }
        if (mesh.material.emissive_texture.valid()) {
            material.params[3] += 32.0f;
            pending_emissive_textures.push_back({ material_index, mesh.material.emissive_texture.image.convertToFormat(QImage::Format_RGBA8888), material.emissive_rect, 32.0f });
        }
        materials->push_back(material);

        for (const Vertex& vertex : mesh.vertices) {
            const Eigen::Vector4f local(vertex.position.x(), vertex.position.y(), vertex.position.z(), 1.0f);
            const Eigen::Vector3f p = (model_matrix * local).hnormalized();
            Eigen::Vector3f n = normal_matrix * vertex.normal;
            if (n.norm() < 1e-5f) {
                n = Eigen::Vector3f(0.0f, 1.0f, 0.0f);
            } else {
                n.normalize();
            }
            Eigen::Vector3f tangent = normal_matrix * vertex.tangent;
            if (tangent.norm() < 1e-5f) {
                Eigen::Vector3f helper = std::abs(n.y()) < 0.95f ? Eigen::Vector3f(0.0f, 1.0f, 0.0f) : Eigen::Vector3f(1.0f, 0.0f, 0.0f);
                tangent = helper.cross(n);
            }
            if (tangent.norm() < 1e-5f) {
                tangent = Eigen::Vector3f(1.0f, 0.0f, 0.0f);
            } else {
                tangent.normalize();
            }
            vertices->push_back({
                p.x(), p.y(), p.z(),
                n.x(), n.y(), n.z(),
                tangent.x(), tangent.y(), tangent.z(),
                vertex.uv.x(), vertex.uv.y(),
                material_index
            });
        }

        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            indices->push_back(vertex_offset + mesh.indices[i + 0]);
            indices->push_back(vertex_offset + mesh.indices[i + 1]);
            indices->push_back(vertex_offset + mesh.indices[i + 2]);
            primitive_materials->push_back(material_index);
        }
        vertex_offset += static_cast<std::uint32_t>(mesh.vertices.size());
    }

    if (materials->empty()) {
        materials->push_back(DxrMaterial{});
    }

    const auto build_atlas = [materials](const std::vector<PendingTexture>& pending_textures,
                                         QImage* atlas,
                                         auto rect_selector) {
        if (pending_textures.empty()) {
            return;
        }

        constexpr int tile_size = 256;
        const int atlas_columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(pending_textures.size()))));
        const int atlas_rows = static_cast<int>(std::ceil(static_cast<double>(pending_textures.size()) / std::max(1, atlas_columns)));
        *atlas = QImage(std::max(1, atlas_columns * tile_size),
                        std::max(1, atlas_rows * tile_size),
                        QImage::Format_RGBA8888);
        atlas->fill(QColor(255, 255, 255, 255));
        QPainter painter(atlas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (std::size_t i = 0; i < pending_textures.size(); ++i) {
            const int col = static_cast<int>(i) % atlas_columns;
            const int row = static_cast<int>(i) / atlas_columns;
            const QRect target(col * tile_size, row * tile_size, tile_size, tile_size);
            painter.drawImage(target, pending_textures[i].image);
            DxrMaterial& material = (*materials)[pending_textures[i].material_index];
            float* rect = rect_selector(material);
            rect[0] = static_cast<float>(target.x()) / static_cast<float>(atlas->width());
            rect[1] = static_cast<float>(target.y()) / static_cast<float>(atlas->height());
            rect[2] = static_cast<float>(target.width()) / static_cast<float>(atlas->width());
            rect[3] = static_cast<float>(target.height()) / static_cast<float>(atlas->height());
        }
    };

    build_atlas(pending_base_color_textures, &texture_atlases->base_color, [](DxrMaterial& material) { return material.base_color_rect; });
    build_atlas(pending_normal_textures, &texture_atlases->normal, [](DxrMaterial& material) { return material.normal_rect; });
    build_atlas(pending_metallic_textures, &texture_atlases->metallic, [](DxrMaterial& material) { return material.metallic_rect; });
    build_atlas(pending_roughness_textures, &texture_atlases->roughness, [](DxrMaterial& material) { return material.roughness_rect; });
    build_atlas(pending_ao_textures, &texture_atlases->ao, [](DxrMaterial& material) { return material.ao_rect; });
    build_atlas(pending_emissive_textures, &texture_atlases->emissive, [](DxrMaterial& material) { return material.emissive_rect; });
}

bool DxrRayTracer::buildAccelerationStructures(const std::vector<DxrVertex>& vertices,
                                               const std::vector<std::uint32_t>& indices,
                                               const std::vector<std::uint32_t>& primitive_materials,
                                               const std::vector<DxrMaterial>& materials,
                                               const DxrTextureAtlases& texture_atlases,
                                               QString* error_message) {
    if (vertices.empty() || indices.size() < 3) {
        if (error_message) {
            *error_message = QStringLiteral("DXR scene geometry is empty.");
        }
        return false;
    }

    const std::size_t aligned_index_count = indices.size() - (indices.size() % 3);
    scene_vertex_count_ = static_cast<std::uint32_t>(vertices.size());
    scene_index_count_ = static_cast<std::uint32_t>(aligned_index_count);
    scene_triangle_count_ = scene_index_count_ / 3;
    scene_material_count_ = static_cast<std::uint32_t>(std::max<std::size_t>(materials.size(), 1));

    if (!createBuffer(static_cast<std::uint64_t>(vertices.size() * sizeof(DxrVertex)),
                      D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ,
                      &vertex_buffer_,
                      error_message)) {
        return false;
    }

    void* vertex_data = nullptr;
    D3D12_RANGE empty_range = { 0, 0 };
    vertex_buffer_->Map(0, &empty_range, &vertex_data);
    std::memcpy(vertex_data, vertices.data(), vertices.size() * sizeof(DxrVertex));
    vertex_buffer_->Unmap(0, nullptr);

    if (!createBuffer(static_cast<std::uint64_t>(aligned_index_count * sizeof(std::uint32_t)),
                      D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ,
                      &index_buffer_,
                      error_message)) {
        return false;
    }

    void* index_data = nullptr;
    index_buffer_->Map(0, &empty_range, &index_data);
    std::memcpy(index_data, indices.data(), aligned_index_count * sizeof(std::uint32_t));
    index_buffer_->Unmap(0, nullptr);

    std::vector<std::uint32_t> primitive_material_upload(scene_triangle_count_, 0);
    for (std::size_t i = 0; i < primitive_material_upload.size() && i < primitive_materials.size(); ++i) {
        primitive_material_upload[i] = std::min<std::uint32_t>(primitive_materials[i], scene_material_count_ - 1);
    }
    if (!createBuffer(static_cast<std::uint64_t>(primitive_material_upload.size() * sizeof(std::uint32_t)),
                      D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ,
                      &primitive_material_buffer_,
                      error_message)) {
        return false;
    }
    void* primitive_material_data = nullptr;
    primitive_material_buffer_->Map(0, &empty_range, &primitive_material_data);
    std::memcpy(primitive_material_data, primitive_material_upload.data(), primitive_material_upload.size() * sizeof(std::uint32_t));
    primitive_material_buffer_->Unmap(0, nullptr);

    const std::vector<DxrMaterial> material_upload = materials.empty() ? std::vector<DxrMaterial>{ DxrMaterial{} } : materials;
    if (!createBuffer(static_cast<std::uint64_t>(material_upload.size() * sizeof(DxrMaterial)),
                      D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ,
                      &material_buffer_,
                      error_message)) {
        return false;
    }
    void* material_data = nullptr;
    material_buffer_->Map(0, &empty_range, &material_data);
    std::memcpy(material_data, material_upload.data(), material_upload.size() * sizeof(DxrMaterial));
    material_buffer_->Unmap(0, nullptr);

    if (!uploadTextureAtlases(texture_atlases, error_message)) {
        return false;
    }

    D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc = {};
    geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry_desc.Triangles.VertexBuffer.StartAddress = vertex_buffer_->GetGPUVirtualAddress();
    geometry_desc.Triangles.VertexBuffer.StrideInBytes = sizeof(DxrVertex);
    geometry_desc.Triangles.VertexCount = scene_vertex_count_;
    geometry_desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry_desc.Triangles.IndexBuffer = index_buffer_->GetGPUVirtualAddress();
    geometry_desc.Triangles.IndexCount = scene_index_count_;
    geometry_desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs = {};
    blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blas_inputs.NumDescs = 1;
    blas_inputs.pGeometryDescs = &geometry_desc;
    blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_info = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &blas_info);
    if (blas_info.ResultDataMaxSizeInBytes == 0) {
        if (error_message) {
            *error_message = QStringLiteral("DXR BLAS prebuild info returned zero size.");
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> blas_scratch;
    if (!createBuffer(alignTo(blas_info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                      D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      &blas_scratch,
                      error_message) ||
        !createBuffer(alignTo(blas_info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                      D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                      &blas_,
                      error_message)) {
        return false;
    }

    D3D12_RAYTRACING_INSTANCE_DESC instance_desc = {};
    instance_desc.Transform[0][0] = 1.0f;
    instance_desc.Transform[1][1] = 1.0f;
    instance_desc.Transform[2][2] = 1.0f;
    instance_desc.InstanceMask = 0xFF;
    instance_desc.InstanceContributionToHitGroupIndex = 0;
    instance_desc.AccelerationStructure = blas_->GetGPUVirtualAddress();

    if (!createBuffer(sizeof(instance_desc),
                      D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ,
                      &instance_desc_buffer_,
                      error_message)) {
        return false;
    }

    void* instance_data = nullptr;
    instance_desc_buffer_->Map(0, &empty_range, &instance_data);
    std::memcpy(instance_data, &instance_desc, sizeof(instance_desc));
    instance_desc_buffer_->Unmap(0, nullptr);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs = {};
    tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlas_inputs.NumDescs = 1;
    tlas_inputs.InstanceDescs = instance_desc_buffer_->GetGPUVirtualAddress();
    tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info = {};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_info);
    if (tlas_info.ResultDataMaxSizeInBytes == 0) {
        if (error_message) {
            *error_message = QStringLiteral("DXR TLAS prebuild info returned zero size.");
        }
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> tlas_scratch;
    if (!createBuffer(alignTo(tlas_info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                      D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      &tlas_scratch,
                      error_message) ||
        !createBuffer(alignTo(tlas_info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                      D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                      &tlas_,
                      error_message)) {
        return false;
    }

    if (!resetCommandList(error_message)) {
        return false;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build_desc = {};
    blas_build_desc.Inputs = blas_inputs;
    blas_build_desc.ScratchAccelerationStructureData = blas_scratch->GetGPUVirtualAddress();
    blas_build_desc.DestAccelerationStructureData = blas_->GetGPUVirtualAddress();
    command_list_->BuildRaytracingAccelerationStructure(&blas_build_desc, 0, nullptr);
    D3D12_RESOURCE_BARRIER blas_barrier = uavBarrier(blas_.Get());
    command_list_->ResourceBarrier(1, &blas_barrier);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build_desc = {};
    tlas_build_desc.Inputs = tlas_inputs;
    tlas_build_desc.ScratchAccelerationStructureData = tlas_scratch->GetGPUVirtualAddress();
    tlas_build_desc.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress();
    command_list_->BuildRaytracingAccelerationStructure(&tlas_build_desc, 0, nullptr);
    D3D12_RESOURCE_BARRIER tlas_barrier = uavBarrier(tlas_.Get());
    command_list_->ResourceBarrier(1, &tlas_barrier);

    return executeCommandList(error_message);
}

bool DxrRayTracer::uploadTextureAtlas(const QImage& atlas,
                                      const QColor& fallback_color,
                                      const QString& label,
                                      Microsoft::WRL::ComPtr<ID3D12Resource>* target,
                                      QString* error_message) {
    QImage image = atlas.isNull() ? QImage(1, 1, QImage::Format_RGBA8888) : atlas.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_RGBA8888);
    }
    if (image.width() == 1 && image.height() == 1) {
        image.fill(fallback_color);
    }

    target->Reset();

    D3D12_HEAP_PROPERTIES default_heap = {};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texture_desc = {};
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = static_cast<UINT64>(image.width());
    texture_desc.Height = static_cast<UINT>(image.height());
    texture_desc.DepthOrArraySize = 1;
    texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texture_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (failIfFailed(device_->CreateCommittedResource(&default_heap,
                                                       D3D12_HEAP_FLAG_NONE,
                                                       &texture_desc,
                                                       D3D12_RESOURCE_STATE_COPY_DEST,
                                                       nullptr,
                                                       IID_PPV_ARGS(target->ReleaseAndGetAddressOf())),
                     QStringLiteral("Create DXR %1 atlas failed").arg(label),
                     error_message)) {
        return false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT num_rows = 0;
    UINT64 row_size = 0;
    UINT64 upload_size = 0;
    device_->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, &num_rows, &row_size, &upload_size);

    Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer;
    if (!createBuffer(upload_size,
                      D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ,
                      &upload_buffer,
                      error_message)) {
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE empty_range = { 0, 0 };
    upload_buffer->Map(0, &empty_range, &mapped);
    auto* dst = static_cast<std::uint8_t*>(mapped);
    for (int y = 0; y < image.height(); ++y) {
        std::memcpy(dst + footprint.Offset + static_cast<std::uint64_t>(y) * footprint.Footprint.RowPitch,
                    image.constScanLine(y),
                    static_cast<std::size_t>(image.width()) * 4);
    }
    upload_buffer->Unmap(0, nullptr);

    if (!resetCommandList(error_message)) {
        return false;
    }

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = upload_buffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION dst_location = {};
    dst_location.pResource = target->Get();
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_location.SubresourceIndex = 0;
    command_list_->CopyTextureRegion(&dst_location, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = transitionBarrier(target->Get(),
                                                       D3D12_RESOURCE_STATE_COPY_DEST,
                                                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    command_list_->ResourceBarrier(1, &barrier);
    return executeCommandList(error_message);
}

bool DxrRayTracer::uploadTextureAtlases(const DxrTextureAtlases& atlases, QString* error_message) {
    return uploadTextureAtlas(atlases.base_color, QColor(255, 255, 255, 255), QStringLiteral("base color"), &base_color_atlas_, error_message) &&
           uploadTextureAtlas(atlases.normal, QColor(128, 128, 255, 255), QStringLiteral("normal"), &normal_atlas_, error_message) &&
           uploadTextureAtlas(atlases.metallic, QColor(255, 255, 255, 255), QStringLiteral("metallic"), &metallic_atlas_, error_message) &&
           uploadTextureAtlas(atlases.roughness, QColor(255, 255, 255, 255), QStringLiteral("roughness"), &roughness_atlas_, error_message) &&
           uploadTextureAtlas(atlases.ao, QColor(255, 255, 255, 255), QStringLiteral("ao"), &ao_atlas_, error_message) &&
           uploadTextureAtlas(atlases.emissive, QColor(255, 255, 255, 255), QStringLiteral("emissive"), &emissive_atlas_, error_message);
}

bool DxrRayTracer::buildOutputResources(QString* error_message) {
    destroyGpuInterop();
    output_texture_.Reset();
    accumulation_texture_.Reset();
    moment_texture_.Reset();
    descriptor_heap_.Reset();

    D3D12_HEAP_PROPERTIES default_heap = {};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texture_desc = {};
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = static_cast<UINT64>(framebuffer_width_);
    texture_desc.Height = static_cast<UINT>(framebuffer_height_);
    texture_desc.DepthOrArraySize = 1;
    texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_RESOURCE_DESC accumulation_desc = texture_desc;
    accumulation_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    if (failIfFailed(device_->CreateCommittedResource(&default_heap,
                                                       D3D12_HEAP_FLAG_NONE,
                                                       &accumulation_desc,
                                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                       nullptr,
                                                       IID_PPV_ARGS(&accumulation_texture_)),
                     QStringLiteral("Create DXR accumulation texture failed"),
                     error_message)) {
        return false;
    }

    D3D12_RESOURCE_DESC moment_desc = texture_desc;
    moment_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
    if (failIfFailed(device_->CreateCommittedResource(&default_heap,
                                                       D3D12_HEAP_FLAG_NONE,
                                                       &moment_desc,
                                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                       nullptr,
                                                       IID_PPV_ARGS(&moment_texture_)),
                     QStringLiteral("Create DXR moment texture failed"),
                     error_message)) {
        return false;
    }
    resetAccumulation();

    return initializeGpuInterop(error_message);
}

bool DxrRayTracer::buildDescriptorHeap(QString* error_message) {
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 14;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (failIfFailed(device_->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&descriptor_heap_)),
                     QStringLiteral("CreateDescriptorHeap failed"),
                     error_message)) {
        return false;
    }

    const UINT descriptor_size = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptor_heap_->GetCPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC as_srv = {};
    as_srv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    as_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    as_srv.RaytracingAccelerationStructure.Location = tlas_->GetGPUVirtualAddress();
    device_->CreateShaderResourceView(nullptr, &as_srv, handle);

    handle.ptr += descriptor_size;
    D3D12_SHADER_RESOURCE_VIEW_DESC position_srv = {};
    position_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    position_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    position_srv.Format = DXGI_FORMAT_UNKNOWN;
    position_srv.Buffer.FirstElement = 0;
    position_srv.Buffer.NumElements = scene_vertex_count_;
    position_srv.Buffer.StructureByteStride = sizeof(DxrVertex);
    device_->CreateShaderResourceView(vertex_buffer_.Get(), &position_srv, handle);

    handle.ptr += descriptor_size;
    D3D12_SHADER_RESOURCE_VIEW_DESC index_srv = {};
    index_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    index_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    index_srv.Format = DXGI_FORMAT_UNKNOWN;
    index_srv.Buffer.FirstElement = 0;
    index_srv.Buffer.NumElements = scene_index_count_;
    index_srv.Buffer.StructureByteStride = sizeof(std::uint32_t);
    device_->CreateShaderResourceView(index_buffer_.Get(), &index_srv, handle);

    handle.ptr += descriptor_size;
    D3D12_SHADER_RESOURCE_VIEW_DESC material_srv = {};
    material_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    material_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    material_srv.Format = DXGI_FORMAT_UNKNOWN;
    material_srv.Buffer.FirstElement = 0;
    material_srv.Buffer.NumElements = scene_material_count_;
    material_srv.Buffer.StructureByteStride = sizeof(DxrMaterial);
    device_->CreateShaderResourceView(material_buffer_.Get(), &material_srv, handle);

    handle.ptr += descriptor_size;
    D3D12_SHADER_RESOURCE_VIEW_DESC primitive_material_srv = {};
    primitive_material_srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    primitive_material_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    primitive_material_srv.Format = DXGI_FORMAT_UNKNOWN;
    primitive_material_srv.Buffer.FirstElement = 0;
    primitive_material_srv.Buffer.NumElements = scene_triangle_count_;
    primitive_material_srv.Buffer.StructureByteStride = sizeof(std::uint32_t);
    device_->CreateShaderResourceView(primitive_material_buffer_.Get(), &primitive_material_srv, handle);

    handle.ptr += descriptor_size;
    D3D12_SHADER_RESOURCE_VIEW_DESC atlas_srv = {};
    atlas_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    atlas_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    atlas_srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    atlas_srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(base_color_atlas_.Get(), &atlas_srv, handle);

    handle.ptr += descriptor_size;
    device_->CreateShaderResourceView(normal_atlas_.Get(), &atlas_srv, handle);

    handle.ptr += descriptor_size;
    device_->CreateShaderResourceView(metallic_atlas_.Get(), &atlas_srv, handle);

    handle.ptr += descriptor_size;
    device_->CreateShaderResourceView(roughness_atlas_.Get(), &atlas_srv, handle);

    handle.ptr += descriptor_size;
    device_->CreateShaderResourceView(ao_atlas_.Get(), &atlas_srv, handle);

    handle.ptr += descriptor_size;
    device_->CreateShaderResourceView(emissive_atlas_.Get(), &atlas_srv, handle);

    handle.ptr += descriptor_size;
    D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
    output_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    output_uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    device_->CreateUnorderedAccessView(output_texture_.Get(), nullptr, &output_uav, handle);

    handle.ptr += descriptor_size;
    D3D12_UNORDERED_ACCESS_VIEW_DESC accumulation_uav = {};
    accumulation_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    accumulation_uav.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    device_->CreateUnorderedAccessView(accumulation_texture_.Get(), nullptr, &accumulation_uav, handle);

    handle.ptr += descriptor_size;
    D3D12_UNORDERED_ACCESS_VIEW_DESC moment_uav = {};
    moment_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    moment_uav.Format = DXGI_FORMAT_R32G32_FLOAT;
    device_->CreateUnorderedAccessView(moment_texture_.Get(), nullptr, &moment_uav, handle);
    return true;
}

bool DxrRayTracer::buildShaderTable(QString* error_message) {
    const void* raygen_id = state_object_properties_->GetShaderIdentifier(kRayGenShader);
    const void* miss_id = state_object_properties_->GetShaderIdentifier(kMissShader);
    const void* shadow_miss_id = state_object_properties_->GetShaderIdentifier(kShadowMissShader);
    const void* hit_id = state_object_properties_->GetShaderIdentifier(kHitGroup);
    const void* shadow_hit_id = state_object_properties_->GetShaderIdentifier(kShadowHitGroup);
    if (!raygen_id || !miss_id || !shadow_miss_id || !hit_id || !shadow_hit_id) {
        if (error_message) {
            *error_message = QStringLiteral("DXR shader identifiers were not found.");
        }
        return false;
    }

    shader_record_size_ = static_cast<std::uint32_t>(alignTo(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
                                                             D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));
    const std::uint64_t section_size = alignTo(shader_record_size_, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    const std::uint64_t miss_section_size = alignTo(shader_record_size_ * 2ull, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    const std::uint64_t hit_section_size = alignTo(shader_record_size_ * 2ull, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    shader_table_raygen_offset_ = 0;
    shader_table_miss_offset_ = section_size;
    shader_table_hit_offset_ = section_size + miss_section_size;
    shader_table_shadow_hit_offset_ = shader_table_hit_offset_ + shader_record_size_;
    const std::uint64_t shader_table_size = section_size + miss_section_size + hit_section_size;

    std::vector<std::uint8_t> table(static_cast<std::size_t>(shader_table_size), 0);
    std::memcpy(table.data() + shader_table_raygen_offset_, raygen_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(table.data() + shader_table_miss_offset_, miss_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(table.data() + shader_table_miss_offset_ + shader_record_size_, shadow_miss_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(table.data() + shader_table_hit_offset_, hit_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    std::memcpy(table.data() + shader_table_shadow_hit_offset_, shadow_hit_id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

    if (!createBuffer(shader_table_size,
                      D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_FLAG_NONE,
                      D3D12_RESOURCE_STATE_GENERIC_READ,
                      &shader_table_,
                      error_message)) {
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE empty_range = { 0, 0 };
    shader_table_->Map(0, &empty_range, &mapped);
    std::memcpy(mapped, table.data(), table.size());
    shader_table_->Unmap(0, nullptr);
    return true;
}

bool DxrRayTracer::dispatchDxr(const FrameRenderSettings& settings, QString* error_message) {
    if (!resetCommandList(error_message)) {
        return false;
    }

    transitionOutput(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* heaps[] = { descriptor_heap_.Get() };
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetComputeRootSignature(global_root_signature_.Get());
    command_list_->SetComputeRootDescriptorTable(0, descriptor_heap_->GetGPUDescriptorHandleForHeapStart());
    const DxrFrameConstants constants = makeFrameConstants(settings);
    command_list_->SetComputeRoot32BitConstants(1,
                                                sizeof(DxrFrameConstants) / sizeof(std::uint32_t),
                                                &constants,
                                                0);
    command_list_->SetPipelineState1(state_object_.Get());

    const D3D12_GPU_VIRTUAL_ADDRESS shader_table_base = shader_table_->GetGPUVirtualAddress();
    D3D12_DISPATCH_RAYS_DESC dispatch_desc = {};
    dispatch_desc.RayGenerationShaderRecord.StartAddress = shader_table_base + shader_table_raygen_offset_;
    dispatch_desc.RayGenerationShaderRecord.SizeInBytes = shader_record_size_;
    dispatch_desc.MissShaderTable.StartAddress = shader_table_base + shader_table_miss_offset_;
    dispatch_desc.MissShaderTable.SizeInBytes = shader_record_size_ * 2ull;
    dispatch_desc.MissShaderTable.StrideInBytes = shader_record_size_;
    dispatch_desc.HitGroupTable.StartAddress = shader_table_base + shader_table_hit_offset_;
    dispatch_desc.HitGroupTable.SizeInBytes = shader_record_size_ * 2ull;
    dispatch_desc.HitGroupTable.StrideInBytes = shader_record_size_;
    dispatch_desc.Width = static_cast<UINT>(framebuffer_width_);
    dispatch_desc.Height = static_cast<UINT>(framebuffer_height_);
    dispatch_desc.Depth = 1;
    command_list_->DispatchRays(&dispatch_desc);

    D3D12_RESOURCE_BARRIER uavs[] = {
        uavBarrier(output_texture_.Get()),
        uavBarrier(accumulation_texture_.Get()),
        uavBarrier(moment_texture_.Get())
    };
    command_list_->ResourceBarrier(static_cast<UINT>(std::size(uavs)), uavs);
    transitionOutput(D3D12_RESOURCE_STATE_COMMON);

    return executeCommandList(error_message);
}

DxrRayTracer::DxrFrameConstants DxrRayTracer::makeFrameConstants(const FrameRenderSettings& settings) const {
    DxrFrameConstants constants;

    const Eigen::Matrix4f inverse_view = settings.view_matrix.inverse();
    Eigen::Vector3f camera_position = settings.camera_position;
    Eigen::Vector3f camera_forward = -inverse_view.block<3, 1>(0, 2);
    Eigen::Vector3f camera_right = inverse_view.block<3, 1>(0, 0);
    Eigen::Vector3f camera_up = inverse_view.block<3, 1>(0, 1);

    if (camera_forward.norm() < 1e-4f) {
        camera_forward = settings.camera_target - settings.camera_position;
        if (camera_forward.norm() < 1e-4f) {
            camera_forward = Eigen::Vector3f(0.0f, 0.0f, -1.0f);
        } else {
            camera_forward.normalize();
        }
    } else {
        camera_forward.normalize();
    }
    if (camera_right.norm() < 1e-4f) {
        camera_right = Eigen::Vector3f(1.0f, 0.0f, 0.0f);
    } else {
        camera_right.normalize();
    }
    if (camera_up.norm() < 1e-4f) {
        camera_up = Eigen::Vector3f(0.0f, 1.0f, 0.0f);
    } else {
        camera_up.normalize();
    }

    const float projection_y = std::abs(settings.projection_matrix(1, 1));
    const float fov_y = projection_y > 1e-4f
        ? 2.0f * std::atan(1.0f / projection_y)
        : 55.0f * 3.1415926535f / 180.0f;
    const float aspect = static_cast<float>(framebuffer_width_) /
        std::max(1.0f, static_cast<float>(framebuffer_height_));

    constants.camera_position[0] = camera_position.x();
    constants.camera_position[1] = camera_position.y();
    constants.camera_position[2] = camera_position.z();
    constants.camera_position[3] = 1.0f;
    constants.camera_forward[0] = camera_forward.x();
    constants.camera_forward[1] = camera_forward.y();
    constants.camera_forward[2] = camera_forward.z();
    constants.camera_forward[3] = 0.0f;
    constants.camera_right[0] = camera_right.x();
    constants.camera_right[1] = camera_right.y();
    constants.camera_right[2] = camera_right.z();
    constants.camera_right[3] = 0.0f;
    constants.camera_up[0] = camera_up.x();
    constants.camera_up[1] = camera_up.y();
    constants.camera_up[2] = camera_up.z();
    constants.camera_up[3] = 0.0f;
    constants.frame_params[0] = fov_y;
    constants.frame_params[1] = aspect;
    const std::uint64_t wrapped_frame_index = accumulation_frame_index_ == 0
        ? 0
        : 1 + (accumulation_frame_index_ %
               (static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - 1ULL));
    constants.frame_params[2] = static_cast<float>(wrapped_frame_index);
    constants.frame_params[3] = static_cast<float>(std::clamp(settings.look_dev.ray_trace.max_bounces, 1, 20));
    constants.render_params[0] = static_cast<float>(std::clamp(settings.look_dev.ray_trace.samples_per_frame, 1, 16));
    constants.render_params[1] = static_cast<float>(std::clamp(settings.look_dev.ray_trace.max_nee_bounces, 0, 8));
    constants.render_params[2] = settings.look_dev.ray_trace.enable_nee ? 1.0f : 0.0f;
    constants.render_params[3] = static_cast<float>(settings.look_dev.ray_trace.view_mode);

    Eigen::Vector3f sun_direction = settings.sun_direction;
    if (sun_direction.norm() < 1e-5f) {
        sun_direction = Eigen::Vector3f(-0.45f, -1.0f, -0.25f);
    }
    sun_direction.normalize();
    constants.sun_direction[0] = sun_direction.x();
    constants.sun_direction[1] = sun_direction.y();
    constants.sun_direction[2] = sun_direction.z();
    constants.sun_direction[3] = 0.0f;
    const float sun_strength = scene_buffers_are_cornell_ ? 0.0f : 2.8f;
    constants.sun_color[0] = settings.sun_color.x() * sun_strength;
    constants.sun_color[1] = settings.sun_color.y() * sun_strength;
    constants.sun_color[2] = settings.sun_color.z() * sun_strength;
    constants.sun_color[3] = 1.0f;

    constants.area_light_corner[0] = scene_buffers_are_cornell_ ? -0.34f : -0.75f;
    constants.area_light_corner[1] = scene_buffers_are_cornell_ ? 0.985f : 1.35f;
    constants.area_light_corner[2] = scene_buffers_are_cornell_ ? -0.72f : -0.75f;
    constants.area_light_corner[3] = 1.0f;
    constants.area_light_edge_u[0] = scene_buffers_are_cornell_ ? 0.68f : 1.5f;
    constants.area_light_edge_u[1] = 0.0f;
    constants.area_light_edge_u[2] = 0.0f;
    constants.area_light_edge_u[3] = 0.0f;
    constants.area_light_edge_v[0] = 0.0f;
    constants.area_light_edge_v[1] = 0.0f;
    constants.area_light_edge_v[2] = scene_buffers_are_cornell_ ? 0.68f : 1.5f;
    constants.area_light_edge_v[3] = 0.0f;
    constants.area_light_emissive[0] = scene_buffers_are_cornell_ ? 16.0f : 5.0f;
    constants.area_light_emissive[1] = scene_buffers_are_cornell_ ? 14.5f : 4.6f;
    constants.area_light_emissive[2] = scene_buffers_are_cornell_ ? 10.5f : 3.8f;
    constants.area_light_emissive[3] = 1.0f;
    return constants;
}

bool DxrRayTracer::loadWglInterop(QString* error_message) {
    if (wgl_dx_open_device_nv_ &&
        wgl_dx_close_device_nv_ &&
        wgl_dx_set_resource_share_handle_nv_ &&
        wgl_dx_register_object_nv_ &&
        wgl_dx_unregister_object_nv_ &&
        wgl_dx_lock_objects_nv_ &&
        wgl_dx_unlock_objects_nv_) {
        return true;
    }

    wgl_dx_open_device_nv_ = reinterpret_cast<WglDxOpenDeviceNv>(wglGetProcAddress("wglDXOpenDeviceNV"));
    wgl_dx_close_device_nv_ = reinterpret_cast<WglDxCloseDeviceNv>(wglGetProcAddress("wglDXCloseDeviceNV"));
    wgl_dx_set_resource_share_handle_nv_ = reinterpret_cast<WglDxSetResourceShareHandleNv>(wglGetProcAddress("wglDXSetResourceShareHandleNV"));
    wgl_dx_register_object_nv_ = reinterpret_cast<WglDxRegisterObjectNv>(wglGetProcAddress("wglDXRegisterObjectNV"));
    wgl_dx_unregister_object_nv_ = reinterpret_cast<WglDxUnregisterObjectNv>(wglGetProcAddress("wglDXUnregisterObjectNV"));
    wgl_dx_lock_objects_nv_ = reinterpret_cast<WglDxLockObjectsNv>(wglGetProcAddress("wglDXLockObjectsNV"));
    wgl_dx_unlock_objects_nv_ = reinterpret_cast<WglDxUnlockObjectsNv>(wglGetProcAddress("wglDXUnlockObjectsNV"));

    if (!wgl_dx_open_device_nv_ ||
        !wgl_dx_close_device_nv_ ||
        !wgl_dx_set_resource_share_handle_nv_ ||
        !wgl_dx_register_object_nv_ ||
        !wgl_dx_unregister_object_nv_ ||
        !wgl_dx_lock_objects_nv_ ||
        !wgl_dx_unlock_objects_nv_) {
        if (error_message) {
            *error_message = QStringLiteral("WGL_NV_DX_interop is not available, so DXR cannot use GPU-only OpenGL present.");
        }
        return false;
    }
    return true;
}

bool DxrRayTracer::initializeGpuInterop(QString* error_message) {
    if (!QOpenGLContext::currentContext()) {
        if (error_message) {
            *error_message = QStringLiteral("DXR/OpenGL GPU present requires an active OpenGL context.");
        }
        return false;
    }
    if (gl_frame_texture_ == 0) {
        if (error_message) {
            *error_message = QStringLiteral("OpenGL display texture was not created.");
        }
        return false;
    }
    if (!loadWglInterop(error_message)) {
        return false;
    }

    auto fail_and_cleanup = [this, error_message](const QString& message) {
        if (error_message) {
            *error_message = message;
        }
        destroyGpuInterop();
        return false;
    };

    UINT device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    const D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    auto register_wgl_present = [this](QString* local_error) {
        glBindTexture(GL_TEXTURE_2D, gl_frame_texture_);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     framebuffer_width_,
                     framebuffer_height_,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (shared_output_handle_ && wgl_dx_set_resource_share_handle_nv_) {
            wgl_dx_set_resource_share_handle_nv_(d3d11_shared_texture_.Get(), shared_output_handle_);
        }

        wgl_interop_device_ = wgl_dx_open_device_nv_(d3d11_device_.Get());
        if (!wgl_interop_device_) {
            if (local_error) {
                *local_error = QStringLiteral("wglDXOpenDeviceNV failed for the D3D11 interop device.");
            }
            return false;
        }

        wgl_interop_object_ = wgl_dx_register_object_nv_(wgl_interop_device_,
                                                         d3d11_shared_texture_.Get(),
                                                         gl_frame_texture_,
                                                         GL_TEXTURE_2D,
                                                         WGL_ACCESS_READ_WRITE_NV);
        if (!wgl_interop_object_) {
            if (local_error) {
                *local_error = QStringLiteral("wglDXRegisterObjectNV failed for the DXR/OpenGL present texture.");
            }
            return false;
        }
        return true;
    };

    HRESULT hr = D3D11CreateDevice(adapter_.Get(),
                           D3D_DRIVER_TYPE_UNKNOWN,
                           nullptr,
                           device_flags,
                           feature_levels,
                           static_cast<UINT>(std::size(feature_levels)),
                           D3D11_SDK_VERSION,
                           &d3d11_device_,
                           &feature_level,
                           &d3d11_context_);
    if (hr == E_INVALIDARG) {
        const D3D_FEATURE_LEVEL fallback_levels[] = { D3D_FEATURE_LEVEL_11_0 };
        hr = D3D11CreateDevice(adapter_.Get(),
                               D3D_DRIVER_TYPE_UNKNOWN,
                               nullptr,
                               device_flags,
                               fallback_levels,
                               static_cast<UINT>(std::size(fallback_levels)),
                               D3D11_SDK_VERSION,
                               &d3d11_device_,
                               &feature_level,
                               &d3d11_context_);
    }
    if (FAILED(hr)) {
        return fail_and_cleanup(hresultMessage(QStringLiteral("Create D3D11 interop device failed"), hr));
    }

    D3D11_TEXTURE2D_DESC shared_desc = {};
    shared_desc.Width = static_cast<UINT>(framebuffer_width_);
    shared_desc.Height = static_cast<UINT>(framebuffer_height_);
    shared_desc.MipLevels = 1;
    shared_desc.ArraySize = 1;
    shared_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    shared_desc.SampleDesc.Count = 1;
    shared_desc.Usage = D3D11_USAGE_DEFAULT;
    shared_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
    shared_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    hr = d3d11_device_->CreateTexture2D(&shared_desc, nullptr, &d3d11_shared_texture_);
    if (SUCCEEDED(hr)) {
        Microsoft::WRL::ComPtr<IDXGIResource1> dxgi_resource;
        hr = d3d11_shared_texture_.As(&dxgi_resource);
        if (FAILED(hr)) {
            return fail_and_cleanup(hresultMessage(QStringLiteral("Query shared texture IDXGIResource1 failed"), hr));
        }

        hr = dxgi_resource->CreateSharedHandle(nullptr,
                                               DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                               nullptr,
                                               &shared_output_handle_);
        if (FAILED(hr)) {
            return fail_and_cleanup(hresultMessage(QStringLiteral("Create NT shared handle for GPU present texture failed"), hr));
        }
        shared_output_handle_is_nt_ = true;
    } else {
        d3d11_shared_texture_.Reset();
        shared_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        hr = d3d11_device_->CreateTexture2D(&shared_desc, nullptr, &d3d11_shared_texture_);
        if (FAILED(hr)) {
            return fail_and_cleanup(hresultMessage(QStringLiteral("Create D3D11 shared present texture failed"), hr));
        }

        Microsoft::WRL::ComPtr<IDXGIResource> dxgi_resource;
        hr = d3d11_shared_texture_.As(&dxgi_resource);
        if (FAILED(hr)) {
            return fail_and_cleanup(hresultMessage(QStringLiteral("Query shared texture IDXGIResource failed"), hr));
        }
        hr = dxgi_resource->GetSharedHandle(&shared_output_handle_);
        if (FAILED(hr)) {
            return fail_and_cleanup(hresultMessage(QStringLiteral("Get legacy shared handle for GPU present texture failed"), hr));
        }
        shared_output_handle_is_nt_ = false;
    }

    hr = device_->OpenSharedHandle(shared_output_handle_, IID_PPV_ARGS(&output_texture_));
    if (FAILED(hr)) {
        return fail_and_cleanup(hresultMessage(QStringLiteral("Open shared DXR output texture in D3D12 failed"), hr));
    }
    output_state_ = D3D12_RESOURCE_STATE_COMMON;

    QString register_error;
    if (!register_wgl_present(&register_error)) {
        return fail_and_cleanup(register_error);
    }

    gpu_present_mode_ = QStringLiteral("D3D11 shared texture + WGL_NV_DX_interop on %1 / GL %2")
        .arg(dxgi_adapter_name_.isEmpty() ? QStringLiteral("unknown DXGI adapter") : dxgi_adapter_name_)
        .arg(gl_renderer_.isEmpty() ? QStringLiteral("unknown renderer") : gl_renderer_);
    gpu_present_ready_ = true;
    return true;
}

void DxrRayTracer::destroyGpuInterop() {
    gpu_present_ready_ = false;
    gpu_present_mode_.clear();
    if (wgl_interop_object_ && wgl_dx_unregister_object_nv_ && wgl_interop_device_) {
        wgl_dx_unregister_object_nv_(wgl_interop_device_, wgl_interop_object_);
    }
    wgl_interop_object_ = nullptr;
    if (wgl_interop_device_ && wgl_dx_close_device_nv_) {
        wgl_dx_close_device_nv_(wgl_interop_device_);
    }
    wgl_interop_device_ = nullptr;
    shared_output_texture_.Reset();
    d3d11_shared_texture_.Reset();
    d3d11_context_.Reset();
    d3d11_device_.Reset();
    if (shared_output_handle_ && shared_output_handle_is_nt_) {
        CloseHandle(shared_output_handle_);
    }
    shared_output_handle_ = nullptr;
    shared_output_handle_is_nt_ = false;
    shared_output_state_ = D3D12_RESOURCE_STATE_COMMON;
}

bool DxrRayTracer::drawGpuInteropImage(QString* error_message) {
    if (!display_program_) {
        return true;
    }
    if (!gpu_present_ready_ || !wgl_interop_device_ || !wgl_interop_object_) {
        if (error_message) {
            *error_message = QStringLiteral("DXR/OpenGL GPU present is not ready.");
        }
        return false;
    }

    if (d3d11_context_) {
        d3d11_context_->Flush();
    }

    HANDLE object = wgl_interop_object_;
    if (!wgl_dx_lock_objects_nv_(wgl_interop_device_, 1, &object)) {
        if (error_message) {
            *error_message = QStringLiteral("wglDXLockObjectsNV failed while presenting DXR output.");
        }
        return false;
    }

    glViewport(0, 0, framebuffer_width_, framebuffer_height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl_frame_texture_);

    display_program_->bind();
    display_program_->setUniformValue("uFrame", 0);
    display_program_->setUniformValue("uTexelSize",
                                      QVector2D(1.0f / std::max(1, framebuffer_width_),
                                                1.0f / std::max(1, framebuffer_height_)));
    display_program_->setUniformValue("uDenoiseStrength", display_denoise_strength_);
    glBindVertexArray(fullscreen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    display_program_->release();
    glBindTexture(GL_TEXTURE_2D, 0);
    glFinish();

    object = wgl_interop_object_;
    if (!wgl_dx_unlock_objects_nv_(wgl_interop_device_, 1, &object)) {
        if (error_message) {
            *error_message = QStringLiteral("wglDXUnlockObjectsNV failed after presenting DXR output.");
        }
        return false;
    }
    return true;
}

void DxrRayTracer::destroyGlDisplay() {
    destroyGpuInterop();
    if (gl_frame_texture_ != 0) {
        glDeleteTextures(1, &gl_frame_texture_);
        gl_frame_texture_ = 0;
    }
    if (fullscreen_vao_ != 0) {
        glDeleteVertexArrays(1, &fullscreen_vao_);
        fullscreen_vao_ = 0;
    }
    delete display_program_;
    display_program_ = nullptr;
}

void DxrRayTracer::releaseDxrResources() {
    destroyGpuInterop();
    shader_table_.Reset();
    descriptor_heap_.Reset();
    moment_texture_.Reset();
    accumulation_texture_.Reset();
    output_texture_.Reset();
    instance_desc_buffer_.Reset();
    tlas_.Reset();
    blas_.Reset();
    base_color_atlas_.Reset();
    normal_atlas_.Reset();
    metallic_atlas_.Reset();
    roughness_atlas_.Reset();
    ao_atlas_.Reset();
    emissive_atlas_.Reset();
    primitive_material_buffer_.Reset();
    material_buffer_.Reset();
    index_buffer_.Reset();
    vertex_buffer_.Reset();
    state_object_properties_.Reset();
    state_object_.Reset();
    global_root_signature_.Reset();
    ray_library_.Reset();
    command_list_.Reset();
    command_allocator_.Reset();
    command_queue_.Reset();
    fence_.Reset();
    device_.Reset();
    adapter_.Reset();
    dxgi_factory_.Reset();
    if (fence_event_) {
        CloseHandle(static_cast<HANDLE>(fence_event_));
        fence_event_ = nullptr;
    }
}

void DxrRayTracer::resetAccumulation() {
    accumulation_frame_index_ = 0;
    last_frame_signature_ = 0;
}

bool DxrRayTracer::ensureSceneMode(const FrameRenderSettings& settings, QString* error_message) {
    if (settings.look_dev.ray_trace.scene_mode == RayTraceSceneMode::CornellBox) {
        if (!scene_buffers_are_cornell_) {
            if (!buildCornellScene(error_message)) {
                return false;
            }
            if (!buildDescriptorHeap(error_message)) {
                return false;
            }
            output_dirty_ = true;
        }
        return true;
    }

    if (scene_buffers_are_cornell_) {
        if (!rebuildLoadedScene(error_message)) {
            return false;
        }
        if (!buildDescriptorHeap(error_message)) {
            return false;
        }
        output_dirty_ = true;
    }
    return true;
}

std::uint64_t DxrRayTracer::frameSignature(const FrameRenderSettings& settings) const {
    auto hash_combine = [](std::uint64_t& seed, std::uint64_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    };
    auto hash_float = [](float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return static_cast<std::uint64_t>(bits);
    };

    std::uint64_t signature = 1469598103934665603ull;
    hash_combine(signature, static_cast<std::uint64_t>(framebuffer_width_));
    hash_combine(signature, static_cast<std::uint64_t>(framebuffer_height_));
    hash_combine(signature, static_cast<std::uint64_t>(scene_triangle_count_));
    hash_combine(signature, static_cast<std::uint64_t>(settings.look_dev.ray_trace.max_bounces));
    hash_combine(signature, static_cast<std::uint64_t>(settings.look_dev.ray_trace.max_nee_bounces));
    hash_combine(signature, static_cast<std::uint64_t>(settings.look_dev.ray_trace.samples_per_frame));
    hash_combine(signature, settings.look_dev.ray_trace.enable_nee ? 1ull : 0ull);
    hash_combine(signature, static_cast<std::uint64_t>(settings.look_dev.ray_trace.view_mode));
    hash_combine(signature, static_cast<std::uint64_t>(settings.look_dev.pbr.metallic_channel));
    hash_combine(signature, static_cast<std::uint64_t>(settings.look_dev.pbr.roughness_channel));
    hash_combine(signature, static_cast<std::uint64_t>(settings.look_dev.pbr.ao_channel));
    hash_combine(signature, static_cast<std::uint64_t>(settings.look_dev.pbr.emissive_channel));
    hash_combine(signature, scene_buffers_are_cornell_ ? 1ull : 0ull);
    for (int i = 0; i < 3; ++i) {
        hash_combine(signature, hash_float(settings.camera_position[i]));
        hash_combine(signature, hash_float(settings.camera_target[i]));
        hash_combine(signature, hash_float(settings.sun_direction[i]));
        hash_combine(signature, hash_float(settings.sun_color[i]));
    }
    return signature;
}

bool DxrRayTracer::resetCommandList(QString* error_message) {
    if (failIfFailed(command_allocator_->Reset(),
                     QStringLiteral("Command allocator reset failed"),
                     error_message) ||
        failIfFailed(command_list_->Reset(command_allocator_.Get(), nullptr),
                     QStringLiteral("Command list reset failed"),
                     error_message)) {
        return false;
    }
    return true;
}

bool DxrRayTracer::executeCommandList(QString* error_message) {
    if (failIfFailed(command_list_->Close(),
                     QStringLiteral("Command list close failed"),
                     error_message)) {
        return false;
    }
    ID3D12CommandList* lists[] = { command_list_.Get() };
    command_queue_->ExecuteCommandLists(1, lists);
    return waitForGpu(error_message);
}

bool DxrRayTracer::waitForGpu(QString* error_message) {
    if (!command_queue_ || !fence_) {
        return true;
    }

    const std::uint64_t signal_value = ++fence_value_;
    if (failIfFailed(command_queue_->Signal(fence_.Get(), signal_value),
                     QStringLiteral("Fence signal failed"),
                     error_message)) {
        return false;
    }
    if (fence_->GetCompletedValue() < signal_value) {
        if (failIfFailed(fence_->SetEventOnCompletion(signal_value, static_cast<HANDLE>(fence_event_)),
                         QStringLiteral("Fence SetEventOnCompletion failed"),
                         error_message)) {
            return false;
        }
        WaitForSingleObject(static_cast<HANDLE>(fence_event_), INFINITE);
    }
    return true;
}

bool DxrRayTracer::createBuffer(std::uint64_t byte_size,
                                D3D12_HEAP_TYPE heap_type,
                                D3D12_RESOURCE_FLAGS flags,
                                D3D12_RESOURCE_STATES initial_state,
                                Microsoft::WRL::ComPtr<ID3D12Resource>* resource,
                                QString* error_message) const {
    D3D12_HEAP_PROPERTIES heap_properties = {};
    heap_properties.Type = heap_type;

    D3D12_RESOURCE_DESC resource_desc = {};
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = std::max<std::uint64_t>(byte_size, 1);
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = flags;

    return !failIfFailed(device_->CreateCommittedResource(&heap_properties,
                                                           D3D12_HEAP_FLAG_NONE,
                                                           &resource_desc,
                                                           initial_state,
                                                           nullptr,
                                                           IID_PPV_ARGS(resource->ReleaseAndGetAddressOf())),
                         QStringLiteral("CreateCommittedResource(buffer) failed"),
                         error_message);
}

void DxrRayTracer::transitionOutput(D3D12_RESOURCE_STATES new_state) {
    if (output_state_ == new_state) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier = transitionBarrier(output_texture_.Get(), output_state_, new_state);
    command_list_->ResourceBarrier(1, &barrier);
    output_state_ = new_state;
}

void DxrRayTracer::transitionSharedOutput(D3D12_RESOURCE_STATES new_state) {
    if (!shared_output_texture_ || shared_output_state_ == new_state) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier = transitionBarrier(shared_output_texture_.Get(), shared_output_state_, new_state);
    command_list_->ResourceBarrier(1, &barrier);
    shared_output_state_ = new_state;
}

std::uint64_t DxrRayTracer::alignTo(std::uint64_t value, std::uint64_t alignment) const {
    return (value + alignment - 1) & ~(alignment - 1);
}

#endif

} // namespace haorendergi
