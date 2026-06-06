#include "scene/gltf_vrm_expression_loader.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <unordered_map>

namespace haorendergi {
namespace {

constexpr quint32 kGlbMagic = 0x46546C67u;
constexpr quint32 kJsonChunk = 0x4E4F534Au;

template <typename T>
T readLe(const QByteArray& data, int offset) {
    T value {};
    if (offset >= 0 && offset + static_cast<int>(sizeof(T)) <= data.size()) {
        std::memcpy(&value, data.constData() + offset, sizeof(T));
    }
    return value;
}

bool readFileBytes(const QString& path, QByteArray* bytes) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    *bytes = file.readAll();
    return true;
}

bool readGltfJson(const QString& path, QJsonObject* json, QString* error_message) {
    QByteArray file_bytes;
    if (!readFileBytes(path, &file_bytes)) {
        if (error_message) {
            *error_message = QStringLiteral("Failed to open %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }

    QByteArray json_bytes;
    if (file_bytes.size() >= 12 && readLe<quint32>(file_bytes, 0) == kGlbMagic) {
        int cursor = 12;
        while (cursor + 8 <= file_bytes.size()) {
            const quint32 chunk_length = readLe<quint32>(file_bytes, cursor);
            const quint32 chunk_type = readLe<quint32>(file_bytes, cursor + 4);
            cursor += 8;
            if (cursor + static_cast<int>(chunk_length) > file_bytes.size()) {
                break;
            }
            if (chunk_type == kJsonChunk) {
                json_bytes = file_bytes.mid(cursor, static_cast<int>(chunk_length));
                break;
            }
            cursor += static_cast<int>((chunk_length + 3u) & ~3u);
        }
    } else {
        json_bytes = file_bytes;
    }

    if (json_bytes.isEmpty()) {
        if (error_message) {
            *error_message = QStringLiteral("The file does not contain a glTF JSON chunk.");
        }
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(json_bytes.trimmed(), &parse_error);
    if (document.isNull() || !document.isObject()) {
        if (error_message) {
            *error_message = QStringLiteral("VRM expression JSON parse failed: %1").arg(parse_error.errorString());
        }
        return false;
    }
    *json = document.object();
    return true;
}

QJsonObject objectAt(const QJsonArray& array, int index) {
    if (index < 0 || index >= array.size()) {
        return QJsonObject();
    }
    return array[index].toObject();
}

std::unordered_map<std::string, int> buildSceneNodeIndex(const SceneModel& scene) {
    std::unordered_map<std::string, int> index;
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        if (!scene.nodes[i].name.empty() && !index.count(scene.nodes[i].name)) {
            index[scene.nodes[i].name] = i;
        }
    }
    return index;
}

QString gltfNodeName(const QJsonArray& nodes, int node_index) {
    const QJsonObject node = objectAt(nodes, node_index);
    return node.value(QStringLiteral("name")).toString();
}

int resolveSceneNodeIndex(const QJsonArray& nodes,
                          int gltf_node_index,
                          const std::unordered_map<std::string, int>& scene_nodes) {
    const QString node_name = gltfNodeName(nodes, gltf_node_index);
    const auto found = scene_nodes.find(node_name.toStdString());
    return found != scene_nodes.end() ? found->second : -1;
}

bool expressionNameExists(const SceneModel& scene, const QString& name) {
    return std::any_of(scene.vrm_expressions.begin(), scene.vrm_expressions.end(), [&](const VrmExpressionData& expression) {
        return QString::fromStdString(expression.name).compare(name, Qt::CaseInsensitive) == 0;
    });
}

VrmLookAtRangeMapData parseRangeMap(const QJsonObject& object) {
    VrmLookAtRangeMapData map;
    map.input_max_value = static_cast<float>(object.value(QStringLiteral("inputMaxValue")).toDouble(90.0));
    map.output_scale = static_cast<float>(object.value(QStringLiteral("outputScale")).toDouble(0.0));
    return map;
}

void parseLookAt(const QJsonObject& vrm, SceneModel* scene) {
    const QJsonObject look_at = vrm.value(QStringLiteral("lookAt")).toObject();
    if (!scene || look_at.isEmpty()) {
        return;
    }

    scene->vrm_look_at.type = look_at.value(QStringLiteral("type")).toString().toStdString();
    const QJsonArray offset = look_at.value(QStringLiteral("offsetFromHeadBone")).toArray();
    if (offset.size() == 3) {
        scene->vrm_look_at.offset_from_head_bone = Eigen::Vector3f(static_cast<float>(offset[0].toDouble()),
                                                                  static_cast<float>(offset[1].toDouble()),
                                                                  static_cast<float>(offset[2].toDouble()));
    }
    scene->vrm_look_at.horizontal_inner = parseRangeMap(look_at.value(QStringLiteral("rangeMapHorizontalInner")).toObject());
    scene->vrm_look_at.horizontal_outer = parseRangeMap(look_at.value(QStringLiteral("rangeMapHorizontalOuter")).toObject());
    scene->vrm_look_at.vertical_down = parseRangeMap(look_at.value(QStringLiteral("rangeMapVerticalDown")).toObject());
    scene->vrm_look_at.vertical_up = parseRangeMap(look_at.value(QStringLiteral("rangeMapVerticalUp")).toObject());
}

void setHumanoidNode(const QJsonObject& human_bones,
                     const QString& key,
                     const QJsonArray& nodes,
                     const std::unordered_map<std::string, int>& scene_nodes,
                     int* node_index,
                     std::string* node_name) {
    if (!node_index || !node_name) {
        return;
    }
    const int gltf_node_index = human_bones.value(key).toObject().value(QStringLiteral("node")).toInt(-1);
    if (gltf_node_index < 0) {
        return;
    }
    const QString name = gltfNodeName(nodes, gltf_node_index);
    const auto found = scene_nodes.find(name.toStdString());
    if (found == scene_nodes.end()) {
        return;
    }
    *node_index = found->second;
    *node_name = name.toStdString();
}

bool nameContainsAny(QString lower, std::initializer_list<const char*> needles) {
    lower.remove(QLatin1Char(' '));
    lower.remove(QLatin1Char('_'));
    lower.remove(QLatin1Char('-'));
    for (const char* needle : needles) {
        QString normalized = QString::fromUtf8(needle).toLower();
        normalized.remove(QLatin1Char(' '));
        normalized.remove(QLatin1Char('_'));
        normalized.remove(QLatin1Char('-'));
        if (lower.contains(normalized)) {
            return true;
        }
    }
    return false;
}

void resolveFallbackEyeBones(SceneModel* scene) {
    if (!scene) {
        return;
    }
    for (int i = 0; i < static_cast<int>(scene->nodes.size()); ++i) {
        const QString lower = QString::fromStdString(scene->nodes[i].name).toLower();
        if (scene->vrm_look_at.left_eye_node_index < 0 &&
            nameContainsAny(lower, { "lefteye", "eyeleft", "eye.l", "eye_l", "l_eye", "eyeleftbone" })) {
            scene->vrm_look_at.left_eye_node_index = i;
            scene->vrm_look_at.left_eye_node_name = scene->nodes[i].name;
        }
        if (scene->vrm_look_at.right_eye_node_index < 0 &&
            nameContainsAny(lower, { "righteye", "eyeright", "eye.r", "eye_r", "r_eye", "eyerightbone" })) {
            scene->vrm_look_at.right_eye_node_index = i;
            scene->vrm_look_at.right_eye_node_name = scene->nodes[i].name;
        }
        if (scene->vrm_look_at.head_node_index < 0 &&
            nameContainsAny(lower, { "head", "j_bip_c_head" })) {
            scene->vrm_look_at.head_node_index = i;
            scene->vrm_look_at.head_node_name = scene->nodes[i].name;
        }
    }
}

void parseHumanoidLookAtBones(const QJsonObject& vrm,
                              const QJsonArray& nodes,
                              const std::unordered_map<std::string, int>& scene_nodes,
                              SceneModel* scene) {
    if (!scene) {
        return;
    }
    const QJsonObject human_bones = vrm.value(QStringLiteral("humanoid")).toObject().value(QStringLiteral("humanBones")).toObject();
    if (!human_bones.isEmpty()) {
        setHumanoidNode(human_bones, QStringLiteral("head"), nodes, scene_nodes, &scene->vrm_look_at.head_node_index, &scene->vrm_look_at.head_node_name);
        setHumanoidNode(human_bones, QStringLiteral("leftEye"), nodes, scene_nodes, &scene->vrm_look_at.left_eye_node_index, &scene->vrm_look_at.left_eye_node_name);
        setHumanoidNode(human_bones, QStringLiteral("rightEye"), nodes, scene_nodes, &scene->vrm_look_at.right_eye_node_index, &scene->vrm_look_at.right_eye_node_name);
    }
    resolveFallbackEyeBones(scene);
}

VrmExpressionData parseExpression(const QString& name,
                                  VrmExpressionCategory category,
                                  const QJsonObject& object,
                                  const QJsonArray& nodes,
                                  const std::unordered_map<std::string, int>& scene_nodes) {
    VrmExpressionData expression;
    expression.name = name.toStdString();
    expression.category = category;
    expression.is_binary = object.value(QStringLiteral("isBinary")).toBool(false);
    expression.override_blink = object.value(QStringLiteral("overrideBlink")).toString().toStdString();
    expression.override_look_at = object.value(QStringLiteral("overrideLookAt")).toString().toStdString();
    expression.override_mouth = object.value(QStringLiteral("overrideMouth")).toString().toStdString();

    const QJsonArray binds = object.value(QStringLiteral("morphTargetBinds")).toArray();
    for (const QJsonValue& value : binds) {
        const QJsonObject bind_object = value.toObject();
        const int gltf_node_index = bind_object.value(QStringLiteral("node")).toInt(-1);
        const int scene_node_index = resolveSceneNodeIndex(nodes, gltf_node_index, scene_nodes);
        if (scene_node_index < 0) {
            continue;
        }

        VrmMorphTargetBindData bind;
        bind.node_index = scene_node_index;
        bind.node_name = gltfNodeName(nodes, gltf_node_index).toStdString();
        bind.morph_target_index = bind_object.value(QStringLiteral("index")).toInt(-1);
        bind.weight = static_cast<float>(bind_object.value(QStringLiteral("weight")).toDouble(1.0));
        if (bind.morph_target_index >= 0) {
            expression.morph_target_binds.push_back(std::move(bind));
        }
    }
    return expression;
}

void parseExpressionGroup(const QJsonObject& group,
                          VrmExpressionCategory category,
                          const QJsonArray& nodes,
                          const std::unordered_map<std::string, int>& scene_nodes,
                          SceneModel* scene) {
    for (auto it = group.begin(); it != group.end(); ++it) {
        const VrmExpressionData expression = parseExpression(it.key(), category, it.value().toObject(), nodes, scene_nodes);
        if (!expressionNameExists(*scene, it.key())) {
            scene->vrm_expressions.push_back(expression);
        }
    }
}

bool isDirectEyeMorphName(const QString& name) {
    return name.startsWith(QStringLiteral("Fcl_EYE_"), Qt::CaseInsensitive) ||
           name.contains(QStringLiteral("Eye"), Qt::CaseInsensitive) ||
           name.contains(QStringLiteral("Iris"), Qt::CaseInsensitive) ||
           name.contains(QStringLiteral("Highlight"), Qt::CaseInsensitive);
}

void addDirectMorphExpressions(SceneModel* scene) {
    if (!scene) {
        return;
    }

    for (const MeshData& mesh : scene->meshes) {
        for (int target_index = 0; target_index < static_cast<int>(mesh.morph_target_names.size()); ++target_index) {
            const QString target_name = QString::fromStdString(mesh.morph_target_names[target_index]);
            if (target_name.isEmpty() || !isDirectEyeMorphName(target_name)) {
                continue;
            }

            VrmExpressionData* expression = nullptr;
            for (VrmExpressionData& candidate : scene->vrm_expressions) {
                if (QString::fromStdString(candidate.name).compare(target_name, Qt::CaseInsensitive) == 0) {
                    expression = &candidate;
                    break;
                }
            }
            if (!expression) {
                VrmExpressionData direct;
                direct.name = target_name.toStdString();
                direct.category = VrmExpressionCategory::DirectMorph;
                scene->vrm_expressions.push_back(std::move(direct));
                expression = &scene->vrm_expressions.back();
            }

            const bool already_bound = std::any_of(expression->morph_target_binds.begin(),
                                                   expression->morph_target_binds.end(),
                                                   [&](const VrmMorphTargetBindData& bind) {
                                                       return bind.node_index == mesh.node_index &&
                                                              bind.morph_target_index == target_index;
                                                   });
            if (already_bound) {
                continue;
            }

            VrmMorphTargetBindData bind;
            bind.node_index = mesh.node_index;
            bind.morph_target_index = target_index;
            bind.weight = 1.0f;
            if (mesh.node_index >= 0 && mesh.node_index < static_cast<int>(scene->nodes.size())) {
                bind.node_name = scene->nodes[mesh.node_index].name;
            }
            expression->morph_target_binds.push_back(std::move(bind));
        }
    }
}

} // namespace

bool loadGltfVrmExpressions(const QString& path, SceneModel* scene, QString* error_message) {
    if (!scene) {
        return false;
    }

    QJsonObject json;
    if (!readGltfJson(path, &json, error_message)) {
        return false;
    }

    const QJsonObject vrm = json.value(QStringLiteral("extensions")).toObject().value(QStringLiteral("VRMC_vrm")).toObject();
    if (vrm.isEmpty()) {
        addDirectMorphExpressions(scene);
        resolveFallbackEyeBones(scene);
        if (error_message) {
            error_message->clear();
        }
        return true;
    }

    const QJsonArray nodes = json.value(QStringLiteral("nodes")).toArray();
    const std::unordered_map<std::string, int> scene_nodes = buildSceneNodeIndex(*scene);
    parseLookAt(vrm, scene);
    parseHumanoidLookAtBones(vrm, nodes, scene_nodes, scene);

    scene->vrm_expressions.clear();
    const QJsonObject expressions = vrm.value(QStringLiteral("expressions")).toObject();
    parseExpressionGroup(expressions.value(QStringLiteral("preset")).toObject(),
                         VrmExpressionCategory::Preset,
                         nodes,
                         scene_nodes,
                         scene);
    parseExpressionGroup(expressions.value(QStringLiteral("custom")).toObject(),
                         VrmExpressionCategory::Custom,
                         nodes,
                         scene_nodes,
                         scene);
    addDirectMorphExpressions(scene);

    if (error_message) {
        error_message->clear();
    }
    return true;
}

} // namespace haorendergi
