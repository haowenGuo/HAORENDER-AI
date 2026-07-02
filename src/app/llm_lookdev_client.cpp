#include "app/llm_lookdev_client.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <memory>

namespace haorendergi {
namespace {

QString unquoteEnvValue(QString value) {
    value = value.trimmed();
    if (value.size() >= 2) {
        const QChar first = value.front();
        const QChar last = value.back();
        if ((first == QLatin1Char('"') && last == QLatin1Char('"')) ||
            (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
            value = value.mid(1, value.size() - 2);
        }
    }
    return value.trimmed();
}

QMap<QString, QString> parseLocalEnvFile(const QString& path) {
    QMap<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return values;
    }

    const QString text = QString::fromUtf8(file.readAll());
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.startsWith(QStringLiteral("export "))) {
            line = line.mid(7).trimmed();
        }

        const int equal_index = line.indexOf(QLatin1Char('='));
        if (equal_index <= 0) {
            continue;
        }

        const QString key = line.left(equal_index).trimmed();
        const QString value = unquoteEnvValue(line.mid(equal_index + 1));
        if (!key.isEmpty() && !value.isEmpty()) {
            values.insert(key, value);
        }
    }
    return values;
}

QMap<QString, QString> loadLocalLlmEnvValues() {
    QStringList candidates;
    const QString config_dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!config_dir.isEmpty()) {
        candidates << QDir(config_dir).filePath(QStringLiteral("llm.env"));
    }

    const QString app_dir = QCoreApplication::applicationDirPath();
    if (!app_dir.isEmpty()) {
        QDir dir(app_dir);
        candidates << dir.filePath(QStringLiteral("llm.env.local"));
        candidates << dir.absoluteFilePath(QStringLiteral("../llm.env.local"));
        candidates << dir.absoluteFilePath(QStringLiteral("../../llm.env.local"));
    }

    QDir current = QDir::current();
    candidates << current.filePath(QStringLiteral("llm.env.local"));
    candidates << current.absoluteFilePath(QStringLiteral("../llm.env.local"));
    candidates << current.absoluteFilePath(QStringLiteral("../../llm.env.local"));

    QMap<QString, QString> merged;
    QStringList visited;
    for (const QString& candidate : candidates) {
        const QString path = QDir::cleanPath(candidate);
        if (visited.contains(path)) {
            continue;
        }
        visited << path;

        const QMap<QString, QString> values = parseLocalEnvFile(path);
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (!merged.contains(it.key())) {
                merged.insert(it.key(), it.value());
            }
        }
    }
    return merged;
}

QString envOrLocalValue(const QMap<QString, QString>& local_values,
                        const QStringList& names,
                        const QString& fallback = QString()) {
    for (const QString& name : names) {
        const QByteArray name_bytes = name.toUtf8();
        const QByteArray value = qgetenv(name_bytes.constData());
        if (!value.isEmpty()) {
            return QString::fromUtf8(value);
        }
    }
    for (const QString& name : names) {
        const QString value = local_values.value(name).trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return fallback;
}

int envOrLocalInt(const QMap<QString, QString>& local_values, const QString& name, int fallback) {
    bool ok = false;
    const QString value = envOrLocalValue(local_values, { name });
    const int parsed = value.toInt(&ok);
    return ok ? parsed : fallback;
}

QString chatCompletionsUrl(QString base_url) {
    base_url = base_url.trimmed();
    while (base_url.endsWith(QLatin1Char('/'))) {
        base_url.chop(1);
    }
    if (base_url.endsWith(QStringLiteral("/chat/completions"))) {
        return base_url;
    }
    return base_url + QStringLiteral("/chat/completions");
}

QJsonObject colorSchemaNote() {
    QJsonObject color;
    color.insert(QStringLiteral("r"), QStringLiteral("0.0-1.0 float"));
    color.insert(QStringLiteral("g"), QStringLiteral("0.0-1.0 float"));
    color.insert(QStringLiteral("b"), QStringLiteral("0.0-1.0 float"));
    return color;
}

QString lookDevStyleKnowledgeBasePrompt() {
    return QString::fromUtf8(R"(
Style knowledge base:
Use this as the agent's taste memory. Translate vague art words into concrete renderer controls before writing the preset.

Mature rendering skill loop:
1. Infer the target family first: PBR product/material, soft game anime, hard cel, figure showcase, painterly, technical illustration, or readability-first stylization.
2. Pick exactly one dominant skill and at most two modifiers. Do not mix every attractive effect together.
3. Convert the skill into HaoRender-GI controls. Respect missing controls; if edited normals, matcap, SSS, face masks, material roughness/base color, transmission, or per-material metallic factors are needed, mention the limitation in assistantReply and approximate with exposed controls.
4. Avoid the ugly stack: high rim + high outline + high specular + high ambient at the same time. For toon/anime, also avoid pure black shadows and excessive normalStrength.
5. If the user says "原神", "二游", "柔和", or "温和", choose the Soft Game Anime skill unless they explicitly ask for hard cel, PBR realism, or product rendering.

Eye/expression skill:
- Use expressionCurve only when the user asks for eye direction, gaze, blink, eyelid, facial expression, emotion, or when the current rig preview needs an eye animation helper. Do not add expression animation for a pure material/rendering request.
- Prefer "blink" for both eyes, "blinkLeft"/"blinkRight" for asymmetric eyelids, and direct VRM morphs such as Fcl_EYE_Joy, Fcl_EYE_Angry, Fcl_EYE_Sorrow, Fcl_EYE_Surprised, Fcl_EYE_Natural, Fcl_EYE_Iris_Hide, and Fcl_EYE_Highlight_Hide for eye mood.
- Use only expression names from the available-expression list in the user message. If a desired control is missing, explain the limitation in assistantReply and use the closest available expression.
- Blink curves should close quickly and reopen quickly: 0 -> 1 -> 0 within about 0.12-0.20 seconds. Mood expressions should ease in and hold at 0.25-0.60 unless the user asks for extreme emotion.
- Keep expression weights 0.0-1.0 and durationSeconds usually 2.0-6.0.
- If the scene has eye bone gaze solver, use gazeCurve for actual eyeball direction. Use expressionCurve for eyelids/emotion and gazeCurve for look direction. Keep natural anime gaze around yaw -18..18 degrees and pitch -12..12 degrees unless the user asks for an extreme pose.

Mature rendering skills:
- UTS/MToon Soft Game Anime: based on separated lit/shade colors, shading steps, feather, highlight, rim, and outline controls. Use Phong + Toon, 3-4 soft bands, colored shadows, cream highlights, gentle rim, modest cool-gray outline, shadowFloor/litFloor to prevent dead blacks, slight rampBias, and materialOverride for dark texture lift. This is the default for soft/Genshin-like game character requests.
- ArcSys hard 2D cel approximation: use Phong + Toon, hardSpecular true, very low fill, 2-3 hard bands, sharp shadow/highlight boundaries, stronger outline. Explain that true ArcSys-like quality also needs edited normals/model/camera work that HaoRender-GI does not expose yet.
- Figure showcase: stylized character product shot. Keep toon bands soft but clean, use brighter exposure, controlled glossy cream highlights, subtle outline, and stronger rim separation. Do not combine thick outline with mirror-like highlights.
- Painterly hand-painted: 4-6 soft bands, desaturated colored shadows, low specular, low outline opacity, lower normalStrength, more fill. Avoid technical sharpness.
- TF2/readability-first: prioritize silhouette readability with rim light, luminance separation, and hue variation. Use little or no black outline; do not crush shadows.
- Gooch technical illustration: cool blue shadows, warm cream highlights, preserved outline, no black shadows. It is for structure/readability, not anime beauty.
- PBR product/material guardrails: keep PBR, tune only exposed PBR controls, and do not invent material parameters. Metallic/roughness/baseColor editing is not exposed in the current UI.

Style profiles:
- PBR Neutral LookDev / 写实材质检查: keep shadingModel PBR, preferredPipeline Raster, toon disabled, outline disabled, exposure 1.00-1.18, normalStrength 0.90-1.10, enableShadows true, enableBackfaceCulling true, iblEnabled true, iblDiffuseStrength 0.50-0.85, iblSpecularStrength 0.65-1.05, skyLightStrength 0.12-0.32. Preserve packed channel indices unless the user explicitly mentions texture packing. This profile is for material inspection, not stylization.
- PBR Soft Studio / 柔和棚拍: keep PBR, exposure 1.08-1.28, normalStrength 0.85-1.05, iblDiffuseStrength 0.75-1.20, iblSpecularStrength 0.55-0.90, skyLightStrength 0.25-0.55, shadows on but not visually crushed. Use when the artist says soft, clean, product preview, gentle light, or not too contrasty while currently in PBR.
- PBR Contrast Product / 高级产品写实: keep PBR, exposure 0.92-1.12, iblDiffuseStrength 0.20-0.55, iblSpecularStrength 0.95-1.45, skyLightStrength 0.05-0.20, normalStrength 0.95-1.20, shadows on, backface culling on. Use for premium, metal, glossy product, black background, or stronger reflection separation.
- PBR Warm/Cool Art Direction / PBR冷暖调: keep PBR. Since the current PBR controls do not expose per-material baseColor/roughness/metallic multipliers, approximate color mood through exposure, IBL diffuse/specular balance, skyLightStrength, and background choice if available. Do not fake toon shadow bands in PBR.
- Soft Anime / 二游柔和 / Genshin-like / 温和 / 柔和: Raster + Phong + Toon, useTonemap true, hardSpecular false, primaryLightOnly false, exposure 1.05-1.24, normalStrength 0.70-1.00, secondaryLightScale 0.25-0.48, ambientStrength 0.07-0.16, diffuseStrength 1.00-1.14, specularStrength 0.20-0.42, smoothness 0.60-0.82, shininess 38-72, diffuseSteps 3-4, diffuseSoftness 0.08-0.15, shadowFloor 0.08-0.18, litFloor 0.42-0.58, rampBias -0.03-0.06, rampContrast 0.75-1.10, shadowMapStrength 0.20-0.50, shadowThreshold 0.36-0.48, shadowSoftness 0.11-0.20, shadowTint cool blue/violet around {0.62,0.68,0.94} or warm peach for skin, highlightThreshold 0.32-0.46, highlightSoftness 0.07-0.13, highlightStrength 0.35-0.70, highlightTint cream white around {1.0,0.97,0.92}, rimStrength 0.18-0.45, rimPower 1.8-3.0, rimThreshold 0.25-0.40, rimSoftness 0.08-0.16, materialOverrideEnabled true, materialTextureStrength 0.70-0.92, materialLift 0.03-0.10, materialSaturation 0.90-1.15, materialContrast 0.75-1.05, outline width 0.8-1.8, opacity 0.35-0.70, dark cool gray outline. Avoid black shadows, hard specular, very thick outlines, and pushing rim/specular both high.
- Graphic Cel / TV Anime / 赛璐璐: hardSpecular true, diffuseSteps 2-3, diffuseSoftness 0.00-0.04, shadowThreshold 0.42-0.56, shadowSoftness 0.00-0.05, highlightThreshold 0.24-0.38, highlightSoftness 0.00-0.04, highlightStrength 0.30-0.75, rimStrength 0.15-0.45, rimPower 2.5-5.0, outline width 2.0-4.5, opacity 0.75-1.0. Avoid too much fill because it erases cel clarity.
- Figure Showcase / 手办 / 宣传图: exposure 1.08-1.30, secondaryLightScale 0.22-0.42, ambientStrength 0.04-0.10, specularStrength 0.45-0.85, smoothness 0.82-0.96, shininess 76-128, diffuseSteps 3-4, diffuseSoftness 0.04-0.10, shadowSoftness 0.06-0.12, highlightStrength 0.65-1.10, rimStrength 0.30-0.70, outline width 0.8-1.8, opacity 0.45-0.75. Avoid thick outline plus mirror-like highlight.
- Painterly / Hand-painted / 厚涂 / 手绘: hardSpecular false, diffuseSteps 4-6, diffuseSoftness 0.10-0.20, shadowSoftness 0.14-0.24, desaturated colored shadowTint, highlightStrength 0.20-0.50, highlightSoftness 0.10-0.18, rimStrength 0.10-0.35, outline width 0.5-1.4, opacity 0.25-0.55, ambientStrength 0.08-0.18. Avoid high outline opacity and sharp glossy highlights.
- Illustrative Readability / TF2-like / 可读性: toon optional or mild, diffuseStrength 1.05-1.25, ambientStrength 0.10-0.22, secondaryLightScale 0.22-0.45, specularStrength 0.35-0.75, smoothness 0.65-0.85, shininess 36-76, rimStrength 0.35-0.85, rimPower 1.5-3.5, outline 0.0-1.2. Prefer rim and luminance separation over heavy black lines.
- Gooch / technical illustration / 技术插画: toon enabled, diffuseSteps 4-6, diffuseSoftness 0.08-0.18, cool blue shadowTint around {0.45,0.58,0.95}, warm cream highlightTint around {1.0,0.88,0.62}, ambientStrength 0.12-0.24, specularStrength 0.18-0.45, outline width 1.0-2.5, opacity 0.65-0.90. Never use fully black shadows.
- Clay / matte review / 白模: toon disabled, exposure 1.00-1.18, ambientStrength 0.12-0.25, secondaryLightScale 0.20-0.45, specularStrength 0.05-0.18, smoothness 0.20-0.45, shininess 12-36, rimStrength 0.08-0.22, outline off or subtle.
- Dark Premium Product / 高级黑底产品: toon usually disabled, exposure 0.85-1.10, ambientStrength 0.00-0.06, secondaryLightScale 0.08-0.20, specularStrength 0.60-1.00, smoothness 0.80-0.98, shininess 80-128, rimStrength 0.50-1.10, rimPower 1.2-2.6, outline usually off. Avoid high ambient fill.

Refinement ladder:
- Too hard / 太硬: set hardSpecular false; raise diffuseSoftness, shadowSoftness, highlightSoftness; lower outline opacity.
- Too blurry / 太糊: lower softness; reduce ambientStrength; raise outline opacity slightly; keep bands readable.
- Too dark / 太黑: raise exposure, ambientStrength, secondaryLightScale; lighten shadowTint; avoid black shadows.
- Too gray / 太灰: raise diffuseStrength; make shadowTint more chromatic; reduce muddy ambient.
- Too plastic / 太塑料: lower specularStrength, smoothness, highlightStrength; warm or desaturate highlightTint.
- Not anime enough / 不够二次元: enable Toon, use 3-step bands, add controlled outline, use colored shadowTint.
- Not premium enough / 不够高级: reduce outline, lower ambient, clean up specular, strengthen rim separation.
- Dirty face/skin / 脸脏: lower shadow darkness, warm shadowTint, raise fill light, avoid hard black bands.
- Weak silhouette / 轮廓不清楚: increase outline width/opacity OR rimStrength, but do not max both.
- Highlight too large / 高光太大: raise highlightThreshold, lower highlightStrength, slightly raise shininess.
- PBR looks weird / PBR奇怪: first keep shadingModel PBR, disable toon/outline influence, normalize exposure 0.95-1.20, normalStrength 0.85-1.10, iblDiffuseStrength 0.45-0.95, iblSpecularStrength 0.65-1.15, skyLightStrength 0.10-0.35. If material roughness/metallic itself is wrong, say current controls cannot directly rewrite material factors.

Question policy:
- If the request is vague, ask one question about the most decisive visual axis: soft vs hard bands, outline strength, highlight size/material feel, cool vs warm shadows, product/figure vs in-game character, or face readability.
- If the user gives a style name plus a modifier, generate one preset immediately.
- If the user critiques an applied result, refine the current settings instead of restarting from defaults.
)");
}

QString jsonDocumentToCompactString(const QJsonDocument& document) {
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

QString extractJsonObjectText(const QString& text) {
    QString cleaned = text.trimmed();
    if (cleaned.startsWith(QStringLiteral("```"))) {
        const int first_newline = cleaned.indexOf(QLatin1Char('\n'));
        const int last_fence = cleaned.lastIndexOf(QStringLiteral("```"));
        if (first_newline >= 0 && last_fence > first_newline) {
            cleaned = cleaned.mid(first_newline + 1, last_fence - first_newline - 1).trimmed();
        }
    }

    const int begin = cleaned.indexOf(QLatin1Char('{'));
    const int end = cleaned.lastIndexOf(QLatin1Char('}'));
    if (begin < 0 || end <= begin) {
        return cleaned;
    }
    return cleaned.mid(begin, end - begin + 1);
}

bool parseExpressionKey(const QJsonValue& value, ScalarKeyframe* key) {
    if (!key || !value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    const double time_seconds = object.contains(QStringLiteral("timeSeconds"))
        ? object.value(QStringLiteral("timeSeconds")).toDouble()
        : object.value(QStringLiteral("time")).toDouble();
    const double weight_value = object.contains(QStringLiteral("weight"))
        ? object.value(QStringLiteral("weight")).toDouble()
        : object.value(QStringLiteral("value")).toDouble();
    if (!std::isfinite(time_seconds) || !std::isfinite(weight_value) || time_seconds < 0.0) {
        return false;
    }
    key->time_ticks = std::clamp(time_seconds, 0.0, 60.0);
    key->value = static_cast<float>(std::clamp(weight_value, 0.0, 1.0));
    return true;
}

std::vector<ExpressionChannelData> parseExpressionChannels(const QJsonArray& channels_array,
                                                           QString* error_message) {
    std::vector<ExpressionChannelData> channels;
    for (const QJsonValue& channel_value : channels_array) {
        if (!channel_value.isObject()) {
            continue;
        }
        const QJsonObject channel_object = channel_value.toObject();
        const QString name = channel_object.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            continue;
        }

        QJsonArray keys_array = channel_object.value(QStringLiteral("keys")).toArray();
        if (keys_array.isEmpty()) {
            keys_array = channel_object.value(QStringLiteral("weights")).toArray();
        }
        ExpressionChannelData channel;
        channel.name = name.toStdString();
        channel.weights.reserve(keys_array.size());
        for (const QJsonValue& key_value : keys_array) {
            ScalarKeyframe key;
            if (parseExpressionKey(key_value, &key)) {
                channel.weights.push_back(key);
            }
        }
        std::sort(channel.weights.begin(), channel.weights.end(), [](const ScalarKeyframe& lhs, const ScalarKeyframe& rhs) {
            return lhs.time_ticks < rhs.time_ticks;
        });
        if (!channel.weights.empty()) {
            channels.push_back(std::move(channel));
        }
    }
    if (channels.empty() && error_message) {
        *error_message = QStringLiteral("Expression curve did not contain valid channels.");
    }
    return channels;
}

bool parseGazeKey(const QJsonValue& value, EyeGazeKeyframeData* key) {
    if (!key || !value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    const double time_seconds = object.contains(QStringLiteral("timeSeconds"))
        ? object.value(QStringLiteral("timeSeconds")).toDouble()
        : object.value(QStringLiteral("time")).toDouble();
    const double yaw_degrees = object.contains(QStringLiteral("yawDegrees"))
        ? object.value(QStringLiteral("yawDegrees")).toDouble()
        : object.value(QStringLiteral("yaw")).toDouble();
    const double pitch_degrees = object.contains(QStringLiteral("pitchDegrees"))
        ? object.value(QStringLiteral("pitchDegrees")).toDouble()
        : object.value(QStringLiteral("pitch")).toDouble();
    const double weight = object.contains(QStringLiteral("weight")) ? object.value(QStringLiteral("weight")).toDouble(1.0) : 1.0;
    if (!std::isfinite(time_seconds) || !std::isfinite(yaw_degrees) || !std::isfinite(pitch_degrees) ||
        !std::isfinite(weight) || time_seconds < 0.0) {
        return false;
    }
    key->time_ticks = std::clamp(time_seconds, 0.0, 60.0);
    key->yaw_degrees = static_cast<float>(std::clamp(yaw_degrees, -45.0, 45.0));
    key->pitch_degrees = static_cast<float>(std::clamp(pitch_degrees, -30.0, 30.0));
    key->weight = static_cast<float>(std::clamp(weight, 0.0, 1.0));
    return true;
}

std::vector<EyeGazeKeyframeData> parseGazeKeys(const QJsonArray& keys_array) {
    std::vector<EyeGazeKeyframeData> keys;
    keys.reserve(keys_array.size());
    for (const QJsonValue& key_value : keys_array) {
        EyeGazeKeyframeData key;
        if (parseGazeKey(key_value, &key)) {
            keys.push_back(key);
        }
    }
    std::sort(keys.begin(), keys.end(), [](const EyeGazeKeyframeData& lhs, const EyeGazeKeyframeData& rhs) {
        return lhs.time_ticks < rhs.time_ticks;
    });
    return keys;
}

void parseExpressionCurvePayload(const QJsonObject& object, LookDevLlmResult* result) {
    if (!result) {
        return;
    }

    QJsonObject curve_object;
    if (object.value(QStringLiteral("expressionCurve")).isObject()) {
        curve_object = object.value(QStringLiteral("expressionCurve")).toObject();
    } else if (object.value(QStringLiteral("expression")).isObject()) {
        curve_object = object.value(QStringLiteral("expression")).toObject();
    } else if (object.value(QStringLiteral("expression_curves")).isObject()) {
        curve_object = object.value(QStringLiteral("expression_curves")).toObject();
    }
    if (curve_object.isEmpty()) {
        return;
    }

    double duration_seconds = curve_object.contains(QStringLiteral("durationSeconds"))
        ? curve_object.value(QStringLiteral("durationSeconds")).toDouble()
        : curve_object.value(QStringLiteral("duration")).toDouble();
    if (!std::isfinite(duration_seconds) || duration_seconds <= 0.0) {
        duration_seconds = 4.0;
    }
    duration_seconds = std::clamp(duration_seconds, 0.1, 60.0);

    QString expression_error;
    std::vector<ExpressionChannelData> channels = parseExpressionChannels(curve_object.value(QStringLiteral("channels")).toArray(),
                                                                          &expression_error);
    if (channels.empty()) {
        return;
    }

    result->expression_duration_seconds = duration_seconds;
    result->expression_channels = std::move(channels);
    result->expression_summary = curve_object.value(QStringLiteral("summary")).toString(QStringLiteral("Expression curve"));
}

void parseGazeCurvePayload(const QJsonObject& object, LookDevLlmResult* result) {
    if (!result) {
        return;
    }
    QJsonObject curve_object;
    if (object.value(QStringLiteral("gazeCurve")).isObject()) {
        curve_object = object.value(QStringLiteral("gazeCurve")).toObject();
    } else if (object.value(QStringLiteral("eyeGaze")).isObject()) {
        curve_object = object.value(QStringLiteral("eyeGaze")).toObject();
    }
    if (curve_object.isEmpty()) {
        return;
    }

    double duration_seconds = curve_object.contains(QStringLiteral("durationSeconds"))
        ? curve_object.value(QStringLiteral("durationSeconds")).toDouble()
        : curve_object.value(QStringLiteral("duration")).toDouble();
    if (!std::isfinite(duration_seconds) || duration_seconds <= 0.0) {
        duration_seconds = 4.0;
    }
    std::vector<EyeGazeKeyframeData> keys = parseGazeKeys(curve_object.value(QStringLiteral("keys")).toArray());
    if (keys.empty()) {
        return;
    }
    result->gaze_duration_seconds = std::clamp(duration_seconds, 0.1, 60.0);
    result->gaze_keys = std::move(keys);
    result->gaze_summary = curve_object.value(QStringLiteral("summary")).toString(QStringLiteral("Eye bone gaze curve"));
}

QString chatContentFromResponse(const QByteArray& response, QString* error_message) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(response, &parse_error);
    if (!document.isObject()) {
        if (error_message) {
            *error_message = parse_error.error == QJsonParseError::NoError
                ? QStringLiteral("LLM response is not a JSON object.")
                : QStringLiteral("LLM response JSON parse failed: %1").arg(parse_error.errorString());
        }
        return QString();
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("error")).isObject()) {
        const QJsonObject error = object.value(QStringLiteral("error")).toObject();
        const QString code = error.value(QStringLiteral("code")).toString();
        const QString message = error.value(QStringLiteral("message")).toString();
        if (error_message) {
            *error_message = code.isEmpty()
                ? QStringLiteral("LLM returned error: %1").arg(message)
                : QStringLiteral("LLM returned error %1: %2").arg(code, message);
        }
        return QString();
    }

    const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject()) {
        if (error_message) {
            *error_message = QStringLiteral("LLM response did not contain choices[0].message.content.");
        }
        return QString();
    }

    const QJsonObject choice = choices.first().toObject();
    const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString();
    if (content.trimmed().isEmpty()) {
        if (error_message) {
            *error_message = QStringLiteral("LLM returned an empty message.");
        }
    }
    return content;
}

QString networkErrorText(QNetworkReply* reply, const QByteArray& body, bool timed_out, int timeout_ms) {
    if (timed_out) {
        return QStringLiteral("LLM request timed out after %1 seconds. Try again, or reduce the request detail.")
            .arg(timeout_ms / 1000);
    }

    QString message = QStringLiteral("LLM request failed: %1").arg(reply->errorString());
    const QVariant status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (status_code.isValid()) {
        message += QStringLiteral(" (HTTP %1)").arg(status_code.toInt());
    }

    const QString response_text = QString::fromUtf8(body).trimmed();
    if (!response_text.isEmpty()) {
        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (document.isObject() && document.object().value(QStringLiteral("error")).isObject()) {
            const QJsonObject error = document.object().value(QStringLiteral("error")).toObject();
            const QString code = error.value(QStringLiteral("code")).toString();
            const QString error_message = error.value(QStringLiteral("message")).toString();
            if (!code.isEmpty() || !error_message.isEmpty()) {
                message += QStringLiteral(" | Ark error %1: %2").arg(code, error_message);
                return message.left(900);
            }
        }
        message += QStringLiteral(" | Response: %1").arg(response_text.left(700));
    }
    return message.left(900);
}

bool parseRecommendationPayload(const QString& content,
                                const QString& source_prompt,
                                LookDevLlmResult* result) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(extractJsonObjectText(content).toUtf8(), &parse_error);
    if (!document.isObject()) {
        if (result) {
            result->error_message = parse_error.error == QJsonParseError::NoError
                ? QStringLiteral("Model reply did not contain a JSON object.")
                : QStringLiteral("Model JSON parse failed: %1").arg(parse_error.errorString());
        }
        return false;
    }

    const QJsonObject object = document.object();
    LookDevLlmResult parsed;
    parsed.ok = true;
    parsed.assistant_reply = object.value(QStringLiteral("assistantReply")).toString();
    if (parsed.assistant_reply.trimmed().isEmpty()) {
        parsed.assistant_reply = object.value(QStringLiteral("assistant_reply")).toString();
    }
    parsed.needs_user_input = object.value(QStringLiteral("needsUserInput")).toBool(false);
    parsed.next_question = object.value(QStringLiteral("nextQuestion")).toString();
    if (parsed.next_question.trimmed().isEmpty()) {
        parsed.next_question = object.value(QStringLiteral("next_question")).toString();
    }
    parseExpressionCurvePayload(object, &parsed);
    parseGazeCurvePayload(object, &parsed);

    const auto appendPreset = [&](const QJsonObject& candidate_object, const QJsonObject& preset_object) {
        StylePreset preset;
        QString preset_error;
        if (!stylePresetFromJson(preset_object, &preset, &preset_error)) {
            parsed.error_message = preset_error;
            return;
        }

        if (preset.source_prompt.isEmpty()) {
            preset.source_prompt = source_prompt;
        }

        AiRecommendationCandidate candidate;
        candidate.slot_label = candidate_object.value(QStringLiteral("slotLabel")).toString(QStringLiteral("Best"));
        if (candidate.slot_label.isEmpty()) {
            candidate.slot_label = QStringLiteral("Best");
        }
        candidate.preset = preset;
        candidate.summary = candidate_object.value(QStringLiteral("summary")).toString(summarizeStylePreset(preset));
        parsed.candidates.push_back(candidate);
    };

    if (object.value(QStringLiteral("preset")).isObject()) {
        appendPreset(object, object.value(QStringLiteral("preset")).toObject());
    } else {
        const QJsonArray candidates = object.value(QStringLiteral("candidates")).toArray();
        for (int i = 0; i < candidates.size() && parsed.candidates.isEmpty(); ++i) {
            if (!candidates.at(i).isObject()) {
                continue;
            }
            const QJsonObject candidate_object = candidates.at(i).toObject();
            const QJsonObject preset_object = candidate_object.value(QStringLiteral("preset")).isObject()
                ? candidate_object.value(QStringLiteral("preset")).toObject()
                : candidate_object;
            appendPreset(candidate_object, preset_object);
        }
    }

    if (!parsed.needs_user_input && parsed.candidates.isEmpty() && parsed.expression_channels.empty() && parsed.gaze_keys.empty()) {
        if (result) {
            result->error_message = parsed.error_message.isEmpty()
                ? QStringLiteral("Model JSON did not contain a valid style preset or expression curve.")
                : parsed.error_message;
        }
        return false;
    }

    if (result) {
        *result = parsed;
    }
    return true;
}

QJsonObject messageObject(const QString& role, const QString& content) {
    QJsonObject message;
    message.insert(QStringLiteral("role"), role);
    message.insert(QStringLiteral("content"), content);
    return message;
}

QString buildUserPrompt(const QString& prompt,
                        const QString& dialogue_context,
                        const LookDevSettings& base_settings,
                        const QStringList& available_expression_names) {
    StylePreset current;
    current.name = QStringLiteral("Current LookDev State");
    current.description = QStringLiteral("The current renderer settings before AI tuning.");
    current.source_prompt = prompt;
    current.focus = base_settings.shading_model == ShadingModel::Phong ? StylePresetFocus::Phong : StylePresetFocus::PbrAssist;
    current.preferred_pipeline = RenderPipeline::Raster;
    current.settings = base_settings;

    const QString current_json = jsonDocumentToCompactString(QJsonDocument(stylePresetToJson(current)));
    const QString mode_policy = base_settings.shading_model == ShadingModel::Pbr
        ? QStringLiteral(
            "Current mode policy:\n"
            "- The current shading model is PBR. Treat this as first-class, not as a request to convert to Phong.\n"
            "- Unless the latest artist message explicitly asks for Toon, Phong, anime, cel, cartoon, outline, rim-light illustration, or Genshin-like stylization, keep lookDev.shadingModel=\"PBR\" and preferredPipeline=\"Raster\".\n"
            "- In PBR mode, Phong/toon/outline controls are inactive; set phong.toon.enabled=false and phong.outline.enabled=false in the returned preset so the UI state is not misleading.\n"
            "- Tune only PBR-effective controls: exposure, normalStrength, enableShadows, enableBackfaceCulling, pbr.iblEnabled, pbr.iblDiffuseStrength, pbr.iblSpecularStrength, pbr.skyLightStrength, and packed texture channels when the user asks about channel mapping.\n"
            "- If the user's desired change needs per-material roughness, metallic, base color, transmission, or clearcoat controls, explain in assistantReply that the current PBR panel cannot directly change those material factors yet, then provide the best approximation using the available PBR controls.")
        : QStringLiteral(
            "Current mode policy:\n"
            "- The current shading model is Phong/Toon-capable. Phong + Toon is allowed for stylized, anime, painterly, figure, or illustration requests.\n"
            "- If the user asks for realistic PBR, material inspection, product render, metal, roughness, or physically based look, switch to lookDev.shadingModel=\"PBR\" and use the PBR profiles.");
    return QStringLiteral(
        "Latest artist message:\n%1\n\n"
        "Dialogue so far:\n%2\n\n"
        "Current full HaoRender style preset JSON:\n%3\n\n"
        "%4\n\n"
        "Available VRM eye/expression controls in the current scene:\n%5\n\n"
        "Act as an iterative LookDev agent. If the request is underspecified, ask one concise question that reveals the user's real desired rendering effect. "
        "If the intent is clear enough, return exactly one full preset. Respect the current mode policy above. "
        "If the user asks for eyes, gaze, blink, eyelids, or facial expression, you may return expressionCurve using only the available expression names above. "
        "If eye bone gaze solver is available, return gazeCurve for look direction and expressionCurve for blink/emotion. "
        "For PBR requests, stay close to the actual PBR controls and do not invent Phong/toon fixes. "
        "For requests like soft Genshin-like anime rendering, create painter-friendly toon bands, soft colored shadows, controlled clean highlights, a modest outline, and gentle rim light. "
        "Return JSON only, following the schema from the system skill.")
        .arg(prompt.trimmed(),
             dialogue_context.trimmed(),
             current_json,
             mode_policy,
             available_expression_names.isEmpty() ? QStringLiteral("(none)") : available_expression_names.join(QStringLiteral(", ")));
}

} // namespace

LookDevLlmClient::LookDevLlmClient(QObject* parent)
    : QObject(parent) {
}

void LookDevLlmClient::requestRecommendations(const LookDevLlmConfig& config,
                                              const QString& prompt,
                                              const QString& dialogue_context,
                                              const LookDevSettings& base_settings,
                                              const QStringList& available_expression_names,
                                              Callback callback) {
    LookDevLlmResult early;
    if (config.api_key.trimmed().isEmpty()) {
        early.error_message = QStringLiteral("Missing API key. Enter one in the LookDev AI panel, set DOUBAO_API_KEY / ARK_API_KEY, or create llm.env.local.");
        callback(early);
        return;
    }
    if (config.base_url.trimmed().isEmpty()) {
        early.error_message = QStringLiteral("Missing LLM base URL.");
        callback(early);
        return;
    }
    if (config.model.trimmed().isEmpty()) {
        early.error_message = QStringLiteral("Missing LLM model or Ark endpoint id.");
        callback(early);
        return;
    }

    QNetworkRequest request(QUrl(chatCompletionsUrl(config.base_url)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(config.api_key.trimmed()).toUtf8());

    QJsonArray messages;
    messages.append(messageObject(QStringLiteral("system"), lookDevParameterSkillPrompt()));
    messages.append(messageObject(QStringLiteral("user"), buildUserPrompt(prompt, dialogue_context, base_settings, available_expression_names)));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), config.model.trimmed());
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("temperature"), 0.25);
    payload.insert(QStringLiteral("stream"), false);
    payload.insert(QStringLiteral("max_tokens"), 3200);

    auto* reply = network_.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    const int timeout_ms = std::clamp(config.timeout_ms, 5000, 180000);
    auto timed_out = std::make_shared<bool>(false);
    QTimer::singleShot(timeout_ms, reply, [reply, timed_out]() {
        if (reply->isRunning()) {
            *timed_out = true;
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [reply, prompt, timeout_ms, timed_out, callback = std::move(callback)]() mutable {
        LookDevLlmResult result;
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            result.error_message = networkErrorText(reply, body, *timed_out, timeout_ms);
            reply->deleteLater();
            callback(result);
            return;
        }

        QString content_error;
        const QString content = chatContentFromResponse(body, &content_error);
        if (content.trimmed().isEmpty()) {
            result.error_message = content_error;
            reply->deleteLater();
            callback(result);
            return;
        }

        if (!parseRecommendationPayload(content, prompt, &result)) {
            if (result.assistant_reply.isEmpty()) {
                result.assistant_reply = content.left(900);
            }
            reply->deleteLater();
            callback(result);
            return;
        }

        reply->deleteLater();
        callback(result);
    });
}

LookDevLlmConfig lookDevLlmConfigFromEnvironment() {
    const QMap<QString, QString> local_values = loadLocalLlmEnvValues();
    LookDevLlmConfig config;
    config.api_key = envOrLocalValue(local_values, { QStringLiteral("DOUBAO_API_KEY"), QStringLiteral("ARK_API_KEY") });
    config.base_url = envOrLocalValue(local_values, { QStringLiteral("DOUBAO_BASE_URL") }, defaultDoubaoBaseUrl());
    config.model = envOrLocalValue(local_values, { QStringLiteral("DOUBAO_MODEL") }, defaultDoubaoModel());
    config.timeout_ms = envOrLocalInt(local_values, QStringLiteral("AI_REQUEST_TIMEOUT_MS"), config.timeout_ms);
    return config;
}

QString defaultDoubaoBaseUrl() {
    return QStringLiteral("https://ark.cn-beijing.volces.com/api/v3");
}

QString defaultDoubaoModel() {
    return QStringLiteral("doubao-seed-2-0-pro-260215");
}

QString lookDevParameterSkillPrompt() {
    const QJsonDocument color_doc(colorSchemaNote());
    const QString color_schema = jsonDocumentToCompactString(color_doc);
    const QString style_knowledge_base = lookDevStyleKnowledgeBasePrompt();
    return QString::fromUtf8(R"(You are HaoRender-GI LookDev AI, a senior rendering TD helping artists tune a Phong/Toon/PBR workbench.

You MUST output JSON only. Do not output markdown. Do not explain outside JSON.

Your task:
- Run an agent loop: understand the artist's intent, ask one useful follow-up question when the target look is still vague, and only generate parameters when the visual direction is clear enough.
- Internally select one rendering skill from the mature skill library before choosing numbers. The skill choice should drive the preset.
- When generating render parameters, return exactly one full HaoRender StylePreset object, not diffs. For eye-only requests, a gazeCurve/expressionCurve without a preset is allowed.
- Respect the current renderer mode from the user message. If current lookDev.shadingModel is PBR, keep PBR unless the artist clearly asks for Toon/Phong/anime/cel/cartoon/outline stylization.
- Prefer Raster + Phong + Toon only for anime, stylized, illustration, figure, character, Genshin-like, cel, painterly, or hand-painted requests.
- Use PBR as the main path for current PBR mode, material inspection, product render, metal/gloss, realism, roughness, texture-channel, or physically based requests.
- Preserve current values when a parameter is unrelated.
- Avoid low-taste parameter stacking: do not raise rim, outline, specular, ambient, and exposure together. For each preset, decide whether the silhouette is solved by outline or rim, and whether material appeal is solved by specular or soft painted bands.

Output schema:
{
  "assistantReply": "Short Chinese reply. If asking, explain what you need. If generating, explain the tuning direction.",
  "needsUserInput": false,
  "nextQuestion": "",
  "summary": "Short human-readable parameter summary.",
  "expressionCurve": {
    "summary": "Optional eye/gaze/blink/expression action. Include only when the user asked for it.",
    "durationSeconds": 4.0,
    "channels": [
      {
        "name": "blink",
        "keys": [
          { "timeSeconds": 0.00, "weight": 0.0 },
          { "timeSeconds": 0.34, "weight": 0.0 },
          { "timeSeconds": 0.42, "weight": 1.0 },
          { "timeSeconds": 0.52, "weight": 0.0 },
          { "timeSeconds": 4.00, "weight": 0.0 }
        ]
      }
    ]
  },
  "gazeCurve": {
    "summary": "Optional eye-bone look direction. Include only when the user asked for gaze/look direction and the scene has an eye bone solver.",
    "durationSeconds": 4.0,
    "keys": [
      { "timeSeconds": 0.00, "yawDegrees": 0.0, "pitchDegrees": 0.0, "weight": 0.0 },
      { "timeSeconds": 0.45, "yawDegrees": 8.0, "pitchDegrees": -2.0, "weight": 1.0 },
      { "timeSeconds": 3.30, "yawDegrees": 8.0, "pitchDegrees": -2.0, "weight": 1.0 },
      { "timeSeconds": 4.00, "yawDegrees": 0.0, "pitchDegrees": 0.0, "weight": 0.0 }
    ]
  },
  "preset": {
        "version": 1,
        "name": "Preset name",
        "description": "Why this candidate matches the request.",
        "sourcePrompt": "Artist prompt",
        "focus": "Phong|Toon|Hybrid|PbrAssist",
        "preferredPipeline": "Raster|OpenGLRayTrace|DXR",
        "lookDev": {
          "shadingModel": "PBR|Phong",
          "exposure": 0.1,
          "normalStrength": 1.0,
          "enableShadows": true,
          "enableBackfaceCulling": true,
          "pbr": {
            "iblEnabled": true,
            "iblDiffuseStrength": 0.55,
            "iblSpecularStrength": 0.8,
            "skyLightStrength": 0.2,
            "metallicChannel": 2,
            "roughnessChannel": 1,
            "aoChannel": 0,
            "emissiveChannel": 0
          },
          "phong": {
            "hardSpecular": false,
            "useTonemap": true,
            "primaryLightOnly": false,
            "secondaryLightScale": 0.2,
            "diffuseStrength": 1.0,
            "ambientStrength": 0.05,
            "ambientColor": COLOR,
            "specularStrength": 0.35,
            "specularTint": COLOR,
            "smoothness": 0.8,
            "specularMapWeight": 1.0,
            "shininess": 64.0,
            "rimStrength": 0.35,
            "rimPower": 2.5,
            "rimTint": COLOR,
            "toon": {
              "enabled": true,
              "diffuseSteps": 3.0,
              "diffuseSoftness": 0.04,
              "shadowFloor": 0.10,
              "litFloor": 0.45,
              "rampBias": 0.0,
              "rampContrast": 1.0,
              "shadowMapStrength": 0.45,
              "shadowThreshold": 0.42,
              "shadowSoftness": 0.07,
              "shadowTint": COLOR,
              "highlightThreshold": 0.35,
              "highlightSoftness": 0.06,
              "highlightStrength": 0.75,
              "highlightTint": COLOR,
              "rimThreshold": 0.3,
              "rimSoftness": 0.08,
              "materialOverrideEnabled": true,
              "materialTextureStrength": 0.85,
              "materialLift": 0.05,
              "materialSaturation": 1.0,
              "materialContrast": 0.95
            },
            "outline": {
              "enabled": true,
              "widthPixels": 2.0,
              "opacity": 0.85,
              "depthBias": 0.0015,
              "color": COLOR
            }
          },
          "rayTrace": {
            "sceneMode": "CornellBox|LoadedModel",
            "viewMode": "Lit|Hit|Normal|Albedo",
            "integrator": "Hybrid|PathTrace|PathTraceNee|PhotonPath",
            "ambientStrength": 0.05,
            "shadowStrength": 1.0,
            "maxBounces": 20,
            "maxNeeBounces": 2,
            "samplesPerFrame": 8,
            "enableNee": true,
            "enablePhotonCache": true,
            "photonRadius": 0.22,
            "photonIntensity": 1.0
          }
        }
  }
}

COLOR schema is )") + color_schema + QString::fromUtf8(R"(.

Valid ranges:
- exposure: 0.1-3.0
- normalStrength: 0.0-2.0
- pbr strengths: 0.0-2.0
- channel indices: 0=R, 1=G, 2=B, 3=A
- secondaryLightScale: 0.0-1.5
- diffuseStrength/specularStrength/specularMapWeight/rimStrength/highlightStrength: 0.0-2.0
- ambientStrength: 0.0-1.0
- smoothness: 0.0-1.0
- shininess: 4.0-128.0
- rimPower: 0.25-8.0
- toon diffuseSteps: 2.0-6.0
- toon softness values: 0.0-0.25 preferred, never above 0.45
- thresholds: 0.0-1.0
- toon shadowFloor: 0.0-0.8, use 0.08-0.18 for soft anime dark-color readability
- toon litFloor: 0.0-1.0, use 0.42-0.58 for soft game-anime lit bands
- toon rampBias: -0.5 to 0.5, positive shifts the model toward the lit side
- toon rampContrast: 0.25-2.5, lower is painterly/soft, higher is graphic cel
- toon shadowMapStrength: 0.0-1.0, lower keeps toon shadows from crushing color
- toon materialTextureStrength: 0.0-1.0
- toon materialLift: 0.0-0.8
- toon materialSaturation: 0.0-2.0
- toon materialContrast: 0.25-2.5
- outline widthPixels: 0.0-12.0
- outline opacity: 0.0-1.0
- outline depthBias: 0.0-0.02

)") + style_knowledge_base + QString::fromUtf8(R"(

If needsUserInput is true:
- Do not include preset.
- Do not include expressionCurve.
- Do not include gazeCurve.
- Ask exactly one concrete question in nextQuestion.
- Prefer questions about the user's actual desired visual result, such as soft vs crisp shadow bands, outline strength, highlight size, warm/cool shadow color, or reference style.

If needsUserInput is false:
- Include exactly one valid preset, one valid expressionCurve, one valid gazeCurve, or a useful combination of them.
- Do not include candidates.

Never invent parameters outside the schema. Always return parseable JSON.)");
}

} // namespace haorendergi
