#pragma once

#include "rigging/ai_bone_mapper.h"
#include "scene/scene_types.h"

#include <Eigen/Dense>

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace haorendergi {

enum class RetargetIssueSeverity {
    Info = 0,
    Warning = 1,
    Error = 2
};

struct RetargetIssue {
    RetargetIssueSeverity severity = RetargetIssueSeverity::Info;
    QString code;
    QString message;
};

struct RetargetSegmentSample {
    QString label;
    QString from_canonical;
    QString to_canonical;
    QString source_from;
    QString source_to;
    QString target_from;
    QString target_to;
    Eigen::Vector3f source_direction = Eigen::Vector3f::Zero();
    Eigen::Vector3f target_direction = Eigen::Vector3f::Zero();
    float direction_dot = 0.0f;
    bool complete = false;
};

struct RetargetAxisSample {
    QString canonical_name;
    QString source_bone;
    QString target_bone;
    Eigen::Vector3f signed_axis_dots = Eigen::Vector3f::Zero();
    float min_abs_axis_dot = 0.0f;
    bool roll_risk = false;
};

struct RetargetProfile {
    float source_height = 1.0f;
    float target_height = 1.0f;
    float translation_scale = 1.0f;
    int mapped_count = 0;
    int unmapped_source_count = 0;
    int unmapped_target_count = 0;
    int high_roll_risk_count = 0;
    QVector<RetargetSegmentSample> segment_samples;
    QVector<RetargetAxisSample> axis_samples;
    QVector<RetargetIssue> issues;
};

RetargetProfile buildRetargetProfile(const SceneModel& source_animation,
                                     const SceneModel& target_bind_scene,
                                     const BoneMappingResult& mapping_result);

QString retargetProfileSummaryText(const RetargetProfile& profile, bool chinese);
QString retargetProfileDetailedText(const RetargetProfile& profile, bool chinese, int max_issues = 8);
QJsonObject retargetProfileToJson(const RetargetProfile& profile);

} // namespace haorendergi
