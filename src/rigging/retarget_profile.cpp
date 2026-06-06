#include "rigging/retarget_profile.h"

#include <QHash>
#include <QJsonArray>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

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

QHash<QString, BoneMapping> buildCanonicalIndex(const BoneMappingResult& mapping_result) {
    QHash<QString, BoneMapping> index;
    for (const BoneMapping& mapping : mapping_result.mappings) {
        if (!mapping.canonical_name.isEmpty() && !index.contains(mapping.canonical_name)) {
            index.insert(mapping.canonical_name, mapping);
        }
    }
    return index;
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

Eigen::Vector3f globalPosition(const std::vector<Eigen::Matrix4f>& transforms, int node_index) {
    if (node_index < 0 || node_index >= static_cast<int>(transforms.size())) {
        return Eigen::Vector3f::Zero();
    }
    return transforms[node_index].block<3, 1>(0, 3);
}

Eigen::Matrix3f orthonormalizedLinear(const Eigen::Matrix4f& transform) {
    Eigen::Matrix3f linear = transform.block<3, 3>(0, 0);
    for (int axis = 0; axis < 3; ++axis) {
        const float length = linear.col(axis).norm();
        if (length > 1e-8f) {
            linear.col(axis) /= length;
        } else {
            linear.col(axis).setZero();
            linear(axis, axis) = 1.0f;
        }
    }
    return linear;
}

float skeletonHeight(const SceneModel& scene) {
    const std::vector<Eigen::Matrix4f> globals = globalBindTransforms(scene);
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

void addIssue(RetargetProfile* profile,
              RetargetIssueSeverity severity,
              const QString& code,
              const QString& message) {
    if (!profile) {
        return;
    }
    RetargetIssue issue;
    issue.severity = severity;
    issue.code = code;
    issue.message = message;
    profile->issues.push_back(issue);
}

QString severityText(RetargetIssueSeverity severity) {
    switch (severity) {
    case RetargetIssueSeverity::Error:
        return QStringLiteral("error");
    case RetargetIssueSeverity::Warning:
        return QStringLiteral("warning");
    case RetargetIssueSeverity::Info:
    default:
        return QStringLiteral("info");
    }
}

bool hasPositionKeys(const SceneModel& source_animation, const QString& bone_name) {
    const QHash<QString, int> node_index = buildNodeIndex(source_animation);
    const int node = node_index.value(bone_name, -1);
    if (node < 0 || source_animation.animations.empty()) {
        return false;
    }
    for (const AnimationChannelData& channel : source_animation.animations.front().channels) {
        if (channel.node_index == node && !channel.positions.empty()) {
            return true;
        }
    }
    return false;
}

RetargetSegmentSample makeSegmentSample(const QString& label,
                                        const QString& from_canonical,
                                        const QString& to_canonical,
                                        const QHash<QString, BoneMapping>& canonical_index,
                                        const QHash<QString, int>& source_nodes,
                                        const QHash<QString, int>& target_nodes,
                                        const std::vector<Eigen::Matrix4f>& source_globals,
                                        const std::vector<Eigen::Matrix4f>& target_globals) {
    RetargetSegmentSample sample;
    sample.label = label;
    sample.from_canonical = from_canonical;
    sample.to_canonical = to_canonical;
    const BoneMapping from_mapping = canonical_index.value(from_canonical);
    const BoneMapping to_mapping = canonical_index.value(to_canonical);
    if (from_mapping.source_bone.isEmpty() || to_mapping.source_bone.isEmpty()) {
        return sample;
    }
    sample.source_from = from_mapping.source_bone;
    sample.source_to = to_mapping.source_bone;
    sample.target_from = from_mapping.target_bone;
    sample.target_to = to_mapping.target_bone;
    const int source_from_node = source_nodes.value(sample.source_from, -1);
    const int source_to_node = source_nodes.value(sample.source_to, -1);
    const int target_from_node = target_nodes.value(sample.target_from, -1);
    const int target_to_node = target_nodes.value(sample.target_to, -1);
    if (source_from_node < 0 || source_to_node < 0 || target_from_node < 0 || target_to_node < 0) {
        return sample;
    }
    sample.source_direction = globalPosition(source_globals, source_to_node) - globalPosition(source_globals, source_from_node);
    sample.target_direction = globalPosition(target_globals, target_to_node) - globalPosition(target_globals, target_from_node);
    const float source_length = sample.source_direction.norm();
    const float target_length = sample.target_direction.norm();
    if (source_length <= 1e-6f || target_length <= 1e-6f) {
        return sample;
    }
    sample.source_direction /= source_length;
    sample.target_direction /= target_length;
    sample.direction_dot = sample.source_direction.dot(sample.target_direction);
    sample.complete = true;
    return sample;
}

QVector<QPair<QString, QString>> segmentPairs() {
    return {
        { QStringLiteral("Hips"), QStringLiteral("Spine") },
        { QStringLiteral("Spine"), QStringLiteral("Chest") },
        { QStringLiteral("Chest"), QStringLiteral("Neck") },
        { QStringLiteral("Neck"), QStringLiteral("Head") },
        { QStringLiteral("LeftUpperArm"), QStringLiteral("LeftLowerArm") },
        { QStringLiteral("LeftLowerArm"), QStringLiteral("LeftHand") },
        { QStringLiteral("RightUpperArm"), QStringLiteral("RightLowerArm") },
        { QStringLiteral("RightLowerArm"), QStringLiteral("RightHand") },
        { QStringLiteral("LeftUpperLeg"), QStringLiteral("LeftLowerLeg") },
        { QStringLiteral("LeftLowerLeg"), QStringLiteral("LeftFoot") },
        { QStringLiteral("RightUpperLeg"), QStringLiteral("RightLowerLeg") },
        { QStringLiteral("RightLowerLeg"), QStringLiteral("RightFoot") },
        { QStringLiteral("LeftHand"), QStringLiteral("LeftIndexProximal") },
        { QStringLiteral("LeftIndexProximal"), QStringLiteral("LeftIndexIntermediate") },
        { QStringLiteral("LeftIndexIntermediate"), QStringLiteral("LeftIndexDistal") },
        { QStringLiteral("RightHand"), QStringLiteral("RightIndexProximal") },
        { QStringLiteral("RightIndexProximal"), QStringLiteral("RightIndexIntermediate") },
        { QStringLiteral("RightIndexIntermediate"), QStringLiteral("RightIndexDistal") },
        { QStringLiteral("LeftHand"), QStringLiteral("LeftThumbMetacarpal") },
        { QStringLiteral("LeftThumbMetacarpal"), QStringLiteral("LeftThumbProximal") },
        { QStringLiteral("LeftThumbProximal"), QStringLiteral("LeftThumbDistal") },
        { QStringLiteral("RightHand"), QStringLiteral("RightThumbMetacarpal") },
        { QStringLiteral("RightThumbMetacarpal"), QStringLiteral("RightThumbProximal") },
        { QStringLiteral("RightThumbProximal"), QStringLiteral("RightThumbDistal") },
    };
}

bool isImportantSegment(const RetargetSegmentSample& sample) {
    return sample.label.contains(QStringLiteral("Arm"), Qt::CaseInsensitive) ||
           sample.label.contains(QStringLiteral("Hand"), Qt::CaseInsensitive) ||
           sample.label.contains(QStringLiteral("Thumb"), Qt::CaseInsensitive) ||
           sample.label.contains(QStringLiteral("Index"), Qt::CaseInsensitive) ||
           sample.label.contains(QStringLiteral("Leg"), Qt::CaseInsensitive) ||
           sample.label.contains(QStringLiteral("Foot"), Qt::CaseInsensitive);
}

QJsonObject vectorToJson(const Eigen::Vector3f& value) {
    QJsonObject object;
    object.insert(QStringLiteral("x"), value.x());
    object.insert(QStringLiteral("y"), value.y());
    object.insert(QStringLiteral("z"), value.z());
    return object;
}

} // namespace

RetargetProfile buildRetargetProfile(const SceneModel& source_animation,
                                     const SceneModel& target_bind_scene,
                                     const BoneMappingResult& mapping_result) {
    RetargetProfile profile;
    profile.source_height = skeletonHeight(source_animation);
    profile.target_height = skeletonHeight(target_bind_scene);
    profile.translation_scale = std::clamp(profile.target_height / profile.source_height, 0.001f, 100.0f);
    profile.mapped_count = mapping_result.mappings.size();
    profile.unmapped_source_count = mapping_result.unmapped_source_bones.size();
    profile.unmapped_target_count = mapping_result.unmapped_target_bones.size();

    const QHash<QString, BoneMapping> canonical_index = buildCanonicalIndex(mapping_result);
    const QHash<QString, int> source_nodes = buildNodeIndex(source_animation);
    const QHash<QString, int> target_nodes = buildNodeIndex(target_bind_scene);
    const std::vector<Eigen::Matrix4f> source_globals = globalBindTransforms(source_animation);
    const std::vector<Eigen::Matrix4f> target_globals = globalBindTransforms(target_bind_scene);

    for (const QPair<QString, QString>& pair : segmentPairs()) {
        const QString label = pair.first + QStringLiteral("->") + pair.second;
        RetargetSegmentSample sample = makeSegmentSample(label,
                                                         pair.first,
                                                         pair.second,
                                                         canonical_index,
                                                         source_nodes,
                                                         target_nodes,
                                                         source_globals,
                                                         target_globals);
        if (!sample.complete && isImportantSegment(sample)) {
            addIssue(&profile,
                     RetargetIssueSeverity::Warning,
                     QStringLiteral("missing_segment"),
                     QStringLiteral("Missing segment mapping for %1.").arg(label));
        } else if (sample.complete && sample.direction_dot < 0.65f) {
            addIssue(&profile,
                     sample.direction_dot < 0.25f ? RetargetIssueSeverity::Error : RetargetIssueSeverity::Warning,
                     QStringLiteral("rest_direction_mismatch"),
                     QStringLiteral("%1 rest-pose direction mismatch: dot=%2.")
                         .arg(label)
                         .arg(sample.direction_dot, 0, 'f', 3));
        }
        profile.segment_samples.push_back(sample);
    }

    for (const BoneMapping& mapping : mapping_result.mappings) {
        const int source_node = source_nodes.value(mapping.source_bone, -1);
        const int target_node = target_nodes.value(mapping.target_bone, -1);
        if (source_node < 0 || target_node < 0) {
            continue;
        }
        const Eigen::Matrix3f source_axes = orthonormalizedLinear(source_globals[source_node]);
        const Eigen::Matrix3f target_axes = orthonormalizedLinear(target_globals[target_node]);
        RetargetAxisSample sample;
        sample.canonical_name = mapping.canonical_name;
        sample.source_bone = mapping.source_bone;
        sample.target_bone = mapping.target_bone;
        for (int axis = 0; axis < 3; ++axis) {
            sample.signed_axis_dots[axis] = source_axes.col(axis).dot(target_axes.col(axis));
        }
        sample.min_abs_axis_dot = std::min({ std::abs(sample.signed_axis_dots.x()),
                                             std::abs(sample.signed_axis_dots.y()),
                                             std::abs(sample.signed_axis_dots.z()) });
        const bool hand_or_finger = mapping.canonical_name.contains(QStringLiteral("Hand"), Qt::CaseInsensitive) ||
                                    mapping.canonical_name.contains(QStringLiteral("Thumb"), Qt::CaseInsensitive) ||
                                    mapping.canonical_name.contains(QStringLiteral("Index"), Qt::CaseInsensitive) ||
                                    mapping.canonical_name.contains(QStringLiteral("Middle"), Qt::CaseInsensitive) ||
                                    mapping.canonical_name.contains(QStringLiteral("Ring"), Qt::CaseInsensitive) ||
                                    mapping.canonical_name.contains(QStringLiteral("Little"), Qt::CaseInsensitive);
        sample.roll_risk = hand_or_finger && sample.min_abs_axis_dot < 0.55f;
        if (sample.roll_risk) {
            ++profile.high_roll_risk_count;
            addIssue(&profile,
                     RetargetIssueSeverity::Warning,
                     QStringLiteral("bone_roll_risk"),
                     QStringLiteral("%1 may need bone-roll/local-axis correction: axis min dot=%2.")
                         .arg(mapping.canonical_name)
                         .arg(sample.min_abs_axis_dot, 0, 'f', 3));
        }
        profile.axis_samples.push_back(sample);
    }

    const BoneMapping hips = canonical_index.value(QStringLiteral("Hips"));
    if (!hips.source_bone.isEmpty() && hasPositionKeys(source_animation, hips.source_bone)) {
        addIssue(&profile,
                 RetargetIssueSeverity::Info,
                 QStringLiteral("hips_position_channel"),
                 QStringLiteral("Source Hips has translation keys. Use in-place preview or split horizontal root motion into a Root channel."));
    }
    if (profile.unmapped_source_count > 0) {
        addIssue(&profile,
                 RetargetIssueSeverity::Warning,
                 QStringLiteral("unmapped_source"),
                 QStringLiteral("%1 source bones are not mapped.").arg(profile.unmapped_source_count));
    }
    return profile;
}

QString retargetProfileSummaryText(const RetargetProfile& profile, bool chinese) {
    if (chinese) {
        return QStringLiteral("Retarget Profile：%1 个映射，scale=%2，roll 风险=%3，诊断=%4。")
            .arg(profile.mapped_count)
            .arg(profile.translation_scale, 0, 'f', 4)
            .arg(profile.high_roll_risk_count)
            .arg(profile.issues.size());
    }
    return QStringLiteral("Retarget profile: %1 mappings, scale=%2, roll risks=%3, issues=%4.")
        .arg(profile.mapped_count)
        .arg(profile.translation_scale, 0, 'f', 4)
        .arg(profile.high_roll_risk_count)
        .arg(profile.issues.size());
}

QString retargetProfileDetailedText(const RetargetProfile& profile, bool chinese, int max_issues) {
    QStringList lines;
    lines << retargetProfileSummaryText(profile, chinese);
    if (chinese) {
        lines << QStringLiteral("Blender式检查：rest方向、bone roll/local axis、root motion、IK后处理。");
    } else {
        lines << QStringLiteral("Blender-style checks: rest direction, bone roll/local axes, root motion, IK post-pass.");
    }

    int emitted = 0;
    for (const RetargetIssue& issue : profile.issues) {
        if (emitted >= max_issues) {
            break;
        }
        lines << QStringLiteral("[%1] %2: %3").arg(severityText(issue.severity), issue.code, issue.message);
        ++emitted;
    }
    if (profile.issues.size() > emitted) {
        lines << (chinese
            ? QStringLiteral("另有 %1 条诊断未显示。").arg(profile.issues.size() - emitted)
            : QStringLiteral("%1 more diagnostics hidden.").arg(profile.issues.size() - emitted));
    }
    return lines.join(QStringLiteral("\n"));
}

QJsonObject retargetProfileToJson(const RetargetProfile& profile) {
    QJsonObject object;
    object.insert(QStringLiteral("source_height"), profile.source_height);
    object.insert(QStringLiteral("target_height"), profile.target_height);
    object.insert(QStringLiteral("translation_scale"), profile.translation_scale);
    object.insert(QStringLiteral("mapped_count"), profile.mapped_count);
    object.insert(QStringLiteral("unmapped_source_count"), profile.unmapped_source_count);
    object.insert(QStringLiteral("unmapped_target_count"), profile.unmapped_target_count);
    object.insert(QStringLiteral("high_roll_risk_count"), profile.high_roll_risk_count);

    QJsonArray issues;
    for (const RetargetIssue& issue : profile.issues) {
        QJsonObject item;
        item.insert(QStringLiteral("severity"), severityText(issue.severity));
        item.insert(QStringLiteral("code"), issue.code);
        item.insert(QStringLiteral("message"), issue.message);
        issues.push_back(item);
    }
    object.insert(QStringLiteral("issues"), issues);

    QJsonArray segments;
    for (const RetargetSegmentSample& sample : profile.segment_samples) {
        QJsonObject item;
        item.insert(QStringLiteral("label"), sample.label);
        item.insert(QStringLiteral("complete"), sample.complete);
        item.insert(QStringLiteral("direction_dot"), sample.direction_dot);
        item.insert(QStringLiteral("source_from"), sample.source_from);
        item.insert(QStringLiteral("source_to"), sample.source_to);
        item.insert(QStringLiteral("target_from"), sample.target_from);
        item.insert(QStringLiteral("target_to"), sample.target_to);
        item.insert(QStringLiteral("source_direction"), vectorToJson(sample.source_direction));
        item.insert(QStringLiteral("target_direction"), vectorToJson(sample.target_direction));
        segments.push_back(item);
    }
    object.insert(QStringLiteral("segments"), segments);

    QJsonArray axes;
    for (const RetargetAxisSample& sample : profile.axis_samples) {
        QJsonObject item;
        item.insert(QStringLiteral("canonical"), sample.canonical_name);
        item.insert(QStringLiteral("source_bone"), sample.source_bone);
        item.insert(QStringLiteral("target_bone"), sample.target_bone);
        item.insert(QStringLiteral("axis_dots"), vectorToJson(sample.signed_axis_dots));
        item.insert(QStringLiteral("min_abs_axis_dot"), sample.min_abs_axis_dot);
        item.insert(QStringLiteral("roll_risk"), sample.roll_risk);
        axes.push_back(item);
    }
    object.insert(QStringLiteral("axes"), axes);
    return object;
}

} // namespace haorendergi
