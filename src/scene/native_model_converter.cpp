#include "scene/native_model_converter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>

namespace haorendergi {
namespace {

bool hasIncompleteFlag(const aiScene* scene) {
    return scene && (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0;
}

QString normalizedExtension(QString extension) {
    extension = extension.trimmed().toLower();
    if (extension.startsWith(QStringLiteral("."))) {
        extension.remove(0, 1);
    }
    if (extension == QStringLiteral("vrm")) {
        return QStringLiteral("glb");
    }
    return extension;
}

bool isGeometryOnlyTarget(const QString& extension) {
    const QString ext = normalizedExtension(extension);
    return ext == QStringLiteral("obj") ||
           ext == QStringLiteral("stl") ||
           ext == QStringLiteral("ply") ||
           ext == QStringLiteral("3mf") ||
           ext == QStringLiteral("3ds");
}

QStringList preferredFormatIdsForExtension(const QString& extension) {
    const QString ext = normalizedExtension(extension);
    if (ext == QStringLiteral("glb")) {
        return { QStringLiteral("glb2"), QStringLiteral("glb") };
    }
    if (ext == QStringLiteral("gltf")) {
        return { QStringLiteral("gltf2"), QStringLiteral("gltf") };
    }
    if (ext == QStringLiteral("fbx")) {
        return { QStringLiteral("fbx"), QStringLiteral("fbxa"), QStringLiteral("fbx_binary") };
    }
    if (ext == QStringLiteral("obj")) {
        return { QStringLiteral("obj") };
    }
    if (ext == QStringLiteral("dae")) {
        return { QStringLiteral("collada"), QStringLiteral("dae") };
    }
    if (ext == QStringLiteral("stl")) {
        return { QStringLiteral("stl"), QStringLiteral("stlb") };
    }
    if (ext == QStringLiteral("ply")) {
        return { QStringLiteral("ply"), QStringLiteral("plyb") };
    }
    if (ext == QStringLiteral("3mf")) {
        return { QStringLiteral("3mf") };
    }
    return { ext };
}

QString importErrorText(const Assimp::Importer& importer) {
    const QString error = QString::fromUtf8(importer.GetErrorString()).trimmed();
    return error.isEmpty() ? QStringLiteral("Assimp returned no detailed error.") : error;
}

} // namespace

QVector<NativeExportFormat> NativeModelConverter::availableFormats() const {
    Assimp::Exporter exporter;
    QVector<NativeExportFormat> formats;
    const std::size_t count = exporter.GetExportFormatCount();
    formats.reserve(static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) {
        const aiExportFormatDesc* desc = exporter.GetExportFormatDescription(i);
        if (!desc || !desc->id) {
            continue;
        }
        NativeExportFormat format;
        format.id = QString::fromUtf8(desc->id);
        format.description = desc->description ? QString::fromUtf8(desc->description) : QString();
        format.extension = desc->fileExtension ? QString::fromUtf8(desc->fileExtension).toLower() : QString();
        formats.push_back(format);
    }
    return formats;
}

QStringList NativeModelConverter::supportedTargetExtensions() const {
    QStringList extensions;
    for (const NativeExportFormat& format : availableFormats()) {
        const QString extension = normalizedExtension(format.extension);
        if (!extension.isEmpty() && !extensions.contains(extension)) {
            extensions << extension;
        }
    }
    extensions.sort();
    return extensions;
}

bool NativeModelConverter::canExportExtension(const QString& extension) const {
    return !resolveFormatId(extension).isEmpty();
}

QString NativeModelConverter::resolveFormatId(const QString& target_extension,
                                              QString* available_formats_text) const {
    const QString ext = normalizedExtension(target_extension);
    const QVector<NativeExportFormat> formats = availableFormats();
    QStringList available;
    for (const NativeExportFormat& format : formats) {
        available << QStringLiteral("%1.%2").arg(format.id, format.extension);
    }
    if (available_formats_text) {
        *available_formats_text = available.join(QStringLiteral(", "));
    }

    const QStringList preferred_ids = preferredFormatIdsForExtension(ext);
    for (const QString& preferred_id : preferred_ids) {
        for (const NativeExportFormat& format : formats) {
            if (format.id.compare(preferred_id, Qt::CaseInsensitive) == 0) {
                return format.id;
            }
        }
    }

    for (const NativeExportFormat& format : formats) {
        if (normalizedExtension(format.extension).compare(ext, Qt::CaseInsensitive) == 0) {
            return format.id;
        }
    }

    return QString();
}

bool NativeModelConverter::convert(const QString& source_path,
                                   const QString& output_path,
                                   const QString& target_extension,
                                   QString* status_message) const {
    if (source_path.trimmed().isEmpty() || output_path.trimmed().isEmpty()) {
        if (status_message) {
            *status_message = QStringLiteral("Native model converter needs both source and output paths.");
        }
        return false;
    }
    if (!QFileInfo::exists(source_path)) {
        if (status_message) {
            *status_message = QStringLiteral("Source model does not exist: %1").arg(QDir::toNativeSeparators(source_path));
        }
        return false;
    }

    QString available_formats;
    const QString format_id = resolveFormatId(target_extension, &available_formats);
    if (format_id.isEmpty()) {
        if (status_message) {
            *status_message = QStringLiteral("Current Assimp build cannot export `%1`. Available exporters: %2.")
                .arg(normalizedExtension(target_extension), available_formats);
        }
        return false;
    }

    const unsigned int import_flag_sets[] = {
        aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices |
            aiProcess_LimitBoneWeights |
            aiProcess_ValidateDataStructure,
        aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_LimitBoneWeights,
        0u
    };

    QString last_import_error;
    const QByteArray encoded_source = QFile::encodeName(source_path);
    for (const unsigned int flags : import_flag_sets) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(encoded_source.constData(), flags);
        if (!scene || !scene->mRootNode) {
            last_import_error = importErrorText(importer);
            continue;
        }
        if (scene->mNumMeshes == 0 && isGeometryOnlyTarget(target_extension)) {
            if (status_message) {
                *status_message = QStringLiteral("Source contains no meshes, so `%1` would be an empty geometry file. Use GLB/GLTF/FBX/DAE for animation or skeleton-only assets.")
                    .arg(normalizedExtension(target_extension));
            }
            return false;
        }

        QDir().mkpath(QFileInfo(output_path).absolutePath());
        QFile::remove(output_path);
        Assimp::Exporter exporter;
        const QByteArray encoded_output = QFile::encodeName(output_path);
        const aiReturn result = exporter.Export(scene,
                                                format_id.toStdString(),
                                                encoded_output.constData(),
                                                aiProcess_ValidateDataStructure);
        if (result == aiReturn_SUCCESS && QFileInfo::exists(output_path)) {
            if (status_message) {
                *status_message = QStringLiteral("Native Assimp conversion completed using exporter `%1`%2.")
                    .arg(format_id,
                         hasIncompleteFlag(scene) ? QStringLiteral(" from an incomplete-flagged scene") : QString());
            }
            return true;
        }

        const QString export_error = QString::fromUtf8(exporter.GetErrorString()).trimmed();
        if (status_message) {
            *status_message = QStringLiteral("Native Assimp export to `%1` failed using `%2`: %3")
                .arg(normalizedExtension(target_extension),
                     format_id,
                     export_error.isEmpty() ? QStringLiteral("Assimp returned no detailed error.") : export_error);
        }
        return false;
    }

    if (status_message) {
        *status_message = QStringLiteral("Native Assimp import failed before `%1` export: %2")
            .arg(normalizedExtension(target_extension),
                 last_import_error.isEmpty() ? QStringLiteral("Assimp returned no detailed error.") : last_import_error);
    }
    return false;
}

} // namespace haorendergi
