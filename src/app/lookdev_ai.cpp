#include "app/lookdev_ai.h"

#include <QColor>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace haorendergi {
namespace {

template <typename T>
T clampValue(T value, T minimum, T maximum) {
    return std::clamp(value, minimum, maximum);
}

bool containsAny(const QString& text, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (text.contains(QString::fromUtf8(needle))) {
            return true;
        }
    }
    return false;
}

QJsonObject colorToJson(const QColor& color) {
    QJsonObject object;
    object.insert(QStringLiteral("r"), color.redF());
    object.insert(QStringLiteral("g"), color.greenF());
    object.insert(QStringLiteral("b"), color.blueF());
    return object;
}

QColor colorFromJson(const QJsonObject& object, const QColor& fallback) {
    const double r = object.value(QStringLiteral("r")).toDouble(fallback.redF());
    const double g = object.value(QStringLiteral("g")).toDouble(fallback.greenF());
    const double b = object.value(QStringLiteral("b")).toDouble(fallback.blueF());
    return QColor::fromRgbF(clampValue(r, 0.0, 1.0), clampValue(g, 0.0, 1.0), clampValue(b, 0.0, 1.0));
}

QString shadingModelToString(ShadingModel model) {
    return model == ShadingModel::Phong ? QStringLiteral("Phong") : QStringLiteral("PBR");
}

ShadingModel shadingModelFromString(const QString& value) {
    return value.compare(QStringLiteral("Phong"), Qt::CaseInsensitive) == 0 ? ShadingModel::Phong : ShadingModel::Pbr;
}

QString renderPipelineToString(RenderPipeline pipeline) {
    switch (pipeline) {
    case RenderPipeline::RayTrace:
        return QStringLiteral("OpenGLRayTrace");
    case RenderPipeline::DxrRayTrace:
        return QStringLiteral("DXR");
    case RenderPipeline::Raster:
    default:
        return QStringLiteral("Raster");
    }
}

RenderPipeline renderPipelineFromString(const QString& value) {
    if (value.compare(QStringLiteral("OpenGLRayTrace"), Qt::CaseInsensitive) == 0) {
        return RenderPipeline::RayTrace;
    }
    if (value.compare(QStringLiteral("DXR"), Qt::CaseInsensitive) == 0) {
        return RenderPipeline::DxrRayTrace;
    }
    return RenderPipeline::Raster;
}

QString rayTraceSceneModeToString(RayTraceSceneMode mode) {
    return mode == RayTraceSceneMode::LoadedModel ? QStringLiteral("LoadedModel") : QStringLiteral("CornellBox");
}

RayTraceSceneMode rayTraceSceneModeFromString(const QString& value) {
    return value.compare(QStringLiteral("LoadedModel"), Qt::CaseInsensitive) == 0
        ? RayTraceSceneMode::LoadedModel
        : RayTraceSceneMode::CornellBox;
}

QString rayTraceViewModeToString(RayTraceViewMode mode) {
    switch (mode) {
    case RayTraceViewMode::Hit:
        return QStringLiteral("Hit");
    case RayTraceViewMode::Normal:
        return QStringLiteral("Normal");
    case RayTraceViewMode::Albedo:
        return QStringLiteral("Albedo");
    case RayTraceViewMode::Lit:
    default:
        return QStringLiteral("Lit");
    }
}

RayTraceViewMode rayTraceViewModeFromString(const QString& value) {
    if (value.compare(QStringLiteral("Hit"), Qt::CaseInsensitive) == 0) {
        return RayTraceViewMode::Hit;
    }
    if (value.compare(QStringLiteral("Normal"), Qt::CaseInsensitive) == 0) {
        return RayTraceViewMode::Normal;
    }
    if (value.compare(QStringLiteral("Albedo"), Qt::CaseInsensitive) == 0) {
        return RayTraceViewMode::Albedo;
    }
    return RayTraceViewMode::Lit;
}

QString rayTraceIntegratorToString(RayTraceIntegrator integrator) {
    switch (integrator) {
    case RayTraceIntegrator::PathTrace:
        return QStringLiteral("PathTrace");
    case RayTraceIntegrator::PathTraceNee:
        return QStringLiteral("PathTraceNee");
    case RayTraceIntegrator::PhotonPath:
        return QStringLiteral("PhotonPath");
    case RayTraceIntegrator::Hybrid:
    default:
        return QStringLiteral("Hybrid");
    }
}

RayTraceIntegrator rayTraceIntegratorFromString(const QString& value) {
    if (value.compare(QStringLiteral("PathTrace"), Qt::CaseInsensitive) == 0) {
        return RayTraceIntegrator::PathTrace;
    }
    if (value.compare(QStringLiteral("PathTraceNee"), Qt::CaseInsensitive) == 0) {
        return RayTraceIntegrator::PathTraceNee;
    }
    if (value.compare(QStringLiteral("PhotonPath"), Qt::CaseInsensitive) == 0) {
        return RayTraceIntegrator::PhotonPath;
    }
    return RayTraceIntegrator::Hybrid;
}

QString stylePresetFocusToString(StylePresetFocus focus) {
    switch (focus) {
    case StylePresetFocus::Toon:
        return QStringLiteral("Toon");
    case StylePresetFocus::Hybrid:
        return QStringLiteral("Hybrid");
    case StylePresetFocus::PbrAssist:
        return QStringLiteral("PbrAssist");
    case StylePresetFocus::Phong:
    default:
        return QStringLiteral("Phong");
    }
}

StylePresetFocus stylePresetFocusFromString(const QString& value) {
    if (value.compare(QStringLiteral("Toon"), Qt::CaseInsensitive) == 0) {
        return StylePresetFocus::Toon;
    }
    if (value.compare(QStringLiteral("Hybrid"), Qt::CaseInsensitive) == 0) {
        return StylePresetFocus::Hybrid;
    }
    if (value.compare(QStringLiteral("PbrAssist"), Qt::CaseInsensitive) == 0) {
        return StylePresetFocus::PbrAssist;
    }
    return StylePresetFocus::Phong;
}

QJsonObject toonSettingsToJson(const ToonShadingSettings& settings) {
    QJsonObject object;
    object.insert(QStringLiteral("enabled"), settings.enabled);
    object.insert(QStringLiteral("diffuseSteps"), settings.diffuse_steps);
    object.insert(QStringLiteral("diffuseSoftness"), settings.diffuse_softness);
    object.insert(QStringLiteral("shadowFloor"), settings.shadow_floor);
    object.insert(QStringLiteral("litFloor"), settings.lit_floor);
    object.insert(QStringLiteral("rampBias"), settings.ramp_bias);
    object.insert(QStringLiteral("rampContrast"), settings.ramp_contrast);
    object.insert(QStringLiteral("shadowMapStrength"), settings.shadow_map_strength);
    object.insert(QStringLiteral("shadowThreshold"), settings.shadow_threshold);
    object.insert(QStringLiteral("shadowSoftness"), settings.shadow_softness);
    object.insert(QStringLiteral("shadowTint"), colorToJson(settings.shadow_tint));
    object.insert(QStringLiteral("highlightThreshold"), settings.highlight_threshold);
    object.insert(QStringLiteral("highlightSoftness"), settings.highlight_softness);
    object.insert(QStringLiteral("highlightStrength"), settings.highlight_strength);
    object.insert(QStringLiteral("highlightTint"), colorToJson(settings.highlight_tint));
    object.insert(QStringLiteral("rimThreshold"), settings.rim_threshold);
    object.insert(QStringLiteral("rimSoftness"), settings.rim_softness);
    object.insert(QStringLiteral("materialOverrideEnabled"), settings.material_override_enabled);
    object.insert(QStringLiteral("materialTextureStrength"), settings.material_texture_strength);
    object.insert(QStringLiteral("materialLift"), settings.material_lift);
    object.insert(QStringLiteral("materialSaturation"), settings.material_saturation);
    object.insert(QStringLiteral("materialContrast"), settings.material_contrast);
    return object;
}

ToonShadingSettings toonSettingsFromJson(const QJsonObject& object, const ToonShadingSettings& defaults) {
    ToonShadingSettings result = defaults;
    result.enabled = object.value(QStringLiteral("enabled")).toBool(result.enabled);
    result.diffuse_steps = static_cast<float>(object.value(QStringLiteral("diffuseSteps")).toDouble(result.diffuse_steps));
    result.diffuse_softness = static_cast<float>(object.value(QStringLiteral("diffuseSoftness")).toDouble(result.diffuse_softness));
    result.shadow_floor = static_cast<float>(object.value(QStringLiteral("shadowFloor")).toDouble(result.shadow_floor));
    result.lit_floor = static_cast<float>(object.value(QStringLiteral("litFloor")).toDouble(result.lit_floor));
    result.ramp_bias = static_cast<float>(object.value(QStringLiteral("rampBias")).toDouble(result.ramp_bias));
    result.ramp_contrast = static_cast<float>(object.value(QStringLiteral("rampContrast")).toDouble(result.ramp_contrast));
    result.shadow_map_strength = static_cast<float>(object.value(QStringLiteral("shadowMapStrength")).toDouble(result.shadow_map_strength));
    result.shadow_threshold = static_cast<float>(object.value(QStringLiteral("shadowThreshold")).toDouble(result.shadow_threshold));
    result.shadow_softness = static_cast<float>(object.value(QStringLiteral("shadowSoftness")).toDouble(result.shadow_softness));
    if (object.contains(QStringLiteral("shadowTint")) && object.value(QStringLiteral("shadowTint")).isObject()) {
        result.shadow_tint = colorFromJson(object.value(QStringLiteral("shadowTint")).toObject(), result.shadow_tint);
    }
    result.highlight_threshold = static_cast<float>(object.value(QStringLiteral("highlightThreshold")).toDouble(result.highlight_threshold));
    result.highlight_softness = static_cast<float>(object.value(QStringLiteral("highlightSoftness")).toDouble(result.highlight_softness));
    result.highlight_strength = static_cast<float>(object.value(QStringLiteral("highlightStrength")).toDouble(result.highlight_strength));
    if (object.contains(QStringLiteral("highlightTint")) && object.value(QStringLiteral("highlightTint")).isObject()) {
        result.highlight_tint = colorFromJson(object.value(QStringLiteral("highlightTint")).toObject(), result.highlight_tint);
    }
    result.rim_threshold = static_cast<float>(object.value(QStringLiteral("rimThreshold")).toDouble(result.rim_threshold));
    result.rim_softness = static_cast<float>(object.value(QStringLiteral("rimSoftness")).toDouble(result.rim_softness));
    result.material_override_enabled = object.value(QStringLiteral("materialOverrideEnabled")).toBool(result.material_override_enabled);
    result.material_texture_strength = static_cast<float>(object.value(QStringLiteral("materialTextureStrength")).toDouble(result.material_texture_strength));
    result.material_lift = static_cast<float>(object.value(QStringLiteral("materialLift")).toDouble(result.material_lift));
    result.material_saturation = static_cast<float>(object.value(QStringLiteral("materialSaturation")).toDouble(result.material_saturation));
    result.material_contrast = static_cast<float>(object.value(QStringLiteral("materialContrast")).toDouble(result.material_contrast));
    return result;
}

QJsonObject phongSettingsToJson(const PhongShadingSettings& settings) {
    QJsonObject object;
    object.insert(QStringLiteral("hardSpecular"), settings.hard_specular);
    object.insert(QStringLiteral("useTonemap"), settings.use_tonemap);
    object.insert(QStringLiteral("primaryLightOnly"), settings.primary_light_only);
    object.insert(QStringLiteral("secondaryLightScale"), settings.secondary_light_scale);
    object.insert(QStringLiteral("diffuseStrength"), settings.diffuse_strength);
    object.insert(QStringLiteral("ambientStrength"), settings.ambient_strength);
    object.insert(QStringLiteral("ambientColor"), colorToJson(settings.ambient_color));
    object.insert(QStringLiteral("specularStrength"), settings.specular_strength);
    object.insert(QStringLiteral("specularTint"), colorToJson(settings.specular_tint));
    object.insert(QStringLiteral("smoothness"), settings.smoothness);
    object.insert(QStringLiteral("specularMapWeight"), settings.specular_map_weight);
    object.insert(QStringLiteral("shininess"), settings.shininess);
    object.insert(QStringLiteral("rimStrength"), settings.rim_strength);
    object.insert(QStringLiteral("rimPower"), settings.rim_power);
    object.insert(QStringLiteral("rimTint"), colorToJson(settings.rim_tint));
    object.insert(QStringLiteral("toon"), toonSettingsToJson(settings.toon));

    QJsonObject outline;
    outline.insert(QStringLiteral("enabled"), settings.outline.enabled);
    outline.insert(QStringLiteral("widthPixels"), settings.outline.width_pixels);
    outline.insert(QStringLiteral("opacity"), settings.outline.opacity);
    outline.insert(QStringLiteral("depthBias"), settings.outline.depth_bias);
    outline.insert(QStringLiteral("color"), colorToJson(settings.outline.color));
    object.insert(QStringLiteral("outline"), outline);
    return object;
}

PhongShadingSettings phongSettingsFromJson(const QJsonObject& object, const PhongShadingSettings& defaults) {
    PhongShadingSettings result = defaults;
    result.hard_specular = object.value(QStringLiteral("hardSpecular")).toBool(result.hard_specular);
    result.use_tonemap = object.value(QStringLiteral("useTonemap")).toBool(result.use_tonemap);
    result.primary_light_only = object.value(QStringLiteral("primaryLightOnly")).toBool(result.primary_light_only);
    result.secondary_light_scale = static_cast<float>(object.value(QStringLiteral("secondaryLightScale")).toDouble(result.secondary_light_scale));
    result.diffuse_strength = static_cast<float>(object.value(QStringLiteral("diffuseStrength")).toDouble(result.diffuse_strength));
    result.ambient_strength = static_cast<float>(object.value(QStringLiteral("ambientStrength")).toDouble(result.ambient_strength));
    if (object.contains(QStringLiteral("ambientColor")) && object.value(QStringLiteral("ambientColor")).isObject()) {
        result.ambient_color = colorFromJson(object.value(QStringLiteral("ambientColor")).toObject(), result.ambient_color);
    }
    result.specular_strength = static_cast<float>(object.value(QStringLiteral("specularStrength")).toDouble(result.specular_strength));
    if (object.contains(QStringLiteral("specularTint")) && object.value(QStringLiteral("specularTint")).isObject()) {
        result.specular_tint = colorFromJson(object.value(QStringLiteral("specularTint")).toObject(), result.specular_tint);
    }
    result.smoothness = static_cast<float>(object.value(QStringLiteral("smoothness")).toDouble(result.smoothness));
    result.specular_map_weight = static_cast<float>(object.value(QStringLiteral("specularMapWeight")).toDouble(result.specular_map_weight));
    result.shininess = static_cast<float>(object.value(QStringLiteral("shininess")).toDouble(result.shininess));
    result.rim_strength = static_cast<float>(object.value(QStringLiteral("rimStrength")).toDouble(result.rim_strength));
    result.rim_power = static_cast<float>(object.value(QStringLiteral("rimPower")).toDouble(result.rim_power));
    if (object.contains(QStringLiteral("rimTint")) && object.value(QStringLiteral("rimTint")).isObject()) {
        result.rim_tint = colorFromJson(object.value(QStringLiteral("rimTint")).toObject(), result.rim_tint);
    }
    if (object.contains(QStringLiteral("toon")) && object.value(QStringLiteral("toon")).isObject()) {
        result.toon = toonSettingsFromJson(object.value(QStringLiteral("toon")).toObject(), result.toon);
    }
    if (object.contains(QStringLiteral("outline")) && object.value(QStringLiteral("outline")).isObject()) {
        const QJsonObject outline = object.value(QStringLiteral("outline")).toObject();
        result.outline.enabled = outline.value(QStringLiteral("enabled")).toBool(result.outline.enabled);
        result.outline.width_pixels = static_cast<float>(outline.value(QStringLiteral("widthPixels")).toDouble(result.outline.width_pixels));
        result.outline.opacity = static_cast<float>(outline.value(QStringLiteral("opacity")).toDouble(result.outline.opacity));
        result.outline.depth_bias = static_cast<float>(outline.value(QStringLiteral("depthBias")).toDouble(result.outline.depth_bias));
        if (outline.contains(QStringLiteral("color")) && outline.value(QStringLiteral("color")).isObject()) {
            result.outline.color = colorFromJson(outline.value(QStringLiteral("color")).toObject(), result.outline.color);
        }
    }
    return result;
}

QJsonObject pbrSettingsToJson(const PbrShadingSettings& settings) {
    QJsonObject object;
    object.insert(QStringLiteral("iblEnabled"), settings.ibl_enabled);
    object.insert(QStringLiteral("iblDiffuseStrength"), settings.ibl_diffuse_strength);
    object.insert(QStringLiteral("iblSpecularStrength"), settings.ibl_specular_strength);
    object.insert(QStringLiteral("skyLightStrength"), settings.sky_light_strength);
    object.insert(QStringLiteral("metallicChannel"), settings.metallic_channel);
    object.insert(QStringLiteral("roughnessChannel"), settings.roughness_channel);
    object.insert(QStringLiteral("aoChannel"), settings.ao_channel);
    object.insert(QStringLiteral("emissiveChannel"), settings.emissive_channel);
    return object;
}

PbrShadingSettings pbrSettingsFromJson(const QJsonObject& object, const PbrShadingSettings& defaults) {
    PbrShadingSettings result = defaults;
    result.ibl_enabled = object.value(QStringLiteral("iblEnabled")).toBool(result.ibl_enabled);
    result.ibl_diffuse_strength = static_cast<float>(object.value(QStringLiteral("iblDiffuseStrength")).toDouble(result.ibl_diffuse_strength));
    result.ibl_specular_strength = static_cast<float>(object.value(QStringLiteral("iblSpecularStrength")).toDouble(result.ibl_specular_strength));
    result.sky_light_strength = static_cast<float>(object.value(QStringLiteral("skyLightStrength")).toDouble(result.sky_light_strength));
    result.metallic_channel = object.value(QStringLiteral("metallicChannel")).toInt(result.metallic_channel);
    result.roughness_channel = object.value(QStringLiteral("roughnessChannel")).toInt(result.roughness_channel);
    result.ao_channel = object.value(QStringLiteral("aoChannel")).toInt(result.ao_channel);
    result.emissive_channel = object.value(QStringLiteral("emissiveChannel")).toInt(result.emissive_channel);
    return result;
}

QJsonObject rayTraceSettingsToJson(const RayTraceSettings& settings) {
    QJsonObject object;
    object.insert(QStringLiteral("sceneMode"), rayTraceSceneModeToString(settings.scene_mode));
    object.insert(QStringLiteral("viewMode"), rayTraceViewModeToString(settings.view_mode));
    object.insert(QStringLiteral("integrator"), rayTraceIntegratorToString(settings.integrator));
    object.insert(QStringLiteral("ambientStrength"), settings.ambient_strength);
    object.insert(QStringLiteral("shadowStrength"), settings.shadow_strength);
    object.insert(QStringLiteral("maxBounces"), settings.max_bounces);
    object.insert(QStringLiteral("maxNeeBounces"), settings.max_nee_bounces);
    object.insert(QStringLiteral("samplesPerFrame"), settings.samples_per_frame);
    object.insert(QStringLiteral("enableNee"), settings.enable_nee);
    object.insert(QStringLiteral("enablePhotonCache"), settings.enable_photon_cache);
    object.insert(QStringLiteral("photonRadius"), settings.photon_radius);
    object.insert(QStringLiteral("photonIntensity"), settings.photon_intensity);
    return object;
}

RayTraceSettings rayTraceSettingsFromJson(const QJsonObject& object, const RayTraceSettings& defaults) {
    RayTraceSettings result = defaults;
    result.scene_mode = rayTraceSceneModeFromString(object.value(QStringLiteral("sceneMode")).toString(rayTraceSceneModeToString(result.scene_mode)));
    result.view_mode = rayTraceViewModeFromString(object.value(QStringLiteral("viewMode")).toString(rayTraceViewModeToString(result.view_mode)));
    result.integrator = rayTraceIntegratorFromString(object.value(QStringLiteral("integrator")).toString(rayTraceIntegratorToString(result.integrator)));
    result.ambient_strength = static_cast<float>(object.value(QStringLiteral("ambientStrength")).toDouble(result.ambient_strength));
    result.shadow_strength = static_cast<float>(object.value(QStringLiteral("shadowStrength")).toDouble(result.shadow_strength));
    result.max_bounces = object.value(QStringLiteral("maxBounces")).toInt(result.max_bounces);
    result.max_nee_bounces = object.value(QStringLiteral("maxNeeBounces")).toInt(result.max_nee_bounces);
    result.samples_per_frame = object.value(QStringLiteral("samplesPerFrame")).toInt(result.samples_per_frame);
    result.enable_nee = object.value(QStringLiteral("enableNee")).toBool(result.enable_nee);
    result.enable_photon_cache = object.value(QStringLiteral("enablePhotonCache")).toBool(result.enable_photon_cache);
    result.photon_radius = static_cast<float>(object.value(QStringLiteral("photonRadius")).toDouble(result.photon_radius));
    result.photon_intensity = static_cast<float>(object.value(QStringLiteral("photonIntensity")).toDouble(result.photon_intensity));
    return result;
}

QJsonObject lookDevSettingsToJson(const LookDevSettings& settings) {
    QJsonObject object;
    object.insert(QStringLiteral("shadingModel"), shadingModelToString(settings.shading_model));
    object.insert(QStringLiteral("exposure"), settings.exposure);
    object.insert(QStringLiteral("normalStrength"), settings.normal_strength);
    object.insert(QStringLiteral("enableShadows"), settings.enable_shadows);
    object.insert(QStringLiteral("enableBackfaceCulling"), settings.enable_backface_culling);
    object.insert(QStringLiteral("pbr"), pbrSettingsToJson(settings.pbr));
    object.insert(QStringLiteral("phong"), phongSettingsToJson(settings.phong));
    object.insert(QStringLiteral("rayTrace"), rayTraceSettingsToJson(settings.ray_trace));
    return object;
}

LookDevSettings lookDevSettingsFromJson(const QJsonObject& object, const LookDevSettings& defaults) {
    LookDevSettings result = defaults;
    result.shading_model = shadingModelFromString(object.value(QStringLiteral("shadingModel")).toString(shadingModelToString(result.shading_model)));
    result.exposure = static_cast<float>(object.value(QStringLiteral("exposure")).toDouble(result.exposure));
    result.normal_strength = static_cast<float>(object.value(QStringLiteral("normalStrength")).toDouble(result.normal_strength));
    result.enable_shadows = object.value(QStringLiteral("enableShadows")).toBool(result.enable_shadows);
    result.enable_backface_culling = object.value(QStringLiteral("enableBackfaceCulling")).toBool(result.enable_backface_culling);
    if (object.contains(QStringLiteral("pbr")) && object.value(QStringLiteral("pbr")).isObject()) {
        result.pbr = pbrSettingsFromJson(object.value(QStringLiteral("pbr")).toObject(), result.pbr);
    }
    if (object.contains(QStringLiteral("phong")) && object.value(QStringLiteral("phong")).isObject()) {
        result.phong = phongSettingsFromJson(object.value(QStringLiteral("phong")).toObject(), result.phong);
    }
    if (object.contains(QStringLiteral("rayTrace")) && object.value(QStringLiteral("rayTrace")).isObject()) {
        result.ray_trace = rayTraceSettingsFromJson(object.value(QStringLiteral("rayTrace")).toObject(), result.ray_trace);
    }
    return result;
}

void clampPhongSettings(PhongShadingSettings* phong) {
    phong->secondary_light_scale = clampValue(phong->secondary_light_scale, 0.0f, 1.5f);
    phong->diffuse_strength = clampValue(phong->diffuse_strength, 0.0f, 2.0f);
    phong->ambient_strength = clampValue(phong->ambient_strength, 0.0f, 1.0f);
    phong->specular_strength = clampValue(phong->specular_strength, 0.0f, 2.0f);
    phong->smoothness = clampValue(phong->smoothness, 0.0f, 1.0f);
    phong->specular_map_weight = clampValue(phong->specular_map_weight, 0.0f, 2.0f);
    phong->shininess = clampValue(phong->shininess, 4.0f, 128.0f);
    phong->rim_strength = clampValue(phong->rim_strength, 0.0f, 2.0f);
    phong->rim_power = clampValue(phong->rim_power, 0.25f, 8.0f);
    phong->toon.diffuse_steps = clampValue(phong->toon.diffuse_steps, 2.0f, 6.0f);
    phong->toon.diffuse_softness = clampValue(phong->toon.diffuse_softness, 0.0f, 0.45f);
    phong->toon.shadow_floor = clampValue(phong->toon.shadow_floor, 0.0f, 0.8f);
    phong->toon.lit_floor = clampValue(phong->toon.lit_floor, 0.0f, 1.0f);
    phong->toon.ramp_bias = clampValue(phong->toon.ramp_bias, -0.5f, 0.5f);
    phong->toon.ramp_contrast = clampValue(phong->toon.ramp_contrast, 0.25f, 2.5f);
    phong->toon.shadow_map_strength = clampValue(phong->toon.shadow_map_strength, 0.0f, 1.0f);
    phong->toon.shadow_threshold = clampValue(phong->toon.shadow_threshold, 0.0f, 1.0f);
    phong->toon.shadow_softness = clampValue(phong->toon.shadow_softness, 0.0f, 0.45f);
    phong->toon.highlight_threshold = clampValue(phong->toon.highlight_threshold, 0.0f, 1.0f);
    phong->toon.highlight_softness = clampValue(phong->toon.highlight_softness, 0.0f, 0.45f);
    phong->toon.highlight_strength = clampValue(phong->toon.highlight_strength, 0.0f, 2.0f);
    phong->toon.rim_threshold = clampValue(phong->toon.rim_threshold, 0.0f, 1.0f);
    phong->toon.rim_softness = clampValue(phong->toon.rim_softness, 0.0f, 0.45f);
    phong->toon.material_texture_strength = clampValue(phong->toon.material_texture_strength, 0.0f, 1.0f);
    phong->toon.material_lift = clampValue(phong->toon.material_lift, 0.0f, 0.8f);
    phong->toon.material_saturation = clampValue(phong->toon.material_saturation, 0.0f, 2.0f);
    phong->toon.material_contrast = clampValue(phong->toon.material_contrast, 0.25f, 2.5f);
    phong->outline.width_pixels = clampValue(phong->outline.width_pixels, 0.0f, 12.0f);
    phong->outline.opacity = clampValue(phong->outline.opacity, 0.0f, 1.0f);
    phong->outline.depth_bias = clampValue(phong->outline.depth_bias, 0.0f, 0.02f);
}

void clampPbrSettings(PbrShadingSettings* pbr) {
    pbr->ibl_diffuse_strength = clampValue(pbr->ibl_diffuse_strength, 0.0f, 2.0f);
    pbr->ibl_specular_strength = clampValue(pbr->ibl_specular_strength, 0.0f, 2.0f);
    pbr->sky_light_strength = clampValue(pbr->sky_light_strength, 0.0f, 2.0f);
    pbr->metallic_channel = clampValue(pbr->metallic_channel, 0, 3);
    pbr->roughness_channel = clampValue(pbr->roughness_channel, 0, 3);
    pbr->ao_channel = clampValue(pbr->ao_channel, 0, 3);
    pbr->emissive_channel = clampValue(pbr->emissive_channel, 0, 3);
}

StylePreset makeBasePreset(const QString& name,
                           const QString& description,
                           StylePresetFocus focus,
                           const QString& source_prompt,
                           const LookDevSettings& base_settings) {
    StylePreset preset;
    preset.name = name;
    preset.description = description;
    preset.source_prompt = source_prompt;
    preset.focus = focus;
    preset.preferred_pipeline = RenderPipeline::Raster;
    preset.settings = base_settings;
    preset.settings.shading_model = ShadingModel::Phong;
    preset.settings.enable_shadows = true;
    preset.settings.enable_backface_culling = true;
    preset.settings.exposure = clampValue(preset.settings.exposure, 0.85f, 1.4f);
    preset.settings.normal_strength = clampValue(preset.settings.normal_strength, 0.8f, 1.4f);
    preset.settings.phong.use_tonemap = true;
    preset.settings.phong.primary_light_only = false;
    return preset;
}

StylePreset makePbrPreset(const QString& name,
                          const QString& description,
                          const QString& source_prompt,
                          const LookDevSettings& base_settings) {
    StylePreset preset;
    preset.name = name;
    preset.description = description;
    preset.source_prompt = source_prompt;
    preset.focus = StylePresetFocus::PbrAssist;
    preset.preferred_pipeline = RenderPipeline::Raster;
    preset.settings = base_settings;
    preset.settings.shading_model = ShadingModel::Pbr;
    preset.settings.enable_shadows = true;
    preset.settings.enable_backface_culling = true;
    preset.settings.exposure = clampValue(preset.settings.exposure, 0.85f, 1.35f);
    preset.settings.normal_strength = clampValue(preset.settings.normal_strength, 0.75f, 1.25f);
    preset.settings.pbr.ibl_enabled = true;
    preset.settings.phong.toon.enabled = false;
    preset.settings.phong.outline.enabled = false;
    return preset;
}

void configurePbrNeutral(StylePreset* preset) {
    preset->name = QStringLiteral("PBR Neutral LookDev");
    preset->description = QStringLiteral("Keeps the renderer in PBR and tunes only effective exposure, IBL, sky fill, normal, and shadow controls.");
    preset->settings.exposure = 1.08f;
    preset->settings.normal_strength = 1.0f;
    preset->settings.pbr.ibl_enabled = true;
    preset->settings.pbr.ibl_diffuse_strength = 0.68f;
    preset->settings.pbr.ibl_specular_strength = 0.86f;
    preset->settings.pbr.sky_light_strength = 0.24f;
    clampPbrSettings(&preset->settings.pbr);
}

void configurePbrSoftStudio(StylePreset* preset) {
    preset->name = QStringLiteral("PBR Soft Studio");
    preset->description = QStringLiteral("Softens the PBR preview with stronger diffuse IBL and sky fill while avoiding toon/Phong controls.");
    preset->settings.exposure = 1.16f;
    preset->settings.normal_strength = 0.92f;
    preset->settings.pbr.ibl_enabled = true;
    preset->settings.pbr.ibl_diffuse_strength = 0.95f;
    preset->settings.pbr.ibl_specular_strength = 0.72f;
    preset->settings.pbr.sky_light_strength = 0.42f;
    clampPbrSettings(&preset->settings.pbr);
}

void configurePbrContrastProduct(StylePreset* preset) {
    preset->name = QStringLiteral("PBR Contrast Product");
    preset->description = QStringLiteral("Uses lower fill and stronger specular IBL for clearer PBR product separation without leaving the PBR model.");
    preset->settings.exposure = 1.0f;
    preset->settings.normal_strength = 1.05f;
    preset->settings.pbr.ibl_enabled = true;
    preset->settings.pbr.ibl_diffuse_strength = 0.38f;
    preset->settings.pbr.ibl_specular_strength = 1.18f;
    preset->settings.pbr.sky_light_strength = 0.14f;
    clampPbrSettings(&preset->settings.pbr);
}

void configureCleanPhong(StylePreset* preset, float intensity) {
    PhongShadingSettings& phong = preset->settings.phong;
    phong.toon.enabled = false;
    phong.hard_specular = false;
    phong.secondary_light_scale = 0.16f + 0.08f * intensity;
    phong.diffuse_strength = 0.95f + 0.10f * intensity;
    phong.ambient_strength = 0.035f + 0.020f * intensity;
    phong.specular_strength = 0.20f + 0.16f * intensity;
    phong.smoothness = 0.72f + 0.18f * intensity;
    phong.specular_map_weight = 1.0f;
    phong.shininess = 30.0f + 28.0f * intensity;
    phong.rim_strength = 0.10f + 0.20f * intensity;
    phong.rim_power = 2.8f + 1.2f * intensity;
    phong.outline.enabled = intensity > 0.45f;
    phong.outline.width_pixels = 1.4f + 1.0f * intensity;
    phong.outline.opacity = 0.75f + 0.10f * intensity;
    clampPhongSettings(&phong);
}

void configureAnimeToon(StylePreset* preset, float intensity) {
    PhongShadingSettings& phong = preset->settings.phong;
    phong.toon.enabled = true;
    phong.hard_specular = intensity > 0.70f;
    phong.primary_light_only = false;
    phong.secondary_light_scale = 0.18f + 0.14f * (1.0f - intensity);
    phong.diffuse_strength = 1.05f;
    phong.ambient_strength = 0.05f;
    phong.specular_strength = 0.30f + 0.55f * intensity;
    phong.smoothness = 0.72f + 0.20f * intensity;
    phong.shininess = 72.0f + 28.0f * intensity;
    phong.rim_strength = 0.22f + 0.34f * intensity;
    phong.rim_power = 2.1f;
    phong.toon.diffuse_steps = intensity > 0.65f ? 4.0f : 3.0f;
    phong.toon.diffuse_softness = 0.04f + 0.07f * (1.0f - intensity);
    phong.toon.shadow_threshold = 0.40f + 0.06f * (1.0f - intensity);
    phong.toon.shadow_softness = 0.05f + 0.08f * (1.0f - intensity);
    phong.toon.shadow_tint = QColor(166, 182, 235);
    phong.toon.highlight_threshold = 0.24f + 0.12f * (1.0f - intensity);
    phong.toon.highlight_softness = 0.05f + 0.04f * (1.0f - intensity);
    phong.toon.highlight_strength = 0.80f + 0.20f * intensity;
    phong.toon.highlight_tint = QColor(255, 248, 240);
    phong.toon.rim_threshold = 0.24f + 0.08f * (1.0f - intensity);
    phong.toon.rim_softness = 0.06f + 0.04f * (1.0f - intensity);
    phong.outline.enabled = true;
    phong.outline.width_pixels = 1.2f + 2.8f * intensity;
    phong.outline.opacity = 0.72f + 0.25f * intensity;
    phong.outline.depth_bias = 0.0014f + 0.0012f * intensity;
    clampPhongSettings(&phong);
}

void configureHybridToon(StylePreset* preset, float intensity) {
    PhongShadingSettings& phong = preset->settings.phong;
    phong.toon.enabled = true;
    phong.hard_specular = false;
    phong.secondary_light_scale = 0.18f;
    phong.diffuse_strength = 1.0f;
    phong.ambient_strength = 0.04f + 0.02f * intensity;
    phong.specular_strength = 0.30f + 0.18f * intensity;
    phong.smoothness = 0.76f + 0.14f * intensity;
    phong.shininess = 42.0f + 20.0f * intensity;
    phong.rim_strength = 0.18f + 0.16f * intensity;
    phong.rim_power = 2.4f;
    phong.toon.diffuse_steps = 4.0f;
    phong.toon.diffuse_softness = 0.07f;
    phong.toon.shadow_threshold = 0.48f;
    phong.toon.shadow_softness = 0.08f;
    phong.toon.shadow_tint = QColor(195, 205, 230);
    phong.toon.highlight_threshold = 0.40f;
    phong.toon.highlight_softness = 0.07f;
    phong.toon.highlight_strength = 0.55f + 0.15f * intensity;
    phong.toon.highlight_tint = QColor(255, 252, 244);
    phong.toon.rim_threshold = 0.32f;
    phong.toon.rim_softness = 0.10f;
    phong.outline.enabled = true;
    phong.outline.width_pixels = 1.6f + 0.8f * intensity;
    phong.outline.opacity = 0.82f + 0.10f * intensity;
    clampPhongSettings(&phong);
}

void configureSoftGameAnime(StylePreset* preset) {
    preset->name = QStringLiteral("Soft Game Anime");
    preset->description = QStringLiteral("MToon/UTS-inspired soft game-anime look: colored shadows, controlled cream highlights, gentle rim, and modest outline.");
    preset->focus = StylePresetFocus::Toon;
    preset->settings.exposure = 1.10f;
    preset->settings.normal_strength = 0.92f;

    PhongShadingSettings& phong = preset->settings.phong;
    phong.toon.enabled = true;
    phong.hard_specular = false;
    phong.use_tonemap = true;
    phong.primary_light_only = false;
    phong.secondary_light_scale = 0.28f;
    phong.diffuse_strength = 1.06f;
    phong.ambient_strength = 0.08f;
    phong.specular_strength = 0.34f;
    phong.specular_tint = QColor(255, 248, 238);
    phong.smoothness = 0.78f;
    phong.specular_map_weight = 0.72f;
    phong.shininess = 58.0f;
    phong.rim_strength = 0.32f;
    phong.rim_power = 2.2f;
    phong.rim_tint = QColor(220, 234, 255);
    phong.toon.diffuse_steps = 3.0f;
    phong.toon.diffuse_softness = 0.10f;
    phong.toon.shadow_floor = 0.12f;
    phong.toon.lit_floor = 0.46f;
    phong.toon.ramp_bias = 0.02f;
    phong.toon.ramp_contrast = 0.90f;
    phong.toon.shadow_map_strength = 0.35f;
    phong.toon.shadow_threshold = 0.44f;
    phong.toon.shadow_softness = 0.13f;
    phong.toon.shadow_tint = QColor(164, 176, 230);
    phong.toon.highlight_threshold = 0.36f;
    phong.toon.highlight_softness = 0.09f;
    phong.toon.highlight_strength = 0.62f;
    phong.toon.highlight_tint = QColor(255, 248, 238);
    phong.toon.rim_threshold = 0.31f;
    phong.toon.rim_softness = 0.10f;
    phong.toon.material_override_enabled = true;
    phong.toon.material_texture_strength = 0.85f;
    phong.toon.material_lift = 0.05f;
    phong.toon.material_saturation = 1.05f;
    phong.toon.material_contrast = 0.92f;
    phong.outline.enabled = true;
    phong.outline.width_pixels = 1.45f;
    phong.outline.opacity = 0.62f;
    phong.outline.depth_bias = 0.0015f;
    phong.outline.color = QColor(18, 21, 32);
    clampPhongSettings(&phong);
}

void configureHardCel(StylePreset* preset) {
    preset->name = QStringLiteral("Hard 2D Cel");
    preset->description = QStringLiteral("ArcSys-style shader-side approximation: hard cel bands, low fill, sharp highlights, and a stronger silhouette line.");
    preset->focus = StylePresetFocus::Toon;
    preset->settings.exposure = 1.04f;
    preset->settings.normal_strength = 0.96f;

    PhongShadingSettings& phong = preset->settings.phong;
    phong.toon.enabled = true;
    phong.hard_specular = true;
    phong.use_tonemap = true;
    phong.primary_light_only = true;
    phong.secondary_light_scale = 0.08f;
    phong.diffuse_strength = 1.08f;
    phong.ambient_strength = 0.04f;
    phong.specular_strength = 0.50f;
    phong.specular_tint = QColor(255, 248, 238);
    phong.smoothness = 0.88f;
    phong.specular_map_weight = 0.85f;
    phong.shininess = 94.0f;
    phong.rim_strength = 0.28f;
    phong.rim_power = 3.8f;
    phong.rim_tint = QColor(225, 235, 255);
    phong.toon.diffuse_steps = 2.0f;
    phong.toon.diffuse_softness = 0.02f;
    phong.toon.shadow_floor = 0.05f;
    phong.toon.lit_floor = 0.38f;
    phong.toon.ramp_bias = 0.05f;
    phong.toon.ramp_contrast = 1.45f;
    phong.toon.shadow_map_strength = 0.75f;
    phong.toon.shadow_threshold = 0.52f;
    phong.toon.shadow_softness = 0.02f;
    phong.toon.shadow_tint = QColor(112, 124, 178);
    phong.toon.highlight_threshold = 0.28f;
    phong.toon.highlight_softness = 0.02f;
    phong.toon.highlight_strength = 0.66f;
    phong.toon.highlight_tint = QColor(255, 249, 235);
    phong.toon.rim_threshold = 0.28f;
    phong.toon.rim_softness = 0.04f;
    phong.toon.material_override_enabled = true;
    phong.toon.material_texture_strength = 0.95f;
    phong.toon.material_lift = 0.02f;
    phong.toon.material_saturation = 1.12f;
    phong.toon.material_contrast = 1.18f;
    phong.outline.enabled = true;
    phong.outline.width_pixels = 2.9f;
    phong.outline.opacity = 0.88f;
    phong.outline.depth_bias = 0.0022f;
    phong.outline.color = QColor(8, 10, 15);
    clampPhongSettings(&phong);
}

void configureFigureShowcase(StylePreset* preset) {
    preset->name = QStringLiteral("Figure Showcase");
    preset->description = QStringLiteral("Character product-shot look: clean toon form, glossy but controlled highlights, subtle outline, and stronger rim separation.");
    preset->focus = StylePresetFocus::Hybrid;
    preset->settings.exposure = 1.18f;
    preset->settings.normal_strength = 0.94f;

    PhongShadingSettings& phong = preset->settings.phong;
    phong.toon.enabled = true;
    phong.hard_specular = false;
    phong.use_tonemap = true;
    phong.primary_light_only = false;
    phong.secondary_light_scale = 0.34f;
    phong.diffuse_strength = 1.08f;
    phong.ambient_strength = 0.07f;
    phong.specular_strength = 0.62f;
    phong.specular_tint = QColor(255, 247, 232);
    phong.smoothness = 0.88f;
    phong.specular_map_weight = 0.92f;
    phong.shininess = 102.0f;
    phong.rim_strength = 0.50f;
    phong.rim_power = 2.0f;
    phong.rim_tint = QColor(232, 240, 255);
    phong.toon.diffuse_steps = 3.0f;
    phong.toon.diffuse_softness = 0.07f;
    phong.toon.shadow_floor = 0.09f;
    phong.toon.lit_floor = 0.45f;
    phong.toon.ramp_bias = 0.03f;
    phong.toon.ramp_contrast = 1.05f;
    phong.toon.shadow_map_strength = 0.45f;
    phong.toon.shadow_threshold = 0.44f;
    phong.toon.shadow_softness = 0.09f;
    phong.toon.shadow_tint = QColor(178, 188, 226);
    phong.toon.highlight_threshold = 0.30f;
    phong.toon.highlight_softness = 0.06f;
    phong.toon.highlight_strength = 0.84f;
    phong.toon.highlight_tint = QColor(255, 247, 232);
    phong.toon.rim_threshold = 0.28f;
    phong.toon.rim_softness = 0.08f;
    phong.toon.material_override_enabled = true;
    phong.toon.material_texture_strength = 0.90f;
    phong.toon.material_lift = 0.035f;
    phong.toon.material_saturation = 1.04f;
    phong.toon.material_contrast = 1.02f;
    phong.outline.enabled = true;
    phong.outline.width_pixels = 1.15f;
    phong.outline.opacity = 0.54f;
    phong.outline.depth_bias = 0.0014f;
    phong.outline.color = QColor(20, 22, 30);
    clampPhongSettings(&phong);
}

void configurePainterly(StylePreset* preset) {
    preset->name = QStringLiteral("Painterly Soft Bands");
    preset->description = QStringLiteral("Hand-painted approximation with soft multi-step bands, low specular, colored shadows, and restrained linework.");
    preset->focus = StylePresetFocus::Toon;
    preset->settings.exposure = 1.12f;
    preset->settings.normal_strength = 0.76f;

    PhongShadingSettings& phong = preset->settings.phong;
    phong.toon.enabled = true;
    phong.hard_specular = false;
    phong.use_tonemap = true;
    phong.primary_light_only = false;
    phong.secondary_light_scale = 0.38f;
    phong.diffuse_strength = 1.02f;
    phong.ambient_strength = 0.15f;
    phong.specular_strength = 0.22f;
    phong.specular_tint = QColor(248, 238, 220);
    phong.smoothness = 0.48f;
    phong.specular_map_weight = 0.55f;
    phong.shininess = 34.0f;
    phong.rim_strength = 0.20f;
    phong.rim_power = 2.4f;
    phong.rim_tint = QColor(228, 234, 248);
    phong.toon.diffuse_steps = 5.0f;
    phong.toon.diffuse_softness = 0.17f;
    phong.toon.shadow_floor = 0.14f;
    phong.toon.lit_floor = 0.55f;
    phong.toon.ramp_bias = -0.02f;
    phong.toon.ramp_contrast = 0.75f;
    phong.toon.shadow_map_strength = 0.25f;
    phong.toon.shadow_threshold = 0.46f;
    phong.toon.shadow_softness = 0.20f;
    phong.toon.shadow_tint = QColor(180, 168, 206);
    phong.toon.highlight_threshold = 0.42f;
    phong.toon.highlight_softness = 0.14f;
    phong.toon.highlight_strength = 0.34f;
    phong.toon.highlight_tint = QColor(252, 238, 216);
    phong.toon.rim_threshold = 0.34f;
    phong.toon.rim_softness = 0.14f;
    phong.toon.material_override_enabled = true;
    phong.toon.material_texture_strength = 0.70f;
    phong.toon.material_lift = 0.08f;
    phong.toon.material_saturation = 0.90f;
    phong.toon.material_contrast = 0.75f;
    phong.outline.enabled = true;
    phong.outline.width_pixels = 0.85f;
    phong.outline.opacity = 0.36f;
    phong.outline.depth_bias = 0.0012f;
    phong.outline.color = QColor(34, 30, 42);
    clampPhongSettings(&phong);
}

void configureGoochIllustration(StylePreset* preset) {
    preset->name = QStringLiteral("Gooch Technical Illustration");
    preset->description = QStringLiteral("Cool-to-warm illustrative shading for clear structure, preserving edge lines without black shadow crush.");
    preset->focus = StylePresetFocus::Toon;
    preset->settings.exposure = 1.05f;
    preset->settings.normal_strength = 0.90f;

    PhongShadingSettings& phong = preset->settings.phong;
    phong.toon.enabled = true;
    phong.hard_specular = false;
    phong.use_tonemap = true;
    phong.primary_light_only = false;
    phong.secondary_light_scale = 0.26f;
    phong.diffuse_strength = 1.02f;
    phong.ambient_strength = 0.18f;
    phong.specular_strength = 0.28f;
    phong.specular_tint = QColor(255, 226, 166);
    phong.smoothness = 0.58f;
    phong.specular_map_weight = 0.60f;
    phong.shininess = 42.0f;
    phong.rim_strength = 0.28f;
    phong.rim_power = 2.8f;
    phong.rim_tint = QColor(232, 240, 255);
    phong.toon.diffuse_steps = 5.0f;
    phong.toon.diffuse_softness = 0.12f;
    phong.toon.shadow_floor = 0.12f;
    phong.toon.lit_floor = 0.50f;
    phong.toon.ramp_bias = 0.00f;
    phong.toon.ramp_contrast = 0.95f;
    phong.toon.shadow_map_strength = 0.35f;
    phong.toon.shadow_threshold = 0.48f;
    phong.toon.shadow_softness = 0.14f;
    phong.toon.shadow_tint = QColor(112, 148, 242);
    phong.toon.highlight_threshold = 0.36f;
    phong.toon.highlight_softness = 0.10f;
    phong.toon.highlight_strength = 0.38f;
    phong.toon.highlight_tint = QColor(255, 226, 166);
    phong.toon.rim_threshold = 0.30f;
    phong.toon.rim_softness = 0.12f;
    phong.toon.material_override_enabled = true;
    phong.toon.material_texture_strength = 0.82f;
    phong.toon.material_lift = 0.05f;
    phong.toon.material_saturation = 0.95f;
    phong.toon.material_contrast = 0.88f;
    phong.outline.enabled = true;
    phong.outline.width_pixels = 1.7f;
    phong.outline.opacity = 0.72f;
    phong.outline.depth_bias = 0.0015f;
    phong.outline.color = QColor(22, 25, 36);
    clampPhongSettings(&phong);
}

void configureReadabilityFirst(StylePreset* preset) {
    preset->name = QStringLiteral("Readability Rim Style");
    preset->description = QStringLiteral("TF2-inspired readability pass using rim and value separation instead of heavy black linework.");
    preset->focus = StylePresetFocus::Phong;
    preset->settings.exposure = 1.06f;
    preset->settings.normal_strength = 1.0f;

    PhongShadingSettings& phong = preset->settings.phong;
    phong.toon.enabled = false;
    phong.hard_specular = false;
    phong.use_tonemap = true;
    phong.primary_light_only = false;
    phong.secondary_light_scale = 0.34f;
    phong.diffuse_strength = 1.12f;
    phong.ambient_strength = 0.16f;
    phong.specular_strength = 0.46f;
    phong.specular_tint = QColor(244, 238, 226);
    phong.smoothness = 0.70f;
    phong.specular_map_weight = 0.82f;
    phong.shininess = 58.0f;
    phong.rim_strength = 0.64f;
    phong.rim_power = 2.2f;
    phong.rim_tint = QColor(222, 236, 255);
    phong.outline.enabled = true;
    phong.outline.width_pixels = 0.6f;
    phong.outline.opacity = 0.30f;
    phong.outline.depth_bias = 0.0010f;
    phong.outline.color = QColor(16, 18, 24);
    clampPhongSettings(&phong);
}

void configureClayReview(StylePreset* preset) {
    preset->name = QStringLiteral("Clay Matte Review");
    preset->description = QStringLiteral("Neutral low-gloss form review with high fill, low specular, and no stylized line dominance.");
    preset->focus = StylePresetFocus::Phong;
    preset->settings.exposure = 1.08f;
    preset->settings.normal_strength = 0.82f;

    PhongShadingSettings& phong = preset->settings.phong;
    phong.toon.enabled = false;
    phong.hard_specular = false;
    phong.use_tonemap = true;
    phong.primary_light_only = false;
    phong.secondary_light_scale = 0.36f;
    phong.diffuse_strength = 1.0f;
    phong.ambient_strength = 0.20f;
    phong.specular_strength = 0.10f;
    phong.smoothness = 0.32f;
    phong.specular_map_weight = 0.25f;
    phong.shininess = 22.0f;
    phong.rim_strength = 0.14f;
    phong.rim_power = 2.8f;
    phong.outline.enabled = false;
    clampPhongSettings(&phong);
}

void applySoftBias(StylePreset* preset) {
    PhongShadingSettings& phong = preset->settings.phong;
    phong.smoothness = clampValue(phong.smoothness - 0.10f, 0.0f, 1.0f);
    phong.shininess = clampValue(phong.shininess - 10.0f, 4.0f, 128.0f);
    phong.toon.shadow_softness = clampValue(phong.toon.shadow_softness + 0.03f, 0.0f, 0.45f);
    phong.toon.highlight_softness = clampValue(phong.toon.highlight_softness + 0.03f, 0.0f, 0.45f);
}

void applySharpBias(StylePreset* preset) {
    PhongShadingSettings& phong = preset->settings.phong;
    phong.smoothness = clampValue(phong.smoothness + 0.08f, 0.0f, 1.0f);
    phong.shininess = clampValue(phong.shininess + 12.0f, 4.0f, 128.0f);
    phong.toon.shadow_softness = clampValue(phong.toon.shadow_softness - 0.02f, 0.0f, 0.45f);
    phong.toon.highlight_softness = clampValue(phong.toon.highlight_softness - 0.02f, 0.0f, 0.45f);
}

void applyOutlineBias(StylePreset* preset, float width_delta) {
    PhongShadingSettings& phong = preset->settings.phong;
    phong.outline.enabled = true;
    phong.outline.width_pixels = clampValue(phong.outline.width_pixels + width_delta, 0.0f, 12.0f);
    phong.outline.opacity = clampValue(phong.outline.opacity + 0.06f, 0.0f, 1.0f);
}

void applyRimBias(StylePreset* preset, float strength_delta) {
    PhongShadingSettings& phong = preset->settings.phong;
    phong.rim_strength = clampValue(phong.rim_strength + strength_delta, 0.0f, 2.0f);
}

void applyCoolShadowBias(StylePreset* preset) {
    preset->settings.phong.toon.shadow_tint = QColor(150, 176, 255);
    preset->settings.phong.rim_tint = QColor(222, 236, 255);
}

void applyWarmShadowBias(StylePreset* preset) {
    preset->settings.phong.toon.shadow_tint = QColor(212, 184, 156);
    preset->settings.phong.rim_tint = QColor(255, 236, 208);
}

QStringList buildSummaryBits(const StylePreset& preset) {
    if (preset.settings.shading_model == ShadingModel::Pbr) {
        const PbrShadingSettings& pbr = preset.settings.pbr;
        QStringList bits;
        bits << QStringLiteral("PBR Assist");
        bits << QStringLiteral("exposure %1").arg(QString::number(preset.settings.exposure, 'f', 2));
        bits << QStringLiteral("IBL diff %1").arg(QString::number(pbr.ibl_diffuse_strength, 'f', 2));
        bits << QStringLiteral("IBL spec %1").arg(QString::number(pbr.ibl_specular_strength, 'f', 2));
        bits << QStringLiteral("sky %1").arg(QString::number(pbr.sky_light_strength, 'f', 2));
        return bits;
    }

    const PhongShadingSettings& phong = preset.settings.phong;
    QStringList bits;
    bits << stylePresetFocusLabel(preset.focus);
    bits << (phong.toon.enabled
             ? QStringLiteral("%1-step toon").arg(static_cast<int>(std::lround(phong.toon.diffuse_steps)))
             : QStringLiteral("clean phong"));
    bits << QStringLiteral("spec %1").arg(QString::number(phong.specular_strength, 'f', 2));
    if (phong.outline.enabled) {
        bits << QStringLiteral("outline %1 px").arg(QString::number(phong.outline.width_pixels, 'f', 1));
    }
    if (phong.rim_strength > 0.01f) {
        bits << QStringLiteral("rim %1").arg(QString::number(phong.rim_strength, 'f', 2));
    }
    return bits;
}

} // namespace

QJsonObject stylePresetToJson(const StylePreset& preset) {
    QJsonObject object;
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("name"), preset.name);
    object.insert(QStringLiteral("description"), preset.description);
    object.insert(QStringLiteral("sourcePrompt"), preset.source_prompt);
    object.insert(QStringLiteral("focus"), stylePresetFocusToString(preset.focus));
    object.insert(QStringLiteral("preferredPipeline"), renderPipelineToString(preset.preferred_pipeline));
    object.insert(QStringLiteral("lookDev"), lookDevSettingsToJson(preset.settings));
    return object;
}

bool stylePresetFromJson(const QJsonObject& object, StylePreset* preset, QString* error_message) {
    if (!preset) {
        if (error_message) {
            *error_message = QStringLiteral("Preset destination is null.");
        }
        return false;
    }

    if (!object.contains(QStringLiteral("lookDev")) || !object.value(QStringLiteral("lookDev")).isObject()) {
        if (error_message) {
            *error_message = QStringLiteral("Preset file does not contain a valid lookDev payload.");
        }
        return false;
    }

    StylePreset result;
    result.name = object.value(QStringLiteral("name")).toString(QStringLiteral("Imported Preset"));
    result.description = object.value(QStringLiteral("description")).toString(QStringLiteral("Imported look development preset."));
    result.source_prompt = object.value(QStringLiteral("sourcePrompt")).toString();
    result.focus = stylePresetFocusFromString(object.value(QStringLiteral("focus")).toString(QStringLiteral("Phong")));
    result.preferred_pipeline = renderPipelineFromString(object.value(QStringLiteral("preferredPipeline")).toString(QStringLiteral("Raster")));
    result.settings = lookDevSettingsFromJson(object.value(QStringLiteral("lookDev")).toObject(), result.settings);
    clampPhongSettings(&result.settings.phong);
    *preset = result;
    return true;
}

QVector<AiRecommendationCandidate> buildLookDevRecommendations(const QString& prompt, const LookDevSettings& base_settings) {
    const QString normalized = prompt.trimmed().toLower();
    const bool wants_toon = containsAny(normalized, { "toon", "anime", "cartoon", "cel", "cell", "genshin", "mihoyo", "卡通", "动漫", "二次元", "二游", "赛璐璐", "原神" });
    const bool current_pbr = base_settings.shading_model == ShadingModel::Pbr;
    const bool explicit_pbr = containsAny(normalized, { "pbr", "physically", "realistic", "metal", "roughness", "material", "texture", "写实", "真实", "金属", "粗糙", "材质", "贴图", "产品" });
    const bool wants_outline = containsAny(normalized, { "outline", "silhouette", "描边", "轮廓", "线稿" });
    const bool wants_rim = containsAny(normalized, { "rim", "back light", "edge light", "边缘光", "轮廓光" });
    const bool wants_soft = containsAny(normalized, { "soft", "gentle", "mild", "柔", "柔和", "温柔", "温和", "朦", "奶油" });
    const bool wants_sharp = containsAny(normalized, { "sharp", "clean", "crisp", "硬", "锐", "清晰", "干净" });
    const bool wants_figure = containsAny(normalized, { "figure", "statue", "product", "showcase", "手办", "展示", "宣传", "产品", "棚拍" });
    const bool wants_cool = containsAny(normalized, { "cool", "blue", "cyan", "冷", "蓝", "青" });
    const bool wants_warm = containsAny(normalized, { "warm", "gold", "orange", "暖", "金", "橙" });
    const bool wants_hard_cel = containsAny(normalized, { "arcsys", "guilty gear", "dragon ball fighterz", "hard cel", "2d", "罪恶装备", "龙珠斗士", "硬赛璐璐", "动画截图", "强2d" });
    const bool wants_painterly = containsAny(normalized, { "painterly", "hand painted", "painted", "illustration", "厚涂", "手绘", "插画", "绘画感" });
    const bool wants_gooch = containsAny(normalized, { "gooch", "technical illustration", "technical", "冷暖体积", "技术插画", "说明书", "结构" });
    const bool wants_readability = containsAny(normalized, { "tf2", "readability", "readable", "shape", "silhouette clarity", "可读性", "清楚", "轮廓清楚", "形体" });
    const bool wants_clay = containsAny(normalized, { "clay", "matte", "white model", "白模", "灰模", "黏土", "哑光", "形体检查" });

    if ((current_pbr || explicit_pbr) && !wants_toon && !wants_outline) {
        StylePreset pbr_preset = makePbrPreset(QStringLiteral("PBR Neutral LookDev"),
                                               QStringLiteral("Keeps the renderer in PBR and tunes only effective PBR controls."),
                                               prompt,
                                               base_settings);
        if (wants_soft || containsAny(normalized, { "studio", "softbox", "棚拍", "柔光", "干净" })) {
            configurePbrSoftStudio(&pbr_preset);
        } else if (wants_figure || containsAny(normalized, { "premium", "contrast", "高级", "黑底", "反射" })) {
            configurePbrContrastProduct(&pbr_preset);
        } else {
            configurePbrNeutral(&pbr_preset);
        }
        if (wants_sharp) {
            pbr_preset.settings.normal_strength = clampValue(pbr_preset.settings.normal_strength + 0.08f, 0.0f, 2.0f);
            pbr_preset.settings.pbr.sky_light_strength = clampValue(pbr_preset.settings.pbr.sky_light_strength - 0.08f, 0.0f, 2.0f);
        }
        if (wants_cool) {
            pbr_preset.description += QStringLiteral(" Cool mood is approximated through cleaner sky/specular balance because material color tint is not exposed in the current PBR panel.");
        }
        if (wants_warm) {
            pbr_preset.settings.exposure = clampValue(pbr_preset.settings.exposure + 0.06f, 0.1f, 3.0f);
            pbr_preset.description += QStringLiteral(" Warm mood is approximated through exposure/fill because material color tint is not exposed in the current PBR panel.");
        }
        AiRecommendationCandidate candidate;
        candidate.slot_label = QStringLiteral("Best");
        candidate.preset = pbr_preset;
        candidate.summary = summarizeStylePreset(pbr_preset);
        return QVector<AiRecommendationCandidate>{ candidate };
    }

    StylePreset selected = makeBasePreset(QStringLiteral("Soft Game Anime"),
                                          QStringLiteral("Mature rendering-skill fallback for one coherent artist-facing look."),
                                          StylePresetFocus::Toon,
                                          prompt,
                                          base_settings);

    if (wants_clay) {
        configureClayReview(&selected);
    } else if (wants_gooch) {
        configureGoochIllustration(&selected);
    } else if (wants_painterly) {
        configurePainterly(&selected);
    } else if (wants_hard_cel || (wants_toon && wants_sharp && !wants_soft)) {
        configureHardCel(&selected);
    } else if (wants_figure) {
        configureFigureShowcase(&selected);
    } else if (wants_readability && !wants_toon) {
        configureReadabilityFirst(&selected);
    } else if (wants_toon || wants_soft) {
        configureSoftGameAnime(&selected);
    } else {
        selected.name = QStringLiteral("Polished Phong");
        selected.description = QStringLiteral("Conservative readable lookdev pass using controlled specular and rim separation without forcing a toon style.");
        selected.focus = StylePresetFocus::Phong;
        configureCleanPhong(&selected, 0.58f);
    }

    if (wants_soft && !wants_clay) {
        applySoftBias(&selected);
    }
    if (wants_sharp && !wants_soft) {
        applySharpBias(&selected);
    }
    if (wants_outline) {
        applyOutlineBias(&selected, selected.settings.phong.toon.enabled ? 0.35f : 0.65f);
    }
    if (wants_rim) {
        applyRimBias(&selected, selected.settings.phong.outline.enabled ? 0.10f : 0.18f);
    }
    if (wants_cool && selected.settings.phong.toon.enabled) {
        applyCoolShadowBias(&selected);
    }
    if (wants_warm && selected.settings.phong.toon.enabled) {
        applyWarmShadowBias(&selected);
    }

    clampPhongSettings(&selected.settings.phong);
    AiRecommendationCandidate candidate;
    candidate.slot_label = QStringLiteral("Best");
    candidate.preset = selected;
    candidate.summary = summarizeStylePreset(selected);
    return QVector<AiRecommendationCandidate>{ candidate };
}

QString stylePresetFocusLabel(StylePresetFocus focus) {
    switch (focus) {
    case StylePresetFocus::Toon:
        return QStringLiteral("Toon");
    case StylePresetFocus::Hybrid:
        return QStringLiteral("Hybrid");
    case StylePresetFocus::PbrAssist:
        return QStringLiteral("PBR Assist");
    case StylePresetFocus::Phong:
    default:
        return QStringLiteral("Phong");
    }
}

QString summarizeStylePreset(const StylePreset& preset) {
    return buildSummaryBits(preset).join(QStringLiteral("  |  "));
}

} // namespace haorendergi
