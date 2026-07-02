#include "app/asset_converter_window.h"

#include "rigging/skeleton_extractor.h"
#include "scene/native_model_converter.h"
#include "scene/vrm_native_writer.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTextBrowser>
#include <QTextStream>
#include <QVBoxLayout>

#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace haorendergi {

namespace {

QString trAsset(bool chinese, const QString& english, const QString& chinese_text) {
    return chinese ? chinese_text : english;
}

QString formatCount(std::size_t value) {
    return QLocale().toString(static_cast<qulonglong>(value));
}

QString suffixLower(const QString& path) {
    return QFileInfo(path).suffix().toLower();
}

QString boolText(bool value) {
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

bool writeUtf8TextFile(const QString& path, const QString& text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

QString powerShellSingleQuote(QString value) {
    value.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(value);
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

QStringList conversionRisks(const SceneModel& scene,
                            const SkeletonGraph& skeleton,
                            const QString& source_extension,
                            const QString& target_format,
                            bool chinese) {
    QStringList risks;
    const bool has_skeleton = !skeleton.empty();
    const bool has_morphs = countMorphTargets(scene) > 0 || !scene.vrm_expressions.empty();
    const bool has_mtoon = std::any_of(scene.meshes.begin(), scene.meshes.end(), [](const MeshData& mesh) {
        return mesh.material.mtoon;
    });
    if ((target_format == QStringLiteral("VRM") || target_format == QStringLiteral("PMX")) && !has_skeleton) {
        risks << trAsset(chinese,
                         QStringLiteral("Target format expects a character skeleton, but no reliable skeleton was found."),
                         QStringLiteral("目标格式需要角色骨骼，但当前没有找到可靠的骨骼。"));
    }
    if (target_format == QStringLiteral("VRM") && skeleton.recognizedBoneCount() < 15) {
        risks << trAsset(chinese,
                         QStringLiteral("VRM export needs humanoid bone semantics; current recognized bone count is low."),
                         QStringLiteral("VRM 导出需要人形骨骼语义，当前识别到的人形骨骼数量偏少。"));
    }
    if (target_format == QStringLiteral("PMX") && !has_morphs) {
        risks << trAsset(chinese,
                         QStringLiteral("PMX/MMD characters usually need facial morphs; none were detected."),
                         QStringLiteral("PMX/MMD 角色通常需要面部 morph，但当前没有检测到。"));
    }
    if (target_format == QStringLiteral("GLB") && source_extension == QStringLiteral("fbx")) {
        risks << trAsset(chinese,
                         QStringLiteral("FBX animation and material interpretation may differ after glTF export."),
                         QStringLiteral("FBX 动画和材质在导出为 glTF 后可能会有解释差异。"));
    }
    if (target_format == QStringLiteral("PMX") && has_mtoon) {
        risks << trAsset(chinese,
                         QStringLiteral("MToon material must be approximated as MMD toon/shade/matcap settings."),
                         QStringLiteral("MToon 材质需要近似映射为 MMD 的 toon、shade、matcap 设置。"));
    }
    if (scene.hasAnimations() && target_format == QStringLiteral("VRM")) {
        risks << trAsset(chinese,
                         QStringLiteral("VRM carries avatar data; animation may need VRMA export rather than embedding."),
                         QStringLiteral("VRM 主要承载角色数据，动画可能更适合导出为 VRMA，而不是直接嵌入。"));
    }
    if (risks.isEmpty()) {
        risks << trAsset(chinese,
                         QStringLiteral("No blocking issue detected. Visual QA is still required after export."),
                         QStringLiteral("未检测到阻塞问题，但导出后仍需要做视觉质检。"));
    }
    return risks;
}

QString markdownList(const QStringList& items) {
    QString text;
    for (const QString& item : items) {
        text += QStringLiteral("- %1\n").arg(item);
    }
    return text;
}

} // namespace

AssetConverterWindow::AssetConverterWindow(QWidget* parent, bool embedded)
    : QDialog(parent),
      embedded_(embedded) {
    setObjectName(QStringLiteral("WorkspaceRoot"));
    if (embedded_) {
        setWindowFlags(Qt::Widget);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    } else {
        setWindowFlags(windowFlags() | Qt::Window);
        resize(1280, 860);
        setMinimumSize(1040, 680);
    }
    buildUi();
    refreshEmptyState();
}

void AssetConverterWindow::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(12);

    title_label_ = new QLabel(QStringLiteral("Asset Converter"), this);
    title_label_->setProperty("panelTitle", "true");
    subtitle_label_ = new QLabel(QStringLiteral("FBX / GLB / GLTF / OBJ / DAE / STL / PLY / VRM conversion planning, QA and preview"), this);
    subtitle_label_->setProperty("panelSubtitle", "true");
    root->addWidget(title_label_);
    root->addWidget(subtitle_label_);

    auto* control_row = new QHBoxLayout();
    control_row->setSpacing(8);
    source_path_edit_ = new QLineEdit(this);
    source_path_edit_->setReadOnly(true);
    source_path_edit_->setPlaceholderText(QStringLiteral("Load a source asset..."));
    import_button_ = new QPushButton(QStringLiteral("Import"), this);
    import_button_->setObjectName(QStringLiteral("ActionButton"));
    target_combo_ = new QComboBox(this);
    target_combo_->addItem(QStringLiteral("GLB"), static_cast<int>(TargetFormat::Glb));
    target_combo_->addItem(QStringLiteral("GLTF"), static_cast<int>(TargetFormat::Gltf));
    target_combo_->addItem(QStringLiteral("FBX"), static_cast<int>(TargetFormat::Fbx));
    target_combo_->addItem(QStringLiteral("OBJ"), static_cast<int>(TargetFormat::Obj));
    target_combo_->addItem(QStringLiteral("DAE / Collada"), static_cast<int>(TargetFormat::Dae));
    target_combo_->addItem(QStringLiteral("STL"), static_cast<int>(TargetFormat::Stl));
    target_combo_->addItem(QStringLiteral("PLY"), static_cast<int>(TargetFormat::Ply));
    target_combo_->addItem(QStringLiteral("VRM"), static_cast<int>(TargetFormat::Vrm));
    target_combo_->addItem(QStringLiteral("PMX / MMD (later)"), static_cast<int>(TargetFormat::Pmx));
    output_dir_edit_ = new QLineEdit(this);
    output_dir_edit_->setReadOnly(true);
    output_dir_edit_->setPlaceholderText(QStringLiteral("Output directory"));
    output_button_ = new QPushButton(QStringLiteral("Output"), this);
    output_button_->setObjectName(QStringLiteral("ActionButton"));
    control_row->addWidget(source_path_edit_, 3);
    control_row->addWidget(import_button_);
    control_row->addWidget(target_combo_);
    control_row->addWidget(output_dir_edit_, 2);
    control_row->addWidget(output_button_);
    root->addLayout(control_row);

    auto* action_row = new QHBoxLayout();
    action_row->setSpacing(8);
    plan_button_ = new QPushButton(QStringLiteral("AI Plan"), this);
    plan_button_->setObjectName(QStringLiteral("ActionButton"));
    execute_button_ = new QPushButton(QStringLiteral("Execute"), this);
    execute_button_->setObjectName(QStringLiteral("ActionButton"));
    report_button_ = new QPushButton(QStringLiteral("Save Report"), this);
    report_button_->setObjectName(QStringLiteral("ActionButton"));
    status_label_ = new QLabel(QStringLiteral("Ready"), this);
    status_label_->setProperty("panelSubtitle", "true");
    action_row->addWidget(plan_button_);
    action_row->addWidget(execute_button_);
    action_row->addWidget(report_button_);
    action_row->addStretch(1);
    action_row->addWidget(status_label_);
    root->addLayout(action_row);

    auto* body = new QHBoxLayout();
    body->setSpacing(12);

    auto* left = new QVBoxLayout();
    left->setSpacing(10);
    inventory_view_ = new QTextBrowser(this);
    inventory_view_->setMinimumHeight(150);
    plan_view_ = new QTextBrowser(this);
    plan_view_->setMinimumHeight(220);
    report_view_ = new QTextBrowser(this);
    report_view_->setMinimumHeight(180);
    left->addWidget(inventory_view_, 1);
    left->addWidget(plan_view_, 1);
    left->addWidget(report_view_, 1);
    body->addLayout(left, 1);

    auto* preview_column = new QVBoxLayout();
    preview_column->setSpacing(10);
    before_label_ = new QLabel(QStringLiteral("Before"), this);
    before_label_->setProperty("sectionTitle", "true");
    before_viewport_ = new RenderViewport(this);
    before_viewport_->setMinimumHeight(260);
    after_label_ = new QLabel(QStringLiteral("After / Export Preview"), this);
    after_label_->setProperty("sectionTitle", "true");
    after_viewport_ = new RenderViewport(this);
    after_viewport_->setMinimumHeight(260);
    preview_column->addWidget(before_label_);
    preview_column->addWidget(before_viewport_, 1);
    preview_column->addWidget(after_label_);
    preview_column->addWidget(after_viewport_, 1);
    body->addLayout(preview_column, 1);

    root->addLayout(body, 1);

    connect(import_button_, &QPushButton::clicked, this, [this]() { chooseSource(); });
    connect(output_button_, &QPushButton::clicked, this, [this]() { chooseOutputDirectory(); });
    connect(plan_button_, &QPushButton::clicked, this, [this]() { generateConversionPlan(); });
    connect(execute_button_, &QPushButton::clicked, this, [this]() { executeConversion(); });
    connect(report_button_, &QPushButton::clicked, this, [this]() { saveReport(); });
    connect(target_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        refreshTargetUi();
        generateConversionPlan();
    });
    refreshLanguage();
}

void AssetConverterWindow::setChineseUi(bool enabled) {
    chinese_ui_ = enabled;
    refreshLanguage();
}

void AssetConverterWindow::refreshLanguage() {
    setWindowTitle(trAsset(chinese_ui_, QStringLiteral("Asset Converter"), QStringLiteral("资产转换器")));
    if (title_label_) {
        title_label_->setText(trAsset(chinese_ui_, QStringLiteral("Asset Converter"), QStringLiteral("资产转换器")));
    }
    if (subtitle_label_) {
        subtitle_label_->setText(trAsset(
            chinese_ui_,
            QStringLiteral("FBX / GLB / GLTF / OBJ / DAE / STL / PLY / VRM conversion planning, QA and preview"),
            QStringLiteral("FBX / GLB / GLTF / OBJ / DAE / STL / PLY / VRM 转换规划、质检与预览")));
    }
    if (source_path_edit_) {
        source_path_edit_->setPlaceholderText(trAsset(chinese_ui_,
                                                      QStringLiteral("Load a source asset..."),
                                                      QStringLiteral("加载源模型资源...")));
    }
    if (output_dir_edit_) {
        output_dir_edit_->setPlaceholderText(trAsset(chinese_ui_,
                                                     QStringLiteral("Output directory"),
                                                     QStringLiteral("输出目录")));
    }
    if (import_button_) {
        import_button_->setText(trAsset(chinese_ui_, QStringLiteral("Import"), QStringLiteral("导入")));
    }
    if (output_button_) {
        output_button_->setText(trAsset(chinese_ui_, QStringLiteral("Output"), QStringLiteral("输出目录")));
    }
    if (plan_button_) {
        plan_button_->setText(trAsset(chinese_ui_, QStringLiteral("AI Plan"), QStringLiteral("AI 转换计划")));
    }
    if (execute_button_) {
        execute_button_->setText(trAsset(chinese_ui_, QStringLiteral("Execute"), QStringLiteral("执行转换")));
    }
    if (report_button_) {
        report_button_->setText(trAsset(chinese_ui_, QStringLiteral("Save Report"), QStringLiteral("保存报告")));
    }
    if (before_label_) {
        before_label_->setText(trAsset(chinese_ui_, QStringLiteral("Before"), QStringLiteral("转换前")));
    }
    if (after_label_) {
        after_label_->setText(trAsset(chinese_ui_, QStringLiteral("After / Export Preview"), QStringLiteral("转换后 / 导出预览")));
    }

    refreshTargetComboText();
    if (source_scene_.empty()) {
        refreshEmptyState();
        if (status_label_) {
            status_label_->setText(trAsset(chinese_ui_, QStringLiteral("Ready"), QStringLiteral("就绪")));
        }
        return;
    }

    refreshSourceSummary();
    generateConversionPlan();
    if (!last_status_.isEmpty()) {
        status_label_->setText(last_output_model_path_.isEmpty()
            ? trAsset(chinese_ui_,
                      QStringLiteral("Plan generated; backend required"),
                      QStringLiteral("已生成计划；需要后端补充"))
            : trAsset(chinese_ui_,
                      QStringLiteral("Converted: %1"),
                      QStringLiteral("已转换：%1")).arg(QFileInfo(last_output_model_path_).fileName()));
    } else if (!source_path_.isEmpty()) {
        status_label_->setText(trAsset(chinese_ui_,
                                      QStringLiteral("Loaded %1"),
                                      QStringLiteral("已加载：%1")).arg(QFileInfo(source_path_).fileName()));
    }
}

void AssetConverterWindow::refreshTargetComboText() {
    if (!target_combo_) {
        return;
    }
    const QSignalBlocker blocker(target_combo_);
    for (int i = 0; i < target_combo_->count(); ++i) {
        const auto format = static_cast<TargetFormat>(target_combo_->itemData(i).toInt());
        QString text;
        switch (format) {
        case TargetFormat::Glb:
            text = QStringLiteral("GLB");
            break;
        case TargetFormat::Gltf:
            text = QStringLiteral("GLTF");
            break;
        case TargetFormat::Fbx:
            text = QStringLiteral("FBX");
            break;
        case TargetFormat::Obj:
            text = QStringLiteral("OBJ");
            break;
        case TargetFormat::Dae:
            text = QStringLiteral("DAE / Collada");
            break;
        case TargetFormat::Stl:
            text = QStringLiteral("STL");
            break;
        case TargetFormat::Ply:
            text = QStringLiteral("PLY");
            break;
        case TargetFormat::Vrm:
            text = QStringLiteral("VRM");
            break;
        case TargetFormat::Pmx:
            text = trAsset(chinese_ui_, QStringLiteral("PMX / MMD (later)"), QStringLiteral("PMX / MMD（后续）"));
            break;
        }
        target_combo_->setItemText(i, text);
    }
}

void AssetConverterWindow::chooseSource() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        trAsset(chinese_ui_, QStringLiteral("Import Source Asset"), QStringLiteral("导入源模型资源")),
        source_path_.isEmpty() ? QDir::homePath() : source_path_,
        trAsset(chinese_ui_,
                QStringLiteral("Model Files (*.fbx *.glb *.gltf *.vrm *.obj *.dae *.stl *.ply);;Complex / Later (*.pmx *.pmd);;All Files (*.*)"),
                QStringLiteral("模型文件 (*.fbx *.glb *.gltf *.vrm *.obj *.dae *.stl *.ply);;复杂格式 / 后续支持 (*.pmx *.pmd);;所有文件 (*.*)")));
    if (!path.isEmpty()) {
        loadSourcePath(path);
    }
}

void AssetConverterWindow::chooseOutputDirectory() {
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        trAsset(chinese_ui_, QStringLiteral("Choose Output Directory"), QStringLiteral("选择输出目录")),
        output_directory_.isEmpty() ? defaultOutputDirectory() : output_directory_);
    if (!directory.isEmpty()) {
        output_directory_ = QDir::toNativeSeparators(directory);
        output_dir_edit_->setText(output_directory_);
    }
}

void AssetConverterWindow::loadSourcePath(const QString& path) {
    QString error_message;
    SceneModel loaded = model_loader_.loadFromFile(path, &error_message);
    if (loaded.empty()) {
        QMessageBox::warning(this,
                             trAsset(chinese_ui_, QStringLiteral("Asset Converter"), QStringLiteral("资产转换器")),
                             error_message.isEmpty()
                                 ? trAsset(chinese_ui_,
                                           QStringLiteral("Failed to load source asset."),
                                           QStringLiteral("源模型资源加载失败。"))
                                 : error_message);
        return;
    }

    source_path_ = path;
    source_path_edit_->setText(QDir::toNativeSeparators(path));
    source_scene_ = std::move(loaded);
    converted_scene_ = SceneModel();
    last_output_model_path_.clear();
    last_status_.clear();
    last_backend_note_.clear();

    SkeletonExtractor extractor;
    QString skeleton_error;
    source_skeleton_ = extractor.loadFromFile(path, &skeleton_error);

    if (output_directory_.isEmpty()) {
        output_directory_ = defaultOutputDirectory();
        output_dir_edit_->setText(output_directory_);
    }

    before_viewport_->setScene(&source_scene_);
    before_viewport_->resetCamera();
    after_viewport_->setScene(nullptr);
    refreshSourceSummary();
    generateConversionPlan();
    status_label_->setText(trAsset(chinese_ui_,
                                  QStringLiteral("Loaded %1"),
                                  QStringLiteral("已加载：%1")).arg(QFileInfo(path).fileName()));
}

void AssetConverterWindow::refreshEmptyState() {
    inventory_view_->setMarkdown(trAsset(
        chinese_ui_,
        QStringLiteral("### Source Inventory\n\nImport an FBX, GLB, GLTF, VRM, OBJ, DAE, STL or PLY file to inspect it."),
        QStringLiteral("### 源资源概况\n\n导入 FBX、GLB、GLTF、VRM、OBJ、DAE、STL 或 PLY 文件后，这里会显示模型内容。")));
    plan_view_->setMarkdown(trAsset(
        chinese_ui_,
        QStringLiteral("### Conversion Plan\n\nNo plan generated yet."),
        QStringLiteral("### 转换计划\n\n还没有生成计划。")));
    report_view_->setMarkdown(trAsset(
        chinese_ui_,
        QStringLiteral("### Conversion Report\n\nNo conversion run yet."),
        QStringLiteral("### 转换报告\n\n还没有执行转换。")));
    execute_button_->setEnabled(false);
    report_button_->setEnabled(false);
}

void AssetConverterWindow::refreshSourceSummary() {
    inventory_view_->setMarkdown(buildInventoryText());
    execute_button_->setEnabled(!source_scene_.empty());
    report_button_->setEnabled(!source_scene_.empty());
}

void AssetConverterWindow::refreshTargetUi() {
    if (!source_path_.isEmpty()) {
        status_label_->setText(trAsset(chinese_ui_,
                                      QStringLiteral("Target: %1"),
                                      QStringLiteral("目标格式：%1")).arg(targetFormatName()));
    }
}

AssetConverterWindow::TargetFormat AssetConverterWindow::targetFormat() const {
    return static_cast<TargetFormat>(target_combo_->currentData().toInt());
}

QString AssetConverterWindow::targetFormatName() const {
    switch (targetFormat()) {
    case TargetFormat::Vrm:
        return QStringLiteral("VRM");
    case TargetFormat::Pmx:
        return QStringLiteral("PMX");
    case TargetFormat::Glb:
        return QStringLiteral("GLB");
    case TargetFormat::Gltf:
        return QStringLiteral("GLTF");
    case TargetFormat::Fbx:
        return QStringLiteral("FBX");
    case TargetFormat::Obj:
        return QStringLiteral("OBJ");
    case TargetFormat::Dae:
        return QStringLiteral("DAE");
    case TargetFormat::Stl:
        return QStringLiteral("STL");
    case TargetFormat::Ply:
        return QStringLiteral("PLY");
    default:
        return QStringLiteral("GLB");
    }
}

QString AssetConverterWindow::targetExtension() const {
    switch (targetFormat()) {
    case TargetFormat::Vrm:
        return QStringLiteral("vrm");
    case TargetFormat::Pmx:
        return QStringLiteral("pmx");
    case TargetFormat::Glb:
        return QStringLiteral("glb");
    case TargetFormat::Gltf:
        return QStringLiteral("gltf");
    case TargetFormat::Fbx:
        return QStringLiteral("fbx");
    case TargetFormat::Obj:
        return QStringLiteral("obj");
    case TargetFormat::Dae:
        return QStringLiteral("dae");
    case TargetFormat::Stl:
        return QStringLiteral("stl");
    case TargetFormat::Ply:
        return QStringLiteral("ply");
    default:
        return QStringLiteral("glb");
    }
}

QString AssetConverterWindow::defaultOutputDirectory() const {
    const QFileInfo info(source_path_);
    const QString base = info.exists() ? info.completeBaseName() : QStringLiteral("asset");
    return QDir::toNativeSeparators(QDir(QDir::currentPath()).filePath(QStringLiteral("Converted/%1_%2")
        .arg(base, targetFormatName().toLower())));
}

QString AssetConverterWindow::buildInventoryText() const {
    if (source_scene_.empty()) {
        return trAsset(chinese_ui_,
                       QStringLiteral("### Source Inventory\n\nNo asset loaded."),
                       QStringLiteral("### 源资源概况\n\n尚未加载模型资源。"));
    }

    const QFileInfo info(source_path_);
    QString text = chinese_ui_ ? QStringLiteral("### 源资源概况\n\n") : QStringLiteral("### Source Inventory\n\n");
    text += (chinese_ui_ ? QStringLiteral("- 文件：`%1`\n") : QStringLiteral("- File: `%1`\n"))
        .arg(QDir::toNativeSeparators(source_path_));
    text += (chinese_ui_ ? QStringLiteral("- 格式：`%1`\n") : QStringLiteral("- Format: `%1`\n"))
        .arg(info.suffix().toUpper());
    text += (chinese_ui_ ? QStringLiteral("- 网格：%1\n") : QStringLiteral("- Meshes: %1\n"))
        .arg(formatCount(source_scene_.meshes.size()));
    text += (chinese_ui_ ? QStringLiteral("- 顶点：%1\n") : QStringLiteral("- Vertices: %1\n"))
        .arg(formatCount(source_scene_.vertexCount()));
    text += (chinese_ui_ ? QStringLiteral("- 三角形：%1\n") : QStringLiteral("- Triangles: %1\n"))
        .arg(formatCount(source_scene_.triangleCount()));
    text += (chinese_ui_ ? QStringLiteral("- 场景节点：%1\n") : QStringLiteral("- Scene nodes: %1\n"))
        .arg(formatCount(source_scene_.nodes.size()));
    text += (chinese_ui_
        ? QStringLiteral("- 骨骼：%1，蒙皮骨骼：%2，已识别人形骨骼：%3\n")
        : QStringLiteral("- Skeleton bones: %1, skinned bones: %2, recognized humanoid bones: %3\n"))
        .arg(formatCount(source_skeleton_.bones.size()),
             formatCount(source_skeleton_.skinnedBoneCount()),
             formatCount(source_skeleton_.recognizedBoneCount()));
    text += (chinese_ui_ ? QStringLiteral("- 蒙皮网格：%1\n") : QStringLiteral("- Skinned meshes: %1\n"))
        .arg(formatCount(countSkinnedMeshes(source_scene_)));
    text += (chinese_ui_
        ? QStringLiteral("- 材质：%1，贴图：%2，材质标签：`%3`\n")
        : QStringLiteral("- Materials: %1, textures: %2, material tags: `%3`\n"))
        .arg(formatCount(source_scene_.meshes.size()),
             formatCount(countTextures(source_scene_)),
             materialFeatureTags(source_scene_).join(QStringLiteral(", ")));
    text += (chinese_ui_
        ? QStringLiteral("- Morph 目标：%1，VRM 表情：%2\n")
        : QStringLiteral("- Morph targets: %1, VRM expressions: %2\n"))
        .arg(formatCount(countMorphTargets(source_scene_)),
             formatCount(source_scene_.vrm_expressions.size()));
    text += (chinese_ui_ ? QStringLiteral("- 动画：%1\n") : QStringLiteral("- Animations: %1\n"))
        .arg(formatCount(source_scene_.animations.size()));
    if (!source_scene_.animations.empty()) {
        const AnimationClipData& clip = source_scene_.animations.front();
        text += (chinese_ui_
            ? QStringLiteral("- 第一个动画片段：`%1`，%2 秒，%3 个通道\n")
            : QStringLiteral("- First clip: `%1`, %2s, %3 channels\n"))
            .arg(QString::fromStdString(clip.name).isEmpty() ? QStringLiteral("Animation") : QString::fromStdString(clip.name))
            .arg(clip.durationSeconds(), 0, 'f', 2)
            .arg(formatCount(clip.channels.size()));
    }
    return text;
}

QString AssetConverterWindow::buildPlanText() const {
    if (source_scene_.empty()) {
        return trAsset(chinese_ui_,
                       QStringLiteral("### Conversion Plan\n\nImport a source asset first."),
                       QStringLiteral("### 转换计划\n\n请先导入源模型资源。"));
    }

    const QString target = targetFormatName();
    const QStringList risks = conversionRisks(source_scene_, source_skeleton_, suffixLower(source_path_), target, chinese_ui_);
    QStringList steps;
    if (chinese_ui_) {
        steps << QStringLiteral("整理源文件路径、缩放和坐标系元数据。")
              << QStringLiteral("统计网格、材质、骨骼、morph 和动画内容。")
              << QStringLiteral("为兼容人形角色的格式建立语义骨骼表。")
              << QStringLiteral("把源材质模型映射到目标格式的着色语义。")
              << QStringLiteral("尽量保留 morph / 表情名称，并标记目标格式不支持的通道。")
              << QStringLiteral("执行导出后重新加载结果，用左右视口做视觉对比。")
              << QStringLiteral("写出 JSON 转换计划和 Markdown 转换报告。");
    } else {
        steps << QStringLiteral("Normalize source path, scale and coordinate metadata.")
              << QStringLiteral("Inventory mesh, material, skeleton, morph and animation payloads.")
              << QStringLiteral("Build semantic bone table for humanoid-compatible formats.")
              << QStringLiteral("Map material model into target shading vocabulary.")
              << QStringLiteral("Preserve morph/expression names and flag unsupported target channels.")
              << QStringLiteral("Run export backend and reload the result for visual comparison.")
              << QStringLiteral("Write JSON plan and Markdown conversion report.");
    }

    if (target == QStringLiteral("VRM")) {
        steps << trAsset(chinese_ui_,
                         QStringLiteral("Export GLB in-process, then inject VRM 1.0 meta and humanoid mapping through the native writer."),
                         QStringLiteral("先在程序内导出 GLB，再通过原生 VRM writer 写入 VRM 1.0 元信息和人形骨骼映射。"));
    } else if (target == QStringLiteral("PMX")) {
        steps << trAsset(chinese_ui_,
                         QStringLiteral("MMD/PMX is intentionally deferred because it needs toon, morph, rigid body and physics-specific conversion."),
                         QStringLiteral("MMD/PMX 暂缓实现，因为它需要 toon、morph、刚体和物理等专门转换。"));
    } else {
        steps << trAsset(chinese_ui_,
                         QStringLiteral("Use the built-in Assimp native exporter for this simple model target."),
                         QStringLiteral("这个简单模型目标格式会优先使用内置 Assimp 原生导出器。"));
    }

    QString text = (chinese_ui_
        ? QStringLiteral("### AI 转换计划：`%1` -> `%2`\n\n")
        : QStringLiteral("### AI Conversion Plan: `%1` -> `%2`\n\n"))
        .arg(QFileInfo(source_path_).suffix().toUpper(), target);
    text += (chinese_ui_ ? QStringLiteral("#### 步骤\n\n") : QStringLiteral("#### Steps\n\n")) + markdownList(steps) + QStringLiteral("\n");
    text += (chinese_ui_ ? QStringLiteral("#### 风险 / 人工质检\n\n") : QStringLiteral("#### Risks / Manual QA\n\n")) + markdownList(risks) + QStringLiteral("\n");
    text += chinese_ui_ ? QStringLiteral("#### 后端\n\n") : QStringLiteral("#### Backend\n\n");
    if (target == QStringLiteral("PMX")) {
        text += trAsset(chinese_ui_,
                        QStringLiteral("- PMX/MMD is not handled in this simple-format phase. No native PMX writer is exposed yet.\n"),
                        QStringLiteral("- PMX/MMD 不在当前简单格式阶段处理，暂时没有暴露原生 PMX writer。\n"));
    } else if (target == QStringLiteral("VRM")) {
        text += trAsset(chinese_ui_,
                        QStringLiteral("- VRM export is attempted in-process through the native VRM 1.0 writer. MToon/expression write-back is still a later pass.\n"),
                        QStringLiteral("- VRM 会通过原生 VRM 1.0 writer 在程序内尝试导出；MToon / 表情完整写回仍属于后续阶段。\n"));
    } else {
        NativeModelConverter converter;
        text += converter.canExportExtension(targetExtension())
            ? trAsset(chinese_ui_,
                      QStringLiteral("- `%1` export is attempted in-process through Assimp Exporter.\n"),
                      QStringLiteral("- `%1` 会通过 Assimp Exporter 在程序内尝试导出。\n")).arg(target)
            : trAsset(chinese_ui_,
                      QStringLiteral("- `%1` export is not exposed by the current Assimp build; Execute will report the available exporters.\n"),
                      QStringLiteral("- 当前 Assimp 构建没有暴露 `%1` 导出；执行转换时会报告可用导出器。\n")).arg(target);
    }
    text += trAsset(chinese_ui_,
                    QStringLiteral("- Execute also writes fallback/debug files `haorender_asset_convert_blender.py`, `run_conversion.ps1`, and `backend_instructions.md` into the output directory.\n"),
                    QStringLiteral("- 执行转换时还会在输出目录写出 `haorender_asset_convert_blender.py`、`run_conversion.ps1` 和 `backend_instructions.md`，便于调试和兜底。\n"));
    return text;
}

QString AssetConverterWindow::buildReportText(const QString& status,
                                              const QString& output_model_path,
                                              const QString& backend_note) const {
    QString text = chinese_ui_
        ? QStringLiteral("# HAORENDER-AI 资产转换报告\n\n")
        : QStringLiteral("# HAORENDER-AI Asset Conversion Report\n\n");
    text += (chinese_ui_ ? QStringLiteral("- 源文件：`%1`\n") : QStringLiteral("- Source: `%1`\n"))
        .arg(QDir::toNativeSeparators(source_path_));
    text += (chinese_ui_ ? QStringLiteral("- 目标格式：`%1`\n") : QStringLiteral("- Target: `%1`\n"))
        .arg(targetFormatName());
    if (!output_model_path.isEmpty()) {
        text += (chinese_ui_ ? QStringLiteral("- 输出模型：`%1`\n") : QStringLiteral("- Output model: `%1`\n"))
            .arg(QDir::toNativeSeparators(output_model_path));
    }
    if (!status.isEmpty()) {
        text += (chinese_ui_ ? QStringLiteral("- 状态：`%1`\n") : QStringLiteral("- Status: `%1`\n"))
            .arg(status);
    }
    if (!backend_note.isEmpty()) {
        text += (chinese_ui_ ? QStringLiteral("- 后端备注：%1\n") : QStringLiteral("- Backend note: %1\n"))
            .arg(backend_note);
    }
    text += QStringLiteral("\n");
    text += buildInventoryText();
    text += QStringLiteral("\n\n");
    text += buildPlanText();
    return text;
}

QJsonObject AssetConverterWindow::buildPlanJson(const QString& status,
                                                const QString& output_model_path,
                                                const QString& backend_note) const {
    QJsonObject root;
    root["source"] = QDir::toNativeSeparators(source_path_);
    root["sourceFormat"] = QFileInfo(source_path_).suffix().toUpper();
    root["targetFormat"] = targetFormatName();
    root["outputModel"] = QDir::toNativeSeparators(output_model_path);
    root["status"] = status;
    root["backendNote"] = backend_note;
    root["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root["backendBridgeScript"] = QStringLiteral("haorender_asset_convert_blender.py");
    root["backendRunScript"] = QStringLiteral("run_conversion.ps1");
    root["backendInstructions"] = QStringLiteral("backend_instructions.md");

    QJsonObject inventory;
    inventory["meshes"] = static_cast<int>(source_scene_.meshes.size());
    inventory["vertices"] = static_cast<qint64>(source_scene_.vertexCount());
    inventory["triangles"] = static_cast<qint64>(source_scene_.triangleCount());
    inventory["nodes"] = static_cast<int>(source_scene_.nodes.size());
    inventory["skeletonBones"] = source_skeleton_.bones.size();
    inventory["skinnedBones"] = source_skeleton_.skinnedBoneCount();
    inventory["recognizedHumanoidBones"] = source_skeleton_.recognizedBoneCount();
    inventory["morphTargets"] = countMorphTargets(source_scene_);
    inventory["vrmExpressions"] = static_cast<int>(source_scene_.vrm_expressions.size());
    inventory["animations"] = static_cast<int>(source_scene_.animations.size());
    inventory["materialTags"] = QJsonArray::fromStringList(materialFeatureTags(source_scene_));
    root["inventory"] = inventory;

    root["risks"] = QJsonArray::fromStringList(conversionRisks(source_scene_, source_skeleton_, suffixLower(source_path_), targetFormatName(), chinese_ui_));
    return root;
}

void AssetConverterWindow::generateConversionPlan() {
    plan_view_->setMarkdown(buildPlanText());
    report_view_->setMarkdown(buildReportText(last_status_, last_output_model_path_, last_backend_note_));
}

void AssetConverterWindow::executeConversion() {
    if (source_scene_.empty() || source_path_.isEmpty()) {
        QMessageBox::information(this,
                                 trAsset(chinese_ui_, QStringLiteral("Asset Converter"), QStringLiteral("资产转换器")),
                                 trAsset(chinese_ui_,
                                         QStringLiteral("Import a source asset first."),
                                         QStringLiteral("请先导入源模型资源。")));
        return;
    }

    if (output_directory_.isEmpty()) {
        output_directory_ = defaultOutputDirectory();
        output_dir_edit_->setText(output_directory_);
    }
    QDir().mkpath(output_directory_);

    const QFileInfo source_info(source_path_);
    const QString output_model_path = QDir(output_directory_).filePath(
        QStringLiteral("%1.%2").arg(source_info.completeBaseName(), targetExtension()));
    QString backend_note;
    bool model_written = false;

    if (suffixLower(source_path_) == targetExtension()) {
        QFile::remove(output_model_path);
        model_written = QFile::copy(source_path_, output_model_path);
        backend_note = model_written
            ? trAsset(chinese_ui_,
                      QStringLiteral("Source format already matches target; copied the model as an identity export."),
                      QStringLiteral("源格式与目标格式一致，已按原样复制模型。"))
            : trAsset(chinese_ui_,
                      QStringLiteral("Failed to copy source model as identity export."),
                      QStringLiteral("按原样复制源模型失败。"));
    } else if (targetFormat() == TargetFormat::Vrm) {
        model_written = runNativeVrmConversion(output_model_path, &backend_note);
    } else if (targetFormat() == TargetFormat::Pmx) {
        backend_note = trAsset(chinese_ui_,
                               QStringLiteral("MMD/PMX is intentionally deferred in the simple-format conversion phase. Native PMX writer is not exposed yet."),
                               QStringLiteral("MMD/PMX 在当前简单格式转换阶段暂缓实现，尚未暴露原生 PMX writer。"));
    } else {
        model_written = runNativeSimpleModelConversion(output_model_path, &backend_note);
        if (!model_written && targetFormat() == TargetFormat::Glb) {
            QString blender_note;
            model_written = runBlenderGlbConversion(output_model_path, &blender_note);
            backend_note = QStringLiteral("%1 Fallback: %2").arg(backend_note, blender_note).trimmed();
        }
    }

    last_output_model_path_ = model_written ? output_model_path : QString();
    last_status_ = model_written ? QStringLiteral("model_written") : QStringLiteral("plan_ready_backend_required");
    last_backend_note_ = backend_note;
    writePlanAndReport(output_directory_, output_model_path, last_status_, last_backend_note_);

    if (model_written) {
        QString load_error;
        converted_scene_ = model_loader_.loadFromFile(output_model_path, &load_error);
        if (!converted_scene_.empty()) {
            after_viewport_->setScene(&converted_scene_);
            after_viewport_->resetCamera();
        }
    }

    report_view_->setMarkdown(buildReportText(last_status_, output_model_path, last_backend_note_));
    status_label_->setText(model_written
        ? trAsset(chinese_ui_,
                  QStringLiteral("Converted: %1"),
                  QStringLiteral("已转换：%1")).arg(QFileInfo(output_model_path).fileName())
        : trAsset(chinese_ui_,
                  QStringLiteral("Plan generated; backend required"),
                  QStringLiteral("已生成计划；需要后端补充")));
}

bool AssetConverterWindow::runNativeGlbConversion(const QString& output_model_path,
                                                  QString* backend_note) {
    NativeModelConverter converter;
    return converter.convert(source_path_, output_model_path, QStringLiteral("glb"), backend_note);
}

bool AssetConverterWindow::runNativeSimpleModelConversion(const QString& output_model_path,
                                                          QString* backend_note) {
    NativeModelConverter converter;
    return converter.convert(source_path_, output_model_path, targetExtension(), backend_note);
}

bool AssetConverterWindow::runNativeVrmConversion(const QString& output_model_path,
                                                  QString* backend_note) {
    VrmNativeWriter writer;
    QString status;
    const bool ok = writer.writeFromSource(source_path_, output_model_path, source_skeleton_, &status);
    if (backend_note) {
        *backend_note = ok
            ? status
            : trAsset(chinese_ui_,
                      QStringLiteral("%1 A developer fallback package was generated, but the product path should be the native VRM writer."),
                      QStringLiteral("%1 已生成开发者兜底包，但产品路径应该走原生 VRM writer。")).arg(status);
    }
    return ok;
}

bool AssetConverterWindow::runBlenderGlbConversion(const QString& output_model_path,
                                                   QString* backend_note) {
    const QString blender = findBlenderExecutable();
    if (blender.isEmpty()) {
        if (backend_note) {
            *backend_note = trAsset(chinese_ui_,
                                    QStringLiteral("Optional Blender fallback was not found. Native GLB export should be the normal product path; inspect the native export error above."),
                                    QStringLiteral("未找到可选 Blender 兜底路径。正常产品路径应使用原生 GLB 导出，请检查上面的原生导出错误。"));
        }
        return false;
    }

    const QString script_path = QDir(output_directory_).filePath(QStringLiteral("haorender_asset_convert_blender.py"));
    QFile script(script_path);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (backend_note) {
            *backend_note = trAsset(chinese_ui_,
                                    QStringLiteral("Failed to write Blender bridge script."),
                                    QStringLiteral("写入 Blender 桥接脚本失败。"));
        }
        return false;
    }
    QTextStream out(&script);
    out << buildBlenderBridgeScript();
    script.close();

    QProcess process;
    process.setProgram(blender);
    process.setArguments({
        QStringLiteral("--background"),
        QStringLiteral("--python"),
        script_path,
        QStringLiteral("--"),
        QStringLiteral("--input"),
        source_path_,
        QStringLiteral("--output"),
        output_model_path,
        QStringLiteral("--target"),
        QStringLiteral("GLB")
    });
    process.setWorkingDirectory(output_directory_);
    process.start();
    if (!process.waitForFinished(180000)) {
        process.kill();
        if (backend_note) {
            *backend_note = trAsset(chinese_ui_,
                                    QStringLiteral("Blender conversion timed out."),
                                    QStringLiteral("Blender 转换超时。"));
        }
        return false;
    }
    const QString stdout_text = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString stderr_text = QString::fromLocal8Bit(process.readAllStandardError());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !QFileInfo::exists(output_model_path)) {
        if (backend_note) {
            *backend_note = trAsset(chinese_ui_,
                                    QStringLiteral("Blender conversion failed. %1 %2"),
                                    QStringLiteral("Blender 转换失败。%1 %2")).arg(stdout_text, stderr_text).trimmed();
        }
        return false;
    }
    if (backend_note) {
        *backend_note = trAsset(chinese_ui_,
                                QStringLiteral("Blender GLB export completed."),
                                QStringLiteral("Blender GLB 导出完成。"));
    }
    return true;
}

void AssetConverterWindow::writePlanAndReport(const QString& output_directory,
                                              const QString& output_model_path,
                                              const QString& status,
                                              const QString& backend_note) const {
    QDir().mkpath(output_directory);
    const QString plan_path = QDir(output_directory).filePath(QStringLiteral("conversion_plan.json"));
    const QString report_path = QDir(output_directory).filePath(QStringLiteral("conversion_report.md"));

    QFile plan_file(plan_path);
    if (plan_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        plan_file.write(QJsonDocument(buildPlanJson(status, output_model_path, backend_note)).toJson(QJsonDocument::Indented));
    }

    QFile report_file(report_path);
    if (report_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&report_file);
        stream << buildReportText(status, output_model_path, backend_note);
    }

    writeBackendTaskPackage(output_directory, output_model_path, status, backend_note);
}

void AssetConverterWindow::writeBackendTaskPackage(const QString& output_directory,
                                                   const QString& output_model_path,
                                                   const QString& status,
                                                   const QString& backend_note) const {
    QDir().mkpath(output_directory);
    const QDir dir(output_directory);
    writeUtf8TextFile(dir.filePath(QStringLiteral("haorender_asset_convert_blender.py")),
                      buildBlenderBridgeScript());
    writeUtf8TextFile(dir.filePath(QStringLiteral("run_conversion.ps1")),
                      buildRunConversionScript(output_model_path));
    writeUtf8TextFile(dir.filePath(QStringLiteral("backend_instructions.md")),
                      buildBackendReadme(output_model_path, status, backend_note));

    QJsonObject state;
    state["source"] = QDir::toNativeSeparators(source_path_);
    state["sourceFormat"] = QFileInfo(source_path_).suffix().toUpper();
    state["targetFormat"] = targetFormatName();
    state["requestedOutputModel"] = QDir::toNativeSeparators(output_model_path);
    state["status"] = status;
    state["backendNote"] = backend_note;
    state["requiresBlender"] = false;
    state["optionalBlenderFallback"] = true;
    state["requiresMmdTools"] = false;
    state["requiresVrmExporter"] = false;
    state["nativeWriterAvailable"] = targetFormat() == TargetFormat::Glb || targetFormat() == TargetFormat::Vrm;
    state["nativeWriterPlanned"] = targetFormat() == TargetFormat::Pmx;
    state["fallbackRequiresMmdTools"] = targetFormat() == TargetFormat::Pmx;
    state["fallbackRequiresVrmExporter"] = targetFormat() == TargetFormat::Vrm;
    state["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    writeUtf8TextFile(dir.filePath(QStringLiteral("backend_status.json")),
                      QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Indented)));
}

QString AssetConverterWindow::buildBackendReadme(const QString& output_model_path,
                                                 const QString& status,
                                                 const QString& backend_note) const {
    const QString command = QStringLiteral(".\\run_conversion.ps1");
    QString text = QStringLiteral("# HAORENDER-AI Conversion Backend Package\n\n");
    text += QStringLiteral("This folder is a reproducible conversion task generated by the Asset Converter.\n\n");
    text += QStringLiteral("## Task\n\n");
    text += QStringLiteral("- Source: `%1`\n").arg(QDir::toNativeSeparators(source_path_));
    text += QStringLiteral("- Target format: `%1`\n").arg(targetFormatName());
    text += QStringLiteral("- Requested output model: `%1`\n").arg(QDir::toNativeSeparators(output_model_path));
    text += QStringLiteral("- Current status: `%1`\n").arg(status.isEmpty() ? QStringLiteral("not_run") : status);
    if (!backend_note.isEmpty()) {
        text += QStringLiteral("- Backend note: %1\n").arg(backend_note);
    }
    text += QStringLiteral("\n## Generated Files\n\n");
    text += QStringLiteral("- `conversion_plan.json`: machine-readable plan, inventory and risk list.\n");
    text += QStringLiteral("- `conversion_report.md`: human-readable QA and conversion report.\n");
    text += QStringLiteral("- `haorender_asset_convert_blender.py`: optional developer fallback bridge script.\n");
    text += QStringLiteral("- `run_conversion.ps1`: optional fallback runner that launches Blender with the correct source, target and output paths.\n");
    text += QStringLiteral("- `backend_status.json`: minimal backend requirements and last known status.\n\n");
    text += QStringLiteral("## Run\n\n```powershell\n%1\n```\n\n").arg(command);
    text += QStringLiteral("## Backend Requirements\n\n");
    text += QStringLiteral("- GLB: the app tries native Assimp export first. Blender is not a normal user requirement.\n");
    text += QStringLiteral("- PMX/MMD: the product direction is a native PMX writer. The Blender + `mmd_tools` path is only a temporary developer fallback until that lands.\n");
    text += QStringLiteral("- VRM: the app now tries native GLB plus VRM 1.0 metadata writing. VRM exporter add-ons remain a temporary developer fallback for incomplete cases.\n\n");
    text += QStringLiteral("## QA Policy\n\n");
    text += QStringLiteral("Treat the generated model as a candidate, not as a guaranteed lossless conversion. Reopen it in HAORENDER-AI, compare skeleton, material, morph and animation summaries, then update the report with any manual fixes.\n");
    return text;
}

QString AssetConverterWindow::buildBlenderBridgeScript() const {
    return QString::fromUtf8(R"PY(# Generated by HAORENDER-AI Asset Converter.
import argparse
import os
import sys
import traceback

import bpy


def log(message):
    print("[HAORENDER-AI] " + str(message), flush=True)


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def op_exists(path):
    current = bpy.ops
    for part in path.split("."):
        if not hasattr(current, part):
            return False
        current = getattr(current, part)
    return True


def call_operator(path, **kwargs):
    current = bpy.ops
    for part in path.split("."):
        current = getattr(current, part)
    return current(**kwargs)


def import_obj(filepath):
    if op_exists("wm.obj_import"):
        return bpy.ops.wm.obj_import(filepath=filepath)
    if op_exists("import_scene.obj"):
        return bpy.ops.import_scene.obj(filepath=filepath)
    raise RuntimeError("OBJ importer was not found in this Blender build.")


def import_mmd(filepath):
    candidates = [
        "mmd_tools.import_model",
        "mmd_tools.import_pmx",
        "mmd_tools.import_pmd",
    ]
    errors = []
    for candidate in candidates:
        if op_exists(candidate):
            try:
                return call_operator(candidate, filepath=filepath)
            except TypeError:
                try:
                    return call_operator(candidate, files=[{"name": os.path.basename(filepath)}], directory=os.path.dirname(filepath))
                except Exception as exc:
                    errors.append(f"{candidate}: {exc}")
            except Exception as exc:
                errors.append(f"{candidate}: {exc}")
    raise RuntimeError("PMX/PMD import requires the Blender mmd_tools add-on. " + "; ".join(errors))


def import_asset(filepath):
    ext = os.path.splitext(filepath)[1].lower()
    log(f"Importing {filepath}")
    if ext == ".fbx":
        return bpy.ops.import_scene.fbx(filepath=filepath, automatic_bone_orientation=False)
    if ext in (".glb", ".gltf", ".vrm"):
        return bpy.ops.import_scene.gltf(filepath=filepath)
    if ext == ".obj":
        return import_obj(filepath)
    if ext == ".dae":
        return bpy.ops.wm.collada_import(filepath=filepath)
    if ext in (".pmx", ".pmd"):
        return import_mmd(filepath)
    raise RuntimeError("Unsupported source format for Blender bridge: " + ext)


def export_glb(filepath):
    log(f"Exporting GLB {filepath}")
    return bpy.ops.export_scene.gltf(
        filepath=filepath,
        export_format="GLB",
        export_skins=True,
        export_morph=True,
        export_animations=True,
    )


def export_pmx(filepath):
    candidates = [
        "mmd_tools.export_model",
        "mmd_tools.export_pmx",
    ]
    errors = []
    for candidate in candidates:
        if op_exists(candidate):
            try:
                log(f"Exporting PMX through {candidate}: {filepath}")
                return call_operator(candidate, filepath=filepath)
            except TypeError:
                try:
                    return call_operator(candidate, filepath=filepath, scale=1.0)
                except Exception as exc:
                    errors.append(f"{candidate}: {exc}")
            except Exception as exc:
                errors.append(f"{candidate}: {exc}")
    raise RuntimeError("PMX export requires the Blender mmd_tools add-on. " + "; ".join(errors))


def export_vrm(filepath):
    candidates = [
        "export_scene.vrm",
        "vrm.export_vrm",
        "wm.vrm_export",
    ]
    errors = []
    for candidate in candidates:
        if op_exists(candidate):
            try:
                log(f"Exporting VRM through {candidate}: {filepath}")
                return call_operator(candidate, filepath=filepath)
            except Exception as exc:
                errors.append(f"{candidate}: {exc}")
    raise RuntimeError("VRM export requires a Blender VRM exporter add-on. " + "; ".join(errors))


def export_asset(filepath, target):
    os.makedirs(os.path.dirname(os.path.abspath(filepath)), exist_ok=True)
    target_upper = target.upper()
    if target_upper == "GLB":
        return export_glb(filepath)
    if target_upper == "PMX":
        return export_pmx(filepath)
    if target_upper == "VRM":
        return export_vrm(filepath)
    raise RuntimeError("Unsupported target format: " + target)


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--target", required=True, choices=["GLB", "PMX", "VRM", "glb", "pmx", "vrm"])
    args = parser.parse_args(argv)

    clear_scene()
    import_asset(args.input)
    export_asset(args.output, args.target)
    log("Conversion script finished.")


if __name__ == "__main__":
    try:
        main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:])
    except Exception:
        traceback.print_exc()
        sys.exit(1)
)PY");
}

QString AssetConverterWindow::buildRunConversionScript(const QString& output_model_path) const {
    const QString found_blender = findBlenderExecutable();
    const QString blender = found_blender.isEmpty()
        ? QStringLiteral("blender")
        : found_blender;
    QString text;
    text += QStringLiteral("$ErrorActionPreference = 'Stop'\n");
    text += QStringLiteral("$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path\n");
    text += QStringLiteral("$blender = %1\n").arg(powerShellSingleQuote(blender));
    text += QStringLiteral("$bridge = Join-Path $scriptDir 'haorender_asset_convert_blender.py'\n");
    text += QStringLiteral("$source = %1\n").arg(powerShellSingleQuote(source_path_));
    text += QStringLiteral("$output = %1\n").arg(powerShellSingleQuote(output_model_path));
    text += QStringLiteral("$target = %1\n").arg(powerShellSingleQuote(targetFormatName()));
    text += QStringLiteral("Write-Host \"HAORENDER-AI conversion: $source -> $output ($target)\"\n");
    text += QStringLiteral("& $blender --background --python $bridge -- --input $source --output $output --target $target\n");
    text += QStringLiteral("if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }\n");
    text += QStringLiteral("Write-Host 'HAORENDER-AI conversion finished.'\n");
    return text;
}

void AssetConverterWindow::saveReport() {
    if (source_scene_.empty()) {
        QMessageBox::information(this,
                                 trAsset(chinese_ui_, QStringLiteral("Asset Converter"), QStringLiteral("资产转换器")),
                                 trAsset(chinese_ui_,
                                         QStringLiteral("Import a source asset first."),
                                         QStringLiteral("请先导入源模型资源。")));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this,
        trAsset(chinese_ui_, QStringLiteral("Save Conversion Report"), QStringLiteral("保存转换报告")),
        QDir(defaultOutputDirectory()).filePath(QStringLiteral("conversion_report.md")),
        trAsset(chinese_ui_,
                QStringLiteral("Markdown (*.md);;All Files (*.*)"),
                QStringLiteral("Markdown (*.md);;所有文件 (*.*)")));
    if (path.isEmpty()) {
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this,
                             trAsset(chinese_ui_, QStringLiteral("Asset Converter"), QStringLiteral("资产转换器")),
                             trAsset(chinese_ui_,
                                     QStringLiteral("Failed to save report."),
                                     QStringLiteral("保存报告失败。")));
        return;
    }
    QTextStream stream(&file);
    stream << buildReportText(last_status_, last_output_model_path_, last_backend_note_);
    status_label_->setText(trAsset(chinese_ui_, QStringLiteral("Report saved"), QStringLiteral("报告已保存")));
}

QString AssetConverterWindow::findBlenderExecutable() const {
    const QString path_blender = QStandardPaths::findExecutable(QStringLiteral("blender"));
    if (!path_blender.isEmpty()) {
        return path_blender;
    }
    const QStringList candidates = {
        QStringLiteral("C:/Program Files/Blender Foundation/Blender 4.3/blender.exe"),
        QStringLiteral("C:/Program Files/Blender Foundation/Blender 4.2/blender.exe"),
        QStringLiteral("C:/Program Files/Blender Foundation/Blender 4.1/blender.exe"),
        QStringLiteral("C:/Program Files/Blender Foundation/Blender/blender.exe")
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

} // namespace haorendergi
