#include "rigging/retarget_quality.h"

#include <QHash>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace haorendergi {
namespace {

struct TransformParts {
    Eigen::Vector3f translation = Eigen::Vector3f::Zero();
    Eigen::Quaternionf rotation = Eigen::Quaternionf::Identity();
    Eigen::Vector3f scale = Eigen::Vector3f::Ones();
};

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

QHash<QString, int> buildCanonicalIndex(const SceneModel& scene,
                                        const BoneMappingResult& mapping_result,
                                        bool target_side) {
    const QHash<QString, int> nodes = buildNodeIndex(scene);
    QHash<QString, int> index;
    for (const BoneMapping& mapping : mapping_result.mappings) {
        const int node = nodes.value(target_side ? mapping.target_bone : mapping.source_bone, -1);
        if (node >= 0 && !mapping.canonical_name.isEmpty() && !index.contains(mapping.canonical_name)) {
            index.insert(mapping.canonical_name, node);
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

std::vector<Eigen::Matrix4f> globalBindTransforms(const SceneModel& scene) {
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

std::vector<Eigen::Matrix4f> globalPoseTransforms(const SceneModel& scene,
                                                  const AnimationClipData& clip,
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

Eigen::Vector3f positionOf(const std::vector<Eigen::Matrix4f>& globals, int node) {
    if (node < 0 || node >= static_cast<int>(globals.size())) {
        return Eigen::Vector3f::Zero();
    }
    return globals[node].block<3, 1>(0, 3);
}

Eigen::Quaternionf humanoidRestBasisRotation(const SceneModel& scene,
                                             const QHash<QString, int>& canonical) {
    const std::vector<Eigen::Matrix4f> globals = globalBindTransforms(scene);
    const auto canonicalNode = [&canonical](const QString& primary,
                                            const QString& secondary,
                                            const QString& tertiary) {
        int node = canonical.value(primary, -1);
        if (node < 0 && !secondary.isEmpty()) {
            node = canonical.value(secondary, -1);
        }
        if (node < 0 && !tertiary.isEmpty()) {
            node = canonical.value(tertiary, -1);
        }
        return node;
    };

    const int hips = canonicalNode(QStringLiteral("Hips"), QString(), QString());
    const int upper_body = canonicalNode(QStringLiteral("Head"), QStringLiteral("Chest"), QStringLiteral("Spine"));
    const int left_side = canonicalNode(QStringLiteral("LeftUpperArm"), QStringLiteral("LeftShoulder"), QStringLiteral("LeftHand"));
    const int right_side = canonicalNode(QStringLiteral("RightUpperArm"), QStringLiteral("RightShoulder"), QStringLiteral("RightHand"));
    if (hips < 0 || upper_body < 0 || left_side < 0 || right_side < 0 ||
        hips >= static_cast<int>(globals.size()) ||
        upper_body >= static_cast<int>(globals.size()) ||
        left_side >= static_cast<int>(globals.size()) ||
        right_side >= static_cast<int>(globals.size())) {
        return Eigen::Quaternionf::Identity();
    }

    Eigen::Vector3f up = positionOf(globals, upper_body) - positionOf(globals, hips);
    Eigen::Vector3f side = positionOf(globals, left_side) - positionOf(globals, right_side);
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

float skeletonHeight(const SceneModel& scene) {
    const std::vector<Eigen::Matrix4f> globals = globalBindTransforms(scene);
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();
    for (const Eigen::Matrix4f& transform : globals) {
        min_y = std::min(min_y, transform(1, 3));
        max_y = std::max(max_y, transform(1, 3));
    }
    const float height = max_y - min_y;
    return height > 1e-4f ? height : 1.0f;
}

void addIssue(RetargetQualityReport* report,
              RetargetQualitySeverity severity,
              const QString& code,
              const QString& message,
              double value,
              double penalty) {
    if (!report) {
        return;
    }
    RetargetQualityIssue issue;
    issue.severity = severity;
    issue.code = code;
    issue.message = message;
    issue.value = value;
    report->issues.push_back(issue);
    report->score = std::max(0.0, report->score - penalty);
}

QString gradeForScore(double score) {
    if (score >= 85.0) return QStringLiteral("A");
    if (score >= 70.0) return QStringLiteral("B");
    if (score >= 55.0) return QStringLiteral("C");
    if (score >= 40.0) return QStringLiteral("D");
    return QStringLiteral("F");
}

bool hasRotationChannel(const QHash<int, const AnimationChannelData*>& channels, int node) {
    const AnimationChannelData* channel = channels.value(node, nullptr);
    return channel && !channel->rotations.empty();
}

Eigen::Vector3f palmNormal(const QString& side,
                           const QHash<QString, int>& canonical,
                           const std::vector<Eigen::Matrix4f>& globals) {
    const int hand = canonical.value(side + QStringLiteral("Hand"), -1);
    const int index = canonical.value(side + QStringLiteral("IndexProximal"), -1);
    const int middle = canonical.value(side + QStringLiteral("MiddleProximal"), -1);
    const int little = canonical.value(side + QStringLiteral("LittleProximal"), -1);
    if (hand < 0 || index < 0 || middle < 0 || little < 0) {
        return Eigen::Vector3f::Zero();
    }
    const Eigen::Vector3f hand_pos = positionOf(globals, hand);
    Eigen::Vector3f finger_axis = positionOf(globals, middle) - hand_pos;
    Eigen::Vector3f spread_axis = positionOf(globals, index) - positionOf(globals, little);
    if (finger_axis.squaredNorm() < 1e-8f || spread_axis.squaredNorm() < 1e-8f) {
        return Eigen::Vector3f::Zero();
    }
    Eigen::Vector3f normal = finger_axis.normalized().cross(spread_axis.normalized());
    return normal.squaredNorm() > 1e-8f ? normal.normalized() : Eigen::Vector3f::Zero();
}

void evaluateMissingMajorChannels(const QHash<QString, int>& canonical,
                                  const QHash<int, const AnimationChannelData*>& channels,
                                  RetargetQualityReport* report) {
    static const QStringList major = {
        QStringLiteral("Hips"), QStringLiteral("Spine"), QStringLiteral("Chest"), QStringLiteral("Head"),
        QStringLiteral("LeftUpperArm"), QStringLiteral("LeftLowerArm"), QStringLiteral("LeftHand"),
        QStringLiteral("RightUpperArm"), QStringLiteral("RightLowerArm"), QStringLiteral("RightHand"),
        QStringLiteral("LeftUpperLeg"), QStringLiteral("LeftLowerLeg"), QStringLiteral("LeftFoot"),
        QStringLiteral("RightUpperLeg"), QStringLiteral("RightLowerLeg"), QStringLiteral("RightFoot")
    };
    QStringList missing;
    for (const QString& name : major) {
        const int node = canonical.value(name, -1);
        if (node >= 0 && !hasRotationChannel(channels, node)) {
            missing << name;
        }
    }
    report->missing_major_channels = missing.size();
    if (!missing.isEmpty()) {
        const double penalty = std::min(30.0, 2.0 * static_cast<double>(missing.size()));
        addIssue(report,
                 missing.size() > 6 ? RetargetQualitySeverity::Error : RetargetQualitySeverity::Warning,
                 QStringLiteral("missing_major_channels"),
                 QStringLiteral("Missing retargeted rotation channels: %1.").arg(missing.join(QStringLiteral(", "))),
                 missing.size(),
                 penalty);
    }
}

void evaluateRootMotion(const QHash<QString, int>& canonical,
                        const QHash<int, const AnimationChannelData*>& channels,
                        float height,
                        RetargetQualityReport* report) {
    const int hips = canonical.value(QStringLiteral("Hips"), -1);
    const AnimationChannelData* channel = channels.value(hips, nullptr);
    if (!channel || channel->positions.empty()) {
        return;
    }
    Eigen::Vector3f min_v = Eigen::Vector3f::Constant(std::numeric_limits<float>::max());
    Eigen::Vector3f max_v = Eigen::Vector3f::Constant(std::numeric_limits<float>::lowest());
    for (const VectorKeyframe& key : channel->positions) {
        min_v = min_v.cwiseMin(key.value);
        max_v = max_v.cwiseMax(key.value);
    }
    const Eigen::Vector3f range = max_v - min_v;
    const double horizontal = std::sqrt(range.x() * range.x() + range.z() * range.z()) / std::max(height, 1e-4f);
    const double vertical = range.y() / std::max(height, 1e-4f);
    report->root_motion_ratio = std::max(horizontal, vertical);
    if (horizontal > 0.45 || vertical > 0.40) {
        addIssue(report,
                 RetargetQualitySeverity::Warning,
                 QStringLiteral("root_motion_excess"),
                 QStringLiteral("Root/Hips position range is large: horizontal=%1 height, vertical=%2 height.")
                     .arg(horizontal, 0, 'f', 3)
                     .arg(vertical, 0, 'f', 3),
                 report->root_motion_ratio,
                 std::min(18.0, report->root_motion_ratio * 18.0));
    }
}

void evaluateEyeMapping(const BoneMappingResult& mapping_result, RetargetQualityReport* report) {
    for (const BoneMapping& mapping : mapping_result.mappings) {
        const bool left_eye = mapping.canonical_name == QStringLiteral("LeftEye");
        const bool right_eye = mapping.canonical_name == QStringLiteral("RightEye");
        if (!left_eye && !right_eye) {
            continue;
        }
        const QString source_lower = mapping.source_bone.toLower();
        const QString target_lower = mapping.target_bone.toLower();
        const bool source_wrong = (left_eye && source_lower.contains(QStringLiteral("_r"))) ||
            (left_eye && source_lower.contains(QStringLiteral("right"))) ||
            (right_eye && source_lower.contains(QStringLiteral("_l"))) ||
            (right_eye && source_lower.contains(QStringLiteral("left")));
        const bool target_wrong = (left_eye && target_lower.contains(QStringLiteral("_r"))) ||
            (left_eye && target_lower.contains(QStringLiteral("right"))) ||
            (right_eye && target_lower.contains(QStringLiteral("_l"))) ||
            (right_eye && target_lower.contains(QStringLiteral("left")));
        if (source_wrong || target_wrong) {
            ++report->eye_reverse_count;
            addIssue(report,
                     RetargetQualitySeverity::Error,
                     QStringLiteral("eye_side_reversed"),
                     QStringLiteral("%1 may be mapped to the wrong eye side: %2 -> %3.")
                         .arg(mapping.canonical_name, mapping.source_bone, mapping.target_bone),
                     1.0,
                     15.0);
        }
    }
}

int canonicalNode(const QHash<QString, int>& canonical,
                  const QString& primary,
                  const QString& secondary = QString(),
                  const QString& tertiary = QString()) {
    int node = canonical.value(primary, -1);
    if (node < 0 && !secondary.isEmpty()) {
        node = canonical.value(secondary, -1);
    }
    if (node < 0 && !tertiary.isEmpty()) {
        node = canonical.value(tertiary, -1);
    }
    return node;
}

bool eyeOnExpectedSide(const SceneModel& scene,
                       const QHash<QString, int>& canonical,
                       const QString& side,
                       float height,
                       double* side_value) {
    const int head = canonicalNode(canonical, QStringLiteral("Head"), QStringLiteral("Neck"), QStringLiteral("Chest"));
    const int eye = canonical.value(side + QStringLiteral("Eye"), -1);
    const std::vector<Eigen::Matrix4f> globals = globalBindTransforms(scene);
    if (head < 0 || eye < 0 ||
        head >= static_cast<int>(globals.size()) ||
        eye >= static_cast<int>(globals.size())) {
        return true;
    }

    const Eigen::Vector3f side_axis =
        (humanoidRestBasisRotation(scene, canonical) * Eigen::Vector3f(1.0f, 0.0f, 0.0f)).normalized();
    const double projected_side =
        static_cast<double>((positionOf(globals, eye) - positionOf(globals, head)).dot(side_axis));
    if (side_value) {
        *side_value = projected_side;
    }

    const double dead_zone = std::max(static_cast<double>(height) * 0.004, 1e-4);
    if (std::abs(projected_side) <= dead_zone) {
        return true;
    }
    return side == QStringLiteral("Left") ? projected_side > 0.0 : projected_side < 0.0;
}

void evaluateEyeSideGeometry(const SceneModel& source_animation,
                             const SceneModel& target_bind_scene,
                             const QHash<QString, int>& source_canonical,
                             const QHash<QString, int>& target_canonical,
                             float target_height,
                             RetargetQualityReport* report) {
    for (const QString& side : { QStringLiteral("Left"), QStringLiteral("Right") }) {
        double source_side = 0.0;
        double target_side = 0.0;
        const bool source_ok = source_animation.nodes.empty()
            ? true
            : eyeOnExpectedSide(source_animation, source_canonical, side, target_height, &source_side);
        const bool target_ok = eyeOnExpectedSide(target_bind_scene, target_canonical, side, target_height, &target_side);
        if (!source_ok || !target_ok) {
            ++report->eye_reverse_count;
            addIssue(report,
                     RetargetQualitySeverity::Error,
                     QStringLiteral("eye_side_reversed"),
                     QStringLiteral("%1Eye appears on the wrong side of the head: sourceSide=%2, targetSide=%3.")
                         .arg(side)
                         .arg(source_side, 0, 'f', 4)
                         .arg(target_side, 0, 'f', 4),
                     1.0,
                     15.0);
        }
    }
}

} // namespace

RetargetQualityReport scoreRetargetedAnimation(const SceneModel& target_bind_scene,
                                               const SceneModel& retargeted_scene,
                                               const BoneMappingResult& mapping_result) {
    return scoreRetargetedAnimation(SceneModel(), target_bind_scene, retargeted_scene, mapping_result);
}

RetargetQualityReport scoreRetargetedAnimation(const SceneModel& source_animation,
                                               const SceneModel& target_bind_scene,
                                               const SceneModel& retargeted_scene,
                                               const BoneMappingResult& mapping_result) {
    RetargetQualityReport report;
    if (retargeted_scene.animations.empty()) {
        addIssue(&report,
                 RetargetQualitySeverity::Error,
                 QStringLiteral("no_animation"),
                 QStringLiteral("Retargeted scene has no animation clip."),
                 1.0,
                 100.0);
        report.grade = gradeForScore(report.score);
        return report;
    }

    const AnimationClipData& clip = retargeted_scene.animations.front();
    const QHash<int, const AnimationChannelData*> channels = buildChannelIndex(clip);
    const QHash<QString, int> canonical = buildCanonicalIndex(retargeted_scene, mapping_result, true);
    const float height = skeletonHeight(target_bind_scene);
    const std::vector<Eigen::Matrix4f> bind_globals = globalBindTransforms(target_bind_scene);
    const bool has_source_pose = !source_animation.nodes.empty() && !source_animation.animations.empty();
    const AnimationClipData* source_clip = has_source_pose ? &source_animation.animations.front() : nullptr;
    const QHash<int, const AnimationChannelData*> source_channels =
        source_clip ? buildChannelIndex(*source_clip) : QHash<int, const AnimationChannelData*>();
    const QHash<QString, int> source_canonical =
        has_source_pose ? buildCanonicalIndex(source_animation, mapping_result, false) : QHash<QString, int>();
    const Eigen::Quaternionf source_rest_to_target_rest =
        has_source_pose
            ? (humanoidRestBasisRotation(target_bind_scene, canonical) *
               humanoidRestBasisRotation(source_animation, source_canonical).conjugate()).normalized()
            : Eigen::Quaternionf::Identity();

    evaluateMissingMajorChannels(canonical, channels, &report);
    evaluateRootMotion(canonical, channels, height, &report);
    evaluateEyeMapping(mapping_result, &report);
    evaluateEyeSideGeometry(source_animation, target_bind_scene, source_canonical, canonical, height, &report);

    const int left_foot = canonical.value(QStringLiteral("LeftFoot"), -1);
    const int right_foot = canonical.value(QStringLiteral("RightFoot"), -1);
    float bind_floor = std::numeric_limits<float>::max();
    if (left_foot >= 0) bind_floor = std::min(bind_floor, positionOf(bind_globals, left_foot).y());
    if (right_foot >= 0) bind_floor = std::min(bind_floor, positionOf(bind_globals, right_foot).y());
    if (!std::isfinite(bind_floor)) {
        bind_floor = target_bind_scene.bounds.valid() ? target_bind_scene.bounds.min.y() : 0.0f;
    }

    const int sample_count = 36;
    const double duration = std::max(clip.duration_ticks, 1.0);
    double foot_float_sum = 0.0;
    double foot_lowest_sum = 0.0;
    int foot_samples = 0;
    int foot_high_samples = 0;
    double min_shoulder_ratio = 1.0;
    int shoulder_samples = 0;
    int shoulder_collapsed_samples = 0;
    QHash<QString, Eigen::Vector3f> previous_palm;
    QHash<QString, Eigen::Quaternionf> previous_hand_rotation;
    int wrist_expected_samples = 0;
    int wrist_expected_reversed_samples = 0;

    const auto bindReach = [&](const QString& side) {
        const int shoulder = canonical.value(side + QStringLiteral("Shoulder"), -1);
        const int hand = canonical.value(side + QStringLiteral("Hand"), -1);
        const int upper = canonical.value(side + QStringLiteral("UpperArm"), -1);
        const int anchor = shoulder >= 0 ? shoulder : upper;
        if (anchor < 0 || hand < 0) {
            return 0.0f;
        }
        return (positionOf(bind_globals, hand) - positionOf(bind_globals, anchor)).norm();
    };
    const float left_bind_reach = bindReach(QStringLiteral("Left"));
    const float right_bind_reach = bindReach(QStringLiteral("Right"));

    for (int i = 0; i < sample_count; ++i) {
        const double t = duration * static_cast<double>(i) / static_cast<double>(std::max(1, sample_count - 1));
        const std::vector<Eigen::Matrix4f> globals = globalPoseTransforms(retargeted_scene, clip, channels, t);
        const std::vector<Eigen::Matrix4f> source_globals =
            source_clip ? globalPoseTransforms(source_animation,
                                               *source_clip,
                                               source_channels,
                                               std::min(t, std::max(source_clip->duration_ticks, 1.0)))
                        : std::vector<Eigen::Matrix4f>();
        if (left_foot >= 0 || right_foot >= 0) {
            float lowest = std::numeric_limits<float>::max();
            if (left_foot >= 0) lowest = std::min(lowest, positionOf(globals, left_foot).y());
            if (right_foot >= 0) lowest = std::min(lowest, positionOf(globals, right_foot).y());
            const double floating = std::max(0.0, static_cast<double>(lowest - bind_floor));
            foot_float_sum += floating;
            foot_lowest_sum += lowest;
            ++foot_samples;
            if (floating / std::max(static_cast<double>(height), 1e-4) > 0.055) {
                ++foot_high_samples;
            }
        }

        for (const QString& side : { QStringLiteral("Left"), QStringLiteral("Right") }) {
            const int shoulder = canonical.value(side + QStringLiteral("Shoulder"), -1);
            const int upper = canonical.value(side + QStringLiteral("UpperArm"), -1);
            const int hand = canonical.value(side + QStringLiteral("Hand"), -1);
            const int anchor = shoulder >= 0 ? shoulder : upper;
            const float bind_reach = side == QStringLiteral("Left") ? left_bind_reach : right_bind_reach;
            if (anchor >= 0 && hand >= 0 && bind_reach > 1e-5f) {
                const double ratio = (positionOf(globals, hand) - positionOf(globals, anchor)).norm() / bind_reach;
                min_shoulder_ratio = std::min(min_shoulder_ratio, ratio);
                ++shoulder_samples;
                if (ratio < 0.32) {
                    ++shoulder_collapsed_samples;
                }
            }

            const Eigen::Vector3f palm = palmNormal(side, canonical, globals);
            if (palm.squaredNorm() > 1e-8f) {
                bool compared_to_source = false;
                if (!source_globals.empty()) {
                    const Eigen::Vector3f source_palm = palmNormal(side, source_canonical, source_globals);
                    if (source_palm.squaredNorm() > 1e-8f) {
                        const Eigen::Vector3f expected_palm = (source_rest_to_target_rest * source_palm).normalized();
                        ++wrist_expected_samples;
                        if (expected_palm.dot(palm) < -0.45f) {
                            ++wrist_expected_reversed_samples;
                        }
                        compared_to_source = true;
                    }
                }
                if (!compared_to_source &&
                    previous_palm.contains(side) &&
                    previous_hand_rotation.contains(side) &&
                    hand >= 0 &&
                    hand < static_cast<int>(globals.size())) {
                    const double dot = previous_palm.value(side).dot(palm);
                    const Eigen::Quaternionf hand_rotation = decomposeTransform(globals[hand]).rotation;
                    const double hand_rotation_delta =
                        2.0 * std::acos(std::clamp(std::abs(previous_hand_rotation.value(side).dot(hand_rotation)),
                                                   0.0f,
                                                   1.0f));
                    if (dot < -0.58 && hand_rotation_delta > 1.75) {
                        ++report.wrist_flip_count;
                    }
                }
                previous_palm.insert(side, palm);
                if (hand >= 0 && hand < static_cast<int>(globals.size())) {
                    previous_hand_rotation.insert(side, decomposeTransform(globals[hand]).rotation);
                }
            }
        }
    }

    if (wrist_expected_samples > 0) {
        const double reversed_ratio =
            static_cast<double>(wrist_expected_reversed_samples) / static_cast<double>(wrist_expected_samples);
        if (wrist_expected_reversed_samples >= 3 && reversed_ratio > 0.12) {
            report.wrist_flip_count = wrist_expected_reversed_samples;
        } else {
            report.wrist_flip_count = 0;
        }
    }

    if (foot_samples > 0) {
        report.foot_float_ratio = foot_float_sum / static_cast<double>(foot_samples) / std::max(static_cast<double>(height), 1e-4);
        const double avg_lowest = foot_lowest_sum / static_cast<double>(foot_samples);
        const double high_ratio = static_cast<double>(foot_high_samples) / static_cast<double>(foot_samples);
        if (report.foot_float_ratio > 0.055 && high_ratio > 0.35) {
            addIssue(&report,
                     report.foot_float_ratio > 0.12 ? RetargetQualitySeverity::Error : RetargetQualitySeverity::Warning,
                     QStringLiteral("foot_floating"),
                     QStringLiteral("Lowest foot floats above bind floor: avg=%1 height, high-frame-ratio=%2, bindFloor=%3, avgLowestFoot=%4.")
                         .arg(report.foot_float_ratio, 0, 'f', 3)
                         .arg(high_ratio, 0, 'f', 3)
                         .arg(bind_floor, 0, 'f', 4)
                         .arg(avg_lowest, 0, 'f', 4),
                     report.foot_float_ratio,
                     std::min(20.0, report.foot_float_ratio * 120.0));
        }
    }

    report.shoulder_collapse_ratio = min_shoulder_ratio;
    const double shoulder_collapse_frame_ratio = shoulder_samples > 0
        ? static_cast<double>(shoulder_collapsed_samples) / static_cast<double>(shoulder_samples)
        : 0.0;
    if (min_shoulder_ratio < 0.24 || shoulder_collapse_frame_ratio > 0.18) {
        addIssue(&report,
                 min_shoulder_ratio < 0.18 || shoulder_collapse_frame_ratio > 0.45
                     ? RetargetQualitySeverity::Error
                     : RetargetQualitySeverity::Warning,
                 QStringLiteral("shoulder_collapse"),
                 QStringLiteral("Shoulder-to-hand reach collapsed: min ratio=%1, low-frame-ratio=%2.")
                     .arg(min_shoulder_ratio, 0, 'f', 3)
                     .arg(shoulder_collapse_frame_ratio, 0, 'f', 3),
                 min_shoulder_ratio,
                 std::min(18.0, (0.36 - min_shoulder_ratio) * 45.0 + shoulder_collapse_frame_ratio * 12.0));
    }

    if (report.wrist_flip_count > 0) {
        addIssue(&report,
                 report.wrist_flip_count > 2 ? RetargetQualitySeverity::Error : RetargetQualitySeverity::Warning,
                 QStringLiteral("wrist_flip"),
                 QStringLiteral("Palm normal flipped between sampled frames %1 times.").arg(report.wrist_flip_count),
                 report.wrist_flip_count,
                 std::min(20.0, 6.0 * static_cast<double>(report.wrist_flip_count)));
    }

    report.grade = gradeForScore(report.score);
    return report;
}

QString retargetQualitySummaryText(const RetargetQualityReport& report, bool chinese) {
    if (chinese) {
        return QStringLiteral("质量评分：%1 / 100，等级 %2，问题：%3。")
            .arg(report.score, 0, 'f', 1)
            .arg(report.grade)
            .arg(retargetQualityIssueCodes(report));
    }
    return QStringLiteral("Quality score: %1 / 100, grade %2, issues: %3.")
        .arg(report.score, 0, 'f', 1)
        .arg(report.grade)
        .arg(retargetQualityIssueCodes(report));
}

QString retargetQualityIssueCodes(const RetargetQualityReport& report) {
    if (report.issues.isEmpty()) {
        return QStringLiteral("none");
    }
    QStringList codes;
    for (const RetargetQualityIssue& issue : report.issues) {
        if (!codes.contains(issue.code)) {
            codes << issue.code;
        }
    }
    return codes.join(QStringLiteral("|"));
}

} // namespace haorendergi
