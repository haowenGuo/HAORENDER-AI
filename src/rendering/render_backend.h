#pragma once

#include "scene/scene_types.h"

#include <QColor>
#include <QString>

#include <Eigen/Dense>

namespace haorendergi {

enum class RenderPipeline {
    Raster = 0,
    RayTrace = 1,
    DxrRayTrace = 2
};

enum class RayTraceViewMode {
    Lit = 0,
    Hit = 1,
    Normal = 2,
    Albedo = 3
};

enum class RayTraceSceneMode {
    CornellBox = 0,
    LoadedModel = 1
};

enum class RayTraceIntegrator {
    Hybrid = 0,
    PathTrace = 1,
    PathTraceNee = 2,
    PhotonPath = 3
};

struct PbrShadingSettings {
    bool ibl_enabled = true;
    float ibl_diffuse_strength = 0.55f;
    float ibl_specular_strength = 0.80f;
    float sky_light_strength = 0.20f;
    int metallic_channel = 2;
    int roughness_channel = 1;
    int ao_channel = 0;
    int emissive_channel = 0;
};

struct PhongOutlineSettings {
    bool enabled = false;
    float width_pixels = 2.5f;
    float opacity = 1.0f;
    float depth_bias = 0.0015f;
    QColor color = QColor(16, 18, 24);
};

struct ToonShadingSettings {
    bool enabled = false;
    float diffuse_steps = 3.0f;
    float diffuse_softness = 0.03f;
    float shadow_floor = 0.08f;
    float lit_floor = 0.42f;
    float ramp_bias = 0.0f;
    float ramp_contrast = 1.0f;
    float shadow_map_strength = 0.45f;
    float shadow_threshold = 0.42f;
    float shadow_softness = 0.05f;
    QColor shadow_tint = QColor(186, 198, 255);
    float highlight_threshold = 0.36f;
    float highlight_softness = 0.06f;
    float highlight_strength = 0.85f;
    QColor highlight_tint = QColor(255, 248, 240);
    float rim_threshold = 0.30f;
    float rim_softness = 0.08f;
    bool material_override_enabled = false;
    float material_texture_strength = 1.0f;
    float material_lift = 0.0f;
    float material_saturation = 1.0f;
    float material_contrast = 1.0f;
};

struct PhongShadingSettings {
    bool hard_specular = false;
    bool use_tonemap = false;
    bool primary_light_only = true;
    float secondary_light_scale = 0.12f;
    float diffuse_strength = 1.0f;
    float ambient_strength = 0.03f;
    QColor ambient_color = QColor(255, 255, 255);
    float specular_strength = 0.12f;
    QColor specular_tint = QColor(255, 255, 255);
    float smoothness = 1.0f;
    float specular_map_weight = 1.0f;
    float shininess = 28.0f;
    float rim_strength = 0.0f;
    float rim_power = 2.5f;
    QColor rim_tint = QColor(255, 255, 255);
    ToonShadingSettings toon;
    PhongOutlineSettings outline;
};

struct RayTraceSettings {
    RayTraceSceneMode scene_mode = RayTraceSceneMode::CornellBox;
    RayTraceViewMode view_mode = RayTraceViewMode::Lit;
    RayTraceIntegrator integrator = RayTraceIntegrator::PathTraceNee;
    float ambient_strength = 0.05f;
    float shadow_strength = 1.0f;
    int max_bounces = 20;
    int max_nee_bounces = 2;
    int samples_per_frame = 8;
    bool enable_nee = true;
    bool enable_photon_cache = true;
    float photon_radius = 0.22f;
    float photon_intensity = 1.0f;
};

struct LookDevSettings {
    ShadingModel shading_model = ShadingModel::Pbr;
    float exposure = 1.0f;
    float normal_strength = 1.0f;
    bool enable_shadows = true;
    bool enable_backface_culling = true;
    PbrShadingSettings pbr;
    PhongShadingSettings phong;
    RayTraceSettings ray_trace;
};

struct FrameRenderSettings {
    const SceneModel* scene = nullptr;
    Eigen::Matrix4f model_matrix = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f view_matrix = Eigen::Matrix4f::Identity();
    Eigen::Matrix4f projection_matrix = Eigen::Matrix4f::Identity();
    Eigen::Vector3f camera_position = Eigen::Vector3f(0.0f, 0.0f, 3.0f);
    Eigen::Vector3f camera_target = Eigen::Vector3f::Zero();
    Eigen::Vector3f sun_direction = Eigen::Vector3f(-0.45f, -1.0f, -0.25f).normalized();
    Eigen::Vector3f sun_color = Eigen::Vector3f(1.0f, 0.96f, 0.9f);
    QColor clear_color = QColor(24, 28, 34);
    LookDevSettings look_dev;
};

struct RenderStats {
    double frame_ms = 0.0;
    double shadow_ms = 0.0;
    double main_ms = 0.0;
    QString note;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual QString backendName() const = 0;
    virtual bool initialize(QString* error_message) = 0;
    virtual void shutdown() = 0;
    virtual void resize(int framebuffer_width, int framebuffer_height) = 0;
    virtual void uploadScene(const SceneModel& scene) = 0;
    virtual RenderStats render(const FrameRenderSettings& settings) = 0;
};

} // namespace haorendergi
