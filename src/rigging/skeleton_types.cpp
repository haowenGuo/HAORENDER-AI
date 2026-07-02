#include "rigging/skeleton_types.h"

#include <QDir>

namespace haorendergi {

namespace {

QJsonObject vectorToJson(const Eigen::Vector3f& value) {
    QJsonObject object;
    object.insert(QStringLiteral("x"), value.x());
    object.insert(QStringLiteral("y"), value.y());
    object.insert(QStringLiteral("z"), value.z());
    return object;
}

} // namespace

QString rigSideToString(RigSide side) {
    switch (side) {
    case RigSide::Center:
        return QStringLiteral("center");
    case RigSide::Left:
        return QStringLiteral("left");
    case RigSide::Right:
        return QStringLiteral("right");
    case RigSide::Unknown:
    default:
        return QStringLiteral("unknown");
    }
}

QJsonObject skeletonGraphToJson(const SkeletonGraph& skeleton) {
    QJsonObject object;
    object.insert(QStringLiteral("sourcePath"), QDir::toNativeSeparators(skeleton.source_path));
    object.insert(QStringLiteral("assetLabel"), skeleton.asset_label);
    object.insert(QStringLiteral("boneCount"), skeleton.bones.size());
    object.insert(QStringLiteral("skinnedBoneCount"), skeleton.skinnedBoneCount());
    object.insert(QStringLiteral("recognizedBoneCount"), skeleton.recognizedBoneCount());

    QJsonArray bones;
    for (const SkeletonBone& bone : skeleton.bones) {
        QJsonObject bone_object;
        bone_object.insert(QStringLiteral("name"), bone.name);
        bone_object.insert(QStringLiteral("normalizedName"), bone.normalized_name);
        bone_object.insert(QStringLiteral("parentIndex"), bone.parent_index);
        bone_object.insert(QStringLiteral("depth"), bone.depth);
        bone_object.insert(QStringLiteral("referencedBySkin"), bone.referenced_by_skin);
        bone_object.insert(QStringLiteral("hasAnimationChannel"), bone.has_animation_channel);
        bone_object.insert(QStringLiteral("localTranslation"), vectorToJson(bone.local_translation));
        bone_object.insert(QStringLiteral("canonicalName"), bone.semantic.canonical_name);
        bone_object.insert(QStringLiteral("side"), rigSideToString(bone.semantic.side));
        bone_object.insert(QStringLiteral("semanticConfidence"), bone.semantic.confidence);
        bones.append(bone_object);
    }
    object.insert(QStringLiteral("bones"), bones);

    QJsonArray animations;
    for (const AnimationClipInfo& animation : skeleton.animations) {
        QJsonObject animation_object;
        animation_object.insert(QStringLiteral("name"), animation.name);
        animation_object.insert(QStringLiteral("duration"), animation.duration);
        animation_object.insert(QStringLiteral("ticksPerSecond"), animation.ticks_per_second);
        animation_object.insert(QStringLiteral("channelCount"), animation.channel_count);
        animations.append(animation_object);
    }
    object.insert(QStringLiteral("animations"), animations);
    return object;
}

QJsonObject boneMappingResultToJson(const SkeletonGraph& source,
                                    const SkeletonGraph& target,
                                    const BoneMappingResult& result) {
    QJsonObject object;
    object.insert(QStringLiteral("format"), QStringLiteral("HaoRigMap"));
    object.insert(QStringLiteral("version"), 1);
    object.insert(QStringLiteral("summary"), result.summary);
    object.insert(QStringLiteral("source"), skeletonGraphToJson(source));
    object.insert(QStringLiteral("target"), skeletonGraphToJson(target));

    QJsonArray mappings;
    for (const BoneMapping& mapping : result.mappings) {
        QJsonObject mapping_object;
        mapping_object.insert(QStringLiteral("sourceBone"), mapping.source_bone);
        mapping_object.insert(QStringLiteral("targetBone"), mapping.target_bone);
        mapping_object.insert(QStringLiteral("canonicalName"), mapping.canonical_name);
        mapping_object.insert(QStringLiteral("confidence"), mapping.confidence);
        mapping_object.insert(QStringLiteral("reason"), mapping.reason);
        mappings.append(mapping_object);
    }
    object.insert(QStringLiteral("mappings"), mappings);

    QJsonArray unmapped_sources;
    for (const QString& name : result.unmapped_source_bones) {
        unmapped_sources.append(name);
    }
    object.insert(QStringLiteral("unmappedSourceBones"), unmapped_sources);

    QJsonArray unmapped_targets;
    for (const QString& name : result.unmapped_target_bones) {
        unmapped_targets.append(name);
    }
    object.insert(QStringLiteral("unmappedTargetBones"), unmapped_targets);
    return object;
}

} // namespace haorendergi
