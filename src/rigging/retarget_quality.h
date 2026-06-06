#pragma once

#include "rigging/ai_bone_mapper.h"
#include "scene/scene_types.h"

#include <QString>
#include <QVector>

namespace haorendergi {

enum class RetargetQualitySeverity {
    Info = 0,
    Warning = 1,
    Error = 2
};

struct RetargetQualityIssue {
    RetargetQualitySeverity severity = RetargetQualitySeverity::Info;
    QString code;
    QString message;
    double value = 0.0;
};

struct RetargetQualityReport {
    double score = 100.0;
    QString grade = QStringLiteral("A");
    double root_motion_ratio = 0.0;
    double foot_float_ratio = 0.0;
    double shoulder_collapse_ratio = 1.0;
    int wrist_flip_count = 0;
    int eye_reverse_count = 0;
    int missing_major_channels = 0;
    QVector<RetargetQualityIssue> issues;
};

RetargetQualityReport scoreRetargetedAnimation(const SceneModel& target_bind_scene,
                                               const SceneModel& retargeted_scene,
                                               const BoneMappingResult& mapping_result);

RetargetQualityReport scoreRetargetedAnimation(const SceneModel& source_animation,
                                               const SceneModel& target_bind_scene,
                                               const SceneModel& retargeted_scene,
                                               const BoneMappingResult& mapping_result);

QString retargetQualitySummaryText(const RetargetQualityReport& report, bool chinese);
QString retargetQualityIssueCodes(const RetargetQualityReport& report);

} // namespace haorendergi
