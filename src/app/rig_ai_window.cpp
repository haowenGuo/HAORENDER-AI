#include "app/rig_ai_window.h"

#include "app/expression_agent_tools.h"
#include "app/render_viewport.h"
#include "rigging/animation_retargeter.h"
#include "rigging/retarget_profile.h"
#include "rigging/retarget_quality.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace haorendergi {

namespace {

QString rigFileFilter() {
    return QStringLiteral("Rig / Animation Files (*.vrm *.vrma *.fbx *.bvh *.gltf *.glb *.dae *.pmx);;VRM / glTF (*.vrm *.gltf *.glb);;Animation Files (*.vrma *.fbx *.bvh *.glb);;All Files (*.*)");
}

bool isLikelyMotionFile(const QString& path) {
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QString lower_path = QDir::toNativeSeparators(path).toLower();
    const QString base = info.completeBaseName().toLower();
    return suffix == QStringLiteral("vrma") ||
           suffix == QStringLiteral("fbx") ||
           lower_path.contains(QStringLiteral("\\motions\\")) ||
           lower_path.contains(QStringLiteral("/motions/")) ||
           lower_path.contains(QStringLiteral("motionpack")) ||
           base == QStringLiteral("idle") ||
           base == QStringLiteral("wave") ||
           base == QStringLiteral("dance");
}

void selectPreferredItem(QComboBox* combo, const QStringList& preferred_endings) {
    if (!combo) {
        return;
    }
    for (const QString& ending : preferred_endings) {
        for (int i = 0; i < combo->count(); ++i) {
            const QString path = QDir::fromNativeSeparators(combo->itemData(i).toString()).toLower();
            if (!path.isEmpty() && path.endsWith(QDir::fromNativeSeparators(ending).toLower())) {
                combo->setCurrentIndex(i);
                return;
            }
        }
    }
}

QTableWidgetItem* makeReadOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QTableWidgetItem* makeConfidenceItem(float confidence) {
    auto* item = makeReadOnlyItem(QString::number(confidence, 'f', 2));
    if (confidence >= 0.88f) {
        item->setData(Qt::ForegroundRole, QColor(98, 220, 150));
    } else if (confidence >= 0.72f) {
        item->setData(Qt::ForegroundRole, QColor(245, 190, 82));
    } else {
        item->setData(Qt::ForegroundRole, QColor(240, 120, 120));
    }
    return item;
}

void clearLayoutWidgets(QVBoxLayout* layout) {
    if (!layout) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            delete widget;
        } else if (QLayout* child_layout = item->layout()) {
            while (QLayoutItem* child = child_layout->takeAt(0)) {
                if (QWidget* child_widget = child->widget()) {
                    delete child_widget;
                }
                delete child;
            }
            delete child_layout;
        }
        delete item;
    }
}

QSlider* addGazeSliderRow(QVBoxLayout* layout,
                          QWidget* parent,
                          const QString& label_text,
                          int minimum,
                          int maximum,
                          int value,
                          QLabel** value_label) {
    auto* row_widget = new QWidget(parent);
    auto* row = new QHBoxLayout(row_widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    auto* label = new QLabel(label_text, row_widget);
    label->setProperty("panelSubtitle", "true");
    label->setMinimumWidth(84);
    auto* slider = new QSlider(Qt::Horizontal, row_widget);
    slider->setRange(minimum, maximum);
    slider->setValue(value);
    *value_label = new QLabel(QString::number(value), row_widget);
    (*value_label)->setProperty("valueText", "primary");
    (*value_label)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    (*value_label)->setMinimumWidth(42);
    row->addWidget(label);
    row->addWidget(slider, 1);
    row->addWidget(*value_label);
    layout->addWidget(row_widget);
    return slider;
}

} // namespace

RigAiWindow::RigAiWindow(QWidget* parent, bool embedded)
    : QDialog(parent),
      embedded_(embedded) {
    setObjectName(QStringLiteral("WorkspaceRoot"));
    if (embedded_) {
        setWindowFlags(Qt::Widget);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(0, 0);
    } else {
        setWindowFlags(windowFlags() | Qt::Window);
        setWindowModality(Qt::NonModal);
        resize(1280, 860);
        setMinimumSize(1040, 680);
    }

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(12);

    title_label_ = new QLabel(this);
    title_label_->setProperty("panelTitle", "true");
    subtitle_label_ = new QLabel(this);
    subtitle_label_->setProperty("panelSubtitle", "true");
    subtitle_label_->setWordWrap(true);
    root->addWidget(title_label_);
    root->addWidget(subtitle_label_);

    auto* body_widget = new QWidget(this);
    auto* body_layout = new QHBoxLayout(body_widget);
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(12);

    auto* left_scroll = new QScrollArea(body_widget);
    left_scroll->setWidgetResizable(true);
    left_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    left_scroll->setFrameShape(QFrame::NoFrame);
    left_scroll->setMinimumWidth(360);
    left_scroll->setMaximumWidth(640);
    left_scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto* left_panel = new QWidget(left_scroll);
    left_panel->setMinimumWidth(0);
    left_panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* left_layout = new QVBoxLayout(left_panel);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(10);
    left_scroll->setWidget(left_panel);

    auto* right_panel = new QWidget(body_widget);
    right_panel->setMinimumWidth(360);
    right_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* right_layout = new QVBoxLayout(right_panel);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(10);

    body_layout->addWidget(left_scroll, 0);
    body_layout->addWidget(right_panel, 1);
    root->addWidget(body_widget, 1);

    auto* io_frame = new QFrame(this);
    io_frame->setProperty("panelSection", "true");
    io_frame->setMaximumHeight(250);
    auto* io_layout = new QVBoxLayout(io_frame);
    io_layout->setContentsMargins(12, 12, 12, 12);
    io_layout->setSpacing(8);

    target_label_ = new QLabel(io_frame);
    target_label_->setProperty("fieldLabel", "true");
    target_label_->setMinimumWidth(140);
    target_path_edit_ = new QLineEdit(io_frame);
    target_path_edit_->setReadOnly(true);
    target_path_edit_->setMinimumWidth(0);
    target_path_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    target_button_ = new QPushButton(io_frame);
    target_button_->setObjectName(QStringLiteral("ActionButton"));
    target_button_->setMinimumWidth(112);
    auto* target_row = new QHBoxLayout();
    target_row->setSpacing(10);
    target_row->addWidget(target_label_);
    target_row->addWidget(target_button_);
    target_row->addWidget(target_path_edit_, 1);
    io_layout->addLayout(target_row);

    target_summary_label_ = new QLabel(io_frame);
    target_summary_label_->setProperty("fieldLabel", "true");
    target_summary_label_->setWordWrap(true);
    target_summary_label_->setContentsMargins(150, 0, 0, 4);
    io_layout->addWidget(target_summary_label_);

    source_label_ = new QLabel(io_frame);
    source_label_->setProperty("fieldLabel", "true");
    source_label_->setMinimumWidth(140);
    source_path_edit_ = new QLineEdit(io_frame);
    source_path_edit_->setReadOnly(true);
    source_path_edit_->setMinimumWidth(0);
    source_path_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    source_button_ = new QPushButton(io_frame);
    source_button_->setObjectName(QStringLiteral("ActionButton"));
    source_button_->setMinimumWidth(112);
    auto* source_row = new QHBoxLayout();
    source_row->setSpacing(10);
    source_row->addWidget(source_label_);
    source_row->addWidget(source_button_);
    source_row->addWidget(source_path_edit_, 1);
    io_layout->addLayout(source_row);

    source_summary_label_ = new QLabel(io_frame);
    source_summary_label_->setProperty("fieldLabel", "true");
    source_summary_label_->setWordWrap(true);
    source_summary_label_->setContentsMargins(150, 0, 0, 0);
    io_layout->addWidget(source_summary_label_);
    left_layout->addWidget(io_frame);

    auto* aigril_frame = new QFrame(this);
    aigril_frame->setProperty("panelSection", "true");
    aigril_frame->setMaximumHeight(250);
    auto* aigril_layout = new QVBoxLayout(aigril_frame);
    aigril_layout->setContentsMargins(12, 12, 12, 12);
    aigril_layout->setSpacing(8);

    auto* aigril_header_row = new QHBoxLayout();
    aigril_header_row->setSpacing(8);
    aigril_library_label_ = new QLabel(aigril_frame);
    aigril_library_label_->setProperty("fieldLabel", "true");
    aigril_scan_button_ = new QPushButton(aigril_frame);
    aigril_scan_button_->setObjectName(QStringLiteral("ActionButton"));
    aigril_header_row->addWidget(aigril_library_label_);
    aigril_header_row->addWidget(aigril_scan_button_);
    aigril_header_row->addStretch(1);
    aigril_layout->addLayout(aigril_header_row);

    auto* aigril_target_row = new QHBoxLayout();
    aigril_target_row->setSpacing(8);
    aigril_target_combo_ = new QComboBox(aigril_frame);
    aigril_target_combo_->setMinimumWidth(0);
    aigril_target_combo_->setMinimumContentsLength(18);
    aigril_target_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    aigril_target_combo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    aigril_load_target_button_ = new QPushButton(aigril_frame);
    aigril_load_target_button_->setObjectName(QStringLiteral("ActionButton"));
    aigril_target_row->addWidget(aigril_load_target_button_);
    aigril_target_row->addWidget(aigril_target_combo_, 1);
    aigril_layout->addLayout(aigril_target_row);

    auto* aigril_source_row = new QHBoxLayout();
    aigril_source_row->setSpacing(8);
    aigril_source_combo_ = new QComboBox(aigril_frame);
    aigril_source_combo_->setMinimumWidth(0);
    aigril_source_combo_->setMinimumContentsLength(18);
    aigril_source_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    aigril_source_combo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    aigril_load_source_button_ = new QPushButton(aigril_frame);
    aigril_load_source_button_->setObjectName(QStringLiteral("ActionButton"));
    aigril_source_row->addWidget(aigril_load_source_button_);
    aigril_source_row->addWidget(aigril_source_combo_, 1);
    aigril_layout->addLayout(aigril_source_row);
    left_layout->addWidget(aigril_frame);

    auto* control_row = new QHBoxLayout();
    control_row->setSpacing(8);
    generate_button_ = new QPushButton(this);
    generate_button_->setObjectName(QStringLiteral("AiSendButton"));
    export_button_ = new QPushButton(this);
    export_button_->setObjectName(QStringLiteral("ActionButton"));
    export_button_->setEnabled(false);
    control_row->addWidget(generate_button_);
    control_row->addWidget(export_button_);
    control_row->addStretch(1);
    mapping_summary_label_ = new QLabel(this);
    mapping_summary_label_->setProperty("panelSubtitle", "true");
    control_row->addWidget(mapping_summary_label_);
    left_layout->addLayout(control_row);

    auto* preview_frame = new QFrame(this);
    preview_frame->setProperty("panelSection", "true");
    auto* preview_layout = new QVBoxLayout(preview_frame);
    preview_layout->setContentsMargins(12, 12, 12, 12);
    preview_layout->setSpacing(8);

    auto* preview_header = new QHBoxLayout();
    preview_header->setSpacing(8);
    preview_title_label_ = new QLabel(preview_frame);
    preview_title_label_->setProperty("panelTitle", "true");
    preview_status_label_ = new QLabel(preview_frame);
    preview_status_label_->setProperty("panelSubtitle", "true");
    preview_status_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    preview_status_label_->setWordWrap(false);
    play_pause_button_ = new QPushButton(preview_frame);
    play_pause_button_->setObjectName(QStringLiteral("ActionButton"));
    play_pause_button_->setEnabled(false);
    restart_animation_button_ = new QPushButton(preview_frame);
    restart_animation_button_->setObjectName(QStringLiteral("ActionButton"));
    restart_animation_button_->setEnabled(false);
    preview_header->addWidget(preview_title_label_);
    preview_header->addStretch(1);
    preview_header->addWidget(play_pause_button_);
    preview_header->addWidget(restart_animation_button_);
    preview_header->addWidget(preview_status_label_);
    preview_layout->addLayout(preview_header);

    preview_viewport_ = new RenderViewport(preview_frame);
    preview_viewport_->setMinimumHeight(300);
    preview_viewport_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    preview_layout->addWidget(preview_viewport_, 1);
    right_layout->addWidget(preview_frame, 3);

    agent_view_ = new QTextBrowser(this);
    agent_view_->setObjectName(QStringLiteral("AiChatView"));
    agent_view_->setReadOnly(true);
    agent_view_->setLineWrapMode(QTextEdit::WidgetWidth);
    agent_view_->setMinimumHeight(110);
    agent_view_->setMaximumHeight(150);
    agent_view_->document()->setDocumentMargin(10);
    left_layout->addWidget(agent_view_, 1);

    eye_expression_section_ = new QFrame(this);
    eye_expression_section_->setProperty("panelSection", "true");
    auto* eye_layout = new QVBoxLayout(eye_expression_section_);
    eye_layout->setContentsMargins(12, 12, 12, 12);
    eye_layout->setSpacing(8);
    auto* eye_header = new QHBoxLayout();
    eye_expression_title_label_ = new QLabel(QStringLiteral("Eye / Expression Debug"), eye_expression_section_);
    eye_expression_title_label_->setProperty("panelTitle", "true");
    eye_expression_empty_label_ = new QLabel(eye_expression_section_);
    eye_expression_empty_label_->setProperty("panelSubtitle", "true");
    eye_expression_empty_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    eye_header->addWidget(eye_expression_title_label_);
    eye_header->addStretch(1);
    eye_header->addWidget(eye_expression_empty_label_);
    eye_layout->addLayout(eye_header);
    auto* eye_button_row = new QHBoxLayout();
    eye_expression_reset_button_ = new QPushButton(eye_expression_section_);
    eye_expression_reset_button_->setObjectName(QStringLiteral("ActionButton"));
    eye_expression_blink_curve_button_ = new QPushButton(eye_expression_section_);
    eye_expression_blink_curve_button_->setObjectName(QStringLiteral("ActionButton"));
    eye_button_row->addWidget(eye_expression_reset_button_);
    eye_button_row->addWidget(eye_expression_blink_curve_button_);
    eye_button_row->addStretch(1);
    eye_layout->addLayout(eye_button_row);
    eye_gaze_yaw_slider_ = addGazeSliderRow(eye_layout, eye_expression_section_, QStringLiteral("gaze yaw"), -30, 30, 0, &eye_gaze_yaw_value_label_);
    eye_gaze_pitch_slider_ = addGazeSliderRow(eye_layout, eye_expression_section_, QStringLiteral("gaze pitch"), -20, 20, 0, &eye_gaze_pitch_value_label_);
    eye_gaze_weight_slider_ = addGazeSliderRow(eye_layout, eye_expression_section_, QStringLiteral("gaze weight"), 0, 100, 100, &eye_gaze_weight_value_label_);
    if (eye_gaze_weight_value_label_) {
        eye_gaze_weight_value_label_->setText(QStringLiteral("1.00"));
    }
    eye_expression_controls_layout_ = new QVBoxLayout();
    eye_expression_controls_layout_->setContentsMargins(0, 0, 0, 0);
    eye_expression_controls_layout_->setSpacing(6);
    eye_layout->addLayout(eye_expression_controls_layout_);
    left_layout->addWidget(eye_expression_section_, 0);

    mapping_table_ = new QTableWidget(this);
    mapping_table_->setColumnCount(5);
    mapping_table_->horizontalHeader()->setStretchLastSection(true);
    mapping_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    mapping_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mapping_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    mapping_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    mapping_table_->verticalHeader()->setVisible(false);
    mapping_table_->setAlternatingRowColors(true);
    mapping_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    mapping_table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    right_layout->addWidget(mapping_table_, 2);

    configurePreviewViewport();
    refreshLanguage();
    connect(target_button_, &QPushButton::clicked, this, [this]() { chooseTargetSkeleton(); });
    connect(source_button_, &QPushButton::clicked, this, [this]() { chooseSourceAnimation(); });
    connect(aigril_scan_button_, &QPushButton::clicked, this, [this]() { scanAigrilAssets(); });
    connect(aigril_load_target_button_, &QPushButton::clicked, this, [this]() { loadSelectedAigrilTarget(); });
    connect(aigril_load_source_button_, &QPushButton::clicked, this, [this]() { loadSelectedAigrilSource(); });
    connect(generate_button_, &QPushButton::clicked, this, [this]() { generateMapping(); });
    connect(export_button_, &QPushButton::clicked, this, [this]() { exportMapping(); });
    connect(play_pause_button_, &QPushButton::clicked, this, [this]() { toggleAnimationPlayback(); });
    connect(restart_animation_button_, &QPushButton::clicked, this, [this]() { restartAnimationPlayback(); });
    connect(eye_expression_reset_button_, &QPushButton::clicked, this, [this]() { resetEyeExpressionControls(); });
    connect(eye_expression_blink_curve_button_, &QPushButton::clicked, this, [this]() { applySoftBlinkExpressionCurve(); });
    connect(eye_gaze_yaw_slider_, &QSlider::valueChanged, this, [this](int value) {
        if (eye_gaze_yaw_value_label_) {
            eye_gaze_yaw_value_label_->setText(QString::number(value));
        }
        applyEyeGazeControl();
    });
    connect(eye_gaze_pitch_slider_, &QSlider::valueChanged, this, [this](int value) {
        if (eye_gaze_pitch_value_label_) {
            eye_gaze_pitch_value_label_->setText(QString::number(value));
        }
        applyEyeGazeControl();
    });
    connect(eye_gaze_weight_slider_, &QSlider::valueChanged, this, [this](int value) {
        if (eye_gaze_weight_value_label_) {
            eye_gaze_weight_value_label_->setText(QString::number(static_cast<double>(value) / 100.0, 'f', 2));
        }
        applyEyeGazeControl();
    });
    scanAigrilAssets();
}

void RigAiWindow::setChineseUi(bool enabled) {
    if (chinese_ui_ == enabled) {
        refreshLanguage();
        return;
    }
    chinese_ui_ = enabled;
    refreshLanguage();
}

QString RigAiWindow::trText(const QString& english) const {
    if (!chinese_ui_) {
        return english;
    }

    struct Pair {
        const char* english;
        const char* chinese;
    };
    static const Pair pairs[] = {
        { "HaoRig AI - Skeleton Mapping", u8"HaoRig AI - 骨骼映射" },
        { "HaoRig AI", u8"HaoRig AI" },
        { "AI-assisted humanoid bone mapping for VRM / FBX / BVH / glTF animation retargeting.", u8"面向 VRM / FBX / BVH / glTF 动画重定向的 AI 人形骨骼映射。" },
        { "Target character", u8"目标角色" },
        { "Source animation", u8"源动画" },
        { "Load Target", u8"加载目标" },
        { "Load Source", u8"加载源动画" },
        { "AIGRIL asset library", u8"AIGRIL 资源库" },
        { "Scan AIGRIL", u8"扫描 AIGRIL" },
        { "Use Character", u8"使用角色" },
        { "Use Motion", u8"使用动作" },
        { "Rig Preview", u8"绑定预览" },
        { "Play", u8"播放" },
        { "Pause", u8"暂停" },
        { "Restart", u8"重播" },
        { "Eye / Expression Debug", u8"眼部 / 表情调试" },
        { "No VRM eye expressions found.", u8"未找到 VRM 眼部表情。" },
        { "Reset Expressions", u8"重置表情" },
        { "Soft Blink Curve", u8"柔和眨眼曲线" },
        { "Expression controls reset.", u8"表情控制已重置。" },
        { "Soft blink expression curve is playing.", u8"正在播放柔和眨眼表情曲线。" },
        { "Load a character to preview it here.", u8"加载角色后会在这里预览。" },
        { "Target character preview loaded.", u8"目标角色预览已加载。" },
        { "Retarget animation preview is playing.", u8"重定向动画预览正在播放。" },
        { "Load a VRM / GLB / FBX character skeleton", u8"加载 VRM / GLB / FBX 角色骨架" },
        { "Load an FBX / BVH / GLB animation skeleton", u8"加载 FBX / BVH / GLB 动画骨架" },
        { "No AIGRIL character found.", u8"未找到 AIGRIL 角色。" },
        { "No AIGRIL motion found.", u8"未找到 AIGRIL 动作。" },
        { "No target skeleton loaded.", u8"未加载目标骨架。" },
        { "No source animation skeleton loaded.", u8"未加载源动画骨架。" },
        { "No skeleton loaded.", u8"未加载骨架。" },
        { "Generate AI Mapping", u8"生成 AI 映射" },
        { "Export haorigmap.json", u8"导出 haorigmap.json" },
        { "Load both skeletons to generate a mapping.", u8"加载目标骨架和源动画后生成映射。" },
        { "Ready to generate AI skeleton mapping.", u8"已准备好生成 AI 骨骼映射。" },
        { "Source Bone", u8"源骨骼" },
        { "Canonical", u8"标准骨骼" },
        { "Target Bone", u8"目标骨骼" },
        { "Confidence", u8"置信度" },
        { "Reason", u8"原因" },
        { "HaoRig Agent", u8"HaoRig Agent" },
        { "Target", u8"目标" },
        { "Source", u8"源" },
        { "Load both files, generate mapping, review low-confidence rows, then export haorigmap.json.", u8"加载两个文件，生成映射，检查低置信度行，然后导出 haorigmap.json。" },
        { "Load Target Character Skeleton", u8"加载目标角色骨架" },
        { "Load Source Animation Skeleton", u8"加载源动画骨架" },
        { "No skeleton found.", u8"没有找到骨架。" },
        { "Load both a target character and a source animation first.", u8"请先加载目标角色和源动画。" },
        { "Generate a mapping before export.", u8"请先生成映射再导出。" },
        { "Export Skeleton Mapping", u8"导出骨骼映射" },
        { "HaoRig Mapping (*.haorigmap.json);;JSON (*.json);;All Files (*.*)", u8"HaoRig 映射 (*.haorigmap.json);;JSON (*.json);;所有文件 (*.*)" },
        { "Rig / Animation Files (*.vrm *.vrma *.fbx *.bvh *.gltf *.glb *.dae *.pmx);;VRM / glTF (*.vrm *.gltf *.glb);;Animation Files (*.vrma *.fbx *.bvh *.glb);;All Files (*.*)", u8"绑定 / 动画文件 (*.vrm *.vrma *.fbx *.bvh *.gltf *.glb *.dae *.pmx);;VRM / glTF (*.vrm *.gltf *.glb);;动画文件 (*.vrma *.fbx *.bvh *.glb);;所有文件 (*.*)" }
    };
    for (const Pair& pair : pairs) {
        if (english == QString::fromUtf8(pair.english)) {
            return QString::fromUtf8(pair.chinese);
        }
    }
    return english;
}

QString RigAiWindow::skeletonSummaryText(const SkeletonGraph& skeleton) const {
    if (skeleton.empty()) {
        return trText(QStringLiteral("No skeleton loaded."));
    }
    if (chinese_ui_) {
        return QStringLiteral("%1 个节点，%2 个蒙皮骨骼，%3 个已识别人形骨骼，%4 个动画片段")
            .arg(skeleton.bones.size())
            .arg(skeleton.skinnedBoneCount())
            .arg(skeleton.recognizedBoneCount())
            .arg(skeleton.animations.size());
    }
    return QStringLiteral("%1 nodes, %2 skinned bones, %3 recognized humanoid bones, %4 animation clips")
        .arg(skeleton.bones.size())
        .arg(skeleton.skinnedBoneCount())
        .arg(skeleton.recognizedBoneCount())
        .arg(skeleton.animations.size());
}

void RigAiWindow::refreshLanguage() {
    setWindowTitle(trText(QStringLiteral("HaoRig AI - Skeleton Mapping")));
    if (title_label_) {
        title_label_->setText(trText(QStringLiteral("HaoRig AI")));
    }
    if (subtitle_label_) {
        subtitle_label_->setText(trText(QStringLiteral("AI-assisted humanoid bone mapping for VRM / FBX / BVH / glTF animation retargeting.")));
    }
    if (target_label_) {
        target_label_->setText(trText(QStringLiteral("Target character")));
    }
    if (source_label_) {
        source_label_->setText(trText(QStringLiteral("Source animation")));
    }
    if (target_button_) {
        target_button_->setText(trText(QStringLiteral("Load Target")));
    }
    if (source_button_) {
        source_button_->setText(trText(QStringLiteral("Load Source")));
    }
    if (aigril_library_label_) {
        aigril_library_label_->setText(trText(QStringLiteral("AIGRIL asset library")));
    }
    if (aigril_scan_button_) {
        aigril_scan_button_->setText(trText(QStringLiteral("Scan AIGRIL")));
    }
    if (aigril_load_target_button_) {
        aigril_load_target_button_->setText(trText(QStringLiteral("Use Character")));
    }
    if (aigril_load_source_button_) {
        aigril_load_source_button_->setText(trText(QStringLiteral("Use Motion")));
    }
    if (target_path_edit_) {
        target_path_edit_->setPlaceholderText(trText(QStringLiteral("Load a VRM / GLB / FBX character skeleton")));
    }
    if (source_path_edit_) {
        source_path_edit_->setPlaceholderText(trText(QStringLiteral("Load an FBX / BVH / GLB animation skeleton")));
    }
    if (preview_title_label_) {
        preview_title_label_->setText(trText(QStringLiteral("Rig Preview")));
    }
    if (eye_expression_title_label_) {
        eye_expression_title_label_->setText(trText(QStringLiteral("Eye / Expression Debug")));
    }
    if (eye_expression_reset_button_) {
        eye_expression_reset_button_->setText(trText(QStringLiteral("Reset Expressions")));
    }
    if (eye_expression_blink_curve_button_) {
        eye_expression_blink_curve_button_->setText(trText(QStringLiteral("Soft Blink Curve")));
    }
    if (preview_status_label_ && preview_status_label_->text().isEmpty()) {
        preview_status_label_->setText(trText(QStringLiteral("Load a character to preview it here.")));
    }
    refreshPlaybackControls();
    if (target_summary_label_) {
        target_summary_label_->setText(target_skeleton_.empty()
            ? trText(QStringLiteral("No target skeleton loaded."))
            : skeletonSummaryText(target_skeleton_));
    }
    if (source_summary_label_) {
        source_summary_label_->setText(source_skeleton_.empty()
            ? trText(QStringLiteral("No source animation skeleton loaded."))
            : skeletonSummaryText(source_skeleton_));
    }
    if (generate_button_) {
        generate_button_->setText(trText(QStringLiteral("Generate AI Mapping")));
    }
    if (export_button_) {
        export_button_->setText(trText(QStringLiteral("Export haorigmap.json")));
    }
    if (mapping_table_) {
        mapping_table_->setHorizontalHeaderLabels({
            trText(QStringLiteral("Source Bone")),
            trText(QStringLiteral("Canonical")),
            trText(QStringLiteral("Target Bone")),
            trText(QStringLiteral("Confidence")),
            trText(QStringLiteral("Reason"))
        });
    }
    refreshSummary();
    if (agent_view_ && target_skeleton_.empty() && source_skeleton_.empty() && mapping_result_.mappings.isEmpty()) {
        agent_view_->clear();
        appendAgentMessage(trText(QStringLiteral("HaoRig Agent")),
                           trText(QStringLiteral("Load both files, generate mapping, review low-confidence rows, then export haorigmap.json.")));
    }
    refreshEyeExpressionPanel();
}

void RigAiWindow::chooseTargetSkeleton() {
    const QString path = QFileDialog::getOpenFileName(this,
                                                      trText(QStringLiteral("Load Target Character Skeleton")),
                                                      target_path_edit_->text().isEmpty() ? QDir::currentPath() : target_path_edit_->text(),
                                                      trText(rigFileFilter()));
    if (path.isEmpty()) {
        return;
    }
    loadTargetSkeletonPath(path, true);
}

void RigAiWindow::chooseSourceAnimation() {
    const QString path = QFileDialog::getOpenFileName(this,
                                                      trText(QStringLiteral("Load Source Animation Skeleton")),
                                                      source_path_edit_->text().isEmpty() ? QDir::currentPath() : source_path_edit_->text(),
                                                      trText(rigFileFilter()));
    if (path.isEmpty()) {
        return;
    }
    loadSourceAnimationPath(path);
}

QString RigAiWindow::defaultAigrilRoot() const {
    const QStringList candidates = {
        QStringLiteral("F:/AIGril/Resources"),
        QStringLiteral("F:/AIGril_main_release/Resources"),
        QStringLiteral("F:/AIGril_render_deploy/Resources")
    };
    for (const QString& candidate : candidates) {
        if (QDir(candidate).exists()) {
            return candidate;
        }
    }
    return QStringLiteral("F:/AIGril/Resources");
}

void RigAiWindow::scanAigrilAssets() {
    if (!aigril_target_combo_ || !aigril_source_combo_) {
        return;
    }

    const QString root = defaultAigrilRoot();
    aigril_target_combo_->clear();
    aigril_source_combo_->clear();
    if (!QDir(root).exists()) {
        aigril_target_combo_->addItem(trText(QStringLiteral("No AIGRIL character found.")), QString());
        aigril_source_combo_->addItem(trText(QStringLiteral("No AIGRIL motion found.")), QString());
        return;
    }

    QDirIterator iterator(root,
                          QStringList() << QStringLiteral("*.vrm") << QStringLiteral("*.vrma")
                                        << QStringLiteral("*.fbx") << QStringLiteral("*.glb")
                                        << QStringLiteral("*.gltf"),
                          QDir::Files,
                          QDirIterator::Subdirectories);
    QStringList targets;
    QStringList sources;
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (isLikelyMotionFile(path)) {
            sources << path;
        } else if (suffix == QStringLiteral("vrm") ||
                   suffix == QStringLiteral("glb") ||
                   suffix == QStringLiteral("gltf")) {
            targets << path;
        }
    }
    targets.sort(Qt::CaseInsensitive);
    sources.sort(Qt::CaseInsensitive);

    for (const QString& path : targets) {
        aigril_target_combo_->addItem(QDir(root).relativeFilePath(path), path);
    }
    for (const QString& path : sources) {
        aigril_source_combo_->addItem(QDir(root).relativeFilePath(path), path);
    }
    if (aigril_target_combo_->count() == 0) {
        aigril_target_combo_->addItem(trText(QStringLiteral("No AIGRIL character found.")), QString());
    }
    if (aigril_source_combo_->count() == 0) {
        aigril_source_combo_->addItem(trText(QStringLiteral("No AIGRIL motion found.")), QString());
    }
    selectPreferredItem(aigril_target_combo_,
                        QStringList() << QStringLiteral("F:/AIGril/Resources/AiGril.glb")
                                      << QStringLiteral("F:/AIGril/Resources/AiGril.vrm")
                                      << QStringLiteral("F:/AIGril/Resources/AiGril_18.vrm")
                                      << QStringLiteral("F:/AIGril/Resources/AiGril-18.vrm"));
    selectPreferredItem(aigril_source_combo_,
                        QStringList() << QStringLiteral("F:/AIGril/Resources/VRMA_MotionPack/vrma/VRMA_17.vrma")
                                      << QStringLiteral("F:/AIGril/Resources/VRMA_MotionPack/vrma/Idle.vrma")
                                      << QStringLiteral("F:/AIGril/Resources/fbx/VRMA/Idle.vrma")
                                      << QStringLiteral("F:/AIGril/Resources/idle.glb"));

    appendAgentMessage(trText(QStringLiteral("HaoRig Agent")),
                       chinese_ui_
                           ? QStringLiteral("已扫描 %1：%2 个角色，%3 个动作。").arg(QDir::toNativeSeparators(root)).arg(targets.size()).arg(sources.size())
                           : QStringLiteral("Scanned %1: %2 characters, %3 motions.").arg(QDir::toNativeSeparators(root)).arg(targets.size()).arg(sources.size()));
}

void RigAiWindow::loadSelectedAigrilTarget() {
    const QString path = aigril_target_combo_ ? aigril_target_combo_->currentData().toString() : QString();
    if (!path.isEmpty()) {
        loadTargetSkeletonPath(path, true);
    }
}

void RigAiWindow::loadSelectedAigrilSource() {
    const QString path = aigril_source_combo_ ? aigril_source_combo_->currentData().toString() : QString();
    if (!path.isEmpty()) {
        loadSourceAnimationPath(path);
    }
}

void RigAiWindow::loadTargetAndSourcePaths(const QString& target_path, const QString& source_path) {
    const QString clean_target = target_path.trimmed();
    const QString clean_source = source_path.trimmed();
    if (!clean_target.isEmpty()) {
        loadTargetSkeletonPath(clean_target, true);
    }
    if (!clean_source.isEmpty()) {
        loadSourceAnimationPath(clean_source);
    }
}

void RigAiWindow::loadTargetSkeletonPath(const QString& path, bool preview_target) {
    QString error;
    SkeletonGraph skeleton = extractor_.loadFromFile(path, &error);
    if (skeleton.empty()) {
        QMessageBox::warning(this, trText(QStringLiteral("HaoRig AI")), error.isEmpty() ? trText(QStringLiteral("No skeleton found.")) : error);
        return;
    }
    setTargetSkeleton(skeleton);
    if (preview_target) {
        previewTargetCharacter(path);
    }
    if (!source_skeleton_.empty()) {
        generateMapping();
    }
}

void RigAiWindow::loadSourceAnimationPath(const QString& path) {
    QString error;
    SkeletonGraph skeleton = extractor_.loadFromFile(path, &error);
    if (skeleton.empty()) {
        QMessageBox::warning(this, trText(QStringLiteral("HaoRig AI")), error.isEmpty() ? trText(QStringLiteral("No skeleton found.")) : error);
        return;
    }
    setSourceSkeleton(skeleton);
    if (!target_skeleton_.empty()) {
        generateMapping();
    }
}

void RigAiWindow::generateMapping() {
    if (target_skeleton_.empty() || source_skeleton_.empty()) {
        QMessageBox::information(this, trText(QStringLiteral("HaoRig AI")), trText(QStringLiteral("Load both a target character and a source animation first.")));
        return;
    }

    mapping_result_ = mapper_.mapSkeletons(source_skeleton_, target_skeleton_);
    refreshMappingTable();
    refreshSummary();
    export_button_->setEnabled(!mapping_result_.mappings.isEmpty());
    const QString summary_text = chinese_ui_
        ? QStringLiteral("%1 个映射，%2 个源骨骼需要检查，%3 个目标骨骼未使用。")
              .arg(mapping_result_.mappings.size())
              .arg(mapping_result_.unmapped_source_bones.size())
              .arg(mapping_result_.unmapped_target_bones.size())
        : mapping_result_.summary;
    appendAgentMessage(trText(QStringLiteral("HaoRig Agent")),
                           chinese_ui_
                           ? QStringLiteral("%1 请在导出前检查低置信度或未映射的行。").arg(summary_text)
                           : QStringLiteral("%1 Low-confidence or unmapped rows should be checked before export.").arg(summary_text));
    previewRetargetedAnimation(mapping_result_);
}

void RigAiWindow::refreshEyeExpressionPanel() {
    if (!eye_expression_section_ || !eye_expression_controls_layout_) {
        return;
    }

    clearLayoutWidgets(eye_expression_controls_layout_);
    eye_expression_sliders_.clear();

    const QStringList expression_names = availableEyeExpressionNames(preview_scene_);
    const bool has_expressions = !expression_names.isEmpty();
    const bool has_gaze_solver = hasEyeGazeSolver(preview_scene_);
    if (eye_expression_empty_label_) {
        eye_expression_empty_label_->setText(has_expressions
            ? QStringLiteral("%1").arg(expression_names.size())
            : trText(QStringLiteral("No VRM eye expressions found.")));
    }
    if (eye_expression_reset_button_) {
        eye_expression_reset_button_->setEnabled(has_expressions);
    }
    if (eye_expression_blink_curve_button_) {
        eye_expression_blink_curve_button_->setEnabled(
            expression_names.contains(QStringLiteral("blink"), Qt::CaseInsensitive) ||
            expression_names.contains(QStringLiteral("Fcl_EYE_Close"), Qt::CaseInsensitive));
    }
    if (eye_gaze_yaw_slider_) {
        eye_gaze_yaw_slider_->setEnabled(has_gaze_solver);
    }
    if (eye_gaze_pitch_slider_) {
        eye_gaze_pitch_slider_->setEnabled(has_gaze_solver);
    }
    if (eye_gaze_weight_slider_) {
        eye_gaze_weight_slider_->setEnabled(has_gaze_solver);
    }

    for (const QString& expression_name : expression_names) {
        auto* row_widget = new QWidget(eye_expression_section_);
        auto* row = new QHBoxLayout(row_widget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);

        auto* name_label = new QLabel(eyeExpressionDisplayLabel(expression_name), row_widget);
        name_label->setProperty("panelSubtitle", "true");
        name_label->setMinimumWidth(104);
        auto* slider = new QSlider(Qt::Horizontal, row_widget);
        slider->setRange(0, 100);
        slider->setValue(0);
        auto* value_label = new QLabel(QStringLiteral("0.00"), row_widget);
        value_label->setProperty("valueText", "primary");
        value_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value_label->setMinimumWidth(42);
        row->addWidget(name_label);
        row->addWidget(slider, 1);
        row->addWidget(value_label);
        eye_expression_controls_layout_->addWidget(row_widget);

        connect(slider, &QSlider::valueChanged, this, [this, expression_name, value_label](int value) {
            const double weight = static_cast<double>(value) / 100.0;
            if (value_label) {
                value_label->setText(QString::number(weight, 'f', 2));
            }
            applyExpressionWeight(expression_name, weight);
        });
        eye_expression_sliders_.push_back(ExpressionSliderBinding{ expression_name, slider, value_label });
    }
}

void RigAiWindow::applyExpressionWeight(const QString& expression_name, double weight) {
    if (!preview_viewport_) {
        return;
    }
    preview_viewport_->setExpressionWeight(expression_name.toStdString(), static_cast<float>(weight));
}

void RigAiWindow::applyEyeGazeControl() {
    if (!preview_viewport_ || !eye_gaze_yaw_slider_ || !eye_gaze_pitch_slider_ || !eye_gaze_weight_slider_) {
        return;
    }
    preview_viewport_->setEyeGaze(static_cast<float>(eye_gaze_yaw_slider_->value()),
                                  static_cast<float>(eye_gaze_pitch_slider_->value()),
                                  static_cast<float>(eye_gaze_weight_slider_->value()) / 100.0f);
}

void RigAiWindow::toggleAnimationPlayback() {
    if (!preview_viewport_ || !preview_scene_.hasAnimations()) {
        return;
    }
    preview_viewport_->setAnimationPlaybackEnabled(!preview_viewport_->animationPlaybackEnabled());
    refreshPlaybackControls();
}

void RigAiWindow::restartAnimationPlayback() {
    if (!preview_viewport_ || !preview_scene_.hasAnimations()) {
        return;
    }
    preview_viewport_->restartAnimationPlayback();
    preview_viewport_->setAnimationPlaybackEnabled(true);
    refreshPlaybackControls();
}

void RigAiWindow::refreshPlaybackControls() {
    const bool can_play = preview_viewport_ && preview_scene_.hasAnimations();
    if (play_pause_button_) {
        play_pause_button_->setEnabled(can_play);
        play_pause_button_->setText(trText(can_play && preview_viewport_->animationPlaybackEnabled()
            ? QStringLiteral("Pause")
            : QStringLiteral("Play")));
    }
    if (restart_animation_button_) {
        restart_animation_button_->setEnabled(can_play);
        restart_animation_button_->setText(trText(QStringLiteral("Restart")));
    }
}

void RigAiWindow::resetEyeExpressionControls() {
    if (preview_viewport_) {
        preview_viewport_->clearExpressionCurvePreview();
        preview_viewport_->clearExpressionWeights();
        preview_viewport_->clearEyeGazeCurvePreview();
        preview_viewport_->clearEyeGaze();
    }
    if (eye_gaze_yaw_slider_) {
        const bool blocked = eye_gaze_yaw_slider_->blockSignals(true);
        eye_gaze_yaw_slider_->setValue(0);
        eye_gaze_yaw_slider_->blockSignals(blocked);
    }
    if (eye_gaze_pitch_slider_) {
        const bool blocked = eye_gaze_pitch_slider_->blockSignals(true);
        eye_gaze_pitch_slider_->setValue(0);
        eye_gaze_pitch_slider_->blockSignals(blocked);
    }
    if (eye_gaze_weight_slider_) {
        const bool blocked = eye_gaze_weight_slider_->blockSignals(true);
        eye_gaze_weight_slider_->setValue(100);
        eye_gaze_weight_slider_->blockSignals(blocked);
    }
    if (eye_gaze_yaw_value_label_) {
        eye_gaze_yaw_value_label_->setText(QStringLiteral("0"));
    }
    if (eye_gaze_pitch_value_label_) {
        eye_gaze_pitch_value_label_->setText(QStringLiteral("0"));
    }
    if (eye_gaze_weight_value_label_) {
        eye_gaze_weight_value_label_->setText(QStringLiteral("1.00"));
    }
    for (const ExpressionSliderBinding& binding : eye_expression_sliders_) {
        if (binding.slider) {
            const bool blocked = binding.slider->blockSignals(true);
            binding.slider->setValue(0);
            binding.slider->blockSignals(blocked);
        }
        if (binding.value_label) {
            binding.value_label->setText(QStringLiteral("0.00"));
        }
    }
    setPreviewStatus(trText(QStringLiteral("Expression controls reset.")));
}

void RigAiWindow::applySoftBlinkExpressionCurve() {
    if (!preview_viewport_) {
        return;
    }
    const ExpressionCurvePlan plan = buildSoftBlinkCurve(preview_scene_);
    if (!plan.valid()) {
        setPreviewStatus(trText(QStringLiteral("No VRM eye expressions found.")));
        return;
    }
    preview_viewport_->setExpressionCurvePreview(plan.channels, plan.duration_seconds);
    for (const ExpressionSliderBinding& binding : eye_expression_sliders_) {
        if (binding.slider) {
            const bool blocked = binding.slider->blockSignals(true);
            binding.slider->setValue(0);
            binding.slider->blockSignals(blocked);
        }
        if (binding.value_label) {
            binding.value_label->setText(QStringLiteral("0.00"));
        }
    }
    setPreviewStatus(trText(QStringLiteral("Soft blink expression curve is playing.")));
}

void RigAiWindow::configurePreviewViewport() {
    if (!preview_viewport_) {
        return;
    }

    LookDevSettings settings;
    settings.shading_model = ShadingModel::Phong;
    settings.enable_shadows = false;
    settings.enable_backface_culling = false;
    settings.phong.ambient_strength = 0.18f;
    settings.phong.diffuse_strength = 0.95f;
    settings.phong.specular_strength = 0.08f;
    settings.phong.rim_strength = 0.10f;
    settings.phong.rim_power = 2.2f;
    preview_viewport_->setRenderPipeline(RenderPipeline::Raster);
    preview_viewport_->setLookDevSettings(settings);
    preview_viewport_->setClearColor(QColor(22, 25, 30));
    preview_viewport_->setAnimationPlaybackEnabled(false);
}

void RigAiWindow::previewTargetCharacter(const QString& path) {
    if (!preview_viewport_) {
        return;
    }

    target_scene_ = SceneModel();
    preview_scene_ = SceneModel();
    preview_viewport_->setScene(nullptr);
    preview_viewport_->setAnimationPlaybackEnabled(false);
    refreshPlaybackControls();

    QString error;
    SceneModel loaded = model_loader_.loadFromFile(path, &error);
    if (loaded.empty()) {
        const QString message = error.isEmpty() ? trText(QStringLiteral("No skeleton found.")) : error;
        setPreviewStatus(message);
        appendAgentMessage(trText(QStringLiteral("HaoRig Agent")), message);
        return;
    }

    target_scene_ = std::move(loaded);
    preview_scene_ = target_scene_;
    configurePreviewViewport();
    preview_viewport_->setScene(&preview_scene_);
    preview_viewport_->setAnimationPlaybackEnabled(false);
    refreshPlaybackControls();
    refreshEyeExpressionPanel();
    setPreviewStatus(chinese_ui_
        ? QStringLiteral("目标角色预览：%1 个 mesh，%2 个三角形。")
              .arg(preview_scene_.meshes.size())
              .arg(preview_scene_.triangleCount())
        : QStringLiteral("Target preview: %1 meshes, %2 tris.")
              .arg(preview_scene_.meshes.size())
              .arg(preview_scene_.triangleCount()));
}

void RigAiWindow::previewRetargetedAnimation(const BoneMappingResult& mapping_result) {
    if (!preview_viewport_ || target_skeleton_.empty() || source_skeleton_.empty()) {
        return;
    }
    if (target_scene_.empty()) {
        QString target_error;
        target_scene_ = model_loader_.loadFromFile(target_skeleton_.source_path, &target_error);
        if (target_scene_.empty()) {
            const QString message = target_error.isEmpty()
                ? QStringLiteral("Failed to load target character for Rig preview.")
                : target_error;
            setPreviewStatus(message);
            appendAgentMessage(trText(QStringLiteral("HaoRig Agent")), message);
            return;
        }
    }

    QString source_error;
    SceneModel source_animation = model_loader_.loadAnimationFromFile(source_skeleton_.source_path, &source_error);
    if (source_animation.nodes.empty() || source_animation.animations.empty()) {
        const QString message = source_error.isEmpty()
            ? QStringLiteral("Failed to load source animation for Rig preview.")
            : source_error;
        setPreviewStatus(message);
        appendAgentMessage(trText(QStringLiteral("HaoRig Agent")), message);
        return;
    }

    const RetargetProfile profile = buildRetargetProfile(source_animation, target_scene_, mapping_result);
    appendAgentMessage(trText(QStringLiteral("HaoRig Agent")),
                       retargetProfileDetailedText(profile, chinese_ui_, 5));

    SceneModel retargeted_scene;
    QString retarget_error;
    if (!retargetAnimationToTarget(source_animation, target_scene_, mapping_result, &retargeted_scene, &retarget_error)) {
        const QString message = retarget_error.isEmpty()
            ? QStringLiteral("Failed to retarget animation for Rig preview.")
            : retarget_error;
        setPreviewStatus(message);
        appendAgentMessage(trText(QStringLiteral("HaoRig Agent")), message);
        return;
    }

    const RetargetQualityReport quality = scoreRetargetedAnimation(source_animation, target_scene_, retargeted_scene, mapping_result);
    QString quality_message = retargetQualitySummaryText(quality, chinese_ui_);
    if (!quality.issues.isEmpty()) {
        QStringList details;
        for (const RetargetQualityIssue& issue : quality.issues) {
            details << QStringLiteral("%1: %2").arg(issue.code, issue.message);
        }
        quality_message += QStringLiteral("\n") + details.join(QStringLiteral("\n"));
    }
    appendAgentMessage(trText(QStringLiteral("HaoRig Agent")), quality_message);

    preview_scene_ = std::move(retargeted_scene);
    configurePreviewViewport();
    preview_viewport_->setScene(&preview_scene_);
    preview_viewport_->setAnimationPlaybackEnabled(true);
    const AnimationClipData& clip = preview_scene_.animations.front();
    refreshEyeExpressionPanel();
    refreshPlaybackControls();
    setPreviewStatus(chinese_ui_
        ? QStringLiteral("动画预览：%1 个通道，%2 秒，质量 %3。")
              .arg(clip.channels.size())
              .arg(clip.durationSeconds(), 0, 'f', 2)
              .arg(quality.grade)
        : QStringLiteral("Animation preview: %1 channels, %2s, quality %3.")
              .arg(clip.channels.size())
              .arg(clip.durationSeconds(), 0, 'f', 2)
              .arg(quality.grade));
}

void RigAiWindow::setPreviewStatus(const QString& text) {
    if (preview_status_label_) {
        preview_status_label_->setText(text);
    }
}

void RigAiWindow::exportMapping() {
    if (source_skeleton_.empty() || target_skeleton_.empty() || mapping_table_->rowCount() == 0) {
        QMessageBox::information(this, trText(QStringLiteral("HaoRig AI")), trText(QStringLiteral("Generate a mapping before export.")));
        return;
    }

    const QString default_name = QStringLiteral("%1_to_%2.haorigmap.json")
                                     .arg(QFileInfo(source_skeleton_.source_path).completeBaseName(),
                                          QFileInfo(target_skeleton_.source_path).completeBaseName());
    const QString path = QFileDialog::getSaveFileName(this,
                                                      trText(QStringLiteral("Export Skeleton Mapping")),
                                                      QDir::current().filePath(default_name),
                                                      trText(QStringLiteral("HaoRig Mapping (*.haorigmap.json);;JSON (*.json);;All Files (*.*)")));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    const BoneMappingResult table_result = mappingFromTable();
    if (!exporter_.writeMapping(path, source_skeleton_, target_skeleton_, table_result, &error)) {
        QMessageBox::warning(this, trText(QStringLiteral("HaoRig AI")), error);
        return;
    }
    appendAgentMessage(trText(QStringLiteral("HaoRig Agent")),
                       chinese_ui_
                           ? QStringLiteral("已导出映射：%1").arg(QDir::toNativeSeparators(path))
                           : QStringLiteral("Exported mapping: %1").arg(QDir::toNativeSeparators(path)));
}

void RigAiWindow::setTargetSkeleton(const SkeletonGraph& skeleton) {
    target_skeleton_ = skeleton;
    target_path_edit_->setText(QDir::toNativeSeparators(skeleton.source_path));
    target_summary_label_->setText(skeletonSummaryText(skeleton));
    mapping_result_ = BoneMappingResult();
    refreshMappingTable();
    refreshSummary();
    export_button_->setEnabled(false);
    appendAgentMessage(trText(QStringLiteral("Target")),
                       chinese_ui_
                           ? QStringLiteral("%1 已加载。%2").arg(skeleton.asset_label, skeletonSummaryText(skeleton))
                           : QStringLiteral("%1 loaded. %2").arg(skeleton.asset_label, skeletonSummaryText(skeleton)));
}

void RigAiWindow::setSourceSkeleton(const SkeletonGraph& skeleton) {
    source_skeleton_ = skeleton;
    source_path_edit_->setText(QDir::toNativeSeparators(skeleton.source_path));
    source_summary_label_->setText(skeletonSummaryText(skeleton));
    mapping_result_ = BoneMappingResult();
    refreshMappingTable();
    refreshSummary();
    export_button_->setEnabled(false);
    appendAgentMessage(trText(QStringLiteral("Source")),
                       chinese_ui_
                           ? QStringLiteral("%1 已加载。%2").arg(skeleton.asset_label, skeletonSummaryText(skeleton))
                           : QStringLiteral("%1 loaded. %2").arg(skeleton.asset_label, skeletonSummaryText(skeleton)));
}

void RigAiWindow::refreshSummary() {
    if (!mapping_result_.mappings.isEmpty()) {
        mapping_summary_label_->setText(chinese_ui_
            ? QStringLiteral("%1 个映射，%2 个源骨骼需要检查，%3 个目标骨骼未使用。")
                  .arg(mapping_result_.mappings.size())
                  .arg(mapping_result_.unmapped_source_bones.size())
                  .arg(mapping_result_.unmapped_target_bones.size())
            : mapping_result_.summary);
    } else if (!target_skeleton_.empty() && !source_skeleton_.empty()) {
        mapping_summary_label_->setText(trText(QStringLiteral("Ready to generate AI skeleton mapping.")));
    } else {
        mapping_summary_label_->setText(trText(QStringLiteral("Load both skeletons to generate a mapping.")));
    }
}

void RigAiWindow::refreshMappingTable() {
    mapping_table_->setRowCount(0);
    mapping_table_->setRowCount(mapping_result_.mappings.size());
    for (int row = 0; row < mapping_result_.mappings.size(); ++row) {
        const BoneMapping& mapping = mapping_result_.mappings[row];
        mapping_table_->setItem(row, 0, makeReadOnlyItem(mapping.source_bone));
        mapping_table_->setItem(row, 1, makeReadOnlyItem(mapping.canonical_name));
        mapping_table_->setItem(row, 2, new QTableWidgetItem(mapping.target_bone));
        mapping_table_->setItem(row, 3, makeConfidenceItem(mapping.confidence));
        mapping_table_->setItem(row, 4, makeReadOnlyItem(mapping.reason));
    }
}

void RigAiWindow::appendAgentMessage(const QString& speaker, const QString& text) {
    if (!agent_view_) {
        return;
    }
    const QString safe_speaker = speaker.toHtmlEscaped();
    const QString safe_text = text.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    agent_view_->append(QStringLiteral("<p><b>%1</b><br>%2</p>").arg(safe_speaker, safe_text));
    agent_view_->moveCursor(QTextCursor::End);
}

BoneMappingResult RigAiWindow::mappingFromTable() const {
    BoneMappingResult result;
    for (int row = 0; row < mapping_table_->rowCount(); ++row) {
        BoneMapping mapping;
        mapping.source_bone = mapping_table_->item(row, 0) ? mapping_table_->item(row, 0)->text() : QString();
        mapping.canonical_name = mapping_table_->item(row, 1) ? mapping_table_->item(row, 1)->text() : QString();
        mapping.target_bone = mapping_table_->item(row, 2) ? mapping_table_->item(row, 2)->text() : QString();
        mapping.confidence = mapping_table_->item(row, 3) ? mapping_table_->item(row, 3)->text().toFloat() : 0.0f;
        mapping.reason = mapping_table_->item(row, 4) ? mapping_table_->item(row, 4)->text() : QString();
        if (!mapping.source_bone.isEmpty() && !mapping.target_bone.isEmpty()) {
            result.mappings.push_back(mapping);
        }
    }
    result.unmapped_source_bones = mapping_result_.unmapped_source_bones;
    result.unmapped_target_bones = mapping_result_.unmapped_target_bones;
    result.summary = QStringLiteral("%1 exported mappings. Manual table edits are preserved.").arg(result.mappings.size());
    return result;
}

} // namespace haorendergi
