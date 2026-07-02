#pragma once

#include <QImage>

#include <Eigen/Dense>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace haorendergi {

enum class ShadingModel {
    Pbr = 0,
    Phong = 1
};

struct Bounds {
    Eigen::Vector3f min = Eigen::Vector3f::Constant(std::numeric_limits<float>::max());
    Eigen::Vector3f max = Eigen::Vector3f::Constant(std::numeric_limits<float>::lowest());

    bool valid() const {
        return min.x() <= max.x() && min.y() <= max.y() && min.z() <= max.z();
    }

    Eigen::Vector3f center() const {
        if (!valid()) {
            return Eigen::Vector3f::Zero();
        }
        return (0.5f * (min + max)).eval();
    }

    Eigen::Vector3f extent() const {
        if (!valid()) {
            return Eigen::Vector3f::Ones();
        }
        return (max - min).eval();
    }

    float radius() const {
        return valid() ? 0.5f * extent().norm() : 1.0f;
    }

    void include(const Eigen::Vector3f& point) {
        min = min.cwiseMin(point);
        max = max.cwiseMax(point);
    }
};

struct Vertex {
    Eigen::Vector3f position = Eigen::Vector3f::Zero();
    Eigen::Vector3f normal = Eigen::Vector3f(0.0f, 0.0f, 1.0f);
    Eigen::Vector3f tangent = Eigen::Vector3f::Zero();
    Eigen::Vector3f bitangent = Eigen::Vector3f::Zero();
    Eigen::Vector2f uv = Eigen::Vector2f::Zero();
    std::array<int, 4> bone_indices = { -1, -1, -1, -1 };
    std::array<float, 4> bone_weights = { 0.0f, 0.0f, 0.0f, 0.0f };

    bool skinned() const {
        return bone_weights[0] > 0.0f;
    }
};

struct TextureData {
    std::string path;
    QImage image;

    bool valid() const {
        return !image.isNull();
    }
};

struct MaterialData {
    std::string name;
    Eigen::Vector3f base_color_factor = Eigen::Vector3f::Ones();
    float base_alpha_factor = 1.0f;
    Eigen::Vector3f emissive_factor = Eigen::Vector3f::Zero();
    float metallic_factor = 0.0f;
    float roughness_factor = 0.85f;
    float ao_factor = 1.0f;
    int alpha_mode = 0; // 0 opaque, 1 mask, 2 blend
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    bool unlit = false;
    bool mtoon = false;
    int render_queue_offset = 0;
    bool transparent_with_z_write = false;
    Eigen::Vector3f mtoon_shade_color_factor = Eigen::Vector3f::Ones();
    Eigen::Vector3f mtoon_rim_color_factor = Eigen::Vector3f::Zero();
    Eigen::Vector3f mtoon_outline_color_factor = Eigen::Vector3f::Zero();
    float mtoon_shading_shift = 0.0f;
    float mtoon_shading_toony = 0.9f;
    float mtoon_rim_lift = 0.0f;
    float mtoon_rim_fresnel_power = 1.0f;
    float mtoon_outline_width = 0.0f;
    TextureData base_color_texture;
    TextureData diffuse_texture;
    TextureData normal_texture;
    TextureData specular_texture;
    TextureData metallic_texture;
    TextureData roughness_texture;
    TextureData ao_texture;
    TextureData emissive_texture;
    TextureData mtoon_shade_multiply_texture;
    TextureData mtoon_matcap_texture;
    TextureData mtoon_outline_width_texture;
};

struct SkinBoneData {
    std::string name;
    int node_index = -1;
    Eigen::Matrix4f inverse_bind_matrix = Eigen::Matrix4f::Identity();
};

struct MorphTargetData {
    std::string name;
    std::vector<Eigen::Vector3f> position_deltas;
    std::vector<Eigen::Vector3f> normal_deltas;
    std::vector<Eigen::Vector3f> tangent_deltas;
};

struct MeshData {
    std::string name;
    int node_index = -1;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    MaterialData material;
    std::vector<SkinBoneData> skin_bones;
    int morph_target_count = 0;
    std::vector<std::string> morph_target_names;
    std::vector<MorphTargetData> morph_targets;
    std::vector<float> morph_weights;
    std::vector<float> morph_default_weights;

    bool skinned() const {
        return !skin_bones.empty();
    }
};

struct SceneNodeData {
    std::string name;
    int parent_index = -1;
    Eigen::Matrix4f local_bind_transform = Eigen::Matrix4f::Identity();
};

struct VectorKeyframe {
    double time_ticks = 0.0;
    Eigen::Vector3f value = Eigen::Vector3f::Zero();
};

struct RotationKeyframe {
    double time_ticks = 0.0;
    Eigen::Quaternionf value = Eigen::Quaternionf::Identity();
};

struct ScalarKeyframe {
    double time_ticks = 0.0;
    float value = 0.0f;
};

struct MorphWeightKeyframe {
    double time_ticks = 0.0;
    std::vector<float> values;
};

struct ExpressionChannelData {
    std::string name;
    std::vector<ScalarKeyframe> weights;
};

struct AnimationChannelData {
    int node_index = -1;
    std::vector<VectorKeyframe> positions;
    std::vector<RotationKeyframe> rotations;
    std::vector<VectorKeyframe> scales;
    std::vector<MorphWeightKeyframe> morph_weights;
};

struct AnimationClipData {
    std::string name;
    double duration_ticks = 0.0;
    double ticks_per_second = 25.0;
    std::vector<AnimationChannelData> channels;
    std::vector<ExpressionChannelData> expression_channels;

    double durationSeconds() const {
        const double tps = ticks_per_second > 0.0 ? ticks_per_second : 25.0;
        return duration_ticks > 0.0 ? duration_ticks / tps : 0.0;
    }
};

enum class VrmExpressionCategory {
    Preset = 0,
    Custom = 1,
    DirectMorph = 2
};

struct VrmMorphTargetBindData {
    int node_index = -1;
    int morph_target_index = -1;
    float weight = 1.0f;
    std::string node_name;
};

struct VrmExpressionData {
    std::string name;
    VrmExpressionCategory category = VrmExpressionCategory::Preset;
    bool is_binary = false;
    std::string override_blink;
    std::string override_look_at;
    std::string override_mouth;
    std::vector<VrmMorphTargetBindData> morph_target_binds;
};

struct VrmLookAtRangeMapData {
    float input_max_value = 90.0f;
    float output_scale = 0.0f;
};

struct VrmLookAtData {
    std::string type;
    int head_node_index = -1;
    int left_eye_node_index = -1;
    int right_eye_node_index = -1;
    std::string head_node_name;
    std::string left_eye_node_name;
    std::string right_eye_node_name;
    Eigen::Vector3f offset_from_head_bone = Eigen::Vector3f::Zero();
    VrmLookAtRangeMapData horizontal_inner;
    VrmLookAtRangeMapData horizontal_outer;
    VrmLookAtRangeMapData vertical_down;
    VrmLookAtRangeMapData vertical_up;
};

struct ExpressionWeightData {
    std::string name;
    float weight = 0.0f;
};

struct EyeGazeControlData {
    bool enabled = false;
    float yaw_degrees = 0.0f;
    float pitch_degrees = 0.0f;
    float weight = 1.0f;
};

struct EyeGazeKeyframeData {
    double time_ticks = 0.0;
    float yaw_degrees = 0.0f;
    float pitch_degrees = 0.0f;
    float weight = 0.0f;
};

struct SceneModel {
    std::string source_path;
    std::vector<MeshData> meshes;
    std::vector<SceneNodeData> nodes;
    std::vector<AnimationClipData> animations;
    std::vector<VrmExpressionData> vrm_expressions;
    std::vector<ExpressionWeightData> expression_weights;
    VrmLookAtData vrm_look_at;
    EyeGazeControlData eye_gaze;
    Bounds bounds;
    bool has_camera = false;
    Eigen::Vector3f camera_position = Eigen::Vector3f::Zero();
    Eigen::Vector3f camera_target = Eigen::Vector3f(0.0f, 0.0f, -1.0f);
    float camera_fov_degrees = 55.0f;
    bool lock_animation_to_bind_floor = false;

    bool empty() const {
        return meshes.empty();
    }

    std::size_t triangleCount() const {
        std::size_t count = 0;
        for (const auto& mesh : meshes) {
            count += mesh.indices.size() / 3;
        }
        return count;
    }

    std::size_t vertexCount() const {
        std::size_t count = 0;
        for (const auto& mesh : meshes) {
            count += mesh.vertices.size();
        }
        return count;
    }

    bool hasAnimations() const {
        return !animations.empty();
    }

    bool hasSkinnedMeshes() const {
        return std::any_of(meshes.begin(), meshes.end(), [](const MeshData& mesh) {
            return mesh.skinned();
        });
    }
};

} // namespace haorendergi
