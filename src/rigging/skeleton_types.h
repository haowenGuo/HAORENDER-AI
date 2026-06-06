#pragma once

#include <Eigen/Dense>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace haorendergi {

enum class RigSide {
    Center,
    Left,
    Right,
    Unknown
};

struct BoneSemantic {
    QString canonical_name;
    RigSide side = RigSide::Unknown;
    float confidence = 0.0f;
};

struct SkeletonBone {
    QString name;
    QString normalized_name;
    int parent_index = -1;
    int depth = 0;
    Eigen::Vector3f local_translation = Eigen::Vector3f::Zero();
    bool referenced_by_skin = false;
    bool has_animation_channel = false;
    BoneSemantic semantic;
};

struct AnimationClipInfo {
    QString name;
    double duration = 0.0;
    double ticks_per_second = 0.0;
    int channel_count = 0;
};

struct SkeletonGraph {
    QString source_path;
    QString asset_label;
    QVector<SkeletonBone> bones;
    QVector<AnimationClipInfo> animations;

    bool empty() const {
        return bones.isEmpty();
    }

    int skinnedBoneCount() const {
        int count = 0;
        for (const SkeletonBone& bone : bones) {
            if (bone.referenced_by_skin) {
                ++count;
            }
        }
        return count;
    }

    int recognizedBoneCount() const {
        int count = 0;
        for (const SkeletonBone& bone : bones) {
            if (!bone.semantic.canonical_name.isEmpty()) {
                ++count;
            }
        }
        return count;
    }
};

struct BoneMapping {
    QString source_bone;
    QString target_bone;
    QString canonical_name;
    float confidence = 0.0f;
    QString reason;
};

struct BoneMappingResult {
    QVector<BoneMapping> mappings;
    QVector<QString> unmapped_source_bones;
    QVector<QString> unmapped_target_bones;
    QString summary;
};

QString rigSideToString(RigSide side);
QJsonObject skeletonGraphToJson(const SkeletonGraph& skeleton);
QJsonObject boneMappingResultToJson(const SkeletonGraph& source,
                                    const SkeletonGraph& target,
                                    const BoneMappingResult& result);

} // namespace haorendergi
