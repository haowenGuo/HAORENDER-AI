#include "rigging/ai_bone_mapper.h"

#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace haorendergi {

namespace {

float depthCompatibility(const SkeletonBone& source, const SkeletonBone& target) {
    const int delta = std::abs(source.depth - target.depth);
    return std::max(0.0f, 1.0f - static_cast<float>(delta) * 0.08f);
}

float skinCompatibility(const SkeletonBone& source, const SkeletonBone& target) {
    if (source.referenced_by_skin == target.referenced_by_skin) {
        return 1.0f;
    }
    return source.referenced_by_skin || target.referenced_by_skin ? 0.75f : 0.9f;
}

bool isAssimpFbxHelperNode(const QString& name) {
    return name.contains(QStringLiteral("$AssimpFbx$"), Qt::CaseInsensitive) ||
           name.contains(QStringLiteral("PreRotation"), Qt::CaseInsensitive) ||
           name.contains(QStringLiteral("PostRotation"), Qt::CaseInsensitive);
}

float sourceAnimationSuitability(const SkeletonBone& bone) {
    float score = 0.0f;
    if (bone.has_animation_channel) {
        score += 0.16f;
    }
    if (bone.referenced_by_skin) {
        score += 0.04f;
    }
    if (isAssimpFbxHelperNode(bone.name)) {
        score -= bone.has_animation_channel ? 0.04f : 0.18f;
    }
    if (bone.name.contains(QStringLiteral("$AssimpFbx$_Translation"), Qt::CaseInsensitive) &&
        !bone.has_animation_channel) {
        score -= 0.12f;
    }
    return score;
}

float targetBoneSuitability(const SkeletonBone& bone) {
    float score = 0.0f;
    if (bone.referenced_by_skin) {
        score += 0.06f;
    }
    if (isAssimpFbxHelperNode(bone.name)) {
        score -= 0.12f;
    }
    return score;
}

float pairScore(const SkeletonBone& source, const SkeletonBone& target) {
    const float semantic_score = 0.55f * source.semantic.confidence + 0.35f * target.semantic.confidence;
    return semantic_score +
        0.06f * depthCompatibility(source, target) +
        0.04f * skinCompatibility(source, target) +
        sourceAnimationSuitability(source) +
        targetBoneSuitability(target);
}

QString reasonFor(const SkeletonBone& source, const SkeletonBone& target, float confidence) {
    QStringList parts;
    parts << QStringLiteral("canonical=%1").arg(source.semantic.canonical_name);
    parts << QStringLiteral("sourceName=%1").arg(source.name);
    parts << QStringLiteral("targetName=%1").arg(target.name);
    parts << QStringLiteral("depth=%1/%2").arg(source.depth).arg(target.depth);
    if (source.referenced_by_skin || target.referenced_by_skin) {
        parts << QStringLiteral("skinBone=%1/%2")
                     .arg(source.referenced_by_skin ? QStringLiteral("yes") : QStringLiteral("no"))
                     .arg(target.referenced_by_skin ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    parts << QStringLiteral("animChannel=%1/%2")
                 .arg(source.has_animation_channel ? QStringLiteral("yes") : QStringLiteral("no"))
                 .arg(target.has_animation_channel ? QStringLiteral("yes") : QStringLiteral("no"));
    if (isAssimpFbxHelperNode(source.name) || isAssimpFbxHelperNode(target.name)) {
        parts << QStringLiteral("assimpHelper=%1/%2")
                     .arg(isAssimpFbxHelperNode(source.name) ? QStringLiteral("yes") : QStringLiteral("no"))
                     .arg(isAssimpFbxHelperNode(target.name) ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    parts << QStringLiteral("confidence=%1").arg(confidence, 0, 'f', 2);
    return parts.join(QStringLiteral("; "));
}

bool isCandidateBone(const SkeletonBone& bone) {
    return !bone.semantic.canonical_name.isEmpty();
}

} // namespace

BoneMappingResult AiBoneMapper::mapSkeletons(const SkeletonGraph& source, const SkeletonGraph& target) const {
    BoneMappingResult result;
    QSet<int> used_targets;
    QSet<int> used_sources;

    QSet<QString> canonical_names;
    for (const SkeletonBone& source_bone : source.bones) {
        if (isCandidateBone(source_bone)) {
            canonical_names.insert(source_bone.semantic.canonical_name);
        }
    }
    QVector<QString> ordered_canonicals;
    for (const QString& canonical_name : canonical_names) {
        ordered_canonicals.push_back(canonical_name);
    }
    std::sort(ordered_canonicals.begin(), ordered_canonicals.end(), [](const QString& lhs, const QString& rhs) {
        const auto priority = [](const QString& canonical) {
            if (canonical == QStringLiteral("Root")) return 0;
            if (canonical == QStringLiteral("Hips")) return 1;
            if (canonical == QStringLiteral("Spine")) return 2;
            if (canonical == QStringLiteral("Chest")) return 3;
            if (canonical == QStringLiteral("UpperChest")) return 4;
            if (canonical == QStringLiteral("Neck")) return 5;
            if (canonical == QStringLiteral("Head")) return 6;
            if (canonical.contains(QStringLiteral("Shoulder"))) return 7;
            if (canonical.contains(QStringLiteral("UpperArm"))) return 8;
            if (canonical.contains(QStringLiteral("LowerArm"))) return 9;
            if (canonical.contains(QStringLiteral("Hand"))) return 10;
            if (canonical.contains(QStringLiteral("UpperLeg"))) return 11;
            if (canonical.contains(QStringLiteral("LowerLeg"))) return 12;
            if (canonical.contains(QStringLiteral("Foot"))) return 13;
            if (canonical.contains(QStringLiteral("Eye"))) return 14;
            return 20;
        };
        const int lp = priority(lhs);
        const int rp = priority(rhs);
        if (lp != rp) {
            return lp < rp;
        }
        return lhs < rhs;
    });

    for (const QString& canonical_name : ordered_canonicals) {
        int best_source_index = -1;
        int best_target_index = -1;
        float best_score = -std::numeric_limits<float>::max();
        for (int source_index = 0; source_index < source.bones.size(); ++source_index) {
            if (used_sources.contains(source_index)) {
                continue;
            }
            const SkeletonBone& source_bone = source.bones[source_index];
            if (!isCandidateBone(source_bone) || source_bone.semantic.canonical_name != canonical_name) {
                continue;
            }
            for (int target_index = 0; target_index < target.bones.size(); ++target_index) {
                if (used_targets.contains(target_index)) {
                    continue;
                }
                const SkeletonBone& target_bone = target.bones[target_index];
                if (target_bone.semantic.canonical_name != canonical_name) {
                    continue;
                }

                const float score = pairScore(source_bone, target_bone);
                if (score > best_score) {
                    best_score = score;
                    best_source_index = source_index;
                    best_target_index = target_index;
                }
            }
        }

        if (best_source_index >= 0 && best_target_index >= 0) {
            const SkeletonBone& source_bone = source.bones[best_source_index];
            const SkeletonBone& target_bone = target.bones[best_target_index];
            BoneMapping mapping;
            mapping.source_bone = source_bone.name;
            mapping.target_bone = target_bone.name;
            mapping.canonical_name = source_bone.semantic.canonical_name;
            mapping.confidence = std::clamp(best_score, 0.0f, 0.99f);
            mapping.reason = reasonFor(source_bone, target_bone, mapping.confidence);
            result.mappings.push_back(mapping);
            used_sources.insert(best_source_index);
            used_targets.insert(best_target_index);
        }
    }

    for (int source_index = 0; source_index < source.bones.size(); ++source_index) {
        const SkeletonBone& source_bone = source.bones[source_index];
        if (isCandidateBone(source_bone) && !used_sources.contains(source_index)) {
            result.unmapped_source_bones.push_back(source_bone.name);
        }
    }

    for (int target_index = 0; target_index < target.bones.size(); ++target_index) {
        const SkeletonBone& target_bone = target.bones[target_index];
        if (isCandidateBone(target_bone) && !used_targets.contains(target_index)) {
            result.unmapped_target_bones.push_back(target_bone.name);
        }
    }

    std::sort(result.mappings.begin(), result.mappings.end(), [](const BoneMapping& lhs, const BoneMapping& rhs) {
        if (lhs.confidence == rhs.confidence) {
            return lhs.canonical_name < rhs.canonical_name;
        }
        return lhs.confidence > rhs.confidence;
    });

    result.summary = QStringLiteral("%1 mapped, %2 source bones need review, %3 target bones unused.")
                         .arg(result.mappings.size())
                         .arg(result.unmapped_source_bones.size())
                         .arg(result.unmapped_target_bones.size());
    return result;
}

} // namespace haorendergi
