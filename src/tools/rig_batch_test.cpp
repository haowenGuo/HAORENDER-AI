#include "rigging/ai_bone_mapper.h"
#include "rigging/animation_retargeter.h"
#include "rigging/retarget_profile.h"
#include "rigging/retarget_quality.h"
#include "rigging/skeleton_extractor.h"
#include "scene/animation_sampler.h"
#include "scene/model_loader.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

using namespace haorendergi;

namespace {

QString csv(const QString& value) {
    QString escaped = value;
    escaped.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString baseName(const QString& path) {
    return QFileInfo(path).fileName();
}

QString statusText(const QString& value) {
    return value.trimmed().isEmpty() ? QStringLiteral("ok") : value.trimmed().replace(QLatin1Char('\n'), QLatin1Char(' '));
}

std::vector<QString> collectFiles(const QString& root, const QStringList& patterns) {
    std::vector<QString> files;
    if (!QDir(root).exists()) {
        return files;
    }
    QDirIterator iterator(root, patterns, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        files.push_back(QDir::toNativeSeparators(iterator.next()));
    }
    std::sort(files.begin(), files.end(), [](const QString& lhs, const QString& rhs) {
        return QString::compare(lhs, rhs, Qt::CaseInsensitive) < 0;
    });
    return files;
}

bool containsAny(const QString& value, std::initializer_list<const char*> tokens) {
    const QString lower = value.toLower();
    for (const char* token : tokens) {
        if (lower.contains(QString::fromLatin1(token).toLower())) {
            return true;
        }
    }
    return false;
}

std::vector<QString> chooseTargets(const QString& root) {
    std::vector<QString> targets;
    const QStringList preferred = {
        QStringLiteral("F:/AIGril/Resources/AiGril.glb"),
        QStringLiteral("F:/AIGril/Resources/AiGril.vrm"),
        QStringLiteral("F:/AIGril/Resources/AiGril_18.vrm"),
        QStringLiteral("F:/AIGril/Resources/AiGril-18.vrm")
    };
    for (const QString& candidate : preferred) {
        if (QFileInfo::exists(candidate)) {
            targets.push_back(QDir::toNativeSeparators(candidate));
        }
    }
    for (const QString& path : collectFiles(root, { QStringLiteral("*.vrm"), QStringLiteral("*.glb"), QStringLiteral("*.gltf") })) {
        const QString name = QFileInfo(path).completeBaseName().toLower();
        if (containsAny(name, { "idle", "dance", "wave" })) {
            continue;
        }
        if (std::find(targets.begin(), targets.end(), path) == targets.end()) {
            targets.push_back(path);
        }
    }
    return targets;
}

std::vector<QString> chooseSources(const QString& root) {
    std::vector<QString> sources;
    const QStringList preferred = {
        QStringLiteral("F:/AIGril/Resources/VRMA_MotionPack/vrma/VRMA_17.vrma"),
        QStringLiteral("F:/AIGril/Resources/VRMA_MotionPack/vrma/Idle.vrma"),
        QStringLiteral("F:/AIGril/Resources/fbx/VRMA/Standing Idle.vrma"),
        QStringLiteral("F:/AIGril/Resources/harvested/motions/vrma/review/walk/WALK-RUN-CYCLES-MOCAP__10-WalkCycle_01_MIXAMO_769.vrma"),
        QStringLiteral("F:/AIGril/Resources/harvested/motions/vrma/review/dance/DANCE-MOTIONS-MOCAP__MacarenaDance_MIXAMO_769.vrma"),
        QStringLiteral("F:/AIGril/Resources/harvested/motions/vrma/review/fight/FIGHT-MOTIONS-MOCAP__Idle_FightStance_MIXAMO_769.vrma"),
        QStringLiteral("F:/AIGril/Resources/harvested/motions/fbx/review/walk/WALK-RUN-CYCLES-MOCAP__10-WalkCycle_01_MIXAMO_769.fbx"),
        QStringLiteral("F:/AIGril/Resources/harvested/motions/fbx/review/dance/DANCE-MOTIONS-MOCAP__MacarenaDance_MIXAMO_769.fbx"),
        QStringLiteral("F:/AIGril/Resources/fbx/FBX/Idle.fbx")
    };
    for (const QString& candidate : preferred) {
        if (QFileInfo::exists(candidate)) {
            sources.push_back(QDir::toNativeSeparators(candidate));
        }
    }

    for (const QString& path : collectFiles(root, { QStringLiteral("*.vrma"), QStringLiteral("*.fbx") })) {
        if (!containsAny(path, { "mixamo", "walk", "run", "dance", "idle", "fight", "standing" })) {
            continue;
        }
        if (std::find(sources.begin(), sources.end(), path) == sources.end()) {
            sources.push_back(path);
        }
    }
    return sources;
}

double boundsDiagonal(const SceneModel& scene) {
    if (!scene.bounds.valid()) {
        return 1.0;
    }
    const Eigen::Vector3f extent = scene.bounds.extent();
    const double diagonal = static_cast<double>(extent.norm());
    return diagonal > 1e-6 ? diagonal : 1.0;
}

double rmsVertexDelta(const SceneModel& lhs, const SceneModel& rhs) {
    double sum = 0.0;
    std::size_t count = 0;
    const std::size_t mesh_count = std::min(lhs.meshes.size(), rhs.meshes.size());
    for (std::size_t mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
        const MeshData& a = lhs.meshes[mesh_index];
        const MeshData& b = rhs.meshes[mesh_index];
        const std::size_t vertex_count = std::min(a.vertices.size(), b.vertices.size());
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            const Eigen::Vector3f delta = a.vertices[vertex_index].position - b.vertices[vertex_index].position;
            sum += static_cast<double>(delta.squaredNorm());
        }
        count += vertex_count;
    }
    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

double sampledMotionScore(const SceneModel& bind_scene, int animation_index, bool* sampled_ok) {
    if (sampled_ok) {
        *sampled_ok = false;
    }
    if (bind_scene.animations.empty()) {
        return 0.0;
    }
    const AnimationClipData& clip = bind_scene.animations[animation_index];
    const double duration = std::max(clip.durationSeconds(), 0.1);
    SceneModel a;
    SceneModel b;
    SceneModel c;
    const bool ok_a = sampleSceneAnimation(bind_scene, animation_index, 0.0, &a);
    const bool ok_b = sampleSceneAnimation(bind_scene, animation_index, duration * 0.33, &b);
    const bool ok_c = sampleSceneAnimation(bind_scene, animation_index, duration * 0.66, &c);
    if (sampled_ok) {
        *sampled_ok = ok_a && ok_b && ok_c;
    }
    if (!(ok_a && ok_b && ok_c)) {
        return 0.0;
    }
    const double diagonal = boundsDiagonal(bind_scene);
    const double d0 = rmsVertexDelta(a, b) / diagonal;
    const double d1 = rmsVertexDelta(b, c) / diagonal;
    return std::max(d0, d1);
}

struct CaseResult {
    QString target;
    QString source;
    QString status;
    int target_nodes = 0;
    int source_nodes = 0;
    int target_skinned_bones = 0;
    int source_clips = 0;
    int source_channels = 0;
    int mapped = 0;
    int unmapped_source = 0;
    int unmapped_target = 0;
    int retarget_channels = 0;
    int profile_issues = 0;
    int roll_risks = 0;
    double quality_score = 0.0;
    QString quality_grade;
    QString quality_issues;
    QString quality_flag;
    int missing_major_channels = 0;
    int wrist_flips = 0;
    int eye_reverses = 0;
    double foot_float_ratio = 0.0;
    double shoulder_collapse_ratio = 1.0;
    double root_motion_ratio = 0.0;
    double translation_scale = 1.0;
    double duration_seconds = 0.0;
    double motion_score = 0.0;
    bool sampled = false;
};

std::unordered_map<std::string, int> nodeIndex(const SceneModel& scene) {
    std::unordered_map<std::string, int> index;
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        index[scene.nodes[i].name] = i;
    }
    return index;
}

const AnimationChannelData* channelForNode(const AnimationClipData& clip, int node_index) {
    for (const AnimationChannelData& channel : clip.channels) {
        if (channel.node_index == node_index) {
            return &channel;
        }
    }
    return nullptr;
}

std::vector<Eigen::Matrix4f> globalBindTransforms(const SceneModel& scene) {
    std::vector<Eigen::Matrix4f> globals(scene.nodes.size(), Eigen::Matrix4f::Identity());
    for (int i = 0; i < static_cast<int>(scene.nodes.size()); ++i) {
        const int parent = scene.nodes[i].parent_index;
        if (parent >= 0 && parent < static_cast<int>(globals.size())) {
            globals[i] = globals[parent] * scene.nodes[i].local_bind_transform;
        } else {
            globals[i] = scene.nodes[i].local_bind_transform;
        }
    }
    return globals;
}

Eigen::Vector3f globalPosition(const std::vector<Eigen::Matrix4f>& globals, int node_index) {
    if (node_index < 0 || node_index >= static_cast<int>(globals.size())) {
        return Eigen::Vector3f::Zero();
    }
    return globals[node_index].block<3, 1>(0, 3);
}

const BoneMapping* mappingForCanonical(const BoneMappingResult& mappings, const QString& canonical) {
    for (const BoneMapping& mapping : mappings.mappings) {
        if (mapping.canonical_name.compare(canonical, Qt::CaseInsensitive) == 0) {
            return &mapping;
        }
    }
    return nullptr;
}

void printSegmentDirection(QTextStream& out,
                           const QString& label,
                           const QString& from_canonical,
                           const QString& to_canonical,
                           const BoneMappingResult& mappings,
                           const SceneModel& source_scene,
                           const SceneModel& target_scene) {
    const BoneMapping* source_from = mappingForCanonical(mappings, from_canonical);
    const BoneMapping* source_to = mappingForCanonical(mappings, to_canonical);
    if (!source_from || !source_to) {
        out << "  " << label << ": missing mapping\n";
        return;
    }

    const auto source_nodes = nodeIndex(source_scene);
    const auto target_nodes = nodeIndex(target_scene);
    const std::string source_from_name = source_from->source_bone.toStdString();
    const std::string source_to_name = source_to->source_bone.toStdString();
    const std::string target_from_name = source_from->target_bone.toStdString();
    const std::string target_to_name = source_to->target_bone.toStdString();
    if (!source_nodes.count(source_from_name) || !source_nodes.count(source_to_name) ||
        !target_nodes.count(target_from_name) || !target_nodes.count(target_to_name)) {
        out << "  " << label << ": missing node\n";
        return;
    }

    const std::vector<Eigen::Matrix4f> source_globals = globalBindTransforms(source_scene);
    const std::vector<Eigen::Matrix4f> target_globals = globalBindTransforms(target_scene);
    Eigen::Vector3f source_direction = globalPosition(source_globals, source_nodes.at(source_to_name)) -
                                       globalPosition(source_globals, source_nodes.at(source_from_name));
    Eigen::Vector3f target_direction = globalPosition(target_globals, target_nodes.at(target_to_name)) -
                                       globalPosition(target_globals, target_nodes.at(target_from_name));
    const float source_length = source_direction.norm();
    const float target_length = target_direction.norm();
    if (source_length > 1e-6f) {
        source_direction /= source_length;
    }
    if (target_length > 1e-6f) {
        target_direction /= target_length;
    }
    const float dot = source_direction.dot(target_direction);
    out << QStringLiteral("  %1: source=%2->%3 target=%4->%5 dot=%6 sourceDir=(%7,%8,%9) targetDir=(%10,%11,%12)\n")
               .arg(label,
                    source_from->source_bone,
                    source_to->source_bone,
                    source_from->target_bone,
                    source_to->target_bone)
               .arg(dot, 0, 'f', 4)
               .arg(source_direction.x(), 0, 'f', 4)
               .arg(source_direction.y(), 0, 'f', 4)
               .arg(source_direction.z(), 0, 'f', 4)
               .arg(target_direction.x(), 0, 'f', 4)
               .arg(target_direction.y(), 0, 'f', 4)
               .arg(target_direction.z(), 0, 'f', 4);
}

void printNodeChain(QTextStream& out, const SceneModel& scene, int node_index) {
    QVector<int> chain;
    int current = node_index;
    while (current >= 0 && current < static_cast<int>(scene.nodes.size())) {
        chain.push_front(current);
        current = scene.nodes[current].parent_index;
    }
    for (int i = 0; i < chain.size(); ++i) {
        const SceneNodeData& node = scene.nodes[chain[i]];
        const Eigen::Vector3f t = node.local_bind_transform.block<3, 1>(0, 3);
        out << (i == 0 ? "" : " -> ")
            << QString::fromStdString(node.name)
            << QStringLiteral("(local=%1,%2,%3)")
                   .arg(t.x(), 0, 'f', 4)
                   .arg(t.y(), 0, 'f', 4)
                   .arg(t.z(), 0, 'f', 4);
    }
    out << '\n';
}

void printPositionRange(QTextStream& out, const AnimationChannelData* channel) {
    if (!channel || channel->positions.empty()) {
        out << "    positions: none\n";
        return;
    }
    Eigen::Vector3f min_value = Eigen::Vector3f::Constant(std::numeric_limits<float>::max());
    Eigen::Vector3f max_value = Eigen::Vector3f::Constant(std::numeric_limits<float>::lowest());
    for (const VectorKeyframe& key : channel->positions) {
        min_value = min_value.cwiseMin(key.value);
        max_value = max_value.cwiseMax(key.value);
    }
    out << QStringLiteral("    positions: keys=%1 min=(%2,%3,%4) max=(%5,%6,%7)\n")
               .arg(channel->positions.size())
               .arg(min_value.x(), 0, 'f', 4)
               .arg(min_value.y(), 0, 'f', 4)
               .arg(min_value.z(), 0, 'f', 4)
               .arg(max_value.x(), 0, 'f', 4)
               .arg(max_value.y(), 0, 'f', 4)
               .arg(max_value.z(), 0, 'f', 4);
}

void printRotationStats(QTextStream& out, const AnimationChannelData* channel) {
    if (!channel || channel->rotations.empty()) {
        out << "    rotations: none\n";
        return;
    }
    float max_angle_degrees = 0.0f;
    const Eigen::Quaternionf first = channel->rotations.front().value.normalized();
    for (const RotationKeyframe& key : channel->rotations) {
        Eigen::Quaternionf delta = first.conjugate() * key.value.normalized();
        if (delta.w() < 0.0f) {
            delta.coeffs() *= -1.0f;
        }
        constexpr float kRadiansToDegrees = 57.29577951308232f;
        const float angle = 2.0f * std::acos(std::clamp(delta.w(), -1.0f, 1.0f)) * kRadiansToDegrees;
        max_angle_degrees = std::max(max_angle_degrees, angle);
    }
    out << QStringLiteral("    rotations: keys=%1 maxDeltaFromFirst=%2deg\n")
               .arg(channel->rotations.size())
               .arg(max_angle_degrees, 0, 'f', 2);
}

bool isHandCanonical(const QString& canonical) {
    return canonical.contains(QStringLiteral("Hand"), Qt::CaseInsensitive) ||
           canonical.contains(QStringLiteral("Thumb"), Qt::CaseInsensitive) ||
           canonical.contains(QStringLiteral("Index"), Qt::CaseInsensitive) ||
           canonical.contains(QStringLiteral("Middle"), Qt::CaseInsensitive) ||
           canonical.contains(QStringLiteral("Ring"), Qt::CaseInsensitive) ||
           canonical.contains(QStringLiteral("Little"), Qt::CaseInsensitive);
}

bool isEyeCanonical(const QString& canonical) {
    return canonical == QStringLiteral("Eye") ||
           canonical.endsWith(QStringLiteral("Eye"));
}

bool isInterestingFaceMesh(const MeshData& mesh) {
    const QString text = QString::fromStdString(mesh.name + " " + mesh.material.name).toLower();
    return text.contains(QStringLiteral("eye")) ||
           text.contains(QStringLiteral("face")) ||
           text.contains(QStringLiteral("head")) ||
           text.contains(QStringLiteral("glass")) ||
           text.contains(QStringLiteral("megane")) ||
           text.contains(QStringLiteral("瞳")) ||
           text.contains(QStringLiteral("目"));
}

QString expressionCategoryText(VrmExpressionCategory category) {
    switch (category) {
    case VrmExpressionCategory::Preset:
        return QStringLiteral("preset");
    case VrmExpressionCategory::Custom:
        return QStringLiteral("custom");
    case VrmExpressionCategory::DirectMorph:
        return QStringLiteral("direct-morph");
    }
    return QStringLiteral("unknown");
}

bool isEyeExpressionName(const QString& name) {
    return name.compare(QStringLiteral("blink"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("blinkLeft"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("blinkRight"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("lookUp"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("lookDown"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("lookLeft"), Qt::CaseInsensitive) == 0 ||
           name.compare(QStringLiteral("lookRight"), Qt::CaseInsensitive) == 0 ||
           name.startsWith(QStringLiteral("Fcl_EYE_"), Qt::CaseInsensitive) ||
           name.contains(QStringLiteral("Iris"), Qt::CaseInsensitive) ||
           name.contains(QStringLiteral("Highlight"), Qt::CaseInsensitive);
}

int affectedMeshCount(const SceneModel& scene, const VrmMorphTargetBindData& bind) {
    int count = 0;
    for (const MeshData& mesh : scene.meshes) {
        if (mesh.node_index == bind.node_index &&
            bind.morph_target_index >= 0 &&
            bind.morph_target_index < static_cast<int>(mesh.morph_targets.size())) {
            ++count;
        }
    }
    return count;
}

double expressionRmsPositionDelta(const SceneModel& scene, const VrmExpressionData& expression) {
    double sum = 0.0;
    std::size_t count = 0;
    for (const VrmMorphTargetBindData& bind : expression.morph_target_binds) {
        for (const MeshData& mesh : scene.meshes) {
            if (mesh.node_index != bind.node_index ||
                bind.morph_target_index < 0 ||
                bind.morph_target_index >= static_cast<int>(mesh.morph_targets.size())) {
                continue;
            }
            const MorphTargetData& target = mesh.morph_targets[bind.morph_target_index];
            for (const Eigen::Vector3f& delta : target.position_deltas) {
                sum += static_cast<double>((delta * bind.weight).squaredNorm());
            }
            count += target.position_deltas.size();
        }
    }
    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

void printExpressionChannelRange(QTextStream& out, const ExpressionChannelData& channel) {
    float min_value = std::numeric_limits<float>::max();
    float max_value = std::numeric_limits<float>::lowest();
    for (const ScalarKeyframe& key : channel.weights) {
        min_value = std::min(min_value, key.value);
        max_value = std::max(max_value, key.value);
    }
    if (channel.weights.empty()) {
        min_value = 0.0f;
        max_value = 0.0f;
    }
    out << QStringLiteral("    expression %1 keys=%2 range=(%3,%4)\n")
               .arg(QString::fromStdString(channel.name))
               .arg(channel.weights.size())
               .arg(min_value, 0, 'f', 3)
               .arg(max_value, 0, 'f', 3);
}

void diagnoseExpressions(const QString& target_path, const std::vector<QString>& source_paths) {
    QTextStream out(stdout);
    ModelLoader loader;
    QString error;
    const SceneModel scene = loader.loadFromFile(target_path, &error);
    out << "EXPRESSION_DIAG target=" << target_path << '\n';
    if (!error.isEmpty()) {
        out << "load error=" << error << '\n';
    }
    out << "expressions=" << scene.vrm_expressions.size()
        << " activeWeights=" << scene.expression_weights.size()
        << " lookAtType=" << QString::fromStdString(scene.vrm_look_at.type)
        << " meshes=" << scene.meshes.size() << '\n';
    out << QStringLiteral("lookAt ranges: hInner=%1 hOuter=%2 vDown=%3 vUp=%4\n")
               .arg(scene.vrm_look_at.horizontal_inner.output_scale, 0, 'f', 3)
               .arg(scene.vrm_look_at.horizontal_outer.output_scale, 0, 'f', 3)
               .arg(scene.vrm_look_at.vertical_down.output_scale, 0, 'f', 3)
               .arg(scene.vrm_look_at.vertical_up.output_scale, 0, 'f', 3);
    out << QStringLiteral("lookAt bones: head=%1(%2) leftEye=%3(%4) rightEye=%5(%6)\n")
               .arg(QString::fromStdString(scene.vrm_look_at.head_node_name))
               .arg(scene.vrm_look_at.head_node_index)
               .arg(QString::fromStdString(scene.vrm_look_at.left_eye_node_name))
               .arg(scene.vrm_look_at.left_eye_node_index)
               .arg(QString::fromStdString(scene.vrm_look_at.right_eye_node_name))
               .arg(scene.vrm_look_at.right_eye_node_index);

    for (const VrmExpressionData& expression : scene.vrm_expressions) {
        const QString name = QString::fromStdString(expression.name);
        if (!isEyeExpressionName(name)) {
            continue;
        }
        out << QStringLiteral("  %1 [%2] binds=%3 rmsDelta=%4 binary=%5 override(blink=%6 lookAt=%7 mouth=%8)\n")
                   .arg(name,
                        expressionCategoryText(expression.category))
                   .arg(expression.morph_target_binds.size())
                   .arg(expressionRmsPositionDelta(scene, expression), 0, 'f', 6)
                   .arg(expression.is_binary ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(QString::fromStdString(expression.override_blink),
                        QString::fromStdString(expression.override_look_at),
                        QString::fromStdString(expression.override_mouth));
        for (const VrmMorphTargetBindData& bind : expression.morph_target_binds) {
            QString morph_name = QStringLiteral("<unknown>");
            for (const MeshData& mesh : scene.meshes) {
                if (mesh.node_index == bind.node_index &&
                    bind.morph_target_index >= 0 &&
                    bind.morph_target_index < static_cast<int>(mesh.morph_target_names.size())) {
                    morph_name = QString::fromStdString(mesh.morph_target_names[bind.morph_target_index]);
                    break;
                }
            }
            out << QStringLiteral("    node=%1 target=%2 morph=%3 weight=%4 affectedMeshes=%5\n")
                       .arg(QString::fromStdString(bind.node_name))
                       .arg(bind.morph_target_index)
                       .arg(morph_name)
                       .arg(bind.weight, 0, 'f', 3)
                       .arg(affectedMeshCount(scene, bind));
        }
    }

    for (const QString& source_path : source_paths) {
        QString source_error;
        const SceneModel source = loader.loadAnimationFromFile(source_path, &source_error);
        out << "EXPRESSION_SOURCE source=" << source_path << '\n';
        if (!source_error.isEmpty()) {
            out << "  load error=" << source_error << '\n';
        }
        out << "  clips=" << source.animations.size() << '\n';
        for (const AnimationClipData& clip : source.animations) {
            out << QStringLiteral("  clip=%1 nodeChannels=%2 expressionChannels=%3 duration=%4s\n")
                       .arg(QString::fromStdString(clip.name))
                       .arg(clip.channels.size())
                       .arg(clip.expression_channels.size())
                       .arg(clip.durationSeconds(), 0, 'f', 3);
            for (const ExpressionChannelData& channel : clip.expression_channels) {
                printExpressionChannelRange(out, channel);
            }
        }
    }
}

void diagnoseMeshes(const QString& target_path) {
    QTextStream out(stdout);
    ModelLoader loader;
    QString error;
    const SceneModel scene = loader.loadFromFile(target_path, &error);
    out << "MESH_DIAG target=" << target_path << '\n';
    if (!error.isEmpty()) {
        out << "load error=" << error << '\n';
    }
    out << "meshes=" << scene.meshes.size() << " nodes=" << scene.nodes.size() << '\n';
    for (int i = 0; i < static_cast<int>(scene.meshes.size()); ++i) {
        const MeshData& mesh = scene.meshes[i];
        if (!isInterestingFaceMesh(mesh)) {
            continue;
        }
        QString node_name = QStringLiteral("<none>");
        if (mesh.node_index >= 0 && mesh.node_index < static_cast<int>(scene.nodes.size())) {
            node_name = QString::fromStdString(scene.nodes[mesh.node_index].name);
        }
        out << QStringLiteral("  mesh[%1] name=%2 node=%3 skinned=%4 verts=%5 morphs=%6 material=%7 baseTex=%8 diffuseTex=%9 alpha=%10 rq=%11 zwrite=%12 unlit=%13 mtoon=%14\n")
                   .arg(i)
                   .arg(QString::fromStdString(mesh.name))
                   .arg(node_name)
                   .arg(mesh.skinned() ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(mesh.vertices.size())
                   .arg(mesh.morph_target_count)
                   .arg(QString::fromStdString(mesh.material.name))
                   .arg(mesh.material.base_color_texture.valid() ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(mesh.material.diffuse_texture.valid() ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(mesh.material.alpha_mode)
                   .arg(mesh.material.render_queue_offset)
                   .arg(mesh.material.transparent_with_z_write ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(mesh.material.unlit ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(mesh.material.mtoon ? QStringLiteral("yes") : QStringLiteral("no"));
        if (!mesh.morph_target_names.empty()) {
            QStringList eye_targets;
            for (const std::string& name : mesh.morph_target_names) {
                const QString target_name = QString::fromStdString(name);
                if (target_name.contains(QStringLiteral("EYE"), Qt::CaseInsensitive) ||
                    target_name.contains(QStringLiteral("Iris"), Qt::CaseInsensitive) ||
                    target_name.contains(QStringLiteral("Highlight"), Qt::CaseInsensitive) ||
                    target_name.contains(QStringLiteral("Blink"), Qt::CaseInsensitive)) {
                    eye_targets << target_name;
                }
            }
            if (!eye_targets.isEmpty()) {
                out << "    eye morph targets: " << eye_targets.join(QStringLiteral(", ")) << '\n';
            }
        }
    }
}

void diagnoseCase(const QString& target_path, const QString& source_path) {
    QTextStream out(stdout);
    SkeletonExtractor extractor;
    AiBoneMapper mapper;
    ModelLoader loader;

    QString target_error;
    const SkeletonGraph target_skeleton = extractor.loadFromFile(target_path, &target_error);
    QString source_error;
    const SkeletonGraph source_skeleton = extractor.loadFromFile(source_path, &source_error);
    const BoneMappingResult mapping = mapper.mapSkeletons(source_skeleton, target_skeleton);

    QString target_model_error;
    const SceneModel target_scene = loader.loadFromFile(target_path, &target_model_error);
    QString source_model_error;
    const SceneModel source_animation = loader.loadAnimationFromFile(source_path, &source_model_error);

    out << "DIAG target=" << target_path << '\n';
    out << "DIAG source=" << source_path << '\n';
    out << "target nodes=" << target_scene.nodes.size() << " source nodes=" << source_animation.nodes.size() << '\n';
    if (!target_error.isEmpty()) {
        out << "target skeleton error=" << target_error << '\n';
    }
    if (!source_error.isEmpty()) {
        out << "source skeleton error=" << source_error << '\n';
    }
    if (!target_model_error.isEmpty()) {
        out << "target model error=" << target_model_error << '\n';
    }
    if (!source_model_error.isEmpty()) {
        out << "source model error=" << source_model_error << '\n';
    }

    const auto source_nodes = nodeIndex(source_animation);
    const auto target_nodes = nodeIndex(target_scene);
    const AnimationClipData* clip = source_animation.animations.empty() ? nullptr : &source_animation.animations.front();
    for (const BoneMapping& item : mapping.mappings) {
        const bool major_limb =
            item.canonical_name.contains(QStringLiteral("Shoulder"), Qt::CaseInsensitive) ||
            item.canonical_name.contains(QStringLiteral("UpperArm"), Qt::CaseInsensitive) ||
            item.canonical_name.contains(QStringLiteral("LowerArm"), Qt::CaseInsensitive) ||
            item.canonical_name.contains(QStringLiteral("Hand"), Qt::CaseInsensitive);
        if (item.canonical_name.compare(QStringLiteral("Root"), Qt::CaseInsensitive) != 0 &&
            item.canonical_name.compare(QStringLiteral("Hips"), Qt::CaseInsensitive) != 0 &&
            item.canonical_name.compare(QStringLiteral("LeftFoot"), Qt::CaseInsensitive) != 0 &&
            item.canonical_name.compare(QStringLiteral("RightFoot"), Qt::CaseInsensitive) != 0 &&
            !major_limb &&
            !isEyeCanonical(item.canonical_name)) {
            continue;
        }
        const int source_index = source_nodes.count(item.source_bone.toStdString()) ? source_nodes.at(item.source_bone.toStdString()) : -1;
        const int target_index = target_nodes.count(item.target_bone.toStdString()) ? target_nodes.at(item.target_bone.toStdString()) : -1;
        out << "mapping " << item.canonical_name
            << " source=" << item.source_bone
            << " target=" << item.target_bone
            << " confidence=" << item.confidence << '\n';
        if (source_index >= 0) {
            out << "  source chain: ";
            printNodeChain(out, source_animation, source_index);
            printPositionRange(out, clip ? channelForNode(*clip, source_index) : nullptr);
            printRotationStats(out, clip ? channelForNode(*clip, source_index) : nullptr);
        }
        if (target_index >= 0) {
            out << "  target chain: ";
            printNodeChain(out, target_scene, target_index);
        }
    }
    out << "core rest-pose segment direction samples:\n";
    printSegmentDirection(out, QStringLiteral("spine"), QStringLiteral("Hips"), QStringLiteral("Spine"), mapping, source_animation, target_scene);
    printSegmentDirection(out, QStringLiteral("leftUpperArm"), QStringLiteral("LeftUpperArm"), QStringLiteral("LeftLowerArm"), mapping, source_animation, target_scene);
    printSegmentDirection(out, QStringLiteral("leftLowerArm"), QStringLiteral("LeftLowerArm"), QStringLiteral("LeftHand"), mapping, source_animation, target_scene);
    printSegmentDirection(out, QStringLiteral("rightUpperArm"), QStringLiteral("RightUpperArm"), QStringLiteral("RightLowerArm"), mapping, source_animation, target_scene);
    printSegmentDirection(out, QStringLiteral("rightLowerArm"), QStringLiteral("RightLowerArm"), QStringLiteral("RightHand"), mapping, source_animation, target_scene);
    printSegmentDirection(out, QStringLiteral("leftUpperLeg"), QStringLiteral("LeftUpperLeg"), QStringLiteral("LeftLowerLeg"), mapping, source_animation, target_scene);
    printSegmentDirection(out, QStringLiteral("leftLowerLeg"), QStringLiteral("LeftLowerLeg"), QStringLiteral("LeftFoot"), mapping, source_animation, target_scene);
    printSegmentDirection(out, QStringLiteral("rightUpperLeg"), QStringLiteral("RightUpperLeg"), QStringLiteral("RightLowerLeg"), mapping, source_animation, target_scene);
    printSegmentDirection(out, QStringLiteral("rightLowerLeg"), QStringLiteral("RightLowerLeg"), QStringLiteral("RightFoot"), mapping, source_animation, target_scene);

    out << "hand/finger mapping samples:\n";
    for (const BoneMapping& item : mapping.mappings) {
        if (!isHandCanonical(item.canonical_name)) {
            continue;
        }
        out << QStringLiteral("  %1: %2 -> %3 confidence=%4\n")
                   .arg(item.canonical_name,
                        item.source_bone,
                        item.target_bone)
                   .arg(item.confidence, 0, 'f', 3);
    }

    out << "eye mapping samples:\n";
    for (const BoneMapping& item : mapping.mappings) {
        if (!isEyeCanonical(item.canonical_name)) {
            continue;
        }
        const int source_index = source_nodes.count(item.source_bone.toStdString()) ? source_nodes.at(item.source_bone.toStdString()) : -1;
        const int target_index = target_nodes.count(item.target_bone.toStdString()) ? target_nodes.at(item.target_bone.toStdString()) : -1;
        out << QStringLiteral("  %1: %2 -> %3 confidence=%4 sourceChannel=%5 targetNode=%6\n")
                   .arg(item.canonical_name,
                        item.source_bone,
                        item.target_bone)
                   .arg(item.confidence, 0, 'f', 3)
                   .arg(clip && channelForNode(*clip, source_index) ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(target_index);
    }

    const RetargetProfile profile = buildRetargetProfile(source_animation, target_scene, mapping);
    out << "retarget profile:\n" << retargetProfileDetailedText(profile, false, 16) << '\n';

    SceneModel retargeted;
    QString retarget_error;
    if (retargetAnimationToTarget(source_animation, target_scene, mapping, &retargeted, &retarget_error) &&
        !retargeted.animations.empty()) {
        const auto retarget_nodes = nodeIndex(retargeted);
        out << "retargeted hips/root position channels:\n";
        for (const BoneMapping& item : mapping.mappings) {
            if (item.canonical_name.compare(QStringLiteral("Root"), Qt::CaseInsensitive) != 0 &&
                item.canonical_name.compare(QStringLiteral("Hips"), Qt::CaseInsensitive) != 0 &&
                !isEyeCanonical(item.canonical_name)) {
                continue;
            }
            const int node_index = retarget_nodes.count(item.target_bone.toStdString()) ? retarget_nodes.at(item.target_bone.toStdString()) : -1;
            out << "  " << item.canonical_name << " target=" << item.target_bone << '\n';
            printPositionRange(out, channelForNode(retargeted.animations.front(), node_index));
            printRotationStats(out, channelForNode(retargeted.animations.front(), node_index));
        }
        const RetargetQualityReport quality = scoreRetargetedAnimation(source_animation, target_scene, retargeted, mapping);
        out << "retarget quality:\n" << retargetQualitySummaryText(quality, false) << '\n';
        for (const RetargetQualityIssue& issue : quality.issues) {
            out << QStringLiteral("  quality %1 value=%2 %3\n")
                       .arg(issue.code)
                       .arg(issue.value, 0, 'f', 4)
                       .arg(issue.message);
        }
    } else if (!retarget_error.isEmpty()) {
        out << "retarget error=" << retarget_error << '\n';
    }
}

CaseResult runCase(const QString& target_path, const QString& source_path) {
    CaseResult result;
    result.target = target_path;
    result.source = source_path;

    SkeletonExtractor extractor;
    AiBoneMapper mapper;
    ModelLoader loader;

    QString target_error;
    const SkeletonGraph target_skeleton = extractor.loadFromFile(target_path, &target_error);
    if (target_skeleton.empty()) {
        result.status = QStringLiteral("target skeleton failed: %1").arg(target_error);
        return result;
    }
    result.target_nodes = target_skeleton.bones.size();
    result.target_skinned_bones = target_skeleton.skinnedBoneCount();

    QString source_error;
    const SkeletonGraph source_skeleton = extractor.loadFromFile(source_path, &source_error);
    if (source_skeleton.empty()) {
        result.status = QStringLiteral("source skeleton failed: %1").arg(source_error);
        return result;
    }
    result.source_nodes = source_skeleton.bones.size();
    result.source_clips = source_skeleton.animations.size();
    result.source_channels = source_skeleton.animations.isEmpty() ? 0 : source_skeleton.animations.front().channel_count;
    if (result.source_clips == 0 || result.source_channels == 0) {
        result.status = QStringLiteral("source animation failed: no transform animation channels");
        return result;
    }

    const BoneMappingResult mapping = mapper.mapSkeletons(source_skeleton, target_skeleton);
    result.mapped = mapping.mappings.size();
    result.unmapped_source = mapping.unmapped_source_bones.size();
    result.unmapped_target = mapping.unmapped_target_bones.size();
    if (mapping.mappings.empty()) {
        result.status = QStringLiteral("no mapping");
        return result;
    }

    QString target_model_error;
    const SceneModel target_scene = loader.loadFromFile(target_path, &target_model_error);
    if (target_scene.empty()) {
        result.status = QStringLiteral("target model failed: %1").arg(target_model_error);
        return result;
    }

    QString source_model_error;
    const SceneModel source_animation = loader.loadAnimationFromFile(source_path, &source_model_error);
    if (source_animation.nodes.empty() || source_animation.animations.empty()) {
        result.status = QStringLiteral("source animation failed: %1").arg(source_model_error);
        return result;
    }

    SceneModel retargeted;
    QString retarget_error;
    if (!retargetAnimationToTarget(source_animation, target_scene, mapping, &retargeted, &retarget_error)) {
        result.status = QStringLiteral("retarget failed: %1").arg(retarget_error);
        return result;
    }

    const RetargetProfile profile = buildRetargetProfile(source_animation, target_scene, mapping);
    result.profile_issues = profile.issues.size();
    result.roll_risks = profile.high_roll_risk_count;
    result.translation_scale = profile.translation_scale;
    result.retarget_channels = retargeted.animations.empty() ? 0 : static_cast<int>(retargeted.animations.front().channels.size());
    result.duration_seconds = retargeted.animations.empty() ? 0.0 : retargeted.animations.front().durationSeconds();
    result.motion_score = sampledMotionScore(retargeted, 0, &result.sampled);
    const RetargetQualityReport quality = scoreRetargetedAnimation(source_animation, target_scene, retargeted, mapping);
    result.quality_score = quality.score;
    result.quality_grade = quality.grade;
    result.quality_issues = retargetQualityIssueCodes(quality);
    if (result.quality_score < 70.0) {
        result.quality_flag = QStringLiteral("low_quality");
    } else if (result.quality_issues != QStringLiteral("none")) {
        result.quality_flag = QStringLiteral("needs_review");
    } else {
        result.quality_flag = QStringLiteral("ok");
    }
    result.missing_major_channels = quality.missing_major_channels;
    result.wrist_flips = quality.wrist_flip_count;
    result.eye_reverses = quality.eye_reverse_count;
    result.foot_float_ratio = quality.foot_float_ratio;
    result.shoulder_collapse_ratio = quality.shoulder_collapse_ratio;
    result.root_motion_ratio = quality.root_motion_ratio;
    result.status.clear();
    return result;
}

void printHeader(QTextStream& out) {
    out << "target,source,status,target_nodes,target_skinned_bones,source_nodes,source_clips,source_channels,"
           "mapped,unmapped_source,unmapped_target,retarget_channels,profile_issues,roll_risks,quality_score,quality_grade,"
           "quality_issues,quality_flag,missing_major_channels,wrist_flips,eye_reverses,foot_float_ratio,shoulder_collapse_ratio,root_motion_ratio,translation_scale,"
           "duration_seconds,motion_score,sampled\n";
}

void printResult(QTextStream& out, const CaseResult& result) {
    out << csv(baseName(result.target)) << ','
        << csv(baseName(result.source)) << ','
        << csv(statusText(result.status)) << ','
        << result.target_nodes << ','
        << result.target_skinned_bones << ','
        << result.source_nodes << ','
        << result.source_clips << ','
        << result.source_channels << ','
        << result.mapped << ','
        << result.unmapped_source << ','
        << result.unmapped_target << ','
        << result.retarget_channels << ','
        << result.profile_issues << ','
        << result.roll_risks << ','
        << QString::number(result.quality_score, 'f', 2) << ','
        << csv(result.quality_grade) << ','
        << csv(result.quality_issues) << ','
        << csv(result.status.trimmed().isEmpty() ? result.quality_flag : QStringLiteral("load_or_retarget_failed")) << ','
        << result.missing_major_channels << ','
        << result.wrist_flips << ','
        << result.eye_reverses << ','
        << QString::number(result.foot_float_ratio, 'f', 6) << ','
        << QString::number(result.shoulder_collapse_ratio, 'f', 6) << ','
        << QString::number(result.root_motion_ratio, 'f', 6) << ','
        << QString::number(result.translation_scale, 'f', 6) << ','
        << QString::number(result.duration_seconds, 'f', 3) << ','
        << QString::number(result.motion_score, 'f', 6) << ','
        << (result.sampled ? "yes" : "no") << '\n';
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QString root = QStringLiteral("F:/AIGril/Resources");
    QString out_path;
    int target_limit = 4;
    int source_limit = 10;
    bool diagnose = false;
    bool mesh_diagnose = false;
    bool expression_diagnose = false;
    std::vector<QString> explicit_targets;
    std::vector<QString> explicit_sources;

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args[i];
        if (arg == QStringLiteral("--root") && i + 1 < args.size()) {
            root = args[++i];
        } else if (arg == QStringLiteral("--out") && i + 1 < args.size()) {
            out_path = args[++i];
        } else if (arg == QStringLiteral("--target-limit") && i + 1 < args.size()) {
            target_limit = std::max(1, args[++i].toInt());
        } else if (arg == QStringLiteral("--source-limit") && i + 1 < args.size()) {
            source_limit = std::max(1, args[++i].toInt());
        } else if (arg == QStringLiteral("--target") && i + 1 < args.size()) {
            explicit_targets.push_back(QDir::toNativeSeparators(args[++i]));
        } else if (arg == QStringLiteral("--source") && i + 1 < args.size()) {
            explicit_sources.push_back(QDir::toNativeSeparators(args[++i]));
        } else if (arg == QStringLiteral("--diagnose")) {
            diagnose = true;
        } else if (arg == QStringLiteral("--mesh-diagnose")) {
            mesh_diagnose = true;
        } else if (arg == QStringLiteral("--expression-diagnose")) {
            expression_diagnose = true;
        }
    }

    std::vector<QString> targets = explicit_targets.empty() ? chooseTargets(root) : explicit_targets;
    std::vector<QString> sources = explicit_sources.empty() ? chooseSources(root) : explicit_sources;
    if (target_limit > 0 && targets.size() > static_cast<std::size_t>(target_limit)) {
        targets.resize(static_cast<std::size_t>(target_limit));
    }
    if (source_limit > 0 && sources.size() > static_cast<std::size_t>(source_limit)) {
        sources.resize(static_cast<std::size_t>(source_limit));
    }

    if (diagnose) {
        for (const QString& target : targets) {
            for (const QString& source : sources) {
                diagnoseCase(target, source);
            }
        }
        return 0;
    }
    if (mesh_diagnose) {
        for (const QString& target : targets) {
            diagnoseMeshes(target);
        }
        return 0;
    }
    if (expression_diagnose) {
        for (const QString& target : targets) {
            diagnoseExpressions(target, sources);
        }
        return 0;
    }

    QFile out_file;
    QTextStream* out = nullptr;
    QTextStream stdout_stream(stdout);
    if (!out_path.isEmpty()) {
        out_file.setFileName(out_path);
        if (!out_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            stdout_stream << "failed to open output: " << out_path << '\n';
            return 2;
        }
        out = new QTextStream(&out_file);
    } else {
        out = &stdout_stream;
    }

    printHeader(*out);
    for (const QString& target : targets) {
        for (const QString& source : sources) {
            printResult(*out, runCase(target, source));
            out->flush();
        }
    }

    if (out != &stdout_stream) {
        delete out;
    }
    return 0;
}
