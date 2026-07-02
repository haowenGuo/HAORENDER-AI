#include "rigging/bone_name_normalizer.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace haorendergi {

namespace {

bool containsAny(const QString& text, std::initializer_list<const char*> tokens) {
    for (const char* token : tokens) {
        if (text.contains(QString::fromLatin1(token))) {
            return true;
        }
    }
    return false;
}

RigSide inferSide(const QString& normalized) {
    const QString padded = QStringLiteral(" ") + normalized + QStringLiteral(" ");
    if (padded.contains(QStringLiteral(" left ")) ||
        padded.contains(QStringLiteral(" l ")) ||
        padded.contains(QStringLiteral(" lft ")) ||
        normalized.startsWith(QStringLiteral("l ")) ||
        normalized.endsWith(QStringLiteral(" l")) ||
        normalized.contains(QStringLiteral("left")) ||
        normalized.contains(QStringLiteral(" l "))) {
        return RigSide::Left;
    }
    if (padded.contains(QStringLiteral(" right ")) ||
        padded.contains(QStringLiteral(" r ")) ||
        padded.contains(QStringLiteral(" rgt ")) ||
        normalized.startsWith(QStringLiteral("r ")) ||
        normalized.endsWith(QStringLiteral(" r")) ||
        normalized.contains(QStringLiteral("right")) ||
        normalized.contains(QStringLiteral(" r "))) {
        return RigSide::Right;
    }

    if (normalized.contains(QStringLiteral("左"))) {
        return RigSide::Left;
    }
    if (normalized.contains(QStringLiteral("右"))) {
        return RigSide::Right;
    }
    return RigSide::Unknown;
}

QString sidePrefix(RigSide side) {
    if (side == RigSide::Left) {
        return QStringLiteral("Left");
    }
    if (side == RigSide::Right) {
        return QStringLiteral("Right");
    }
    return QString();
}

BoneSemantic semantic(const QString& canonical, RigSide side, float confidence) {
    BoneSemantic result;
    result.canonical_name = canonical;
    result.side = side;
    result.confidence = confidence;
    return result;
}

BoneSemantic sideSemantic(const QString& suffix, RigSide side, float confidence) {
    const QString prefix = sidePrefix(side);
    if (prefix.isEmpty()) {
        return semantic(QString(), RigSide::Unknown, 0.0f);
    }
    return semantic(prefix + suffix, side, confidence);
}

bool fingerMatches(const QString& text, std::initializer_list<const char*> names) {
    return containsAny(text, names);
}

QString fingerSegmentSuffix(const QString& compact, bool thumb) {
    const bool has_1 = compact.contains(QStringLiteral("01")) ||
                       compact.contains(QStringLiteral(" 1")) ||
                       compact.endsWith(QStringLiteral("1")) ||
                       compact.contains(QStringLiteral("1"));
    const bool has_2 = compact.contains(QStringLiteral("02")) ||
                       compact.contains(QStringLiteral(" 2")) ||
                       compact.endsWith(QStringLiteral("2")) ||
                       compact.contains(QStringLiteral("2"));
    const bool has_3 = compact.contains(QStringLiteral("03")) ||
                       compact.contains(QStringLiteral(" 3")) ||
                       compact.endsWith(QStringLiteral("3")) ||
                       compact.contains(QStringLiteral("3"));

    if (compact.contains(QStringLiteral("distal")) || has_3) {
        return QStringLiteral("Distal");
    }
    if (thumb) {
        if (compact.contains(QStringLiteral("proximal")) || has_2) {
            return QStringLiteral("Proximal");
        }
        if (compact.contains(QStringLiteral("metacarpal")) ||
            compact.contains(QStringLiteral("carpal")) ||
            has_1) {
            return QStringLiteral("Metacarpal");
        }
    } else {
        if (compact.contains(QStringLiteral("intermediate")) || has_2) {
            return QStringLiteral("Intermediate");
        }
        if (compact.contains(QStringLiteral("proximal")) || has_1) {
            return QStringLiteral("Proximal");
        }
    }
    return QString();
}

BoneSemantic fingerSemantic(const QString& finger_name, const QString& compact, RigSide side, float confidence) {
    const QString segment = fingerSegmentSuffix(compact, finger_name == QStringLiteral("Thumb"));
    return sideSemantic(finger_name + segment, side, segment.isEmpty() ? confidence - 0.08f : confidence);
}

} // namespace

QString normalizeBoneName(const QString& name) {
    QString text = name.toLower();
    text.replace(QStringLiteral("mixamorig:"), QStringLiteral(" "));
    text.replace(QStringLiteral("bip001"), QStringLiteral(" "));
    text.replace(QStringLiteral("bip01"), QStringLiteral(" "));
    text.replace(QStringLiteral("j bip"), QStringLiteral(" "));
    text.replace(QStringLiteral("j_bip"), QStringLiteral(" "));
    text.replace(QStringLiteral("cf_j"), QStringLiteral(" "));
    text.replace(QStringLiteral("armature"), QStringLiteral(" "));
    text.replace(QStringLiteral("skel"), QStringLiteral(" "));
    text.replace(QStringLiteral("valvebiped"), QStringLiteral(" "));
    text.replace(QStringLiteral("bn_"), QStringLiteral(" "));
    text.replace(QStringLiteral("bone"), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("[\\.:\\|/\\\\_\\-]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

BoneSemantic classifyBoneName(const QString& name) {
    const QString normalized = normalizeBoneName(name);
    const RigSide side = inferSide(normalized);
    const QString compact = normalized;

    if (compact.isEmpty()) {
        return semantic(QString(), RigSide::Unknown, 0.0f);
    }

    if (containsAny(compact, { "hips", "hip", "pelvis", "center", "waist", "下半身", "センター" })) {
        return semantic(QStringLiteral("Hips"), RigSide::Center, 0.92f);
    }
    if (containsAny(compact, { "root", "origin", "master", "全ての親", "全親" })) {
        return semantic(QStringLiteral("Root"), RigSide::Center, 0.75f);
    }
    if (containsAny(compact, { "upper chest", "upperchest", "spine 2", "spine2", "chest2", "胸2" })) {
        return semantic(QStringLiteral("UpperChest"), RigSide::Center, 0.88f);
    }
    if (containsAny(compact, { "chest", "spine 1", "spine1", "breast", "上半身2", "胸" })) {
        return semantic(QStringLiteral("Chest"), RigSide::Center, 0.90f);
    }
    if (containsAny(compact, { "spine", "abdomen", "上半身", "腹" })) {
        return semantic(QStringLiteral("Spine"), RigSide::Center, 0.88f);
    }
    if (containsAny(compact, { "neck", "首" })) {
        return semantic(QStringLiteral("Neck"), RigSide::Center, 0.95f);
    }
    if (containsAny(compact, { "head", "頭", "头" })) {
        return semantic(QStringLiteral("Head"), RigSide::Center, 0.96f);
    }
    if (containsAny(compact, { "eye", "目", "眼" })) {
        return side == RigSide::Unknown ? semantic(QStringLiteral("Eye"), RigSide::Center, 0.68f)
                                        : sideSemantic(QStringLiteral("Eye"), side, 0.78f);
    }
    if (containsAny(compact, { "jaw", "chin", "顎", "あご" })) {
        return semantic(QStringLiteral("Jaw"), RigSide::Center, 0.78f);
    }

    if (side != RigSide::Unknown) {
        if (containsAny(compact, { "shoulder", "clavicle", "collar", "鎖骨", "肩" })) {
            return sideSemantic(QStringLiteral("Shoulder"), side, 0.92f);
        }
        if (containsAny(compact, { "upper arm", "upperarm", "arm", "腕" }) &&
            !containsAny(compact, { "forearm", "lower arm", "lowerarm", "hand", "twist" })) {
            return sideSemantic(QStringLiteral("UpperArm"), side, 0.90f);
        }
        if (containsAny(compact, { "forearm", "lower arm", "lowerarm", "elbow", "ひじ", "肘" }) ||
            (containsAny(compact, { "arm" }) && containsAny(compact, { "lower" }))) {
            return sideSemantic(QStringLiteral("LowerArm"), side, 0.91f);
        }
        if (containsAny(compact, { "hand", "wrist", "手首", "手" }) &&
            !containsAny(compact, { "finger", "thumb", "index", "middle", "ring", "little", "pinky" })) {
            return sideSemantic(QStringLiteral("Hand"), side, 0.94f);
        }

        if (fingerMatches(compact, { "thumb", "親指", "拇" })) {
            return fingerSemantic(QStringLiteral("Thumb"), compact, side, 0.78f);
        }
        if (fingerMatches(compact, { "index", "fore", "人指", "人差" })) {
            return fingerSemantic(QStringLiteral("Index"), compact, side, 0.78f);
        }
        if (fingerMatches(compact, { "middle", "中指" })) {
            return fingerSemantic(QStringLiteral("Middle"), compact, side, 0.78f);
        }
        if (fingerMatches(compact, { "ring", "薬指", "无名" })) {
            return fingerSemantic(QStringLiteral("Ring"), compact, side, 0.78f);
        }
        if (fingerMatches(compact, { "little", "pinky", "小指" })) {
            return fingerSemantic(QStringLiteral("Little"), compact, side, 0.78f);
        }

        if (containsAny(compact, { "lower leg", "lowerleg", "calf", "shin", "knee", "ひざ", "膝" }) ||
            (containsAny(compact, { "leg" }) && containsAny(compact, { "lower" })) ||
            compact == QStringLiteral("leftleg") ||
            compact == QStringLiteral("rightleg") ||
            compact == QStringLiteral("l leg") ||
            compact == QStringLiteral("r leg")) {
            return sideSemantic(QStringLiteral("LowerLeg"), side, 0.91f);
        }
        if (containsAny(compact, { "upper leg", "upperleg", "up leg", "upleg", "thigh", "太もも", "腿", "足" }) &&
            !containsAny(compact, { "lower", "calf", "shin", "foot", "toe", "ankle", "knee" })) {
            return sideSemantic(QStringLiteral("UpperLeg"), side, 0.90f);
        }
        if (containsAny(compact, { "foot", "ankle", "足首" }) &&
            !containsAny(compact, { "toe", "つま先" })) {
            return sideSemantic(QStringLiteral("Foot"), side, 0.94f);
        }
        if (containsAny(compact, { "toe", "toes", "つま先" })) {
            return sideSemantic(QStringLiteral("Toes"), side, 0.84f);
        }
    }

    return semantic(QString(), RigSide::Unknown, 0.0f);
}

} // namespace haorendergi
