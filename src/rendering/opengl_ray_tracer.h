#pragma once

#include "rendering/render_backend.h"

#include <QOpenGLFunctions_4_3_Core>

#include <cstdint>
#include <vector>

class QOpenGLShaderProgram;

namespace haorendergi {

class OpenGLRayTracer final : public IRenderBackend, protected QOpenGLFunctions_4_3_Core {
public:
    OpenGLRayTracer();
    ~OpenGLRayTracer() override;

    QString backendName() const override;
    bool initialize(QString* error_message) override;
    void shutdown() override;
    void resize(int framebuffer_width, int framebuffer_height) override;
    void uploadScene(const SceneModel& scene) override;
    RenderStats render(const FrameRenderSettings& settings) override;

private:
    struct GpuTriangle {
        float p0[4] = {};
        float p1[4] = {};
        float p2[4] = {};
        float n0[4] = {};
        float n1[4] = {};
        float n2[4] = {};
        float uv0[4] = {};
        float uv1[4] = {};
        float uv2[4] = {};
        int material_index = 0;
        int pad0 = 0;
        int pad1 = 0;
        int pad2 = 0;
    };

    struct GpuMaterial {
        float base_color[4] = {};
        float emissive_factor[4] = {};
        float pbr_factors[4] = {};
        float albedo_atlas_rect[4] = {};
        float metallic_atlas_rect[4] = {};
        float roughness_atlas_rect[4] = {};
        float ao_atlas_rect[4] = {};
        float emissive_atlas_rect[4] = {};
        int texture_flags0[4] = {};
        int texture_flags1[4] = {};
    };

    struct GpuBvhNode {
        float bounds_min[4] = {};
        float bounds_max[4] = {};
        int left_index = -1;
        int right_index = -1;
        int first_triangle = 0;
        int triangle_count = 0;
    };

    struct GpuLightTriangle {
        float p0[4] = {};
        float p1[4] = {};
        float p2[4] = {};
        float normal_area[4] = {};
        float emission[4] = {};
    };

    struct BuildTriangle {
        GpuTriangle gpu;
        Bounds bounds;
        Eigen::Vector3f centroid = Eigen::Vector3f::Zero();
    };

    struct MaterialAtlasImages {
        QImage albedo;
        QImage metallic;
        QImage roughness;
        QImage ao;
        QImage emissive;
    };

    bool buildProgram(QString* error_message);
    bool buildDisplayProgram(QString* error_message);
    void destroyProgram();
    void createFullscreenVao();
    void destroyFullscreenVao();
    void createAccumulationTargets();
    void destroyAccumulationTargets();
    void resetAccumulation();
    void destroySceneBuffers();
    void uploadBuffer(unsigned int& buffer, unsigned int binding, const void* data, std::size_t byte_size);
    void flattenScene(const SceneModel& scene, int max_texture_size, std::vector<BuildTriangle>& build_triangles, std::vector<GpuMaterial>& materials, MaterialAtlasImages& atlases) const;
    unsigned int uploadTextureAtlas(const QImage& atlas, const QColor& fallback);
    void buildBvh(std::vector<BuildTriangle>& build_triangles, std::vector<GpuTriangle>& triangles, std::vector<GpuBvhNode>& nodes) const;
    int buildBvhNode(std::vector<BuildTriangle>& build_triangles, int first, int count, std::vector<GpuBvhNode>& nodes) const;

    bool initialized_ = false;
    int framebuffer_width_ = 1;
    int framebuffer_height_ = 1;
    unsigned int target_framebuffer_ = 0;
    unsigned int fullscreen_vao_ = 0;
    unsigned int accumulation_fbo_ = 0;
    unsigned int accumulation_textures_[2] = { 0, 0 };
    int accumulation_texture_width_ = 0;
    int accumulation_texture_height_ = 0;
    int accumulation_read_index_ = 0;
    std::uint64_t accumulation_frame_index_ = 0;
    std::uint64_t accumulation_signature_ = 0;
    unsigned int triangle_ssbo_ = 0;
    unsigned int material_ssbo_ = 0;
    unsigned int bvh_ssbo_ = 0;
    unsigned int light_triangle_ssbo_ = 0;
    unsigned int albedo_atlas_texture_ = 0;
    unsigned int metallic_atlas_texture_ = 0;
    unsigned int roughness_atlas_texture_ = 0;
    unsigned int ao_atlas_texture_ = 0;
    unsigned int emissive_atlas_texture_ = 0;
    int triangle_count_ = 0;
    int material_count_ = 0;
    int bvh_node_count_ = 0;
    int light_triangle_count_ = 0;
    int albedo_texture_count_ = 0;
    int metallic_texture_count_ = 0;
    int roughness_texture_count_ = 0;
    int ao_texture_count_ = 0;
    int emissive_texture_count_ = 0;
    QOpenGLShaderProgram* trace_program_ = nullptr;
    QOpenGLShaderProgram* display_program_ = nullptr;
};

} // namespace haorendergi
