#include "scene/gltf_animation_loader.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace haorendergi {
namespace {

constexpr quint32 kGlbMagic = 0x46546C67u; // glTF
constexpr quint32 kJsonChunk = 0x4E4F534Au; // JSON
constexpr quint32 kBinChunk = 0x004E4942u; // BIN

template <typename T>
T readLe(const QByteArray& data, int offset) {
    T value {};
    if (offset >= 0 && offset + static_cast<int>(sizeof(T)) <= data.size()) {
        std::memcpy(&value, data.constData() + offset, sizeof(T));
    }
    return value;
}

bool readFileBytes(const QString& path, QByteArray* bytes, QString* error_message) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error_message) {
            *error_message = QStringLiteral("Failed to open %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    *bytes = file.readAll();
    return true;
}

bool readJsonAndBinary(const QString& path,
                       QJsonObject* json,
                       QVector<QByteArray>* buffers,
                       QString* error_message) {
    QByteArray file_bytes;
    if (!readFileBytes(path, &file_bytes, error_message)) {
        return false;
    }

    QByteArray json_bytes;
    QByteArray binary_chunk;
    const QFileInfo file_info(path);
    if (file_bytes.size() >= 12 && readLe<quint32>(file_bytes, 0) == kGlbMagic) {
        int cursor = 12;
        while (cursor + 8 <= file_bytes.size()) {
            const quint32 chunk_length = readLe<quint32>(file_bytes, cursor);
            const quint32 chunk_type = readLe<quint32>(file_bytes, cursor + 4);
            cursor += 8;
            if (cursor + static_cast<int>(chunk_length) > file_bytes.size()) {
                break;
            }
            const QByteArray chunk = file_bytes.mid(cursor, static_cast<int>(chunk_length));
            if (chunk_type == kJsonChunk) {
                json_bytes = chunk;
            } else if (chunk_type == kBinChunk) {
                binary_chunk = chunk;
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
            *error_message = QStringLiteral("glTF JSON parse failed: %1").arg(parse_error.errorString());
        }
        return false;
    }
    *json = document.object();

    buffers->clear();
    const QJsonArray buffer_array = json->value(QStringLiteral("buffers")).toArray();
    for (int i = 0; i < buffer_array.size(); ++i) {
        const QJsonObject buffer_object = buffer_array[i].toObject();
        const QString uri = buffer_object.value(QStringLiteral("uri")).toString();
        if (uri.isEmpty()) {
            buffers->push_back(binary_chunk);
            continue;
        }
        if (uri.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
            const int comma = uri.indexOf(QLatin1Char(','));
            buffers->push_back(comma >= 0 ? QByteArray::fromBase64(uri.mid(comma + 1).toUtf8()) : QByteArray());
            continue;
        }
        QByteArray external;
        const QString external_path = QDir(file_info.absolutePath()).filePath(uri);
        if (!readFileBytes(external_path, &external, nullptr)) {
            external.clear();
        }
        buffers->push_back(external);
    }
    if (buffers->isEmpty()) {
        buffers->push_back(binary_chunk);
    }
    return true;
}

QJsonObject objectAt(const QJsonArray& array, int index) {
    if (index < 0 || index >= array.size()) {
        return QJsonObject();
    }
    return array[index].toObject();
}

int componentCountForType(const QString& type) {
    if (type == QStringLiteral("SCALAR")) {
        return 1;
    }
    if (type == QStringLiteral("VEC2")) {
        return 2;
    }
    if (type == QStringLiteral("VEC3")) {
        return 3;
    }
    if (type == QStringLiteral("VEC4")) {
        return 4;
    }
    if (type == QStringLiteral("MAT4")) {
        return 16;
    }
    return 0;
}

bool readFloatAccessor(const QJsonObject& json,
                       const QVector<QByteArray>& buffers,
                       int accessor_index,
                       int expected_components,
                       QVector<float>* values) {
    values->clear();
    const QJsonArray accessors = json.value(QStringLiteral("accessors")).toArray();
    const QJsonArray buffer_views = json.value(QStringLiteral("bufferViews")).toArray();
    const QJsonObject accessor = objectAt(accessors, accessor_index);
    if (accessor.isEmpty() || accessor.value(QStringLiteral("componentType")).toInt() != 5126) {
        return false;
    }

    const int components = componentCountForType(accessor.value(QStringLiteral("type")).toString());
    if (components <= 0 || (expected_components > 0 && components != expected_components)) {
        return false;
    }
    const int count = accessor.value(QStringLiteral("count")).toInt();
    const int buffer_view_index = accessor.value(QStringLiteral("bufferView")).toInt(-1);
    const QJsonObject buffer_view = objectAt(buffer_views, buffer_view_index);
    const int buffer_index = buffer_view.value(QStringLiteral("buffer")).toInt(0);
    if (buffer_index < 0 || buffer_index >= buffers.size()) {
        return false;
    }

    const QByteArray& buffer = buffers[buffer_index];
    const int view_offset = buffer_view.value(QStringLiteral("byteOffset")).toInt(0);
    const int accessor_offset = accessor.value(QStringLiteral("byteOffset")).toInt(0);
    const int stride = buffer_view.value(QStringLiteral("byteStride")).toInt(components * static_cast<int>(sizeof(float)));
    const int base_offset = view_offset + accessor_offset;
    if (count <= 0 || base_offset < 0 || stride <= 0) {
        return false;
    }

    values->resize(count * components);
    for (int element = 0; element < count; ++element) {
        const int element_offset = base_offset + element * stride;
        if (element_offset + components * static_cast<int>(sizeof(float)) > buffer.size()) {
            values->clear();
            return false;
        }
        for (int component = 0; component < components; ++component) {
            (*values)[element * components + component] = readLe<float>(buffer, element_offset + component * static_cast<int>(sizeof(float)));
        }
    }
    return true;
}

Eigen::Matrix4f composeNodeTransform(const QJsonObject& node) {
    Eigen::Matrix4f matrix = Eigen::Matrix4f::Identity();
    const QJsonArray matrix_array = node.value(QStringLiteral("matrix")).toArray();
    if (matrix_array.size() == 16) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                matrix(row, column) = static_cast<float>(matrix_array[column * 4 + row].toDouble());
            }
        }
        return matrix;
    }

    Eigen::Vector3f translation = Eigen::Vector3f::Zero();
    Eigen::Vector3f scale = Eigen::Vector3f::Ones();
    Eigen::Quaternionf rotation = Eigen::Quaternionf::Identity();

    const QJsonArray translation_array = node.value(QStringLiteral("translation")).toArray();
    if (translation_array.size() == 3) {
        translation = Eigen::Vector3f(static_cast<float>(translation_array[0].toDouble()),
                                      static_cast<float>(translation_array[1].toDouble()),
                                      static_cast<float>(translation_array[2].toDouble()));
    }
    const QJsonArray scale_array = node.value(QStringLiteral("scale")).toArray();
    if (scale_array.size() == 3) {
        scale = Eigen::Vector3f(static_cast<float>(scale_array[0].toDouble(1.0)),
                                static_cast<float>(scale_array[1].toDouble(1.0)),
                                static_cast<float>(scale_array[2].toDouble(1.0)));
    }
    const QJsonArray rotation_array = node.value(QStringLiteral("rotation")).toArray();
    if (rotation_array.size() == 4) {
        rotation = Eigen::Quaternionf(static_cast<float>(rotation_array[3].toDouble(1.0)),
                                      static_cast<float>(rotation_array[0].toDouble()),
                                      static_cast<float>(rotation_array[1].toDouble()),
                                      static_cast<float>(rotation_array[2].toDouble())).normalized();
    }

    Eigen::Matrix4f result = Eigen::Matrix4f::Identity();
    Eigen::Matrix3f linear = rotation.toRotationMatrix();
    linear.col(0) *= scale.x();
    linear.col(1) *= scale.y();
    linear.col(2) *= scale.z();
    result.block<3, 3>(0, 0) = linear;
    result.block<3, 1>(0, 3) = translation;
    return result;
}

void loadNodes(const QJsonObject& json, SceneModel* scene) {
    const QJsonArray nodes = json.value(QStringLiteral("nodes")).toArray();
    scene->nodes.clear();
    scene->nodes.resize(nodes.size());
    for (int i = 0; i < nodes.size(); ++i) {
        const QJsonObject node = nodes[i].toObject();
        SceneNodeData data;
        data.name = node.value(QStringLiteral("name")).toString(QStringLiteral("node_%1").arg(i)).toStdString();
        data.local_bind_transform = composeNodeTransform(node);
        scene->nodes[i] = data;
    }
    for (int parent = 0; parent < nodes.size(); ++parent) {
        const QJsonArray children = nodes[parent].toObject().value(QStringLiteral("children")).toArray();
        for (const QJsonValue& child_value : children) {
            const int child = child_value.toInt(-1);
            if (child >= 0 && child < scene->nodes.size()) {
                scene->nodes[child].parent_index = parent;
            }
        }
    }
}

QHash<int, QString> loadVrmAnimationExpressionNodes(const QJsonObject& json) {
    QHash<int, QString> expression_by_node;
    const QJsonObject vrm_animation = json.value(QStringLiteral("extensions"))
                                          .toObject()
                                          .value(QStringLiteral("VRMC_vrm_animation"))
                                          .toObject();
    const QJsonObject expressions = vrm_animation.value(QStringLiteral("expressions")).toObject();
    for (const QString& group_name : { QStringLiteral("preset"), QStringLiteral("custom") }) {
        const QJsonObject group = expressions.value(group_name).toObject();
        for (auto it = group.begin(); it != group.end(); ++it) {
            const int node_index = it.value().toObject().value(QStringLiteral("node")).toInt(-1);
            if (node_index >= 0) {
                expression_by_node.insert(node_index, it.key());
            }
        }
    }
    return expression_by_node;
}

void loadAnimations(const QJsonObject& json,
                    const QVector<QByteArray>& buffers,
                    SceneModel* scene) {
    const QJsonArray animations = json.value(QStringLiteral("animations")).toArray();
    const QHash<int, QString> expression_by_node = loadVrmAnimationExpressionNodes(json);
    for (int animation_index = 0; animation_index < animations.size(); ++animation_index) {
        const QJsonObject animation_object = animations[animation_index].toObject();
        const QJsonArray samplers = animation_object.value(QStringLiteral("samplers")).toArray();
        const QJsonArray channels = animation_object.value(QStringLiteral("channels")).toArray();

        AnimationClipData clip;
        clip.name = animation_object.value(QStringLiteral("name"))
                        .toString(QStringLiteral("Animation %1").arg(animation_index + 1))
                        .toStdString();
        clip.ticks_per_second = 1.0;
        QHash<int, int> channel_by_node;

        for (const QJsonValue& channel_value : channels) {
            const QJsonObject channel_object = channel_value.toObject();
            const QJsonObject target = channel_object.value(QStringLiteral("target")).toObject();
            const int node_index = target.value(QStringLiteral("node")).toInt(-1);
            const QString path = target.value(QStringLiteral("path")).toString();
            const int sampler_index = channel_object.value(QStringLiteral("sampler")).toInt(-1);
            const QJsonObject sampler = objectAt(samplers, sampler_index);
            if (node_index < 0 || node_index >= scene->nodes.size() || sampler.isEmpty()) {
                continue;
            }

            QVector<float> input_times;
            QVector<float> output_values;
            if (!readFloatAccessor(json, buffers, sampler.value(QStringLiteral("input")).toInt(-1), 1, &input_times)) {
                continue;
            }
            const int expected_components = path == QStringLiteral("rotation") ? 4 : (path == QStringLiteral("weights") ? 0 : 3);
            if (!readFloatAccessor(json, buffers, sampler.value(QStringLiteral("output")).toInt(-1), expected_components, &output_values)) {
                continue;
            }
            const int weights_per_key = path == QStringLiteral("weights") && !input_times.isEmpty()
                ? output_values.size() / input_times.size()
                : 0;
            const int values_per_key = path == QStringLiteral("weights") ? weights_per_key : expected_components;
            const int key_count = values_per_key > 0 ? std::min(input_times.size(), output_values.size() / values_per_key) : 0;
            if (key_count <= 0) {
                continue;
            }

            int clip_channel_index = channel_by_node.value(node_index, -1);
            if (clip_channel_index < 0) {
                AnimationChannelData channel;
                channel.node_index = node_index;
                clip.channels.push_back(std::move(channel));
                clip_channel_index = static_cast<int>(clip.channels.size()) - 1;
                channel_by_node.insert(node_index, clip_channel_index);
            }
            AnimationChannelData& channel = clip.channels[clip_channel_index];
            for (int key = 0; key < key_count; ++key) {
                const double time = static_cast<double>(input_times[key]);
                clip.duration_ticks = std::max(clip.duration_ticks, time);
                const int base = key * values_per_key;
                if (path == QStringLiteral("translation")) {
                    VectorKeyframe frame;
                    frame.time_ticks = time;
                    frame.value = Eigen::Vector3f(output_values[base + 0], output_values[base + 1], output_values[base + 2]);
                    channel.positions.push_back(frame);
                } else if (path == QStringLiteral("scale")) {
                    VectorKeyframe frame;
                    frame.time_ticks = time;
                    frame.value = Eigen::Vector3f(output_values[base + 0], output_values[base + 1], output_values[base + 2]);
                    channel.scales.push_back(frame);
                } else if (path == QStringLiteral("rotation")) {
                    RotationKeyframe frame;
                    frame.time_ticks = time;
                    frame.value = Eigen::Quaternionf(output_values[base + 3], output_values[base + 0], output_values[base + 1], output_values[base + 2]).normalized();
                    channel.rotations.push_back(frame);
                } else if (path == QStringLiteral("weights")) {
                    MorphWeightKeyframe frame;
                    frame.time_ticks = time;
                    frame.values.reserve(values_per_key);
                    for (int value_index = 0; value_index < values_per_key; ++value_index) {
                        frame.values.push_back(output_values[base + value_index]);
                    }
                    channel.morph_weights.push_back(std::move(frame));
                }
            }
        }
        for (auto expression_it = expression_by_node.begin(); expression_it != expression_by_node.end(); ++expression_it) {
            const int node_index = expression_it.key();
            const QString expression_name = expression_it.value();
            const int channel_index = channel_by_node.value(node_index, -1);
            if (channel_index < 0 || channel_index >= static_cast<int>(clip.channels.size())) {
                continue;
            }
            const AnimationChannelData& node_channel = clip.channels[channel_index];
            ExpressionChannelData expression_channel;
            expression_channel.name = expression_name.toStdString();
            expression_channel.weights.reserve(node_channel.positions.size());
            for (const VectorKeyframe& key : node_channel.positions) {
                expression_channel.weights.push_back(ScalarKeyframe{ key.time_ticks, key.value.x() });
            }
            if (!expression_channel.weights.empty()) {
                clip.expression_channels.push_back(std::move(expression_channel));
            }
        }
        clip.channels.erase(std::remove_if(clip.channels.begin(), clip.channels.end(), [](const AnimationChannelData& channel) {
                                return channel.positions.empty() && channel.rotations.empty() && channel.scales.empty() && channel.morph_weights.empty();
                            }),
                            clip.channels.end());
        if (!clip.channels.empty() || !clip.expression_channels.empty()) {
            scene->animations.push_back(clip);
        }
    }
}

} // namespace

bool loadGltfAnimationScene(const QString& path, SceneModel* scene, QString* error_message) {
    if (!scene) {
        return false;
    }

    QJsonObject json;
    QVector<QByteArray> buffers;
    if (!readJsonAndBinary(path, &json, &buffers, error_message)) {
        return false;
    }

    SceneModel loaded;
    loaded.source_path = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdString();
    loadNodes(json, &loaded);
    loadAnimations(json, buffers, &loaded);
    if (loaded.nodes.empty() || loaded.animations.empty()) {
        if (error_message) {
            *error_message = QStringLiteral("glTF/VRMA animation data was not found.");
        }
        return false;
    }
    *scene = std::move(loaded);
    if (error_message) {
        error_message->clear();
    }
    return true;
}

} // namespace haorendergi
