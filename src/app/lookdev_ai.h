#pragma once

#include "rendering/render_backend.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace haorendergi {

enum class StylePresetFocus {
    Phong = 0,
    Toon = 1,
    Hybrid = 2,
    PbrAssist = 3
};

struct StylePreset {
    QString name;
    QString description;
    QString source_prompt;
    StylePresetFocus focus = StylePresetFocus::Phong;
    RenderPipeline preferred_pipeline = RenderPipeline::Raster;
    LookDevSettings settings;
};

struct AiRecommendationCandidate {
    QString slot_label;
    StylePreset preset;
    QString summary;
};

QJsonObject stylePresetToJson(const StylePreset& preset);
bool stylePresetFromJson(const QJsonObject& object, StylePreset* preset, QString* error_message = nullptr);

QVector<AiRecommendationCandidate> buildLookDevRecommendations(const QString& prompt, const LookDevSettings& base_settings);

QString stylePresetFocusLabel(StylePresetFocus focus);
QString summarizeStylePreset(const StylePreset& preset);

} // namespace haorendergi
