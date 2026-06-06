#include "rigging/animation_retargeter.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace haorendergi {
namespace {

QHash<QString, int> buildNodeIndex(const SceneModel& scene) {
    QHash<QString, int> index;
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        const QString name = QString::fromStdString(scene.nodes[i].name);
        if (!name.isEmpty()) {
            index.insert(name, i);
        }
    }
    return index;
}

QHash<int, const AnimationChannelData*> buildChannelIndex(const AnimationClipData& clip) {
    QHash<int, const AnimationChannelData*> index;
    for (const AnimationChannelData& channel : clip.channels) {
        if (channel.node_index >= 0) {
            index.insert(channel.node_index, &channel);
        }
    }
    return index;
}

AnimationChannelData* mutableChannelForNode(AnimationClipData* clip, int node_index) {
    if (!clip || node_index < 0) {
        return nullptr;
    }
    for (AnimationChannelData& channel : clip->channels) {
        if (channel.node_index == node_index) {
            return &channel;
        }
    }
    AnimationChannelData channel;
    channel.node_index = node_index;
    clip->channels.push_back(std::move(channel));
    return &clip->channels.back();
}

bool isFingerCanonicalName(const QString& canonical_name);
QString sidePrefixForCanonical(const QString& canonical_name);
bool shouldStabilizeUpperLimbCanonical(const QString& canonical_name);
bool shouldSolveUpperLimbDirection(const QString& canonical_name);
bool isUpperLimbIkCanonical(const QString& canonical_name);
bool isEyeCanonical(const QString& canonical_name);
Eigen::Vector3f nodePosition(const std::vector<Eigen::Matrix4f>& globals, int node_index);

QString preferredChildCanonical(const QString& canonical) {
    if (canonical == QStringLiteral("Hips")) return QStringLiteral("Spine");
    if (canonical == QStringLiteral("Spine")) return QStringLiteral("Chest");
    if (canonical == QStringLiteral("Chest")) return QStringLiteral("Neck");
    if (canonical == QStringLiteral("Neck")) return QStringLiteral("Head");
    if (canonical.endsWith(QStringLiteral("Shoulder"))) return canonical.left(canonical.size() - 8) + QStringLiteral("UpperArm");
    if (canonical.endsWith(QStringLiteral("UpperArm"))) return canonical.left(canonical.size() - 8) + QStringLiteral("LowerArm");
    if (canonical.endsWith(QStringLiteral("LowerArm"))) return canonical.left(canonical.size() - 8) + QStringLiteral("Hand");
    if (canonical.endsWith(QStringLiteral("UpperLeg"))) return canonical.left(canonical.size() - 8) + QStringLiteral("LowerLeg");
    if (canonical.endsWith(QStringLiteral("LowerLeg"))) return canonical.left(canonical.size() - 8) + QStringLiteral("Foot");
    if (canonical.endsWith(QStringLiteral("Hand"))) return canonical.left(canonical.size() - 4) + QStringLiteral("MiddleProximal");
    if (canonical.endsWith(QStringLiteral("ThumbMetacarpal"))) return canonical.left(canonical.size() - 15) + QStringLiteral("ThumbProximal");
    if (canonical.endsWith(QStringLiteral("ThumbProximal"))) return canonical.left(canonical.size() - 13) + QStringLiteral("ThumbDistal");
    if (canonical.endsWith(QStringLiteral("IndexProximal"))) return canonical.left(canonical.size() - 13) + QStringLiteral("IndexIntermediate");
    if (canonical.endsWith(QStringLiteral("IndexIntermediate"))) return canonical.left(canonical.size() - 17) + QStringLiteral("IndexDistal");
    if (canonical.endsWith(QStringLiteral("MiddleProximal"))) return canonical.left(canonical.size() - 14) + QStringLiteral("MiddleIntermediate");
    if (canonical.endsWith(QStringLiteral("MiddleIntermediate"))) return canonical.left(canonical.size() - 18) + QStringLiteral("MiddleDistal");
    if (canonical.endsWith(QStringLiteral("RingProximal"))) return canonical.left(canonical.size() - 12) + QStringLiteral("RingIntermediate");
    if (canonical.endsWith(QStringLiteral("RingIntermediate"))) return canonical.left(canonical.size() - 16) + QStringLiteral("RingDistal");
    if (canonical.endsWith(QStringLiteral("LittleProximal"))) return canonical.left(canonical.size() - 14) + QStringLiteral("LittleIntermediate");
    if (canonical.endsWith(QStringLiteral("LittleIntermediate"))) return canonical.left(canonical.size() - 18) + QStringLiteral("LittleDistal");
    return QString();
}

QString preferredParentCanonical(const QString& canonical) {
    if (canonical == QStringLiteral("Spine")) return QStringLiteral("Hips");
    if (canonical == QStringLiteral("Chest")) return QStringLiteral("Spine");
    if (canonical == QStringLiteral("Neck")) return QStringLiteral("Chest");
    if (canonical == QStringLiteral("Head")) return QStringLiteral("Neck");
    if (canonical.endsWith(QStringLiteral("LowerArm"))) return canonical.left(canonical.size() - 8) + QStringLiteral("UpperArm");
    if (canonical.endsWith(QStringLiteral("Hand"))) return canonical.left(canonical.size() - 4) + QStringLiteral("LowerArm");
    if (canonical.endsWith(QStringLiteral("LowerLeg"))) return canonical.left(canonical.size() - 8) + QStringLiteral("UpperLeg");
    if (canonical.endsWith(QStringLiteral("Foot"))) return canonical.left(canonical.size() - 4) + QStringLiteral("LowerLeg");
    if (canonical.endsWith(QStringLiteral("ThumbProximal"))) return canonical.left(canonical.size() - 13) + QStringLiteral("ThumbMetacarpal");
    if (canonical.endsWith(QStringLiteral("ThumbDistal"))) return canonical.left(canonical.size() - 11) + QStringLiteral("ThumbProximal");
    if (canonical.endsWith(QStringLiteral("IndexProximal"))) return canonical.left(canonical.size() - 13) + QStringLiteral("Hand");
    if (canonical.endsWith(QStringLiteral("IndexIntermediate"))) return canonical.left(canonical.size() - 17) + QStringLiteral("IndexProximal");
    if (canonical.endsWith(QStringLiteral("IndexDistal"))) return canonical.left(canonical.size() - 11) + QStringLiteral("IndexIntermediate");
    if (canonical.endsWith(QStringLiteral("MiddleProximal"))) return canonical.left(canonical.size() - 14) + QStringLiteral("Hand");
    if (canonical.endsWith(QStringLiteral("MiddleIntermediate"))) return canonical.left(canonical.size() - 18) + QStringLiteral("MiddleProximal");
    if (canonical.endsWith(QStringLiteral("MiddleDistal"))) return canonical.left(canonical.size() - 12) + QStringLiteral("MiddleIntermediate");
    if (canonical.endsWith(QStringLiteral("RingProximal"))) return canonical.left(canonical.size() - 12) + QStringLiteral("Hand");
    if (canonical.endsWith(QStringLiteral("RingIntermediate"))) return canonical.left(canonical.size() - 16) + QStringLiteral("RingProximal");
    if (canonical.endsWith(QStringLiteral("RingDistal"))) return canonical.left(canonical.size() - 10) + QStringLiteral("RingIntermediate");
    if (canonical.endsWith(QStringLiteral("LittleProximal"))) return canonical.left(canonical.size() - 14) + QStringLiteral("Hand");
    if (canonical.endsWith(QStringLiteral("LittleIntermediate"))) return canonical.left(canonical.size() - 18) + QStringLiteral("LittleProximal");
    if (canonical.endsWith(QStringLiteral("LittleDistal"))) return canonical.left(canonical.size() - 12) + QStringLiteral("LittleIntermediate");
    return QString();
}

std::vector<Eigen::Matrix4f> buildGlobalBindTransforms(const SceneModel& scene) {
    std::vector<Eigen::Matrix4f> globals(scene.nodes.size(), Eigen::Matrix4f::Identity());
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        const int parent = scene.nodes[i].parent_index;
        if (parent >= 0 && parent < static_cast<int>(globals.size())) {
            globals[i] = globals[parent] * scene.nodes[i].local_bind_transform;
        } else {
            globals[i] = scene.nodes[i].local_bind_transform;
        }
    }
    return globals;
}

Eigen::Quaternionf humanoidRestBasisRotation(const SceneModel& scene,
                                             const QHash<QString, int>& canonical_to_node) {
    const std::vector<Eigen::Matrix4f> globals = buildGlobalBindTransforms(scene);
    const auto node_position = [&globals](int node_index) {
        return nodePosition(globals, node_index);
    };
    const auto canonical_node = [&canonical_to_node](const QString& primary,
                                                     const QString& secondary,
                                                     const QString& tertiary) {
        int node = canonical_to_node.value(primary, -1);
        if (node < 0 && !secondary.isEmpty()) {
            node = canonical_to_node.value(secondary, -1);
        }
        if (node < 0 && !tertiary.isEmpty()) {
            node = canonical_to_node.value(tertiary, -1);
        }
        return node;
    };

    const int hips = canonical_node(QStringLiteral("Hips"), QString(), QString());
    const int upper_body = canonical_node(QStringLiteral("Head"),
                                          QStringLiteral("Chest"),
                                          QStringLiteral("Spine"));
    const int left_side = canonical_node(QStringLiteral("LeftUpperArm"),
                                         QStringLiteral("LeftShoulder"),
                                         QStringLiteral("LeftHand"));
    const int right_side = canonical_node(QStringLiteral("RightUpperArm"),
                                          QStringLiteral("RightShoulder"),
                                          QStringLiteral("RightHand"));
    if (hips < 0 || upper_body < 0 || left_side < 0 || right_side < 0) {
        return Eigen::Quaternionf::Identity();
    }

    Eigen::Vector3f up = node_position(upper_body) - node_position(hips);
    Eigen::Vector3f side = node_position(left_side) - node_position(right_side);
    if (up.squaredNorm() < 1e-8f || side.squaredNorm() < 1e-8f) {
        return Eigen::Quaternionf::Identity();
    }
    up.normalize();
    side = side - up * side.dot(up);
    if (side.squaredNorm() < 1e-8f) {
        return Eigen::Quaternionf::Identity();
    }
    side.normalize();
    Eigen::Vector3f forward = side.cross(up);
    if (forward.squaredNorm() < 1e-8f) {
        return Eigen::Quaternionf::Identity();
    }
    forward.normalize();
    up = forward.cross(side).normalized();

    Eigen::Matrix3f basis;
    basis.col(0) = side;
    basis.col(1) = up;
    basis.col(2) = forward;
    return Eigen::Quaternionf(basis).normalized();
}

Eigen::Quaternionf localBindRotation(const SceneModel& scene, int node_index) {
    if (node_index < 0 || node_index >= static_cast<int>(scene.nodes.size())) {
        return Eigen::Quaternionf::Identity();
    }
    Eigen::Matrix3f linear = scene.nodes[node_index].local_bind_transform.block<3, 3>(0, 0);
    for (int axis = 0; axis < 3; ++axis) {
        const float length = linear.col(axis).norm();
        if (length > 1e-8f) {
            linear.col(axis) /= length;
        }
    }
    return Eigen::Quaternionf(linear).normalized();
}

std::vector<Eigen::Quaternionf> buildGlobalBindRotations(const SceneModel& scene) {
    std::vector<Eigen::Quaternionf> globals(scene.nodes.size(), Eigen::Quaternionf::Identity());
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        const Eigen::Quaternionf local = localBindRotation(scene, i);
        const int parent = scene.nodes[i].parent_index;
        if (parent >= 0 && parent < static_cast<int>(globals.size())) {
            globals[i] = (globals[parent] * local).normalized();
        } else {
            globals[i] = local;
        }
    }
    return globals;
}

Eigen::Quaternionf semanticBasisFromDirectionAndReference(const Eigen::Vector3f& direction,
                                                          const Eigen::Vector3f& reference_direction) {
    Eigen::Vector3f y = direction;
    if (y.squaredNorm() < 1e-8f) {
        return Eigen::Quaternionf::Identity();
    }
    y.normalize();

    Eigen::Vector3f reference = reference_direction.squaredNorm() > 1e-8f
        ? reference_direction.normalized()
        : Eigen::Vector3f(0.0f, 1.0f, 0.0f);
    if (std::abs(y.dot(reference)) > 0.94f) {
        reference = Eigen::Vector3f(0.0f, 0.0f, 1.0f);
    }
    if (std::abs(y.dot(reference)) > 0.94f) {
        reference = Eigen::Vector3f(1.0f, 0.0f, 0.0f);
    }

    Eigen::Vector3f z = reference - y * y.dot(reference);
    if (z.squaredNorm() < 1e-8f) {
        return Eigen::Quaternionf::Identity();
    }
    z.normalize();
    Eigen::Vector3f x = y.cross(z);
    if (x.squaredNorm() < 1e-8f) {
        return Eigen::Quaternionf::Identity();
    }
    x.normalize();
    z = x.cross(y).normalized();

    Eigen::Matrix3f basis;
    basis.col(0) = x;
    basis.col(1) = y;
    basis.col(2) = z;
    return Eigen::Quaternionf(basis).normalized();
}

Eigen::Quaternionf semanticBasisFromDirection(const Eigen::Vector3f& direction,
                                              const QString& canonical_name) {
    Eigen::Vector3f reference = Eigen::Vector3f(0.0f, 1.0f, 0.0f);
    if (canonical_name.contains(QStringLiteral("Leg"), Qt::CaseInsensitive) ||
        canonical_name.contains(QStringLiteral("Foot"), Qt::CaseInsensitive) ||
        canonical_name == QStringLiteral("Hips") ||
        canonical_name == QStringLiteral("Spine") ||
        canonical_name == QStringLiteral("Chest") ||
        canonical_name == QStringLiteral("Neck") ||
        canonical_name == QStringLiteral("Head")) {
        reference = Eigen::Vector3f(0.0f, 0.0f, 1.0f);
    }
    return semanticBasisFromDirectionAndReference(direction, reference);
}

Eigen::Vector3f nodePosition(const std::vector<Eigen::Matrix4f>& globals, int node_index) {
    if (node_index < 0 || node_index >= static_cast<int>(globals.size())) {
        return Eigen::Vector3f::Zero();
    }
    return globals[node_index].block<3, 1>(0, 3);
}

Eigen::Vector3f palmNormalForSide(const QString& side,
                                  const QHash<QString, int>& canonical_to_node,
                                  const std::vector<Eigen::Matrix4f>& globals) {
    const int hand = canonical_to_node.value(side + QStringLiteral("Hand"), -1);
    const int index = canonical_to_node.value(side + QStringLiteral("IndexProximal"), -1);
    const int middle = canonical_to_node.value(side + QStringLiteral("MiddleProximal"), -1);
    const int little = canonical_to_node.value(side + QStringLiteral("LittleProximal"), -1);
    if (hand < 0 || index < 0 || middle < 0 || little < 0) {
        return Eigen::Vector3f::Zero();
    }

    const Eigen::Vector3f hand_position = nodePosition(globals, hand);
    Eigen::Vector3f finger_axis = nodePosition(globals, middle) - hand_position;
    Eigen::Vector3f spread_axis = nodePosition(globals, index) - nodePosition(globals, little);
    if (finger_axis.squaredNorm() < 1e-8f || spread_axis.squaredNorm() < 1e-8f) {
        return Eigen::Vector3f::Zero();
    }
    finger_axis.normalize();
    spread_axis.normalize();
    Eigen::Vector3f normal = finger_axis.cross(spread_axis);
    if (normal.squaredNorm() < 1e-8f) {
        return Eigen::Vector3f::Zero();
    }
    return normal.normalized();
}

std::vector<Eigen::Quaternionf> buildSemanticBindRotations(const SceneModel& scene,
                                                           const QHash<QString, int>& canonical_to_node) {
    std::vector<Eigen::Quaternionf> rotations = buildGlobalBindRotations(scene);
    const std::vector<Eigen::Matrix4f> globals = buildGlobalBindTransforms(scene);
    for (auto it = canonical_to_node.constBegin(); it != canonical_to_node.constEnd(); ++it) {
        const QString canonical = it.key();
        const int node = it.value();
        if (node < 0 || node >= static_cast<int>(globals.size())) {
            continue;
        }

        Eigen::Vector3f direction = Eigen::Vector3f::Zero();
        const QString child = preferredChildCanonical(canonical);
        const int child_node = canonical_to_node.value(child, -1);
        if (child_node >= 0 && child_node < static_cast<int>(globals.size())) {
            direction = globals[child_node].block<3, 1>(0, 3) - globals[node].block<3, 1>(0, 3);
        } else {
            const QString parent = preferredParentCanonical(canonical);
            const int parent_node = canonical_to_node.value(parent, -1);
            if (parent_node >= 0 && parent_node < static_cast<int>(globals.size())) {
                direction = globals[node].block<3, 1>(0, 3) - globals[parent_node].block<3, 1>(0, 3);
            }
        }

        if (direction.squaredNorm() > 1e-8f) {
            const QString side = sidePrefixForCanonical(canonical);
            const Eigen::Vector3f palm_normal = side.isEmpty()
                ? Eigen::Vector3f::Zero()
                : palmNormalForSide(side, canonical_to_node, globals);
            if ((canonical.endsWith(QStringLiteral("Hand")) || isFingerCanonicalName(canonical)) &&
                palm_normal.squaredNorm() > 1e-8f) {
                rotations[node] = semanticBasisFromDirectionAndReference(direction, palm_normal);
            } else {
                rotations[node] = semanticBasisFromDirection(direction, canonical);
            }
        }
    }
    return rotations;
}

Eigen::Vector3f localBindTranslation(const SceneModel& scene, int node_index) {
    if (node_index < 0 || node_index >= static_cast<int>(scene.nodes.size())) {
        return Eigen::Vector3f::Zero();
    }
    return scene.nodes[node_index].local_bind_transform.block<3, 1>(0, 3);
}

float skeletonHeight(const SceneModel& scene) {
    const std::vector<Eigen::Matrix4f> globals = buildGlobalBindTransforms(scene);
    if (globals.empty()) {
        return 1.0f;
    }
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();
    for (const Eigen::Matrix4f& transform : globals) {
        const float y = transform(1, 3);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }
    const float height = max_y - min_y;
    return height > 1e-4f ? height : 1.0f;
}

bool isHipsMapping(const BoneMapping& mapping) {
    return mapping.canonical_name.compare(QStringLiteral("hips"), Qt::CaseInsensitive) == 0 ||
           mapping.source_bone.contains(QStringLiteral("hip"), Qt::CaseInsensitive) ||
           mapping.target_bone.contains(QStringLiteral("hip"), Qt::CaseInsensitive);
}

bool isFingerCanonicalName(const QString& canonical_name) {
    return canonical_name.contains(QStringLiteral("Thumb"), Qt::CaseInsensitive) ||
           canonical_name.contains(QStringLiteral("Index"), Qt::CaseInsensitive) ||
           canonical_name.contains(QStringLiteral("Middle"), Qt::CaseInsensitive) ||
           canonical_name.contains(QStringLiteral("Ring"), Qt::CaseInsensitive) ||
           canonical_name.contains(QStringLiteral("Little"), Qt::CaseInsensitive);
}

bool shouldStabilizeUpperLimbCanonical(const QString& canonical_name) {
    return canonical_name.endsWith(QStringLiteral("Shoulder")) ||
           canonical_name.endsWith(QStringLiteral("Hand")) ||
           isFingerCanonicalName(canonical_name);
}

bool shouldSolveUpperLimbDirection(const QString& canonical_name) {
    return canonical_name.endsWith(QStringLiteral("UpperArm")) ||
           canonical_name.endsWith(QStringLiteral("LowerArm"));
}

bool isUpperLimbIkCanonical(const QString& canonical_name) {
    return canonical_name.endsWith(QStringLiteral("Shoulder")) ||
           canonical_name.endsWith(QStringLiteral("UpperArm")) ||
           canonical_name.endsWith(QStringLiteral("LowerArm")) ||
           canonical_name.endsWith(QStringLiteral("Hand"));
}

bool isEyeCanonical(const QString& canonical_name) {
    return canonical_name == QStringLiteral("Eye") ||
           canonical_name.endsWith(QStringLiteral("Eye"));
}

QString sidePrefixForCanonical(const QString& canonical_name) {
    if (canonical_name.startsWith(QStringLiteral("Left"))) {
        return QStringLiteral("Left");
    }
    if (canonical_name.startsWith(QStringLiteral("Right"))) {
        return QStringLiteral("Right");
    }
    return QString();
}

std::vector<VectorKeyframe> scaledPositions(const std::vector<VectorKeyframe>& source, float scale) {
    std::vector<VectorKeyframe> result = source;
    for (VectorKeyframe& key : result) {
        key.value *= scale;
    }
    return result;
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

std::vector<Eigen::Matrix4f> buildGlobalPoseTransforms(const SceneModel& scene,
                                                       const QHash<int, const AnimationChannelData*>& channels,
                                                       double time_ticks) {
    std::vector<Eigen::Matrix4f> locals(scene.nodes.size(), Eigen::Matrix4f::Identity());
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        TransformParts parts = decomposeTransform(scene.nodes[i].local_bind_transform);
        const AnimationChannelData* channel = channels.value(i, nullptr);
        if (channel) {
            parts.translation = sampleVectorKeys(channel->positions, time_ticks, parts.translation);
            parts.rotation = sampleRotationKeys(channel->rotations, time_ticks, parts.rotation);
            parts.scale = sampleVectorKeys(channel->scales, time_ticks, parts.scale);
        }
        locals[i] = composeTransform(parts);
    }

    std::vector<Eigen::Matrix4f> globals(scene.nodes.size(), Eigen::Matrix4f::Identity());
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        const int parent = scene.nodes[i].parent_index;
        if (parent >= 0 && parent < static_cast<int>(globals.size())) {
            globals[i] = globals[parent] * locals[i];
        } else {
            globals[i] = locals[i];
        }
    }
    return globals;
}

Eigen::Quaternionf rotationBetweenVectors(Eigen::Vector3f from, Eigen::Vector3f to) {
    if (from.squaredNorm() < 1e-8f || to.squaredNorm() < 1e-8f) {
        return Eigen::Quaternionf::Identity();
    }
    from.normalize();
    to.normalize();
    const float dot = std::clamp(from.dot(to), -1.0f, 1.0f);
    if (dot > 0.9995f) {
        return Eigen::Quaternionf::Identity();
    }
    if (dot < -0.9995f) {
        Eigen::Vector3f axis = from.cross(Eigen::Vector3f(0.0f, 1.0f, 0.0f));
        if (axis.squaredNorm() < 1e-8f) {
            axis = from.cross(Eigen::Vector3f(1.0f, 0.0f, 0.0f));
        }
        axis.normalize();
        return Eigen::Quaternionf(Eigen::AngleAxisf(3.14159265358979323846f, axis)).normalized();
    }
    Eigen::Vector3f axis = from.cross(to);
    axis.normalize();
    return Eigen::Quaternionf(Eigen::AngleAxisf(std::acos(dot), axis)).normalized();
}

Eigen::Quaternionf weightedLimitedRotation(Eigen::Quaternionf rotation,
                                           float weight,
                                           float max_angle_radians) {
    rotation.normalize();
    if (rotation.w() < 0.0f) {
        rotation.coeffs() *= -1.0f;
    }
    Eigen::AngleAxisf angle_axis(rotation);
    if (angle_axis.angle() < 1e-5f || angle_axis.axis().squaredNorm() < 1e-8f) {
        return Eigen::Quaternionf::Identity();
    }
    const float angle = std::min(angle_axis.angle() * std::clamp(weight, 0.0f, 1.0f), max_angle_radians);
    if (angle < 1e-5f) {
        return Eigen::Quaternionf::Identity();
    }
    return Eigen::Quaternionf(Eigen::AngleAxisf(angle, angle_axis.axis().normalized())).normalized();
}

Eigen::Matrix4f localTransformWithRotation(const SceneModel& scene,
                                           int node_index,
                                           const Eigen::Quaternionf& rotation) {
    TransformParts parts = decomposeTransform(scene.nodes[node_index].local_bind_transform);
    parts.rotation = rotation.normalized();
    return composeTransform(parts);
}

void updatePoseNode(const SceneModel& scene,
                    int node_index,
                    const Eigen::Quaternionf& local_rotation,
                    std::vector<Eigen::Quaternionf>* global_rotations,
                    std::vector<Eigen::Quaternionf>* local_rotations,
                    std::vector<Eigen::Matrix4f>* global_transforms) {
    if (!global_rotations || !local_rotations || !global_transforms ||
        node_index < 0 || node_index >= static_cast<int>(scene.nodes.size())) {
        return;
    }

    const int parent = scene.nodes[node_index].parent_index;
    const Eigen::Quaternionf parent_rotation =
        (parent >= 0 && parent < static_cast<int>(global_rotations->size()))
            ? (*global_rotations)[parent]
            : Eigen::Quaternionf::Identity();
    const Eigen::Matrix4f parent_transform =
        (parent >= 0 && parent < static_cast<int>(global_transforms->size()))
            ? (*global_transforms)[parent]
            : Eigen::Matrix4f::Identity();

    (*local_rotations)[node_index] = local_rotation.normalized();
    (*global_rotations)[node_index] = (parent_rotation * local_rotation).normalized();
    (*global_transforms)[node_index] =
        parent_transform * localTransformWithRotation(scene, node_index, local_rotation);
}

Eigen::Vector3f projectedPole(const Eigen::Vector3f& axis,
                              const Eigen::Vector3f& primary,
                              const Eigen::Vector3f& fallback) {
    if (axis.squaredNorm() < 1e-8f) {
        return Eigen::Vector3f(0.0f, 1.0f, 0.0f);
    }
    Eigen::Vector3f n = axis.normalized();
    Eigen::Vector3f pole = primary - n * primary.dot(n);
    if (pole.squaredNorm() < 1e-8f) {
        pole = fallback - n * fallback.dot(n);
    }
    if (pole.squaredNorm() < 1e-8f) {
        pole = n.cross(Eigen::Vector3f(0.0f, 1.0f, 0.0f));
    }
    if (pole.squaredNorm() < 1e-8f) {
        pole = n.cross(Eigen::Vector3f(1.0f, 0.0f, 0.0f));
    }
    return pole.squaredNorm() > 1e-8f ? pole.normalized() : Eigen::Vector3f(0.0f, 0.0f, 1.0f);
}

Eigen::Quaternionf rotationFromLocalSegmentAndPole(const Eigen::Vector3f& local_segment,
                                                   const Eigen::Vector3f& local_pole,
                                                   const Eigen::Vector3f& desired_segment,
                                                   const Eigen::Vector3f& desired_pole) {
    if (local_segment.squaredNorm() < 1e-8f || desired_segment.squaredNorm() < 1e-8f) {
        return Eigen::Quaternionf::Identity();
    }
    const Eigen::Quaternionf local_basis =
        semanticBasisFromDirectionAndReference(local_segment, local_pole);
    const Eigen::Quaternionf desired_basis =
        semanticBasisFromDirectionAndReference(desired_segment, desired_pole);
    return (desired_basis * local_basis.conjugate()).normalized();
}

float signedAngleAroundAxis(const Eigen::Vector3f& from,
                            const Eigen::Vector3f& to,
                            const Eigen::Vector3f& axis) {
    if (from.squaredNorm() < 1e-8f || to.squaredNorm() < 1e-8f || axis.squaredNorm() < 1e-8f) {
        return 0.0f;
    }
    Eigen::Vector3f n = axis.normalized();
    Eigen::Vector3f a = from - n * from.dot(n);
    Eigen::Vector3f b = to - n * to.dot(n);
    if (a.squaredNorm() < 1e-8f || b.squaredNorm() < 1e-8f) {
        return 0.0f;
    }
    a.normalize();
    b.normalize();
    return std::atan2(n.dot(a.cross(b)), std::clamp(a.dot(b), -1.0f, 1.0f));
}

float segmentLength(const std::vector<Eigen::Matrix4f>& globals, int a, int b) {
    if (a < 0 || b < 0 ||
        a >= static_cast<int>(globals.size()) ||
        b >= static_cast<int>(globals.size())) {
        return 0.0f;
    }
    return (nodePosition(globals, b) - nodePosition(globals, a)).norm();
}

Eigen::Vector3f localSegment(const std::vector<Eigen::Matrix4f>& globals,
                             const std::vector<Eigen::Quaternionf>& global_rotations,
                             int parent,
                             int child) {
    if (parent < 0 || child < 0 ||
        parent >= static_cast<int>(globals.size()) ||
        child >= static_cast<int>(globals.size()) ||
        parent >= static_cast<int>(global_rotations.size())) {
        return Eigen::Vector3f::Zero();
    }
    return global_rotations[parent].conjugate() *
           (nodePosition(globals, child) - nodePosition(globals, parent));
}

void refreshUpperLimbDescendants(const SceneModel& scene,
                                 int upper,
                                 int lower,
                                 int hand,
                                 std::vector<Eigen::Quaternionf>* global_rotations,
                                 std::vector<Eigen::Quaternionf>* local_rotations,
                                 std::vector<Eigen::Matrix4f>* global_transforms) {
    if (!global_rotations || !local_rotations || !global_transforms) {
        return;
    }
    if (upper >= 0 && upper < static_cast<int>(local_rotations->size())) {
        updatePoseNode(scene, upper, (*local_rotations)[upper], global_rotations, local_rotations, global_transforms);
    }
    if (lower >= 0 && lower < static_cast<int>(local_rotations->size())) {
        updatePoseNode(scene, lower, (*local_rotations)[lower], global_rotations, local_rotations, global_transforms);
    }
    if (hand >= 0 && hand < static_cast<int>(local_rotations->size())) {
        updatePoseNode(scene, hand, (*local_rotations)[hand], global_rotations, local_rotations, global_transforms);
    }
}

void compensateShoulderForSide(const QString& side,
                               const SceneModel& target_bind_scene,
                               const std::vector<Eigen::Matrix4f>& source_pose_transforms,
                               const std::vector<Eigen::Matrix4f>& source_bind_transforms,
                               const std::vector<Eigen::Matrix4f>& target_bind_transforms,
                               const Eigen::Quaternionf& source_rest_to_target_rest,
                               const QHash<QString, int>& source_canonical_to_node,
                               const QHash<QString, int>& target_canonical_to_node,
                               int target_upper,
                               int target_lower,
                               int target_hand,
                               std::vector<Eigen::Quaternionf>* target_pose_globals,
                               std::vector<Eigen::Quaternionf>* target_pose_locals,
                               std::vector<Eigen::Matrix4f>* target_pose_transforms) {
    if (!target_pose_globals || !target_pose_locals || !target_pose_transforms) {
        return;
    }

    const int source_shoulder = source_canonical_to_node.value(side + QStringLiteral("Shoulder"), -1);
    const int source_upper = source_canonical_to_node.value(side + QStringLiteral("UpperArm"), -1);
    const int source_hand = source_canonical_to_node.value(side + QStringLiteral("Hand"), -1);
    const int target_shoulder = target_canonical_to_node.value(side + QStringLiteral("Shoulder"), -1);
    if (source_hand < 0 || target_shoulder < 0 || target_upper < 0 ||
        source_hand >= static_cast<int>(source_pose_transforms.size()) ||
        target_shoulder >= static_cast<int>(target_bind_scene.nodes.size()) ||
        target_shoulder >= static_cast<int>(target_pose_globals->size()) ||
        target_shoulder >= static_cast<int>(target_pose_locals->size()) ||
        target_shoulder >= static_cast<int>(target_pose_transforms->size())) {
        return;
    }

    const int source_anchor =
        (source_shoulder >= 0 && source_shoulder < static_cast<int>(source_pose_transforms.size()))
            ? source_shoulder
            : source_upper;
    if (source_anchor < 0 || source_anchor >= static_cast<int>(source_pose_transforms.size()) ||
        source_anchor >= static_cast<int>(source_bind_transforms.size()) ||
        source_hand >= static_cast<int>(source_bind_transforms.size())) {
        return;
    }

    Eigen::Vector3f source_reach =
        nodePosition(source_pose_transforms, source_hand) -
        nodePosition(source_pose_transforms, source_anchor);
    Eigen::Vector3f source_bind_reach =
        nodePosition(source_bind_transforms, source_hand) -
        nodePosition(source_bind_transforms, source_anchor);
    if (source_reach.squaredNorm() < 1e-8f || source_bind_reach.squaredNorm() < 1e-8f) {
        return;
    }

    Eigen::Vector3f current_reach =
        nodePosition(*target_pose_transforms, target_hand) -
        nodePosition(*target_pose_transforms, target_shoulder);
    if (current_reach.squaredNorm() < 1e-8f &&
        target_hand >= 0 &&
        target_hand < static_cast<int>(target_bind_transforms.size()) &&
        target_shoulder < static_cast<int>(target_bind_transforms.size())) {
        current_reach =
            nodePosition(target_bind_transforms, target_hand) -
            nodePosition(target_bind_transforms, target_shoulder);
    }
    Eigen::Vector3f desired_reach = source_rest_to_target_rest * source_reach;
    if (current_reach.squaredNorm() < 1e-8f || desired_reach.squaredNorm() < 1e-8f) {
        return;
    }

    const float source_reach_ratio =
        std::clamp(source_reach.norm() / std::max(source_bind_reach.norm(), 1e-5f), 0.35f, 1.35f);
    const float reach_weight = source_reach_ratio > 0.72f ? 0.34f : 0.18f;
    constexpr float kMaxShoulderSwing = 0.436332312999f; // 25 degrees
    const Eigen::Quaternionf swing =
        weightedLimitedRotation(rotationBetweenVectors(current_reach, desired_reach),
                                reach_weight,
                                kMaxShoulderSwing);
    if (swing.isApprox(Eigen::Quaternionf::Identity(), 1e-5f)) {
        return;
    }

    const Eigen::Quaternionf shoulder_global = (swing * (*target_pose_globals)[target_shoulder]).normalized();
    const int shoulder_parent = target_bind_scene.nodes[target_shoulder].parent_index;
    const Eigen::Quaternionf shoulder_parent_global =
        (shoulder_parent >= 0 && shoulder_parent < static_cast<int>(target_pose_globals->size()))
            ? (*target_pose_globals)[shoulder_parent]
            : Eigen::Quaternionf::Identity();
    const Eigen::Quaternionf shoulder_local = (shoulder_parent_global.conjugate() * shoulder_global).normalized();
    updatePoseNode(target_bind_scene,
                   target_shoulder,
                   shoulder_local,
                   target_pose_globals,
                   target_pose_locals,
                   target_pose_transforms);
    refreshUpperLimbDescendants(target_bind_scene,
                                target_upper,
                                target_lower,
                                target_hand,
                                target_pose_globals,
                                target_pose_locals,
                                target_pose_transforms);
}

void solveUpperLimbIkForSide(const QString& side,
                             const SceneModel& source_animation,
                             const SceneModel& target_bind_scene,
                             const std::vector<Eigen::Matrix4f>& source_pose_transforms,
                             const std::vector<Eigen::Matrix4f>& source_bind_transforms,
                             const std::vector<Eigen::Matrix4f>& target_bind_transforms,
                             const std::vector<Eigen::Quaternionf>& target_bind_rotations,
                             const Eigen::Quaternionf& source_rest_to_target_rest,
                             const QHash<QString, int>& source_canonical_to_node,
                             const QHash<QString, int>& target_canonical_to_node,
                             std::vector<Eigen::Quaternionf>* target_pose_globals,
                             std::vector<Eigen::Quaternionf>* target_pose_locals,
                             std::vector<Eigen::Matrix4f>* target_pose_transforms) {
    Q_UNUSED(source_animation);
    const int source_upper = source_canonical_to_node.value(side + QStringLiteral("UpperArm"), -1);
    const int source_lower = source_canonical_to_node.value(side + QStringLiteral("LowerArm"), -1);
    const int source_hand = source_canonical_to_node.value(side + QStringLiteral("Hand"), -1);
    const int target_upper = target_canonical_to_node.value(side + QStringLiteral("UpperArm"), -1);
    const int target_lower = target_canonical_to_node.value(side + QStringLiteral("LowerArm"), -1);
    const int target_hand = target_canonical_to_node.value(side + QStringLiteral("Hand"), -1);
    if (source_upper < 0 || source_lower < 0 || source_hand < 0 ||
        target_upper < 0 || target_lower < 0 || target_hand < 0 ||
        source_upper >= static_cast<int>(source_pose_transforms.size()) ||
        source_lower >= static_cast<int>(source_pose_transforms.size()) ||
        source_hand >= static_cast<int>(source_pose_transforms.size()) ||
        target_upper >= static_cast<int>(target_bind_transforms.size()) ||
        target_lower >= static_cast<int>(target_bind_transforms.size()) ||
        target_hand >= static_cast<int>(target_bind_transforms.size()) ||
        target_upper >= static_cast<int>(target_bind_rotations.size()) ||
        target_lower >= static_cast<int>(target_bind_rotations.size()) ||
        target_hand >= static_cast<int>(target_bind_rotations.size()) ||
        !target_pose_globals || !target_pose_locals || !target_pose_transforms) {
        return;
    }

    const float source_upper_length = segmentLength(source_bind_transforms, source_upper, source_lower);
    const float source_lower_length = segmentLength(source_bind_transforms, source_lower, source_hand);
    const float target_upper_length = segmentLength(target_bind_transforms, target_upper, target_lower);
    const float target_lower_length = segmentLength(target_bind_transforms, target_lower, target_hand);
    if (source_upper_length <= 1e-5f || source_lower_length <= 1e-5f ||
        target_upper_length <= 1e-5f || target_lower_length <= 1e-5f) {
        return;
    }

    const Eigen::Vector3f source_root = nodePosition(source_pose_transforms, source_upper);
    const Eigen::Vector3f source_elbow = nodePosition(source_pose_transforms, source_lower);
    const Eigen::Vector3f source_wrist = nodePosition(source_pose_transforms, source_hand);
    Eigen::Vector3f source_wrist_vector = source_wrist - source_root;
    if (source_wrist_vector.squaredNorm() < 1e-8f) {
        source_wrist_vector =
            nodePosition(source_bind_transforms, source_hand) -
            nodePosition(source_bind_transforms, source_upper);
    }
    Eigen::Vector3f desired_axis = source_rest_to_target_rest * source_wrist_vector;
    if (desired_axis.squaredNorm() < 1e-8f) {
        desired_axis =
            nodePosition(target_bind_transforms, target_hand) -
            nodePosition(target_bind_transforms, target_upper);
    }
    if (desired_axis.squaredNorm() < 1e-8f) {
        return;
    }
    desired_axis.normalize();

    const float source_pose_distance = std::max(source_wrist_vector.norm(), 1e-5f);
    const float source_total_length = source_upper_length + source_lower_length;
    const float target_total_length = target_upper_length + target_lower_length;
    float desired_distance = target_total_length * std::clamp(source_pose_distance / source_total_length, 0.02f, 1.0f);
    const float min_distance = std::max(std::abs(target_upper_length - target_lower_length) + 1e-4f, target_total_length * 0.36f);
    const float max_distance = target_total_length - 1e-4f;
    desired_distance = std::clamp(desired_distance, min_distance, max_distance);

    Eigen::Vector3f source_pole =
        source_elbow - source_root -
        source_wrist_vector.normalized() * (source_elbow - source_root).dot(source_wrist_vector.normalized());
    Eigen::Vector3f source_bind_axis =
        nodePosition(source_bind_transforms, source_hand) -
        nodePosition(source_bind_transforms, source_upper);
    Eigen::Vector3f source_bind_pole =
        nodePosition(source_bind_transforms, source_lower) -
        nodePosition(source_bind_transforms, source_upper);
    if (source_bind_axis.squaredNorm() > 1e-8f) {
        source_bind_axis.normalize();
        source_bind_pole -= source_bind_axis * source_bind_pole.dot(source_bind_axis);
    }
    Eigen::Vector3f desired_pole =
        projectedPole(desired_axis,
                      source_rest_to_target_rest * source_pole,
                      source_rest_to_target_rest * source_bind_pole);
    const Eigen::Vector3f target_bind_axis =
        nodePosition(target_bind_transforms, target_hand) -
        nodePosition(target_bind_transforms, target_upper);
    const Eigen::Vector3f target_bind_pole =
        nodePosition(target_bind_transforms, target_lower) -
        nodePosition(target_bind_transforms, target_upper);
    desired_pole = projectedPole(desired_axis, desired_pole, target_bind_pole);

    compensateShoulderForSide(side,
                              target_bind_scene,
                              source_pose_transforms,
                              source_bind_transforms,
                              target_bind_transforms,
                              source_rest_to_target_rest,
                              source_canonical_to_node,
                              target_canonical_to_node,
                              target_upper,
                              target_lower,
                              target_hand,
                              target_pose_globals,
                              target_pose_locals,
                              target_pose_transforms);

    const Eigen::Vector3f target_root = nodePosition(*target_pose_transforms, target_upper);
    const float cos_angle =
        std::clamp((target_upper_length * target_upper_length + desired_distance * desired_distance -
                    target_lower_length * target_lower_length) /
                       (2.0f * target_upper_length * desired_distance),
                   -1.0f,
                   1.0f);
    const float along = target_upper_length * cos_angle;
    const float pole_height = std::sqrt(std::max(target_upper_length * target_upper_length - along * along, 0.0f));
    const Eigen::Vector3f target_elbow = target_root + desired_axis * along + desired_pole * pole_height;
    const Eigen::Vector3f target_wrist = target_root + desired_axis * desired_distance;
    const Eigen::Vector3f desired_upper_segment = target_elbow - target_root;
    const Eigen::Vector3f desired_lower_segment = target_wrist - target_elbow;
    if (desired_upper_segment.squaredNorm() < 1e-8f || desired_lower_segment.squaredNorm() < 1e-8f) {
        return;
    }

    const Eigen::Vector3f target_bind_pole_projected =
        projectedPole(target_bind_axis, target_bind_pole, desired_pole);
    const Eigen::Vector3f local_upper_segment =
        localSegment(target_bind_transforms, target_bind_rotations, target_upper, target_lower);
    const Eigen::Vector3f local_lower_segment =
        localSegment(target_bind_transforms, target_bind_rotations, target_lower, target_hand);
    const Eigen::Vector3f local_upper_pole =
        target_bind_rotations[target_upper].conjugate() * target_bind_pole_projected;
    const Eigen::Vector3f local_lower_pole =
        target_bind_rotations[target_lower].conjugate() * target_bind_pole_projected;

    const Eigen::Quaternionf upper_global =
        rotationFromLocalSegmentAndPole(local_upper_segment,
                                        local_upper_pole,
                                        desired_upper_segment,
                                        desired_pole);
    const int upper_parent = target_bind_scene.nodes[target_upper].parent_index;
    const Eigen::Quaternionf upper_parent_global =
        (upper_parent >= 0 && upper_parent < static_cast<int>(target_pose_globals->size()))
            ? (*target_pose_globals)[upper_parent]
            : Eigen::Quaternionf::Identity();
    const Eigen::Quaternionf upper_local = (upper_parent_global.conjugate() * upper_global).normalized();
    updatePoseNode(target_bind_scene, target_upper, upper_local, target_pose_globals, target_pose_locals, target_pose_transforms);

    const Eigen::Quaternionf lower_global =
        rotationFromLocalSegmentAndPole(local_lower_segment,
                                        local_lower_pole,
                                        desired_lower_segment,
                                        desired_pole);
    const Eigen::Quaternionf lower_parent_global = (*target_pose_globals)[target_bind_scene.nodes[target_lower].parent_index];
    const Eigen::Quaternionf lower_local = (lower_parent_global.conjugate() * lower_global).normalized();
    updatePoseNode(target_bind_scene, target_lower, lower_local, target_pose_globals, target_pose_locals, target_pose_transforms);

    const int hand_parent = target_bind_scene.nodes[target_hand].parent_index;
    const Eigen::Quaternionf hand_parent_global =
        (hand_parent >= 0 && hand_parent < static_cast<int>(target_pose_globals->size()))
            ? (*target_pose_globals)[hand_parent]
            : Eigen::Quaternionf::Identity();
    Eigen::Quaternionf hand_global = (hand_parent_global * localBindRotation(target_bind_scene, target_hand)).normalized();

    const Eigen::Vector3f source_palm = source_rest_to_target_rest *
        palmNormalForSide(side, source_canonical_to_node, source_pose_transforms);
    const Eigen::Vector3f target_bind_palm =
        palmNormalForSide(side, target_canonical_to_node, target_bind_transforms);
    if (source_palm.squaredNorm() > 1e-8f && target_bind_palm.squaredNorm() > 1e-8f) {
        const Eigen::Vector3f local_hand_palm =
            target_bind_rotations[target_hand].conjugate() * target_bind_palm;
        const Eigen::Vector3f current_palm = hand_global * local_hand_palm;
        const Eigen::Vector3f forearm_axis = desired_lower_segment.normalized();
        float twist = signedAngleAroundAxis(current_palm, source_palm, forearm_axis);
        constexpr float kMaxWristTwist = 1.0471975512f;
        twist *= 0.72f;
        twist = std::clamp(twist, -kMaxWristTwist, kMaxWristTwist);
        hand_global = (Eigen::Quaternionf(Eigen::AngleAxisf(twist, forearm_axis)) * hand_global).normalized();
    }
    const Eigen::Quaternionf hand_local = (hand_parent_global.conjugate() * hand_global).normalized();
    updatePoseNode(target_bind_scene, target_hand, hand_local, target_pose_globals, target_pose_locals, target_pose_transforms);
}

std::vector<Eigen::Quaternionf> buildGlobalPoseRotations(const SceneModel& scene,
                                                         const QHash<int, const AnimationChannelData*>& channels,
                                                         double time_ticks) {
    std::vector<Eigen::Quaternionf> locals(scene.nodes.size(), Eigen::Quaternionf::Identity());
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        locals[i] = localBindRotation(scene, i);
        const AnimationChannelData* channel = channels.value(i, nullptr);
        if (channel && !channel->rotations.empty()) {
            locals[i] = sampleRotationKeys(channel->rotations, time_ticks, locals[i]);
        }
    }

    std::vector<Eigen::Quaternionf> globals(scene.nodes.size(), Eigen::Quaternionf::Identity());
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        const int parent = scene.nodes[i].parent_index;
        if (parent >= 0 && parent < static_cast<int>(globals.size())) {
            globals[i] = (globals[parent] * locals[i]).normalized();
        } else {
            globals[i] = locals[i];
        }
    }
    return globals;
}

void addSampleTime(std::vector<double>* times, double time_ticks) {
    if (!times || !std::isfinite(time_ticks)) {
        return;
    }
    times->push_back(std::max(0.0, time_ticks));
}

std::vector<double> collectRotationSampleTimes(const AnimationClipData& source_clip,
                                               const QHash<int, const AnimationChannelData*>& source_channels,
                                               const QHash<int, int>& target_to_source) {
    std::vector<double> times;
    addSampleTime(&times, 0.0);
    if (source_clip.duration_ticks > 0.0) {
        addSampleTime(&times, source_clip.duration_ticks);
    }

    for (auto it = target_to_source.constBegin(); it != target_to_source.constEnd(); ++it) {
        const AnimationChannelData* channel = source_channels.value(it.value(), nullptr);
        if (!channel) {
            continue;
        }
        for (const RotationKeyframe& key : channel->rotations) {
            addSampleTime(&times, key.time_ticks);
        }
    }
    for (const AnimationChannelData& channel : source_clip.channels) {
        for (const RotationKeyframe& key : channel.rotations) {
            addSampleTime(&times, key.time_ticks);
        }
    }

    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) {
                    return std::abs(a - b) < 1e-5;
                }),
                times.end());
    return times;
}

QHash<int, std::vector<RotationKeyframe>> retargetGlobalRotationKeys(
    const SceneModel& source_animation,
    const SceneModel& target_bind_scene,
    const AnimationClipData& source_clip,
    const QHash<int, const AnimationChannelData*>& source_channels,
    const QHash<int, int>& target_to_source,
    const QHash<QString, int>& source_canonical_to_node,
    const QHash<QString, int>& target_canonical_to_node) {
    QHash<int, std::vector<RotationKeyframe>> result;
    if (target_to_source.isEmpty()) {
        return result;
    }

    const std::vector<double> sample_times = collectRotationSampleTimes(source_clip, source_channels, target_to_source);
    const std::vector<Eigen::Quaternionf> source_bind_globals = buildGlobalBindRotations(source_animation);
    const std::vector<Eigen::Quaternionf> target_bind_globals = buildGlobalBindRotations(target_bind_scene);
    const std::vector<Eigen::Matrix4f> source_bind_transforms = buildGlobalBindTransforms(source_animation);
    const std::vector<Eigen::Matrix4f> target_bind_transforms = buildGlobalBindTransforms(target_bind_scene);
    const std::vector<Eigen::Quaternionf> source_semantic_bind_globals =
        buildSemanticBindRotations(source_animation, source_canonical_to_node);
    const std::vector<Eigen::Quaternionf> target_semantic_bind_globals =
        buildSemanticBindRotations(target_bind_scene, target_canonical_to_node);
    const Eigen::Quaternionf source_rest_to_target_rest =
        (humanoidRestBasisRotation(target_bind_scene, target_canonical_to_node) *
         humanoidRestBasisRotation(source_animation, source_canonical_to_node).conjugate()).normalized();
    QHash<int, QString> target_node_to_canonical;
    for (auto it = target_canonical_to_node.constBegin(); it != target_canonical_to_node.constEnd(); ++it) {
        target_node_to_canonical.insert(it.value(), it.key());
    }
    const double reference_time_ticks = sample_times.empty() ? 0.0 : sample_times.front();
    const std::vector<Eigen::Quaternionf> source_reference_globals =
        buildGlobalPoseRotations(source_animation, source_channels, reference_time_ticks);

    for (auto it = target_to_source.constBegin(); it != target_to_source.constEnd(); ++it) {
        result.insert(it.key(), {});
    }
    for (auto it = target_canonical_to_node.constBegin(); it != target_canonical_to_node.constEnd(); ++it) {
        const QString canonical = it.key();
        const int target_node = it.value();
        const int source_node = source_canonical_to_node.value(canonical, -1);
        const QString child_canonical = preferredChildCanonical(canonical);
        if (isUpperLimbIkCanonical(canonical) &&
            source_node >= 0 &&
            source_channels.contains(source_node) &&
            (canonical.endsWith(QStringLiteral("Hand")) ||
             (source_canonical_to_node.contains(child_canonical) &&
              target_canonical_to_node.contains(child_canonical)))) {
            result.insert(target_node, {});
        }
    }

    for (double time_ticks : sample_times) {
        const std::vector<Eigen::Quaternionf> source_pose_globals =
            buildGlobalPoseRotations(source_animation, source_channels, time_ticks);
        const std::vector<Eigen::Matrix4f> source_pose_transforms =
            buildGlobalPoseTransforms(source_animation, source_channels, time_ticks);
        std::vector<Eigen::Quaternionf> target_pose_globals(target_bind_scene.nodes.size(), Eigen::Quaternionf::Identity());
        std::vector<Eigen::Quaternionf> target_pose_locals(target_bind_scene.nodes.size(), Eigen::Quaternionf::Identity());
        std::vector<Eigen::Matrix4f> target_pose_transforms(target_bind_scene.nodes.size(), Eigen::Matrix4f::Identity());

        for (int target_node = 0; target_node < static_cast<int>(target_bind_scene.nodes.size()); ++target_node) {
            const Eigen::Quaternionf target_bind_local = localBindRotation(target_bind_scene, target_node);
            const int parent = target_bind_scene.nodes[target_node].parent_index;
            const Eigen::Quaternionf parent_global =
                (parent >= 0 && parent < static_cast<int>(target_pose_globals.size()))
                    ? target_pose_globals[parent]
                    : Eigen::Quaternionf::Identity();

            Eigen::Quaternionf target_global = (parent_global * target_bind_local).normalized();
            const QString canonical = target_node_to_canonical.value(target_node);

            const int source_node = target_to_source.value(target_node, -1);
            const AnimationChannelData* source_channel = source_channels.value(source_node, nullptr);
            if (isEyeCanonical(canonical) &&
                source_node >= 0 &&
                source_node < static_cast<int>(source_bind_globals.size()) &&
                source_channel &&
                !source_channel->rotations.empty()) {
                const Eigen::Quaternionf source_bind_local = localBindRotation(source_animation, source_node);
                const Eigen::Quaternionf source_pose_local =
                    sampleRotationKeys(source_channel->rotations, time_ticks, source_bind_local);
                const Eigen::Quaternionf source_local_delta =
                    (source_bind_local.conjugate() * source_pose_local).normalized();
                const Eigen::Quaternionf target_bind_local = localBindRotation(target_bind_scene, target_node);
                const Eigen::Quaternionf target_local = (target_bind_local * source_local_delta).normalized();
                target_global = (parent_global * target_local).normalized();
            } else if (!isUpperLimbIkCanonical(canonical) &&
                source_node >= 0 &&
                source_node < static_cast<int>(source_bind_globals.size()) &&
                source_node < static_cast<int>(source_pose_globals.size()) &&
                target_node < static_cast<int>(target_bind_globals.size()) &&
                source_node < static_cast<int>(source_semantic_bind_globals.size()) &&
                target_node < static_cast<int>(target_semantic_bind_globals.size()) &&
                source_channel &&
                !source_channel->rotations.empty()) {
                const Eigen::Quaternionf source_raw_to_semantic =
                    (source_bind_globals[source_node].conjugate() * source_semantic_bind_globals[source_node]).normalized();
                const Eigen::Quaternionf source_semantic_reference =
                    (source_reference_globals[source_node] * source_raw_to_semantic).normalized();
                const Eigen::Quaternionf source_semantic_pose =
                    (source_pose_globals[source_node] * source_raw_to_semantic).normalized();
                const Eigen::Quaternionf source_delta =
                    (source_semantic_reference.conjugate() * source_semantic_pose).normalized();
                const Eigen::Quaternionf target_semantic_pose =
                    (target_semantic_bind_globals[target_node] * source_delta).normalized();
                const Eigen::Quaternionf target_raw_to_semantic =
                    (target_bind_globals[target_node].conjugate() * target_semantic_bind_globals[target_node]).normalized();
                target_global = (target_semantic_pose * target_raw_to_semantic.conjugate()).normalized();
            }

            target_pose_globals[target_node] = target_global;
            target_pose_locals[target_node] = (parent_global.conjugate() * target_global).normalized();
            const Eigen::Matrix4f parent_transform =
                (parent >= 0 && parent < static_cast<int>(target_pose_transforms.size()))
                    ? target_pose_transforms[parent]
                    : Eigen::Matrix4f::Identity();
            target_pose_transforms[target_node] =
                parent_transform * localTransformWithRotation(target_bind_scene, target_node, target_pose_locals[target_node]);
        }

        solveUpperLimbIkForSide(QStringLiteral("Left"),
                                source_animation,
                                target_bind_scene,
                                source_pose_transforms,
                                source_bind_transforms,
                                target_bind_transforms,
                                target_bind_globals,
                                source_rest_to_target_rest,
                                source_canonical_to_node,
                                target_canonical_to_node,
                                &target_pose_globals,
                                &target_pose_locals,
                                &target_pose_transforms);
        solveUpperLimbIkForSide(QStringLiteral("Right"),
                                source_animation,
                                target_bind_scene,
                                source_pose_transforms,
                                source_bind_transforms,
                                target_bind_transforms,
                                target_bind_globals,
                                source_rest_to_target_rest,
                                source_canonical_to_node,
                                target_canonical_to_node,
                                &target_pose_globals,
                                &target_pose_locals,
                                &target_pose_transforms);

        for (auto it = result.begin(); it != result.end(); ++it) {
            const int target_node = it.key();
            if (target_node >= 0 && target_node < static_cast<int>(target_pose_locals.size())) {
                it.value().push_back(RotationKeyframe{ time_ticks, target_pose_locals[target_node] });
            }
        }
    }

    return result;
}

std::vector<VectorKeyframe> retargetRootPositions(const std::vector<VectorKeyframe>& source,
                                                  const Eigen::Vector3f& source_rest,
                                                  const Eigen::Vector3f& target_rest,
                                                  float scale,
                                                  float max_vertical_offset) {
    std::vector<VectorKeyframe> result = source;
    const Eigen::Vector3f source_reference = source.empty() ? source_rest : source.front().value;
    for (VectorKeyframe& key : result) {
        Eigen::Vector3f offset = (key.value - source_reference) * scale;
        offset.x() = 0.0f;
        offset.z() = 0.0f;
        offset.y() = std::clamp(offset.y(), -max_vertical_offset, max_vertical_offset);
        key.value = target_rest + offset;
    }
    return result;
}

void stabilizeFootFloor(const SceneModel& target_bind_scene,
                        const QHash<QString, int>& target_canonical_to_node,
                        AnimationClipData* target_clip) {
    if (!target_clip || target_bind_scene.nodes.empty()) {
        return;
    }
    const int hips = target_canonical_to_node.value(QStringLiteral("Hips"), -1);
    const int left_foot = target_canonical_to_node.value(QStringLiteral("LeftFoot"), -1);
    const int right_foot = target_canonical_to_node.value(QStringLiteral("RightFoot"), -1);
    if (hips < 0 || (left_foot < 0 && right_foot < 0)) {
        return;
    }

    const std::vector<Eigen::Matrix4f> bind_globals = buildGlobalBindTransforms(target_bind_scene);
    float bind_floor = std::numeric_limits<float>::max();
    if (left_foot >= 0 && left_foot < static_cast<int>(bind_globals.size())) {
        bind_floor = std::min(bind_floor, nodePosition(bind_globals, left_foot).y());
    }
    if (right_foot >= 0 && right_foot < static_cast<int>(bind_globals.size())) {
        bind_floor = std::min(bind_floor, nodePosition(bind_globals, right_foot).y());
    }
    if (!std::isfinite(bind_floor)) {
        return;
    }

    const double duration = std::max(target_clip->duration_ticks, 1.0);
    const int sample_count = 72;
    QHash<int, const AnimationChannelData*> channel_index = buildChannelIndex(*target_clip);
    std::vector<VectorKeyframe> corrected_positions;
    corrected_positions.reserve(sample_count);
    double correction_sum = 0.0;

    const AnimationChannelData* hips_channel = channel_index.value(hips, nullptr);
    const Eigen::Vector3f hips_bind_translation = localBindTranslation(target_bind_scene, hips);
    const float target_height = skeletonHeight(target_bind_scene);
    const float max_correction = std::max(target_height * 1.25f, 0.35f);
    for (int i = 0; i < sample_count; ++i) {
        const double time_ticks = duration * static_cast<double>(i) / static_cast<double>(std::max(1, sample_count - 1));
        const std::vector<Eigen::Matrix4f> globals = buildGlobalPoseTransforms(target_bind_scene, channel_index, time_ticks);
        float lowest = std::numeric_limits<float>::max();
        if (left_foot >= 0 && left_foot < static_cast<int>(globals.size())) {
            lowest = std::min(lowest, nodePosition(globals, left_foot).y());
        }
        if (right_foot >= 0 && right_foot < static_cast<int>(globals.size())) {
            lowest = std::min(lowest, nodePosition(globals, right_foot).y());
        }
        if (!std::isfinite(lowest)) {
            continue;
        }
        const float correction = std::clamp(std::max(0.0f, bind_floor - lowest), 0.0f, max_correction);
        correction_sum += std::abs(correction);
        Eigen::Vector3f hips_position = hips_bind_translation;
        if (hips_channel) {
            hips_position = sampleVectorKeys(hips_channel->positions, time_ticks, hips_position);
        }
        hips_position.y() += correction;
        corrected_positions.push_back(VectorKeyframe{ time_ticks, hips_position });
    }

    const double average_correction = corrected_positions.empty()
        ? 0.0
        : correction_sum / static_cast<double>(corrected_positions.size());
    if (average_correction < std::max(target_height * 0.015f, 0.01f)) {
        return;
    }

    AnimationChannelData* hips_output = mutableChannelForNode(target_clip, hips);
    if (hips_output) {
        hips_output->positions = std::move(corrected_positions);
    }
}

} // namespace

bool retargetAnimationToTarget(const SceneModel& source_animation,
                               const SceneModel& target_bind_scene,
                               const BoneMappingResult& mapping_result,
                               SceneModel* output_scene,
                               QString* error_message) {
    if (!output_scene) {
        return false;
    }
    if (source_animation.animations.empty() || source_animation.nodes.empty()) {
        if (error_message) {
            *error_message = QStringLiteral("Source animation has no readable skeleton animation.");
        }
        return false;
    }
    if (target_bind_scene.meshes.empty() || target_bind_scene.nodes.empty()) {
        if (error_message) {
            *error_message = QStringLiteral("Target character has no skinned scene to preview.");
        }
        return false;
    }
    if (mapping_result.mappings.empty()) {
        if (error_message) {
            *error_message = QStringLiteral("No bone mapping is available for retarget preview.");
        }
        return false;
    }

    const AnimationClipData& source_clip = source_animation.animations.front();
    const QHash<QString, int> source_nodes = buildNodeIndex(source_animation);
    const QHash<QString, int> target_nodes = buildNodeIndex(target_bind_scene);
    const QHash<int, const AnimationChannelData*> source_channels = buildChannelIndex(source_clip);
    const float target_height = skeletonHeight(target_bind_scene);
    const float translation_scale = std::clamp(target_height / skeletonHeight(source_animation), 0.001f, 100.0f);
    const float root_vertical_limit = std::max(target_height * 0.35f, 0.20f);
    QHash<int, int> target_to_source;
    QHash<QString, int> source_canonical_to_node;
    QHash<QString, int> target_canonical_to_node;

    AnimationClipData target_clip;
    target_clip.name = source_clip.name.empty() ? "Retargeted Animation" : source_clip.name + " retargeted";
    target_clip.duration_ticks = source_clip.duration_ticks;
    target_clip.ticks_per_second = source_clip.ticks_per_second > 0.0 ? source_clip.ticks_per_second : 1.0;
    target_clip.expression_channels = source_clip.expression_channels;

    for (const BoneMapping& mapping : mapping_result.mappings) {
        const int source_node = source_nodes.value(mapping.source_bone, -1);
        const int target_node = target_nodes.value(mapping.target_bone, -1);
        if (source_node < 0 || target_node < 0) {
            continue;
        }
        if (!mapping.canonical_name.isEmpty()) {
            source_canonical_to_node.insert(mapping.canonical_name, source_node);
            target_canonical_to_node.insert(mapping.canonical_name, target_node);
        }
        if (!source_channels.contains(source_node) ||
            shouldStabilizeUpperLimbCanonical(mapping.canonical_name) ||
            isEyeCanonical(mapping.canonical_name)) {
            continue;
        }
        target_to_source.insert(target_node, source_node);
    }

    const QHash<int, std::vector<RotationKeyframe>> retargeted_rotations =
        retargetGlobalRotationKeys(source_animation,
                                   target_bind_scene,
                                   source_clip,
                                   source_channels,
                                   target_to_source,
                                   source_canonical_to_node,
                                   target_canonical_to_node);

    for (const BoneMapping& mapping : mapping_result.mappings) {
        const int source_node = source_nodes.value(mapping.source_bone, -1);
        const int target_node = target_nodes.value(mapping.target_bone, -1);
        if (source_node < 0 || target_node < 0 || !source_channels.contains(source_node)) {
            continue;
        }
        const AnimationChannelData* source_channel = source_channels.value(source_node);
        AnimationChannelData target_channel;
        target_channel.node_index = target_node;
        target_channel.rotations = retargeted_rotations.value(target_node);
        if (isHipsMapping(mapping)) {
            target_channel.positions = retargetRootPositions(source_channel->positions,
                                                             localBindTranslation(source_animation, source_node),
                                                             localBindTranslation(target_bind_scene, target_node),
                                                             translation_scale,
                                                             root_vertical_limit);
        }
        if (!target_channel.rotations.empty() || !target_channel.positions.empty()) {
            target_clip.channels.push_back(std::move(target_channel));
        }
    }

    if (target_clip.channels.empty() && target_clip.expression_channels.empty()) {
        if (error_message) {
            *error_message = QStringLiteral("Retargeting produced no animation channels. Check the bone mapping table.");
        }
        return false;
    }

    stabilizeFootFloor(target_bind_scene, target_canonical_to_node, &target_clip);

    *output_scene = target_bind_scene;
    output_scene->animations.clear();
    output_scene->animations.push_back(std::move(target_clip));
    output_scene->lock_animation_to_bind_floor = true;
    if (error_message) {
        error_message->clear();
    }
    return true;
}

} // namespace haorendergi
