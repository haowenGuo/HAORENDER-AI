#include "rigging/skeleton_extractor.h"

#include "rigging/bone_name_normalizer.h"
#include "scene/gltf_animation_loader.h"
#include "scene/gltf_vrm_humanoid_loader.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace haorendergi {

namespace {

QString assimpStringToQString(const aiString& value) {
    return QString::fromUtf8(value.C_Str());
}

void collectSkinBoneNames(const aiScene* scene, QSet<QString>* names) {
    if (!scene || !names) {
        return;
    }
    for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
        const aiMesh* mesh = scene->mMeshes[mesh_index];
        if (!mesh) {
            continue;
        }
        for (unsigned int bone_index = 0; bone_index < mesh->mNumBones; ++bone_index) {
            const aiBone* bone = mesh->mBones[bone_index];
            if (bone) {
                names->insert(assimpStringToQString(bone->mName));
            }
        }
    }
}

void collectAnimationChannelNames(const aiScene* scene, QSet<QString>* names) {
    if (!scene || !names) {
        return;
    }
    for (unsigned int animation_index = 0; animation_index < scene->mNumAnimations; ++animation_index) {
        const aiAnimation* animation = scene->mAnimations[animation_index];
        if (!animation) {
            continue;
        }
        for (unsigned int channel_index = 0; channel_index < animation->mNumChannels; ++channel_index) {
            const aiNodeAnim* channel = animation->mChannels[channel_index];
            if (channel) {
                names->insert(assimpStringToQString(channel->mNodeName));
            }
        }
    }
}

void appendNode(const aiNode* node,
                int parent_index,
                int depth,
                const QSet<QString>& skin_bones,
                const QSet<QString>& animation_channels,
                SkeletonGraph* graph) {
    if (!node || !graph) {
        return;
    }

    aiVector3D scaling;
    aiQuaternion rotation;
    aiVector3D translation;
    node->mTransformation.Decompose(scaling, rotation, translation);

    SkeletonBone bone;
    bone.name = assimpStringToQString(node->mName);
    if (bone.name.isEmpty()) {
        bone.name = QStringLiteral("node_%1").arg(graph->bones.size());
    }
    bone.normalized_name = normalizeBoneName(bone.name);
    bone.parent_index = parent_index;
    bone.depth = depth;
    bone.local_translation = Eigen::Vector3f(translation.x, translation.y, translation.z);
    bone.referenced_by_skin = skin_bones.contains(bone.name);
    bone.has_animation_channel = animation_channels.contains(bone.name);
    bone.semantic = classifyBoneName(bone.name);

    const int current_index = graph->bones.size();
    graph->bones.push_back(bone);
    for (unsigned int child_index = 0; child_index < node->mNumChildren; ++child_index) {
        appendNode(node->mChildren[child_index], current_index, depth + 1, skin_bones, animation_channels, graph);
    }
}

QString animationName(const aiAnimation* animation, int index) {
    if (!animation || animation->mName.length == 0) {
        return QStringLiteral("Animation %1").arg(index + 1);
    }
    return assimpStringToQString(animation->mName);
}

int depthForNode(const SceneModel& scene, int node_index) {
    int depth = 0;
    int parent = node_index >= 0 && node_index < static_cast<int>(scene.nodes.size())
        ? scene.nodes[node_index].parent_index
        : -1;
    while (parent >= 0 && parent < static_cast<int>(scene.nodes.size())) {
        ++depth;
        parent = scene.nodes[parent].parent_index;
    }
    return depth;
}

RigSide sideFromCanonical(const QString& canonical_name) {
    if (canonical_name.startsWith(QStringLiteral("Left"))) {
        return RigSide::Left;
    }
    if (canonical_name.startsWith(QStringLiteral("Right"))) {
        return RigSide::Right;
    }
    return RigSide::Center;
}

void applyVrmHumanoidSemantics(const QString& path, SkeletonGraph* graph) {
    if (!graph) {
        return;
    }

    QHash<QString, QString> node_to_human_bone;
    if (!loadGltfVrmHumanoidBones(path, &node_to_human_bone)) {
        return;
    }

    QHash<QString, int> exact_bone_index;
    QHash<QString, int> normalized_bone_index;
    for (int i = 0; i < graph->bones.size(); ++i) {
        exact_bone_index.insert(graph->bones[i].name, i);
        normalized_bone_index.insert(normalizeBoneName(graph->bones[i].name), i);
    }

    for (auto it = node_to_human_bone.begin(); it != node_to_human_bone.end(); ++it) {
        int bone_index = exact_bone_index.value(it.key(), -1);
        if (bone_index < 0) {
            bone_index = normalized_bone_index.value(normalizeBoneName(it.key()), -1);
        }
        if (bone_index < 0 || bone_index >= graph->bones.size()) {
            continue;
        }

        SkeletonBone& bone = graph->bones[bone_index];
        bone.semantic.canonical_name = it.value();
        bone.semantic.side = sideFromCanonical(it.value());
        bone.semantic.confidence = 1.0f;
    }
}

SkeletonGraph graphFromAnimationScene(const QString& path, const SceneModel& scene) {
    SkeletonGraph graph;
    graph.source_path = path;
    graph.asset_label = QFileInfo(path).completeBaseName();
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        const SceneNodeData& node = scene.nodes[i];
        SkeletonBone bone;
        bone.name = QString::fromStdString(node.name);
        if (bone.name.isEmpty()) {
            bone.name = QStringLiteral("node_%1").arg(i);
        }
        bone.normalized_name = normalizeBoneName(bone.name);
        bone.parent_index = node.parent_index;
        bone.depth = depthForNode(scene, i);
        bone.local_translation = node.local_bind_transform.block<3, 1>(0, 3);
        bone.referenced_by_skin = false;
        bone.semantic = classifyBoneName(bone.name);
        graph.bones.push_back(bone);
    }
    for (int animation_index = 0; animation_index < static_cast<int>(scene.animations.size()); ++animation_index) {
        const AnimationClipData& clip = scene.animations[animation_index];
        AnimationClipInfo info;
        info.name = QString::fromStdString(clip.name.empty() ? std::string("Animation ") + std::to_string(animation_index + 1) : clip.name);
        info.duration = clip.duration_ticks;
        info.ticks_per_second = clip.ticks_per_second;
        info.channel_count = static_cast<int>(clip.channels.size());
        graph.animations.push_back(info);
        for (const AnimationChannelData& channel : clip.channels) {
            if (channel.node_index >= 0 && channel.node_index < graph.bones.size()) {
                graph.bones[channel.node_index].has_animation_channel = true;
            }
        }
    }
    applyVrmHumanoidSemantics(path, &graph);
    return graph;
}

} // namespace

SkeletonGraph SkeletonExtractor::loadFromFile(const QString& path, QString* error_message) const {
    SkeletonGraph graph;
    graph.source_path = path;
    graph.asset_label = QFileInfo(path).completeBaseName();

    if (path.trimmed().isEmpty()) {
        if (error_message) {
            *error_message = QStringLiteral("No file selected.");
        }
        return graph;
    }
    if (!QFileInfo::exists(path)) {
        if (error_message) {
            *error_message = QStringLiteral("File does not exist: %1").arg(path);
        }
        return graph;
    }

    Assimp::Importer importer;
    const QByteArray encoded_path = QFile::encodeName(path);
    const aiScene* scene = importer.ReadFile(encoded_path.constData(),
                                             aiProcess_JoinIdenticalVertices |
                                                 aiProcess_LimitBoneWeights |
                                                 aiProcess_ValidateDataStructure);
    if (!scene) {
        SceneModel gltf_scene;
        QString gltf_error;
        if (loadGltfAnimationScene(path, &gltf_scene, &gltf_error)) {
            if (error_message) {
                error_message->clear();
            }
            return graphFromAnimationScene(path, gltf_scene);
        }
        if (error_message) {
            const QString assimp_error = QString::fromUtf8(importer.GetErrorString()).trimmed();
            *error_message = assimp_error.isEmpty() ? gltf_error : assimp_error;
        }
        return graph;
    }
    if (!scene->mRootNode) {
        if (error_message) {
            *error_message = QStringLiteral("The file has no scene node hierarchy.");
        }
        return graph;
    }

    QSet<QString> skin_bones;
    collectSkinBoneNames(scene, &skin_bones);
    QSet<QString> animation_channels;
    collectAnimationChannelNames(scene, &animation_channels);
    appendNode(scene->mRootNode, -1, 0, skin_bones, animation_channels, &graph);
    applyVrmHumanoidSemantics(path, &graph);

    for (unsigned int animation_index = 0; animation_index < scene->mNumAnimations; ++animation_index) {
        const aiAnimation* animation = scene->mAnimations[animation_index];
        if (!animation) {
            continue;
        }
        AnimationClipInfo info;
        info.name = animationName(animation, static_cast<int>(animation_index));
        info.duration = animation->mDuration;
        info.ticks_per_second = animation->mTicksPerSecond;
        info.channel_count = static_cast<int>(animation->mNumChannels);
        graph.animations.push_back(info);
    }

    if (graph.bones.isEmpty()) {
        if (error_message) {
            *error_message = QStringLiteral("No node hierarchy or skeleton was extracted.");
        }
    } else if (error_message) {
        error_message->clear();
    }
    return graph;
}

} // namespace haorendergi
