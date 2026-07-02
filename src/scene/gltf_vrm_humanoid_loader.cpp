#include "scene/gltf_vrm_humanoid_loader.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <cstring>

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

bool readGltfJson(const QString& path, QJsonObject* json, QString* error_message) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error_message) {
            *error_message = QStringLiteral("Failed to open %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }

    const QByteArray bytes = file.readAll();
    QByteArray json_bytes;
    if (bytes.size() >= 12 && readLe<quint32>(bytes, 0) == kGlbMagic) {
        int cursor = 12;
        while (cursor + 8 <= bytes.size()) {
            const quint32 chunk_length = readLe<quint32>(bytes, cursor);
            const quint32 chunk_type = readLe<quint32>(bytes, cursor + 4);
            cursor += 8;
            if (cursor + static_cast<int>(chunk_length) > bytes.size()) {
                break;
            }
            if (chunk_type == kJsonChunk) {
                json_bytes = bytes.mid(cursor, static_cast<int>(chunk_length));
                break;
            }
            cursor += static_cast<int>((chunk_length + 3u) & ~3u);
        }
    } else {
        json_bytes = bytes;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(json_bytes.trimmed(), &parse_error);
    if (!document.isObject()) {
        if (error_message) {
            *error_message = QStringLiteral("glTF humanoid JSON parse failed: %1").arg(parse_error.errorString());
        }
        return false;
    }

    *json = document.object();
    return true;
}

QString titleCaseHumanBone(const QString& vrm_key) {
    if (vrm_key.isEmpty()) {
        return QString();
    }
    QString result = vrm_key;
    result[0] = result[0].toUpper();
    return result;
}

QJsonObject humanoidObject(const QJsonObject& json) {
    const QJsonObject extensions = json.value(QStringLiteral("extensions")).toObject();
    const QJsonObject vrm_animation = extensions.value(QStringLiteral("VRMC_vrm_animation")).toObject();
    if (!vrm_animation.isEmpty()) {
        return vrm_animation.value(QStringLiteral("humanoid")).toObject();
    }
    const QJsonObject vrm = extensions.value(QStringLiteral("VRMC_vrm")).toObject();
    return vrm.value(QStringLiteral("humanoid")).toObject();
}

} // namespace

bool loadGltfVrmHumanoidBones(const QString& path,
                              QHash<QString, QString>* node_name_to_human_bone,
                              QString* error_message) {
    if (!node_name_to_human_bone) {
        return false;
    }
    node_name_to_human_bone->clear();

    QJsonObject json;
    if (!readGltfJson(path, &json, error_message)) {
        return false;
    }

    const QJsonArray nodes = json.value(QStringLiteral("nodes")).toArray();
    const QJsonObject human_bones = humanoidObject(json).value(QStringLiteral("humanBones")).toObject();
    if (nodes.isEmpty() || human_bones.isEmpty()) {
        if (error_message) {
            *error_message = QStringLiteral("VRM humanoid mapping was not found.");
        }
        return false;
    }

    for (auto it = human_bones.begin(); it != human_bones.end(); ++it) {
        const int node_index = it.value().toObject().value(QStringLiteral("node")).toInt(-1);
        if (node_index < 0 || node_index >= nodes.size()) {
            continue;
        }
        const QString node_name = nodes[node_index].toObject().value(QStringLiteral("name")).toString();
        if (!node_name.isEmpty()) {
            node_name_to_human_bone->insert(node_name, titleCaseHumanBone(it.key()));
        }
    }

    if (node_name_to_human_bone->isEmpty()) {
        if (error_message) {
            *error_message = QStringLiteral("VRM humanoid mapping did not reference named nodes.");
        }
        return false;
    }
    if (error_message) {
        error_message->clear();
    }
    return true;
}

} // namespace haorendergi
