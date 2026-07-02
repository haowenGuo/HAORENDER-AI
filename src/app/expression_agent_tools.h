#pragma once

#include "scene/scene_types.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace haorendergi {

struct ExpressionCurvePlan {
    QString summary;
    double duration_seconds = 0.0;
    std::vector<ExpressionChannelData> channels;

    bool valid() const {
        return duration_seconds > 0.0 && !channels.empty();
    }
};

struct GazeCurvePlan {
    QString summary;
    double duration_seconds = 0.0;
    std::vector<EyeGazeKeyframeData> keys;

    bool valid() const {
        return duration_seconds > 0.0 && !keys.empty();
    }
};

bool isEyeDebugExpressionName(const QString& name);
QString eyeExpressionDisplayLabel(const QString& name);
QStringList availableEyeExpressionNames(const SceneModel& scene);
bool hasEyeGazeSolver(const SceneModel& scene);
ExpressionCurvePlan buildSoftBlinkCurve(const SceneModel& scene);
ExpressionCurvePlan buildExpressionCurveForPrompt(const SceneModel& scene, const QString& prompt);
GazeCurvePlan buildGazeCurveForPrompt(const SceneModel& scene, const QString& prompt);

} // namespace haorendergi
