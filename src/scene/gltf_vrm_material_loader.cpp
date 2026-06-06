#include "scene/gltf_vrm_material_loader.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstring>

namespace haorendergi {
namespace {

constexpr quint32 kGlbMagic = 0x46546C67u;
constexpr quint32 kJsonChunk = 0x4E4F534Au;
constexpr quint32 kBinChunk = 0x004E4942u;

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

bool readGltfDocument(const QString& path,
                      QJsonObject* json,
                      QVector<QByteArray>* buffers,
                      QString* error_message) {
    QByteArray file_bytes;
    if (!readFileBytes(path, &file_bytes)) {
        if (error_message) {
            *error_message = QStringLiteral("Failed to open %1").arg(QDir::toNativeSeparators(path));
        }
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

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(json_bytes.trimmed(), &parse_error);
    if (document.isNull() || !document.isObject()) {
        if (error_message) {
            *error_message = QStringLiteral("glTF material JSON parse failed: %1").arg(parse_error.errorString());
        }
        return false;
    }
    *json = document.object();

    buffers->clear();
    const QJsonArray buffer_array = json->value(QStringLiteral("buffers")).toArray();
    for (const QJsonValue& value : buffer_array) {
        const QJsonObject buffer_object = value.toObject();
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
        buffers->push_back(readFileBytes(external_path, &external) ? external : QByteArray());
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

Eigen::Vector3f readColor3(const QJsonArray& array, const Eigen::Vector3f& fallback) {
    if (array.size() < 3) {
        return fallback;
    }
    return Eigen::Vector3f(static_cast<float>(array[0].toDouble(fallback.x())),
                           static_cast<float>(array[1].toDouble(fallback.y())),
                           static_cast<float>(array[2].toDouble(fallback.z())));
}

float readAlpha(const QJsonArray& array, float fallback) {
    return array.size() >= 4 ? static_cast<float>(array[3].toDouble(fallback)) : fallback;
}

QByteArray viewBytes(const QJsonObject& json, const QVector<QByteArray>& buffers, int buffer_view_index) {
    const QJsonObject view = objectAt(json.value(QStringLiteral("bufferViews")).toArray(), buffer_view_index);
    const int buffer_index = view.value(QStringLiteral("buffer")).toInt(0);
    if (buffer_index < 0 || buffer_index >= buffers.size()) {
        return QByteArray();
    }
    const int offset = view.value(QStringLiteral("byteOffset")).toInt(0);
    const int length = view.value(QStringLiteral("byteLength")).toInt(0);
    if (offset < 0 || length <= 0 || offset + length > buffers[buffer_index].size()) {
        return QByteArray();
    }
    return buffers[buffer_index].mid(offset, length);
}

QVector<TextureData> loadImages(const QString& path,
                                const QJsonObject& json,
                                const QVector<QByteArray>& buffers) {
    const QFileInfo file_info(path);
    const QJsonArray images = json.value(QStringLiteral("images")).toArray();
    QVector<TextureData> loaded;
    loaded.resize(images.size());
    for (int image_index = 0; image_index < images.size(); ++image_index) {
        const QJsonObject image_object = images[image_index].toObject();
        QByteArray bytes;
        QString source_label;
        if (image_object.contains(QStringLiteral("bufferView"))) {
            bytes = viewBytes(json, buffers, image_object.value(QStringLiteral("bufferView")).toInt(-1));
            source_label = QStringLiteral("embedded:%1").arg(image_index);
        } else {
            const QString uri = image_object.value(QStringLiteral("uri")).toString();
            if (uri.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
                const int comma = uri.indexOf(QLatin1Char(','));
                bytes = comma >= 0 ? QByteArray::fromBase64(uri.mid(comma + 1).toUtf8()) : QByteArray();
                source_label = QStringLiteral("data-uri:%1").arg(image_index);
            } else if (!uri.isEmpty()) {
                const QString image_path = QDir(file_info.absolutePath()).filePath(uri);
                readFileBytes(image_path, &bytes);
                source_label = QDir::toNativeSeparators(image_path);
            }
        }

        QImage image;
        if (!bytes.isEmpty()) {
            image.loadFromData(bytes);
        }
        if (!image.isNull()) {
            TextureData texture;
            texture.path = source_label.toStdString();
            texture.image = image.convertToFormat(QImage::Format_RGBA8888);
            loaded[image_index] = std::move(texture);
        }
    }
    return loaded;
}

TextureData textureAt(const QJsonObject& texture_info,
                      const QJsonArray& textures,
                      const QVector<TextureData>& images) {
    const int texture_index = texture_info.value(QStringLiteral("index")).toInt(-1);
    const QJsonObject texture = objectAt(textures, texture_index);
    const int image_index = texture.value(QStringLiteral("source")).toInt(-1);
    if (image_index < 0 || image_index >= images.size()) {
        return TextureData();
    }
    return images[image_index];
}

int alphaModeIndex(const QString& mode) {
    if (mode == QStringLiteral("MASK")) {
        return 1;
    }
    if (mode == QStringLiteral("BLEND")) {
        return 2;
    }
    return 0;
}

MaterialData parseMaterial(const QJsonObject& material_object,
                           const QJsonArray& textures,
                           const QVector<TextureData>& images) {
    MaterialData material;
    material.name = material_object.value(QStringLiteral("name")).toString().toStdString();
    material.alpha_mode = alphaModeIndex(material_object.value(QStringLiteral("alphaMode")).toString(QStringLiteral("OPAQUE")));
    material.alpha_cutoff = static_cast<float>(material_object.value(QStringLiteral("alphaCutoff")).toDouble(0.5));
    material.double_sided = material_object.value(QStringLiteral("doubleSided")).toBool(false);

    const QJsonObject pbr = material_object.value(QStringLiteral("pbrMetallicRoughness")).toObject();
    const QJsonArray base_factor = pbr.value(QStringLiteral("baseColorFactor")).toArray();
    material.base_color_factor = readColor3(base_factor, Eigen::Vector3f::Ones());
    material.base_alpha_factor = readAlpha(base_factor, 1.0f);
    material.metallic_factor = static_cast<float>(pbr.value(QStringLiteral("metallicFactor")).toDouble(0.0));
    material.roughness_factor = static_cast<float>(pbr.value(QStringLiteral("roughnessFactor")).toDouble(0.85));
    material.base_color_texture = textureAt(pbr.value(QStringLiteral("baseColorTexture")).toObject(), textures, images);
    material.diffuse_texture = material.base_color_texture;

    const QJsonObject normal = material_object.value(QStringLiteral("normalTexture")).toObject();
    material.normal_texture = textureAt(normal, textures, images);
    const QJsonObject emissive = material_object.value(QStringLiteral("emissiveTexture")).toObject();
    material.emissive_texture = textureAt(emissive, textures, images);
    material.emissive_factor = readColor3(material_object.value(QStringLiteral("emissiveFactor")).toArray(), Eigen::Vector3f::Zero());

    const QJsonObject extensions = material_object.value(QStringLiteral("extensions")).toObject();
    material.unlit = extensions.contains(QStringLiteral("KHR_materials_unlit"));
    const QJsonObject mtoon = extensions.value(QStringLiteral("VRMC_materials_mtoon")).toObject();
    if (!mtoon.isEmpty()) {
        material.mtoon = true;
        material.unlit = true;
        material.transparent_with_z_write = mtoon.value(QStringLiteral("transparentWithZWrite")).toBool(false);
        material.render_queue_offset = mtoon.value(QStringLiteral("renderQueueOffsetNumber")).toInt(0);
        material.metallic_factor = 0.0f;
        material.roughness_factor = 1.0f;
        material.mtoon_shade_color_factor = readColor3(mtoon.value(QStringLiteral("shadeColorFactor")).toArray(), Eigen::Vector3f::Ones());
        material.mtoon_rim_color_factor = readColor3(mtoon.value(QStringLiteral("parametricRimColorFactor")).toArray(), Eigen::Vector3f::Zero());
        material.mtoon_outline_color_factor = readColor3(mtoon.value(QStringLiteral("outlineColorFactor")).toArray(), Eigen::Vector3f::Zero());
        material.mtoon_shading_shift = static_cast<float>(mtoon.value(QStringLiteral("shadingShiftFactor")).toDouble(0.0));
        material.mtoon_shading_toony = static_cast<float>(mtoon.value(QStringLiteral("shadingToonyFactor")).toDouble(0.9));
        material.mtoon_rim_lift = static_cast<float>(mtoon.value(QStringLiteral("parametricRimLiftFactor")).toDouble(0.0));
        material.mtoon_rim_fresnel_power = static_cast<float>(mtoon.value(QStringLiteral("parametricRimFresnelPowerFactor")).toDouble(1.0));
        material.mtoon_outline_width = static_cast<float>(mtoon.value(QStringLiteral("outlineWidthFactor")).toDouble(0.0));
        material.mtoon_shade_multiply_texture = textureAt(mtoon.value(QStringLiteral("shadeMultiplyTexture")).toObject(), textures, images);
        material.mtoon_matcap_texture = textureAt(mtoon.value(QStringLiteral("matcapTexture")).toObject(), textures, images);
        material.mtoon_outline_width_texture = textureAt(mtoon.value(QStringLiteral("outlineWidthMultiplyTexture")).toObject(), textures, images);

        if (!material.base_color_texture.valid() && material.mtoon_shade_multiply_texture.valid()) {
            material.base_color_texture = material.mtoon_shade_multiply_texture;
            material.diffuse_texture = material.mtoon_shade_multiply_texture;
        }
    }
    return material;
}

} // namespace

bool loadGltfVrmMaterialOverrides(const QString& path,
                                  std::vector<MaterialData>* materials,
                                  QString* error_message) {
    if (!materials) {
        return false;
    }
    materials->clear();

    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QStringLiteral("glb") &&
        suffix != QStringLiteral("gltf") &&
        suffix != QStringLiteral("vrm")) {
        return false;
    }

    QJsonObject json;
    QVector<QByteArray> buffers;
    if (!readGltfDocument(path, &json, &buffers, error_message)) {
        return false;
    }

    const QJsonArray material_array = json.value(QStringLiteral("materials")).toArray();
    const QJsonArray textures = json.value(QStringLiteral("textures")).toArray();
    const QVector<TextureData> images = loadImages(path, json, buffers);
    materials->reserve(material_array.size());
    for (const QJsonValue& value : material_array) {
        materials->push_back(parseMaterial(value.toObject(), textures, images));
    }
    return !materials->empty();
}

} // namespace haorendergi
