#include "rigging/ai_bone_mapper.h"
#include "rigging/skeleton_extractor.h"
#include "rigging/skeleton_types.h"
#include "rendering/render_backend.h"
#include "scene/model_loader.h"
#include "scene/native_model_converter.h"
#include "scene/vrm_native_writer.h"

#include <Eigen/Dense>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPolygonF>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {

using namespace haorendergi;

QString normalizedExtension(QString extension) {
    extension = extension.trimmed().toLower();
    if (extension.startsWith(QStringLiteral("."))) {
        extension.remove(0, 1);
    }
    return extension;
}

QString suffixLower(const QString& path) {
    return QFileInfo(path).suffix().toLower();
}

int clampInt(float value, int min_value, int max_value) {
    return std::max(min_value, std::min(max_value, static_cast<int>(std::round(value))));
}

QJsonArray vectorToJson(const Eigen::Vector3f& value) {
    QJsonArray array;
    array.append(value.x());
    array.append(value.y());
    array.append(value.z());
    return array;
}

QJsonObject boundsToJson(const Bounds& bounds) {
    QJsonObject object;
    object.insert(QStringLiteral("valid"), bounds.valid());
    object.insert(QStringLiteral("min"), vectorToJson(bounds.min));
    object.insert(QStringLiteral("max"), vectorToJson(bounds.max));
    object.insert(QStringLiteral("center"), vectorToJson(bounds.center()));
    object.insert(QStringLiteral("extent"), vectorToJson(bounds.extent()));
    object.insert(QStringLiteral("radius"), bounds.radius());
    return object;
}

int countTextures(const SceneModel& scene) {
    int count = 0;
    for (const MeshData& mesh : scene.meshes) {
        const MaterialData& material = mesh.material;
        const TextureData* textures[] = {
            &material.base_color_texture,
            &material.diffuse_texture,
            &material.normal_texture,
            &material.specular_texture,
            &material.metallic_texture,
            &material.roughness_texture,
            &material.ao_texture,
            &material.emissive_texture,
            &material.mtoon_shade_multiply_texture,
            &material.mtoon_matcap_texture,
            &material.mtoon_outline_width_texture
        };
        for (const TextureData* texture : textures) {
            if (texture && (!texture->path.empty() || texture->valid())) {
                ++count;
            }
        }
    }
    return count;
}

int countMorphTargets(const SceneModel& scene) {
    int count = 0;
    for (const MeshData& mesh : scene.meshes) {
        count += std::max(mesh.morph_target_count, static_cast<int>(mesh.morph_targets.size()));
    }
    return count;
}

int countSkinnedMeshes(const SceneModel& scene) {
    int count = 0;
    for (const MeshData& mesh : scene.meshes) {
        if (mesh.skinned()) {
            ++count;
        }
    }
    return count;
}

QStringList materialFeatureTags(const SceneModel& scene) {
    bool has_mtoon = false;
    bool has_unlit = false;
    bool has_alpha = false;
    bool has_pbr = false;
    bool has_normal = false;
    for (const MeshData& mesh : scene.meshes) {
        const MaterialData& material = mesh.material;
        has_mtoon = has_mtoon || material.mtoon;
        has_unlit = has_unlit || material.unlit;
        has_alpha = has_alpha || material.alpha_mode != 0 || material.base_alpha_factor < 0.999f;
        has_pbr = has_pbr || material.metallic_factor > 0.001f || material.roughness_factor < 0.999f;
        has_normal = has_normal || material.normal_texture.valid() || !material.normal_texture.path.empty();
    }

    QStringList tags;
    if (has_mtoon) tags << QStringLiteral("MToon");
    if (has_pbr) tags << QStringLiteral("PBR");
    if (has_unlit) tags << QStringLiteral("Unlit");
    if (has_alpha) tags << QStringLiteral("Alpha");
    if (has_normal) tags << QStringLiteral("NormalMap");
    if (tags.isEmpty()) tags << QStringLiteral("Basic");
    return tags;
}

QJsonObject materialToJson(const MaterialData& material) {
    QJsonObject object;
    object.insert(QStringLiteral("name"), QString::fromStdString(material.name));
    object.insert(QStringLiteral("baseColor"), vectorToJson(material.base_color_factor));
    object.insert(QStringLiteral("alpha"), material.base_alpha_factor);
    object.insert(QStringLiteral("metallic"), material.metallic_factor);
    object.insert(QStringLiteral("roughness"), material.roughness_factor);
    object.insert(QStringLiteral("mtoon"), material.mtoon);
    object.insert(QStringLiteral("unlit"), material.unlit);
    object.insert(QStringLiteral("doubleSided"), material.double_sided);
    object.insert(QStringLiteral("hasBaseColorTexture"), material.base_color_texture.valid() || !material.base_color_texture.path.empty());
    object.insert(QStringLiteral("hasNormalTexture"), material.normal_texture.valid() || !material.normal_texture.path.empty());
    object.insert(QStringLiteral("hasMetallicTexture"), material.metallic_texture.valid() || !material.metallic_texture.path.empty());
    object.insert(QStringLiteral("hasRoughnessTexture"), material.roughness_texture.valid() || !material.roughness_texture.path.empty());
    object.insert(QStringLiteral("hasEmissiveTexture"), material.emissive_texture.valid() || !material.emissive_texture.path.empty());
    return object;
}

QJsonArray animationsToJson(const SceneModel& scene) {
    QJsonArray array;
    for (const AnimationClipData& clip : scene.animations) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), QString::fromStdString(clip.name));
        object.insert(QStringLiteral("durationSeconds"), clip.durationSeconds());
        object.insert(QStringLiteral("durationTicks"), clip.duration_ticks);
        object.insert(QStringLiteral("ticksPerSecond"), clip.ticks_per_second);
        object.insert(QStringLiteral("channels"), static_cast<int>(clip.channels.size()));
        object.insert(QStringLiteral("expressionChannels"), static_cast<int>(clip.expression_channels.size()));
        array.append(object);
    }
    return array;
}

QJsonArray expressionsToJson(const SceneModel& scene) {
    QJsonArray array;
    for (const VrmExpressionData& expression : scene.vrm_expressions) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), QString::fromStdString(expression.name));
        object.insert(QStringLiteral("isBinary"), expression.is_binary);
        object.insert(QStringLiteral("morphTargetBinds"), static_cast<int>(expression.morph_target_binds.size()));
        object.insert(QStringLiteral("overrideBlink"), QString::fromStdString(expression.override_blink));
        object.insert(QStringLiteral("overrideLookAt"), QString::fromStdString(expression.override_look_at));
        object.insert(QStringLiteral("overrideMouth"), QString::fromStdString(expression.override_mouth));
        array.append(object);
    }
    return array;
}

QJsonArray meshesToJson(const SceneModel& scene) {
    QJsonArray array;
    for (const MeshData& mesh : scene.meshes) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), QString::fromStdString(mesh.name));
        object.insert(QStringLiteral("nodeIndex"), mesh.node_index);
        object.insert(QStringLiteral("vertices"), static_cast<qint64>(mesh.vertices.size()));
        object.insert(QStringLiteral("triangles"), static_cast<qint64>(mesh.indices.size() / 3));
        object.insert(QStringLiteral("skinned"), mesh.skinned());
        object.insert(QStringLiteral("skinBones"), static_cast<int>(mesh.skin_bones.size()));
        object.insert(QStringLiteral("morphTargets"), std::max(mesh.morph_target_count, static_cast<int>(mesh.morph_targets.size())));
        object.insert(QStringLiteral("material"), materialToJson(mesh.material));
        array.append(object);
    }
    return array;
}

SceneModel loadBestEffortScene(const QString& path, QString* load_error) {
    ModelLoader loader;
    SceneModel scene = loader.loadFromFile(path, load_error);
    if (scene.empty() && scene.nodes.empty() && scene.animations.empty()) {
        QString animation_error;
        SceneModel animation_scene = loader.loadAnimationFromFile(path, &animation_error);
        if (!animation_scene.nodes.empty() || !animation_scene.animations.empty()) {
            scene = std::move(animation_scene);
            if (load_error) {
                load_error->clear();
            }
        } else if (load_error && load_error->trimmed().isEmpty()) {
            *load_error = animation_error;
        }
    }
    return scene;
}

QJsonObject inspectModel(const QString& path) {
    QString load_error;
    SceneModel scene = loadBestEffortScene(path, &load_error);

    SkeletonExtractor extractor;
    QString skeleton_error;
    SkeletonGraph skeleton = extractor.loadFromFile(path, &skeleton_error);

    const bool has_scene_payload = !scene.empty() || !scene.nodes.empty() || !scene.animations.empty();
    QJsonObject root;
    root.insert(QStringLiteral("ok"), has_scene_payload);
    root.insert(QStringLiteral("command"), QStringLiteral("inspect-model"));
    root.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("source"), QDir::toNativeSeparators(path));
    root.insert(QStringLiteral("format"), suffixLower(path).toUpper());
    if (!has_scene_payload) {
        root.insert(QStringLiteral("error"), load_error.trimmed().isEmpty() ? QStringLiteral("Failed to load model.") : load_error);
    }
    if (!skeleton_error.trimmed().isEmpty() && skeleton.empty()) {
        root.insert(QStringLiteral("skeletonError"), skeleton_error);
    }

    QJsonObject summary;
    summary.insert(QStringLiteral("meshes"), static_cast<int>(scene.meshes.size()));
    summary.insert(QStringLiteral("vertices"), static_cast<qint64>(scene.vertexCount()));
    summary.insert(QStringLiteral("triangles"), static_cast<qint64>(scene.triangleCount()));
    summary.insert(QStringLiteral("nodes"), static_cast<int>(scene.nodes.size()));
    summary.insert(QStringLiteral("animations"), static_cast<int>(scene.animations.size()));
    summary.insert(QStringLiteral("materials"), static_cast<int>(scene.meshes.size()));
    summary.insert(QStringLiteral("textures"), countTextures(scene));
    summary.insert(QStringLiteral("skinnedMeshes"), countSkinnedMeshes(scene));
    summary.insert(QStringLiteral("morphTargets"), countMorphTargets(scene));
    summary.insert(QStringLiteral("vrmExpressions"), static_cast<int>(scene.vrm_expressions.size()));
    summary.insert(QStringLiteral("skeletonBones"), skeleton.bones.size());
    summary.insert(QStringLiteral("skinnedBones"), skeleton.skinnedBoneCount());
    summary.insert(QStringLiteral("recognizedHumanoidBones"), skeleton.recognizedBoneCount());
    root.insert(QStringLiteral("summary"), summary);

    root.insert(QStringLiteral("bounds"), boundsToJson(scene.bounds));
    root.insert(QStringLiteral("materialTags"), QJsonArray::fromStringList(materialFeatureTags(scene)));
    root.insert(QStringLiteral("meshes"), meshesToJson(scene));
    root.insert(QStringLiteral("animations"), animationsToJson(scene));
    root.insert(QStringLiteral("vrmExpressions"), expressionsToJson(scene));
    root.insert(QStringLiteral("skeleton"), skeletonGraphToJson(skeleton));

    QJsonObject look_at;
    look_at.insert(QStringLiteral("type"), QString::fromStdString(scene.vrm_look_at.type));
    look_at.insert(QStringLiteral("headNode"), QString::fromStdString(scene.vrm_look_at.head_node_name));
    look_at.insert(QStringLiteral("leftEyeNode"), QString::fromStdString(scene.vrm_look_at.left_eye_node_name));
    look_at.insert(QStringLiteral("rightEyeNode"), QString::fromStdString(scene.vrm_look_at.right_eye_node_name));
    root.insert(QStringLiteral("vrmLookAt"), look_at);

    return root;
}

QColor meshColor(const MeshData& mesh) {
    const Eigen::Vector3f color = mesh.material.base_color_factor;
    return QColor(clampInt(color.x() * 255.0f, 70, 245),
                  clampInt(color.y() * 255.0f, 70, 245),
                  clampInt(color.z() * 255.0f, 70, 245),
                  clampInt(mesh.material.base_alpha_factor * 210.0f, 80, 210));
}

Eigen::Vector3f rotateForPreview(const Eigen::Vector3f& point) {
    const float yaw = -0.55f;
    const float pitch = 0.25f;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);

    Eigen::Vector3f p;
    p.x() = point.x() * cy + point.z() * sy;
    p.z() = -point.x() * sy + point.z() * cy;
    p.y() = point.y();

    Eigen::Vector3f r;
    r.x() = p.x();
    r.y() = p.y() * cp - p.z() * sp;
    r.z() = p.y() * sp + p.z() * cp;
    return r;
}

QPointF projectPoint(const Eigen::Vector3f& point,
                     const Eigen::Vector3f& center,
                     float scale,
                     int width,
                     int height) {
    const Eigen::Vector3f rotated = rotateForPreview(point - center);
    return QPointF(width * 0.5 + rotated.x() * scale,
                   height * 0.56 - rotated.y() * scale);
}

bool writePreviewImage(const QString& input_path, const QString& output_path, int width, int height, QString* status) {
    QString load_error;
    const SceneModel scene = loadBestEffortScene(input_path, &load_error);

    width = std::max(320, std::min(width, 4096));
    height = std::max(240, std::min(height, 4096));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(16, 20, 26));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(0, 0, width, height, QColor(16, 20, 26));

    QLinearGradient gradient(0, 0, 0, height);
    gradient.setColorAt(0.0, QColor(28, 35, 45));
    gradient.setColorAt(1.0, QColor(10, 12, 16));
    painter.fillRect(0, 0, width, height, gradient);

    painter.setPen(QPen(QColor(65, 74, 88), 1));
    painter.drawLine(width * 0.12, height * 0.82, width * 0.88, height * 0.82);

    if (scene.meshes.empty() || !scene.bounds.valid()) {
        painter.setPen(QColor(220, 228, 238));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 18, QFont::DemiBold));
        painter.drawText(QRect(32, 32, width - 64, height - 64),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         load_error.trimmed().isEmpty()
                             ? QStringLiteral("No mesh preview available")
                             : QStringLiteral("Preview unavailable\n%1").arg(load_error));
    } else {
        const Eigen::Vector3f center = scene.bounds.center();
        const Eigen::Vector3f extent = scene.bounds.extent();
        const float longest_axis = std::max(0.0001f, std::max(extent.x(), std::max(extent.y(), extent.z())));
        const float scale = std::min(width, height) * 0.66f / longest_axis;

        std::size_t total_triangles = scene.triangleCount();
        const std::size_t triangle_budget = 45000;
        const std::size_t stride = std::max<std::size_t>(1, total_triangles / triangle_budget);
        std::size_t triangle_index = 0;

        for (const MeshData& mesh : scene.meshes) {
            const QColor fill = meshColor(mesh);
            QColor edge = fill.lighter(140);
            edge.setAlpha(170);
            painter.setPen(QPen(edge, 0.75));
            painter.setBrush(QColor(fill.red(), fill.green(), fill.blue(), std::min(fill.alpha(), 120)));
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3, ++triangle_index) {
                if (triangle_index % stride != 0) {
                    continue;
                }
                const std::uint32_t i0 = mesh.indices[i];
                const std::uint32_t i1 = mesh.indices[i + 1];
                const std::uint32_t i2 = mesh.indices[i + 2];
                if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
                    continue;
                }
                QPolygonF polygon;
                polygon << projectPoint(mesh.vertices[i0].position, center, scale, width, height)
                        << projectPoint(mesh.vertices[i1].position, center, scale, width, height)
                        << projectPoint(mesh.vertices[i2].position, center, scale, width, height);
                painter.drawPolygon(polygon);
            }
        }
    }

    painter.setPen(QColor(235, 240, 248));
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 15, QFont::DemiBold));
    painter.drawText(24, 36, QFileInfo(input_path).fileName());
    painter.setPen(QColor(156, 169, 186));
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 10));
    painter.drawText(24, 58, QStringLiteral("%1 meshes  |  %2 triangles  |  %3 animations")
        .arg(scene.meshes.size())
        .arg(scene.triangleCount())
        .arg(scene.animations.size()));
    painter.end();

    QDir().mkpath(QFileInfo(output_path).absolutePath());
    if (!image.save(output_path)) {
        if (status) {
            *status = QStringLiteral("Failed to save preview image: %1").arg(QDir::toNativeSeparators(output_path));
        }
        return false;
    }
    if (status) {
        *status = QStringLiteral("Preview image written: %1").arg(QDir::toNativeSeparators(output_path));
    }
    return true;
}

bool writeJsonFile(const QString& path, const QJsonObject& object) {
    QFile file(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return true;
}

void printJson(const QJsonObject& object) {
    QTextStream(stdout) << QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)) << Qt::endl;
}

QJsonObject optionObject(const QString& id, const QString& label) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("label"), label);
    return object;
}

QJsonObject rangeControl(const QString& id,
                         const QString& label,
                         double value,
                         double min_value,
                         double max_value,
                         double step,
                         const QString& section) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("range"));
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("label"), label);
    object.insert(QStringLiteral("value"), value);
    object.insert(QStringLiteral("min"), min_value);
    object.insert(QStringLiteral("max"), max_value);
    object.insert(QStringLiteral("step"), step);
    object.insert(QStringLiteral("section"), section);
    return object;
}

QJsonObject toggleControl(const QString& id, const QString& label, bool value, const QString& section) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("toggle"));
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("label"), label);
    object.insert(QStringLiteral("value"), value);
    object.insert(QStringLiteral("section"), section);
    return object;
}

QJsonObject colorControl(const QString& id, const QString& label, const QColor& value, const QString& section) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("color"));
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("label"), label);
    object.insert(QStringLiteral("value"), value.name(QColor::HexRgb));
    object.insert(QStringLiteral("section"), section);
    return object;
}

QJsonObject selectControl(const QString& id,
                          const QString& label,
                          const QString& value,
                          const QJsonArray& options,
                          const QString& section) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("select"));
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("label"), label);
    object.insert(QStringLiteral("value"), value);
    object.insert(QStringLiteral("options"), options);
    object.insert(QStringLiteral("section"), section);
    return object;
}

QJsonArray rgbaChannelOptions() {
    QJsonArray options;
    options.append(optionObject(QStringLiteral("0"), QStringLiteral("R")));
    options.append(optionObject(QStringLiteral("1"), QStringLiteral("G")));
    options.append(optionObject(QStringLiteral("2"), QStringLiteral("B")));
    options.append(optionObject(QStringLiteral("3"), QStringLiteral("A")));
    return options;
}

QJsonArray renderModeOptions() {
    QJsonArray options;
    options.append(optionObject(QStringLiteral("raster"), QStringLiteral("Raster")));
    options.append(optionObject(QStringLiteral("opengl-ray-trace"), QStringLiteral("OpenGL Ray Trace")));
    options.append(optionObject(QStringLiteral("dxr-hardware-rt"), QStringLiteral("DXR Hardware RT")));
    options.append(optionObject(QStringLiteral("cpu-software"), QStringLiteral("CPU Software Renderer")));
    return options;
}

QJsonArray shadingOptions() {
    QJsonArray options;
    options.append(optionObject(QStringLiteral("pbr"), QStringLiteral("PBR")));
    options.append(optionObject(QStringLiteral("phong"), QStringLiteral("Phong / Toon")));
    return options;
}

QJsonArray rayTraceSceneOptions() {
    QJsonArray options;
    options.append(optionObject(QStringLiteral("cornell-box"), QStringLiteral("Cornell Box")));
    options.append(optionObject(QStringLiteral("loaded-model"), QStringLiteral("Loaded Model")));
    return options;
}

QJsonArray rayTraceIntegratorOptions() {
    QJsonArray options;
    options.append(optionObject(QStringLiteral("hybrid"), QStringLiteral("Hybrid RT")));
    options.append(optionObject(QStringLiteral("path-trace"), QStringLiteral("Path Trace")));
    options.append(optionObject(QStringLiteral("path-trace-nee"), QStringLiteral("Path Trace + NEE")));
    options.append(optionObject(QStringLiteral("photon-path"), QStringLiteral("Photon Path")));
    return options;
}

QJsonArray rayTraceViewOptions() {
    QJsonArray options;
    options.append(optionObject(QStringLiteral("lit"), QStringLiteral("Lit")));
    options.append(optionObject(QStringLiteral("hit"), QStringLiteral("Hit / Miss")));
    options.append(optionObject(QStringLiteral("normal"), QStringLiteral("Normal")));
    options.append(optionObject(QStringLiteral("albedo"), QStringLiteral("Albedo")));
    return options;
}

QJsonObject pipelineObject(const QString& id,
                           const QString& label,
                           const QString& backend,
                           const QString& device,
                           const QString& status,
                           const QString& note) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("label"), label);
    object.insert(QStringLiteral("backend"), backend);
    object.insert(QStringLiteral("device"), device);
    object.insert(QStringLiteral("status"), status);
    object.insert(QStringLiteral("note"), note);
    return object;
}

QJsonObject renderCapabilities() {
    LookDevSettings defaults;

    QJsonObject root;
    root.insert(QStringLiteral("ok"), true);
    root.insert(QStringLiteral("command"), QStringLiteral("render-capabilities"));
    root.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("uiContract"), QStringLiteral("Electron owns all controls; C++ exposes render backends, settings, and future frame service."));

    QJsonArray pipelines;
    pipelines.append(pipelineObject(
        QStringLiteral("raster"),
        QStringLiteral("Raster / OpenGL GPU"),
        QStringLiteral("OpenGLRasterizer"),
        QStringLiteral("OpenGL 3.3 GPU"),
        QStringLiteral("active"),
        QStringLiteral("Interactive PBR, Phong, Toon, outline, shadows, IBL, material channel controls.")));
    pipelines.append(pipelineObject(
        QStringLiteral("opengl-ray-trace"),
        QStringLiteral("OpenGL Ray Trace"),
        QStringLiteral("OpenGLRayTracer"),
        QStringLiteral("OpenGL 4.3 GPU SSBO"),
        QStringLiteral("active"),
        QStringLiteral("Cornell / loaded model, hybrid RT, path tracing, NEE, photon cache, AOV views.")));
    pipelines.append(pipelineObject(
        QStringLiteral("dxr-hardware-rt"),
        QStringLiteral("DXR Hardware RT"),
        QStringLiteral("DxrRayTracer"),
        QStringLiteral("Direct3D 12 DXR GPU"),
        QStringLiteral("active-when-supported"),
        QStringLiteral("Hardware ray tracing path with D3D12/DXR resources and OpenGL presentation interop.")));
    pipelines.append(pipelineObject(
        QStringLiteral("cpu-software"),
        QStringLiteral("CPU Software Renderer"),
        QStringLiteral("Legacy HaoRender CPU path"),
        QStringLiteral("CPU"),
        QStringLiteral("bridge-required"),
        QStringLiteral("Core capability must be preserved; Electron should route it through a backend service instead of opening Qt.")));
    root.insert(QStringLiteral("pipelines"), pipelines);

    QJsonArray shared_controls;
    shared_controls.append(selectControl(QStringLiteral("renderPipeline"), QStringLiteral("Render path"), QStringLiteral("raster"), renderModeOptions(), QStringLiteral("Look Dev")));
    shared_controls.append(selectControl(QStringLiteral("shadingModel"), QStringLiteral("Shading model"), QStringLiteral("pbr"), shadingOptions(), QStringLiteral("Look Dev")));
    shared_controls.append(rangeControl(QStringLiteral("exposure"), QStringLiteral("Exposure"), defaults.exposure, 0.1, 3.0, 0.05, QStringLiteral("Look Dev")));
    shared_controls.append(rangeControl(QStringLiteral("normalStrength"), QStringLiteral("Normal intensity"), defaults.normal_strength, 0.0, 2.0, 0.05, QStringLiteral("Look Dev")));
    root.insert(QStringLiteral("sharedControls"), shared_controls);

    QJsonArray raster_controls;
    raster_controls.append(toggleControl(QStringLiteral("pbr.iblEnabled"), QStringLiteral("Enable image based lighting"), defaults.pbr.ibl_enabled, QStringLiteral("PBR Lighting")));
    raster_controls.append(rangeControl(QStringLiteral("pbr.iblDiffuse"), QStringLiteral("IBL diffuse"), defaults.pbr.ibl_diffuse_strength, 0.0, 2.0, 0.05, QStringLiteral("PBR Lighting")));
    raster_controls.append(rangeControl(QStringLiteral("pbr.iblSpecular"), QStringLiteral("IBL specular"), defaults.pbr.ibl_specular_strength, 0.0, 2.0, 0.05, QStringLiteral("PBR Lighting")));
    raster_controls.append(rangeControl(QStringLiteral("pbr.skyLight"), QStringLiteral("Sky light"), defaults.pbr.sky_light_strength, 0.0, 2.0, 0.05, QStringLiteral("PBR Lighting")));
    raster_controls.append(selectControl(QStringLiteral("pbr.metallicChannel"), QStringLiteral("Metallic channel"), QString::number(defaults.pbr.metallic_channel), rgbaChannelOptions(), QStringLiteral("PBR Channel Map")));
    raster_controls.append(selectControl(QStringLiteral("pbr.roughnessChannel"), QStringLiteral("Roughness channel"), QString::number(defaults.pbr.roughness_channel), rgbaChannelOptions(), QStringLiteral("PBR Channel Map")));
    raster_controls.append(selectControl(QStringLiteral("pbr.aoChannel"), QStringLiteral("AO channel"), QString::number(defaults.pbr.ao_channel), rgbaChannelOptions(), QStringLiteral("PBR Channel Map")));
    raster_controls.append(selectControl(QStringLiteral("pbr.emissiveChannel"), QStringLiteral("Emissive channel"), QString::number(defaults.pbr.emissive_channel), rgbaChannelOptions(), QStringLiteral("PBR Channel Map")));
    raster_controls.append(toggleControl(QStringLiteral("phong.hardSpecular"), QStringLiteral("Hard-edge specular"), defaults.phong.hard_specular, QStringLiteral("Phong Surface")));
    raster_controls.append(toggleControl(QStringLiteral("phong.tonemap"), QStringLiteral("Apply tone mapping"), defaults.phong.use_tonemap, QStringLiteral("Phong Surface")));
    raster_controls.append(toggleControl(QStringLiteral("phong.primaryOnly"), QStringLiteral("Primary light only"), defaults.phong.primary_light_only, QStringLiteral("Phong Surface")));
    raster_controls.append(rangeControl(QStringLiteral("phong.secondaryLight"), QStringLiteral("Secondary light"), defaults.phong.secondary_light_scale, 0.0, 1.5, 0.02, QStringLiteral("Phong Surface")));
    raster_controls.append(rangeControl(QStringLiteral("phong.diffuse"), QStringLiteral("Diffuse strength"), defaults.phong.diffuse_strength, 0.0, 2.0, 0.02, QStringLiteral("Phong Surface")));
    raster_controls.append(rangeControl(QStringLiteral("phong.ambient"), QStringLiteral("Ambient strength"), defaults.phong.ambient_strength, 0.0, 1.0, 0.01, QStringLiteral("Phong Surface")));
    raster_controls.append(rangeControl(QStringLiteral("phong.specular"), QStringLiteral("Specular strength"), defaults.phong.specular_strength, 0.0, 2.0, 0.02, QStringLiteral("Phong Surface")));
    raster_controls.append(rangeControl(QStringLiteral("phong.smoothness"), QStringLiteral("Smoothness"), defaults.phong.smoothness, 0.0, 1.0, 0.01, QStringLiteral("Phong Surface")));
    raster_controls.append(rangeControl(QStringLiteral("phong.specularMapWeight"), QStringLiteral("Specular map weight"), defaults.phong.specular_map_weight, 0.0, 2.0, 0.02, QStringLiteral("Phong Surface")));
    raster_controls.append(rangeControl(QStringLiteral("phong.shininess"), QStringLiteral("Shininess"), defaults.phong.shininess, 4.0, 128.0, 2.0, QStringLiteral("Phong Surface")));
    raster_controls.append(colorControl(QStringLiteral("phong.ambientColor"), QStringLiteral("Ambient color"), defaults.phong.ambient_color, QStringLiteral("Phong Color Tuning")));
    raster_controls.append(colorControl(QStringLiteral("phong.specularTint"), QStringLiteral("Specular tint"), defaults.phong.specular_tint, QStringLiteral("Phong Color Tuning")));
    raster_controls.append(rangeControl(QStringLiteral("phong.rimStrength"), QStringLiteral("Rim strength"), defaults.phong.rim_strength, 0.0, 2.0, 0.02, QStringLiteral("Rim Light")));
    raster_controls.append(rangeControl(QStringLiteral("phong.rimPower"), QStringLiteral("Rim power"), defaults.phong.rim_power, 0.25, 8.0, 0.25, QStringLiteral("Rim Light")));
    raster_controls.append(colorControl(QStringLiteral("phong.rimTint"), QStringLiteral("Rim tint"), defaults.phong.rim_tint, QStringLiteral("Rim Light")));
    raster_controls.append(toggleControl(QStringLiteral("toon.enabled"), QStringLiteral("Enable toon shaping"), defaults.phong.toon.enabled, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.diffuseSteps"), QStringLiteral("Diffuse steps"), defaults.phong.toon.diffuse_steps, 2.0, 6.0, 1.0, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.bandSoftness"), QStringLiteral("Band softness"), defaults.phong.toon.diffuse_softness, 0.0, 0.25, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.shadowFloor"), QStringLiteral("Shadow floor"), defaults.phong.toon.shadow_floor, 0.0, 0.8, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.litFloor"), QStringLiteral("Lit floor"), defaults.phong.toon.lit_floor, 0.0, 1.0, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.rampBias"), QStringLiteral("Ramp bias"), defaults.phong.toon.ramp_bias, -0.5, 0.5, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.rampContrast"), QStringLiteral("Ramp contrast"), defaults.phong.toon.ramp_contrast, 0.25, 2.5, 0.05, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.shadowReceive"), QStringLiteral("Shadow receive"), defaults.phong.toon.shadow_map_strength, 0.0, 1.0, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.shadowThreshold"), QStringLiteral("Shadow threshold"), defaults.phong.toon.shadow_threshold, 0.0, 1.0, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.shadowSoftness"), QStringLiteral("Shadow softness"), defaults.phong.toon.shadow_softness, 0.0, 0.25, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(colorControl(QStringLiteral("toon.shadowTint"), QStringLiteral("Shadow tint"), defaults.phong.toon.shadow_tint, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.highlightThreshold"), QStringLiteral("Highlight threshold"), defaults.phong.toon.highlight_threshold, 0.0, 1.0, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.highlightSoftness"), QStringLiteral("Highlight softness"), defaults.phong.toon.highlight_softness, 0.0, 0.25, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.highlightStrength"), QStringLiteral("Highlight strength"), defaults.phong.toon.highlight_strength, 0.0, 2.0, 0.02, QStringLiteral("Toon Shading")));
    raster_controls.append(colorControl(QStringLiteral("toon.highlightTint"), QStringLiteral("Highlight tint"), defaults.phong.toon.highlight_tint, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.rimThreshold"), QStringLiteral("Rim threshold"), defaults.phong.toon.rim_threshold, 0.0, 1.0, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(rangeControl(QStringLiteral("toon.rimSoftness"), QStringLiteral("Rim softness"), defaults.phong.toon.rim_softness, 0.0, 0.25, 0.01, QStringLiteral("Toon Shading")));
    raster_controls.append(toggleControl(QStringLiteral("toon.materialOverride"), QStringLiteral("Enable material override"), defaults.phong.toon.material_override_enabled, QStringLiteral("Material Toon Override")));
    raster_controls.append(rangeControl(QStringLiteral("toon.textureInfluence"), QStringLiteral("Texture influence"), defaults.phong.toon.material_texture_strength, 0.0, 1.0, 0.01, QStringLiteral("Material Toon Override")));
    raster_controls.append(rangeControl(QStringLiteral("toon.albedoLift"), QStringLiteral("Albedo lift"), defaults.phong.toon.material_lift, 0.0, 0.8, 0.01, QStringLiteral("Material Toon Override")));
    raster_controls.append(rangeControl(QStringLiteral("toon.albedoSaturation"), QStringLiteral("Albedo saturation"), defaults.phong.toon.material_saturation, 0.0, 2.0, 0.05, QStringLiteral("Material Toon Override")));
    raster_controls.append(rangeControl(QStringLiteral("toon.albedoContrast"), QStringLiteral("Albedo contrast"), defaults.phong.toon.material_contrast, 0.25, 2.5, 0.05, QStringLiteral("Material Toon Override")));
    raster_controls.append(toggleControl(QStringLiteral("outline.enabled"), QStringLiteral("Enable silhouette outline"), defaults.phong.outline.enabled, QStringLiteral("Outline")));
    raster_controls.append(rangeControl(QStringLiteral("outline.width"), QStringLiteral("Width (px)"), defaults.phong.outline.width_pixels, 0.0, 12.0, 0.25, QStringLiteral("Outline")));
    raster_controls.append(rangeControl(QStringLiteral("outline.opacity"), QStringLiteral("Opacity"), defaults.phong.outline.opacity, 0.0, 1.0, 0.01, QStringLiteral("Outline")));
    raster_controls.append(rangeControl(QStringLiteral("outline.depthBias"), QStringLiteral("Depth bias"), defaults.phong.outline.depth_bias, 0.0, 0.02, 0.0005, QStringLiteral("Outline")));
    raster_controls.append(colorControl(QStringLiteral("outline.color"), QStringLiteral("Outline color"), defaults.phong.outline.color, QStringLiteral("Outline")));
    raster_controls.append(toggleControl(QStringLiteral("raster.shadows"), QStringLiteral("Enable shadows"), defaults.enable_shadows, QStringLiteral("Raster Settings")));
    raster_controls.append(toggleControl(QStringLiteral("raster.backfaceCulling"), QStringLiteral("Enable backface culling"), defaults.enable_backface_culling, QStringLiteral("Raster Settings")));
    root.insert(QStringLiteral("rasterControls"), raster_controls);

    QJsonArray ray_controls;
    ray_controls.append(selectControl(QStringLiteral("ray.scene"), QStringLiteral("Scene"), QStringLiteral("cornell-box"), rayTraceSceneOptions(), QStringLiteral("Ray Trace Preview")));
    ray_controls.append(selectControl(QStringLiteral("ray.integrator"), QStringLiteral("Integrator"), QStringLiteral("path-trace-nee"), rayTraceIntegratorOptions(), QStringLiteral("Ray Trace Preview")));
    ray_controls.append(selectControl(QStringLiteral("ray.view"), QStringLiteral("Debug view"), QStringLiteral("lit"), rayTraceViewOptions(), QStringLiteral("Ray Trace Preview")));
    ray_controls.append(rangeControl(QStringLiteral("ray.ambient"), QStringLiteral("Ambient"), defaults.ray_trace.ambient_strength, 0.0, 0.5, 0.01, QStringLiteral("Ray Trace Preview")));
    ray_controls.append(rangeControl(QStringLiteral("ray.shadowStrength"), QStringLiteral("Shadow strength"), defaults.ray_trace.shadow_strength, 0.0, 1.0, 0.01, QStringLiteral("Ray Trace Preview")));
    ray_controls.append(rangeControl(QStringLiteral("ray.maxBounces"), QStringLiteral("Max bounces"), defaults.ray_trace.max_bounces, 1.0, 24.0, 1.0, QStringLiteral("Ray Trace Preview")));
    ray_controls.append(rangeControl(QStringLiteral("ray.neeBounces"), QStringLiteral("NEE bounces"), defaults.ray_trace.max_nee_bounces, 0.0, 8.0, 1.0, QStringLiteral("Ray Trace Preview")));
    ray_controls.append(rangeControl(QStringLiteral("ray.samplesPerFrame"), QStringLiteral("Samples / frame"), defaults.ray_trace.samples_per_frame, 1.0, 8.0, 1.0, QStringLiteral("Ray Trace Preview")));
    ray_controls.append(toggleControl(QStringLiteral("ray.enableNee"), QStringLiteral("Enable next-event estimation"), defaults.ray_trace.enable_nee, QStringLiteral("Ray Trace Preview")));
    ray_controls.append(toggleControl(QStringLiteral("ray.enablePhotonCache"), QStringLiteral("Enable photon cache"), defaults.ray_trace.enable_photon_cache, QStringLiteral("Ray Trace Preview")));
    ray_controls.append(rangeControl(QStringLiteral("ray.photonRadius"), QStringLiteral("Photon radius"), defaults.ray_trace.photon_radius, 0.02, 1.2, 0.01, QStringLiteral("Ray Trace Preview")));
    ray_controls.append(rangeControl(QStringLiteral("ray.photonIntensity"), QStringLiteral("Photon intensity"), defaults.ray_trace.photon_intensity, 0.0, 4.0, 0.05, QStringLiteral("Ray Trace Preview")));
    root.insert(QStringLiteral("rayTraceControls"), ray_controls);

    QJsonArray capture_controls;
    capture_controls.append(optionObject(QStringLiteral("snapshot"), QStringLiteral("Snapshot")));
    capture_controls.append(optionObject(QStringLiteral("reset-camera"), QStringLiteral("Reset Camera")));
    capture_controls.append(optionObject(QStringLiteral("open-model"), QStringLiteral("Open Model")));
    root.insert(QStringLiteral("captureActions"), capture_controls);

    return root;
}

void printUsage() {
    QTextStream out(stdout);
    out << "haorender-core.exe commands:\n"
        << "  --inspect-model <input> [--json <report.json>]\n"
        << "  --convert-model <input> <output> [target-extension] [--json <report.json>]\n"
        << "  --rig-map <target-character> <source-animation> [--json <mapping.json>]\n"
        << "  --render-preview <input> <output.png> [--width 960] [--height 640]\n"
        << "  --render-capabilities\n"
        << "  --list-export-formats\n";
}

int failJson(const QString& command, const QString& message, int code) {
    QJsonObject object;
    object.insert(QStringLiteral("ok"), false);
    object.insert(QStringLiteral("command"), command);
    object.insert(QStringLiteral("error"), message);
    printJson(object);
    return code;
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty() || args.contains(QStringLiteral("--help")) || args.contains(QStringLiteral("-h"))) {
        printUsage();
        return args.isEmpty() ? 1 : 0;
    }

    QString json_output_path;
    int width = 960;
    int height = 640;
    QStringList positional;
    for (int i = 0; i < args.size(); ++i) {
        const QString arg = args[i];
        if (arg == QStringLiteral("--json") && i + 1 < args.size()) {
            json_output_path = args[++i];
        } else if (arg == QStringLiteral("--width") && i + 1 < args.size()) {
            width = args[++i].toInt();
        } else if (arg == QStringLiteral("--height") && i + 1 < args.size()) {
            height = args[++i].toInt();
        } else {
            positional << arg;
        }
    }

    if (positional.first() == QStringLiteral("--list-export-formats")) {
        NativeModelConverter converter;
        QJsonObject root;
        root.insert(QStringLiteral("ok"), true);
        root.insert(QStringLiteral("command"), QStringLiteral("list-export-formats"));
        QJsonArray formats;
        for (const NativeExportFormat& format : converter.availableFormats()) {
            QJsonObject item;
            item.insert(QStringLiteral("id"), format.id);
            item.insert(QStringLiteral("extension"), format.extension);
            item.insert(QStringLiteral("description"), format.description);
            formats.append(item);
        }
        root.insert(QStringLiteral("formats"), formats);
        printJson(root);
        return 0;
    }

    if (positional.first() == QStringLiteral("--render-capabilities")) {
        const QJsonObject result = renderCapabilities();
        if (!json_output_path.isEmpty()) {
            writeJsonFile(json_output_path, result);
        }
        printJson(result);
        return 0;
    }

    if (positional.first() == QStringLiteral("--inspect-model")) {
        if (positional.size() < 2) {
            return failJson(QStringLiteral("inspect-model"), QStringLiteral("Missing input model path."), 2);
        }
        const QJsonObject result = inspectModel(positional[1]);
        if (!json_output_path.isEmpty()) {
            writeJsonFile(json_output_path, result);
        }
        printJson(result);
        return result.value(QStringLiteral("ok")).toBool() ? 0 : 3;
    }

    if (positional.first() == QStringLiteral("--convert-model")) {
        if (positional.size() < 3) {
            return failJson(QStringLiteral("convert-model"), QStringLiteral("Missing input or output path."), 2);
        }
        const QString input_path = positional[1];
        const QString output_path = positional[2];
        QString target_extension = positional.size() >= 4 ? positional[3] : QFileInfo(output_path).suffix();
        target_extension = normalizedExtension(target_extension);

        QString status;
        bool ok = false;
        if (target_extension == QStringLiteral("vrm")) {
            SkeletonExtractor extractor;
            QString skeleton_error;
            const SkeletonGraph skeleton = extractor.loadFromFile(input_path, &skeleton_error);
            if (skeleton.empty()) {
                status = QStringLiteral("Failed to extract skeleton: %1").arg(skeleton_error);
            } else {
                VrmNativeWriter writer;
                ok = writer.writeFromSource(input_path, output_path, skeleton, &status);
            }
        } else {
            NativeModelConverter converter;
            ok = converter.convert(input_path, output_path, target_extension, &status);
        }

        QJsonObject result;
        result.insert(QStringLiteral("ok"), ok);
        result.insert(QStringLiteral("command"), QStringLiteral("convert-model"));
        result.insert(QStringLiteral("source"), QDir::toNativeSeparators(input_path));
        result.insert(QStringLiteral("output"), QDir::toNativeSeparators(output_path));
        result.insert(QStringLiteral("targetFormat"), target_extension.toUpper());
        result.insert(QStringLiteral("status"), status);
        result.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (ok) {
            result.insert(QStringLiteral("inspect"), inspectModel(output_path));
        }
        if (!json_output_path.isEmpty()) {
            writeJsonFile(json_output_path, result);
        }
        printJson(result);
        return ok ? 0 : 3;
    }

    if (positional.first() == QStringLiteral("--rig-map")) {
        if (positional.size() < 3) {
            return failJson(QStringLiteral("rig-map"), QStringLiteral("Missing target character or source animation path."), 2);
        }
        const QString target_path = positional[1];
        const QString source_path = positional[2];

        SkeletonExtractor extractor;
        QString target_error;
        QString source_error;
        const SkeletonGraph target_skeleton = extractor.loadFromFile(target_path, &target_error);
        const SkeletonGraph source_skeleton = extractor.loadFromFile(source_path, &source_error);

        QJsonObject result;
        result.insert(QStringLiteral("ok"), !target_skeleton.empty() && !source_skeleton.empty());
        result.insert(QStringLiteral("command"), QStringLiteral("rig-map"));
        result.insert(QStringLiteral("targetPath"), QDir::toNativeSeparators(target_path));
        result.insert(QStringLiteral("sourcePath"), QDir::toNativeSeparators(source_path));
        result.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

        if (target_skeleton.empty()) {
            result.insert(QStringLiteral("targetError"), target_error.trimmed().isEmpty() ? QStringLiteral("No target skeleton found.") : target_error);
        }
        if (source_skeleton.empty()) {
            result.insert(QStringLiteral("sourceError"), source_error.trimmed().isEmpty() ? QStringLiteral("No source skeleton found.") : source_error);
        }

        if (!target_skeleton.empty() && !source_skeleton.empty()) {
            AiBoneMapper mapper;
            const BoneMappingResult mapping = mapper.mapSkeletons(source_skeleton, target_skeleton);
            QJsonObject mapping_json = boneMappingResultToJson(source_skeleton, target_skeleton, mapping);
            result.insert(QStringLiteral("summary"), mapping.summary);
            result.insert(QStringLiteral("mappedCount"), mapping.mappings.size());
            result.insert(QStringLiteral("unmappedSourceCount"), mapping.unmapped_source_bones.size());
            result.insert(QStringLiteral("unmappedTargetCount"), mapping.unmapped_target_bones.size());
            result.insert(QStringLiteral("mapping"), mapping_json);
        }

        if (!json_output_path.isEmpty()) {
            writeJsonFile(json_output_path, result);
        }
        printJson(result);
        return result.value(QStringLiteral("ok")).toBool() ? 0 : 3;
    }

    if (positional.first() == QStringLiteral("--render-preview")) {
        if (positional.size() < 3) {
            return failJson(QStringLiteral("render-preview"), QStringLiteral("Missing input or output image path."), 2);
        }
        QString status;
        const bool ok = writePreviewImage(positional[1], positional[2], width, height, &status);
        QJsonObject result;
        result.insert(QStringLiteral("ok"), ok);
        result.insert(QStringLiteral("command"), QStringLiteral("render-preview"));
        result.insert(QStringLiteral("source"), QDir::toNativeSeparators(positional[1]));
        result.insert(QStringLiteral("output"), QDir::toNativeSeparators(positional[2]));
        result.insert(QStringLiteral("width"), width);
        result.insert(QStringLiteral("height"), height);
        result.insert(QStringLiteral("status"), status);
        printJson(result);
        return ok ? 0 : 3;
    }

    return failJson(QStringLiteral("unknown"), QStringLiteral("Unknown command."), 2);
}
