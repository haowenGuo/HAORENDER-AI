#pragma once

#include "rendering/render_backend.h"

#include <QOpenGLFunctions_3_3_Core>

#include <cstdint>
#include <vector>

class QOpenGLShaderProgram;

namespace haorendergi {

class OpenGLRasterizer final : public IRenderBackend, protected QOpenGLFunctions_3_3_Core {
public:
    OpenGLRasterizer();
    ~OpenGLRasterizer() override;

    QString backendName() const override;
    bool initialize(QString* error_message) override;
    void shutdown() override;
    void resize(int framebuffer_width, int framebuffer_height) override;
    void uploadScene(const SceneModel& scene) override;
    RenderStats render(const FrameRenderSettings& settings) override;

private:
    struct GlVertex {
        float position[3];
        float normal[3];
        float tangent[3];
        float bitangent[3];
        float uv[2];
    };

    struct GpuMaterial {
        Eigen::Vector3f base_color_factor = Eigen::Vector3f::Ones();
        float base_alpha_factor = 1.0f;
        Eigen::Vector3f emissive_factor = Eigen::Vector3f::Zero();
        float metallic_factor = 0.0f;
        float roughness_factor = 0.85f;
        float ao_factor = 1.0f;
        int alpha_mode = 0;
        float alpha_cutoff = 0.5f;
        int render_queue_offset = 0;
        bool double_sided = false;
        bool unlit = false;
        bool mtoon = false;
        bool transparent_with_z_write = false;
        bool face_overlay = false;
        Eigen::Vector3f mtoon_shade_color_factor = Eigen::Vector3f::Ones();
        Eigen::Vector3f mtoon_rim_color_factor = Eigen::Vector3f::Zero();
        float mtoon_shading_shift = 0.0f;
        float mtoon_shading_toony = 0.9f;
        float mtoon_rim_lift = 0.0f;
        float mtoon_rim_fresnel_power = 1.0f;
        unsigned int base_color_texture = 0;
        unsigned int diffuse_texture = 0;
        unsigned int normal_texture = 0;
        unsigned int specular_texture = 0;
        unsigned int metallic_texture = 0;
        unsigned int roughness_texture = 0;
        unsigned int ao_texture = 0;
        unsigned int emissive_texture = 0;
        unsigned int mtoon_shade_texture = 0;
        unsigned int mtoon_matcap_texture = 0;
        bool has_base_color_texture = false;
        bool has_diffuse_texture = false;
        bool has_normal_texture = false;
        bool has_specular_texture = false;
        bool has_metallic_texture = false;
        bool has_roughness_texture = false;
        bool has_ao_texture = false;
        bool has_emissive_texture = false;
        bool has_mtoon_shade_texture = false;
        bool has_mtoon_matcap_texture = false;
    };

    struct GpuMesh {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        unsigned int ebo = 0;
        int vertex_count = 0;
        int index_count = 0;
        std::uint64_t static_signature = 0;
        GpuMaterial material;
    };

    bool buildPrograms(QString* error_message);
    std::vector<GlVertex> buildVertexBuffer(const MeshData& mesh) const;
    std::uint64_t staticMeshSignature(const MeshData& mesh) const;
    bool tryUpdateDynamicVertices(const SceneModel& scene);
    void destroyPrograms();
    void destroyScene();
    void createFallbackTextures();
    void destroyFallbackTextures();
    void ensureShadowResources();
    void destroyShadowResources();
    unsigned int uploadTexture(const QImage& image, bool generate_mipmaps);
    unsigned int createColorTexture(unsigned char r, unsigned char g, unsigned char b, unsigned char a, bool srgb);
    void renderShadowPass(const FrameRenderSettings& settings, RenderStats& stats);
    void renderMainPass(const FrameRenderSettings& settings, RenderStats& stats);
    void renderOutlinePass(const FrameRenderSettings& settings);
    Eigen::Matrix4f buildLightMatrix(const FrameRenderSettings& settings) const;

    bool initialized_ = false;
    int framebuffer_width_ = 1;
    int framebuffer_height_ = 1;
    unsigned int target_framebuffer_ = 0;

    unsigned int fallback_white_ = 0;
    unsigned int fallback_black_ = 0;
    unsigned int fallback_normal_ = 0;
    unsigned int shadow_fbo_ = 0;
    unsigned int shadow_depth_texture_ = 0;
    int shadow_resolution_ = 2048;

    QOpenGLShaderProgram* main_program_ = nullptr;
    QOpenGLShaderProgram* outline_program_ = nullptr;
    QOpenGLShaderProgram* shadow_program_ = nullptr;
    std::vector<GpuMesh> meshes_;
    Eigen::Matrix4f light_matrix_ = Eigen::Matrix4f::Identity();
};

} // namespace haorendergi
