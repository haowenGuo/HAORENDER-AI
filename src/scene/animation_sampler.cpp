#include "scene/animation_sampler.h"

#include "scene/vrm_expression_controller.h"

#include <algorithm>
#include <cmath>

namespace haorendergi {
namespace {

struct TransformParts {
    Eigen::Vector3f translation = Eigen::Vector3f::Zero();
    Eigen::Quaternionf rotation = Eigen::Quaternionf::Identity();
    Eigen::Vector3f scale = Eigen::Vector3f::Ones();
};

TransformParts decomposeTransform(const Eigen::Matrix4f& matrix) {
    TransformParts parts;
    parts.translation = matrix.block<3, 1>(0, 3);

    Eigen::Matrix3f linear = matrix.block<3, 3>(0, 0);
    for (int axis = 0; axis < 3; ++axis) {
        const float length = linear.col(axis).norm();
        parts.scale[axis] = length > 1e-8f ? length : 1.0f;
        if (length > 1e-8f) {
            linear.col(axis) /= length;
        }
    }
    parts.rotation = Eigen::Quaternionf(linear).normalized();
    return parts;
}

Eigen::Matrix4f composeTransform(const TransformParts& parts) {
    Eigen::Matrix4f matrix = Eigen::Matrix4f::Identity();
    Eigen::Matrix3f linear = parts.rotation.normalized().toRotationMatrix();
    linear.col(0) *= parts.scale.x();
    linear.col(1) *= parts.scale.y();
    linear.col(2) *= parts.scale.z();
    matrix.block<3, 3>(0, 0) = linear;
    matrix.block<3, 1>(0, 3) = parts.translation;
    return matrix;
}

float lookAtLimit(float configured_limit, float fallback) {
    return configured_limit > 1e-4f ? configured_limit : fallback;
}

void applyEyeGazeToLocalPose(const SceneModel& scene, std::vector<Eigen::Matrix4f>* local_transforms) {
    if (!local_transforms || !scene.eye_gaze.enabled || scene.eye_gaze.weight <= 1e-5f) {
        return;
    }
    const int left_eye = scene.vrm_look_at.left_eye_node_index;
    const int right_eye = scene.vrm_look_at.right_eye_node_index;
    if ((left_eye < 0 || left_eye >= static_cast<int>(local_transforms->size())) &&
        (right_eye < 0 || right_eye >= static_cast<int>(local_transforms->size()))) {
        return;
    }

    const float yaw_limit = std::max(lookAtLimit(scene.vrm_look_at.horizontal_inner.output_scale, 18.0f),
                                     lookAtLimit(scene.vrm_look_at.horizontal_outer.output_scale, 18.0f));
    const float pitch_up_limit = lookAtLimit(scene.vrm_look_at.vertical_up.output_scale, 12.0f);
    const float pitch_down_limit = lookAtLimit(scene.vrm_look_at.vertical_down.output_scale, 12.0f);
    const float weight = std::clamp(scene.eye_gaze.weight, 0.0f, 1.0f);
    const float yaw = std::clamp(scene.eye_gaze.yaw_degrees, -yaw_limit, yaw_limit) * weight;
    const float pitch_min = -pitch_down_limit;
    const float pitch_max = pitch_up_limit;
    const float pitch = std::clamp(scene.eye_gaze.pitch_degrees, pitch_min, pitch_max) * weight;
    if (std::abs(yaw) <= 1e-4f && std::abs(pitch) <= 1e-4f) {
        return;
    }

    constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
    const Eigen::Quaternionf gaze_rotation =
        (Eigen::AngleAxisf(yaw * kDegreesToRadians, Eigen::Vector3f::UnitY()) *
         Eigen::AngleAxisf(pitch * kDegreesToRadians, Eigen::Vector3f::UnitX())).normalized();

    const auto apply_to_eye = [&](int node_index) {
        if (node_index < 0 || node_index >= static_cast<int>(local_transforms->size())) {
            return;
        }
        TransformParts parts = decomposeTransform((*local_transforms)[node_index]);
        parts.rotation = (parts.rotation * gaze_rotation).normalized();
        (*local_transforms)[node_index] = composeTransform(parts);
    };
    apply_to_eye(left_eye);
    apply_to_eye(right_eye);
}

Eigen::Vector3f sampleVectorKeys(const std::vector<VectorKeyframe>& keys,
                                 double time_ticks,
                                 const Eigen::Vector3f& fallback) {
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1 || time_ticks <= keys.front().time_ticks) {
        return keys.front().value;
    }
    if (time_ticks >= keys.back().time_ticks) {
        return keys.back().value;
    }

    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const VectorKeyframe& a = keys[i];
        const VectorKeyframe& b = keys[i + 1];
        if (time_ticks >= a.time_ticks && time_ticks <= b.time_ticks) {
            const double span = std::max(1e-8, b.time_ticks - a.time_ticks);
            const float t = static_cast<float>((time_ticks - a.time_ticks) / span);
            return ((1.0f - t) * a.value + t * b.value).eval();
        }
    }
    return keys.back().value;
}

float sampleScalarKeys(const std::vector<ScalarKeyframe>& keys,
                       double time_ticks,
                       float fallback) {
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1 || time_ticks <= keys.front().time_ticks) {
        return keys.front().value;
    }
    if (time_ticks >= keys.back().time_ticks) {
        return keys.back().value;
    }

    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const ScalarKeyframe& a = keys[i];
        const ScalarKeyframe& b = keys[i + 1];
        if (time_ticks >= a.time_ticks && time_ticks <= b.time_ticks) {
            const double span = std::max(1e-8, b.time_ticks - a.time_ticks);
            const float t = static_cast<float>((time_ticks - a.time_ticks) / span);
            return (1.0f - t) * a.value + t * b.value;
        }
    }
    return keys.back().value;
}

std::vector<float> sampleMorphWeightKeys(const std::vector<MorphWeightKeyframe>& keys,
                                         double time_ticks,
                                         const std::vector<float>& fallback) {
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1 || time_ticks <= keys.front().time_ticks) {
        return keys.front().values;
    }
    if (time_ticks >= keys.back().time_ticks) {
        return keys.back().values;
    }

    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const MorphWeightKeyframe& a = keys[i];
        const MorphWeightKeyframe& b = keys[i + 1];
        if (time_ticks >= a.time_ticks && time_ticks <= b.time_ticks) {
            const double span = std::max(1e-8, b.time_ticks - a.time_ticks);
            const float t = static_cast<float>((time_ticks - a.time_ticks) / span);
            const std::size_t count = std::max(a.values.size(), b.values.size());
            std::vector<float> values(count, 0.0f);
            for (std::size_t value_index = 0; value_index < count; ++value_index) {
                const float av = value_index < a.values.size() ? a.values[value_index] : 0.0f;
                const float bv = value_index < b.values.size() ? b.values[value_index] : 0.0f;
                values[value_index] = (1.0f - t) * av + t * bv;
            }
            return values;
        }
    }
    return keys.back().values;
}

Eigen::Quaternionf sampleRotationKeys(const std::vector<RotationKeyframe>& keys,
                                      double time_ticks,
                                      const Eigen::Quaternionf& fallback) {
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1 || time_ticks <= keys.front().time_ticks) {
        return keys.front().value.normalized();
    }
    if (time_ticks >= keys.back().time_ticks) {
        return keys.back().value.normalized();
    }

    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const RotationKeyframe& a = keys[i];
        const RotationKeyframe& b = keys[i + 1];
        if (time_ticks >= a.time_ticks && time_ticks <= b.time_ticks) {
            const double span = std::max(1e-8, b.time_ticks - a.time_ticks);
            const float t = static_cast<float>((time_ticks - a.time_ticks) / span);
            return a.value.slerp(t, b.value).normalized();
        }
    }
    return keys.back().value.normalized();
}

std::vector<Eigen::Matrix4f> buildNodePose(const SceneModel& scene,
                                           const AnimationClipData& clip,
                                           double time_seconds) {
    std::vector<Eigen::Matrix4f> local_transforms(scene.nodes.size(), Eigen::Matrix4f::Identity());
    for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
        local_transforms[i] = scene.nodes[i].local_bind_transform;
    }

    const double tps = clip.ticks_per_second > 0.0 ? clip.ticks_per_second : 25.0;
    const double duration_ticks = clip.duration_ticks > 0.0 ? clip.duration_ticks : 1.0;
    const double raw_ticks = std::fmod(std::max(0.0, time_seconds) * tps, duration_ticks);
    const double time_ticks = raw_ticks < 0.0 ? raw_ticks + duration_ticks : raw_ticks;

    for (const AnimationChannelData& channel : clip.channels) {
        if (channel.node_index < 0 || channel.node_index >= static_cast<int>(local_transforms.size())) {
            continue;
        }
        TransformParts parts = decomposeTransform(scene.nodes[channel.node_index].local_bind_transform);
        parts.translation = sampleVectorKeys(channel.positions, time_ticks, parts.translation);
        parts.rotation = sampleRotationKeys(channel.rotations, time_ticks, parts.rotation);
        parts.scale = sampleVectorKeys(channel.scales, time_ticks, parts.scale);
        local_transforms[channel.node_index] = composeTransform(parts);
    }

    applyEyeGazeToLocalPose(scene, &local_transforms);

    std::vector<Eigen::Matrix4f> global_transforms(local_transforms.size(), Eigen::Matrix4f::Identity());
    for (std::size_t i = 0; i < local_transforms.size(); ++i) {
        const int parent_index = scene.nodes[i].parent_index;
        if (parent_index >= 0 && parent_index < static_cast<int>(global_transforms.size())) {
            global_transforms[i] = global_transforms[parent_index] * local_transforms[i];
        } else {
            global_transforms[i] = local_transforms[i];
        }
    }
    return global_transforms;
}

std::vector<Eigen::Matrix4f> buildGlobalBindTransforms(const SceneModel& scene) {
    std::vector<Eigen::Matrix4f> globals(scene.nodes.size(), Eigen::Matrix4f::Identity());
    for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
        const int parent_index = scene.nodes[i].parent_index;
        if (parent_index >= 0 && parent_index < static_cast<int>(globals.size())) {
            globals[i] = globals[parent_index] * scene.nodes[i].local_bind_transform;
        } else {
            globals[i] = scene.nodes[i].local_bind_transform;
        }
    }
    return globals;
}

std::vector<ExpressionWeightData> sampleExpressionWeights(const SceneModel& scene,
                                                          const AnimationClipData& clip,
                                                          double time_ticks) {
    std::vector<ExpressionWeightData> weights = scene.expression_weights;
    for (const ExpressionChannelData& channel : clip.expression_channels) {
        const float sampled_weight = std::clamp(sampleScalarKeys(channel.weights, time_ticks, 0.0f), 0.0f, 1.0f);
        auto found = std::find_if(weights.begin(), weights.end(), [&](const ExpressionWeightData& item) {
            return item.name == channel.name;
        });
        if (found == weights.end()) {
            if (sampled_weight > 1e-5f) {
                weights.push_back(ExpressionWeightData{ channel.name, sampled_weight });
            }
        } else {
            found->weight = std::max(found->weight, sampled_weight);
        }
    }
    return weights;
}

const AnimationChannelData* channelForNode(const AnimationClipData& clip, int node_index) {
    for (const AnimationChannelData& channel : clip.channels) {
        if (channel.node_index == node_index) {
            return &channel;
        }
    }
    return nullptr;
}

std::vector<float> buildRuntimeMorphWeights(const SceneModel& scene,
                                            const MeshData& mesh,
                                            const AnimationClipData& clip,
                                            double time_ticks,
                                            const std::vector<ExpressionWeightData>& expression_weights) {
    std::vector<float> morph_weights = baseMorphWeightsForMesh(mesh);
    if (const AnimationChannelData* channel = channelForNode(clip, mesh.node_index)) {
        morph_weights = sampleMorphWeightKeys(channel->morph_weights, time_ticks, morph_weights);
    }
    if (morph_weights.size() < mesh.morph_targets.size()) {
        morph_weights.resize(mesh.morph_targets.size(), 0.0f);
    }
    applyVrmExpressionWeightsToMesh(scene, mesh, expression_weights, &morph_weights);
    return morph_weights;
}

std::vector<Vertex> buildMorphedVertices(const MeshData& bind_mesh) {
    std::vector<Vertex> vertices = bind_mesh.vertices;
    if (bind_mesh.morph_targets.empty() || bind_mesh.morph_weights.empty()) {
        return vertices;
    }

    const std::size_t target_count = std::min(bind_mesh.morph_targets.size(), bind_mesh.morph_weights.size());
    for (std::size_t target_index = 0; target_index < target_count; ++target_index) {
        const float weight = bind_mesh.morph_weights[target_index];
        if (std::abs(weight) <= 1e-6f) {
            continue;
        }

        const MorphTargetData& target = bind_mesh.morph_targets[target_index];
        const std::size_t position_count = std::min(vertices.size(), target.position_deltas.size());
        for (std::size_t vertex_index = 0; vertex_index < position_count; ++vertex_index) {
            vertices[vertex_index].position += weight * target.position_deltas[vertex_index];
        }

        const std::size_t normal_count = std::min(vertices.size(), target.normal_deltas.size());
        for (std::size_t vertex_index = 0; vertex_index < normal_count; ++vertex_index) {
            vertices[vertex_index].normal += weight * target.normal_deltas[vertex_index];
        }

        const std::size_t tangent_count = std::min(vertices.size(), target.tangent_deltas.size());
        for (std::size_t vertex_index = 0; vertex_index < tangent_count; ++vertex_index) {
            vertices[vertex_index].tangent += weight * target.tangent_deltas[vertex_index];
        }
    }

    for (Vertex& vertex : vertices) {
        if (vertex.normal.squaredNorm() > 1e-8f) {
            vertex.normal.normalize();
        }
        if (vertex.tangent.squaredNorm() > 1e-8f) {
            vertex.tangent.normalize();
        }
    }
    return vertices;
}

void animateRigidMeshNode(const MeshData& bind_mesh,
                          const std::vector<Eigen::Matrix4f>& global_transforms,
                          const std::vector<Eigen::Matrix4f>& global_bind_transforms,
                          MeshData* output_mesh,
                          Bounds* bounds) {
    if (!output_mesh) {
        return;
    }

    output_mesh->vertices = buildMorphedVertices(bind_mesh);
    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
    if (bind_mesh.node_index >= 0 &&
        bind_mesh.node_index < static_cast<int>(global_transforms.size()) &&
        bind_mesh.node_index < static_cast<int>(global_bind_transforms.size())) {
        transform = global_transforms[bind_mesh.node_index] * global_bind_transforms[bind_mesh.node_index].inverse();
    }

    Eigen::Matrix3f normal_matrix = transform.block<3, 3>(0, 0);
    if (std::abs(normal_matrix.determinant()) > 1e-8f) {
        normal_matrix = normal_matrix.inverse().transpose();
    }

    for (Vertex& vertex : output_mesh->vertices) {
        const Eigen::Vector4f p(vertex.position.x(), vertex.position.y(), vertex.position.z(), 1.0f);
        vertex.position = (transform * p).hnormalized();
        vertex.normal = (normal_matrix * vertex.normal).normalized();
        if (!vertex.tangent.isZero(0.0f)) {
            vertex.tangent = (normal_matrix * vertex.tangent).normalized();
        }
        if (!vertex.bitangent.isZero(0.0f)) {
            vertex.bitangent = (normal_matrix * vertex.bitangent).normalized();
        }
        bounds->include(vertex.position);
    }
}

void skinMesh(const MeshData& bind_mesh,
              const std::vector<Eigen::Matrix4f>& global_transforms,
              const std::vector<Eigen::Matrix4f>& global_bind_transforms,
              MeshData* output_mesh,
              Bounds* bounds) {
    if (!output_mesh) {
        return;
    }
    if (!bind_mesh.skinned()) {
        animateRigidMeshNode(bind_mesh, global_transforms, global_bind_transforms, output_mesh, bounds);
        return;
    }

    const std::vector<Vertex> morphed_vertices = buildMorphedVertices(bind_mesh);
    output_mesh->vertices.resize(morphed_vertices.size());
    for (std::size_t i = 0; i < morphed_vertices.size(); ++i) {
        const Vertex& source = morphed_vertices[i];
        Vertex animated = source;
        Eigen::Vector4f skinned_position = Eigen::Vector4f::Zero();
        Eigen::Vector3f skinned_normal = Eigen::Vector3f::Zero();
        Eigen::Vector3f skinned_tangent = Eigen::Vector3f::Zero();
        Eigen::Vector3f skinned_bitangent = Eigen::Vector3f::Zero();
        float total_weight = 0.0f;

        for (int influence = 0; influence < 4; ++influence) {
            const int skin_bone_index = source.bone_indices[influence];
            const float weight = source.bone_weights[influence];
            if (skin_bone_index < 0 || skin_bone_index >= static_cast<int>(bind_mesh.skin_bones.size()) || weight <= 0.0f) {
                continue;
            }

            const SkinBoneData& skin_bone = bind_mesh.skin_bones[skin_bone_index];
            if (skin_bone.node_index < 0 || skin_bone.node_index >= static_cast<int>(global_transforms.size())) {
                continue;
            }

            const Eigen::Matrix4f skin_matrix = global_transforms[skin_bone.node_index] * skin_bone.inverse_bind_matrix;
            const Eigen::Vector4f p(source.position.x(), source.position.y(), source.position.z(), 1.0f);
            skinned_position += weight * (skin_matrix * p);
            skinned_normal += weight * (skin_matrix.block<3, 3>(0, 0) * source.normal);
            if (!source.tangent.isZero(0.0f)) {
                skinned_tangent += weight * (skin_matrix.block<3, 3>(0, 0) * source.tangent);
            }
            if (!source.bitangent.isZero(0.0f)) {
                skinned_bitangent += weight * (skin_matrix.block<3, 3>(0, 0) * source.bitangent);
            }
            total_weight += weight;
        }

        if (total_weight > 1e-6f) {
            animated.position = skinned_position.hnormalized();
            animated.normal = skinned_normal.squaredNorm() > 1e-8f ? skinned_normal.normalized() : source.normal;
            animated.tangent = skinned_tangent.squaredNorm() > 1e-8f ? skinned_tangent.normalized() : source.tangent;
            animated.bitangent = skinned_bitangent.squaredNorm() > 1e-8f ? skinned_bitangent.normalized() : source.bitangent;
        }
        output_mesh->vertices[i] = animated;
        bounds->include(animated.position);
    }
}

void translateMesh(MeshData* mesh, const Eigen::Vector3f& delta) {
    if (!mesh) {
        return;
    }
    for (Vertex& vertex : mesh->vertices) {
        vertex.position += delta;
    }
}

void translateBounds(Bounds* bounds, const Eigen::Vector3f& delta) {
    if (!bounds || !bounds->valid()) {
        return;
    }
    bounds->min += delta;
    bounds->max += delta;
}

} // namespace

bool sampleSceneAnimation(const SceneModel& bind_scene,
                          int animation_index,
                          double time_seconds,
                          SceneModel* output_scene) {
    if (!output_scene ||
        animation_index < 0 ||
        animation_index >= static_cast<int>(bind_scene.animations.size()) ||
        bind_scene.nodes.empty()) {
        return false;
    }

    if (output_scene->meshes.size() != bind_scene.meshes.size()) {
        *output_scene = bind_scene;
    }

    const AnimationClipData& clip = bind_scene.animations[animation_index];
    const double tps = clip.ticks_per_second > 0.0 ? clip.ticks_per_second : 25.0;
    const double duration_ticks = clip.duration_ticks > 0.0 ? clip.duration_ticks : 1.0;
    const double raw_ticks = std::fmod(std::max(0.0, time_seconds) * tps, duration_ticks);
    const double time_ticks = raw_ticks < 0.0 ? raw_ticks + duration_ticks : raw_ticks;
    const std::vector<ExpressionWeightData> expression_weights = sampleExpressionWeights(bind_scene, clip, time_ticks);
    const std::vector<Eigen::Matrix4f> global_transforms = buildNodePose(bind_scene, clip, time_seconds);
    const std::vector<Eigen::Matrix4f> global_bind_transforms = buildGlobalBindTransforms(bind_scene);
    Bounds animated_bounds;
    for (std::size_t mesh_index = 0; mesh_index < bind_scene.meshes.size(); ++mesh_index) {
        MeshData runtime_mesh = bind_scene.meshes[mesh_index];
        runtime_mesh.morph_weights = buildRuntimeMorphWeights(bind_scene, runtime_mesh, clip, time_ticks, expression_weights);
        skinMesh(runtime_mesh, global_transforms, global_bind_transforms, &output_scene->meshes[mesh_index], &animated_bounds);
    }
    if (bind_scene.lock_animation_to_bind_floor && animated_bounds.valid() && bind_scene.bounds.valid()) {
        const float floor_delta = bind_scene.bounds.min.y() - animated_bounds.min.y();
        if (std::abs(floor_delta) > 1e-5f) {
            const Eigen::Vector3f delta(0.0f, floor_delta, 0.0f);
            for (MeshData& mesh : output_scene->meshes) {
                translateMesh(&mesh, delta);
            }
            translateBounds(&animated_bounds, delta);
        }
    }
    if (animated_bounds.valid()) {
        output_scene->bounds = animated_bounds;
    }
    return true;
}

} // namespace haorendergi
