#include "rendering/opengl_ray_tracer.h"

#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QRect>
#include <QSurfaceFormat>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace haorendergi {
namespace {

constexpr unsigned int kTriangleBinding = 0;
constexpr unsigned int kMaterialBinding = 1;
constexpr unsigned int kBvhBinding = 2;
constexpr unsigned int kLightTriangleBinding = 3;
constexpr int kAlbedoAtlasTextureUnit = 0;
constexpr int kMetallicAtlasTextureUnit = 1;
constexpr int kRoughnessAtlasTextureUnit = 2;
constexpr int kAoAtlasTextureUnit = 3;
constexpr int kEmissiveAtlasTextureUnit = 4;
constexpr int kHistoryTextureUnit = 5;
constexpr int kLeafTriangleCount = 4;
constexpr int kAtlasPadding = 2;
constexpr int kMaxRayTraceAtlasTileSize = 2048;
constexpr int kMinRayTraceAtlasTileSize = 256;

struct PendingAtlasTexture {
    int material_index = -1;
    QImage source_image;
    QImage image;
    QRect rect;
};

int nextPowerOfTwo(int value) {
    int result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

std::uint64_t hashCombine(std::uint64_t seed, std::uint64_t value) {
    constexpr std::uint64_t kMul = 0x9e3779b97f4a7c15ULL;
    return seed ^ (value + kMul + (seed << 6) + (seed >> 2));
}

std::uint64_t hashFloat(float value) {
    union {
        float f;
        std::uint32_t u;
    } bits {};
    bits.f = value;
    return bits.u;
}

const char* traceVertexShaderSource() {
    return R"(#version 430 core
out vec2 vUv;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 p = positions[gl_VertexID];
    vUv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";
}

std::string traceFragmentShaderSource() {
    return std::string(R"(#version 430 core
in vec2 vUv;
out vec4 fragColor;

struct GpuTriangle {
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 n0;
    vec4 n1;
    vec4 n2;
    vec4 uv0;
    vec4 uv1;
    vec4 uv2;
    int material_index;
    int pad0;
    int pad1;
    int pad2;
};

struct GpuMaterial {
    vec4 base_color;
    vec4 emissive_factor;
    vec4 pbr_factors;
    vec4 albedo_atlas_rect;
    vec4 metallic_atlas_rect;
    vec4 roughness_atlas_rect;
    vec4 ao_atlas_rect;
    vec4 emissive_atlas_rect;
    ivec4 texture_flags0;
    ivec4 texture_flags1;
};

struct GpuBvhNode {
    vec4 bounds_min;
    vec4 bounds_max;
    int left_index;
    int right_index;
    int first_triangle;
    int triangle_count;
};

struct GpuLightTriangle {
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 normal_area;
    vec4 emission;
};

struct HitRecord {
    float t;
    int triangle_index;
    vec3 position;
    vec3 normal;
    vec3 barycentric;
    vec2 uv;
};

layout(std430, binding = 0) readonly buffer TriangleBuffer {
    GpuTriangle triangles[];
};

layout(std430, binding = 1) readonly buffer MaterialBuffer {
    GpuMaterial materials[];
};

layout(std430, binding = 2) readonly buffer BvhBuffer {
    GpuBvhNode bvh_nodes[];
};

layout(std430, binding = 3) readonly buffer LightTriangleBuffer {
    GpuLightTriangle light_triangles[];
};

uniform mat4 uInvViewProjection;
uniform mat4 uModel;
uniform mat4 uInvModel;
uniform mat3 uNormalMatrix;
uniform vec3 uCameraPosition;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uClearColor;
uniform float uExposure;
uniform float uAmbientStrength;
uniform float uShadowStrength;
uniform int uEnableShadows;
uniform int uViewMode;
uniform int uSceneMode;
uniform int uTraceAlgorithm;
uniform int uEnableNee;
uniform int uEnablePhotonCache;
uniform int uMaxBounces;
uniform int uMaxNeeBounces;
uniform int uSamplesPerFrame;
uniform float uPhotonRadius;
uniform float uPhotonIntensity;
uniform int uTriangleCount;
uniform int uMaterialCount;
uniform int uBvhNodeCount;
uniform int uLightTriangleCount;
uniform sampler2D uAlbedoAtlas;
uniform sampler2D uMetallicAtlas;
uniform sampler2D uRoughnessAtlas;
uniform sampler2D uAoAtlas;
uniform sampler2D uEmissiveAtlas;
uniform sampler2D uHistoryTexture;
uniform int uHistoryValid;
uniform int uFrameIndex;
uniform int uHistoryFrameCount;
uniform int uOutputLinear;
uniform int uMetallicChannel;
uniform int uRoughnessChannel;
uniform int uAoChannel;
uniform int uEmissiveChannel;

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
    return pow(max(value, vec3(0.0)), vec3(2.2));
}

vec3 linearToSrgb(vec3 value) {
    return pow(max(value, vec3(0.0)), vec3(1.0 / 2.2));
}

vec2 atlasUv(vec4 rect, vec2 uv) {
    return rect.xy + fract(uv) * rect.zw;
}

vec3 resolveBaseColor(GpuMaterial material, vec2 uv) {
    vec3 baseColor = max(material.base_color.rgb, vec3(0.0));
    if (material.texture_flags0.x != 0) {
        baseColor *= srgbToLinear(texture(uAlbedoAtlas, atlasUv(material.albedo_atlas_rect, uv)).rgb);
    }
    return baseColor;
}

float resolveMetallic(GpuMaterial material, vec2 uv) {
    float metallic = clamp(material.pbr_factors.y, 0.0, 1.0);
    if (material.texture_flags0.y != 0) {
        vec4 sampleValue = texture(uMetallicAtlas, atlasUv(material.metallic_atlas_rect, uv));
        metallic *= channelValue(sampleValue, uMetallicChannel);
    }
    return clamp(metallic, 0.0, 1.0);
}

float resolveRoughness(GpuMaterial material, vec2 uv) {
    float roughness = clamp(material.pbr_factors.x, 0.04, 1.0);
    if (material.texture_flags0.z != 0) {
        vec4 sampleValue = texture(uRoughnessAtlas, atlasUv(material.roughness_atlas_rect, uv));
        roughness *= channelValue(sampleValue, uRoughnessChannel);
    }
    return clamp(roughness, 0.04, 1.0);
}

float resolveAo(GpuMaterial material, vec2 uv) {
    float ao = clamp(material.pbr_factors.z, 0.0, 1.0);
    if (material.texture_flags0.w != 0) {
        vec4 sampleValue = texture(uAoAtlas, atlasUv(material.ao_atlas_rect, uv));
        ao *= channelValue(sampleValue, uAoChannel);
    }
    return clamp(ao, 0.0, 1.0);
}

vec3 resolveEmissive(GpuMaterial material, vec2 uv) {
    vec3 emissive = max(material.emissive_factor.rgb, vec3(0.0));
    if (material.texture_flags1.x != 0) {
        vec4 emissiveSample = texture(uEmissiveAtlas, atlasUv(material.emissive_atlas_rect, uv));
        emissive *= srgbToLinear(emissiveSample.rgb) * channelValue(emissiveSample, uEmissiveChannel);
    }
    return emissive;
}

uint hashUint(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float rng(inout uint state) {
    state = hashUint(state + 0x9e3779b9u);
    return float(state) * (1.0 / 4294967296.0);
}

vec3 cosineHemisphereSample(vec3 n, inout uint rngState) {
    float u1 = max(rng(rngState), 1e-6);
    float u2 = rng(rngState);
    float r = sqrt(u1);
    float phi = 6.28318530718 * u2;
    vec3 local = vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, n));
    vec3 bitangent = cross(n, tangent);
    return normalize(local.x * tangent + local.y * bitangent + local.z * n);
}
)") + R"(
const int RT_SCENE_CORNELL = 0;
const int RT_TRACE_HYBRID = 0;
const int RT_TRACE_PATH = 1;
const int RT_TRACE_PATH_NEE = 2;
const int RT_TRACE_PHOTON = 3;
const int CORNELL_DIFFUSE = 0;
const int CORNELL_METAL = 1;
const int CORNELL_GLASS = 2;
const int CORNELL_LIGHT = 3;
const float RT_PI = 3.14159265359;
const float RT_MIN_HIT_TIME = 0.001;
const float RT_RAY_NUDGE = 0.001;
const int RT_MAX_PATH_BOUNCES = 24;
const int RT_MAX_SPP = 16;
const int RT_DEEP_PATH_PERIOD = 4;

struct CornellHit {
    float t;
    vec3 position;
    vec3 normal;
    vec3 albedo;
    vec3 emission;
    int material;
    float eta;
    float roughness;
    float metallic;
};

CornellHit emptyCornellHit() {
    CornellHit hit;
    hit.t = 1e30;
    hit.position = vec3(0.0);
    hit.normal = vec3(0.0, 1.0, 0.0);
    hit.albedo = vec3(0.0);
    hit.emission = vec3(0.0);
    hit.material = CORNELL_DIFFUSE;
    hit.eta = 1.0;
    hit.roughness = 0.65;
    hit.metallic = 0.0;
    return hit;
}

void commitCornellHit(inout CornellHit hit,
                      float t,
                      vec3 rayOrigin,
                      vec3 rayDirection,
                      vec3 normal,
                      vec3 albedo,
                      vec3 emission,
                      int material,
                      float eta,
                      float roughness,
                      float metallic) {
    if (t <= RT_MIN_HIT_TIME || t >= hit.t) {
        return;
    }
    hit.t = t;
    hit.position = rayOrigin + rayDirection * t;
    hit.normal = normalize(normal);
    hit.albedo = albedo;
    hit.emission = emission;
    hit.material = material;
    hit.eta = eta;
    hit.roughness = clamp(roughness, 0.04, 1.0);
    hit.metallic = clamp(metallic, 0.0, 1.0);
}

void intersectCornellPlaneY(inout CornellHit hit, vec3 rayOrigin, vec3 rayDirection, float y, vec3 normal, vec3 albedo) {
    if (abs(rayDirection.y) < 1e-5) {
        return;
    }
    float t = (y - rayOrigin.y) / rayDirection.y;
    vec3 p = rayOrigin + rayDirection * t;
    if (p.x >= -1.0 && p.x <= 1.0 && p.z >= -1.0 && p.z <= 1.0) {
        commitCornellHit(hit, t, rayOrigin, rayDirection, normal, albedo, vec3(0.0), CORNELL_DIFFUSE, 1.0, 0.72, 0.0);
    }
}

void intersectCornellCeiling(inout CornellHit hit, vec3 rayOrigin, vec3 rayDirection) {
    if (abs(rayDirection.y) < 1e-5) {
        return;
    }
    float t = (1.0 - rayOrigin.y) / rayDirection.y;
    vec3 p = rayOrigin + rayDirection * t;
    if (p.x < -1.0 || p.x > 1.0 || p.z < -1.0 || p.z > 1.0) {
        return;
    }

    bool onLight = p.x >= -0.32 && p.x <= 0.32 && p.z >= -0.62 && p.z <= -0.10;
    if (onLight) {
        commitCornellHit(hit, t, rayOrigin, rayDirection, vec3(0.0, -1.0, 0.0), vec3(1.0), vec3(18.0, 15.5, 11.5), CORNELL_LIGHT, 1.0, 0.04, 0.0);
    } else {
        commitCornellHit(hit, t, rayOrigin, rayDirection, vec3(0.0, -1.0, 0.0), vec3(0.82), vec3(0.0), CORNELL_DIFFUSE, 1.0, 0.72, 0.0);
    }
}

void intersectCornellPlaneX(inout CornellHit hit, vec3 rayOrigin, vec3 rayDirection, float x, vec3 normal, vec3 albedo) {
    if (abs(rayDirection.x) < 1e-5) {
        return;
    }
    float t = (x - rayOrigin.x) / rayDirection.x;
    vec3 p = rayOrigin + rayDirection * t;
    if (p.y >= -1.0 && p.y <= 1.0 && p.z >= -1.0 && p.z <= 1.0) {
        commitCornellHit(hit, t, rayOrigin, rayDirection, normal, albedo, vec3(0.0), CORNELL_DIFFUSE, 1.0, 0.76, 0.0);
    }
}

void intersectCornellPlaneZ(inout CornellHit hit, vec3 rayOrigin, vec3 rayDirection, float z, vec3 normal, vec3 albedo) {
    if (abs(rayDirection.z) < 1e-5) {
        return;
    }
    float t = (z - rayOrigin.z) / rayDirection.z;
    vec3 p = rayOrigin + rayDirection * t;
    if (p.x >= -1.0 && p.x <= 1.0 && p.y >= -1.0 && p.y <= 1.0) {
        commitCornellHit(hit, t, rayOrigin, rayDirection, normal, albedo, vec3(0.0), CORNELL_DIFFUSE, 1.0, 0.72, 0.0);
    }
}

void intersectCornellSphere(inout CornellHit hit,
                            vec3 rayOrigin,
                            vec3 rayDirection,
                            vec3 center,
                            float radius,
                            vec3 albedo,
                            int material,
                            float eta,
                            float roughness,
                            float metallic) {
    vec3 oc = rayOrigin - center;
    float halfB = dot(oc, rayDirection);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = halfB * halfB - c;
    if (discriminant < 0.0) {
        return;
    }
    float root = sqrt(discriminant);
    float t = -halfB - root;
    if (t <= RT_MIN_HIT_TIME) {
        t = -halfB + root;
    }
    if (t <= RT_MIN_HIT_TIME || t >= hit.t) {
        return;
    }
    vec3 p = rayOrigin + rayDirection * t;
    commitCornellHit(hit, t, rayOrigin, rayDirection, normalize(p - center), albedo, vec3(0.0), material, eta, roughness, metallic);
}

CornellHit traceCornellScene(vec3 rayOrigin, vec3 rayDirection) {
    CornellHit hit = emptyCornellHit();
    intersectCornellPlaneY(hit, rayOrigin, rayDirection, -1.0, vec3(0.0, 1.0, 0.0), vec3(0.82));
    intersectCornellCeiling(hit, rayOrigin, rayDirection);
    intersectCornellPlaneX(hit, rayOrigin, rayDirection, -1.0, vec3(1.0, 0.0, 0.0), vec3(0.63, 0.065, 0.05));
    intersectCornellPlaneX(hit, rayOrigin, rayDirection, 1.0, vec3(-1.0, 0.0, 0.0), vec3(0.14, 0.45, 0.09));
    intersectCornellPlaneZ(hit, rayOrigin, rayDirection, -1.0, vec3(0.0, 0.0, 1.0), vec3(0.82));
    intersectCornellSphere(hit, rayOrigin, rayDirection, vec3(-0.52, -0.72, -0.22), 0.28, vec3(1.0, 0.71, 0.30), CORNELL_METAL, 1.0, 0.16, 1.0);
    intersectCornellSphere(hit, rayOrigin, rayDirection, vec3(0.43, -0.68, 0.02), 0.32, vec3(0.75, 0.90, 1.0), CORNELL_GLASS, 1.5, 0.02, 0.0);
    intersectCornellSphere(hit, rayOrigin, rayDirection, vec3(0.64, -0.30, -0.36), 0.18, vec3(0.86, 0.88, 0.92), CORNELL_METAL, 1.0, 0.06, 1.0);
    return hit;
}

bool cornellOccluded(vec3 origin, vec3 direction, float maxT) {
    CornellHit blocker = traceCornellScene(origin, direction);
    return blocker.t < maxT - 0.006 && blocker.material != CORNELL_LIGHT;
}

float schlickFresnel(float cosine, float eta) {
    float r0 = (1.0 - eta) / (1.0 + eta);
    r0 = r0 * r0;
    float x = clamp(1.0 - cosine, 0.0, 1.0);
    return r0 + (1.0 - r0) * x * x * x * x * x;
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = max(dot(N, H), 0.0);
    float ndoth2 = ndoth * ndoth;
    float denom = ndoth2 * (a2 - 1.0) + 1.0;
    return a2 / max(RT_PI * denom * denom, 1e-5);
}

float geometrySchlickGGX(float ndotv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 1e-5);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

bool isDeltaCornellMaterial(int material) {
    return material == CORNELL_GLASS;
}

void resolvePathSampleBudget(int sampleIndex, int frameIndex, out int sampleBounces, out int sampleNeeBounces) {
    sampleBounces = clamp(uMaxBounces, 1, RT_MAX_PATH_BOUNCES);
    sampleNeeBounces = 0;

    if (uEnableNee != 0 && (uTraceAlgorithm == RT_TRACE_PATH_NEE || uTraceAlgorithm == RT_TRACE_PHOTON)) {
        sampleNeeBounces = min(sampleBounces, clamp(uMaxNeeBounces, 0, RT_MAX_PATH_BOUNCES));
    }

    bool warmupFrame = frameIndex < 3;
    int pixelHash = int(gl_FragCoord.x) + int(gl_FragCoord.y) + frameIndex + sampleIndex;
    bool deepPathSample = (pixelHash % RT_DEEP_PATH_PERIOD) == 0;
    if (warmupFrame && !deepPathSample) {
        sampleBounces = min(sampleBounces, 2);
        sampleNeeBounces = min(sampleNeeBounces, 1);
    }
}

vec3 sampleCornellDirect(CornellHit hit) {
    if (hit.material == CORNELL_LIGHT) {
        return hit.emission;
    }

    vec3 lightSamples[5] = vec3[5](
        vec3(0.00, 0.995, -0.36),
        vec3(-0.26, 0.995, -0.58),
        vec3(0.26, 0.995, -0.58),
        vec3(-0.26, 0.995, -0.14),
        vec3(0.26, 0.995, -0.14)
    );

    vec3 radiance = vec3(0.0);
    vec3 lightEmission = vec3(18.0, 15.5, 11.5);
    float lightArea = 0.64 * 0.52;
    for (int i = 0; i < 5; ++i) {
        vec3 toLight = lightSamples[i] - hit.position;
        float dist2 = max(dot(toLight, toLight), 1e-4);
        float dist = sqrt(dist2);
        vec3 L = toLight / dist;
        float surfaceFacing = max(dot(hit.normal, L), 0.0);
        float lightFacing = max(dot(vec3(0.0, -1.0, 0.0), -L), 0.0);
        if (surfaceFacing > 0.0 && lightFacing > 0.0 && !cornellOccluded(hit.position + hit.normal * 0.003, L, dist)) {
            radiance += lightEmission * surfaceFacing * lightFacing * lightArea / dist2;
        }
    }
    return hit.albedo * radiance * (1.0 / (5.0 * RT_PI));
}

vec3 sampleCornellPbrDirect(CornellHit hit, vec3 V) {
    vec3 lightSamples[5] = vec3[5](
        vec3(0.00, 0.995, -0.36),
        vec3(-0.26, 0.995, -0.58),
        vec3(0.26, 0.995, -0.58),
        vec3(-0.26, 0.995, -0.14),
        vec3(0.26, 0.995, -0.14)
    );

    vec3 N = normalize(hit.normal);
    vec3 f0 = mix(vec3(0.04), hit.albedo, hit.metallic);
    vec3 radiance = vec3(0.0);
    vec3 lightEmission = vec3(18.0, 15.5, 11.5);
    float lightArea = 0.64 * 0.52;
    for (int i = 0; i < 5; ++i) {
        vec3 toLight = lightSamples[i] - hit.position;
        float dist2 = max(dot(toLight, toLight), 1e-4);
        float dist = sqrt(dist2);
        vec3 L = toLight / dist;
        vec3 H = normalize(V + L);
        float ndotl = max(dot(N, L), 0.0);
        float ndotv = max(dot(N, V), 0.0);
        float lightFacing = max(dot(vec3(0.0, -1.0, 0.0), -L), 0.0);
        if (ndotl > 0.0 && ndotv > 0.0 && lightFacing > 0.0 && !cornellOccluded(hit.position + N * 0.003, L, dist)) {
            vec3 F = fresnelSchlick(max(dot(H, V), 0.0), f0);
            float NDF = distributionGGX(N, H, hit.roughness);
            float G = geometrySmith(N, V, L, hit.roughness);
            vec3 specular = (NDF * G * F) / max(4.0 * ndotv * ndotl, 1e-4);
            vec3 kD = (vec3(1.0) - F) * (1.0 - hit.metallic);
            vec3 brdf = kD * hit.albedo / RT_PI + specular;
            radiance += brdf * lightEmission * ndotl * lightFacing * lightArea / dist2;
        }
    }
    return radiance * 0.58;
}

vec3 cornellAmbientBounce(CornellHit hit) {
    vec3 bounce = vec3(0.022);
    bounce += vec3(0.12, 0.018, 0.014) * max(dot(hit.normal, vec3(-1.0, 0.0, 0.0)), 0.0);
    bounce += vec3(0.018, 0.10, 0.018) * max(dot(hit.normal, vec3(1.0, 0.0, 0.0)), 0.0);
    bounce += vec3(0.06, 0.055, 0.05) * max(dot(hit.normal, vec3(0.0, 1.0, 0.0)), 0.0);
    return hit.albedo * bounce * max(uAmbientStrength, 0.0) * 8.0;
}

vec3 shadeCornellOneBounce(vec3 rayOrigin, vec3 rayDirection) {
    CornellHit hit = traceCornellScene(rayOrigin, rayDirection);
    if (hit.t >= 1e20) {
        return vec3(0.0);
    }
    if (hit.material == CORNELL_LIGHT) {
        return hit.emission;
    }
    if (hit.material == CORNELL_DIFFUSE) {
        return sampleCornellDirect(hit) + cornellAmbientBounce(hit);
    }
    if (hit.material == CORNELL_METAL) {
        vec3 V = normalize(-rayDirection);
        vec3 f0 = mix(vec3(0.04), hit.albedo, hit.metallic);
        vec3 F = fresnelSchlick(max(dot(hit.normal, V), 0.0), f0);
        vec3 directSpecular = sampleCornellPbrDirect(hit, V);
        vec3 reflected = normalize(reflect(rayDirection, hit.normal));
        CornellHit reflectedHit = traceCornellScene(hit.position + hit.normal * 0.003, reflected);
        if (reflectedHit.material == CORNELL_LIGHT) {
            return directSpecular + reflectedHit.emission * F;
        }
        if (reflectedHit.t < 1e20) {
            vec3 bounce = sampleCornellDirect(reflectedHit) + cornellAmbientBounce(reflectedHit);
            return directSpecular + bounce * F * mix(0.95, 0.55, hit.roughness);
        }
        return directSpecular + F * vec3(0.018, 0.022, 0.028) * mix(0.95, 0.45, hit.roughness);
    }
    return hit.albedo * 0.04;
}

vec3 traceCornellRadiance(vec3 rayOrigin, vec3 rayDirection) {
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    vec3 origin = rayOrigin;
    vec3 direction = normalize(rayDirection);

    for (int bounce = 0; bounce < 6; ++bounce) {
        CornellHit hit = traceCornellScene(origin, direction);
        if (hit.t >= 1e20) {
            radiance += throughput * vec3(0.012, 0.015, 0.018);
            break;
        }
        if (hit.material == CORNELL_LIGHT) {
            radiance += throughput * hit.emission;
            break;
        }
        if (hit.material == CORNELL_DIFFUSE) {
            radiance += throughput * (sampleCornellDirect(hit) + cornellAmbientBounce(hit));
            break;
        }
        if (hit.material == CORNELL_METAL) {
            vec3 V = normalize(-direction);
            vec3 f0 = mix(vec3(0.04), hit.albedo, hit.metallic);
            vec3 F = fresnelSchlick(max(dot(hit.normal, V), 0.0), f0);
            radiance += throughput * sampleCornellPbrDirect(hit, V);
            vec3 reflected = normalize(reflect(direction, hit.normal));
            throughput *= F * mix(0.98, 0.55, hit.roughness);
            origin = hit.position + hit.normal * 0.003;
            direction = reflected;
            continue;
        }

        vec3 outwardNormal = hit.normal;
        bool frontFace = dot(direction, outwardNormal) < 0.0;
        vec3 orientedNormal = frontFace ? outwardNormal : -outwardNormal;
        float etaRatio = frontFace ? (1.0 / hit.eta) : hit.eta;
        float cosTheta = min(dot(-direction, orientedNormal), 1.0);
        float sin2Theta = max(1.0 - cosTheta * cosTheta, 0.0);
        float reflectance = schlickFresnel(cosTheta, hit.eta);
        vec3 reflected = normalize(reflect(direction, orientedNormal));
        vec3 reflectedColor = shadeCornellOneBounce(hit.position + orientedNormal * 0.003, reflected);
        radiance += throughput * reflectedColor * reflectance * 0.45;

        bool cannotRefract = etaRatio * sqrt(sin2Theta) > 1.0;
        if (cannotRefract) {
            throughput *= hit.albedo;
            origin = hit.position + orientedNormal * 0.003;
            direction = reflected;
        } else {
            vec3 refracted = normalize(refract(direction, orientedNormal, etaRatio));
            throughput *= mix(vec3(1.0), hit.albedo, 0.22) * (1.0 - reflectance * 0.35);
            origin = hit.position + refracted * 0.004;
            direction = refracted;
        }
    }

    return radiance;
}

vec3 renderCornell(vec3 rayOrigin, vec3 rayDirection) {
    vec3 localOrigin = rayOrigin - vec3(0.0, 0.45, 0.0);
    vec3 localDirection = normalize(rayDirection);
    CornellHit primaryHit = traceCornellScene(localOrigin, localDirection);
    if (uViewMode == 1) {
        return primaryHit.t < 1e20 ? vec3(1.0) : vec3(0.0);
    }
    if (uViewMode == 2) {
        return primaryHit.t < 1e20 ? primaryHit.normal * 0.5 + 0.5 : vec3(0.0);
    }
    if (uViewMode == 3) {
        if (primaryHit.t >= 1e20) {
            return vec3(0.0);
        }
        return primaryHit.material == CORNELL_LIGHT ? normalize(primaryHit.emission) : primaryHit.albedo;
    }
    return traceCornellRadiance(localOrigin, localDirection);
}
)" + R"(
bool intersectAabb(vec3 rayOrigin, vec3 rayDirection, vec3 boundsMin, vec3 boundsMax, float closestT) {
    vec3 safeDirection = mix(vec3(1e-8), rayDirection, greaterThan(abs(rayDirection), vec3(1e-8)));
    vec3 invDirection = 1.0 / safeDirection;
    vec3 t0 = (boundsMin - rayOrigin) * invDirection;
    vec3 t1 = (boundsMax - rayOrigin) * invDirection;
    vec3 tNear = min(t0, t1);
    vec3 tFar = max(t0, t1);
    float enterT = max(max(tNear.x, tNear.y), max(tNear.z, 0.0));
    float exitT = min(min(tFar.x, tFar.y), tFar.z);
    return enterT <= min(exitT, closestT);
}

bool intersectTriangle(int triangleIndex, vec3 rayOrigin, vec3 rayDirection, inout HitRecord hit) {
    GpuTriangle tri = triangles[triangleIndex];
    vec3 p0 = tri.p0.xyz;
    vec3 p1 = tri.p1.xyz;
    vec3 p2 = tri.p2.xyz;
    vec3 e1 = p1 - p0;
    vec3 e2 = p2 - p0;
    vec3 pvec = cross(rayDirection, e2);
    float det = dot(e1, pvec);
    if (abs(det) < 1e-7) {
        return false;
    }

    float invDet = 1.0 / det;
    vec3 tvec = rayOrigin - p0;
    float u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) {
        return false;
    }

    vec3 qvec = cross(tvec, e1);
    float v = dot(rayDirection, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) {
        return false;
    }

    float t = dot(e2, qvec) * invDet;
    if (t <= 0.0002 || t >= hit.t) {
        return false;
    }

    float w = 1.0 - u - v;
    vec3 normal = normalize(tri.n0.xyz * w + tri.n1.xyz * u + tri.n2.xyz * v);
    if (dot(normal, rayDirection) > 0.0) {
        normal = -normal;
    }

    hit.t = t;
    hit.triangle_index = triangleIndex;
    hit.position = rayOrigin + rayDirection * t;
    hit.normal = normal;
    hit.barycentric = vec3(w, u, v);
    hit.uv = tri.uv0.xy * w + tri.uv1.xy * u + tri.uv2.xy * v;
    return true;
}

bool intersectTriangleAny(int triangleIndex, vec3 rayOrigin, vec3 rayDirection, float maxT) {
    GpuTriangle tri = triangles[triangleIndex];
    vec3 p0 = tri.p0.xyz;
    vec3 e1 = tri.p1.xyz - p0;
    vec3 e2 = tri.p2.xyz - p0;
    vec3 pvec = cross(rayDirection, e2);
    float det = dot(e1, pvec);
    if (abs(det) < 1e-7) {
        return false;
    }

    float invDet = 1.0 / det;
    vec3 tvec = rayOrigin - p0;
    float u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) {
        return false;
    }

    vec3 qvec = cross(tvec, e1);
    float v = dot(rayDirection, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) {
        return false;
    }

    float t = dot(e2, qvec) * invDet;
    return t > RT_MIN_HIT_TIME && t < maxT;
}

HitRecord traceClosest(vec3 rayOrigin, vec3 rayDirection) {
    HitRecord hit;
    hit.t = 1e30;
    hit.triangle_index = -1;
    hit.position = vec3(0.0);
    hit.normal = vec3(0.0, 1.0, 0.0);
    hit.barycentric = vec3(1.0, 0.0, 0.0);
    hit.uv = vec2(0.0);

    if (uBvhNodeCount <= 0 || uTriangleCount <= 0) {
        return hit;
    }

    int stack[96];
    int stackSize = 0;
    stack[stackSize++] = 0;

    while (stackSize > 0) {
        int nodeIndex = stack[--stackSize];
        if (nodeIndex < 0 || nodeIndex >= uBvhNodeCount) {
            continue;
        }

        GpuBvhNode node = bvh_nodes[nodeIndex];
        if (!intersectAabb(rayOrigin, rayDirection, node.bounds_min.xyz, node.bounds_max.xyz, hit.t)) {
            continue;
        }

        if (node.triangle_count > 0) {
            for (int i = 0; i < node.triangle_count; ++i) {
                int triangleIndex = node.first_triangle + i;
                if (triangleIndex >= 0 && triangleIndex < uTriangleCount) {
                    intersectTriangle(triangleIndex, rayOrigin, rayDirection, hit);
                }
            }
        } else {
            if (stackSize < 94) {
                stack[stackSize++] = node.left_index;
                stack[stackSize++] = node.right_index;
            }
        }
    }

    return hit;
}

bool traceAny(vec3 rayOrigin, vec3 rayDirection, float maxT) {
    if (uBvhNodeCount <= 0 || uTriangleCount <= 0) {
        return false;
    }

    int stack[96];
    int stackSize = 0;
    stack[stackSize++] = 0;

    while (stackSize > 0) {
        int nodeIndex = stack[--stackSize];
        if (nodeIndex < 0 || nodeIndex >= uBvhNodeCount) {
            continue;
        }

        GpuBvhNode node = bvh_nodes[nodeIndex];
        if (!intersectAabb(rayOrigin, rayDirection, node.bounds_min.xyz, node.bounds_max.xyz, maxT)) {
            continue;
        }

        if (node.triangle_count > 0) {
            for (int i = 0; i < node.triangle_count; ++i) {
                int triangleIndex = node.first_triangle + i;
                if (triangleIndex >= 0 && triangleIndex < uTriangleCount && intersectTriangleAny(triangleIndex, rayOrigin, rayDirection, maxT)) {
                    return true;
                }
            }
        } else if (stackSize < 94) {
            stack[stackSize++] = node.left_index;
            stack[stackSize++] = node.right_index;
        }
    }

    return false;
}

CornellHit traceImportedModelInCornell(vec3 localOrigin, vec3 localDirection) {
    CornellHit modelHit = emptyCornellHit();
    if (uBvhNodeCount <= 0 || uTriangleCount <= 0 || uMaterialCount <= 0) {
        return modelHit;
    }

    const float modelScale = 0.42;
    const vec3 modelTranslate = vec3(0.02, -0.58, -0.50);
    vec3 normalizedOrigin = (localOrigin - modelTranslate) / modelScale;
    vec3 normalizedDirection = localDirection / modelScale;
    vec3 objectOrigin = (uInvModel * vec4(normalizedOrigin, 1.0)).xyz;
    vec3 objectDirection = normalize((uInvModel * vec4(normalizedDirection, 0.0)).xyz);

    HitRecord hit = traceClosest(objectOrigin, objectDirection);
    if (hit.triangle_index < 0) {
        return modelHit;
    }

    vec3 normalizedPosition = (uModel * vec4(hit.position, 1.0)).xyz;
    vec3 localPosition = modelTranslate + normalizedPosition * modelScale;
    float localT = length(localPosition - localOrigin);
    if (dot(localPosition - localOrigin, localDirection) <= 0.0) {
        return modelHit;
    }

    GpuTriangle hitTriangle = triangles[hit.triangle_index];
    int materialIndex = clamp(hitTriangle.material_index, 0, max(uMaterialCount - 1, 0));
    GpuMaterial material = materials[materialIndex];
    vec3 baseColor = resolveBaseColor(material, hit.uv);
    float metallic = resolveMetallic(material, hit.uv);
    float roughness = resolveRoughness(material, hit.uv);
    float ao = resolveAo(material, hit.uv);
    vec3 emissive = resolveEmissive(material, hit.uv);
    vec3 localNormal = normalize(uNormalMatrix * hit.normal);
    if (dot(localNormal, localDirection) > 0.0) {
        localNormal = -localNormal;
    }

    modelHit.t = localT;
    modelHit.position = localPosition;
    modelHit.normal = localNormal;
    modelHit.albedo = max(baseColor * mix(0.45, 1.0, ao), vec3(0.0));
    modelHit.emission = emissive;
    modelHit.material = metallic > 0.55 ? CORNELL_METAL : CORNELL_DIFFUSE;
    modelHit.eta = 1.0;
    modelHit.roughness = roughness;
    modelHit.metallic = metallic;
    return modelHit;
}

vec3 shadeImportedModelInCornell(CornellHit hit, vec3 localViewDirection) {
    vec3 V = normalize(-localViewDirection);
    vec3 color = sampleCornellPbrDirect(hit, V) + cornellAmbientBounce(hit) * (1.0 - 0.5 * hit.metallic);
    if (hit.metallic > 0.05) {
        vec3 reflected = normalize(reflect(localViewDirection, hit.normal));
        vec3 reflectedColor = shadeCornellOneBounce(hit.position + hit.normal * 0.003, reflected);
        color += reflectedColor * mix(0.28, 1.0, 1.0 - hit.roughness) * hit.metallic;
    }
    color += hit.emission;
    float rim = pow(clamp(1.0 - max(dot(hit.normal, V), 0.0), 0.0, 1.0), 3.0);
    color += hit.albedo * vec3(0.10, 0.12, 0.15) * rim;
    return color;
}

CornellHit traceCornellWithModel(vec3 localOrigin, vec3 localDirection) {
    CornellHit roomHit = traceCornellScene(localOrigin, localDirection);
    CornellHit modelHit = traceImportedModelInCornell(localOrigin, localDirection);
    return modelHit.t < roomHit.t ? modelHit : roomHit;
}

vec3 evaluateCornellBrdf(CornellHit hit, vec3 V, vec3 L) {
    vec3 N = normalize(hit.normal);
    float ndotl = max(dot(N, L), 0.0);
    if (ndotl <= 0.0) {
        return vec3(0.0);
    }
    if (hit.material == CORNELL_GLASS) {
        return vec3(0.0);
    }
    if (hit.material == CORNELL_DIFFUSE) {
        return hit.albedo / RT_PI;
    }

    vec3 H = normalize(V + L);
    float ndotv = max(dot(N, V), 0.0);
    if (ndotv <= 0.0) {
        return vec3(0.0);
    }
    vec3 f0 = mix(vec3(0.04), hit.albedo, hit.metallic);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), f0);
    float NDF = distributionGGX(N, H, hit.roughness);
    float G = geometrySmith(N, V, L, hit.roughness);
    vec3 specular = (NDF * G * F) / max(4.0 * ndotv * ndotl, 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - hit.metallic);
    return kD * hit.albedo / RT_PI + specular;
}

vec3 photonCacheEstimate(CornellHit hit, inout uint rngState) {
    if (uEnablePhotonCache == 0 || hit.material == CORNELL_LIGHT || hit.material == CORNELL_GLASS) {
        return vec3(0.0);
    }

    vec3 N = normalize(hit.normal);
    vec3 cache = vec3(0.0);
    float radius = max(uPhotonRadius, 0.02);
    int photonSamples = (uTraceAlgorithm == RT_TRACE_PHOTON) ? 14 : 6;
    for (int i = 0; i < 14; ++i) {
        if (i >= photonSamples) {
            break;
        }
        vec3 lightPoint = vec3(mix(-0.32, 0.32, rng(rngState)), 0.995, mix(-0.62, -0.10, rng(rngState)));
        vec3 lightDir = cosineHemisphereSample(vec3(0.0, -1.0, 0.0), rngState);
        CornellHit photonHit = traceCornellWithModel(lightPoint + vec3(0.0, -0.002, 0.0), lightDir);
        if (photonHit.t >= 1e20 || photonHit.material == CORNELL_LIGHT || photonHit.material == CORNELL_GLASS) {
            continue;
        }
        vec3 delta = hit.position - photonHit.position;
        float d2 = dot(delta, delta);
        if (d2 > radius * radius) {
            continue;
        }
        float kernel = exp(-d2 / max(radius * radius * 0.35, 1e-5));
        float align = max(dot(N, normalize(photonHit.normal)), 0.0);
        cache += photonHit.albedo * kernel * (0.4 + 0.6 * align);
    }
    return cache * (uPhotonIntensity / float(max(photonSamples, 1)));
}

vec3 sampleCornellNee(CornellHit hit, vec3 V, inout uint rngState) {
    if (uEnableNee == 0 || hit.material == CORNELL_LIGHT || hit.material == CORNELL_GLASS) {
        return vec3(0.0);
    }

    vec3 lightPoint = vec3(mix(-0.32, 0.32, rng(rngState)), 0.995, mix(-0.62, -0.10, rng(rngState)));
    vec3 toLight = lightPoint - hit.position;
    float dist2 = max(dot(toLight, toLight), 1e-4);
    float dist = sqrt(dist2);
    vec3 L = toLight / dist;
    float lightFacing = max(dot(vec3(0.0, -1.0, 0.0), -L), 0.0);
    float surfaceFacing = max(dot(hit.normal, L), 0.0);
    if (surfaceFacing <= 0.0 || lightFacing <= 0.0) {
        return vec3(0.0);
    }

    CornellHit blocker = traceCornellWithModel(hit.position + hit.normal * 0.003, L);
    if (blocker.t < dist - 0.006 && blocker.material != CORNELL_LIGHT) {
        return vec3(0.0);
    }

    vec3 lightEmission = vec3(18.0, 15.5, 11.5);
    float lightArea = 0.64 * 0.52;
    vec3 brdf = evaluateCornellBrdf(hit, V, L);
    return brdf * lightEmission * lightArea * surfaceFacing * lightFacing / dist2;
}

vec3 traceCornellPath(vec3 localOrigin, vec3 localDirection, inout uint rngState, int maxBounces, int maxNeeBounces) {
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    vec3 origin = localOrigin;
    vec3 direction = normalize(localDirection);
    int clampedMaxBounces = clamp(maxBounces, 1, RT_MAX_PATH_BOUNCES);
    int clampedMaxNeeBounces = clamp(maxNeeBounces, 0, clampedMaxBounces);

    for (int bounce = 0; bounce < RT_MAX_PATH_BOUNCES; ++bounce) {
        if (bounce >= clampedMaxBounces) {
            break;
        }

        CornellHit hit = traceCornellWithModel(origin, direction);
        if (hit.t >= 1e20) {
            radiance += throughput * vec3(0.010, 0.013, 0.017);
            break;
        }
        if (hit.material == CORNELL_LIGHT) {
            radiance += throughput * hit.emission;
            break;
        }

        vec3 V = normalize(-direction);
        if (bounce < clampedMaxNeeBounces && (uTraceAlgorithm == RT_TRACE_PATH_NEE || uTraceAlgorithm == RT_TRACE_PHOTON)) {
            radiance += throughput * sampleCornellNee(hit, V, rngState);
        }
        if (uTraceAlgorithm == RT_TRACE_PHOTON) {
            radiance += throughput * photonCacheEstimate(hit, rngState);
        }

        vec3 n = hit.normal;
        vec3 newDirection = direction;
        if (hit.material == CORNELL_DIFFUSE) {
            newDirection = cosineHemisphereSample(n, rngState);
            throughput *= hit.albedo;
        } else if (hit.material == CORNELL_METAL) {
            vec3 reflectDir = normalize(reflect(direction, n));
            vec3 roughDir = cosineHemisphereSample(n, rngState);
            newDirection = normalize(mix(reflectDir, roughDir, hit.roughness * hit.roughness));
            vec3 f0 = mix(vec3(0.04), hit.albedo, hit.metallic);
            vec3 F = fresnelSchlick(max(dot(n, V), 0.0), f0);
            throughput *= F * mix(0.98, 0.50, hit.roughness);
        } else {
            vec3 outwardNormal = n;
            bool frontFace = dot(direction, outwardNormal) < 0.0;
            vec3 orientedNormal = frontFace ? outwardNormal : -outwardNormal;
            float etaRatio = frontFace ? (1.0 / hit.eta) : hit.eta;
            float cosTheta = min(dot(-direction, orientedNormal), 1.0);
            float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
            bool cannotRefract = etaRatio * sinTheta > 1.0;
            float reflectance = schlickFresnel(cosTheta, hit.eta);
            if (cannotRefract || rng(rngState) < reflectance) {
                newDirection = normalize(reflect(direction, orientedNormal));
                throughput *= mix(vec3(1.0), hit.albedo, 0.15);
            } else {
                newDirection = normalize(refract(direction, orientedNormal, etaRatio));
                throughput *= mix(vec3(1.0), hit.albedo, 0.06);
            }
        }

        throughput = min(throughput, vec3(6.0));

        if (bounce >= 2) {
            float p = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.08, 0.98);
            if (rng(rngState) > p) {
                break;
            }
            throughput /= p;
        }

        origin = hit.position + newDirection * RT_RAY_NUDGE;
        direction = normalize(newDirection);
    }

    return min(radiance, vec3(8.0));
}
)"+R"(
vec3 rayDirectionForPixel(vec2 uv) {
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 nearPoint = uInvViewProjection * vec4(ndc, -1.0, 1.0);
    vec4 farPoint = uInvViewProjection * vec4(ndc, 1.0, 1.0);
    nearPoint.xyz /= max(abs(nearPoint.w), 1e-6);
    farPoint.xyz /= max(abs(farPoint.w), 1e-6);
    return normalize(farPoint.xyz - nearPoint.xyz);
}

vec3 evaluateModelBrdf(vec3 baseColor, float metallic, float roughness, vec3 N, vec3 V, vec3 L) {
    float ndotl = max(dot(N, L), 0.0);
    float ndotv = max(dot(N, V), 0.0);
    if (ndotl <= 0.0 || ndotv <= 0.0) {
        return vec3(0.0);
    }

    vec3 H = normalize(V + L);
    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), f0);
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 specular = (NDF * G * F) / max(4.0 * ndotv * ndotl, 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return kD * baseColor / RT_PI + specular;
}

vec3 sampleImportedLightNee(vec3 position,
                            vec3 N,
                            vec3 V,
                            vec3 baseColor,
                            float metallic,
                            float roughness,
                            inout uint rngState) {
    if (uLightTriangleCount <= 0) {
        return vec3(0.0);
    }

    int lightIndex = min(int(floor(rng(rngState) * float(uLightTriangleCount))), uLightTriangleCount - 1);
    GpuLightTriangle light = light_triangles[lightIndex];

    float su = sqrt(max(rng(rngState), 1e-6));
    float u = rng(rngState);
    vec3 lightPos = light.p0.xyz * (1.0 - su) +
                    light.p1.xyz * (su * (1.0 - u)) +
                    light.p2.xyz * (su * u);

    vec3 toLight = lightPos - position;
    float dist2 = max(dot(toLight, toLight), 1e-6);
    float dist = sqrt(dist2);
    vec3 L = toLight / dist;

    float surfaceCos = max(dot(N, L), 0.0);
    float lightCos = abs(dot(normalize(light.normal_area.xyz), -L));
    if (surfaceCos <= 0.0 || lightCos <= 0.0) {
        return vec3(0.0);
    }

    float shadowMax = max(dist - 0.006, 0.0);
    if (shadowMax <= 0.0 || traceAny(position + N * RT_RAY_NUDGE, L, shadowMax)) {
        return vec3(0.0);
    }

    float area = max(light.normal_area.w, 1e-6);
    vec3 brdf = evaluateModelBrdf(baseColor, metallic, roughness, N, V, L);
    return brdf * light.emission.rgb * surfaceCos * lightCos * area * float(uLightTriangleCount) / dist2;
}

vec3 traceModelPath(vec3 objectOrigin, vec3 objectDirection, vec3 sunDirectionObject, inout uint rngState, int maxBounces, int maxNeeBounces) {
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    vec3 origin = objectOrigin;
    vec3 direction = normalize(objectDirection);
    int clampedMaxBounces = clamp(maxBounces, 1, RT_MAX_PATH_BOUNCES);
    int clampedMaxNeeBounces = clamp(maxNeeBounces, 0, clampedMaxBounces);

    for (int bounce = 0; bounce < RT_MAX_PATH_BOUNCES; ++bounce) {
        if (bounce >= clampedMaxBounces) {
            break;
        }

        HitRecord hit = traceClosest(origin, direction);
        if (hit.triangle_index < 0) {
            vec3 sky = mix(uClearColor * 0.50, uClearColor * 1.38 + vec3(0.08, 0.10, 0.12), clamp(direction.y * 0.5 + 0.5, 0.0, 1.0));
            radiance += throughput * sky;
            break;
        }

        GpuTriangle tri = triangles[hit.triangle_index];
        int materialIndex = clamp(tri.material_index, 0, max(uMaterialCount - 1, 0));
        GpuMaterial material = materials[materialIndex];
        vec3 baseColor = resolveBaseColor(material, hit.uv);
        float metallic = resolveMetallic(material, hit.uv);
        float roughness = resolveRoughness(material, hit.uv);
        float ao = resolveAo(material, hit.uv);
        vec3 emissive = resolveEmissive(material, hit.uv);

        vec3 N = normalize(hit.normal);
        if (dot(N, direction) > 0.0) {
            N = -N;
        }
        vec3 V = normalize(-direction);
        if (max(emissive.r, max(emissive.g, emissive.b)) > 0.0) {
            radiance += throughput * emissive;
            break;
        }

        if (bounce < clampedMaxNeeBounces && uEnableNee != 0) {
            radiance += throughput * sampleImportedLightNee(hit.position, N, V, baseColor, metallic, roughness, rngState);

            vec3 L = normalize(-sunDirectionObject);
            float ndotl = max(dot(N, L), 0.0);
            if (ndotl > 0.0) {
                bool occluded = traceAny(hit.position + N * 0.0025, L, 1e20);
                if (!occluded) {
                    vec3 direct = evaluateModelBrdf(baseColor, metallic, roughness, N, V, L) * uSunColor * ndotl;
                    radiance += throughput * direct;
                }
            }
        }

        if (uTraceAlgorithm == RT_TRACE_PHOTON && uEnablePhotonCache != 0) {
            float hemi = max(dot(N, vec3(0.0, 1.0, 0.0)), 0.0);
            radiance += throughput * baseColor * ao * hemi * (0.04 * uPhotonIntensity);
        }

        vec3 newDirection;
        if (metallic > 0.55) {
            vec3 reflectDir = normalize(reflect(direction, N));
            vec3 roughDir = cosineHemisphereSample(N, rngState);
            newDirection = normalize(mix(reflectDir, roughDir, roughness * roughness));
            vec3 f0 = mix(vec3(0.04), baseColor, metallic);
            vec3 F = fresnelSchlick(max(dot(N, V), 0.0), f0);
            throughput *= F * mix(0.98, 0.55, roughness) * ao;
        } else {
            newDirection = cosineHemisphereSample(N, rngState);
            throughput *= baseColor * ao;
        }

        throughput = min(throughput, vec3(6.0));

        if (bounce >= 2) {
            float p = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.07, 0.98);
            if (rng(rngState) > p) {
                break;
            }
            throughput /= p;
        }

        origin = hit.position + newDirection * RT_RAY_NUDGE;
        direction = normalize(newDirection);
    }

    return min(radiance, vec3(8.0));
}

void main() {
    vec3 worldOrigin = uCameraPosition;
    vec3 worldDirection = rayDirectionForPixel(vUv);
    bool usePath = (uTraceAlgorithm != RT_TRACE_HYBRID) && (uViewMode == 0);
    vec3 color = vec3(0.0);

    if (usePath) {
        int spp = clamp(uSamplesPerFrame, 1, RT_MAX_SPP);
        ivec2 histSize = max(textureSize(uHistoryTexture, 0), ivec2(1));
        vec2 invRes = 1.0 / vec2(histSize);
        vec3 sampleAccum = vec3(0.0);
        int frameIndex = max(uFrameIndex, 0);
        for (int s = 0; s < RT_MAX_SPP; ++s) {
            if (s >= spp) {
                break;
            }
            int sampleMaxBounces;
            int sampleMaxNeeBounces;
            resolvePathSampleBudget(s, frameIndex, sampleMaxBounces, sampleMaxNeeBounces);

            uint frameSeed = uint(frameIndex);
            uint seed = hashUint(uint(gl_FragCoord.x) * 1973u ^ uint(gl_FragCoord.y) * 9277u ^ (frameSeed + 1u) * 26699u ^ uint(s) * 104729u);
            vec2 jitter = (vec2(rng(seed), rng(seed)) - 0.5) * invRes;
            vec3 sampleWorldDirection = rayDirectionForPixel(clamp(vUv + jitter, vec2(0.0), vec2(1.0)));
            if (uSceneMode == RT_SCENE_CORNELL) {
                vec3 localOrigin = worldOrigin - vec3(0.0, 0.45, 0.0);
                sampleAccum += traceCornellPath(localOrigin, normalize(sampleWorldDirection), seed, sampleMaxBounces, sampleMaxNeeBounces);
            } else {
                vec3 objectOrigin = (uInvModel * vec4(worldOrigin, 1.0)).xyz;
                vec3 objectDirection = normalize((uInvModel * vec4(sampleWorldDirection, 0.0)).xyz);
                vec3 sunDirectionObject = normalize((uInvModel * vec4(uSunDirection, 0.0)).xyz);
                sampleAccum += traceModelPath(objectOrigin, objectDirection, sunDirectionObject, seed, sampleMaxBounces, sampleMaxNeeBounces);
            }
        }
        color = sampleAccum / float(max(spp, 1));
    } else if (uSceneMode == RT_SCENE_CORNELL) {
        vec3 localOrigin = worldOrigin - vec3(0.0, 0.45, 0.0);
        vec3 localDirection = normalize(worldDirection);
        CornellHit roomHit = traceCornellScene(localOrigin, localDirection);
        CornellHit modelHit = traceImportedModelInCornell(localOrigin, localDirection);
        bool modelIsPrimary = modelHit.t < roomHit.t;
        color = modelIsPrimary ? shadeImportedModelInCornell(modelHit, localDirection) : renderCornell(worldOrigin, worldDirection);
        if (uViewMode == 1 && modelIsPrimary) {
            color = vec3(1.0);
        } else if (uViewMode == 2 && modelIsPrimary) {
            color = modelHit.normal * 0.5 + 0.5;
        } else if (uViewMode == 3 && modelIsPrimary) {
            color = modelHit.albedo;
        }
    } else {
        vec3 objectOrigin = (uInvModel * vec4(worldOrigin, 1.0)).xyz;
        vec3 objectDirection = normalize((uInvModel * vec4(worldDirection, 0.0)).xyz);
        HitRecord hit = traceClosest(objectOrigin, objectDirection);
        if (hit.triangle_index < 0) {
            color = mix(uClearColor * 0.65, uClearColor * 1.20, clamp(vUv.y, 0.0, 1.0));
        } else {
            GpuTriangle hitTriangle = triangles[hit.triangle_index];
            int materialIndex = clamp(hitTriangle.material_index, 0, max(uMaterialCount - 1, 0));
            GpuMaterial material = materials[materialIndex];
            vec3 baseColor = resolveBaseColor(material, hit.uv);
            vec3 worldNormal = normalize(uNormalMatrix * hit.normal);
            float metallic = resolveMetallic(material, hit.uv);
            float roughness = resolveRoughness(material, hit.uv);
            float ao = resolveAo(material, hit.uv);
            vec3 emissive = resolveEmissive(material, hit.uv);

            if (uViewMode == 1) {
                color = vec3(1.0);
            } else if (uViewMode == 2) {
                color = worldNormal * 0.5 + 0.5;
            } else if (uViewMode == 3) {
                color = baseColor;
            } else {
                vec3 lightDirectionWorld = normalize(-uSunDirection);
                vec3 lightDirectionObject = normalize((uInvModel * vec4(lightDirectionWorld, 0.0)).xyz);
                vec3 worldPosition = (uModel * vec4(hit.position, 1.0)).xyz;
                vec3 V = normalize(uCameraPosition - worldPosition);
                vec3 H = normalize(V + lightDirectionWorld);
                float ndotl = max(dot(worldNormal, lightDirectionWorld), 0.0);
                float ndotv = max(dot(worldNormal, V), 0.0);

                float visibility = 1.0;
                if (uEnableShadows != 0 && ndotl > 0.0) {
                    vec3 shadowOrigin = hit.position + hit.normal * 0.0025;
                    bool occluded = traceAny(shadowOrigin, lightDirectionObject, 1e20);
                    visibility = occluded ? (1.0 - clamp(uShadowStrength, 0.0, 1.0)) : 1.0;
                }

                vec3 f0 = mix(vec3(0.04), baseColor, metallic);
                vec3 F = fresnelSchlick(max(dot(H, V), 0.0), f0);
                float NDF = distributionGGX(worldNormal, H, roughness);
                float G = geometrySmith(worldNormal, V, lightDirectionWorld, roughness);
                vec3 specular = (NDF * G * F) / max(4.0 * ndotv * ndotl, 1e-4);
                vec3 kS = F;
                vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
                vec3 direct = (kD * baseColor / RT_PI + specular) * uSunColor * ndotl * visibility;
                vec3 R = reflect(-V, worldNormal);
                vec3 sky = mix(uClearColor * 0.5, uClearColor * 1.35 + vec3(0.10, 0.12, 0.14), clamp(R.y * 0.5 + 0.5, 0.0, 1.0));
                vec3 skySpecular = sky * F * (1.0 - roughness * 0.72);

                vec3 ambientDiffuse = baseColor * ao * max(uAmbientStrength, 0.0);
                vec3 ambientSpecular = f0 * (1.0 - roughness) * ao * max(uAmbientStrength, 0.0) * 0.35;
                color = ambientDiffuse + ambientSpecular + direct + skySpecular * max(0.2, metallic) + emissive;
            }
        }
    }

    if (usePath && uOutputLinear != 0) {
        vec3 linearColor = max(color, vec3(0.0));
        if (uHistoryValid != 0) {
            vec3 history = max(texture(uHistoryTexture, vUv).rgb, vec3(0.0));
            float historyCount = max(float(uHistoryFrameCount), 1.0);
            linearColor = (history * historyCount + linearColor) / (historyCount + 1.0);
        }
        fragColor = vec4(min(linearColor, vec3(64.0)), 1.0);
        return;
    }

    color *= max(uExposure, 0.0);
    color = color / (color + vec3(1.0));
    vec3 srgbColor = linearToSrgb(color);
    fragColor = vec4(srgbColor, 1.0);
}
)";
}

const char* displayFragmentShaderSource() {
    return R"(#version 430 core
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uDisplayTexture;
uniform float uExposure;

vec3 linearToSrgb(vec3 value) {
    return pow(max(value, vec3(0.0)), vec3(1.0 / 2.2));
}

void main() {
    vec3 color = max(texture(uDisplayTexture, vUv).rgb, vec3(0.0));
    color *= max(uExposure, 0.0);
    color = color / (color + vec3(1.0));
    fragColor = vec4(linearToSrgb(color), 1.0);
}
)";
}

void setMatrix4(QOpenGLFunctions_4_3_Core* gl, QOpenGLShaderProgram* program, const char* name, const Eigen::Matrix4f& matrix) {
    gl->glUniformMatrix4fv(program->uniformLocation(name), 1, GL_FALSE, matrix.data());
}

void setMatrix3(QOpenGLFunctions_4_3_Core* gl, QOpenGLShaderProgram* program, const char* name, const Eigen::Matrix3f& matrix) {
    gl->glUniformMatrix3fv(program->uniformLocation(name), 1, GL_FALSE, matrix.data());
}

void fillVec4(float output[4], const Eigen::Vector3f& value, float w = 0.0f) {
    output[0] = value.x();
    output[1] = value.y();
    output[2] = value.z();
    output[3] = w;
}

void fillUv(float output[4], const Eigen::Vector2f& value) {
    output[0] = value.x();
    output[1] = value.y();
    output[2] = 0.0f;
    output[3] = 0.0f;
}

void fillRect(float output[4], const QRect& rect, const QSize& atlas_size) {
    const float atlas_width = static_cast<float>(std::max(atlas_size.width(), 1));
    const float atlas_height = static_cast<float>(std::max(atlas_size.height(), 1));
    const float rect_width = static_cast<float>(std::max(rect.width(), 1));
    const float rect_height = static_cast<float>(std::max(rect.height(), 1));
    output[0] = (static_cast<float>(rect.x()) + 0.5f) / atlas_width;
    output[1] = (static_cast<float>(rect.y()) + 0.5f) / atlas_height;
    output[2] = std::max(rect_width - 1.0f, 0.0f) / atlas_width;
    output[3] = std::max(rect_height - 1.0f, 0.0f) / atlas_height;
}

const TextureData* chooseAlbedoTexture(const MaterialData& material) {
    if (material.base_color_texture.valid()) {
        return &material.base_color_texture;
    }
    if (material.diffuse_texture.valid()) {
        return &material.diffuse_texture;
    }
    return nullptr;
}

const TextureData* chooseMetallicTexture(const MaterialData& material) {
    if (material.metallic_texture.valid()) {
        return &material.metallic_texture;
    }
    return nullptr;
}

const TextureData* chooseRoughnessTexture(const MaterialData& material) {
    if (material.roughness_texture.valid()) {
        return &material.roughness_texture;
    }
    return nullptr;
}

const TextureData* chooseAoTexture(const MaterialData& material) {
    if (material.ao_texture.valid()) {
        return &material.ao_texture;
    }
    return nullptr;
}

const TextureData* chooseEmissiveTexture(const MaterialData& material) {
    if (material.emissive_texture.valid()) {
        return &material.emissive_texture;
    }
    return nullptr;
}

QImage scaleImageToMaxDimension(const QImage& image, int max_dimension) {
    if (image.isNull()) {
        return image;
    }
    const int safe_max_dimension = std::max(max_dimension, 1);
    if (image.width() <= safe_max_dimension && image.height() <= safe_max_dimension) {
        return image;
    }
    return image.scaled(safe_max_dimension,
                        safe_max_dimension,
                        Qt::KeepAspectRatio,
                        Qt::SmoothTransformation);
}

bool packAtlas(std::vector<PendingAtlasTexture>& pending_textures, int max_texture_size, int max_tile_size, int& atlas_width, int& atlas_height) {
    int total_area = 0;
    int widest = 1;
    for (PendingAtlasTexture& pending : pending_textures) {
        pending.image = scaleImageToMaxDimension(pending.source_image, max_tile_size);
        if (pending.image.isNull()) {
            continue;
        }
        widest = std::max(widest, pending.image.width() + kAtlasPadding * 2);
        total_area += (pending.image.width() + kAtlasPadding * 2) * (pending.image.height() + kAtlasPadding * 2);
    }

    atlas_width = std::min(max_texture_size,
                           std::max(nextPowerOfTwo(static_cast<int>(std::sqrt(static_cast<double>(std::max(total_area, 1))))),
                                    nextPowerOfTwo(widest)));
    atlas_width = std::max(atlas_width, 64);

    while (true) {
        int cursor_x = kAtlasPadding;
        int cursor_y = kAtlasPadding;
        int row_height = 0;

        for (PendingAtlasTexture& pending : pending_textures) {
            if (pending.image.isNull()) {
                pending.rect = QRect();
                continue;
            }
            if (cursor_x + pending.image.width() + kAtlasPadding > atlas_width) {
                cursor_x = kAtlasPadding;
                cursor_y += row_height + kAtlasPadding;
                row_height = 0;
            }

            pending.rect = QRect(cursor_x, cursor_y, pending.image.width(), pending.image.height());
            cursor_x += pending.image.width() + kAtlasPadding;
            row_height = std::max(row_height, pending.image.height());
        }

        atlas_height = nextPowerOfTwo(cursor_y + row_height + kAtlasPadding);
        if (atlas_height <= max_texture_size) {
            return true;
        }
        if (atlas_width >= max_texture_size) {
            return false;
        }
        atlas_width = std::min(atlas_width * 2, max_texture_size);
    }
}

bool buildAtlasImage(std::vector<PendingAtlasTexture>& pending_textures, int max_texture_size, QImage& atlas) {
    if (pending_textures.empty()) {
        atlas = QImage();
        return false;
    }

    int atlas_width = 0;
    int atlas_height = 0;
    bool packed = false;
    const int largest_tile_size = std::min(kMaxRayTraceAtlasTileSize, std::max(max_texture_size - kAtlasPadding * 2, 1));
    for (int tile_size = largest_tile_size; tile_size >= kMinRayTraceAtlasTileSize; tile_size /= 2) {
        if (packAtlas(pending_textures, max_texture_size, tile_size, atlas_width, atlas_height)) {
            packed = true;
            break;
        }
    }
    if (!packed && largest_tile_size < kMinRayTraceAtlasTileSize) {
        packed = packAtlas(pending_textures, max_texture_size, largest_tile_size, atlas_width, atlas_height);
    }
    if (!packed || atlas_height > max_texture_size) {
        atlas = QImage();
        return false;
    }

    atlas = QImage(atlas_width, atlas_height, QImage::Format_RGBA8888);
    atlas.fill(Qt::transparent);
    QPainter painter(&atlas);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (const PendingAtlasTexture& pending : pending_textures) {
        painter.drawImage(pending.rect.topLeft(), pending.image);
    }
    painter.end();
    return true;
}

} // namespace

OpenGLRayTracer::OpenGLRayTracer() = default;

OpenGLRayTracer::~OpenGLRayTracer() = default;

QString OpenGLRayTracer::backendName() const {
    return QStringLiteral("OpenGL Ray Trace Preview");
}

bool OpenGLRayTracer::initialize(QString* error_message) {
    if (initialized_) {
        return true;
    }

    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        if (error_message) {
            *error_message = QStringLiteral("No active OpenGL context.");
        }
        return false;
    }

    const QSurfaceFormat format = context->format();
    if (format.majorVersion() < 4 || (format.majorVersion() == 4 && format.minorVersion() < 3)) {
        if (error_message) {
            *error_message = QStringLiteral("OpenGL ray tracing path requires an OpenGL 4.3 core context for SSBO scene buffers.");
        }
        return false;
    }

    initializeOpenGLFunctions();
    if (!buildProgram(error_message)) {
        shutdown();
        return false;
    }
    createFullscreenVao();
    createAccumulationTargets();
    resetAccumulation();
    initialized_ = true;
    return true;
}

void OpenGLRayTracer::shutdown() {
    if (QOpenGLContext::currentContext()) {
        destroySceneBuffers();
        destroyAccumulationTargets();
        destroyFullscreenVao();
        destroyProgram();
    }
    initialized_ = false;
}

void OpenGLRayTracer::resize(int framebuffer_width, int framebuffer_height) {
    const int new_width = std::max(framebuffer_width, 1);
    const int new_height = std::max(framebuffer_height, 1);
    const bool changed = new_width != framebuffer_width_ || new_height != framebuffer_height_;
    framebuffer_width_ = new_width;
    framebuffer_height_ = new_height;
    if (initialized_ && changed) {
        createAccumulationTargets();
        resetAccumulation();
    }
}

void OpenGLRayTracer::uploadScene(const SceneModel& scene) {
    if (!initialized_) {
        return;
    }

    destroySceneBuffers();

    std::vector<BuildTriangle> build_triangles;
    std::vector<GpuMaterial> materials;
    MaterialAtlasImages atlases;
    GLint gl_max_texture_size = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_max_texture_size);
    const int max_texture_size = std::clamp(static_cast<int>(gl_max_texture_size), 512, 8192);
    flattenScene(scene, max_texture_size, build_triangles, materials, atlases);

    std::vector<GpuTriangle> triangles;
    std::vector<GpuBvhNode> nodes;
    buildBvh(build_triangles, triangles, nodes);

    std::vector<GpuLightTriangle> light_triangles;
    light_triangles.reserve(64);
    const auto vec3FromArray = [](const float value[4]) {
        return Eigen::Vector3f(value[0], value[1], value[2]);
    };
    for (const GpuTriangle& triangle : triangles) {
        if (triangle.material_index < 0 || triangle.material_index >= static_cast<int>(materials.size())) {
            continue;
        }
        const GpuMaterial& material = materials[triangle.material_index];
        const Eigen::Vector3f emission(material.emissive_factor[0], material.emissive_factor[1], material.emissive_factor[2]);
        if (emission.maxCoeff() <= 0.0f) {
            continue;
        }

        const Eigen::Vector3f p0 = vec3FromArray(triangle.p0);
        const Eigen::Vector3f p1 = vec3FromArray(triangle.p1);
        const Eigen::Vector3f p2 = vec3FromArray(triangle.p2);
        Eigen::Vector3f normal = (p1 - p0).cross(p2 - p0);
        const float area = 0.5f * normal.norm();
        if (area <= 1e-8f) {
            continue;
        }
        normal.normalize();

        GpuLightTriangle light {};
        fillVec4(light.p0, p0, 1.0f);
        fillVec4(light.p1, p1, 1.0f);
        fillVec4(light.p2, p2, 1.0f);
        fillVec4(light.normal_area, normal, area);
        fillVec4(light.emission, emission, 0.0f);
        light_triangles.push_back(light);
    }

    triangle_count_ = static_cast<int>(triangles.size());
    material_count_ = static_cast<int>(materials.size());
    bvh_node_count_ = static_cast<int>(nodes.size());
    light_triangle_count_ = static_cast<int>(light_triangles.size());
    albedo_texture_count_ = static_cast<int>(std::count_if(materials.begin(), materials.end(), [](const GpuMaterial& material) {
        return material.texture_flags0[0] != 0;
    }));
    metallic_texture_count_ = static_cast<int>(std::count_if(materials.begin(), materials.end(), [](const GpuMaterial& material) {
        return material.texture_flags0[1] != 0;
    }));
    roughness_texture_count_ = static_cast<int>(std::count_if(materials.begin(), materials.end(), [](const GpuMaterial& material) {
        return material.texture_flags0[2] != 0;
    }));
    ao_texture_count_ = static_cast<int>(std::count_if(materials.begin(), materials.end(), [](const GpuMaterial& material) {
        return material.texture_flags0[3] != 0;
    }));
    emissive_texture_count_ = static_cast<int>(std::count_if(materials.begin(), materials.end(), [](const GpuMaterial& material) {
        return material.texture_flags1[0] != 0;
    }));

    if (!triangles.empty()) {
        uploadBuffer(triangle_ssbo_, kTriangleBinding, triangles.data(), triangles.size() * sizeof(GpuTriangle));
    }
    if (!materials.empty()) {
        uploadBuffer(material_ssbo_, kMaterialBinding, materials.data(), materials.size() * sizeof(GpuMaterial));
    }
    if (!nodes.empty()) {
        uploadBuffer(bvh_ssbo_, kBvhBinding, nodes.data(), nodes.size() * sizeof(GpuBvhNode));
    }
    if (!light_triangles.empty()) {
        uploadBuffer(light_triangle_ssbo_, kLightTriangleBinding, light_triangles.data(), light_triangles.size() * sizeof(GpuLightTriangle));
    }
    albedo_atlas_texture_ = uploadTextureAtlas(atlases.albedo, QColor(255, 255, 255));
    metallic_atlas_texture_ = uploadTextureAtlas(atlases.metallic, QColor(0, 0, 0));
    roughness_atlas_texture_ = uploadTextureAtlas(atlases.roughness, QColor(255, 255, 255));
    ao_atlas_texture_ = uploadTextureAtlas(atlases.ao, QColor(255, 255, 255));
    emissive_atlas_texture_ = uploadTextureAtlas(atlases.emissive, QColor(0, 0, 0));
    resetAccumulation();
}

RenderStats OpenGLRayTracer::render(const FrameRenderSettings& settings) {
    RenderStats stats;
    if (!initialized_) {
        stats.note = QStringLiteral("OpenGL ray tracing backend is not initialized.");
        return stats;
    }

    GLint current_draw_framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &current_draw_framebuffer);
    target_framebuffer_ = static_cast<unsigned int>(std::max(current_draw_framebuffer, 0));

    const auto frame_start = std::chrono::high_resolution_clock::now();
    const QColor clear = settings.clear_color;

    const bool render_cornell = settings.look_dev.ray_trace.scene_mode == RayTraceSceneMode::CornellBox;
    if (!render_cornell && (!settings.scene || settings.scene->empty() || triangle_count_ <= 0 || bvh_node_count_ <= 0)) {
        glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
        glViewport(0, 0, framebuffer_width_, framebuffer_height_);
        glClearColor(clear.redF(), clear.greenF(), clear.blueF(), 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        stats.note = QStringLiteral("Ray tracing path has no uploaded scene.");
        const auto frame_end = std::chrono::high_resolution_clock::now();
        stats.frame_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(frame_end - frame_start).count();
        return stats;
    }

    const bool accumulation_enabled =
        settings.look_dev.ray_trace.view_mode == RayTraceViewMode::Lit &&
        settings.look_dev.ray_trace.integrator != RayTraceIntegrator::Hybrid;

    const auto pass_start = std::chrono::high_resolution_clock::now();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    Eigen::Matrix4f inv_view_projection = Eigen::Matrix4f::Identity();
    const Eigen::Matrix4f view_projection = settings.projection_matrix * settings.view_matrix;
    if (std::abs(view_projection.determinant()) > 1e-8f) {
        inv_view_projection = view_projection.inverse();
    }
    Eigen::Matrix4f inv_model = Eigen::Matrix4f::Identity();
    if (std::abs(settings.model_matrix.determinant()) > 1e-8f) {
        inv_model = settings.model_matrix.inverse();
    }
    Eigen::Matrix3f normal_matrix = settings.model_matrix.block<3, 3>(0, 0);
    if (std::abs(normal_matrix.determinant()) > 1e-8f) {
        normal_matrix = normal_matrix.inverse().transpose();
    }

    if (accumulation_enabled) {
        createAccumulationTargets();
        std::uint64_t signature = 1469598103934665603ULL;
        for (int i = 0; i < 16; ++i) {
            signature = hashCombine(signature, hashFloat(settings.view_matrix.data()[i]));
            signature = hashCombine(signature, hashFloat(settings.projection_matrix.data()[i]));
            signature = hashCombine(signature, hashFloat(settings.model_matrix.data()[i]));
        }
        signature = hashCombine(signature, hashFloat(settings.camera_position.x()));
        signature = hashCombine(signature, hashFloat(settings.camera_position.y()));
        signature = hashCombine(signature, hashFloat(settings.camera_position.z()));
        signature = hashCombine(signature, static_cast<std::uint64_t>(settings.look_dev.ray_trace.scene_mode));
        signature = hashCombine(signature, static_cast<std::uint64_t>(settings.look_dev.ray_trace.integrator));
        signature = hashCombine(signature, static_cast<std::uint64_t>(settings.look_dev.ray_trace.max_bounces));
        signature = hashCombine(signature, static_cast<std::uint64_t>(settings.look_dev.ray_trace.max_nee_bounces));
        signature = hashCombine(signature, static_cast<std::uint64_t>(settings.look_dev.ray_trace.samples_per_frame));
        signature = hashCombine(signature, hashFloat(settings.look_dev.ray_trace.photon_radius));
        signature = hashCombine(signature, hashFloat(settings.look_dev.ray_trace.photon_intensity));
        signature = hashCombine(signature, settings.look_dev.ray_trace.enable_nee ? 1ULL : 0ULL);
        signature = hashCombine(signature, settings.look_dev.ray_trace.enable_photon_cache ? 1ULL : 0ULL);
        signature = hashCombine(signature, static_cast<std::uint64_t>(framebuffer_width_));
        signature = hashCombine(signature, static_cast<std::uint64_t>(framebuffer_height_));
        if (signature != accumulation_signature_) {
            accumulation_signature_ = signature;
            resetAccumulation();
        }
    } else {
        accumulation_signature_ = 0;
        accumulation_frame_index_ = 0;
    }

    const int history_read_index = accumulation_read_index_;
    const int history_write_index = accumulation_enabled ? (1 - accumulation_read_index_) : accumulation_read_index_;
    const std::uint64_t effective_history_frames = accumulation_enabled ? accumulation_frame_index_ : 0ULL;
    const int shader_frame_index = static_cast<int>(accumulation_frame_index_ % static_cast<std::uint64_t>(std::numeric_limits<int>::max()));
    const int shader_history_frame_count = static_cast<int>(std::min<std::uint64_t>(
        effective_history_frames,
        static_cast<std::uint64_t>(std::numeric_limits<int>::max())));

    if (accumulation_enabled) {
        glBindFramebuffer(GL_FRAMEBUFFER, accumulation_fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumulation_textures_[history_write_index], 0);
        glViewport(0, 0, framebuffer_width_, framebuffer_height_);
        if (effective_history_frames == 0) {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
        glViewport(0, 0, framebuffer_width_, framebuffer_height_);
        glClearColor(clear.redF(), clear.greenF(), clear.blueF(), 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    trace_program_->bind();
    setMatrix4(this, trace_program_, "uInvViewProjection", inv_view_projection);
    setMatrix4(this, trace_program_, "uModel", settings.model_matrix);
    setMatrix4(this, trace_program_, "uInvModel", inv_model);
    setMatrix3(this, trace_program_, "uNormalMatrix", normal_matrix);
    trace_program_->setUniformValue("uCameraPosition", settings.camera_position.x(), settings.camera_position.y(), settings.camera_position.z());
    trace_program_->setUniformValue("uSunDirection", settings.sun_direction.x(), settings.sun_direction.y(), settings.sun_direction.z());
    trace_program_->setUniformValue("uSunColor", settings.sun_color.x(), settings.sun_color.y(), settings.sun_color.z());
    trace_program_->setUniformValue("uClearColor", clear.redF(), clear.greenF(), clear.blueF());
    trace_program_->setUniformValue("uExposure", settings.look_dev.exposure);
    trace_program_->setUniformValue("uAmbientStrength", settings.look_dev.ray_trace.ambient_strength);
    trace_program_->setUniformValue("uShadowStrength", settings.look_dev.ray_trace.shadow_strength);
    trace_program_->setUniformValue("uEnableShadows", settings.look_dev.enable_shadows ? 1 : 0);
    trace_program_->setUniformValue("uViewMode", static_cast<int>(settings.look_dev.ray_trace.view_mode));
    trace_program_->setUniformValue("uSceneMode", static_cast<int>(settings.look_dev.ray_trace.scene_mode));
    trace_program_->setUniformValue("uTraceAlgorithm", static_cast<int>(settings.look_dev.ray_trace.integrator));
    trace_program_->setUniformValue("uEnableNee", settings.look_dev.ray_trace.enable_nee ? 1 : 0);
    trace_program_->setUniformValue("uEnablePhotonCache", settings.look_dev.ray_trace.enable_photon_cache ? 1 : 0);
    trace_program_->setUniformValue("uMaxBounces", settings.look_dev.ray_trace.max_bounces);
    trace_program_->setUniformValue("uMaxNeeBounces", settings.look_dev.ray_trace.max_nee_bounces);
    trace_program_->setUniformValue("uSamplesPerFrame", settings.look_dev.ray_trace.samples_per_frame);
    trace_program_->setUniformValue("uPhotonRadius", settings.look_dev.ray_trace.photon_radius);
    trace_program_->setUniformValue("uPhotonIntensity", settings.look_dev.ray_trace.photon_intensity);
    trace_program_->setUniformValue("uTriangleCount", triangle_count_);
    trace_program_->setUniformValue("uMaterialCount", material_count_);
    trace_program_->setUniformValue("uBvhNodeCount", bvh_node_count_);
    trace_program_->setUniformValue("uLightTriangleCount", light_triangle_count_);
    trace_program_->setUniformValue("uAlbedoAtlas", kAlbedoAtlasTextureUnit);
    trace_program_->setUniformValue("uMetallicAtlas", kMetallicAtlasTextureUnit);
    trace_program_->setUniformValue("uRoughnessAtlas", kRoughnessAtlasTextureUnit);
    trace_program_->setUniformValue("uAoAtlas", kAoAtlasTextureUnit);
    trace_program_->setUniformValue("uEmissiveAtlas", kEmissiveAtlasTextureUnit);
    trace_program_->setUniformValue("uHistoryTexture", kHistoryTextureUnit);
    trace_program_->setUniformValue("uHistoryValid", accumulation_enabled && effective_history_frames > 0 ? 1 : 0);
    trace_program_->setUniformValue("uFrameIndex", shader_frame_index);
    trace_program_->setUniformValue("uHistoryFrameCount", shader_history_frame_count);
    trace_program_->setUniformValue("uOutputLinear", accumulation_enabled ? 1 : 0);
    trace_program_->setUniformValue("uMetallicChannel", settings.look_dev.pbr.metallic_channel);
    trace_program_->setUniformValue("uRoughnessChannel", settings.look_dev.pbr.roughness_channel);
    trace_program_->setUniformValue("uAoChannel", settings.look_dev.pbr.ao_channel);
    trace_program_->setUniformValue("uEmissiveChannel", settings.look_dev.pbr.emissive_channel);

    glActiveTexture(GL_TEXTURE0 + kAlbedoAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, albedo_atlas_texture_);
    glActiveTexture(GL_TEXTURE0 + kMetallicAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, metallic_atlas_texture_);
    glActiveTexture(GL_TEXTURE0 + kRoughnessAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, roughness_atlas_texture_);
    glActiveTexture(GL_TEXTURE0 + kAoAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, ao_atlas_texture_);
    glActiveTexture(GL_TEXTURE0 + kEmissiveAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, emissive_atlas_texture_);
    glActiveTexture(GL_TEXTURE0 + kHistoryTextureUnit);
    glBindTexture(GL_TEXTURE_2D, accumulation_textures_[history_read_index]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kTriangleBinding, triangle_ssbo_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialBinding, material_ssbo_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBvhBinding, bvh_ssbo_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kLightTriangleBinding, light_triangle_ssbo_);

    glBindVertexArray(fullscreen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    trace_program_->release();

    if (accumulation_enabled && display_program_) {
        glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);
        glViewport(0, 0, framebuffer_width_, framebuffer_height_);
        glClearColor(clear.redF(), clear.greenF(), clear.blueF(), 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        display_program_->bind();
        display_program_->setUniformValue("uDisplayTexture", kHistoryTextureUnit);
        display_program_->setUniformValue("uExposure", settings.look_dev.exposure);
        glActiveTexture(GL_TEXTURE0 + kHistoryTextureUnit);
        glBindTexture(GL_TEXTURE_2D, accumulation_textures_[history_write_index]);
        glBindVertexArray(fullscreen_vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        display_program_->release();
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kTriangleBinding, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialBinding, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBvhBinding, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kLightTriangleBinding, 0);
    glActiveTexture(GL_TEXTURE0 + kHistoryTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0 + kEmissiveAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0 + kAoAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0 + kRoughnessAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0 + kMetallicAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0 + kAlbedoAtlasTextureUnit);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (accumulation_enabled) {
        glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer_);

        accumulation_read_index_ = history_write_index;
        if (accumulation_frame_index_ < std::numeric_limits<std::uint64_t>::max() - 1ULL) {
            ++accumulation_frame_index_;
        }
    }

    const auto pass_end = std::chrono::high_resolution_clock::now();
    stats.main_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(pass_end - pass_start).count();
    const auto frame_end = std::chrono::high_resolution_clock::now();
    stats.frame_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(frame_end - frame_start).count();
    const QString integrator_name = [&]() {
        switch (settings.look_dev.ray_trace.integrator) {
        case RayTraceIntegrator::Hybrid:
            return QStringLiteral("Hybrid");
        case RayTraceIntegrator::PathTrace:
            return QStringLiteral("Path");
        case RayTraceIntegrator::PathTraceNee:
            return QStringLiteral("Path+NEE");
        case RayTraceIntegrator::PhotonPath:
        default:
            return QStringLiteral("Photon");
        }
    }();
    stats.note = render_cornell
        ? QStringLiteral("Cornell %1: bounce=%2 nee=%3 spp=%4 accum=%5")
              .arg(integrator_name)
              .arg(settings.look_dev.ray_trace.max_bounces)
              .arg(settings.look_dev.ray_trace.max_nee_bounces)
              .arg(settings.look_dev.ray_trace.samples_per_frame)
              .arg(static_cast<qulonglong>(accumulation_frame_index_))
        : QStringLiteral("%1 RT: %2 tris, %3 BVH, %4 lights, maps A/M/R/AO/E = %5/%6/%7/%8/%9")
              .arg(integrator_name)
              .arg(triangle_count_)
              .arg(bvh_node_count_)
              .arg(light_triangle_count_)
              .arg(albedo_texture_count_)
              .arg(metallic_texture_count_)
              .arg(roughness_texture_count_)
              .arg(ao_texture_count_)
              .arg(emissive_texture_count_);
    return stats;
}

bool OpenGLRayTracer::buildProgram(QString* error_message) {
    destroyProgram();
    trace_program_ = new QOpenGLShaderProgram();
    const std::string fragment_source = traceFragmentShaderSource();
    if (!trace_program_->addShaderFromSourceCode(QOpenGLShader::Vertex, traceVertexShaderSource()) ||
        !trace_program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment_source.c_str()) ||
        !trace_program_->link()) {
        if (error_message) {
            *error_message = trace_program_->log();
        }
        return false;
    }
    if (!buildDisplayProgram(error_message)) {
        return false;
    }
    return true;
}

bool OpenGLRayTracer::buildDisplayProgram(QString* error_message) {
    display_program_ = new QOpenGLShaderProgram();
    if (!display_program_->addShaderFromSourceCode(QOpenGLShader::Vertex, traceVertexShaderSource()) ||
        !display_program_->addShaderFromSourceCode(QOpenGLShader::Fragment, displayFragmentShaderSource()) ||
        !display_program_->link()) {
        if (error_message) {
            *error_message = display_program_->log();
        }
        return false;
    }
    return true;
}

void OpenGLRayTracer::destroyProgram() {
    delete trace_program_;
    trace_program_ = nullptr;
    delete display_program_;
    display_program_ = nullptr;
}

void OpenGLRayTracer::createFullscreenVao() {
    if (fullscreen_vao_ == 0) {
        glGenVertexArrays(1, &fullscreen_vao_);
    }
}

void OpenGLRayTracer::createAccumulationTargets() {
    const int width = std::max(framebuffer_width_, 1);
    const int height = std::max(framebuffer_height_, 1);
    const bool has_textures = accumulation_textures_[0] != 0 && accumulation_textures_[1] != 0;
    const bool size_matches = has_textures && accumulation_texture_width_ == width && accumulation_texture_height_ == height;

    if (accumulation_fbo_ == 0) {
        glGenFramebuffers(1, &accumulation_fbo_);
    }
    if (!has_textures) {
        glGenTextures(2, accumulation_textures_);
    }
    if (size_matches) {
        return;
    }
    for (unsigned int texture : accumulation_textures_) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    accumulation_texture_width_ = width;
    accumulation_texture_height_ = height;
}

void OpenGLRayTracer::destroyFullscreenVao() {
    if (fullscreen_vao_ != 0) {
        glDeleteVertexArrays(1, &fullscreen_vao_);
        fullscreen_vao_ = 0;
    }
}

void OpenGLRayTracer::destroyAccumulationTargets() {
    if (accumulation_fbo_ != 0) {
        glDeleteFramebuffers(1, &accumulation_fbo_);
        accumulation_fbo_ = 0;
    }
    if (accumulation_textures_[0] != 0 || accumulation_textures_[1] != 0) {
        glDeleteTextures(2, accumulation_textures_);
        accumulation_textures_[0] = 0;
        accumulation_textures_[1] = 0;
    }
    accumulation_texture_width_ = 0;
    accumulation_texture_height_ = 0;
    accumulation_read_index_ = 0;
    accumulation_frame_index_ = 0;
    accumulation_signature_ = 0;
}

void OpenGLRayTracer::resetAccumulation() {
    accumulation_read_index_ = 0;
    accumulation_frame_index_ = 0;
    if (accumulation_textures_[0] != 0 && accumulation_textures_[1] != 0) {
        for (unsigned int texture : accumulation_textures_) {
            glBindTexture(GL_TEXTURE_2D, texture);
            std::vector<float> clearData(static_cast<std::size_t>(std::max(framebuffer_width_, 1) * std::max(framebuffer_height_, 1) * 4), 0.0f);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, std::max(framebuffer_width_, 1), std::max(framebuffer_height_, 1), GL_RGBA, GL_FLOAT, clearData.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void OpenGLRayTracer::destroySceneBuffers() {
    for (unsigned int* buffer : { &triangle_ssbo_, &material_ssbo_, &bvh_ssbo_, &light_triangle_ssbo_ }) {
        if (*buffer != 0) {
            glDeleteBuffers(1, buffer);
            *buffer = 0;
        }
    }
    for (unsigned int* texture : { &albedo_atlas_texture_, &metallic_atlas_texture_, &roughness_atlas_texture_, &ao_atlas_texture_, &emissive_atlas_texture_ }) {
        if (*texture != 0) {
            glDeleteTextures(1, texture);
            *texture = 0;
        }
    }
    triangle_count_ = 0;
    material_count_ = 0;
    bvh_node_count_ = 0;
    light_triangle_count_ = 0;
    albedo_texture_count_ = 0;
    metallic_texture_count_ = 0;
    roughness_texture_count_ = 0;
    ao_texture_count_ = 0;
    emissive_texture_count_ = 0;
}

void OpenGLRayTracer::uploadBuffer(unsigned int& buffer, unsigned int binding, const void* data, std::size_t byte_size) {
    if (buffer == 0) {
        glGenBuffers(1, &buffer);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(byte_size), data, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

unsigned int OpenGLRayTracer::uploadTextureAtlas(const QImage& atlas, const QColor& fallback) {
    QImage source = atlas;
    if (source.isNull()) {
        source = QImage(1, 1, QImage::Format_RGBA8888);
        source.fill(fallback);
    } else {
        source = source.convertToFormat(QImage::Format_RGBA8888);
    }

    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 source.width(),
                 source.height(),
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 source.constBits());
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void OpenGLRayTracer::flattenScene(const SceneModel& scene,
                                   int max_texture_size,
                                   std::vector<BuildTriangle>& build_triangles,
                                   std::vector<GpuMaterial>& materials,
                                   MaterialAtlasImages& atlases) const {
    build_triangles.clear();
    materials.clear();
    atlases = MaterialAtlasImages {};
    build_triangles.reserve(scene.triangleCount());
    materials.reserve(scene.meshes.size());
    std::vector<PendingAtlasTexture> pending_albedo_textures;
    std::vector<PendingAtlasTexture> pending_metallic_textures;
    std::vector<PendingAtlasTexture> pending_roughness_textures;
    std::vector<PendingAtlasTexture> pending_ao_textures;
    std::vector<PendingAtlasTexture> pending_emissive_textures;

    for (const MeshData& mesh : scene.meshes) {
        GpuMaterial material {};
        fillVec4(material.base_color, mesh.material.base_color_factor, 1.0f);
        fillVec4(material.emissive_factor, mesh.material.emissive_factor, 0.0f);
        material.pbr_factors[0] = mesh.material.roughness_factor;
        material.pbr_factors[1] = mesh.material.metallic_factor;
        material.pbr_factors[2] = mesh.material.ao_factor;
        material.pbr_factors[3] = 0.0f;
        const int material_index = static_cast<int>(materials.size());
        materials.push_back(material);

        if (const TextureData* albedo_texture = chooseAlbedoTexture(mesh.material)) {
            PendingAtlasTexture pending;
            pending.material_index = material_index;
            pending.source_image = albedo_texture->image.convertToFormat(QImage::Format_RGBA8888);
            if (!pending.source_image.isNull()) {
                materials[material_index].texture_flags0[0] = 1;
                pending_albedo_textures.push_back(std::move(pending));
            }
        }
        if (const TextureData* metallic_texture = chooseMetallicTexture(mesh.material)) {
            PendingAtlasTexture pending;
            pending.material_index = material_index;
            pending.source_image = metallic_texture->image.convertToFormat(QImage::Format_RGBA8888);
            if (!pending.source_image.isNull()) {
                materials[material_index].texture_flags0[1] = 1;
                pending_metallic_textures.push_back(std::move(pending));
            }
        }
        if (const TextureData* roughness_texture = chooseRoughnessTexture(mesh.material)) {
            PendingAtlasTexture pending;
            pending.material_index = material_index;
            pending.source_image = roughness_texture->image.convertToFormat(QImage::Format_RGBA8888);
            if (!pending.source_image.isNull()) {
                materials[material_index].texture_flags0[2] = 1;
                pending_roughness_textures.push_back(std::move(pending));
            }
        }
        if (const TextureData* ao_texture = chooseAoTexture(mesh.material)) {
            PendingAtlasTexture pending;
            pending.material_index = material_index;
            pending.source_image = ao_texture->image.convertToFormat(QImage::Format_RGBA8888);
            if (!pending.source_image.isNull()) {
                materials[material_index].texture_flags0[3] = 1;
                pending_ao_textures.push_back(std::move(pending));
            }
        }
        if (const TextureData* emissive_texture = chooseEmissiveTexture(mesh.material)) {
            PendingAtlasTexture pending;
            pending.material_index = material_index;
            pending.source_image = emissive_texture->image.convertToFormat(QImage::Format_RGBA8888);
            if (!pending.source_image.isNull()) {
                materials[material_index].texture_flags1[0] = 1;
                pending_emissive_textures.push_back(std::move(pending));
            }
        }

        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const std::uint32_t i0 = mesh.indices[i + 0];
            const std::uint32_t i1 = mesh.indices[i + 1];
            const std::uint32_t i2 = mesh.indices[i + 2];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
                continue;
            }

            const Vertex& v0 = mesh.vertices[i0];
            const Vertex& v1 = mesh.vertices[i1];
            const Vertex& v2 = mesh.vertices[i2];
            const Eigen::Vector3f edge1 = v1.position - v0.position;
            const Eigen::Vector3f edge2 = v2.position - v0.position;
            Eigen::Vector3f face_normal = edge1.cross(edge2);
            if (face_normal.squaredNorm() < 1e-10f) {
                continue;
            }
            face_normal.normalize();

            const Eigen::Vector3f n0 = v0.normal.squaredNorm() > 1e-8f ? v0.normal.normalized() : face_normal;
            const Eigen::Vector3f n1 = v1.normal.squaredNorm() > 1e-8f ? v1.normal.normalized() : face_normal;
            const Eigen::Vector3f n2 = v2.normal.squaredNorm() > 1e-8f ? v2.normal.normalized() : face_normal;

            BuildTriangle triangle {};
            fillVec4(triangle.gpu.p0, v0.position, 1.0f);
            fillVec4(triangle.gpu.p1, v1.position, 1.0f);
            fillVec4(triangle.gpu.p2, v2.position, 1.0f);
            fillVec4(triangle.gpu.n0, n0, 0.0f);
            fillVec4(triangle.gpu.n1, n1, 0.0f);
            fillVec4(triangle.gpu.n2, n2, 0.0f);
            fillUv(triangle.gpu.uv0, v0.uv);
            fillUv(triangle.gpu.uv1, v1.uv);
            fillUv(triangle.gpu.uv2, v2.uv);
            triangle.gpu.material_index = material_index;
            triangle.bounds.include(v0.position);
            triangle.bounds.include(v1.position);
            triangle.bounds.include(v2.position);
            triangle.centroid = (v0.position + v1.position + v2.position) / 3.0f;
            build_triangles.push_back(triangle);
        }
    }

    if (buildAtlasImage(pending_albedo_textures, max_texture_size, atlases.albedo)) {
        for (const PendingAtlasTexture& pending : pending_albedo_textures) {
            if (pending.material_index >= 0 && pending.material_index < static_cast<int>(materials.size())) {
                fillRect(materials[pending.material_index].albedo_atlas_rect, pending.rect, atlases.albedo.size());
            }
        }
    } else {
        for (GpuMaterial& gpu_material : materials) {
            gpu_material.texture_flags0[0] = 0;
        }
    }

    if (buildAtlasImage(pending_metallic_textures, max_texture_size, atlases.metallic)) {
        for (const PendingAtlasTexture& pending : pending_metallic_textures) {
            if (pending.material_index >= 0 && pending.material_index < static_cast<int>(materials.size())) {
                fillRect(materials[pending.material_index].metallic_atlas_rect, pending.rect, atlases.metallic.size());
            }
        }
    } else {
        for (GpuMaterial& gpu_material : materials) {
            gpu_material.texture_flags0[1] = 0;
        }
    }

    if (buildAtlasImage(pending_roughness_textures, max_texture_size, atlases.roughness)) {
        for (const PendingAtlasTexture& pending : pending_roughness_textures) {
            if (pending.material_index >= 0 && pending.material_index < static_cast<int>(materials.size())) {
                fillRect(materials[pending.material_index].roughness_atlas_rect, pending.rect, atlases.roughness.size());
            }
        }
    } else {
        for (GpuMaterial& gpu_material : materials) {
            gpu_material.texture_flags0[2] = 0;
        }
    }

    if (buildAtlasImage(pending_ao_textures, max_texture_size, atlases.ao)) {
        for (const PendingAtlasTexture& pending : pending_ao_textures) {
            if (pending.material_index >= 0 && pending.material_index < static_cast<int>(materials.size())) {
                fillRect(materials[pending.material_index].ao_atlas_rect, pending.rect, atlases.ao.size());
            }
        }
    } else {
        for (GpuMaterial& gpu_material : materials) {
            gpu_material.texture_flags0[3] = 0;
        }
    }

    if (buildAtlasImage(pending_emissive_textures, max_texture_size, atlases.emissive)) {
        for (const PendingAtlasTexture& pending : pending_emissive_textures) {
            if (pending.material_index >= 0 && pending.material_index < static_cast<int>(materials.size())) {
                fillRect(materials[pending.material_index].emissive_atlas_rect, pending.rect, atlases.emissive.size());
            }
        }
    } else {
        for (GpuMaterial& gpu_material : materials) {
            gpu_material.texture_flags1[0] = 0;
        }
    }
}

void OpenGLRayTracer::buildBvh(std::vector<BuildTriangle>& build_triangles, std::vector<GpuTriangle>& triangles, std::vector<GpuBvhNode>& nodes) const {
    triangles.clear();
    nodes.clear();
    if (build_triangles.empty()) {
        return;
    }

    nodes.reserve(build_triangles.size() * 2);
    buildBvhNode(build_triangles, 0, static_cast<int>(build_triangles.size()), nodes);

    triangles.reserve(build_triangles.size());
    for (const BuildTriangle& triangle : build_triangles) {
        triangles.push_back(triangle.gpu);
    }
}

int OpenGLRayTracer::buildBvhNode(std::vector<BuildTriangle>& build_triangles, int first, int count, std::vector<GpuBvhNode>& nodes) const {
    const int node_index = static_cast<int>(nodes.size());
    nodes.emplace_back();

    Bounds node_bounds;
    Bounds centroid_bounds;
    for (int i = first; i < first + count; ++i) {
        node_bounds.include(build_triangles[i].bounds.min);
        node_bounds.include(build_triangles[i].bounds.max);
        centroid_bounds.include(build_triangles[i].centroid);
    }

    fillVec4(nodes[node_index].bounds_min, node_bounds.min, 0.0f);
    fillVec4(nodes[node_index].bounds_max, node_bounds.max, 0.0f);

    if (count <= kLeafTriangleCount || !centroid_bounds.valid()) {
        nodes[node_index].first_triangle = first;
        nodes[node_index].triangle_count = count;
        return node_index;
    }

    const Eigen::Vector3f extent = centroid_bounds.extent();
    int axis = 0;
    if (extent.y() > extent.x() && extent.y() >= extent.z()) {
        axis = 1;
    } else if (extent.z() > extent.x() && extent.z() >= extent.y()) {
        axis = 2;
    }

    if (extent[axis] < 1e-7f) {
        nodes[node_index].first_triangle = first;
        nodes[node_index].triangle_count = count;
        return node_index;
    }

    std::sort(build_triangles.begin() + first, build_triangles.begin() + first + count, [axis](const BuildTriangle& lhs, const BuildTriangle& rhs) {
        return lhs.centroid[axis] < rhs.centroid[axis];
    });

    const int left_count = count / 2;
    const int right_count = count - left_count;
    const int left_index = buildBvhNode(build_triangles, first, left_count, nodes);
    const int right_index = buildBvhNode(build_triangles, first + left_count, right_count, nodes);

    nodes[node_index].left_index = left_index;
    nodes[node_index].right_index = right_index;
    nodes[node_index].first_triangle = 0;
    nodes[node_index].triangle_count = 0;
    return node_index;
}

} // namespace haorendergi
