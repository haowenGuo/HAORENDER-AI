#pragma once

#include "app/lookdev_ai.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace haorendergi {

struct LookDevLlmConfig {
    QString api_key;
    QString base_url;
    QString model;
    int timeout_ms = 90000;
};

struct LookDevLlmResult {
    bool ok = false;
    bool needs_user_input = false;
    QString assistant_reply;
    QString next_question;
    QString error_message;
    QVector<AiRecommendationCandidate> candidates;
    std::vector<ExpressionChannelData> expression_channels;
    double expression_duration_seconds = 0.0;
    QString expression_summary;
    std::vector<EyeGazeKeyframeData> gaze_keys;
    double gaze_duration_seconds = 0.0;
    QString gaze_summary;
};

class LookDevLlmClient final : public QObject {
public:
    using Callback = std::function<void(const LookDevLlmResult&)>;

    explicit LookDevLlmClient(QObject* parent = nullptr);

    void requestRecommendations(const LookDevLlmConfig& config,
                                const QString& prompt,
                                const QString& dialogue_context,
                                const LookDevSettings& base_settings,
                                const QStringList& available_expression_names,
                                Callback callback);

private:
    QNetworkAccessManager network_;
};

LookDevLlmConfig lookDevLlmConfigFromEnvironment();
QString defaultDoubaoBaseUrl();
QString defaultDoubaoModel();
QString lookDevParameterSkillPrompt();

} // namespace haorendergi
