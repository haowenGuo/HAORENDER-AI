#include "scene/model_loader.h"

#include "scene/gltf_animation_loader.h"
#include "scene/gltf_vrm_expression_loader.h"
#include "scene/gltf_vrm_material_loader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace haorendergi {
namespace {

Eigen::Vector3f parseRgbValue(const QString& text, const Eigen::Vector3f& fallback) {
    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    if (parts.size() < 3) {
        return fallback;
    }

    bool ok_x = false;
    bool ok_y = false;
    bool ok_z = false;
    const float x = parts[0].trimmed().toFloat(&ok_x);
    const float y = parts[1].trimmed().toFloat(&ok_y);
    const float z = parts[2].trimmed().toFloat(&ok_z);
    return ok_x && ok_y && ok_z ? Eigen::Vector3f(x, y, z) : fallback;
}

Eigen::Matrix4f parseMitsubaMatrix(const QString& text) {
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    Eigen::Matrix4f matrix = Eigen::Matrix4f::Identity();
    if (parts.size() != 16) {
        return matrix;
    }

    for (int i = 0; i < 16; ++i) {
        bool ok = false;
        const float value = parts[i].toFloat(&ok);
        if (!ok) {
            return Eigen::Matrix4f::Identity();
        }
        matrix(i / 4, i % 4) = value;
    }
    return matrix;
}

Eigen::Matrix4f toEigenMatrix(const aiMatrix4x4& matrix) {
    Eigen::Matrix4f result;
    result << matrix.a1, matrix.a2, matrix.a3, matrix.a4,
              matrix.b1, matrix.b2, matrix.b3, matrix.b4,
              matrix.c1, matrix.c2, matrix.c3, matrix.c4,
              matrix.d1, matrix.d2, matrix.d3, matrix.d4;
    return result;
}

Eigen::Vector3f toEigenVector(const aiVector3D& value) {
    return Eigen::Vector3f(value.x, value.y, value.z);
}

Eigen::Quaternionf toEigenQuaternion(const aiQuaternion& value) {
    return Eigen::Quaternionf(value.w, value.x, value.y, value.z).normalized();
}

void addBoneInfluence(Vertex* vertex, int bone_index, float weight) {
    if (!vertex || bone_index < 0 || weight <= 0.0f) {
        return;
    }

    for (int i = 0; i < 4; ++i) {
        if (vertex->bone_weights[i] <= 0.0f) {
            vertex->bone_indices[i] = bone_index;
            vertex->bone_weights[i] = weight;
            return;
        }
    }

    int weakest = 0;
    for (int i = 1; i < 4; ++i) {
        if (vertex->bone_weights[i] < vertex->bone_weights[weakest]) {
            weakest = i;
        }
    }
    if (weight > vertex->bone_weights[weakest]) {
        vertex->bone_indices[weakest] = bone_index;
        vertex->bone_weights[weakest] = weight;
    }
}

void normalizeBoneWeights(Vertex* vertex) {
    float total = 0.0f;
    for (float weight : vertex->bone_weights) {
        total += weight;
    }
    if (total <= 1e-6f) {
        return;
    }
    for (float& weight : vertex->bone_weights) {
        weight /= total;
    }
}

TextureData loadTextureFile(const QString& path) {
    TextureData slot;
    slot.path = QDir::toNativeSeparators(QDir::cleanPath(path)).toStdString();

    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (!image.isNull()) {
        slot.image = image.convertToFormat(QImage::Format_RGBA8888);
    }
    return slot;
}

TextureData loadTextureSlot(const aiMaterial* material,
                            aiTextureType texture_type,
                            const QString& base_directory) {
    TextureData slot;
    if (!material || material->GetTextureCount(texture_type) == 0) {
        return slot;
    }

    aiString texture_path;
    if (material->GetTexture(texture_type, 0, &texture_path) != aiReturn_SUCCESS) {
        return slot;
    }

    QString resolved = QString::fromUtf8(texture_path.C_Str());
    QFileInfo info(resolved);
    if (info.isRelative()) {
        resolved = QDir(base_directory).filePath(resolved);
    }
    slot.path = QDir::toNativeSeparators(QDir::cleanPath(resolved)).toStdString();

    QImageReader reader(resolved);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (!image.isNull()) {
        slot.image = image.convertToFormat(QImage::Format_RGBA8888);
    }
    return slot;
}

MaterialData parseMitsubaBsdf(QXmlStreamReader& xml, const QString& base_directory) {
    MaterialData material;
    material.name = xml.attributes().value(QStringLiteral("id")).toString().toStdString();
    const QString material_id = QString::fromStdString(material.name);
    const QString root_type = xml.attributes().value(QStringLiteral("type")).toString();
    if (root_type.contains(QStringLiteral("conductor"), Qt::CaseInsensitive)) {
        material.base_color_factor = Eigen::Vector3f(0.75f, 0.72f, 0.66f);
        material.metallic_factor = 1.0f;
        material.roughness_factor = 0.25f;
    } else if (root_type.contains(QStringLiteral("dielectric"), Qt::CaseInsensitive)) {
        material.base_color_factor = Eigen::Vector3f(0.75f, 0.9f, 1.0f);
        material.roughness_factor = 0.03f;
    }
    if (material_id.contains(QStringLiteral("Emitter"), Qt::CaseInsensitive) ||
        material_id.contains(QStringLiteral("Light"), Qt::CaseInsensitive)) {
        material.base_color_factor = Eigen::Vector3f::Ones();
        material.emissive_factor = Eigen::Vector3f(12.0f, 11.0f, 9.0f);
        material.roughness_factor = 0.2f;
    }

    int depth = 1;
    QString active_texture_name;
    QString active_texture_filename;
    while (!xml.atEnd() && depth > 0) {
        xml.readNext();
        if (xml.isStartElement()) {
            ++depth;
            const QString name = xml.name().toString();
            const auto attributes = xml.attributes();
            if (name == QStringLiteral("bsdf")) {
                const QString type = attributes.value(QStringLiteral("type")).toString();
                if (type.contains(QStringLiteral("conductor"), Qt::CaseInsensitive)) {
                    material.base_color_factor = Eigen::Vector3f(0.72f, 0.70f, 0.66f);
                    material.metallic_factor = 1.0f;
                    material.roughness_factor = std::min(material.roughness_factor, 0.35f);
                } else if (type.contains(QStringLiteral("dielectric"), Qt::CaseInsensitive)) {
                    material.base_color_factor = Eigen::Vector3f(0.75f, 0.9f, 1.0f);
                    material.roughness_factor = 0.03f;
                }
            } else if (name == QStringLiteral("float")) {
                const QString float_name = attributes.value(QStringLiteral("name")).toString();
                if (float_name == QStringLiteral("alpha")) {
                    material.roughness_factor = std::clamp(attributes.value(QStringLiteral("value")).toFloat(), 0.04f, 1.0f);
                }
            } else if (name == QStringLiteral("rgb")) {
                const QString rgb_name = attributes.value(QStringLiteral("name")).toString();
                const Eigen::Vector3f value = parseRgbValue(attributes.value(QStringLiteral("value")).toString(), material.base_color_factor);
                if (rgb_name == QStringLiteral("reflectance") || rgb_name == QStringLiteral("diffuse_reflectance")) {
                    material.base_color_factor = value;
                } else if (rgb_name == QStringLiteral("specular_reflectance")) {
                    material.base_color_factor = value.cwiseMax(Eigen::Vector3f::Constant(0.08f));
                    material.metallic_factor = 1.0f;
                }
            } else if (name == QStringLiteral("texture")) {
                active_texture_name = attributes.value(QStringLiteral("name")).toString();
                active_texture_filename.clear();
            } else if (name == QStringLiteral("string")) {
                const QString string_name = attributes.value(QStringLiteral("name")).toString();
                if (string_name == QStringLiteral("filename") && !active_texture_name.isEmpty()) {
                    active_texture_filename = attributes.value(QStringLiteral("value")).toString();
                }
            }
        } else if (xml.isEndElement()) {
            const QString name = xml.name().toString();
            if (name == QStringLiteral("texture") && !active_texture_name.isEmpty() && !active_texture_filename.isEmpty()) {
                if (active_texture_name == QStringLiteral("reflectance") || active_texture_name == QStringLiteral("diffuse_reflectance")) {
                    material.base_color_texture = loadTextureFile(QDir(base_directory).filePath(active_texture_filename));
                    material.diffuse_texture = material.base_color_texture;
                }
                active_texture_name.clear();
                active_texture_filename.clear();
            }
            --depth;
        }
    }
    return material;
}

Eigen::Vector3f readColor3(const aiMaterial* material,
                           const char* key,
                           unsigned int type,
                           unsigned int index,
                           const Eigen::Vector3f& fallback) {
    aiColor4D value;
    if (material && aiGetMaterialColor(material, key, type, index, &value) == aiReturn_SUCCESS) {
        return Eigen::Vector3f(value.r, value.g, value.b);
    }
    return fallback;
}

float readFloat(const aiMaterial* material,
                const char* key,
                unsigned int type,
                unsigned int index,
                float fallback) {
    float value = fallback;
    if (material && aiGetMaterialFloat(material, key, type, index, &value) == aiReturn_SUCCESS) {
        return value;
    }
    return fallback;
}

MaterialData loadMaterialData(const aiMaterial* material, const QString& base_directory) {
    MaterialData material_data;
    if (!material) {
        return material_data;
    }

    aiString material_name;
    if (material->Get(AI_MATKEY_NAME, material_name) == aiReturn_SUCCESS) {
        material_data.name = material_name.C_Str();
    }
    material_data.base_color_factor = readColor3(material, AI_MATKEY_BASE_COLOR, Eigen::Vector3f::Ones());
    if (material_data.base_color_factor.isZero(0.0f)) {
        material_data.base_color_factor = readColor3(material, AI_MATKEY_COLOR_DIFFUSE, Eigen::Vector3f::Ones());
    }
    material_data.emissive_factor = readColor3(material, AI_MATKEY_COLOR_EMISSIVE, Eigen::Vector3f::Zero());
    material_data.metallic_factor = readFloat(material, AI_MATKEY_METALLIC_FACTOR, 0.0f);
    material_data.roughness_factor = readFloat(material, AI_MATKEY_ROUGHNESS_FACTOR, 0.85f);
    material_data.ao_factor = 1.0f;

    material_data.base_color_texture = loadTextureSlot(material, aiTextureType_BASE_COLOR, base_directory);
    material_data.diffuse_texture = loadTextureSlot(material, aiTextureType_DIFFUSE, base_directory);
    material_data.normal_texture = loadTextureSlot(material, aiTextureType_NORMALS, base_directory);
    if (!material_data.normal_texture.valid()) {
        material_data.normal_texture = loadTextureSlot(material, aiTextureType_HEIGHT, base_directory);
    }
    material_data.specular_texture = loadTextureSlot(material, aiTextureType_SPECULAR, base_directory);
    material_data.metallic_texture = loadTextureSlot(material, aiTextureType_METALNESS, base_directory);
    material_data.roughness_texture = loadTextureSlot(material, aiTextureType_DIFFUSE_ROUGHNESS, base_directory);
    material_data.ao_texture = loadTextureSlot(material, aiTextureType_AMBIENT_OCCLUSION, base_directory);
    material_data.emissive_texture = loadTextureSlot(material, aiTextureType_EMISSIVE, base_directory);

    // Packed ORM fallback: many assets reuse one texture for metallic/roughness(/ao).
    if (!material_data.roughness_texture.valid() && material_data.metallic_texture.valid()) {
        material_data.roughness_texture = material_data.metallic_texture;
    }
    if (!material_data.metallic_texture.valid() && material_data.roughness_texture.valid()) {
        material_data.metallic_texture = material_data.roughness_texture;
    }
    if (!material_data.ao_texture.valid() &&
        material_data.metallic_texture.valid() &&
        material_data.roughness_texture.valid() &&
        material_data.metallic_texture.path == material_data.roughness_texture.path) {
        material_data.ao_texture = material_data.metallic_texture;
    }
    return material_data;
}

Vertex buildVertex(const aiMesh* mesh, unsigned int index) {
    Vertex vertex;
    vertex.position = Eigen::Vector3f(mesh->mVertices[index].x, mesh->mVertices[index].y, mesh->mVertices[index].z);
    if (mesh->HasNormals()) {
        vertex.normal = Eigen::Vector3f(mesh->mNormals[index].x, mesh->mNormals[index].y, mesh->mNormals[index].z);
    }
    if (mesh->HasTangentsAndBitangents()) {
        vertex.tangent = Eigen::Vector3f(mesh->mTangents[index].x, mesh->mTangents[index].y, mesh->mTangents[index].z);
        vertex.bitangent = Eigen::Vector3f(mesh->mBitangents[index].x, mesh->mBitangents[index].y, mesh->mBitangents[index].z);
    }
    if (mesh->HasTextureCoords(0)) {
        vertex.uv = Eigen::Vector2f(mesh->mTextureCoords[0][index].x, mesh->mTextureCoords[0][index].y);
    }
    return vertex;
}

bool morphVectorsLookAbsolute(const aiVector3D* values,
                              unsigned int value_count,
                              const std::vector<Vertex>& base_vertices,
                              Eigen::Vector3f Vertex::*field) {
    if (!values || value_count == 0 || base_vertices.empty()) {
        return false;
    }

    const unsigned int sample_count = std::min<unsigned int>(value_count, static_cast<unsigned int>(base_vertices.size()));
    double as_delta_cost = 0.0;
    double as_absolute_cost = 0.0;
    for (unsigned int i = 0; i < sample_count; ++i) {
        const Eigen::Vector3f raw = toEigenVector(values[i]);
        const Eigen::Vector3f base = base_vertices[i].*field;
        as_delta_cost += static_cast<double>(raw.squaredNorm());
        as_absolute_cost += static_cast<double>((raw - base).squaredNorm());
    }
    return as_absolute_cost < as_delta_cost * 0.5;
}

void loadMorphTargets(const aiMesh* mesh, MeshData* mesh_data) {
    if (!mesh || !mesh_data || mesh->mNumAnimMeshes == 0) {
        return;
    }

    mesh_data->morph_targets.clear();
    mesh_data->morph_weights.clear();
    mesh_data->morph_targets.reserve(mesh->mNumAnimMeshes);
    mesh_data->morph_weights.reserve(mesh->mNumAnimMeshes);

    for (unsigned int morph_index = 0; morph_index < mesh->mNumAnimMeshes; ++morph_index) {
        const aiAnimMesh* morph_mesh = mesh->mAnimMeshes[morph_index];
        if (!morph_mesh) {
            continue;
        }

        MorphTargetData target;
        target.name = morph_index < mesh_data->morph_target_names.size()
            ? mesh_data->morph_target_names[morph_index]
            : std::string();

        const unsigned int vertex_count = std::min<unsigned int>(
            morph_mesh->mNumVertices,
            static_cast<unsigned int>(mesh_data->vertices.size()));

        if (morph_mesh->mVertices && vertex_count > 0) {
            target.position_deltas.assign(mesh_data->vertices.size(), Eigen::Vector3f::Zero());
            const bool absolute_values = morphVectorsLookAbsolute(morph_mesh->mVertices,
                                                                  vertex_count,
                                                                  mesh_data->vertices,
                                                                  &Vertex::position);
            for (unsigned int vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                const Eigen::Vector3f value = toEigenVector(morph_mesh->mVertices[vertex_index]);
                target.position_deltas[vertex_index] = absolute_values
                    ? (value - mesh_data->vertices[vertex_index].position).eval()
                    : value;
            }
        }

        if (morph_mesh->mNormals && vertex_count > 0) {
            target.normal_deltas.assign(mesh_data->vertices.size(), Eigen::Vector3f::Zero());
            const bool absolute_values = morphVectorsLookAbsolute(morph_mesh->mNormals,
                                                                  vertex_count,
                                                                  mesh_data->vertices,
                                                                  &Vertex::normal);
            for (unsigned int vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                const Eigen::Vector3f value = toEigenVector(morph_mesh->mNormals[vertex_index]);
                target.normal_deltas[vertex_index] = absolute_values
                    ? (value - mesh_data->vertices[vertex_index].normal).eval()
                    : value;
            }
        }

        if (morph_mesh->mTangents && vertex_count > 0) {
            target.tangent_deltas.assign(mesh_data->vertices.size(), Eigen::Vector3f::Zero());
            const bool absolute_values = morphVectorsLookAbsolute(morph_mesh->mTangents,
                                                                  vertex_count,
                                                                  mesh_data->vertices,
                                                                  &Vertex::tangent);
            for (unsigned int vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                const Eigen::Vector3f value = toEigenVector(morph_mesh->mTangents[vertex_index]);
                target.tangent_deltas[vertex_index] = absolute_values
                    ? (value - mesh_data->vertices[vertex_index].tangent).eval()
                    : value;
            }
        }

        const float default_weight = static_cast<float>(morph_mesh->mWeight);
        mesh_data->morph_weights.push_back(default_weight);
        mesh_data->morph_default_weights.push_back(default_weight);
        mesh_data->morph_targets.push_back(std::move(target));
    }
    mesh_data->morph_target_count = static_cast<int>(mesh_data->morph_targets.size());
}

MeshData processMesh(const aiMesh* mesh,
                     const aiScene* scene,
                     const QString& base_directory,
                     const std::unordered_map<std::string, int>& node_indices,
                     const std::vector<MaterialData>* material_overrides,
                     int attached_node_index,
                     Bounds& bounds) {
    MeshData mesh_data;
    mesh_data.node_index = attached_node_index;
    if (mesh->mName.length > 0) {
        mesh_data.name = mesh->mName.C_Str();
    }
    mesh_data.morph_target_count = static_cast<int>(mesh->mNumAnimMeshes);
    mesh_data.morph_target_names.reserve(mesh->mNumAnimMeshes);
    for (unsigned int morph_index = 0; morph_index < mesh->mNumAnimMeshes; ++morph_index) {
        const aiAnimMesh* morph_mesh = mesh->mAnimMeshes[morph_index];
        mesh_data.morph_target_names.push_back(morph_mesh && morph_mesh->mName.length > 0
                                                   ? morph_mesh->mName.C_Str()
                                                   : std::string());
    }

    mesh_data.vertices.reserve(mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        Vertex vertex = buildVertex(mesh, i);
        mesh_data.vertices.push_back(vertex);
        bounds.include(vertex.position);
    }
    loadMorphTargets(mesh, &mesh_data);

    mesh_data.indices.reserve(mesh->mNumFaces * 3);
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            mesh_data.indices.push_back(static_cast<std::uint32_t>(face.mIndices[j]));
        }
    }

    if (material_overrides &&
        mesh->mMaterialIndex < material_overrides->size() &&
        (!(*material_overrides)[mesh->mMaterialIndex].name.empty() ||
         (*material_overrides)[mesh->mMaterialIndex].base_color_texture.valid())) {
        mesh_data.material = (*material_overrides)[mesh->mMaterialIndex];
    } else if (mesh->mMaterialIndex < scene->mNumMaterials) {
        mesh_data.material = loadMaterialData(scene->mMaterials[mesh->mMaterialIndex], base_directory);
    }

    for (unsigned int bone_index = 0; bone_index < mesh->mNumBones; ++bone_index) {
        const aiBone* bone = mesh->mBones[bone_index];
        if (!bone) {
            continue;
        }

        SkinBoneData skin_bone;
        skin_bone.name = bone->mName.C_Str();
        const auto found_node = node_indices.find(skin_bone.name);
        if (found_node != node_indices.end()) {
            skin_bone.node_index = found_node->second;
        }
        skin_bone.inverse_bind_matrix = toEigenMatrix(bone->mOffsetMatrix);
        const int local_bone_index = static_cast<int>(mesh_data.skin_bones.size());
        mesh_data.skin_bones.push_back(skin_bone);

        for (unsigned int weight_index = 0; weight_index < bone->mNumWeights; ++weight_index) {
            const aiVertexWeight& weight = bone->mWeights[weight_index];
            if (weight.mVertexId < mesh_data.vertices.size()) {
                addBoneInfluence(&mesh_data.vertices[weight.mVertexId], local_bone_index, weight.mWeight);
            }
        }
    }

    if (!mesh_data.skin_bones.empty()) {
        for (Vertex& vertex : mesh_data.vertices) {
            normalizeBoneWeights(&vertex);
        }
    }
    return mesh_data;
}

void processNode(const aiNode* node,
                 const aiScene* scene,
                 const QString& base_directory,
                 const std::unordered_map<std::string, int>& node_indices,
                 const std::vector<MaterialData>* material_overrides,
                 SceneModel& model) {
    const std::string node_name = node && node->mName.length > 0 ? node->mName.C_Str() : std::string();
    const auto found_node = node_indices.find(node_name);
    const int attached_node_index = found_node != node_indices.end() ? found_node->second : -1;
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        model.meshes.push_back(processMesh(mesh, scene, base_directory, node_indices, material_overrides, attached_node_index, model.bounds));
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        processNode(node->mChildren[i], scene, base_directory, node_indices, material_overrides, model);
    }
}

void collectSceneNodes(const aiNode* node,
                       int parent_index,
                       SceneModel& model,
                       std::unordered_map<std::string, int>& node_indices) {
    if (!node) {
        return;
    }

    SceneNodeData node_data;
    node_data.name = node->mName.C_Str();
    if (node_data.name.empty()) {
        node_data.name = "node_" + std::to_string(model.nodes.size());
    }
    node_data.parent_index = parent_index;
    node_data.local_bind_transform = toEigenMatrix(node->mTransformation);
    const int current_index = static_cast<int>(model.nodes.size());
    model.nodes.push_back(node_data);
    node_indices[node_data.name] = current_index;

    for (unsigned int child_index = 0; child_index < node->mNumChildren; ++child_index) {
        collectSceneNodes(node->mChildren[child_index], current_index, model, node_indices);
    }
}

AnimationClipData loadAnimationClip(const aiAnimation* animation,
                                    int animation_index,
                                    const std::unordered_map<std::string, int>& node_indices) {
    AnimationClipData clip;
    clip.name = animation && animation->mName.length > 0
        ? animation->mName.C_Str()
        : ("Animation " + std::to_string(animation_index + 1));
    if (!animation) {
        return clip;
    }

    clip.duration_ticks = animation->mDuration;
    clip.ticks_per_second = animation->mTicksPerSecond > 0.0 ? animation->mTicksPerSecond : 25.0;
    for (unsigned int channel_index = 0; channel_index < animation->mNumChannels; ++channel_index) {
        const aiNodeAnim* channel = animation->mChannels[channel_index];
        if (!channel) {
            continue;
        }

        const std::string node_name = channel->mNodeName.C_Str();
        const auto found_node = node_indices.find(node_name);
        if (found_node == node_indices.end()) {
            continue;
        }

        AnimationChannelData channel_data;
        channel_data.node_index = found_node->second;
        channel_data.positions.reserve(channel->mNumPositionKeys);
        for (unsigned int key_index = 0; key_index < channel->mNumPositionKeys; ++key_index) {
            const aiVectorKey& key = channel->mPositionKeys[key_index];
            channel_data.positions.push_back(VectorKeyframe{ key.mTime, toEigenVector(key.mValue) });
        }
        channel_data.rotations.reserve(channel->mNumRotationKeys);
        for (unsigned int key_index = 0; key_index < channel->mNumRotationKeys; ++key_index) {
            const aiQuatKey& key = channel->mRotationKeys[key_index];
            channel_data.rotations.push_back(RotationKeyframe{ key.mTime, toEigenQuaternion(key.mValue) });
        }
        channel_data.scales.reserve(channel->mNumScalingKeys);
        for (unsigned int key_index = 0; key_index < channel->mNumScalingKeys; ++key_index) {
            const aiVectorKey& key = channel->mScalingKeys[key_index];
            channel_data.scales.push_back(VectorKeyframe{ key.mTime, toEigenVector(key.mValue) });
        }
        clip.channels.push_back(std::move(channel_data));
    }
    return clip;
}

void loadAnimations(const aiScene* scene,
                    const std::unordered_map<std::string, int>& node_indices,
                    SceneModel& model) {
    if (!scene) {
        return;
    }
    model.animations.reserve(scene->mNumAnimations);
    for (unsigned int animation_index = 0; animation_index < scene->mNumAnimations; ++animation_index) {
        AnimationClipData clip = loadAnimationClip(scene->mAnimations[animation_index],
                                                   static_cast<int>(animation_index),
                                                   node_indices);
        if (!clip.channels.empty()) {
            model.animations.push_back(std::move(clip));
        }
    }
}

void applyTransform(MeshData& mesh, const Eigen::Matrix4f& transform, Bounds& bounds) {
    Eigen::Matrix3f normal_matrix = transform.block<3, 3>(0, 0);
    if (std::abs(normal_matrix.determinant()) > 1e-8f) {
        normal_matrix = normal_matrix.inverse().transpose();
    }

    for (Vertex& vertex : mesh.vertices) {
        const Eigen::Vector4f p(vertex.position.x(), vertex.position.y(), vertex.position.z(), 1.0f);
        vertex.position = (transform * p).hnormalized();
        vertex.normal = (normal_matrix * vertex.normal).normalized();
        if (!vertex.tangent.isZero(0.0f)) {
            vertex.tangent = (normal_matrix * vertex.tangent).normalized();
        }
        if (!vertex.bitangent.isZero(0.0f)) {
            vertex.bitangent = (normal_matrix * vertex.bitangent).normalized();
        }
        bounds.include(vertex.position);
    }

    const Eigen::Matrix3f linear = transform.block<3, 3>(0, 0);
    for (MorphTargetData& target : mesh.morph_targets) {
        for (Eigen::Vector3f& delta : target.position_deltas) {
            delta = (linear * delta).eval();
        }
        for (Eigen::Vector3f& delta : target.normal_deltas) {
            delta = (normal_matrix * delta).eval();
        }
        for (Eigen::Vector3f& delta : target.tangent_deltas) {
            delta = (normal_matrix * delta).eval();
        }
    }
}

bool appendAssimpFile(const QString& path,
                      const Eigen::Matrix4f& transform,
                      const MaterialData* material_override,
                      SceneModel& model,
                      QString* error_message) {
    QFileInfo file_info(path);
    if (!file_info.exists()) {
        if (error_message) {
            *error_message = QString("Referenced model file not found: %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        QDir::toNativeSeparators(path).toStdString(),
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_FixInfacingNormals |
        aiProcess_FlipUVs);

    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0) {
        if (error_message) {
            *error_message = QString("Assimp load failed for %1: %2").arg(QDir::toNativeSeparators(path), importer.GetErrorString());
        }
        return false;
    }

    const QString base_directory = file_info.absolutePath();
    std::unordered_map<std::string, int> node_indices;
    SceneModel local_model;
    collectSceneNodes(scene->mRootNode, -1, local_model, node_indices);
    std::vector<MaterialData> material_overrides;
    loadGltfVrmMaterialOverrides(path, &material_overrides);
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        Bounds ignored_bounds;
        MeshData mesh = processMesh(scene->mMeshes[i], scene, base_directory, node_indices, &material_overrides, -1, ignored_bounds);
        if (mesh.name.empty()) {
            mesh.name = file_info.completeBaseName().toStdString();
        }
        if (material_override) {
            mesh.material = *material_override;
        }
        applyTransform(mesh, transform, model.bounds);
        model.meshes.push_back(std::move(mesh));
    }
    return true;
}

struct MitsubaShapeInfo {
    QString filename;
    QString material_id;
    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
};

void appendRectangleLight(const Eigen::Matrix4f& transform,
                          const Eigen::Vector3f& radiance,
                          SceneModel& model) {
    const Eigen::Vector3f p0 = (transform * Eigen::Vector4f(-1.0f, -1.0f, 0.0f, 1.0f)).hnormalized();
    const Eigen::Vector3f p1 = (transform * Eigen::Vector4f(1.0f, -1.0f, 0.0f, 1.0f)).hnormalized();
    const Eigen::Vector3f p2 = (transform * Eigen::Vector4f(1.0f, 1.0f, 0.0f, 1.0f)).hnormalized();
    const Eigen::Vector3f p3 = (transform * Eigen::Vector4f(-1.0f, 1.0f, 0.0f, 1.0f)).hnormalized();

    Eigen::Vector3f normal = (p1 - p0).cross(p2 - p0);
    if (normal.squaredNorm() < 1e-8f) {
        normal = Eigen::Vector3f(0.0f, 1.0f, 0.0f);
    } else {
        normal.normalize();
    }

    MeshData mesh;
    mesh.name = "MitsubaAreaLight";
    mesh.material.name = "MitsubaAreaLight";
    mesh.material.base_color_factor = Eigen::Vector3f::Ones();
    mesh.material.emissive_factor = radiance;
    mesh.material.roughness_factor = 0.2f;

    const Eigen::Vector3f positions[4] = { p0, p1, p2, p3 };
    const Eigen::Vector2f uvs[4] = {
        Eigen::Vector2f(0.0f, 0.0f),
        Eigen::Vector2f(1.0f, 0.0f),
        Eigen::Vector2f(1.0f, 1.0f),
        Eigen::Vector2f(0.0f, 1.0f),
    };
    for (int i = 0; i < 4; ++i) {
        Vertex vertex;
        vertex.position = positions[i];
        vertex.normal = normal;
        vertex.uv = uvs[i];
        mesh.vertices.push_back(vertex);
        model.bounds.include(vertex.position);
    }
    mesh.indices = { 0, 1, 2, 0, 2, 3 };
    model.meshes.push_back(std::move(mesh));
}

MitsubaShapeInfo parseMitsubaObjShape(QXmlStreamReader& xml) {
    MitsubaShapeInfo shape;
    int depth = 1;
    bool in_to_world = false;
    while (!xml.atEnd() && depth > 0) {
        xml.readNext();
        if (xml.isStartElement()) {
            ++depth;
            const QString name = xml.name().toString();
            const auto attributes = xml.attributes();
            if (name == QStringLiteral("string") && attributes.value(QStringLiteral("name")) == QStringLiteral("filename")) {
                shape.filename = attributes.value(QStringLiteral("value")).toString();
            } else if (name == QStringLiteral("transform") && attributes.value(QStringLiteral("name")) == QStringLiteral("to_world")) {
                in_to_world = true;
            } else if (name == QStringLiteral("matrix") && in_to_world) {
                shape.transform = parseMitsubaMatrix(attributes.value(QStringLiteral("value")).toString());
            } else if (name == QStringLiteral("ref")) {
                shape.material_id = attributes.value(QStringLiteral("id")).toString();
            }
        } else if (xml.isEndElement()) {
            if (xml.name() == QStringLiteral("transform")) {
                in_to_world = false;
            }
            --depth;
        }
    }
    return shape;
}

void parseMitsubaSensor(QXmlStreamReader& xml, SceneModel& model) {
    float fov_degrees = 55.0f;
    Eigen::Matrix4f to_world = Eigen::Matrix4f::Identity();
    bool has_transform = false;
    bool in_to_world = false;
    int depth = 1;

    while (!xml.atEnd() && depth > 0) {
        xml.readNext();
        if (xml.isStartElement()) {
            ++depth;
            const QString name = xml.name().toString();
            const auto attributes = xml.attributes();
            if (name == QStringLiteral("float") && attributes.value(QStringLiteral("name")) == QStringLiteral("fov")) {
                fov_degrees = attributes.value(QStringLiteral("value")).toFloat();
            } else if (name == QStringLiteral("transform") && attributes.value(QStringLiteral("name")) == QStringLiteral("to_world")) {
                in_to_world = true;
            } else if (name == QStringLiteral("matrix") && in_to_world) {
                to_world = parseMitsubaMatrix(attributes.value(QStringLiteral("value")).toString());
                has_transform = true;
            }
        } else if (xml.isEndElement()) {
            if (xml.name() == QStringLiteral("transform")) {
                in_to_world = false;
            }
            --depth;
        }
    }

    if (has_transform) {
        const Eigen::Vector3f position = to_world.block<3, 1>(0, 3);
        Eigen::Vector3f forward = to_world.block<3, 1>(0, 2);
        if (forward.squaredNorm() < 1e-8f) {
            forward = Eigen::Vector3f(0.0f, 0.0f, -1.0f);
        } else {
            forward.normalize();
        }
        model.has_camera = true;
        model.camera_position = position;
        model.camera_target = position + forward;
        model.camera_fov_degrees = std::clamp(fov_degrees, 20.0f, 100.0f);
    }
}

void parseMitsubaRectangleShape(QXmlStreamReader& xml, SceneModel& model) {
    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
    Eigen::Vector3f radiance = Eigen::Vector3f::Zero();
    bool has_emitter = false;
    bool in_to_world = false;
    bool in_emitter = false;
    int depth = 1;

    while (!xml.atEnd() && depth > 0) {
        xml.readNext();
        if (xml.isStartElement()) {
            ++depth;
            const QString name = xml.name().toString();
            const auto attributes = xml.attributes();
            if (name == QStringLiteral("transform") && attributes.value(QStringLiteral("name")) == QStringLiteral("to_world")) {
                in_to_world = true;
            } else if (name == QStringLiteral("matrix") && in_to_world) {
                transform = parseMitsubaMatrix(attributes.value(QStringLiteral("value")).toString());
            } else if (name == QStringLiteral("emitter")) {
                in_emitter = true;
            } else if (name == QStringLiteral("rgb") && in_emitter && attributes.value(QStringLiteral("name")) == QStringLiteral("radiance")) {
                radiance = parseRgbValue(attributes.value(QStringLiteral("value")).toString(), Eigen::Vector3f::Zero());
                has_emitter = true;
            }
        } else if (xml.isEndElement()) {
            if (xml.name() == QStringLiteral("transform")) {
                in_to_world = false;
            } else if (xml.name() == QStringLiteral("emitter")) {
                in_emitter = false;
            }
            --depth;
        }
    }

    if (has_emitter && radiance.maxCoeff() > 0.0f) {
        appendRectangleLight(transform, radiance, model);
    }
}

SceneModel loadMitsubaXmlScene(const QString& path, QString* error_message) {
    SceneModel model;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error_message) {
            *error_message = QString("Failed to open Mitsuba scene: %1").arg(QDir::toNativeSeparators(path));
        }
        return model;
    }

    QFileInfo file_info(path);
    const QString base_directory = file_info.absolutePath();
    QXmlStreamReader xml(&file);
    std::unordered_map<std::string, MaterialData> materials;
    std::vector<MitsubaShapeInfo> shapes;

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }

        const QString name = xml.name().toString();
        const auto attributes = xml.attributes();
        if (name == QStringLiteral("sensor")) {
            parseMitsubaSensor(xml, model);
        } else if (name == QStringLiteral("bsdf") && attributes.hasAttribute(QStringLiteral("id"))) {
            MaterialData material = parseMitsubaBsdf(xml, base_directory);
            if (!material.name.empty()) {
                materials[material.name] = std::move(material);
            }
        } else if (name == QStringLiteral("shape")) {
            const QString type = attributes.value(QStringLiteral("type")).toString();
            if (type == QStringLiteral("obj")) {
                shapes.push_back(parseMitsubaObjShape(xml));
            } else if (type == QStringLiteral("rectangle")) {
                parseMitsubaRectangleShape(xml, model);
            }
        }
    }

    if (xml.hasError()) {
        if (error_message) {
            *error_message = QString("Mitsuba XML parse failed: %1").arg(xml.errorString());
        }
        return SceneModel();
    }

    model.source_path = QDir::toNativeSeparators(file_info.absoluteFilePath()).toStdString();
    QString last_error;
    for (const MitsubaShapeInfo& shape : shapes) {
        if (shape.filename.isEmpty()) {
            continue;
        }

        const QString obj_path = QDir(base_directory).filePath(shape.filename);
        const MaterialData* material = nullptr;
        const auto found = materials.find(shape.material_id.toStdString());
        if (found != materials.end()) {
            material = &found->second;
        }
        QString append_error;
        if (!appendAssimpFile(obj_path, shape.transform, material, model, &append_error)) {
            last_error = append_error;
        }
    }

    if (model.empty() && error_message) {
        *error_message = last_error.isEmpty()
            ? QStringLiteral("Mitsuba scene did not contain any loadable OBJ shapes.")
            : last_error;
    } else if (error_message) {
        error_message->clear();
    }
    return model;
}

} // namespace

SceneModel ModelLoader::loadFromFile(const QString& path, QString* error_message) const {
    SceneModel model;
    QFileInfo file_info(path);
    if (!file_info.exists()) {
        if (error_message) {
            *error_message = QString("Model file not found: %1").arg(QDir::toNativeSeparators(path));
        }
        return model;
    }

    if (file_info.suffix().compare(QStringLiteral("xml"), Qt::CaseInsensitive) == 0) {
        return loadMitsubaXmlScene(path, error_message);
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        QDir::toNativeSeparators(path).toStdString(),
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_FixInfacingNormals |
        aiProcess_FlipUVs);

    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0) {
        if (error_message) {
            *error_message = QString("Assimp load failed: %1").arg(importer.GetErrorString());
        }
        return model;
    }

    model.source_path = QDir::toNativeSeparators(file_info.absoluteFilePath()).toStdString();
    std::unordered_map<std::string, int> node_indices;
    collectSceneNodes(scene->mRootNode, -1, model, node_indices);
    std::vector<MaterialData> material_overrides;
    loadGltfVrmMaterialOverrides(path, &material_overrides);
    processNode(scene->mRootNode, scene, file_info.absolutePath(), node_indices, &material_overrides, model);
    loadGltfVrmExpressions(path, &model);
    loadAnimations(scene, node_indices, model);
    if (!model.bounds.valid()) {
        model.bounds.include(Eigen::Vector3f(-0.5f, -0.5f, -0.5f));
        model.bounds.include(Eigen::Vector3f(0.5f, 0.5f, 0.5f));
    }
    if (error_message) {
        error_message->clear();
    }
    return model;
}

SceneModel ModelLoader::loadAnimationFromFile(const QString& path, QString* error_message) const {
    SceneModel model;
    QFileInfo file_info(path);
    if (!file_info.exists()) {
        if (error_message) {
            *error_message = QString("Animation file not found: %1").arg(QDir::toNativeSeparators(path));
        }
        return model;
    }

    const auto try_gltf_fallback = [&]() -> bool {
        SceneModel gltf_model;
        QString gltf_error;
        if (loadGltfAnimationScene(path, &gltf_model, &gltf_error)) {
            model = std::move(gltf_model);
            if (error_message) {
                error_message->clear();
            }
            return true;
        }
        if (error_message && !gltf_error.isEmpty()) {
            *error_message = gltf_error;
        }
        return false;
    };

    const QString suffix = file_info.suffix().toLower();
    if (suffix == QStringLiteral("vrma")) {
        try_gltf_fallback();
        return model;
    }

    Assimp::Importer importer;
    const QByteArray encoded_path = QFile::encodeName(path);
    const aiScene* scene = importer.ReadFile(encoded_path.constData(), aiProcess_ValidateDataStructure);
    if (!scene || !scene->mRootNode) {
        Assimp::Importer retry_importer;
        scene = retry_importer.ReadFile(encoded_path.constData(), 0);
        if (!scene || !scene->mRootNode) {
            if (suffix == QStringLiteral("glb") || suffix == QStringLiteral("gltf")) {
                try_gltf_fallback();
                return model;
            }
            if (error_message) {
                const char* first_error = importer.GetErrorString();
                const char* retry_error = retry_importer.GetErrorString();
                const QString assimp_error = !QString::fromUtf8(first_error).trimmed().isEmpty()
                    ? QString::fromUtf8(first_error)
                    : QString::fromUtf8(retry_error);
                *error_message = assimp_error.trimmed().isEmpty()
                    ? QStringLiteral("Assimp could not read the animation skeleton.")
                    : assimp_error;
            }
            return model;
        }

        model.source_path = QDir::toNativeSeparators(file_info.absoluteFilePath()).toStdString();
        std::unordered_map<std::string, int> node_indices;
        collectSceneNodes(scene->mRootNode, -1, model, node_indices);
        loadAnimations(scene, node_indices, model);
    } else {
        model.source_path = QDir::toNativeSeparators(file_info.absoluteFilePath()).toStdString();
        std::unordered_map<std::string, int> node_indices;
        collectSceneNodes(scene->mRootNode, -1, model, node_indices);
        loadAnimations(scene, node_indices, model);
    }

    if (model.nodes.empty() || model.animations.empty()) {
        if ((suffix == QStringLiteral("glb") || suffix == QStringLiteral("gltf")) && try_gltf_fallback()) {
            return model;
        }
        if (error_message) {
            *error_message = QStringLiteral("No animation channels were found in %1.").arg(QDir::toNativeSeparators(path));
        }
        return SceneModel();
    }
    if (error_message) {
        error_message->clear();
    }
    return model;
}

} // namespace haorendergi
