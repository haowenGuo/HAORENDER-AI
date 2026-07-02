#include "scene/vrm_native_writer.h"

#include "rigging/bone_name_normalizer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUuid>

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace haorendergi {
namespace {

constexpr quint32 kGlbMagic = 0x46546C67u;
constexpr quint32 kGlbVersion = 2u;
constexpr quint32 kJsonChunk = 0x4E4F534Au;

struct GlbChunk {
    quint32 type = 0;
    QByteArray data;
};

struct GlbDocument {
    QJsonObject json;
    std::vector<GlbChunk> non_json_chunks;
};

struct HumanBoneCandidate {
    QString bone_name;
    QString vrm_key;
    int node_index = -1;
    float score = 0.0f;
};

struct ChainBone {
    const SkeletonBone* bone = nullptr;
    int order = 0;
};

template <typename T>
T readLe(const QByteArray& data, int offset) {
    T value {};
    if (offset >= 0 && offset + static_cast<int>(sizeof(T)) <= data.size()) {
        std::memcpy(&value, data.constData() + offset, sizeof(T));
    }
    return value;
}

void appendLe32(QByteArray* data, quint32 value) {
    if (!data) {
        return;
    }
    const char bytes[4] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu)
    };
    data->append(bytes, 4);
}

void padTo4(QByteArray* data, char padding) {
    if (!data) {
        return;
    }
    while ((data->size() % 4) != 0) {
        data->append(padding);
    }
}

QString canonicalToVrmKey(const QString& canonical_name) {
    if (canonical_name.isEmpty() ||
        canonical_name == QStringLiteral("Root") ||
        canonical_name == QStringLiteral("Eye")) {
        return QString();
    }
    QString key = canonical_name;
    key[0] = key[0].toLower();
    return key;
}

QStringList requiredVrmHumanBones() {
    return {
        QStringLiteral("hips"),
        QStringLiteral("spine"),
        QStringLiteral("head"),
        QStringLiteral("leftUpperLeg"),
        QStringLiteral("leftLowerLeg"),
        QStringLiteral("leftFoot"),
        QStringLiteral("rightUpperLeg"),
        QStringLiteral("rightLowerLeg"),
        QStringLiteral("rightFoot"),
        QStringLiteral("leftUpperArm"),
        QStringLiteral("leftLowerArm"),
        QStringLiteral("leftHand"),
        QStringLiteral("rightUpperArm"),
        QStringLiteral("rightLowerArm"),
        QStringLiteral("rightHand")
    };
}

RigSide sideFromLooseName(const QString& normalized_name) {
    const QString padded = QStringLiteral(" ") + normalized_name + QStringLiteral(" ");
    if (padded.contains(QStringLiteral(" left ")) ||
        padded.contains(QStringLiteral(" l ")) ||
        normalized_name.contains(QStringLiteral("left")) ||
        normalized_name.contains(QStringLiteral(" l "))) {
        return RigSide::Left;
    }
    if (padded.contains(QStringLiteral(" right ")) ||
        padded.contains(QStringLiteral(" r ")) ||
        normalized_name.contains(QStringLiteral("right")) ||
        normalized_name.contains(QStringLiteral(" r "))) {
        return RigSide::Right;
    }
    return RigSide::Unknown;
}

int lastNumberInName(const QString& name, int fallback) {
    static const QRegularExpression number_pattern(QStringLiteral("(\\d+)"));
    QRegularExpressionMatchIterator it = number_pattern.globalMatch(name);
    int value = fallback;
    while (it.hasNext()) {
        bool ok = false;
        const int parsed = it.next().captured(1).toInt(&ok);
        if (ok) {
            value = parsed;
        }
    }
    return value;
}

void sortChain(std::vector<ChainBone>* chain) {
    if (!chain) {
        return;
    }
    std::sort(chain->begin(), chain->end(), [](const ChainBone& lhs, const ChainBone& rhs) {
        const int left_depth = lhs.bone ? lhs.bone->depth : 0;
        const int right_depth = rhs.bone ? rhs.bone->depth : 0;
        if (left_depth != right_depth) {
            return left_depth < right_depth;
        }
        return lhs.order < rhs.order;
    });
}

bool readGlbDocument(const QString& path, GlbDocument* document, QString* error_message) {
    if (!document) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error_message) {
            *error_message = QStringLiteral("Failed to open GLB: %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.size() < 20 ||
        readLe<quint32>(bytes, 0) != kGlbMagic ||
        readLe<quint32>(bytes, 4) != kGlbVersion) {
        if (error_message) {
            *error_message = QStringLiteral("Native VRM writer expected a GLB 2.0 file.");
        }
        return false;
    }

    const quint32 declared_length = readLe<quint32>(bytes, 8);
    if (declared_length > static_cast<quint32>(bytes.size())) {
        if (error_message) {
            *error_message = QStringLiteral("GLB header length is larger than the file.");
        }
        return false;
    }

    QByteArray json_bytes;
    document->non_json_chunks.clear();
    int cursor = 12;
    const int readable_length = static_cast<int>(std::min<quint32>(declared_length, static_cast<quint32>(bytes.size())));
    while (cursor + 8 <= readable_length) {
        const quint32 chunk_length = readLe<quint32>(bytes, cursor);
        const quint32 chunk_type = readLe<quint32>(bytes, cursor + 4);
        cursor += 8;
        if (chunk_length > static_cast<quint32>(readable_length - cursor)) {
            if (error_message) {
                *error_message = QStringLiteral("GLB chunk length is invalid.");
            }
            return false;
        }
        const QByteArray chunk = bytes.mid(cursor, static_cast<int>(chunk_length));
        cursor += static_cast<int>(chunk_length);
        if (chunk_type == kJsonChunk) {
            json_bytes = chunk;
        } else {
            document->non_json_chunks.push_back(GlbChunk{ chunk_type, chunk });
        }
    }

    QJsonParseError parse_error;
    const QJsonDocument json_document = QJsonDocument::fromJson(json_bytes.trimmed(), &parse_error);
    if (!json_document.isObject()) {
        if (error_message) {
            *error_message = QStringLiteral("GLB JSON parse failed: %1").arg(parse_error.errorString());
        }
        return false;
    }
    document->json = json_document.object();
    return true;
}

bool writeGlbDocument(const QString& path, const GlbDocument& document, QString* error_message) {
    QByteArray json_bytes = QJsonDocument(document.json).toJson(QJsonDocument::Compact);
    padTo4(&json_bytes, ' ');

    quint32 total_length = 12u + 8u + static_cast<quint32>(json_bytes.size());
    for (const GlbChunk& chunk : document.non_json_chunks) {
        total_length += 8u + static_cast<quint32>(chunk.data.size());
    }

    QByteArray bytes;
    bytes.reserve(static_cast<int>(total_length));
    appendLe32(&bytes, kGlbMagic);
    appendLe32(&bytes, kGlbVersion);
    appendLe32(&bytes, total_length);
    appendLe32(&bytes, static_cast<quint32>(json_bytes.size()));
    appendLe32(&bytes, kJsonChunk);
    bytes.append(json_bytes);
    for (const GlbChunk& chunk : document.non_json_chunks) {
        appendLe32(&bytes, static_cast<quint32>(chunk.data.size()));
        appendLe32(&bytes, chunk.type);
        bytes.append(chunk.data);
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error_message) {
            *error_message = QStringLiteral("Failed to write VRM file: %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (error_message) {
            *error_message = QStringLiteral("Failed to write all VRM bytes.");
        }
        return false;
    }
    return true;
}

void appendUniqueString(QJsonArray* array, const QString& value) {
    if (!array || value.isEmpty()) {
        return;
    }
    for (const QJsonValue& item : *array) {
        if (item.toString() == value) {
            return;
        }
    }
    array->append(value);
}

QHash<QString, int> buildNodeNameIndex(const QJsonArray& nodes) {
    QHash<QString, int> index;
    for (int i = 0; i < nodes.size(); ++i) {
        const QString name = nodes[i].toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && !index.contains(name)) {
            index.insert(name, i);
        }
    }
    return index;
}

QHash<QString, int> buildNormalizedNodeNameIndex(const QJsonArray& nodes) {
    QHash<QString, int> index;
    for (int i = 0; i < nodes.size(); ++i) {
        const QString name = nodes[i].toObject().value(QStringLiteral("name")).toString();
        const QString normalized = normalizeBoneName(name);
        if (!normalized.isEmpty() && !index.contains(normalized)) {
            index.insert(normalized, i);
        }
    }
    return index;
}

bool exportSourceToGlb(const QString& source_path, const QString& glb_path, QString* error_message) {
    QFile::remove(glb_path);

    Assimp::Importer importer;
    const QByteArray input_path = QFile::encodeName(source_path);
    const aiScene* scene = importer.ReadFile(
        input_path.constData(),
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights |
        aiProcess_ValidateDataStructure);
    if (!scene || !scene->mRootNode) {
        if (error_message) {
            *error_message = QStringLiteral("Assimp import failed before VRM export: %1")
                .arg(QString::fromUtf8(importer.GetErrorString()));
        }
        return false;
    }
    if (scene->mNumMeshes == 0) {
        if (error_message) {
            *error_message = QStringLiteral("Source contains no meshes. VRM export needs an avatar mesh; use GLB/GLTF/FBX/DAE for animation or skeleton-only assets.");
        }
        return false;
    }

    Assimp::Exporter exporter;
    QString format_id;
    QStringList available_formats;
    for (std::size_t i = 0; i < exporter.GetExportFormatCount(); ++i) {
        const aiExportFormatDesc* desc = exporter.GetExportFormatDescription(i);
        if (!desc || !desc->id) {
            continue;
        }
        const QString id = QString::fromUtf8(desc->id);
        const QString extension = desc->fileExtension ? QString::fromUtf8(desc->fileExtension) : QString();
        available_formats << QStringLiteral("%1.%2").arg(id, extension);
        if (id.compare(QStringLiteral("glb2"), Qt::CaseInsensitive) == 0 ||
            (extension.compare(QStringLiteral("glb"), Qt::CaseInsensitive) == 0 &&
             id.contains(QStringLiteral("gltf"), Qt::CaseInsensitive))) {
            format_id = id;
            break;
        }
    }
    if (format_id.isEmpty()) {
        if (error_message) {
            *error_message = QStringLiteral("This Assimp build does not expose a GLB exporter. Available exporters: %1.")
                .arg(available_formats.join(QStringLiteral(", ")));
        }
        return false;
    }

    QDir().mkpath(QFileInfo(glb_path).absolutePath());
    const QByteArray output_path = QFile::encodeName(glb_path);
    const aiReturn result = exporter.Export(scene,
                                            format_id.toStdString(),
                                            output_path.constData(),
                                            aiProcess_ValidateDataStructure);
    if (result != aiReturn_SUCCESS || !QFileInfo::exists(glb_path)) {
        if (error_message) {
            *error_message = QStringLiteral("Assimp GLB export failed using `%1`: %2")
                .arg(format_id, QString::fromUtf8(exporter.GetErrorString()));
        }
        return false;
    }
    return true;
}

QJsonObject buildMeta(const QString& source_path) {
    const QString name = QFileInfo(source_path).completeBaseName().trimmed();
    QJsonObject meta;
    meta.insert(QStringLiteral("name"), name.isEmpty() ? QStringLiteral("HAORENDER-AI Avatar") : name);
    meta.insert(QStringLiteral("version"), QStringLiteral("1.0"));
    meta.insert(QStringLiteral("authors"), QJsonArray{ QStringLiteral("Unknown") });
    meta.insert(QStringLiteral("licenseUrl"), QStringLiteral("https://vrm.dev/licenses/1.0/"));

    QJsonObject extras;
    extras.insert(QStringLiteral("generator"), QStringLiteral("HAORENDER-AI native VRM writer"));
    extras.insert(QStringLiteral("warning"), QStringLiteral("Author and license metadata are placeholders. Verify before distribution."));
    meta.insert(QStringLiteral("extras"), extras);
    return meta;
}

bool patchVrmExtension(GlbDocument* document,
                       const QString& source_path,
                       const SkeletonGraph& skeleton,
                       QString* status_message) {
    if (!document) {
        return false;
    }

    const QJsonArray nodes = document->json.value(QStringLiteral("nodes")).toArray();
    if (nodes.isEmpty()) {
        if (status_message) {
            *status_message = QStringLiteral("Exported GLB has no nodes; cannot write VRM humanoid mapping.");
        }
        return false;
    }

    const QHash<QString, int> exact_nodes = buildNodeNameIndex(nodes);
    const QHash<QString, int> normalized_nodes = buildNormalizedNodeNameIndex(nodes);

    QHash<QString, HumanBoneCandidate> candidates;
    const auto add_candidate = [&](const QString& vrm_key, const SkeletonBone& bone, float base_score) {
        if (vrm_key.isEmpty()) {
            return;
        }
        int node_index = exact_nodes.value(bone.name, -1);
        if (node_index < 0) {
            node_index = normalized_nodes.value(normalizeBoneName(bone.name), -1);
        }
        if (node_index < 0) {
            return;
        }

        const float score = base_score +
            (bone.referenced_by_skin ? 0.20f : 0.0f) -
            static_cast<float>(std::max(0, bone.depth)) * 0.001f;
        if (!candidates.contains(vrm_key) || candidates.value(vrm_key).score < score) {
            candidates.insert(vrm_key, HumanBoneCandidate{ bone.name, vrm_key, node_index, score });
        }
    };

    for (const SkeletonBone& bone : skeleton.bones) {
        add_candidate(canonicalToVrmKey(bone.semantic.canonical_name), bone, bone.semantic.confidence);
    }

    std::vector<ChainBone> torso_chain;
    std::vector<ChainBone> neck_chain;
    std::vector<ChainBone> left_leg_chain;
    std::vector<ChainBone> right_leg_chain;
    std::vector<ChainBone> left_arm_chain;
    std::vector<ChainBone> right_arm_chain;

    for (const SkeletonBone& bone : skeleton.bones) {
        const QString normalized = normalizeBoneName(bone.name);
        const int order = lastNumberInName(normalized, 0);
        if (normalized.contains(QStringLiteral("torso joint")) ||
            normalized.contains(QStringLiteral("spine joint"))) {
            torso_chain.push_back(ChainBone{ &bone, order });
            continue;
        }
        if (normalized.contains(QStringLiteral("neck joint"))) {
            neck_chain.push_back(ChainBone{ &bone, order });
            continue;
        }

        const RigSide side = sideFromLooseName(normalized);
        if (normalized.contains(QStringLiteral("leg joint"))) {
            if (side == RigSide::Left) {
                left_leg_chain.push_back(ChainBone{ &bone, order });
            } else if (side == RigSide::Right) {
                right_leg_chain.push_back(ChainBone{ &bone, order });
            }
            continue;
        }
        if (normalized.contains(QStringLiteral("arm joint"))) {
            if (side == RigSide::Left) {
                left_arm_chain.push_back(ChainBone{ &bone, order });
            } else if (side == RigSide::Right) {
                right_arm_chain.push_back(ChainBone{ &bone, order });
            }
        }
    }

    sortChain(&torso_chain);
    sortChain(&neck_chain);
    sortChain(&left_leg_chain);
    sortChain(&right_leg_chain);
    sortChain(&left_arm_chain);
    sortChain(&right_arm_chain);

    const auto add_chain_candidate = [&](const QString& key, const std::vector<ChainBone>& chain, std::size_t index, float score) {
        if (index < chain.size() && chain[index].bone) {
            add_candidate(key, *chain[index].bone, score);
        }
    };
    add_chain_candidate(QStringLiteral("hips"), torso_chain, 0, 0.82f);
    add_chain_candidate(QStringLiteral("spine"), torso_chain, 1, 0.78f);
    add_chain_candidate(QStringLiteral("chest"), torso_chain, 2, 0.74f);
    add_chain_candidate(QStringLiteral("neck"), neck_chain, 0, 0.78f);
    add_chain_candidate(QStringLiteral("head"), neck_chain, neck_chain.size() > 1 ? 1 : 0, 0.78f);

    const auto add_limb_chain = [&](const QString& side_prefix, const std::vector<ChainBone>& chain, bool leg) {
        if (leg) {
            add_chain_candidate(side_prefix + QStringLiteral("UpperLeg"), chain, 0, 0.78f);
            add_chain_candidate(side_prefix + QStringLiteral("LowerLeg"), chain, 1, 0.78f);
            add_chain_candidate(side_prefix + QStringLiteral("Foot"), chain, 2, 0.76f);
            add_chain_candidate(side_prefix + QStringLiteral("Toes"), chain, 3, 0.68f);
        } else {
            add_chain_candidate(side_prefix + QStringLiteral("UpperArm"), chain, 0, 0.78f);
            add_chain_candidate(side_prefix + QStringLiteral("LowerArm"), chain, 1, 0.78f);
            add_chain_candidate(side_prefix + QStringLiteral("Hand"), chain, 2, 0.76f);
        }
    };
    add_limb_chain(QStringLiteral("left"), left_leg_chain, true);
    add_limb_chain(QStringLiteral("right"), right_leg_chain, true);
    add_limb_chain(QStringLiteral("left"), left_arm_chain, false);
    add_limb_chain(QStringLiteral("right"), right_arm_chain, false);

    QSet<int> used_nodes;
    QJsonObject human_bones;
    for (auto it = candidates.begin(); it != candidates.end(); ++it) {
        const HumanBoneCandidate candidate = it.value();
        if (candidate.node_index < 0 || used_nodes.contains(candidate.node_index)) {
            continue;
        }
        used_nodes.insert(candidate.node_index);
        QJsonObject bone_object;
        bone_object.insert(QStringLiteral("node"), candidate.node_index);
        human_bones.insert(candidate.vrm_key, bone_object);
    }

    QStringList missing_required;
    for (const QString& required : requiredVrmHumanBones()) {
        if (!human_bones.contains(required)) {
            missing_required << required;
        }
    }
    if (!missing_required.isEmpty()) {
        if (status_message) {
            *status_message = QStringLiteral("Native VRM writer found %1 humanoid bones but is missing required VRM bones: %2.")
                .arg(human_bones.size())
                .arg(missing_required.join(QStringLiteral(", ")));
        }
        return false;
    }

    QJsonObject humanoid;
    humanoid.insert(QStringLiteral("humanBones"), human_bones);

    QJsonObject vrm;
    vrm.insert(QStringLiteral("specVersion"), QStringLiteral("1.0"));
    vrm.insert(QStringLiteral("meta"), buildMeta(source_path));
    vrm.insert(QStringLiteral("humanoid"), humanoid);

    QJsonObject extensions = document->json.value(QStringLiteral("extensions")).toObject();
    extensions.insert(QStringLiteral("VRMC_vrm"), vrm);
    document->json.insert(QStringLiteral("extensions"), extensions);

    QJsonArray extensions_used = document->json.value(QStringLiteral("extensionsUsed")).toArray();
    appendUniqueString(&extensions_used, QStringLiteral("VRMC_vrm"));
    document->json.insert(QStringLiteral("extensionsUsed"), extensions_used);

    QJsonArray extensions_required = document->json.value(QStringLiteral("extensionsRequired")).toArray();
    appendUniqueString(&extensions_required, QStringLiteral("VRMC_vrm"));
    document->json.insert(QStringLiteral("extensionsRequired"), extensions_required);

    QJsonObject asset = document->json.value(QStringLiteral("asset")).toObject();
    const QString old_generator = asset.value(QStringLiteral("generator")).toString();
    asset.insert(QStringLiteral("generator"),
                 old_generator.isEmpty()
                     ? QStringLiteral("HAORENDER-AI native VRM writer")
                     : old_generator + QStringLiteral(" + HAORENDER-AI native VRM writer"));
    document->json.insert(QStringLiteral("asset"), asset);

    QJsonObject extras = document->json.value(QStringLiteral("extras")).toObject();
    QJsonObject haorender;
    haorender.insert(QStringLiteral("nativeVrmWriter"), true);
    haorender.insert(QStringLiteral("mappedHumanoidBones"), human_bones.size());
    haorender.insert(QStringLiteral("conversionId"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    extras.insert(QStringLiteral("HAORENDER_AI"), haorender);
    document->json.insert(QStringLiteral("extras"), extras);

    if (status_message) {
        *status_message = QStringLiteral("Native VRM 1.0 metadata written with %1 humanoid bones.")
            .arg(human_bones.size());
    }
    return true;
}

} // namespace

bool VrmNativeWriter::writeFromSource(const QString& source_path,
                                      const QString& output_path,
                                      const SkeletonGraph& skeleton,
                                      QString* status_message) const {
    if (source_path.trimmed().isEmpty() || output_path.trimmed().isEmpty()) {
        if (status_message) {
            *status_message = QStringLiteral("Native VRM writer needs both source and output paths.");
        }
        return false;
    }
    if (skeleton.empty()) {
        if (status_message) {
            *status_message = QStringLiteral("Native VRM writer needs a detected humanoid skeleton.");
        }
        return false;
    }

    const QFileInfo output_info(output_path);
    const QString temp_glb_path = QDir(output_info.absolutePath()).filePath(
        QStringLiteral(".%1.%2.glb")
            .arg(output_info.completeBaseName().isEmpty() ? QStringLiteral("avatar") : output_info.completeBaseName(),
                 QUuid::createUuid().toString(QUuid::WithoutBraces)));

    QString export_error;
    if (!exportSourceToGlb(source_path, temp_glb_path, &export_error)) {
        if (status_message) {
            *status_message = export_error;
        }
        return false;
    }

    GlbDocument document;
    QString glb_error;
    if (!readGlbDocument(temp_glb_path, &document, &glb_error)) {
        QFile::remove(temp_glb_path);
        if (status_message) {
            *status_message = glb_error;
        }
        return false;
    }

    QString patch_status;
    if (!patchVrmExtension(&document, source_path, skeleton, &patch_status)) {
        QFile::remove(temp_glb_path);
        if (status_message) {
            *status_message = patch_status;
        }
        return false;
    }

    QFile::remove(output_path);
    QString write_error;
    if (!writeGlbDocument(output_path, document, &write_error)) {
        QFile::remove(temp_glb_path);
        if (status_message) {
            *status_message = write_error;
        }
        return false;
    }
    QFile::remove(temp_glb_path);

    if (status_message) {
        *status_message = patch_status;
    }
    return true;
}

} // namespace haorendergi
