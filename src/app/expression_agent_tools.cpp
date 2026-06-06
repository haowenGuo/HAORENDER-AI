#include "app/expression_agent_tools.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace haorendergi {
namespace {

bool containsAny(const QString& text, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (text.contains(QString::fromUtf8(needle), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool sceneHasExpression(const SceneModel& scene, const QString& name) {
    return std::any_of(scene.vrm_expressions.begin(), scene.vrm_expressions.end(), [&](const VrmExpressionData& expression) {
        return QString::fromStdString(expression.name).compare(name, Qt::CaseInsensitive) == 0;
    });
}

QString resolveExpressionName(const SceneModel& scene, std::initializer_list<const char*> candidates) {
    for (const char* candidate : candidates) {
        const QString name = QString::fromUtf8(candidate);
        if (sceneHasExpression(scene, name)) {
            return name;
        }
    }
    return QString();
}

ExpressionChannelData makeChannel(const QString& name, std::initializer_list<ScalarKeyframe> keys) {
    ExpressionChannelData channel;
    channel.name = name.toStdString();
    channel.weights.assign(keys.begin(), keys.end());
    return channel;
}

void addHoldExpression(ExpressionCurvePlan* plan, const QString& name, float weight, double begin, double end) {
    if (!plan || name.isEmpty() || weight <= 0.0f) {
        return;
    }
    plan->channels.push_back(makeChannel(name, {
        ScalarKeyframe{ 0.00, 0.0f },
        ScalarKeyframe{ begin, 0.0f },
        ScalarKeyframe{ begin + 0.12, weight },
        ScalarKeyframe{ end - 0.12, weight },
        ScalarKeyframe{ end, 0.0f },
        ScalarKeyframe{ plan->duration_seconds, 0.0f }
    }));
}

} // namespace

bool isEyeDebugExpressionName(const QString& name) {
    return name.compare(QStringLiteral("blink"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("blinkLeft"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("blinkRight"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("lookUp"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("lookDown"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("lookLeft"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("lookRight"), Qt::CaseInsensitive) == 0 ||
           name.startsWith(QStringLiteral("Fcl_EYE_"), Qt::CaseInsensitive);
}

QString eyeExpressionDisplayLabel(const QString& name) {
    if (name.compare(QStringLiteral("blink"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("blink");
    }
    if (name.compare(QStringLiteral("blinkLeft"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("blink L");
    }
    if (name.compare(QStringLiteral("blinkRight"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("blink R");
    }
    if (name.startsWith(QStringLiteral("Fcl_EYE_"), Qt::CaseInsensitive)) {
        return name.mid(QStringLiteral("Fcl_EYE_").size()).replace(QLatin1Char('_'), QLatin1Char(' '));
    }
    if (name.startsWith(QStringLiteral("look"), Qt::CaseInsensitive)) {
        return name;
    }
    return name;
}

QStringList availableEyeExpressionNames(const SceneModel& scene) {
    QStringList names;
    for (const VrmExpressionData& expression : scene.vrm_expressions) {
        const QString name = QString::fromStdString(expression.name);
        if (!isEyeDebugExpressionName(name) || names.contains(name, Qt::CaseInsensitive)) {
            continue;
        }
        names.push_back(name);
    }
    std::sort(names.begin(), names.end(), [](const QString& lhs, const QString& rhs) {
        const auto sortKey = [](const QString& name) {
            if (name.compare(QStringLiteral("blink"), Qt::CaseInsensitive) == 0) {
                return 0;
            }
            if (name.compare(QStringLiteral("blinkLeft"), Qt::CaseInsensitive) == 0) {
                return 1;
            }
            if (name.compare(QStringLiteral("blinkRight"), Qt::CaseInsensitive) == 0) {
                return 2;
            }
            if (name.startsWith(QStringLiteral("look"), Qt::CaseInsensitive)) {
                return 3;
            }
            if (name.startsWith(QStringLiteral("Fcl_EYE_Close"), Qt::CaseInsensitive)) {
                return 4;
            }
            if (name.startsWith(QStringLiteral("Fcl_EYE_"), Qt::CaseInsensitive)) {
                return 5;
            }
            return 10;
        };
        const int lhs_key = sortKey(lhs);
        const int rhs_key = sortKey(rhs);
        if (lhs_key != rhs_key) {
            return lhs_key < rhs_key;
        }
        return QString::compare(lhs, rhs, Qt::CaseInsensitive) < 0;
    });
    return names;
}

bool hasEyeGazeSolver(const SceneModel& scene) {
    return scene.vrm_look_at.left_eye_node_index >= 0 || scene.vrm_look_at.right_eye_node_index >= 0;
}

ExpressionCurvePlan buildSoftBlinkCurve(const SceneModel& scene) {
    ExpressionCurvePlan plan;
    const QString blink_name = resolveExpressionName(scene, { "blink", "Fcl_EYE_Close" });
    if (blink_name.isEmpty()) {
        return plan;
    }

    plan.summary = QStringLiteral("Soft blink expression curve");
    plan.duration_seconds = 4.0;
    plan.channels.push_back(makeChannel(blink_name, {
        ScalarKeyframe{ 0.00, 0.0f },
        ScalarKeyframe{ 0.32, 0.0f },
        ScalarKeyframe{ 0.40, 1.0f },
        ScalarKeyframe{ 0.48, 0.0f },
        ScalarKeyframe{ 2.40, 0.0f },
        ScalarKeyframe{ 2.48, 1.0f },
        ScalarKeyframe{ 2.58, 0.0f },
        ScalarKeyframe{ 4.00, 0.0f }
    }));
    return plan;
}

ExpressionCurvePlan buildExpressionCurveForPrompt(const SceneModel& scene, const QString& prompt) {
    ExpressionCurvePlan plan;
    if (availableEyeExpressionNames(scene).isEmpty()) {
        return plan;
    }

    const QString text = prompt.trimmed();
    if (!containsAny(text, { "eye", "eyes", "gaze", "blink", "expression", "look at", "look left", "look right",
                             "眼", "眼神", "眨", "眨眼", "表情", "神情", "视线", "看向", "闭眼", "瞳孔", "高光" })) {
        return plan;
    }

    plan.summary = QStringLiteral("AI eye/expression curve");
    plan.duration_seconds = 4.0;

    ExpressionCurvePlan blink = buildSoftBlinkCurve(scene);
    if (blink.valid()) {
        plan.channels.insert(plan.channels.end(), blink.channels.begin(), blink.channels.end());
    }

    QString mood_name;
    float mood_weight = 0.35f;
    if (containsAny(text, { "happy", "joy", "smile", "开心", "高兴", "温柔", "柔和", "可爱", "甜" })) {
        mood_name = resolveExpressionName(scene, { "Fcl_EYE_Joy", "Fcl_EYE_Fun", "Fcl_EYE_Natural" });
        mood_weight = 0.42f;
    } else if (containsAny(text, { "angry", "serious", "sharp", "生气", "严肃", "锐利" })) {
        mood_name = resolveExpressionName(scene, { "Fcl_EYE_Angry" });
        mood_weight = 0.45f;
    } else if (containsAny(text, { "sad", "sorrow", "melancholy", "悲伤", "难过", "忧郁" })) {
        mood_name = resolveExpressionName(scene, { "Fcl_EYE_Sorrow" });
        mood_weight = 0.45f;
    } else if (containsAny(text, { "surprise", "surprised", "惊讶", "吃惊" })) {
        mood_name = resolveExpressionName(scene, { "Fcl_EYE_Surprised", "Fcl_EYE_Spread" });
        mood_weight = 0.55f;
    } else if (containsAny(text, { "natural", "neutral", "正常", "自然" })) {
        mood_name = resolveExpressionName(scene, { "Fcl_EYE_Natural" });
        mood_weight = 0.35f;
    }
    addHoldExpression(&plan, mood_name, mood_weight, 0.15, 3.85);

    if (containsAny(text, { "hide highlight", "dead eye", "失去高光", "无高光", "黑眼", "死鱼眼" })) {
        addHoldExpression(&plan, resolveExpressionName(scene, { "Fcl_EYE_Highlight_Hide" }), 0.8f, 0.15, 3.85);
    }
    if (containsAny(text, { "hide iris", "small pupil", "隐藏瞳孔", "收缩瞳孔" })) {
        addHoldExpression(&plan, resolveExpressionName(scene, { "Fcl_EYE_Iris_Hide" }), 0.65f, 0.15, 3.85);
    }
    if (containsAny(text, { "look left", "左看", "看左", "向左" })) {
        addHoldExpression(&plan, resolveExpressionName(scene, { "lookLeft" }), 0.65f, 0.20, 3.80);
    } else if (containsAny(text, { "look right", "右看", "看右", "向右" })) {
        addHoldExpression(&plan, resolveExpressionName(scene, { "lookRight" }), 0.65f, 0.20, 3.80);
    } else if (containsAny(text, { "look up", "上看", "看上", "向上" })) {
        addHoldExpression(&plan, resolveExpressionName(scene, { "lookUp" }), 0.55f, 0.20, 3.80);
    } else if (containsAny(text, { "look down", "下看", "看下", "向下" })) {
        addHoldExpression(&plan, resolveExpressionName(scene, { "lookDown" }), 0.55f, 0.20, 3.80);
    }

    if (plan.channels.empty()) {
        return ExpressionCurvePlan();
    }
    return plan;
}

GazeCurvePlan buildGazeCurveForPrompt(const SceneModel& scene, const QString& prompt) {
    GazeCurvePlan plan;
    if (!hasEyeGazeSolver(scene)) {
        return plan;
    }

    const QString text = prompt.trimmed();
    if (!containsAny(text, { "eye", "eyes", "gaze", "look at", "look left", "look right", "look up", "look down",
                             "眼", "眼神", "视线", "看向", "左看", "右看", "上看", "下看", "瞳孔" })) {
        return plan;
    }

    float yaw = 0.0f;
    float pitch = 0.0f;
    if (containsAny(text, { "look left", "左看", "看左", "向左" })) {
        yaw = -12.0f;
    } else if (containsAny(text, { "look right", "右看", "看右", "向右" })) {
        yaw = 12.0f;
    }
    if (containsAny(text, { "look up", "上看", "看上", "向上" })) {
        pitch = 7.0f;
    } else if (containsAny(text, { "look down", "下看", "看下", "向下", "垂眼" })) {
        pitch = -7.0f;
    }

    if (std::abs(yaw) <= 1e-4f && std::abs(pitch) <= 1e-4f &&
        containsAny(text, { "gentle", "soft", "温柔", "柔和", "自然", "可爱" })) {
        yaw = 4.0f;
        pitch = -2.0f;
    }
    if (std::abs(yaw) <= 1e-4f && std::abs(pitch) <= 1e-4f) {
        return plan;
    }

    plan.summary = QStringLiteral("AI eye bone gaze curve");
    plan.duration_seconds = 4.0;
    plan.keys = {
        EyeGazeKeyframeData{ 0.00, 0.0f, 0.0f, 0.0f },
        EyeGazeKeyframeData{ 0.25, yaw, pitch, 0.0f },
        EyeGazeKeyframeData{ 0.55, yaw, pitch, 1.0f },
        EyeGazeKeyframeData{ 3.35, yaw, pitch, 1.0f },
        EyeGazeKeyframeData{ 3.80, 0.0f, 0.0f, 0.0f },
        EyeGazeKeyframeData{ 4.00, 0.0f, 0.0f, 0.0f }
    };
    return plan;
}

} // namespace haorendergi
