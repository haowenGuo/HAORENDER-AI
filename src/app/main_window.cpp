#include "app/main_window.h"

#include "app/asset_converter_window.h"
#include "app/expression_agent_tools.h"
#include "app/rig_ai_window.h"
#include "rigging/animation_retargeter.h"
#include "rigging/skeleton_extractor.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QStyle>
#include <QSlider>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <utility>

namespace haorendergi {
class SliderField final : public QWidget {
public:
    SliderField(double minimum, double maximum, double step, int decimals, double initial_value, QWidget* parent = nullptr)
        : QWidget(parent),
          minimum_(minimum),
          maximum_(maximum),
          step_(step),
          decimals_(decimals) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(28);
        setMaximumHeight(34);

        slider_ = new QSlider(Qt::Horizontal, this);
        slider_->setRange(0, std::max(1, static_cast<int>(std::lround((maximum_ - minimum_) / step_))));
        slider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        value_label_ = new QLabel(this);
        value_label_->setProperty("valueText", "primary");
        value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value_label_->setMinimumWidth(decimals_ == 0 ? 56 : 64);
        value_label_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(slider_, 1);
        layout->addWidget(value_label_);

        QObject::connect(slider_, &QSlider::valueChanged, this, [this](int) {
            updateValueLabel();
            if (on_value_changed_) {
                on_value_changed_(this->value());
            }
        });

        setValue(initial_value);
    }

    double value() const {
        return minimum_ + static_cast<double>(slider_->value()) * step_;
    }

    void setValue(double value) {
        const int slider_value = std::clamp(static_cast<int>(std::lround((value - minimum_) / step_)), slider_->minimum(), slider_->maximum());
        const bool blocked = slider_->blockSignals(true);
        slider_->setValue(slider_value);
        slider_->blockSignals(blocked);
        updateValueLabel();
    }

    void setOnValueChanged(std::function<void(double)> callback) {
        on_value_changed_ = std::move(callback);
    }

private:
    void updateValueLabel() {
        value_label_->setText(QString::number(value(), 'f', decimals_));
    }

    double minimum_ = 0.0;
    double maximum_ = 1.0;
    double step_ = 0.01;
    int decimals_ = 2;
    QSlider* slider_ = nullptr;
    QLabel* value_label_ = nullptr;
    std::function<void(double)> on_value_changed_;
};

class ColorField final : public QWidget {
public:
    explicit ColorField(const QColor& initial_color, QWidget* parent = nullptr)
        : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        layout->setAlignment(Qt::AlignTop);
        layout->setSizeConstraint(QLayout::SetMinimumSize);

        red_slider_ = addChannel(QStringLiteral("R"), initial_color.redF(), layout);
        green_slider_ = addChannel(QStringLiteral("G"), initial_color.greenF(), layout);
        blue_slider_ = addChannel(QStringLiteral("B"), initial_color.blueF(), layout);
    }

    QColor color() const {
        return QColor::fromRgbF(red_slider_->value(), green_slider_->value(), blue_slider_->value());
    }

    void setColor(const QColor& color) {
        suppress_notifications_ = true;
        red_slider_->setValue(color.redF());
        green_slider_->setValue(color.greenF());
        blue_slider_->setValue(color.blueF());
        suppress_notifications_ = false;
    }

    void setOnColorChanged(std::function<void(const QColor&)> callback) {
        on_color_changed_ = std::move(callback);
    }

private:
    SliderField* addChannel(const QString& name, double initial_value, QVBoxLayout* parent_layout) {
        auto* row = new QWidget(this);
        row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->setMinimumHeight(28);
        row->setMaximumHeight(34);

        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(10);

        auto* label = new QLabel(name, row);
        label->setProperty("fieldLabel", "true");
        label->setMinimumWidth(16);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        row_layout->addWidget(label);

        auto* slider = new SliderField(0.0, 1.0, 0.01, 2, initial_value, row);
        row_layout->addWidget(slider, 1);
        parent_layout->addWidget(row, 0);

        slider->setOnValueChanged([this](double) {
            if (!suppress_notifications_ && on_color_changed_) {
                on_color_changed_(color());
            }
        });
        return slider;
    }

    SliderField* red_slider_ = nullptr;
    SliderField* green_slider_ = nullptr;
    SliderField* blue_slider_ = nullptr;
    bool suppress_notifications_ = false;
    std::function<void(const QColor&)> on_color_changed_;
};

class CurrentPageStackedWidget final : public QStackedWidget {
public:
    explicit CurrentPageStackedWidget(QWidget* parent = nullptr)
        : QStackedWidget(parent) {}

    QSize sizeHint() const override {
        return pageSizeHint(false);
    }

    QSize minimumSizeHint() const override {
        return pageSizeHint(true);
    }

private:
    QSize pageSizeHint(bool minimum) const {
        QWidget* page = currentWidget();
        if (!page) {
            return QStackedWidget::sizeHint();
        }
        if (QLayout* page_layout = page->layout()) {
            page_layout->activate();
            const QSize layout_hint = minimum ? page_layout->minimumSize() : page_layout->sizeHint();
            return layout_hint.expandedTo(QSize(1, 1));
        }
        const QSize hinted = minimum ? page->minimumSizeHint() : page->sizeHint();
        return hinted.expandedTo(QSize(1, 1));
    }
};

namespace {

QString formatMs(double value) {
    return QString::number(value, 'f', value >= 100.0 ? 0 : 2) + " ms";
}

QString formatCount(std::size_t value) {
    return QLocale().toString(static_cast<qulonglong>(value));
}

QLabel* makeLabel(const QString& text, const char* property_name, const char* property_value, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty(property_name, property_value);
    return label;
}

QToolButton* makeActionButton(QAction* action, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setObjectName(QStringLiteral("ActionButton"));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setToolTip(action ? action->text() : QString());
    button->setAutoRaise(false);
    return button;
}

QPushButton* makeActionPushButton(QAction* action, QWidget* parent) {
    auto* button = new QPushButton(action->icon(), action->text(), parent);
    button->setObjectName(QStringLiteral("ActionButton"));
    QObject::connect(button, &QPushButton::clicked, action, &QAction::trigger);
    return button;
}

QFrame* makeInspectorSection(const QString& title, QWidget* parent, QVBoxLayout** content_layout) {
    auto* section = new QFrame(parent);
    section->setProperty("panelSection", "true");
    section->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(8);
    layout->setAlignment(Qt::AlignTop);
    layout->setSizeConstraint(QLayout::SetMinimumSize);

    auto* title_label = new QLabel(title, section);
    title_label->setProperty("sectionTitle", "true");
    title_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(title_label, 0);

    auto* content = new QVBoxLayout();
    content->setSpacing(7);
    content->setAlignment(Qt::AlignTop);
    content->setSizeConstraint(QLayout::SetMinimumSize);
    layout->addLayout(content, 0);
    if (content_layout) {
        *content_layout = content;
    }
    return section;
}

void addField(QVBoxLayout* layout, const QString& label_text, QWidget* field) {
    auto* caption = new QLabel(label_text, field->parentWidget());
    caption->setProperty("fieldLabel", "true");
    caption->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    if (!qobject_cast<QLabel*>(field)) {
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    layout->addWidget(caption, 0);
    layout->addWidget(field, 0);
}

QWidget* makeMetricBlock(const QString& label_text, QLabel** value_label, QWidget* parent) {
    auto* panel = new QFrame(parent);
    panel->setProperty("metricBlock", "true");

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(2);

    auto* label = new QLabel(label_text, panel);
    label->setProperty("metricLabel", "true");
    layout->addWidget(label);

    *value_label = new QLabel(QStringLiteral("--"), panel);
    (*value_label)->setProperty("metricValue", "true");
    layout->addWidget(*value_label);
    return panel;
}

QWidget* makeModePage(QStackedWidget* stack, QVBoxLayout** layout_out) {
    auto* page = new QWidget(stack);
    page->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignTop);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    if (layout_out) {
        *layout_out = layout;
    }
    return page;
}

void setComboIndexSilently(QComboBox* combo, int index) {
    if (!combo) {
        return;
    }
    const bool blocked = combo->blockSignals(true);
    combo->setCurrentIndex(index);
    combo->blockSignals(blocked);
}

void setCheckedSilently(QCheckBox* check_box, bool checked) {
    if (!check_box) {
        return;
    }
    const bool blocked = check_box->blockSignals(true);
    check_box->setChecked(checked);
    check_box->blockSignals(blocked);
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

struct UiTextPair {
    const char* english;
    const char* chinese;
};

const UiTextPair kUiTextPairs[] = {
    { "Render", u8"渲染" },
    { "Open", u8"打开" },
    { "Convert", u8"转换" },
    { "Asset Converter", u8"资产转换器" },
    { "Asset Converter workspace", u8"资产转换器工作区" },
    { "Rig AI", u8"骨骼 AI" },
    { "HaoRig AI skeleton mapping", u8"HaoRig AI 骨骼映射" },
    { "Rig AI workspace", u8"骨骼 AI 工作区" },
    { "Reset", u8"重置" },
    { "Snapshot", u8"截图" },
    { "OpenGL render workbench", u8"OpenGL 渲染工作台" },
    { "HaoRender-GI render workbench", u8"HaoRender-GI 渲染工作台" },
    { "Workspace ready", u8"工作区已就绪" },
    { "SCENE VIEW", u8"场景视图" },
    { "Play", u8"播放" },
    { "Pause", u8"暂停" },
    { "Restart", u8"重播" },
    { "INSPECTOR", u8"检查器" },
    { "Reference-grade look-dev controls", u8"LookDev 参数控制" },
    { "No asset loaded", u8"未加载资源" },
    { "Eye / Expression Debug", u8"眼部 / 表情调试" },
    { "No VRM eye expressions found.", u8"未找到 VRM 眼部表情。" },
    { "Reset Expressions", u8"重置表情" },
    { "Soft Blink Curve", u8"柔和眨眼曲线" },
    { "Expression controls reset.", u8"表情控制已重置。" },
    { "Soft blink expression curve is playing.", u8"正在播放柔和眨眼表情曲线。" },
    { "OpenGL Rasterizer | PBR", u8"OpenGL 光栅化 | PBR" },
    { "Interactive preview ready", u8"交互预览已就绪" },
    { "LMB orbit   RMB pan   Wheel zoom", u8"左键旋转   右键平移   滚轮缩放" },
    { "Settings", u8"设置" },
    { "Language", u8"语言" },
    { "UI background", u8"界面背景" },
    { "English", u8"英文" },
    { "Chinese", u8"中文" },
    { "Studio Dark", u8"工作室深色" },
    { "Deep Black", u8"纯黑" },
    { "Neutral Gray", u8"中性灰" },
    { "Paper White", u8"纸白" },
    { "Warm Beige", u8"暖米色" },
    { "Cool Blue", u8"冷蓝色" },
    { "Forest Green", u8"森林绿" },
    { "Image...", u8"图片..." },
    { "Choose UI Image", u8"选择界面图片" },
    { "Viewport background", u8"渲染窗口背景" },
    { "Choose Viewport Image", u8"选择渲染窗口图片" },
    { "LookDev AI", u8"LookDev AI" },
    { "Save", u8"保存" },
    { "Load", u8"加载" },
    { "Send", u8"发送" },
    { "Thinking...", u8"思考中..." },
    { "Ask for a rendering look...", u8"描述你想要的渲染风格..." },
    { "Asset", u8"资源" },
    { "Current model", u8"当前模型" },
    { "Meshes", u8"网格" },
    { "Triangles", u8"三角形" },
    { "Vertices", u8"顶点" },
    { "Load a mesh or scene file to begin.", u8"加载一个模型或场景文件开始。" },
    { "Look Dev", u8"LookDev" },
    { "Render path", u8"渲染路径" },
    { "Shading model", u8"着色模型" },
    { "Exposure", u8"曝光" },
    { "Normal intensity", u8"法线强度" },
    { "Raster", u8"光栅化" },
    { "OpenGL Ray Trace", u8"OpenGL 光线追踪" },
    { "DXR Hardware RT", u8"DXR 硬件光追" },
    { "PBR", u8"PBR" },
    { "Phong", u8"Phong" },
    { "PBR Lighting", u8"PBR 光照" },
    { "Enable image based lighting", u8"启用环境图光照" },
    { "IBL diffuse", u8"IBL 漫反射" },
    { "IBL specular", u8"IBL 高光" },
    { "Sky light", u8"天空光" },
    { "PBR Channel Map", u8"PBR 通道映射" },
    { "Metallic channel", u8"金属度通道" },
    { "Roughness channel", u8"粗糙度通道" },
    { "AO channel", u8"AO 通道" },
    { "Emissive channel", u8"自发光通道" },
    { "Phong Surface", u8"Phong 表面" },
    { "Hard-edge specular", u8"硬边高光" },
    { "Apply tone mapping", u8"应用色调映射" },
    { "Primary light only", u8"仅主光源" },
    { "Secondary light", u8"辅光" },
    { "Diffuse strength", u8"漫反射强度" },
    { "Ambient strength", u8"环境光强度" },
    { "Specular strength", u8"高光强度" },
    { "Smoothness", u8"平滑度" },
    { "Specular map weight", u8"高光贴图权重" },
    { "Shininess", u8"高光锐度" },
    { "Phong Color Tuning", u8"Phong 颜色调整" },
    { "Ambient color", u8"环境光颜色" },
    { "Specular tint", u8"高光色调" },
    { "Rim Light", u8"边缘光" },
    { "Rim strength", u8"边缘光强度" },
    { "Rim power", u8"边缘光范围" },
    { "Rim tint", u8"边缘光色调" },
    { "Toon Shading", u8"卡通着色" },
    { "Enable toon shaping", u8"启用卡通分层" },
    { "Diffuse steps", u8"漫反射阶数" },
    { "Band softness", u8"分层柔和度" },
    { "Shadow floor", u8"暗部亮度下限" },
    { "Lit floor", u8"亮部亮度下限" },
    { "Ramp bias", u8"Ramp 偏移" },
    { "Ramp contrast", u8"Ramp 对比度" },
    { "Shadow receive", u8"阴影接收强度" },
    { "Shadow threshold", u8"阴影阈值" },
    { "Shadow softness", u8"阴影柔和度" },
    { "Shadow tint", u8"阴影色调" },
    { "Highlight threshold", u8"高光阈值" },
    { "Highlight softness", u8"高光柔和度" },
    { "Highlight strength", u8"高光强度" },
    { "Highlight tint", u8"高光色调" },
    { "Rim threshold", u8"边缘光阈值" },
    { "Rim softness", u8"边缘光柔和度" },
    { "Material Toon Override", u8"材质 Toon 覆盖" },
    { "Enable material override", u8"启用材质覆盖" },
    { "Texture influence", u8"贴图影响" },
    { "Albedo lift", u8"底色提亮" },
    { "Albedo saturation", u8"底色饱和度" },
    { "Albedo contrast", u8"底色对比度" },
    { "Outline", u8"描边" },
    { "Enable silhouette outline", u8"启用轮廓描边" },
    { "Width (px)", u8"宽度 (px)" },
    { "Opacity", u8"不透明度" },
    { "Depth bias", u8"深度偏移" },
    { "Outline color", u8"描边颜色" },
    { "Ray Trace Preview", u8"光追预览" },
    { "Scene", u8"场景" },
    { "Cornell Box", u8"Cornell 盒" },
    { "Loaded Model", u8"已加载模型" },
    { "Integrator", u8"积分器" },
    { "Hybrid RT", u8"混合光追" },
    { "Path Trace", u8"路径追踪" },
    { "Path Trace + NEE", u8"路径追踪 + NEE" },
    { "Photon Path", u8"光子路径" },
    { "Debug view", u8"调试视图" },
    { "Lit", u8"光照" },
    { "Hit / Miss", u8"命中 / 未命中" },
    { "Normal", u8"法线" },
    { "Albedo", u8"反照率" },
    { "Ambient", u8"环境光" },
    { "Shadow strength", u8"阴影强度" },
    { "Max bounces", u8"最大反弹" },
    { "NEE bounces", u8"NEE 反弹" },
    { "Samples / frame", u8"每帧采样" },
    { "Enable next-event estimation", u8"启用下一事件估计" },
    { "Enable photon cache", u8"启用光子缓存" },
    { "Photon radius", u8"光子半径" },
    { "Photon intensity", u8"光子强度" },
    { "Raster Settings", u8"光栅设置" },
    { "Enable shadows", u8"启用阴影" },
    { "Enable backface culling", u8"启用背面剔除" },
    { "Backend", u8"后端" },
    { "Capture", u8"捕获" },
    { "Preset", u8"预设" },
    { "Summary", u8"摘要" },
    { "Rationale", u8"理由" },
    { "Apply", u8"应用" },
    { "Apply Preset", u8"应用预设" },
    { "No preset yet", u8"暂无预设" },
    { "Talk with the agent until the target look is clear enough to create one preset.", u8"和 Agent 继续描述，直到目标风格足够清楚后生成一套预设。" },
    { "The agent can ask follow-up questions before generating parameters.", u8"Agent 可以先追问，再生成参数。" },
    { "Ready. Describe the target look; I will ask follow-ups or generate one tuned preset.", u8"已就绪。描述目标风格后，我会追问或生成一套调好的预设。" },
    { "Tell the agent what to adjust next...", u8"继续告诉 Agent 下一步要调整什么..." }
};

QString translateKnownUiText(const QString& text, bool target_chinese) {
    for (const UiTextPair& pair : kUiTextPairs) {
        const QString english = QString::fromUtf8(pair.english);
        const QString chinese = QString::fromUtf8(pair.chinese);
        if (target_chinese && text == english) {
            return chinese;
        }
        if (!target_chinese && text == chinese) {
            return english;
        }
    }
    return text;
}

void translateWidgetTree(QWidget* root, bool target_chinese) {
    if (!root) {
        return;
    }
    for (QLabel* label : root->findChildren<QLabel*>()) {
        label->setText(translateKnownUiText(label->text(), target_chinese));
    }
    for (QPushButton* button : root->findChildren<QPushButton*>()) {
        button->setText(translateKnownUiText(button->text(), target_chinese));
    }
    for (QCheckBox* check_box : root->findChildren<QCheckBox*>()) {
        check_box->setText(translateKnownUiText(check_box->text(), target_chinese));
    }
    for (QComboBox* combo : root->findChildren<QComboBox*>()) {
        const bool blocked = combo->blockSignals(true);
        for (int i = 0; i < combo->count(); ++i) {
            combo->setItemText(i, translateKnownUiText(combo->itemText(i), target_chinese));
        }
        combo->blockSignals(blocked);
    }
    for (QLineEdit* line_edit : root->findChildren<QLineEdit*>()) {
        line_edit->setPlaceholderText(translateKnownUiText(line_edit->placeholderText(), target_chinese));
    }
    for (QPlainTextEdit* plain_text_edit : root->findChildren<QPlainTextEdit*>()) {
        plain_text_edit->setPlaceholderText(translateKnownUiText(plain_text_edit->placeholderText(), target_chinese));
    }
}

QFrame* makeCandidateCard(QWidget* parent,
                          QLabel** slot_label,
                          QLabel** name_label,
                          QLabel** summary_label,
                          QLabel** reason_label,
                          QPushButton** apply_button) {
    auto* card = new QFrame(parent);
    card->setProperty("candidateCard", "true");

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    *slot_label = new QLabel(QStringLiteral("A"), card);
    (*slot_label)->setProperty("candidateSlot", "true");
    layout->addWidget(*slot_label);

    *name_label = new QLabel(QStringLiteral("Preset"), card);
    (*name_label)->setProperty("candidateTitle", "true");
    (*name_label)->setWordWrap(true);
    layout->addWidget(*name_label);

    *summary_label = new QLabel(QStringLiteral("Summary"), card);
    (*summary_label)->setProperty("candidateSummary", "true");
    (*summary_label)->setWordWrap(true);
    layout->addWidget(*summary_label);

    *reason_label = new QLabel(QStringLiteral("Rationale"), card);
    (*reason_label)->setProperty("candidateReason", "true");
    (*reason_label)->setWordWrap(true);
    layout->addWidget(*reason_label);

    *apply_button = new QPushButton(QStringLiteral("Apply"), card);
    (*apply_button)->setObjectName(QStringLiteral("ActionButton"));
    layout->addWidget(*apply_button);

    return card;
}

ShadingModel shadingModelFromIndex(int index) {
    return index == 1 ? ShadingModel::Phong : ShadingModel::Pbr;
}

RenderPipeline renderPipelineFromIndex(int index) {
    switch (index) {
    case 1:
        return RenderPipeline::RayTrace;
    case 2:
        return RenderPipeline::DxrRayTrace;
    case 0:
    default:
        return RenderPipeline::Raster;
    }
}

RayTraceSceneMode rayTraceSceneModeFromIndex(int index) {
    return index == 1 ? RayTraceSceneMode::LoadedModel : RayTraceSceneMode::CornellBox;
}

RayTraceViewMode rayTraceViewModeFromIndex(int index) {
    switch (index) {
    case 1:
        return RayTraceViewMode::Hit;
    case 2:
        return RayTraceViewMode::Normal;
    case 3:
        return RayTraceViewMode::Albedo;
    default:
        return RayTraceViewMode::Lit;
    }
}

RayTraceIntegrator rayTraceIntegratorFromIndex(int index) {
    switch (index) {
    case 1:
        return RayTraceIntegrator::PathTrace;
    case 2:
        return RayTraceIntegrator::PathTraceNee;
    case 3:
        return RayTraceIntegrator::PhotonPath;
    case 0:
    default:
        return RayTraceIntegrator::Hybrid;
    }
}

QColor blendColor(const QColor& from, const QColor& to, double amount) {
    amount = std::clamp(amount, 0.0, 1.0);
    const auto blend_channel = [amount](int a, int b) {
        return std::clamp(static_cast<int>(std::lround(static_cast<double>(a) * (1.0 - amount) +
                                                       static_cast<double>(b) * amount)), 0, 255);
    };
    return QColor(blend_channel(from.red(), to.red()),
                  blend_channel(from.green(), to.green()),
                  blend_channel(from.blue(), to.blue()));
}

QString cssColor(const QColor& color) {
    return QStringLiteral("#%1%2%3")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'));
}

} // namespace

MainWindow::MainWindow(const QString& startup_model_path,
                       RenderPipeline initial_pipeline,
                       const QString& startup_style_preset_path,
                       const QString& startup_source_animation_path,
                       QWidget* parent)
    : QMainWindow(parent),
      render_pipeline_(initial_pipeline),
      startup_model_path_(startup_model_path),
      startup_style_preset_path_(startup_style_preset_path),
      startup_source_animation_path_(startup_source_animation_path),
      ai_client_(this) {
    if (QLocale::system().language() == QLocale::Chinese) {
        ui_language_ = UiLanguage::Chinese;
    }
    if ((render_pipeline_ == RenderPipeline::RayTrace || render_pipeline_ == RenderPipeline::DxrRayTrace) &&
        !startup_model_path_.isEmpty()) {
        look_settings_.ray_trace.scene_mode = RayTraceSceneMode::LoadedModel;
    }
    buildUi();
    loadDefaultModel();
    if (!startup_style_preset_path_.trimmed().isEmpty()) {
        loadStylePresetFromFile(startup_style_preset_path_);
    }
    if (!startup_source_animation_path_.trimmed().isEmpty() && !startup_model_path_.trimmed().isEmpty()) {
        QTimer::singleShot(250, this, [this]() {
            previewRigAnimationWithAutoMapping(startup_model_path_, startup_source_animation_path_);
            showRenderWorkspace();
        });
    }
}

void MainWindow::applyWorkspaceTheme() {
    const QString base_style = QStringLiteral(R"(
QMainWindow, QWidget#WorkspaceRoot {
    background: #0b1016;
    color: #dce5ef;
}

QFrame#WorkspaceBar {
    background: #0f151d;
    border-bottom: 1px solid #1c2633;
}

QLabel#AppNameLabel {
    color: #f5f8fb;
    font-size: 18px;
    font-weight: 600;
}

QLabel#WorkspaceMetaLabel {
    color: #90a2b8;
    font-size: 11px;
}

QFrame#ViewportHeader,
QFrame#ViewportFooter,
QFrame#ViewportShell,
QFrame#InspectorPane,
QFrame[panelSection="true"],
QFrame[metricBlock="true"] {
    background: #111924;
    border: 1px solid #1e2a39;
    border-radius: 8px;
}

QFrame#ViewportShell {
    background: #0a0f15;
}

QFrame#ViewportShell > QOpenGLWidget {
    background: #0a0f15;
    border-radius: 7px;
}

QToolButton#ActionButton,
QPushButton#ActionButton {
    background: #172231;
    color: #eef3f8;
    border: 1px solid #2a3a4f;
    border-radius: 6px;
    padding: 7px 12px;
    font-weight: 600;
}

QToolButton#ActionButton:hover,
QPushButton#ActionButton:hover {
    background: #1c2a3c;
    border-color: #38506d;
}

QToolButton#ActionButton:pressed,
QPushButton#ActionButton:pressed {
    background: #15202e;
}

QLabel[panelEyebrow="true"] {
    color: #7f92a9;
    font-size: 10px;
    font-weight: 600;
}

QLabel[panelTitle="true"] {
    color: #f4f7fb;
    font-size: 16px;
    font-weight: 600;
}

QLabel[panelSubtitle="true"] {
    color: #91a3ba;
    font-size: 11px;
}

QLabel[sectionTitle="true"] {
    color: #f4f7fb;
    font-size: 13px;
    font-weight: 600;
}

QLabel[fieldLabel="true"] {
    color: #8b9db3;
    font-size: 10px;
    font-weight: 600;
}

QLabel[valueText="primary"] {
    color: #edf3f8;
    font-size: 12px;
    font-weight: 500;
}

QLabel[metricLabel="true"] {
    color: #8091a6;
    font-size: 10px;
    font-weight: 600;
}

QLabel[metricValue="true"] {
    color: #f8fbff;
    font-size: 16px;
    font-weight: 600;
}

QLabel#FooterHintLabel {
    color: #91a4bb;
    font-size: 11px;
}

QLabel#FooterNoteLabel {
    color: #c4d0dc;
    font-size: 11px;
}

QComboBox,
QPlainTextEdit,
QTextBrowser,
QSlider {
    min-height: 22px;
}

QComboBox,
QPlainTextEdit,
QTextBrowser {
    min-height: 34px;
    padding: 0 10px;
    background: #0d141d;
    color: #eef3f8;
    border: 1px solid #2a394d;
    border-radius: 6px;
    selection-background-color: #2b68ff;
}

QComboBox:hover {
    border-color: #3a5373;
}

QPlainTextEdit:hover {
    border-color: #3a5373;
}

QTextBrowser#AiChatView {
    background: #0b1118;
    border: 1px solid #223043;
    border-radius: 8px;
    padding: 0;
}

QFrame#AiComposer {
    background: #0d141d;
    border: 1px solid #2a394d;
    border-radius: 8px;
}

QFrame#AiComposer:hover {
    border-color: #3a5373;
}

QPlainTextEdit#AiComposerInput {
    background: transparent;
    border: none;
    padding: 8px 10px;
    color: #eef3f8;
}

QPushButton#AiSendButton {
    background: #eef3f8;
    color: #0d141d;
    border: none;
    border-radius: 6px;
    padding: 7px 12px;
    font-weight: 700;
}

QPushButton#AiSendButton:hover {
    background: #ffffff;
}

QPlainTextEdit {
    padding: 8px 10px;
    selection-background-color: #2b68ff;
}

QComboBox::drop-down {
    border: none;
    width: 24px;
}

QSlider::groove:horizontal {
    height: 6px;
    background: #1a2431;
    border-radius: 3px;
}

QSlider::sub-page:horizontal {
    background: #4f82ff;
    border-radius: 3px;
}

QSlider::handle:horizontal {
    background: #f4f7fb;
    width: 14px;
    margin: -5px 0;
    border-radius: 7px;
}

QCheckBox {
    color: #dce5ef;
    spacing: 8px;
}

QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border-radius: 4px;
    border: 1px solid #324154;
    background: #0d141d;
}

QCheckBox::indicator:checked {
    background: #4f82ff;
    border-color: #4f82ff;
}

QFrame[candidateCard="true"] {
    background: #0d141d;
    border: 1px solid #223043;
    border-radius: 6px;
}

QLabel[candidateSlot="true"] {
    color: #7f95af;
    font-size: 10px;
    font-weight: 700;
}

QLabel[candidateTitle="true"] {
    color: #f2f6fb;
    font-size: 13px;
    font-weight: 600;
}

QLabel[candidateSummary="true"] {
    color: #c9d5e2;
    font-size: 11px;
}

QLabel[candidateReason="true"] {
    color: #8fa4bb;
    font-size: 11px;
}

QScrollArea,
QScrollArea > QWidget > QWidget {
    background: transparent;
    border: none;
}

QSplitter::handle {
    background: #0b1016;
    width: 4px;
}

QStatusBar {
    background: #0f151d;
    color: #91a4bb;
    border-top: 1px solid #1c2633;
}

QStatusBar::item {
    border: none;
}
    )");

    const QColor root = uiBackgroundColorForIndex(ui_theme_index_);
    const bool light = root.lightness() > 150;
    const QColor white(255, 255, 255);
    const QColor black(0, 0, 0);
    const QColor accent(79, 130, 255);

    const QColor bar = light ? blendColor(root, white, 0.32) : blendColor(root, white, 0.06);
    const QColor panel = light ? blendColor(root, white, 0.48) : blendColor(root, white, 0.10);
    const QColor shell = light ? blendColor(root, black, 0.06) : blendColor(root, black, 0.12);
    const QColor control = light ? blendColor(root, white, 0.70) : blendColor(root, white, 0.16);
    const QColor control_hover = light ? blendColor(root, white, 0.82) : blendColor(root, white, 0.22);
    const QColor control_pressed = light ? blendColor(root, black, 0.04) : blendColor(root, white, 0.11);
    const QColor input = light ? blendColor(root, white, 0.78) : blendColor(root, black, 0.08);
    const QColor input_hover = light ? blendColor(root, white, 0.90) : blendColor(root, white, 0.08);
    const QColor border = light ? blendColor(root, black, 0.20) : blendColor(root, white, 0.18);
    const QColor border_hover = light ? blendColor(root, black, 0.30) : blendColor(root, white, 0.30);
    const QColor text = light ? QColor(23, 31, 43) : QColor(220, 229, 239);
    const QColor title = light ? QColor(11, 18, 29) : QColor(245, 248, 251);
    const QColor muted = light ? QColor(82, 94, 110) : QColor(144, 162, 184);
    const QColor subtle = light ? QColor(103, 114, 130) : QColor(127, 146, 169);
    const QColor send_bg = light ? QColor(28, 38, 52) : QColor(238, 243, 248);
    const QColor send_text = light ? QColor(248, 251, 255) : QColor(13, 20, 29);
    const QColor slider_track = light ? blendColor(root, black, 0.12) : blendColor(root, white, 0.12);
    const QColor slider_handle = light ? QColor(29, 39, 52) : QColor(244, 247, 251);
    QString image_style;
    if (ui_theme_index_ == 7 && !ui_background_image_path_.isEmpty()) {
        QString image_path = QDir::fromNativeSeparators(ui_background_image_path_);
        image_path.replace(QStringLiteral("\\"), QStringLiteral("/"));
        image_style = QStringLiteral(R"(
QWidget#WorkspaceRoot {
    border-image: url("%1") 0 0 0 0 stretch stretch;
}
QFrame#WorkspaceBar,
QFrame#ViewportHeader,
QFrame#ViewportFooter,
QFrame[panelSection="true"],
QFrame[metricBlock="true"] {
    background: rgba(%2, %3, %4, %5);
}
QScrollArea,
QScrollArea > QWidget > QWidget {
    background: transparent;
}
        )")
            .arg(image_path)
            .arg(panel.red())
            .arg(panel.green())
            .arg(panel.blue())
            .arg(light ? 226 : 232);
    }

    QString theme_style = QStringLiteral(R"(
QMainWindow, QWidget#WorkspaceRoot {
    background: @root;
    color: @text;
}

QFrame#WorkspaceBar {
    background: @bar;
    border-bottom: 1px solid @border;
}

QLabel#AppNameLabel,
QLabel[panelTitle="true"],
QLabel[sectionTitle="true"],
QLabel[metricValue="true"],
QLabel[candidateTitle="true"] {
    color: @title;
}

QLabel#WorkspaceMetaLabel,
QLabel[panelSubtitle="true"],
QLabel#FooterHintLabel,
QLabel#FooterNoteLabel,
QLabel[candidateReason="true"] {
    color: @muted;
}

QLabel[panelEyebrow="true"],
QLabel[fieldLabel="true"],
QLabel[metricLabel="true"],
QLabel[candidateSlot="true"] {
    color: @subtle;
}

QLabel[valueText="primary"],
QLabel[candidateSummary="true"],
QCheckBox {
    color: @text;
}

QFrame#ViewportHeader,
QFrame#ViewportFooter,
QFrame#InspectorPane,
QFrame[panelSection="true"],
QFrame[metricBlock="true"] {
    background: @panel;
    border: 1px solid @border;
}

QFrame#ViewportShell,
QFrame#ViewportShell > QOpenGLWidget {
    background: @shell;
    border-color: @border;
}

QToolButton#ActionButton,
QPushButton#ActionButton {
    background: @control;
    color: @text;
    border: 1px solid @border;
}

QToolButton#ActionButton:hover,
QPushButton#ActionButton:hover {
    background: @controlHover;
    border-color: @borderHover;
}

QToolButton#ActionButton:pressed,
QPushButton#ActionButton:pressed {
    background: @controlPressed;
}

QComboBox,
QPlainTextEdit,
QTextBrowser,
QFrame#AiComposer,
QFrame[candidateCard="true"] {
    background: @input;
    color: @text;
    border: 1px solid @border;
}

QTextBrowser#AiChatView {
    background: @input;
    border: 1px solid @border;
}

QComboBox:hover,
QPlainTextEdit:hover,
QFrame#AiComposer:hover {
    background: @inputHover;
    border-color: @borderHover;
}

QPlainTextEdit#AiComposerInput {
    background: transparent;
    color: @text;
}

QPushButton#AiSendButton {
    background: @sendBg;
    color: @sendText;
}

QPushButton#AiSendButton:hover {
    background: @sendHover;
}

QSlider::groove:horizontal {
    background: @sliderTrack;
}

QSlider::sub-page:horizontal,
QCheckBox::indicator:checked {
    background: @accent;
    border-color: @accent;
}

QSlider::handle:horizontal {
    background: @sliderHandle;
}

QCheckBox::indicator {
    background: @input;
    border: 1px solid @border;
}

QSplitter::handle {
    background: @root;
}

QStatusBar {
    background: @bar;
    color: @muted;
    border-top: 1px solid @border;
}
    )");
    const auto set_color = [&theme_style](const QString& token, const QColor& color) {
        theme_style.replace(token, cssColor(color));
    };
    set_color(QStringLiteral("@root"), root);
    set_color(QStringLiteral("@bar"), bar);
    set_color(QStringLiteral("@panel"), panel);
    set_color(QStringLiteral("@shell"), shell);
    set_color(QStringLiteral("@controlHover"), control_hover);
    set_color(QStringLiteral("@controlPressed"), control_pressed);
    set_color(QStringLiteral("@control"), control);
    set_color(QStringLiteral("@inputHover"), input_hover);
    set_color(QStringLiteral("@input"), input);
    set_color(QStringLiteral("@borderHover"), border_hover);
    set_color(QStringLiteral("@border"), border);
    set_color(QStringLiteral("@title"), title);
    set_color(QStringLiteral("@muted"), muted);
    set_color(QStringLiteral("@subtle"), subtle);
    set_color(QStringLiteral("@text"), text);
    set_color(QStringLiteral("@sendHover"), light ? blendColor(send_bg, black, 0.08) : white);
    set_color(QStringLiteral("@sendBg"), send_bg);
    set_color(QStringLiteral("@sendText"), send_text);
    set_color(QStringLiteral("@sliderTrack"), slider_track);
    set_color(QStringLiteral("@sliderHandle"), slider_handle);
    set_color(QStringLiteral("@accent"), accent);

    setStyleSheet(base_style + theme_style + image_style);
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("HaoRender-GI"));
    setMinimumSize(900, 620);
    applyWorkspaceTheme();

    render_workspace_action_ = new QAction(style()->standardIcon(QStyle::SP_ComputerIcon), QStringLiteral("Render"), this);
    open_model_action_ = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("Open"), this);
    asset_converter_action_ = new QAction(style()->standardIcon(QStyle::SP_DriveHDIcon), QStringLiteral("Convert"), this);
    open_rig_ai_action_ = new QAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("Rig AI"), this);
    reset_camera_action_ = new QAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Reset"), this);
    save_snapshot_action_ = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("Snapshot"), this);

    connect(open_model_action_, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("Open Scene / Model"),
            current_model_path_.isEmpty() ? findDefaultModelPath() : current_model_path_,
            QStringLiteral("Scene / Model Files (*.xml *.obj *.fbx *.vrm *.vrma *.gltf *.glb *.dae *.3ds *.stl *.ply *.x *.pmx);;Mitsuba Scene (*.xml);;Model Files (*.obj *.fbx *.vrm *.gltf *.glb *.dae *.3ds *.stl *.ply *.x *.pmx);;Animation Files (*.vrma *.fbx *.glb);;All Files (*.*)"));
        if (!path.isEmpty()) {
            if (QFileInfo(path).suffix().compare(QStringLiteral("vrma"), Qt::CaseInsensitive) == 0) {
                if (!current_model_path_.isEmpty() && !scene_.empty()) {
                    previewRigAnimationWithAutoMapping(current_model_path_, path);
                    showRenderWorkspace();
                } else {
                    openRigAiWindow();
                    statusBar()->showMessage(QStringLiteral("Load a target character first, then apply the VRMA animation."), 6000);
                }
                return;
            }
            loadModel(path);
            showRenderWorkspace();
        }
    });
    connect(render_workspace_action_, &QAction::triggered, this, [this]() { showRenderWorkspace(); });
    connect(asset_converter_action_, &QAction::triggered, this, [this]() { openAssetConverterWindow(); });
    connect(open_rig_ai_action_, &QAction::triggered, this, [this]() { openRigAiWindow(); });
    connect(reset_camera_action_, &QAction::triggered, this, [this]() { viewport_->resetCamera(); });
    connect(save_snapshot_action_, &QAction::triggered, this, [this]() { saveSnapshot(); });

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("WorkspaceRoot"));
    auto* root_layout = new QVBoxLayout(central);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    auto* workspace_bar = new QFrame(central);
    workspace_bar->setObjectName(QStringLiteral("WorkspaceBar"));
    auto* workspace_layout = new QHBoxLayout(workspace_bar);
    workspace_layout->setContentsMargins(18, 12, 18, 12);
    workspace_layout->setSpacing(10);

    auto* branding_layout = new QVBoxLayout();
    branding_layout->setSpacing(2);
    auto* app_name_label = new QLabel(QStringLiteral("HaoRender-GI"), workspace_bar);
    app_name_label->setObjectName(QStringLiteral("AppNameLabel"));
    workspace_subtitle_label_ = new QLabel(QStringLiteral("OpenGL render workbench"), workspace_bar);
    workspace_subtitle_label_->setObjectName(QStringLiteral("WorkspaceMetaLabel"));
    branding_layout->addWidget(app_name_label);
    branding_layout->addWidget(workspace_subtitle_label_);

    workspace_layout->addLayout(branding_layout, 0);
    workspace_layout->addWidget(makeActionButton(render_workspace_action_, workspace_bar));
    workspace_layout->addWidget(makeActionButton(open_model_action_, workspace_bar));
    workspace_layout->addWidget(makeActionButton(asset_converter_action_, workspace_bar));
    workspace_layout->addWidget(makeActionButton(open_rig_ai_action_, workspace_bar));
    workspace_layout->addWidget(makeActionButton(reset_camera_action_, workspace_bar));
    workspace_layout->addWidget(makeActionButton(save_snapshot_action_, workspace_bar));
    workspace_layout->addStretch(1);
    root_layout->addWidget(workspace_bar);
    workspace_stack_ = new QStackedWidget(central);
    workspace_stack_->setObjectName(QStringLiteral("WorkspaceStack"));

    auto* workspace_body = new QWidget(central);
    render_workspace_page_ = workspace_body;
    auto* workspace_body_layout = new QHBoxLayout(workspace_body);
    workspace_body_layout->setContentsMargins(0, 0, 0, 0);
    workspace_body_layout->setSpacing(0);

    auto* viewport_pane = new QWidget();
    viewport_pane->setMinimumWidth(0);
    viewport_pane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* viewport_layout = new QVBoxLayout(viewport_pane);
    viewport_layout->setContentsMargins(18, 18, 12, 18);
    viewport_layout->setSpacing(12);

    auto* viewport_header = new QFrame(viewport_pane);
    viewport_header->setObjectName(QStringLiteral("ViewportHeader"));
    auto* viewport_header_layout = new QHBoxLayout(viewport_header);
    viewport_header_layout->setContentsMargins(14, 12, 14, 12);
    viewport_header_layout->setSpacing(12);

    auto* viewport_title_stack = new QVBoxLayout();
    viewport_title_stack->setSpacing(2);
    auto* viewport_eyebrow = makeLabel(QStringLiteral("SCENE VIEW"), "panelEyebrow", "true", viewport_header);
    viewport_title_label_ = makeLabel(QStringLiteral("No asset loaded"), "panelTitle", "true", viewport_header);
    viewport_subtitle_label_ = makeLabel(QStringLiteral("OpenGL Rasterizer | PBR"), "panelSubtitle", "true", viewport_header);
    viewport_title_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    viewport_subtitle_label_->setWordWrap(true);
    viewport_subtitle_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    viewport_title_stack->addWidget(viewport_eyebrow);
    viewport_title_stack->addWidget(viewport_title_label_);
    viewport_title_stack->addWidget(viewport_subtitle_label_);
    play_pause_button_ = new QPushButton(QStringLiteral("Play"), viewport_header);
    play_pause_button_->setObjectName(QStringLiteral("ActionButton"));
    play_pause_button_->setEnabled(false);
    restart_animation_button_ = new QPushButton(QStringLiteral("Restart"), viewport_header);
    restart_animation_button_->setObjectName(QStringLiteral("ActionButton"));
    restart_animation_button_->setEnabled(false);
    auto* playback_row = new QHBoxLayout();
    playback_row->setContentsMargins(0, 6, 0, 0);
    playback_row->setSpacing(8);
    playback_row->addWidget(play_pause_button_);
    playback_row->addWidget(restart_animation_button_);
    playback_row->addStretch(1);
    viewport_title_stack->addLayout(playback_row);

    viewport_header_layout->addLayout(viewport_title_stack, 1);
    auto* frame_metric = makeMetricBlock(QStringLiteral("Frame"), &frame_value_, viewport_header);
    auto* shadow_metric = makeMetricBlock(QStringLiteral("Shadow"), &shadow_value_, viewport_header);
    auto* main_metric = makeMetricBlock(QStringLiteral("Main"), &main_value_, viewport_header);
    frame_metric->setVisible(false);
    shadow_metric->setVisible(false);
    main_metric->setVisible(false);
    viewport_header_layout->addWidget(frame_metric);
    viewport_header_layout->addWidget(shadow_metric);
    viewport_header_layout->addWidget(main_metric);
    viewport_layout->addWidget(viewport_header);

    auto* viewport_shell = new QFrame(viewport_pane);
    viewport_shell->setObjectName(QStringLiteral("ViewportShell"));
    auto* viewport_shell_layout = new QVBoxLayout(viewport_shell);
    viewport_shell_layout->setContentsMargins(1, 1, 1, 1);
    viewport_shell_layout->setSpacing(0);

    viewport_ = new RenderViewport(viewport_shell);
    viewport_->setClearColor(viewport_background_color_);
    viewport_->setStatsCallback([this](const RenderStats& stats) { updateRenderStats(stats); });
    viewport_shell_layout->addWidget(viewport_);
    viewport_layout->addWidget(viewport_shell, 1);

    auto* viewport_footer = new QFrame(viewport_pane);
    viewport_footer->setObjectName(QStringLiteral("ViewportFooter"));
    auto* viewport_footer_layout = new QHBoxLayout(viewport_footer);
    viewport_footer_layout->setContentsMargins(14, 10, 14, 10);
    viewport_footer_layout->setSpacing(12);

    auto* footer_hint_label = new QLabel(QStringLiteral("LMB orbit   RMB pan   Wheel zoom"), viewport_footer);
    footer_hint_label->setObjectName(QStringLiteral("FooterHintLabel"));
    note_value_ = new QLabel(QStringLiteral("Interactive preview ready"), viewport_footer);
    note_value_->setObjectName(QStringLiteral("FooterNoteLabel"));
    note_value_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    note_value_->setMinimumWidth(260);

    viewport_footer_layout->addWidget(footer_hint_label, 1);
    viewport_footer_layout->addWidget(note_value_);
    viewport_layout->addWidget(viewport_footer);

    auto* inspector_scroll = new QScrollArea();
    inspector_scroll->setWidgetResizable(true);
    inspector_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* inspector_root = new QWidget(inspector_scroll);
    auto* inspector_layout = new QVBoxLayout(inspector_root);
    inspector_layout->setContentsMargins(0, 18, 18, 18);
    inspector_layout->setSpacing(12);
    inspector_layout->setAlignment(Qt::AlignTop);
    inspector_layout->setSizeConstraint(QLayout::SetMinimumSize);

    auto* inspector_pane = new QFrame(inspector_root);
    inspector_pane->setObjectName(QStringLiteral("InspectorPane"));
    inspector_pane->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* inspector_pane_layout = new QVBoxLayout(inspector_pane);
    inspector_pane_layout->setContentsMargins(16, 16, 16, 16);
    inspector_pane_layout->setSpacing(12);
    inspector_pane_layout->setAlignment(Qt::AlignTop);
    inspector_pane_layout->setSizeConstraint(QLayout::SetMinimumSize);

    auto* inspector_header = new QVBoxLayout();
    inspector_header->setSpacing(2);
    auto* inspector_title = makeLabel(QStringLiteral("INSPECTOR"), "panelEyebrow", "true", inspector_pane);
    auto* inspector_subtitle = makeLabel(QStringLiteral("Reference-grade look-dev controls"), "panelSubtitle", "true", inspector_pane);
    inspector_header->addWidget(inspector_title);
    inspector_header->addWidget(inspector_subtitle);
    inspector_pane_layout->addLayout(inspector_header);

    QVBoxLayout* settings_layout = nullptr;
    auto* settings_section = makeInspectorSection(QStringLiteral("Settings"), inspector_pane, &settings_layout);
    language_combo_ = new QComboBox(settings_section);
    language_combo_->addItems({
        QStringLiteral("English"),
        QStringLiteral("Chinese")
    });
    language_combo_->setCurrentIndex(ui_language_ == UiLanguage::Chinese ? 1 : 0);
    addField(settings_layout, QStringLiteral("Language"), language_combo_);
    background_combo_ = new QComboBox(settings_section);
    background_combo_->addItems({
        QStringLiteral("Studio Dark"),
        QStringLiteral("Deep Black"),
        QStringLiteral("Neutral Gray"),
        QStringLiteral("Paper White"),
        QStringLiteral("Warm Beige"),
        QStringLiteral("Cool Blue"),
        QStringLiteral("Forest Green"),
        QStringLiteral("Image...")
    });
    background_combo_->setCurrentIndex(0);
    addField(settings_layout, QStringLiteral("UI background"), background_combo_);
    ui_background_image_button_ = new QPushButton(QStringLiteral("Choose UI Image"), settings_section);
    ui_background_image_button_->setObjectName(QStringLiteral("ActionButton"));
    settings_layout->addWidget(ui_background_image_button_);
    viewport_background_combo_ = new QComboBox(settings_section);
    viewport_background_combo_->addItems({
        QStringLiteral("Studio Dark"),
        QStringLiteral("Deep Black"),
        QStringLiteral("Neutral Gray"),
        QStringLiteral("Paper White"),
        QStringLiteral("Warm Beige"),
        QStringLiteral("Cool Blue"),
        QStringLiteral("Forest Green"),
        QStringLiteral("Image...")
    });
    viewport_background_combo_->setCurrentIndex(0);
    addField(settings_layout, QStringLiteral("Viewport background"), viewport_background_combo_);
    viewport_background_image_button_ = new QPushButton(QStringLiteral("Choose Viewport Image"), settings_section);
    viewport_background_image_button_->setObjectName(QStringLiteral("ActionButton"));
    settings_layout->addWidget(viewport_background_image_button_);
    inspector_pane_layout->addWidget(settings_section);

    QVBoxLayout* ai_layout = nullptr;
    auto* ai_section = makeInspectorSection(QStringLiteral("LookDev AI"), inspector_pane, &ai_layout);
    const LookDevLlmConfig env_llm_config = lookDevLlmConfigFromEnvironment();

    ai_chat_view_ = new QTextBrowser(ai_section);
    ai_chat_view_->setObjectName(QStringLiteral("AiChatView"));
    ai_chat_view_->setReadOnly(true);
    ai_chat_view_->setOpenExternalLinks(false);
    ai_chat_view_->setFrameShape(QFrame::NoFrame);
    ai_chat_view_->document()->setDocumentMargin(10);
    ai_chat_view_->setMinimumHeight(220);
    ai_chat_view_->setMaximumHeight(320);
    ai_layout->addWidget(ai_chat_view_);

    auto* ai_composer = new QFrame(ai_section);
    ai_composer->setObjectName(QStringLiteral("AiComposer"));
    auto* ai_composer_layout = new QVBoxLayout(ai_composer);
    ai_composer_layout->setContentsMargins(6, 6, 6, 6);
    ai_composer_layout->setSpacing(6);

    ai_prompt_edit_ = new QPlainTextEdit(ai_composer);
    ai_prompt_edit_->setObjectName(QStringLiteral("AiComposerInput"));
    ai_prompt_edit_->setPlaceholderText(QStringLiteral("Ask for a rendering look..."));
    ai_prompt_edit_->setFixedHeight(78);
    ai_composer_layout->addWidget(ai_prompt_edit_);

    auto* ai_button_row = new QHBoxLayout();
    ai_button_row->setContentsMargins(4, 0, 4, 0);
    ai_button_row->setSpacing(8);
    ai_save_preset_button_ = new QPushButton(QStringLiteral("Save"), ai_composer);
    ai_save_preset_button_->setObjectName(QStringLiteral("ActionButton"));
    ai_load_preset_button_ = new QPushButton(QStringLiteral("Load"), ai_composer);
    ai_load_preset_button_->setObjectName(QStringLiteral("ActionButton"));
    ai_recommend_button_ = new QPushButton(QStringLiteral("Send"), ai_composer);
    ai_recommend_button_->setObjectName(QStringLiteral("AiSendButton"));
    ai_button_row->addWidget(ai_save_preset_button_);
    ai_button_row->addWidget(ai_load_preset_button_);
    ai_button_row->addStretch(1);
    ai_button_row->addWidget(ai_recommend_button_);
    ai_composer_layout->addLayout(ai_button_row);
    ai_layout->addWidget(ai_composer);

    ai_api_key_edit_ = new QLineEdit(ai_section);
    ai_api_key_edit_->setEchoMode(QLineEdit::Password);
    ai_api_key_edit_->setMinimumWidth(0);
    ai_api_key_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ai_api_key_edit_->setPlaceholderText(env_llm_config.api_key.trimmed().isEmpty()
        ? QStringLiteral("No local key found; paste once or create llm.env.local")
        : QStringLiteral("Loaded from local config; leave blank to use it"));
    addField(ai_layout, QStringLiteral("API key"), ai_api_key_edit_);

    ai_base_url_edit_ = new QLineEdit(env_llm_config.base_url.isEmpty() ? defaultDoubaoBaseUrl() : env_llm_config.base_url, ai_section);
    ai_base_url_edit_->setMinimumWidth(0);
    ai_base_url_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ai_base_url_edit_->setPlaceholderText(defaultDoubaoBaseUrl());
    addField(ai_layout, QStringLiteral("Base URL"), ai_base_url_edit_);

    ai_model_edit_ = new QLineEdit(env_llm_config.model.isEmpty() ? defaultDoubaoModel() : env_llm_config.model, ai_section);
    ai_model_edit_->setMinimumWidth(0);
    ai_model_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ai_model_edit_->setPlaceholderText(QStringLiteral("Doubao model name or Ark endpoint id"));
    addField(ai_layout, QStringLiteral("Model"), ai_model_edit_);

    ai_reply_label_ = makeLabel(QString(), "candidateReason", "true", ai_section);
    ai_reply_label_->setVisible(false);
    appendAiSystemMessage(uiText(QStringLiteral("Ready. Describe the target look; I will ask follow-ups or generate one tuned preset.")));

    for (int i = 0; i < 3; ++i) {
        ai_candidate_cards_[i] = makeCandidateCard(ai_section,
                                                   &ai_candidate_slot_labels_[i],
                                                   &ai_candidate_name_labels_[i],
                                                   &ai_candidate_summary_labels_[i],
                                                   &ai_candidate_reason_labels_[i],
                                                   &ai_candidate_apply_buttons_[i]);
        ai_candidate_cards_[i]->setVisible(i == 0);
        ai_layout->addWidget(ai_candidate_cards_[i]);
    }

    QVBoxLayout* asset_layout = nullptr;
    auto* asset_section = makeInspectorSection(QStringLiteral("Asset"), inspector_pane, &asset_layout);
    model_path_value_ = makeLabel(QStringLiteral("No asset loaded"), "valueText", "primary", asset_section);
    model_path_value_->setWordWrap(true);
    addField(asset_layout, QStringLiteral("Current model"), model_path_value_);

    auto* asset_metrics = new QGridLayout();
    asset_metrics->setHorizontalSpacing(18);
    asset_metrics->setVerticalSpacing(4);
    auto* meshes_label = makeLabel(QStringLiteral("Meshes"), "fieldLabel", "true", asset_section);
    auto* triangles_label = makeLabel(QStringLiteral("Triangles"), "fieldLabel", "true", asset_section);
    auto* vertices_label = makeLabel(QStringLiteral("Vertices"), "fieldLabel", "true", asset_section);
    mesh_value_ = makeLabel(QStringLiteral("0"), "metricValue", "true", asset_section);
    triangle_value_ = makeLabel(QStringLiteral("0"), "metricValue", "true", asset_section);
    vertex_value_ = makeLabel(QStringLiteral("0"), "metricValue", "true", asset_section);
    asset_metrics->addWidget(meshes_label, 0, 0);
    asset_metrics->addWidget(triangles_label, 0, 1);
    asset_metrics->addWidget(vertices_label, 0, 2);
    asset_metrics->addWidget(mesh_value_, 1, 0);
    asset_metrics->addWidget(triangle_value_, 1, 1);
    asset_metrics->addWidget(vertex_value_, 1, 2);
    asset_layout->addLayout(asset_metrics);
    inspector_pane_layout->addWidget(asset_section);

    QVBoxLayout* eye_expression_layout = nullptr;
    eye_expression_section_ = makeInspectorSection(QStringLiteral("Eye / Expression Debug"), inspector_pane, &eye_expression_layout);
    eye_expression_empty_label_ = makeLabel(QStringLiteral("No VRM eye expressions found."), "fieldLabel", "true", eye_expression_section_);
    eye_expression_empty_label_->setWordWrap(true);
    eye_expression_layout->addWidget(eye_expression_empty_label_);

    auto* eye_expression_button_row = new QHBoxLayout();
    eye_expression_button_row->setContentsMargins(0, 0, 0, 0);
    eye_expression_button_row->setSpacing(8);
    eye_expression_reset_button_ = new QPushButton(QStringLiteral("Reset Expressions"), eye_expression_section_);
    eye_expression_reset_button_->setObjectName(QStringLiteral("ActionButton"));
    eye_expression_blink_curve_button_ = new QPushButton(QStringLiteral("Soft Blink Curve"), eye_expression_section_);
    eye_expression_blink_curve_button_->setObjectName(QStringLiteral("ActionButton"));
    eye_expression_button_row->addWidget(eye_expression_reset_button_);
    eye_expression_button_row->addWidget(eye_expression_blink_curve_button_);
    eye_expression_layout->addLayout(eye_expression_button_row);

    eye_gaze_yaw_slider_ = new SliderField(-30.0, 30.0, 1.0, 0, 0.0, eye_expression_section_);
    eye_gaze_pitch_slider_ = new SliderField(-20.0, 20.0, 1.0, 0, 0.0, eye_expression_section_);
    eye_gaze_weight_slider_ = new SliderField(0.0, 1.0, 0.01, 2, 1.0, eye_expression_section_);
    addField(eye_expression_layout, QStringLiteral("gaze yaw"), eye_gaze_yaw_slider_);
    addField(eye_expression_layout, QStringLiteral("gaze pitch"), eye_gaze_pitch_slider_);
    addField(eye_expression_layout, QStringLiteral("gaze weight"), eye_gaze_weight_slider_);

    eye_expression_controls_layout_ = new QVBoxLayout();
    eye_expression_controls_layout_->setContentsMargins(0, 0, 0, 0);
    eye_expression_controls_layout_->setSpacing(7);
    eye_expression_controls_layout_->setAlignment(Qt::AlignTop);
    eye_expression_layout->addLayout(eye_expression_controls_layout_);
    inspector_pane_layout->addWidget(eye_expression_section_);

    QVBoxLayout* lookdev_layout = nullptr;
    auto* lookdev_section = makeInspectorSection(QStringLiteral("Look Dev"), inspector_pane, &lookdev_layout);
    render_pipeline_combo_ = new QComboBox(lookdev_section);
    render_pipeline_combo_->addItems({
        QStringLiteral("Raster"),
        QStringLiteral("OpenGL Ray Trace"),
        QStringLiteral("DXR Hardware RT")
    });
    render_pipeline_combo_->setCurrentIndex(static_cast<int>(render_pipeline_));
    addField(lookdev_layout, QStringLiteral("Render path"), render_pipeline_combo_);

    shading_combo_ = new QComboBox(lookdev_section);
    shading_combo_->addItems({ QStringLiteral("PBR"), QStringLiteral("Phong") });
    shading_combo_->setCurrentIndex(look_settings_.shading_model == ShadingModel::Phong ? 1 : 0);
    addField(lookdev_layout, QStringLiteral("Shading model"), shading_combo_);

    exposure_slider_ = new SliderField(0.1, 3.0, 0.05, 2, look_settings_.exposure, lookdev_section);
    addField(lookdev_layout, QStringLiteral("Exposure"), exposure_slider_);

    normal_strength_slider_ = new SliderField(0.0, 2.0, 0.05, 2, look_settings_.normal_strength, lookdev_section);
    addField(lookdev_layout, QStringLiteral("Normal intensity"), normal_strength_slider_);
    inspector_pane_layout->addWidget(lookdev_section);
    inspector_pane_layout->addWidget(ai_section);

    shading_detail_stack_ = new CurrentPageStackedWidget(inspector_pane);
    shading_detail_stack_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    QVBoxLayout* pbr_page_layout = nullptr;
    auto* pbr_page = makeModePage(shading_detail_stack_, &pbr_page_layout);
    QVBoxLayout* pbr_lighting_layout = nullptr;
    auto* pbr_lighting_section = makeInspectorSection(QStringLiteral("PBR Lighting"), pbr_page, &pbr_lighting_layout);
    ibl_enabled_check_ = new QCheckBox(QStringLiteral("Enable image based lighting"), pbr_lighting_section);
    ibl_enabled_check_->setChecked(look_settings_.pbr.ibl_enabled);
    pbr_lighting_layout->addWidget(ibl_enabled_check_);
    ibl_diffuse_slider_ = new SliderField(0.0, 2.0, 0.05, 2, look_settings_.pbr.ibl_diffuse_strength, pbr_lighting_section);
    addField(pbr_lighting_layout, QStringLiteral("IBL diffuse"), ibl_diffuse_slider_);
    ibl_specular_slider_ = new SliderField(0.0, 2.0, 0.05, 2, look_settings_.pbr.ibl_specular_strength, pbr_lighting_section);
    addField(pbr_lighting_layout, QStringLiteral("IBL specular"), ibl_specular_slider_);
    sky_light_slider_ = new SliderField(0.0, 2.0, 0.05, 2, look_settings_.pbr.sky_light_strength, pbr_lighting_section);
    addField(pbr_lighting_layout, QStringLiteral("Sky light"), sky_light_slider_);
    pbr_page_layout->addWidget(pbr_lighting_section);

    QVBoxLayout* pbr_channels_layout = nullptr;
    auto* pbr_channels_section = makeInspectorSection(QStringLiteral("PBR Channel Map"), pbr_page, &pbr_channels_layout);
    QStringList channel_items = { QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B"), QStringLiteral("A") };
    metallic_channel_combo_ = new QComboBox(pbr_channels_section);
    roughness_channel_combo_ = new QComboBox(pbr_channels_section);
    ao_channel_combo_ = new QComboBox(pbr_channels_section);
    emissive_channel_combo_ = new QComboBox(pbr_channels_section);
    for (QComboBox* combo : { metallic_channel_combo_, roughness_channel_combo_, ao_channel_combo_, emissive_channel_combo_ }) {
        combo->addItems(channel_items);
    }
    metallic_channel_combo_->setCurrentIndex(look_settings_.pbr.metallic_channel);
    roughness_channel_combo_->setCurrentIndex(look_settings_.pbr.roughness_channel);
    ao_channel_combo_->setCurrentIndex(look_settings_.pbr.ao_channel);
    emissive_channel_combo_->setCurrentIndex(look_settings_.pbr.emissive_channel);
    addField(pbr_channels_layout, QStringLiteral("Metallic channel"), metallic_channel_combo_);
    addField(pbr_channels_layout, QStringLiteral("Roughness channel"), roughness_channel_combo_);
    addField(pbr_channels_layout, QStringLiteral("AO channel"), ao_channel_combo_);
    addField(pbr_channels_layout, QStringLiteral("Emissive channel"), emissive_channel_combo_);
    pbr_page_layout->addWidget(pbr_channels_section);
    shading_detail_stack_->addWidget(pbr_page);

    QVBoxLayout* phong_page_layout = nullptr;
    auto* phong_page = makeModePage(shading_detail_stack_, &phong_page_layout);
    QVBoxLayout* phong_surface_layout = nullptr;
    auto* phong_surface_section = makeInspectorSection(QStringLiteral("Phong Surface"), phong_page, &phong_surface_layout);
    phong_hard_specular_check_ = new QCheckBox(QStringLiteral("Hard-edge specular"), phong_surface_section);
    phong_hard_specular_check_->setChecked(look_settings_.phong.hard_specular);
    phong_surface_layout->addWidget(phong_hard_specular_check_);
    phong_tonemap_check_ = new QCheckBox(QStringLiteral("Apply tone mapping"), phong_surface_section);
    phong_tonemap_check_->setChecked(look_settings_.phong.use_tonemap);
    phong_surface_layout->addWidget(phong_tonemap_check_);
    phong_primary_only_check_ = new QCheckBox(QStringLiteral("Primary light only"), phong_surface_section);
    phong_primary_only_check_->setChecked(look_settings_.phong.primary_light_only);
    phong_surface_layout->addWidget(phong_primary_only_check_);
    phong_secondary_slider_ = new SliderField(0.0, 1.5, 0.02, 2, look_settings_.phong.secondary_light_scale, phong_surface_section);
    addField(phong_surface_layout, QStringLiteral("Secondary light"), phong_secondary_slider_);
    phong_diffuse_slider_ = new SliderField(0.0, 2.0, 0.02, 2, look_settings_.phong.diffuse_strength, phong_surface_section);
    addField(phong_surface_layout, QStringLiteral("Diffuse strength"), phong_diffuse_slider_);
    phong_ambient_slider_ = new SliderField(0.0, 1.0, 0.01, 2, look_settings_.phong.ambient_strength, phong_surface_section);
    addField(phong_surface_layout, QStringLiteral("Ambient strength"), phong_ambient_slider_);
    phong_specular_slider_ = new SliderField(0.0, 2.0, 0.02, 2, look_settings_.phong.specular_strength, phong_surface_section);
    addField(phong_surface_layout, QStringLiteral("Specular strength"), phong_specular_slider_);
    phong_smoothness_slider_ = new SliderField(0.0, 1.0, 0.01, 2, look_settings_.phong.smoothness, phong_surface_section);
    addField(phong_surface_layout, QStringLiteral("Smoothness"), phong_smoothness_slider_);
    phong_specular_map_weight_slider_ = new SliderField(0.0, 2.0, 0.02, 2, look_settings_.phong.specular_map_weight, phong_surface_section);
    addField(phong_surface_layout, QStringLiteral("Specular map weight"), phong_specular_map_weight_slider_);
    phong_shininess_slider_ = new SliderField(4.0, 128.0, 2.0, 0, look_settings_.phong.shininess, phong_surface_section);
    addField(phong_surface_layout, QStringLiteral("Shininess"), phong_shininess_slider_);
    phong_page_layout->addWidget(phong_surface_section);

    QVBoxLayout* phong_color_layout = nullptr;
    auto* phong_color_section = makeInspectorSection(QStringLiteral("Phong Color Tuning"), phong_page, &phong_color_layout);
    phong_ambient_color_field_ = new ColorField(look_settings_.phong.ambient_color, phong_color_section);
    addField(phong_color_layout, QStringLiteral("Ambient color"), phong_ambient_color_field_);
    phong_specular_tint_field_ = new ColorField(look_settings_.phong.specular_tint, phong_color_section);
    addField(phong_color_layout, QStringLiteral("Specular tint"), phong_specular_tint_field_);
    phong_page_layout->addWidget(phong_color_section);

    QVBoxLayout* phong_rim_layout = nullptr;
    auto* phong_rim_section = makeInspectorSection(QStringLiteral("Rim Light"), phong_page, &phong_rim_layout);
    phong_rim_strength_slider_ = new SliderField(0.0, 2.0, 0.02, 2, look_settings_.phong.rim_strength, phong_rim_section);
    addField(phong_rim_layout, QStringLiteral("Rim strength"), phong_rim_strength_slider_);
    phong_rim_power_slider_ = new SliderField(0.25, 8.0, 0.25, 2, look_settings_.phong.rim_power, phong_rim_section);
    addField(phong_rim_layout, QStringLiteral("Rim power"), phong_rim_power_slider_);
    phong_rim_tint_field_ = new ColorField(look_settings_.phong.rim_tint, phong_rim_section);
    addField(phong_rim_layout, QStringLiteral("Rim tint"), phong_rim_tint_field_);
    phong_page_layout->addWidget(phong_rim_section);

    QVBoxLayout* phong_toon_layout = nullptr;
    auto* phong_toon_section = makeInspectorSection(QStringLiteral("Toon Shading"), phong_page, &phong_toon_layout);
    phong_toon_enabled_check_ = new QCheckBox(QStringLiteral("Enable toon shaping"), phong_toon_section);
    phong_toon_enabled_check_->setChecked(look_settings_.phong.toon.enabled);
    phong_toon_layout->addWidget(phong_toon_enabled_check_);
    phong_toon_steps_slider_ = new SliderField(2.0, 6.0, 1.0, 0, look_settings_.phong.toon.diffuse_steps, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Diffuse steps"), phong_toon_steps_slider_);
    phong_toon_softness_slider_ = new SliderField(0.0, 0.25, 0.01, 2, look_settings_.phong.toon.diffuse_softness, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Band softness"), phong_toon_softness_slider_);
    phong_toon_shadow_floor_slider_ = new SliderField(0.0, 0.8, 0.01, 2, look_settings_.phong.toon.shadow_floor, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Shadow floor"), phong_toon_shadow_floor_slider_);
    phong_toon_lit_floor_slider_ = new SliderField(0.0, 1.0, 0.01, 2, look_settings_.phong.toon.lit_floor, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Lit floor"), phong_toon_lit_floor_slider_);
    phong_toon_ramp_bias_slider_ = new SliderField(-0.5, 0.5, 0.01, 2, look_settings_.phong.toon.ramp_bias, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Ramp bias"), phong_toon_ramp_bias_slider_);
    phong_toon_ramp_contrast_slider_ = new SliderField(0.25, 2.5, 0.05, 2, look_settings_.phong.toon.ramp_contrast, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Ramp contrast"), phong_toon_ramp_contrast_slider_);
    phong_toon_shadow_map_strength_slider_ = new SliderField(0.0, 1.0, 0.01, 2, look_settings_.phong.toon.shadow_map_strength, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Shadow receive"), phong_toon_shadow_map_strength_slider_);
    phong_toon_shadow_threshold_slider_ = new SliderField(0.0, 1.0, 0.01, 2, look_settings_.phong.toon.shadow_threshold, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Shadow threshold"), phong_toon_shadow_threshold_slider_);
    phong_toon_shadow_softness_slider_ = new SliderField(0.0, 0.25, 0.01, 2, look_settings_.phong.toon.shadow_softness, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Shadow softness"), phong_toon_shadow_softness_slider_);
    phong_toon_shadow_tint_field_ = new ColorField(look_settings_.phong.toon.shadow_tint, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Shadow tint"), phong_toon_shadow_tint_field_);
    phong_toon_highlight_threshold_slider_ = new SliderField(0.0, 1.0, 0.01, 2, look_settings_.phong.toon.highlight_threshold, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Highlight threshold"), phong_toon_highlight_threshold_slider_);
    phong_toon_highlight_softness_slider_ = new SliderField(0.0, 0.25, 0.01, 2, look_settings_.phong.toon.highlight_softness, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Highlight softness"), phong_toon_highlight_softness_slider_);
    phong_toon_highlight_strength_slider_ = new SliderField(0.0, 2.0, 0.02, 2, look_settings_.phong.toon.highlight_strength, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Highlight strength"), phong_toon_highlight_strength_slider_);
    phong_toon_highlight_tint_field_ = new ColorField(look_settings_.phong.toon.highlight_tint, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Highlight tint"), phong_toon_highlight_tint_field_);
    phong_toon_rim_threshold_slider_ = new SliderField(0.0, 1.0, 0.01, 2, look_settings_.phong.toon.rim_threshold, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Rim threshold"), phong_toon_rim_threshold_slider_);
    phong_toon_rim_softness_slider_ = new SliderField(0.0, 0.25, 0.01, 2, look_settings_.phong.toon.rim_softness, phong_toon_section);
    addField(phong_toon_layout, QStringLiteral("Rim softness"), phong_toon_rim_softness_slider_);
    phong_page_layout->addWidget(phong_toon_section);

    QVBoxLayout* phong_material_toon_layout = nullptr;
    auto* phong_material_toon_section = makeInspectorSection(QStringLiteral("Material Toon Override"), phong_page, &phong_material_toon_layout);
    phong_toon_material_override_check_ = new QCheckBox(QStringLiteral("Enable material override"), phong_material_toon_section);
    phong_toon_material_override_check_->setChecked(look_settings_.phong.toon.material_override_enabled);
    phong_material_toon_layout->addWidget(phong_toon_material_override_check_);
    phong_toon_material_texture_strength_slider_ = new SliderField(0.0, 1.0, 0.01, 2, look_settings_.phong.toon.material_texture_strength, phong_material_toon_section);
    addField(phong_material_toon_layout, QStringLiteral("Texture influence"), phong_toon_material_texture_strength_slider_);
    phong_toon_material_lift_slider_ = new SliderField(0.0, 0.8, 0.01, 2, look_settings_.phong.toon.material_lift, phong_material_toon_section);
    addField(phong_material_toon_layout, QStringLiteral("Albedo lift"), phong_toon_material_lift_slider_);
    phong_toon_material_saturation_slider_ = new SliderField(0.0, 2.0, 0.05, 2, look_settings_.phong.toon.material_saturation, phong_material_toon_section);
    addField(phong_material_toon_layout, QStringLiteral("Albedo saturation"), phong_toon_material_saturation_slider_);
    phong_toon_material_contrast_slider_ = new SliderField(0.25, 2.5, 0.05, 2, look_settings_.phong.toon.material_contrast, phong_material_toon_section);
    addField(phong_material_toon_layout, QStringLiteral("Albedo contrast"), phong_toon_material_contrast_slider_);
    phong_page_layout->addWidget(phong_material_toon_section);

    QVBoxLayout* phong_outline_layout = nullptr;
    auto* phong_outline_section = makeInspectorSection(QStringLiteral("Outline"), phong_page, &phong_outline_layout);
    phong_outline_enabled_check_ = new QCheckBox(QStringLiteral("Enable silhouette outline"), phong_outline_section);
    phong_outline_enabled_check_->setChecked(look_settings_.phong.outline.enabled);
    phong_outline_layout->addWidget(phong_outline_enabled_check_);
    phong_outline_width_slider_ = new SliderField(0.0, 12.0, 0.25, 2, look_settings_.phong.outline.width_pixels, phong_outline_section);
    addField(phong_outline_layout, QStringLiteral("Width (px)"), phong_outline_width_slider_);
    phong_outline_opacity_slider_ = new SliderField(0.0, 1.0, 0.01, 2, look_settings_.phong.outline.opacity, phong_outline_section);
    addField(phong_outline_layout, QStringLiteral("Opacity"), phong_outline_opacity_slider_);
    phong_outline_depth_bias_slider_ = new SliderField(0.0, 0.02, 0.0005, 4, look_settings_.phong.outline.depth_bias, phong_outline_section);
    addField(phong_outline_layout, QStringLiteral("Depth bias"), phong_outline_depth_bias_slider_);
    phong_outline_color_field_ = new ColorField(look_settings_.phong.outline.color, phong_outline_section);
    addField(phong_outline_layout, QStringLiteral("Outline color"), phong_outline_color_field_);
    phong_page_layout->addWidget(phong_outline_section);
    shading_detail_stack_->addWidget(phong_page);

    inspector_pane_layout->addWidget(shading_detail_stack_);

    QVBoxLayout* ray_trace_layout = nullptr;
    ray_trace_section_ = makeInspectorSection(QStringLiteral("Ray Trace Preview"), inspector_pane, &ray_trace_layout);
    ray_trace_scene_combo_ = new QComboBox(ray_trace_section_);
    ray_trace_scene_combo_->addItems({
        QStringLiteral("Cornell Box"),
        QStringLiteral("Loaded Model")
    });
    ray_trace_scene_combo_->setCurrentIndex(static_cast<int>(look_settings_.ray_trace.scene_mode));
    addField(ray_trace_layout, QStringLiteral("Scene"), ray_trace_scene_combo_);
    ray_trace_integrator_combo_ = new QComboBox(ray_trace_section_);
    ray_trace_integrator_combo_->addItems({
        QStringLiteral("Hybrid RT"),
        QStringLiteral("Path Trace"),
        QStringLiteral("Path Trace + NEE"),
        QStringLiteral("Photon Path")
    });
    ray_trace_integrator_combo_->setCurrentIndex(static_cast<int>(look_settings_.ray_trace.integrator));
    addField(ray_trace_layout, QStringLiteral("Integrator"), ray_trace_integrator_combo_);
    ray_trace_view_combo_ = new QComboBox(ray_trace_section_);
    ray_trace_view_combo_->addItems({
        QStringLiteral("Lit"),
        QStringLiteral("Hit / Miss"),
        QStringLiteral("Normal"),
        QStringLiteral("Albedo")
    });
    ray_trace_view_combo_->setCurrentIndex(static_cast<int>(look_settings_.ray_trace.view_mode));
    addField(ray_trace_layout, QStringLiteral("Debug view"), ray_trace_view_combo_);
    ray_trace_ambient_slider_ = new SliderField(0.0, 0.5, 0.01, 2, look_settings_.ray_trace.ambient_strength, ray_trace_section_);
    addField(ray_trace_layout, QStringLiteral("Ambient"), ray_trace_ambient_slider_);
    ray_trace_shadow_slider_ = new SliderField(0.0, 1.0, 0.01, 2, look_settings_.ray_trace.shadow_strength, ray_trace_section_);
    addField(ray_trace_layout, QStringLiteral("Shadow strength"), ray_trace_shadow_slider_);
    ray_trace_bounces_slider_ = new SliderField(1.0, 24.0, 1.0, 0, static_cast<double>(look_settings_.ray_trace.max_bounces), ray_trace_section_);
    addField(ray_trace_layout, QStringLiteral("Max bounces"), ray_trace_bounces_slider_);
    ray_trace_nee_bounces_slider_ = new SliderField(0.0, 8.0, 1.0, 0, static_cast<double>(look_settings_.ray_trace.max_nee_bounces), ray_trace_section_);
    addField(ray_trace_layout, QStringLiteral("NEE bounces"), ray_trace_nee_bounces_slider_);
    ray_trace_spp_slider_ = new SliderField(1.0, 8.0, 1.0, 0, static_cast<double>(look_settings_.ray_trace.samples_per_frame), ray_trace_section_);
    addField(ray_trace_layout, QStringLiteral("Samples / frame"), ray_trace_spp_slider_);
    ray_trace_nee_check_ = new QCheckBox(QStringLiteral("Enable next-event estimation"), ray_trace_section_);
    ray_trace_nee_check_->setChecked(look_settings_.ray_trace.enable_nee);
    ray_trace_layout->addWidget(ray_trace_nee_check_);
    ray_trace_photon_check_ = new QCheckBox(QStringLiteral("Enable photon cache"), ray_trace_section_);
    ray_trace_photon_check_->setChecked(look_settings_.ray_trace.enable_photon_cache);
    ray_trace_layout->addWidget(ray_trace_photon_check_);
    ray_trace_photon_radius_slider_ = new SliderField(0.02, 1.2, 0.01, 2, look_settings_.ray_trace.photon_radius, ray_trace_section_);
    addField(ray_trace_layout, QStringLiteral("Photon radius"), ray_trace_photon_radius_slider_);
    ray_trace_photon_intensity_slider_ = new SliderField(0.0, 4.0, 0.05, 2, look_settings_.ray_trace.photon_intensity, ray_trace_section_);
    addField(ray_trace_layout, QStringLiteral("Photon intensity"), ray_trace_photon_intensity_slider_);
    inspector_pane_layout->addWidget(ray_trace_section_);

    QVBoxLayout* raster_layout = nullptr;
    auto* raster_section = makeInspectorSection(QStringLiteral("Raster Settings"), inspector_pane, &raster_layout);
    shadows_check_ = new QCheckBox(QStringLiteral("Enable shadows"), raster_section);
    shadows_check_->setChecked(look_settings_.enable_shadows);
    raster_layout->addWidget(shadows_check_);
    culling_check_ = new QCheckBox(QStringLiteral("Enable backface culling"), raster_section);
    culling_check_->setChecked(look_settings_.enable_backface_culling);
    raster_layout->addWidget(culling_check_);
    backend_value_ = makeLabel(QStringLiteral("OpenGL Rasterizer"), "valueText", "primary", raster_section);
    addField(raster_layout, QStringLiteral("Backend"), backend_value_);
    inspector_pane_layout->addWidget(raster_section);

    QVBoxLayout* capture_layout = nullptr;
    auto* capture_section = makeInspectorSection(QStringLiteral("Capture"), inspector_pane, &capture_layout);
    capture_layout->addWidget(makeActionPushButton(save_snapshot_action_, capture_section));
    capture_layout->addWidget(makeActionPushButton(reset_camera_action_, capture_section));
    capture_layout->addWidget(makeActionPushButton(open_model_action_, capture_section));
    inspector_pane_layout->addWidget(capture_section);

    inspector_layout->addWidget(inspector_pane);

    inspector_scroll->setWidget(inspector_root);
    inspector_scroll->setMinimumWidth(320);
    inspector_scroll->setMaximumWidth(420);
    inspector_scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    workspace_body_layout->addWidget(viewport_pane, 1);
    workspace_body_layout->addWidget(inspector_scroll, 0);

    workspace_stack_->addWidget(workspace_body);
    root_layout->addWidget(workspace_stack_, 1);
    setCentralWidget(central);

    connect(language_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        ui_language_ = index == 1 ? UiLanguage::Chinese : UiLanguage::English;
        refreshUiLanguage();
        updateSceneSummary();
        updateViewportChrome();
        refreshAiCandidateCards();
        statusBar()->showMessage(uiText(QStringLiteral("Workspace ready")), 3000);
    });
    connect(background_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        ui_theme_index_ = index;
        if (index == 7 && ui_background_image_path_.isEmpty()) {
            chooseUiBackgroundImage();
            return;
        }
        applyWorkspaceTheme();
        refreshUiLanguage();
        updateBackgroundControls();
        statusBar()->showMessage(uiText(QStringLiteral("Workspace ready")), 2000);
    });
    connect(ui_background_image_button_, &QPushButton::clicked, this, [this]() { chooseUiBackgroundImage(); });
    connect(viewport_background_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        viewport_background_index_ = index;
        if (index == 7) {
            if (viewport_background_image_path_.isEmpty()) {
                chooseViewportBackgroundImage();
                return;
            }
            QImage image(viewport_background_image_path_);
            if (!image.isNull() && viewport_) {
                viewport_->setBackgroundImage(image);
            }
        } else {
            viewport_background_color_ = uiBackgroundColorForIndex(index);
            if (viewport_) {
                viewport_->clearBackgroundImage();
                viewport_->setClearColor(viewport_background_color_);
            }
        }
        updateBackgroundControls();
        statusBar()->showMessage(uiText(QStringLiteral("Workspace ready")), 2000);
    });
    connect(viewport_background_image_button_, &QPushButton::clicked, this, [this]() { chooseViewportBackgroundImage(); });
    connect(render_pipeline_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        render_pipeline_ = renderPipelineFromIndex(index);
        if (viewport_) {
            viewport_->setRenderPipeline(render_pipeline_);
        }
        syncShadingModeUi();
        applyLookDevToViewport();
    });
    connect(shading_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        look_settings_.shading_model = shadingModelFromIndex(index);
        syncShadingModeUi();
        applyLookDevToViewport();
    });
    exposure_slider_->setOnValueChanged([this](double value) {
        look_settings_.exposure = static_cast<float>(value);
        applyLookDevToViewport();
    });
    normal_strength_slider_->setOnValueChanged([this](double value) {
        look_settings_.normal_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    connect(shadows_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.enable_shadows = checked;
        applyLookDevToViewport();
    });
    connect(culling_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.enable_backface_culling = checked;
        applyLookDevToViewport();
    });

    connect(ibl_enabled_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.pbr.ibl_enabled = checked;
        applyLookDevToViewport();
    });
    ibl_diffuse_slider_->setOnValueChanged([this](double value) {
        look_settings_.pbr.ibl_diffuse_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    ibl_specular_slider_->setOnValueChanged([this](double value) {
        look_settings_.pbr.ibl_specular_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    sky_light_slider_->setOnValueChanged([this](double value) {
        look_settings_.pbr.sky_light_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    connect(metallic_channel_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        look_settings_.pbr.metallic_channel = index;
        applyLookDevToViewport();
    });
    connect(roughness_channel_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        look_settings_.pbr.roughness_channel = index;
        applyLookDevToViewport();
    });
    connect(ao_channel_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        look_settings_.pbr.ao_channel = index;
        applyLookDevToViewport();
    });
    connect(emissive_channel_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        look_settings_.pbr.emissive_channel = index;
        applyLookDevToViewport();
    });

    connect(ray_trace_scene_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        look_settings_.ray_trace.scene_mode = rayTraceSceneModeFromIndex(index);
        applyLookDevToViewport();
    });
    connect(ray_trace_integrator_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        look_settings_.ray_trace.integrator = rayTraceIntegratorFromIndex(index);
        applyLookDevToViewport();
    });
    connect(ray_trace_view_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        look_settings_.ray_trace.view_mode = rayTraceViewModeFromIndex(index);
        applyLookDevToViewport();
    });
    ray_trace_ambient_slider_->setOnValueChanged([this](double value) {
        look_settings_.ray_trace.ambient_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    ray_trace_shadow_slider_->setOnValueChanged([this](double value) {
        look_settings_.ray_trace.shadow_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    ray_trace_bounces_slider_->setOnValueChanged([this](double value) {
        look_settings_.ray_trace.max_bounces = std::clamp(static_cast<int>(std::lround(value)), 1, 32);
        applyLookDevToViewport();
    });
    ray_trace_nee_bounces_slider_->setOnValueChanged([this](double value) {
        look_settings_.ray_trace.max_nee_bounces = std::clamp(static_cast<int>(std::lround(value)), 0, 16);
        applyLookDevToViewport();
    });
    ray_trace_spp_slider_->setOnValueChanged([this](double value) {
        look_settings_.ray_trace.samples_per_frame = std::clamp(static_cast<int>(std::lround(value)), 1, 16);
        applyLookDevToViewport();
    });
    connect(ray_trace_nee_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.ray_trace.enable_nee = checked;
        applyLookDevToViewport();
    });
    connect(ray_trace_photon_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.ray_trace.enable_photon_cache = checked;
        applyLookDevToViewport();
    });
    ray_trace_photon_radius_slider_->setOnValueChanged([this](double value) {
        look_settings_.ray_trace.photon_radius = static_cast<float>(value);
        applyLookDevToViewport();
    });
    ray_trace_photon_intensity_slider_->setOnValueChanged([this](double value) {
        look_settings_.ray_trace.photon_intensity = static_cast<float>(value);
        applyLookDevToViewport();
    });

    connect(phong_hard_specular_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.phong.hard_specular = checked;
        applyLookDevToViewport();
    });
    connect(phong_tonemap_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.phong.use_tonemap = checked;
        applyLookDevToViewport();
    });
    connect(phong_primary_only_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.phong.primary_light_only = checked;
        applyLookDevToViewport();
    });
    phong_secondary_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.secondary_light_scale = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_diffuse_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.diffuse_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_ambient_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.ambient_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_ambient_color_field_->setOnColorChanged([this](const QColor& color) {
        look_settings_.phong.ambient_color = color;
        applyLookDevToViewport();
    });
    phong_specular_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.specular_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_specular_tint_field_->setOnColorChanged([this](const QColor& color) {
        look_settings_.phong.specular_tint = color;
        applyLookDevToViewport();
    });
    phong_smoothness_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.smoothness = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_specular_map_weight_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.specular_map_weight = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_shininess_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.shininess = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_rim_strength_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.rim_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_rim_power_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.rim_power = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_rim_tint_field_->setOnColorChanged([this](const QColor& color) {
        look_settings_.phong.rim_tint = color;
        applyLookDevToViewport();
    });
    connect(phong_toon_enabled_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.phong.toon.enabled = checked;
        applyLookDevToViewport();
    });
    phong_toon_steps_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.diffuse_steps = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_softness_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.diffuse_softness = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_shadow_floor_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.shadow_floor = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_lit_floor_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.lit_floor = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_ramp_bias_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.ramp_bias = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_ramp_contrast_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.ramp_contrast = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_shadow_map_strength_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.shadow_map_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_shadow_threshold_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.shadow_threshold = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_shadow_softness_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.shadow_softness = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_shadow_tint_field_->setOnColorChanged([this](const QColor& color) {
        look_settings_.phong.toon.shadow_tint = color;
        applyLookDevToViewport();
    });
    phong_toon_highlight_threshold_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.highlight_threshold = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_highlight_softness_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.highlight_softness = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_highlight_strength_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.highlight_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_highlight_tint_field_->setOnColorChanged([this](const QColor& color) {
        look_settings_.phong.toon.highlight_tint = color;
        applyLookDevToViewport();
    });
    phong_toon_rim_threshold_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.rim_threshold = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_rim_softness_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.rim_softness = static_cast<float>(value);
        applyLookDevToViewport();
    });
    connect(phong_toon_material_override_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.phong.toon.material_override_enabled = checked;
        applyLookDevToViewport();
    });
    phong_toon_material_texture_strength_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.material_texture_strength = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_material_lift_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.material_lift = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_material_saturation_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.material_saturation = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_toon_material_contrast_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.toon.material_contrast = static_cast<float>(value);
        applyLookDevToViewport();
    });
    connect(phong_outline_enabled_check_, &QCheckBox::toggled, this, [this](bool checked) {
        look_settings_.phong.outline.enabled = checked;
        applyLookDevToViewport();
    });
    phong_outline_width_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.outline.width_pixels = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_outline_opacity_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.outline.opacity = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_outline_depth_bias_slider_->setOnValueChanged([this](double value) {
        look_settings_.phong.outline.depth_bias = static_cast<float>(value);
        applyLookDevToViewport();
    });
    phong_outline_color_field_->setOnColorChanged([this](const QColor& color) {
        look_settings_.phong.outline.color = color;
        applyLookDevToViewport();
    });
    connect(ai_recommend_button_, &QPushButton::clicked, this, [this]() { generateAiRecommendations(); });
    connect(ai_save_preset_button_, &QPushButton::clicked, this, [this]() { saveStylePreset(); });
    connect(ai_load_preset_button_, &QPushButton::clicked, this, [this]() { loadStylePreset(); });
    connect(play_pause_button_, &QPushButton::clicked, this, [this]() { toggleAnimationPlayback(); });
    connect(restart_animation_button_, &QPushButton::clicked, this, [this]() { restartAnimationPlayback(); });
    connect(eye_expression_reset_button_, &QPushButton::clicked, this, [this]() { resetEyeExpressionControls(); });
    connect(eye_expression_blink_curve_button_, &QPushButton::clicked, this, [this]() { applySoftBlinkExpressionCurve(); });
    eye_gaze_yaw_slider_->setOnValueChanged([this](double) { applyEyeGazeControl(); });
    eye_gaze_pitch_slider_->setOnValueChanged([this](double) { applyEyeGazeControl(); });
    eye_gaze_weight_slider_->setOnValueChanged([this](double) { applyEyeGazeControl(); });
    for (int i = 0; i < static_cast<int>(ai_candidate_apply_buttons_.size()); ++i) {
        connect(ai_candidate_apply_buttons_[i], &QPushButton::clicked, this, [this, i]() {
            if (i >= 0 && i < ai_candidates_.size()) {
                applyStylePreset(ai_candidates_.at(i).preset, true);
            }
        });
    }

    syncShadingModeUi();
    refreshEyeExpressionPanel();
    refreshAiCandidateCards();
    refreshUiLanguage();
    refreshPlaybackControls();
    updateBackgroundControls();
    applyLookDevToViewport();
    statusBar()->showMessage(uiText(QStringLiteral("Workspace ready")));
    updateSceneSummary();
    updateRenderStats(RenderStats{});
}

void MainWindow::applyLookDevToViewport() {
    if (viewport_) {
        viewport_->setRenderPipeline(render_pipeline_);
        viewport_->setClearColor(viewport_background_index_ == 7 ? QColor(0, 0, 0, 0) : viewport_background_color_);
        viewport_->setLookDevSettings(look_settings_);
    }
    updateViewportChrome();
}

void MainWindow::refreshEyeExpressionPanel() {
    if (!eye_expression_section_ || !eye_expression_controls_layout_) {
        return;
    }

    clearLayoutWidgets(eye_expression_controls_layout_);
    eye_expression_sliders_.clear();

    const QStringList expression_names = availableEyeExpressionNames(scene_);
    const bool has_gaze_solver = hasEyeGazeSolver(scene_);

    const bool has_expressions = !expression_names.empty();
    if (eye_expression_empty_label_) {
        eye_expression_empty_label_->setText(has_expressions
            ? QStringLiteral("%1 expressions ready").arg(expression_names.size())
            : uiText(QStringLiteral("No VRM eye expressions found.")));
        eye_expression_empty_label_->setVisible(true);
    }
    if (eye_expression_reset_button_) {
        eye_expression_reset_button_->setEnabled(has_expressions);
    }
    if (eye_expression_blink_curve_button_) {
        eye_expression_blink_curve_button_->setEnabled(
            std::any_of(expression_names.begin(), expression_names.end(), [](const QString& name) {
                return name.compare(QStringLiteral("blink"), Qt::CaseInsensitive) == 0 ||
                       name.compare(QStringLiteral("Fcl_EYE_Close"), Qt::CaseInsensitive) == 0;
            }));
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
        auto* slider = new SliderField(0.0, 1.0, 0.01, 2, 0.0, eye_expression_section_);
        addField(eye_expression_controls_layout_, eyeExpressionDisplayLabel(expression_name), slider);
        slider->setOnValueChanged([this, expression_name](double value) {
            applyExpressionWeight(expression_name, value);
        });
        eye_expression_sliders_.push_back(ExpressionSliderBinding{ expression_name, slider });
    }

    eye_expression_section_->setVisible(true);
    eye_expression_section_->updateGeometry();
}

void MainWindow::applyExpressionWeight(const QString& expression_name, double weight) {
    if (!viewport_) {
        return;
    }
    viewport_->setExpressionWeight(expression_name.toStdString(), static_cast<float>(weight));
}

void MainWindow::applyEyeGazeControl() {
    if (!viewport_ || !eye_gaze_yaw_slider_ || !eye_gaze_pitch_slider_ || !eye_gaze_weight_slider_) {
        return;
    }
    viewport_->setEyeGaze(static_cast<float>(eye_gaze_yaw_slider_->value()),
                          static_cast<float>(eye_gaze_pitch_slider_->value()),
                          static_cast<float>(eye_gaze_weight_slider_->value()));
}

void MainWindow::toggleAnimationPlayback() {
    if (!viewport_ || !scene_.hasAnimations()) {
        return;
    }
    viewport_->setAnimationPlaybackEnabled(!viewport_->animationPlaybackEnabled());
    refreshPlaybackControls();
}

void MainWindow::restartAnimationPlayback() {
    if (!viewport_ || !scene_.hasAnimations()) {
        return;
    }
    viewport_->restartAnimationPlayback();
    viewport_->setAnimationPlaybackEnabled(true);
    refreshPlaybackControls();
}

void MainWindow::refreshPlaybackControls() {
    const bool can_play = viewport_ && scene_.hasAnimations();
    if (play_pause_button_) {
        play_pause_button_->setVisible(can_play);
        play_pause_button_->setEnabled(can_play);
        play_pause_button_->setText(uiText(can_play && viewport_->animationPlaybackEnabled()
            ? QStringLiteral("Pause")
            : QStringLiteral("Play")));
    }
    if (restart_animation_button_) {
        restart_animation_button_->setVisible(can_play);
        restart_animation_button_->setEnabled(can_play);
        restart_animation_button_->setText(uiText(QStringLiteral("Restart")));
    }
}

void MainWindow::applyExpressionCurveFromAgent(const std::vector<ExpressionChannelData>& channels,
                                               double duration_seconds,
                                               const QString& summary) {
    if (!viewport_ || channels.empty() || duration_seconds <= 0.0) {
        return;
    }
    viewport_->setExpressionCurvePreview(channels, duration_seconds);
    for (const ExpressionSliderBinding& binding : eye_expression_sliders_) {
        if (binding.slider) {
            binding.slider->setValue(0.0);
        }
    }
    const QString message = summary.trimmed().isEmpty()
        ? QStringLiteral("Applied AI expression curve.")
        : QStringLiteral("Applied AI expression curve: %1").arg(summary.trimmed());
    appendAiSystemMessage(message);
    statusBar()->showMessage(message, 3500);
}

void MainWindow::applyGazeCurveFromAgent(const std::vector<EyeGazeKeyframeData>& keys,
                                         double duration_seconds,
                                         const QString& summary) {
    if (!viewport_ || keys.empty() || duration_seconds <= 0.0) {
        return;
    }
    viewport_->setEyeGazeCurvePreview(keys, duration_seconds);
    if (eye_gaze_yaw_slider_) {
        eye_gaze_yaw_slider_->setValue(0.0);
    }
    if (eye_gaze_pitch_slider_) {
        eye_gaze_pitch_slider_->setValue(0.0);
    }
    if (eye_gaze_weight_slider_) {
        eye_gaze_weight_slider_->setValue(1.0);
    }
    const QString message = summary.trimmed().isEmpty()
        ? QStringLiteral("Applied AI eye bone gaze curve.")
        : QStringLiteral("Applied AI eye bone gaze curve: %1").arg(summary.trimmed());
    appendAiSystemMessage(message);
    statusBar()->showMessage(message, 3500);
}

void MainWindow::resetEyeExpressionControls() {
    if (viewport_) {
        viewport_->clearExpressionCurvePreview();
        viewport_->clearExpressionWeights();
        viewport_->clearEyeGazeCurvePreview();
        viewport_->clearEyeGaze();
    }
    if (eye_gaze_yaw_slider_) {
        eye_gaze_yaw_slider_->setValue(0.0);
    }
    if (eye_gaze_pitch_slider_) {
        eye_gaze_pitch_slider_->setValue(0.0);
    }
    if (eye_gaze_weight_slider_) {
        eye_gaze_weight_slider_->setValue(1.0);
    }
    for (const ExpressionSliderBinding& binding : eye_expression_sliders_) {
        if (binding.slider) {
            binding.slider->setValue(0.0);
        }
    }
    statusBar()->showMessage(uiText(QStringLiteral("Expression controls reset.")), 2500);
}

void MainWindow::applySoftBlinkExpressionCurve() {
    if (!viewport_) {
        return;
    }

    const ExpressionCurvePlan plan = buildSoftBlinkCurve(scene_);
    if (!plan.valid()) {
        statusBar()->showMessage(uiText(QStringLiteral("No VRM eye expressions found.")), 3000);
        return;
    }

    viewport_->setExpressionCurvePreview(plan.channels, plan.duration_seconds);
    for (const ExpressionSliderBinding& binding : eye_expression_sliders_) {
        if (binding.slider) {
            binding.slider->setValue(0.0);
        }
    }
    statusBar()->showMessage(uiText(QStringLiteral("Soft blink expression curve is playing.")), 3000);
}

void MainWindow::syncShadingModeUi() {
    const bool raster_active = render_pipeline_ == RenderPipeline::Raster;
    const bool ray_trace_active = render_pipeline_ == RenderPipeline::RayTrace ||
        render_pipeline_ == RenderPipeline::DxrRayTrace;
    if (shading_combo_) {
        shading_combo_->setEnabled(raster_active);
    }
    if (shading_detail_stack_) {
        const int current_page = look_settings_.shading_model == ShadingModel::Phong ? 1 : 0;
        shading_detail_stack_->setCurrentIndex(current_page);
        if (raster_active) {
            shading_detail_stack_->updateGeometry();
            const int page_height = shading_detail_stack_->sizeHint().height();
            shading_detail_stack_->setMinimumHeight(page_height);
            shading_detail_stack_->setMaximumHeight(page_height);
        } else {
            shading_detail_stack_->setMinimumHeight(0);
            shading_detail_stack_->setMaximumHeight(QWIDGETSIZE_MAX);
        }
        shading_detail_stack_->setVisible(raster_active);
        shading_detail_stack_->setEnabled(raster_active);
    }
    if (ray_trace_section_) {
        ray_trace_section_->setVisible(ray_trace_active);
        ray_trace_section_->setEnabled(ray_trace_active);
    }
}

void MainWindow::syncLookDevControls() {
    setComboIndexSilently(render_pipeline_combo_, static_cast<int>(render_pipeline_));
    setComboIndexSilently(shading_combo_, look_settings_.shading_model == ShadingModel::Phong ? 1 : 0);

    if (exposure_slider_) {
        exposure_slider_->setValue(look_settings_.exposure);
    }
    if (normal_strength_slider_) {
        normal_strength_slider_->setValue(look_settings_.normal_strength);
    }
    setCheckedSilently(shadows_check_, look_settings_.enable_shadows);
    setCheckedSilently(culling_check_, look_settings_.enable_backface_culling);

    setCheckedSilently(ibl_enabled_check_, look_settings_.pbr.ibl_enabled);
    if (ibl_diffuse_slider_) {
        ibl_diffuse_slider_->setValue(look_settings_.pbr.ibl_diffuse_strength);
    }
    if (ibl_specular_slider_) {
        ibl_specular_slider_->setValue(look_settings_.pbr.ibl_specular_strength);
    }
    if (sky_light_slider_) {
        sky_light_slider_->setValue(look_settings_.pbr.sky_light_strength);
    }
    setComboIndexSilently(metallic_channel_combo_, look_settings_.pbr.metallic_channel);
    setComboIndexSilently(roughness_channel_combo_, look_settings_.pbr.roughness_channel);
    setComboIndexSilently(ao_channel_combo_, look_settings_.pbr.ao_channel);
    setComboIndexSilently(emissive_channel_combo_, look_settings_.pbr.emissive_channel);

    setComboIndexSilently(ray_trace_scene_combo_, static_cast<int>(look_settings_.ray_trace.scene_mode));
    setComboIndexSilently(ray_trace_integrator_combo_, static_cast<int>(look_settings_.ray_trace.integrator));
    setComboIndexSilently(ray_trace_view_combo_, static_cast<int>(look_settings_.ray_trace.view_mode));
    if (ray_trace_ambient_slider_) {
        ray_trace_ambient_slider_->setValue(look_settings_.ray_trace.ambient_strength);
    }
    if (ray_trace_shadow_slider_) {
        ray_trace_shadow_slider_->setValue(look_settings_.ray_trace.shadow_strength);
    }
    if (ray_trace_bounces_slider_) {
        ray_trace_bounces_slider_->setValue(look_settings_.ray_trace.max_bounces);
    }
    if (ray_trace_nee_bounces_slider_) {
        ray_trace_nee_bounces_slider_->setValue(look_settings_.ray_trace.max_nee_bounces);
    }
    if (ray_trace_spp_slider_) {
        ray_trace_spp_slider_->setValue(look_settings_.ray_trace.samples_per_frame);
    }
    setCheckedSilently(ray_trace_nee_check_, look_settings_.ray_trace.enable_nee);
    setCheckedSilently(ray_trace_photon_check_, look_settings_.ray_trace.enable_photon_cache);
    if (ray_trace_photon_radius_slider_) {
        ray_trace_photon_radius_slider_->setValue(look_settings_.ray_trace.photon_radius);
    }
    if (ray_trace_photon_intensity_slider_) {
        ray_trace_photon_intensity_slider_->setValue(look_settings_.ray_trace.photon_intensity);
    }

    setCheckedSilently(phong_hard_specular_check_, look_settings_.phong.hard_specular);
    setCheckedSilently(phong_tonemap_check_, look_settings_.phong.use_tonemap);
    setCheckedSilently(phong_primary_only_check_, look_settings_.phong.primary_light_only);
    if (phong_secondary_slider_) {
        phong_secondary_slider_->setValue(look_settings_.phong.secondary_light_scale);
    }
    if (phong_diffuse_slider_) {
        phong_diffuse_slider_->setValue(look_settings_.phong.diffuse_strength);
    }
    if (phong_ambient_slider_) {
        phong_ambient_slider_->setValue(look_settings_.phong.ambient_strength);
    }
    if (phong_ambient_color_field_) {
        phong_ambient_color_field_->setColor(look_settings_.phong.ambient_color);
    }
    if (phong_specular_slider_) {
        phong_specular_slider_->setValue(look_settings_.phong.specular_strength);
    }
    if (phong_specular_tint_field_) {
        phong_specular_tint_field_->setColor(look_settings_.phong.specular_tint);
    }
    if (phong_smoothness_slider_) {
        phong_smoothness_slider_->setValue(look_settings_.phong.smoothness);
    }
    if (phong_specular_map_weight_slider_) {
        phong_specular_map_weight_slider_->setValue(look_settings_.phong.specular_map_weight);
    }
    if (phong_shininess_slider_) {
        phong_shininess_slider_->setValue(look_settings_.phong.shininess);
    }
    if (phong_rim_strength_slider_) {
        phong_rim_strength_slider_->setValue(look_settings_.phong.rim_strength);
    }
    if (phong_rim_power_slider_) {
        phong_rim_power_slider_->setValue(look_settings_.phong.rim_power);
    }
    if (phong_rim_tint_field_) {
        phong_rim_tint_field_->setColor(look_settings_.phong.rim_tint);
    }
    setCheckedSilently(phong_toon_enabled_check_, look_settings_.phong.toon.enabled);
    if (phong_toon_steps_slider_) {
        phong_toon_steps_slider_->setValue(look_settings_.phong.toon.diffuse_steps);
    }
    if (phong_toon_softness_slider_) {
        phong_toon_softness_slider_->setValue(look_settings_.phong.toon.diffuse_softness);
    }
    if (phong_toon_shadow_floor_slider_) {
        phong_toon_shadow_floor_slider_->setValue(look_settings_.phong.toon.shadow_floor);
    }
    if (phong_toon_lit_floor_slider_) {
        phong_toon_lit_floor_slider_->setValue(look_settings_.phong.toon.lit_floor);
    }
    if (phong_toon_ramp_bias_slider_) {
        phong_toon_ramp_bias_slider_->setValue(look_settings_.phong.toon.ramp_bias);
    }
    if (phong_toon_ramp_contrast_slider_) {
        phong_toon_ramp_contrast_slider_->setValue(look_settings_.phong.toon.ramp_contrast);
    }
    if (phong_toon_shadow_map_strength_slider_) {
        phong_toon_shadow_map_strength_slider_->setValue(look_settings_.phong.toon.shadow_map_strength);
    }
    if (phong_toon_shadow_threshold_slider_) {
        phong_toon_shadow_threshold_slider_->setValue(look_settings_.phong.toon.shadow_threshold);
    }
    if (phong_toon_shadow_softness_slider_) {
        phong_toon_shadow_softness_slider_->setValue(look_settings_.phong.toon.shadow_softness);
    }
    if (phong_toon_shadow_tint_field_) {
        phong_toon_shadow_tint_field_->setColor(look_settings_.phong.toon.shadow_tint);
    }
    if (phong_toon_highlight_threshold_slider_) {
        phong_toon_highlight_threshold_slider_->setValue(look_settings_.phong.toon.highlight_threshold);
    }
    if (phong_toon_highlight_softness_slider_) {
        phong_toon_highlight_softness_slider_->setValue(look_settings_.phong.toon.highlight_softness);
    }
    if (phong_toon_highlight_strength_slider_) {
        phong_toon_highlight_strength_slider_->setValue(look_settings_.phong.toon.highlight_strength);
    }
    if (phong_toon_highlight_tint_field_) {
        phong_toon_highlight_tint_field_->setColor(look_settings_.phong.toon.highlight_tint);
    }
    if (phong_toon_rim_threshold_slider_) {
        phong_toon_rim_threshold_slider_->setValue(look_settings_.phong.toon.rim_threshold);
    }
    if (phong_toon_rim_softness_slider_) {
        phong_toon_rim_softness_slider_->setValue(look_settings_.phong.toon.rim_softness);
    }
    setCheckedSilently(phong_toon_material_override_check_, look_settings_.phong.toon.material_override_enabled);
    if (phong_toon_material_texture_strength_slider_) {
        phong_toon_material_texture_strength_slider_->setValue(look_settings_.phong.toon.material_texture_strength);
    }
    if (phong_toon_material_lift_slider_) {
        phong_toon_material_lift_slider_->setValue(look_settings_.phong.toon.material_lift);
    }
    if (phong_toon_material_saturation_slider_) {
        phong_toon_material_saturation_slider_->setValue(look_settings_.phong.toon.material_saturation);
    }
    if (phong_toon_material_contrast_slider_) {
        phong_toon_material_contrast_slider_->setValue(look_settings_.phong.toon.material_contrast);
    }
    setCheckedSilently(phong_outline_enabled_check_, look_settings_.phong.outline.enabled);
    if (phong_outline_width_slider_) {
        phong_outline_width_slider_->setValue(look_settings_.phong.outline.width_pixels);
    }
    if (phong_outline_opacity_slider_) {
        phong_outline_opacity_slider_->setValue(look_settings_.phong.outline.opacity);
    }
    if (phong_outline_depth_bias_slider_) {
        phong_outline_depth_bias_slider_->setValue(look_settings_.phong.outline.depth_bias);
    }
    if (phong_outline_color_field_) {
        phong_outline_color_field_->setColor(look_settings_.phong.outline.color);
    }

    syncShadingModeUi();
}

QString MainWindow::uiText(const QString& english) const {
    return translateKnownUiText(english, ui_language_ == UiLanguage::Chinese);
}

QColor MainWindow::uiBackgroundColorForIndex(int index) const {
    switch (index) {
    case 1:
        return QColor(3, 5, 8);
    case 2:
        return QColor(82, 86, 92);
    case 3:
        return QColor(232, 235, 238);
    case 4:
        return QColor(211, 195, 170);
    case 5:
        return QColor(27, 42, 64);
    case 6:
        return QColor(24, 48, 40);
    case 0:
    default:
        return QColor(23, 27, 32);
    }
}

void MainWindow::updateBackgroundControls() {
    if (ui_background_image_button_) {
        ui_background_image_button_->setEnabled(ui_theme_index_ == 7);
        ui_background_image_button_->setText(ui_background_image_path_.isEmpty()
            ? uiText(QStringLiteral("Choose UI Image"))
            : QFileInfo(ui_background_image_path_).fileName());
    }
    if (viewport_background_image_button_) {
        viewport_background_image_button_->setEnabled(viewport_background_index_ == 7);
        viewport_background_image_button_->setText(viewport_background_image_path_.isEmpty()
            ? uiText(QStringLiteral("Choose Viewport Image"))
            : QFileInfo(viewport_background_image_path_).fileName());
    }
}

void MainWindow::chooseUiBackgroundImage() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        uiText(QStringLiteral("Choose UI Image")),
        ui_background_image_path_.isEmpty() ? QString() : QFileInfo(ui_background_image_path_).absolutePath(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*.*)"));
    if (path.isEmpty()) {
        if (ui_background_image_path_.isEmpty()) {
            ui_theme_index_ = 0;
            setComboIndexSilently(background_combo_, 0);
        }
        updateBackgroundControls();
        return;
    }

    ui_background_image_path_ = path;
    ui_theme_index_ = 7;
    setComboIndexSilently(background_combo_, ui_theme_index_);
    applyWorkspaceTheme();
    refreshUiLanguage();
    updateBackgroundControls();
}

void MainWindow::chooseViewportBackgroundImage() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        uiText(QStringLiteral("Choose Viewport Image")),
        viewport_background_image_path_.isEmpty() ? QString() : QFileInfo(viewport_background_image_path_).absolutePath(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*.*)"));
    if (path.isEmpty()) {
        if (viewport_background_image_path_.isEmpty()) {
            viewport_background_index_ = 0;
            setComboIndexSilently(viewport_background_combo_, 0);
        }
        updateBackgroundControls();
        return;
    }

    QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, QStringLiteral("HaoRender-GI"), QStringLiteral("Failed to load background image."));
        return;
    }

    viewport_background_image_path_ = path;
    viewport_background_index_ = 7;
    setComboIndexSilently(viewport_background_combo_, viewport_background_index_);
    if (viewport_) {
        viewport_->setBackgroundImage(image);
        viewport_->setClearColor(QColor(0, 0, 0, 0));
    }
    updateBackgroundControls();
}

void MainWindow::refreshUiLanguage() {
    const bool target_chinese = ui_language_ == UiLanguage::Chinese;
    setComboIndexSilently(language_combo_, target_chinese ? 1 : 0);
    setComboIndexSilently(background_combo_, ui_theme_index_);
    setComboIndexSilently(viewport_background_combo_, viewport_background_index_);
    translateWidgetTree(this, target_chinese);
    if (render_workspace_action_) {
        render_workspace_action_->setText(uiText(QStringLiteral("Render")));
    }
    if (open_model_action_) {
        open_model_action_->setText(uiText(QStringLiteral("Open")));
    }
    if (open_rig_ai_action_) {
        open_rig_ai_action_->setText(uiText(QStringLiteral("Rig AI")));
    }
    if (asset_converter_action_) {
        asset_converter_action_->setText(uiText(QStringLiteral("Convert")));
    }
    if (reset_camera_action_) {
        reset_camera_action_->setText(uiText(QStringLiteral("Reset")));
    }
    if (save_snapshot_action_) {
        save_snapshot_action_->setText(uiText(QStringLiteral("Snapshot")));
    }
    if (ai_recommend_button_ && ai_recommend_button_->isEnabled()) {
        ai_recommend_button_->setText(uiText(QStringLiteral("Send")));
    }
    if (ai_save_preset_button_) {
        ai_save_preset_button_->setText(uiText(QStringLiteral("Save")));
    }
    if (ai_load_preset_button_) {
        ai_load_preset_button_->setText(uiText(QStringLiteral("Load")));
    }
    if (eye_expression_reset_button_) {
        eye_expression_reset_button_->setText(uiText(QStringLiteral("Reset Expressions")));
    }
    if (eye_expression_blink_curve_button_) {
        eye_expression_blink_curve_button_->setText(uiText(QStringLiteral("Soft Blink Curve")));
    }
    if (eye_expression_empty_label_) {
        refreshEyeExpressionPanel();
    }
    refreshPlaybackControls();
    updateBackgroundControls();
    syncShadingModeUi();
    if (rig_ai_window_) {
        rig_ai_window_->setStyleSheet(styleSheet());
        rig_ai_window_->setChineseUi(target_chinese);
    }
    if (asset_converter_window_) {
        asset_converter_window_->setStyleSheet(styleSheet());
        asset_converter_window_->setChineseUi(target_chinese);
    }
}

void MainWindow::appendAiChatMessage(const QString& speaker, const QString& text) {
    if (!ai_chat_view_) {
        return;
    }
    const QString trimmed_text = text.trimmed();
    if (trimmed_text.isEmpty()) {
        return;
    }

    const QString speaker_key = speaker.toLower();
    const bool is_user = speaker_key.contains(QStringLiteral("you")) || speaker_key.contains(QStringLiteral("artist"));
    const bool is_agent = speaker_key.contains(QStringLiteral("agent"));
    const bool is_system = speaker_key.contains(QStringLiteral("system"));
    QString display_name = speaker;
    QString label_color = QStringLiteral("#9fb0c4");
    QString bubble_background = QStringLiteral("#111924");
    QString bubble_border = QStringLiteral("#223043");
    QString align = QStringLiteral("left");
    int bubble_width = 92;

    if (is_user) {
        display_name = QStringLiteral("You");
        label_color = QStringLiteral("#d8e1ec");
        bubble_background = QStringLiteral("#243145");
        bubble_border = QStringLiteral("#334761");
        align = QStringLiteral("right");
        bubble_width = 84;
    } else if (is_agent) {
        display_name = QStringLiteral("LookDev Agent");
        label_color = QStringLiteral("#8fe0bd");
        bubble_background = QStringLiteral("#0f1721");
    } else if (is_system) {
        display_name = QStringLiteral("System");
        label_color = QStringLiteral("#94a3b8");
        bubble_background = QStringLiteral("#0d141d");
        bubble_border = QStringLiteral("#1b2837");
    }

    QString body = trimmed_text.toHtmlEscaped();
    body.replace(QStringLiteral("\n"), QStringLiteral("<br>"));

    ai_chat_view_->append(QStringLiteral(
        "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"margin:0 0 10px 0;\">"
        "<tr><td align=\"%1\">"
        "<table width=\"%2%\" cellspacing=\"0\" cellpadding=\"0\" "
        "style=\"background-color:%3;border:1px solid %4;\">"
        "<tr><td style=\"padding:10px 12px;\">"
        "<div style=\"font-size:10px;font-weight:700;color:%5;margin-bottom:4px;\">%6</div>"
        "<div style=\"font-size:12px;line-height:1.45;color:#e6edf5;\">%7</div>"
        "</td></tr>"
        "</table>"
        "</td></tr>"
        "</table>")
        .arg(align)
        .arg(bubble_width)
        .arg(bubble_background, bubble_border, label_color, display_name.toHtmlEscaped(), body));
    ai_chat_view_->moveCursor(QTextCursor::End);
}

void MainWindow::appendAiSystemMessage(const QString& text) {
    appendAiChatMessage(QStringLiteral("System"), text);
}

void MainWindow::generateAiRecommendations() {
    const QString prompt = ai_prompt_edit_ ? ai_prompt_edit_->toPlainText().trimmed() : QString();
    if (prompt.isEmpty()) {
        appendAiSystemMessage(QStringLiteral("先告诉 LookDev agent 你想要的画面感觉，例如：柔和的原神式二游渲染，阴影偏蓝紫，高光小而干净。"));
        if (ai_reply_label_) {
            ai_reply_label_->setText(QStringLiteral("先告诉 LookDev agent 你想要的画面感觉，例如：柔和的原神式二游渲染，阴影偏蓝紫，高光小而干净。"));
        }
        statusBar()->showMessage(QStringLiteral("Enter a LookDev goal first"), 3000);
        return;
    }

    appendAiChatMessage(QStringLiteral("You"), prompt);
    ai_dialogue_context_ += QStringLiteral("Artist: %1\n").arg(prompt);

    const LookDevLlmConfig config = currentLlmConfig();
    if (config.api_key.trimmed().isEmpty()) {
        const QVector<AiRecommendationCandidate> local_candidates = buildLookDevRecommendations(prompt, look_settings_);
        const ExpressionCurvePlan local_expression_plan = buildExpressionCurveForPrompt(scene_, prompt);
        const GazeCurvePlan local_gaze_plan = buildGazeCurveForPrompt(scene_, prompt);
        ai_candidates_.clear();
        if (!local_candidates.isEmpty()) {
            ai_candidates_.push_back(local_candidates.first());
            ai_candidates_.last().slot_label = QStringLiteral("Best");
        }
        appendAiChatMessage(QStringLiteral("Agent"),
                            QStringLiteral("我没有读到本地 API Key，所以先用本地规则生成一套可用参数。把 llm.env.local 放到项目根目录后，下次发送会直接走大模型。"));
        if (!ai_candidates_.isEmpty()) {
            appendAiSystemMessage(QStringLiteral("Generated one preset: %1").arg(ai_candidates_.first().preset.name));
        }
        if (local_expression_plan.valid()) {
            applyExpressionCurveFromAgent(local_expression_plan.channels,
                                          local_expression_plan.duration_seconds,
                                          local_expression_plan.summary);
        }
        if (local_gaze_plan.valid()) {
            applyGazeCurveFromAgent(local_gaze_plan.keys,
                                    local_gaze_plan.duration_seconds,
                                    local_gaze_plan.summary);
        }
        if (ai_reply_label_) {
            ai_reply_label_->setText(QStringLiteral("Generated one offline rule-based preset because no local API key was found."));
        }
        if (ai_prompt_edit_) {
            ai_prompt_edit_->clear();
            ai_prompt_edit_->setPlaceholderText(QStringLiteral("Tell the agent what to adjust next..."));
        }
        refreshAiCandidateCards();
        statusBar()->showMessage(QStringLiteral("Generated one local LookDev preset"), 4000);
        return;
    }

    if (ai_recommend_button_) {
        ai_recommend_button_->setEnabled(false);
        ai_recommend_button_->setText(uiText(QStringLiteral("Thinking...")));
    }
    if (ai_reply_label_) {
        ai_reply_label_->setText(QStringLiteral("Agent is reading the full parameter skill and deciding whether to ask a better question or create one tuned preset..."));
    }
    appendAiSystemMessage(QStringLiteral("Sending this turn to the LookDev LLM agent..."));
    if (ai_prompt_edit_) {
        ai_prompt_edit_->clear();
    }
    statusBar()->showMessage(QStringLiteral("Sending LookDev agent turn to LLM..."), 4000);

    QStringList available_eye_controls = availableEyeExpressionNames(scene_);
    if (hasEyeGazeSolver(scene_)) {
        available_eye_controls.push_front(QStringLiteral("EyeBoneGazeSolver(yawDegrees,pitchDegrees,weight)"));
    }
    ai_client_.requestRecommendations(config,
                                      prompt,
                                      ai_dialogue_context_,
                                      look_settings_,
                                      available_eye_controls,
                                      [this, prompt](const LookDevLlmResult& result) {
        finishAiRecommendations(result, prompt);
    });
}

LookDevLlmConfig MainWindow::currentLlmConfig() const {
    LookDevLlmConfig config = lookDevLlmConfigFromEnvironment();
    if (ai_base_url_edit_ && !ai_base_url_edit_->text().trimmed().isEmpty()) {
        config.base_url = ai_base_url_edit_->text().trimmed();
    }
    if (ai_model_edit_ && !ai_model_edit_->text().trimmed().isEmpty()) {
        config.model = ai_model_edit_->text().trimmed();
    }
    if (ai_api_key_edit_ && !ai_api_key_edit_->text().trimmed().isEmpty()) {
        config.api_key = ai_api_key_edit_->text().trimmed();
    }
    return config;
}

void MainWindow::finishAiRecommendations(const LookDevLlmResult& result, const QString& prompt) {
    if (ai_recommend_button_) {
        ai_recommend_button_->setEnabled(true);
        ai_recommend_button_->setText(uiText(QStringLiteral("Send")));
    }

    if (result.ok) {
        QString reply_text = result.assistant_reply.trimmed();
        if (!result.next_question.trimmed().isEmpty()) {
            if (!reply_text.isEmpty()) {
                reply_text += QStringLiteral("\n\n");
            }
            reply_text += result.next_question.trimmed();
        }
        if (!reply_text.isEmpty()) {
            ai_dialogue_context_ += QStringLiteral("Agent: %1\n").arg(reply_text);
        }

        if (result.needs_user_input) {
            ai_candidates_.clear();
            refreshAiCandidateCards();
            const QString shown_text = reply_text.isEmpty()
                ? QStringLiteral("我还需要一个更具体的审美选择，再继续生成参数。")
                : reply_text;
            appendAiChatMessage(QStringLiteral("Agent"), shown_text);
            if (ai_reply_label_) {
                ai_reply_label_->setText(shown_text);
            }
            if (ai_prompt_edit_) {
                ai_prompt_edit_->setPlaceholderText(result.next_question.trimmed().isEmpty()
                    ? QStringLiteral("Reply with the missing visual preference...")
                    : result.next_question.trimmed());
            }
            statusBar()->showMessage(QStringLiteral("Agent asked a follow-up question"), 5000);
            return;
        }

        if (!result.candidates.isEmpty()) {
            ai_candidates_.clear();
            ai_candidates_.push_back(result.candidates.first());
            ai_candidates_.last().slot_label = QStringLiteral("Best");
            refreshAiCandidateCards();
            const QString shown_text = reply_text.isEmpty()
                ? QStringLiteral("我已经生成一套完整的 LookDev 参数。你可以先应用它，再告诉我哪里还不像。")
                : reply_text;
            appendAiChatMessage(QStringLiteral("Agent"), shown_text);
            appendAiSystemMessage(QStringLiteral("Generated one preset: %1").arg(ai_candidates_.first().preset.name));
            if (!result.expression_channels.empty()) {
                applyExpressionCurveFromAgent(result.expression_channels,
                                              result.expression_duration_seconds,
                                              result.expression_summary);
            }
            if (!result.gaze_keys.empty()) {
                applyGazeCurveFromAgent(result.gaze_keys,
                                        result.gaze_duration_seconds,
                                        result.gaze_summary);
            }
            if (ai_reply_label_) {
                ai_reply_label_->setText(shown_text);
            }
            if (ai_prompt_edit_) {
                ai_prompt_edit_->setPlaceholderText(QStringLiteral("Tell the agent what still feels off: shadows, highlights, outline, warmth, contrast..."));
            }
            statusBar()->showMessage(QStringLiteral("Generated one LLM LookDev preset"), 5000);
            return;
        }

        if (!result.expression_channels.empty() || !result.gaze_keys.empty()) {
            ai_candidates_.clear();
            refreshAiCandidateCards();
            const QString shown_text = reply_text.isEmpty()
                ? QStringLiteral("我已经生成一段眼部 / 表情 / 视线曲线并应用到预览。")
                : reply_text;
            appendAiChatMessage(QStringLiteral("Agent"), shown_text);
            if (!result.expression_channels.empty()) {
                applyExpressionCurveFromAgent(result.expression_channels,
                                              result.expression_duration_seconds,
                                              result.expression_summary);
            }
            if (!result.gaze_keys.empty()) {
                applyGazeCurveFromAgent(result.gaze_keys,
                                        result.gaze_duration_seconds,
                                        result.gaze_summary);
            }
            if (ai_reply_label_) {
                ai_reply_label_->setText(shown_text);
            }
            if (ai_prompt_edit_) {
                ai_prompt_edit_->setPlaceholderText(QStringLiteral("Tell the agent how to refine the gaze, blink timing, or expression weight..."));
            }
            statusBar()->showMessage(QStringLiteral("Generated one LLM expression curve"), 5000);
            return;
        }
    }

    const QVector<AiRecommendationCandidate> local_candidates = buildLookDevRecommendations(prompt, look_settings_);
    const ExpressionCurvePlan local_expression_plan = buildExpressionCurveForPrompt(scene_, prompt);
    const GazeCurvePlan local_gaze_plan = buildGazeCurveForPrompt(scene_, prompt);
    ai_candidates_.clear();
    if (!local_candidates.isEmpty()) {
        ai_candidates_.push_back(local_candidates.first());
        ai_candidates_.last().slot_label = QStringLiteral("Best");
    }
    refreshAiCandidateCards();
    const QString error_text = result.error_message.trimmed().isEmpty()
        ? QStringLiteral("LLM did not return valid preset JSON.")
        : result.error_message.trimmed();
    appendAiSystemMessage(QStringLiteral("Cloud LLM fallback: %1").arg(error_text));
    appendAiChatMessage(QStringLiteral("Agent"),
                        QStringLiteral("云端返回不可用，我先生成一套本地规则参数，避免这轮对话断掉。你可以继续描述不满意的地方。"));
    if (!ai_candidates_.isEmpty()) {
        appendAiSystemMessage(QStringLiteral("Generated one local preset: %1").arg(ai_candidates_.first().preset.name));
    }
    if (local_expression_plan.valid()) {
        applyExpressionCurveFromAgent(local_expression_plan.channels,
                                      local_expression_plan.duration_seconds,
                                      local_expression_plan.summary);
    }
    if (local_gaze_plan.valid()) {
        applyGazeCurveFromAgent(local_gaze_plan.keys,
                                local_gaze_plan.duration_seconds,
                                local_gaze_plan.summary);
    }
    if (ai_reply_label_) {
        ai_reply_label_->setText(QStringLiteral("Cloud LLM fallback: %1\nUsing one local rule-based preset instead.").arg(error_text));
    }
    statusBar()->showMessage(QStringLiteral("LLM failed; generated one local LookDev preset"), 5000);
}

void MainWindow::refreshAiCandidateCards() {
    for (int i = 0; i < static_cast<int>(ai_candidate_name_labels_.size()); ++i) {
        const bool visible_slot = i == 0;
        if (ai_candidate_cards_[i]) {
            ai_candidate_cards_[i]->setVisible(visible_slot);
        }
        if (!visible_slot) {
            continue;
        }
        const bool has_candidate = i < ai_candidates_.size();
        if (ai_candidate_slot_labels_[i]) {
            ai_candidate_slot_labels_[i]->setText(has_candidate
                ? QStringLiteral("%1 / %2").arg(ai_candidates_.at(i).slot_label, stylePresetFocusLabel(ai_candidates_.at(i).preset.focus))
                : QStringLiteral("Agent / Waiting"));
        }
        if (ai_candidate_name_labels_[i]) {
            ai_candidate_name_labels_[i]->setText(has_candidate ? ai_candidates_.at(i).preset.name : uiText(QStringLiteral("No preset yet")));
        }
        if (ai_candidate_summary_labels_[i]) {
            ai_candidate_summary_labels_[i]->setText(has_candidate ? ai_candidates_.at(i).summary : uiText(QStringLiteral("Talk with the agent until the target look is clear enough to create one preset.")));
        }
        if (ai_candidate_reason_labels_[i]) {
            ai_candidate_reason_labels_[i]->setText(has_candidate ? ai_candidates_.at(i).preset.description : uiText(QStringLiteral("The agent can ask follow-up questions before generating parameters.")));
        }
        if (ai_candidate_apply_buttons_[i]) {
            ai_candidate_apply_buttons_[i]->setEnabled(has_candidate);
            ai_candidate_apply_buttons_[i]->setText(has_candidate ? uiText(QStringLiteral("Apply Preset")) : uiText(QStringLiteral("Apply")));
        }
    }
}

void MainWindow::applyStylePreset(const StylePreset& preset, bool switch_pipeline) {
    look_settings_ = preset.settings;
    if (switch_pipeline) {
        render_pipeline_ = preset.preferred_pipeline;
    }
    syncLookDevControls();
    applyLookDevToViewport();
    statusBar()->showMessage(QStringLiteral("Applied preset: %1").arg(preset.name), 4000);
}

bool MainWindow::loadStylePresetFromFile(const QString& path) {
    const QString trimmed_path = path.trimmed();
    if (trimmed_path.isEmpty()) {
        return false;
    }

    QFile file(trimmed_path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("HaoRender-GI"), QStringLiteral("Failed to open the preset file."));
        return false;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (document.isNull() || !document.isObject()) {
        const QString message = parse_error.error == QJsonParseError::NoError
            ? QStringLiteral("Preset file is not valid JSON.")
            : QStringLiteral("Preset file is not valid JSON: %1").arg(parse_error.errorString());
        QMessageBox::warning(this, QStringLiteral("HaoRender-GI"), message);
        return false;
    }

    StylePreset preset;
    QString error_message;
    if (!stylePresetFromJson(document.object(), &preset, &error_message)) {
        QMessageBox::warning(this, QStringLiteral("HaoRender-GI"), error_message.isEmpty() ? QStringLiteral("Preset file is invalid.") : error_message);
        return false;
    }

    if (ai_prompt_edit_ && !preset.source_prompt.isEmpty()) {
        ai_prompt_edit_->setPlainText(preset.source_prompt);
    }
    applyStylePreset(preset, true);
    statusBar()->showMessage(QStringLiteral("Loaded preset: %1").arg(QDir::toNativeSeparators(trimmed_path)), 5000);
    return true;
}

void MainWindow::saveStylePreset() {
    std::filesystem::create_directories("StylePresets");
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString default_name = QDir::toNativeSeparators(QString::fromStdString(
        (std::filesystem::path("StylePresets") / QString("lookdev_%1.haostyle.json").arg(timestamp).toStdString()).string()));
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save LookDev Preset"),
        default_name,
        QStringLiteral("HaoRender Style Preset (*.haostyle.json *.json);;JSON Files (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    StylePreset preset;
    preset.name = QFileInfo(path).completeBaseName();
    preset.description = QStringLiteral("Saved from the HaoRender-GI LookDev AI workbench.");
    preset.source_prompt = ai_prompt_edit_ ? ai_prompt_edit_->toPlainText().trimmed() : QString();
    preset.focus = look_settings_.phong.toon.enabled
        ? (look_settings_.phong.outline.enabled ? StylePresetFocus::Hybrid : StylePresetFocus::Toon)
        : StylePresetFocus::Phong;
    preset.preferred_pipeline = render_pipeline_;
    preset.settings = look_settings_;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("HaoRender-GI"), QStringLiteral("Failed to open the preset file for writing."));
        return;
    }

    const QJsonDocument document(stylePresetToJson(preset));
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        QMessageBox::warning(this, QStringLiteral("HaoRender-GI"), QStringLiteral("Failed to write the preset file."));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Saved preset to %1").arg(QDir::toNativeSeparators(path)), 5000);
}

void MainWindow::loadStylePreset() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Load LookDev Preset"),
        QStringLiteral("StylePresets"),
        QStringLiteral("HaoRender Style Preset (*.haostyle.json *.json);;JSON Files (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    loadStylePresetFromFile(path);
}

void MainWindow::showRenderWorkspace() {
    if (workspace_stack_ && render_workspace_page_) {
        workspace_stack_->setCurrentWidget(render_workspace_page_);
    }
    updateSceneSummary();
    updateViewportChrome();
    statusBar()->showMessage(uiText(QStringLiteral("Workspace ready")), 2500);
}

void MainWindow::openAssetConverterWindow() {
    if (!asset_converter_window_) {
        asset_converter_window_ = new AssetConverterWindow(workspace_stack_ ? static_cast<QWidget*>(workspace_stack_) : this, true);
        asset_converter_window_->setStyleSheet(styleSheet());
        asset_converter_window_->setChineseUi(ui_language_ == UiLanguage::Chinese);
        if (workspace_stack_) {
            workspace_stack_->addWidget(asset_converter_window_);
        }
    }
    if (workspace_stack_) {
        workspace_stack_->setCurrentWidget(asset_converter_window_);
    } else {
        asset_converter_window_->show();
    }
    if (!current_model_path_.trimmed().isEmpty()) {
        asset_converter_window_->loadSourcePath(current_model_path_);
    }
    workspace_subtitle_label_->setText(uiText(QStringLiteral("Asset Converter")));
    statusBar()->showMessage(uiText(QStringLiteral("Asset Converter workspace")), 2500);
}

void MainWindow::openRigAiWindow() {
    if (!rig_ai_window_) {
        rig_ai_window_ = new RigAiWindow(workspace_stack_ ? static_cast<QWidget*>(workspace_stack_) : this, true);
        rig_ai_window_->setStyleSheet(styleSheet());
        rig_ai_window_->setChineseUi(ui_language_ == UiLanguage::Chinese);
        if (workspace_stack_) {
            workspace_stack_->addWidget(rig_ai_window_);
        }
    }
    if (workspace_stack_) {
        workspace_stack_->setCurrentWidget(rig_ai_window_);
    } else {
        rig_ai_window_->show();
    }
    if (!rig_ai_startup_paths_applied_ &&
        !startup_model_path_.trimmed().isEmpty()) {
        rig_ai_startup_paths_applied_ = true;
        const QString target_path = startup_model_path_;
        const QString source_path = startup_source_animation_path_;
        QTimer::singleShot(0, rig_ai_window_, [this, target_path, source_path]() {
            if (rig_ai_window_) {
                rig_ai_window_->loadTargetAndSourcePaths(target_path, source_path);
            }
        });
    }
    workspace_subtitle_label_->setText(uiText(QStringLiteral("HaoRig AI skeleton mapping")));
    statusBar()->showMessage(uiText(QStringLiteral("Rig AI workspace")), 2500);
}

void MainWindow::loadModel(const QString& path, bool activate_loaded_ray_trace_scene) {
    QString error_message;
    SceneModel loaded = model_loader_.loadFromFile(path, &error_message);
    if (loaded.empty()) {
        QMessageBox::warning(this, QStringLiteral("HaoRender-GI"), error_message.isEmpty() ? QStringLiteral("Failed to load model.") : error_message);
        statusBar()->showMessage(error_message, 5000);
        return;
    }

    scene_ = std::move(loaded);
    current_model_path_ = path;
    if (activate_loaded_ray_trace_scene &&
        (render_pipeline_ == RenderPipeline::RayTrace ||
         render_pipeline_ == RenderPipeline::DxrRayTrace)) {
        look_settings_.ray_trace.scene_mode = RayTraceSceneMode::LoadedModel;
        if (ray_trace_scene_combo_) {
            const bool blocked = ray_trace_scene_combo_->blockSignals(true);
            ray_trace_scene_combo_->setCurrentIndex(static_cast<int>(RayTraceSceneMode::LoadedModel));
            ray_trace_scene_combo_->blockSignals(blocked);
        }
    }
    viewport_->setScene(&scene_);
    viewport_->setAnimationPlaybackEnabled(false);
    refreshEyeExpressionPanel();
    refreshPlaybackControls();
    applyLookDevToViewport();
    updateSceneSummary();
    statusBar()->showMessage(QStringLiteral("Loaded %1").arg(QDir::toNativeSeparators(path)), 5000);
}

void MainWindow::previewRetargetedRigAnimation(const QString& target_path,
                                               const QString& source_path,
                                               const BoneMappingResult& mapping_result) {
    QString target_error;
    SceneModel target_scene = model_loader_.loadFromFile(target_path, &target_error);
    if (target_scene.empty()) {
        QMessageBox::warning(this,
                             QStringLiteral("HaoRig AI"),
                             target_error.isEmpty() ? QStringLiteral("Failed to load target character.") : target_error);
        return;
    }

    QString source_error;
    SceneModel source_animation = model_loader_.loadAnimationFromFile(source_path, &source_error);
    if (source_animation.nodes.empty() || source_animation.animations.empty()) {
        QMessageBox::warning(this,
                             QStringLiteral("HaoRig AI"),
                             source_error.isEmpty() ? QStringLiteral("Failed to load source animation.") : source_error);
        return;
    }

    SceneModel preview_scene;
    QString retarget_error;
    if (!retargetAnimationToTarget(source_animation, target_scene, mapping_result, &preview_scene, &retarget_error)) {
        QMessageBox::warning(this,
                             QStringLiteral("HaoRig AI"),
                             retarget_error.isEmpty() ? QStringLiteral("Failed to retarget animation.") : retarget_error);
        return;
    }

    scene_ = std::move(preview_scene);
    current_model_path_ = target_path;
    render_pipeline_ = RenderPipeline::Raster;
    look_settings_.shading_model = ShadingModel::Phong;
    viewport_->setScene(&scene_);
    viewport_->setAnimationPlaybackEnabled(true);
    refreshEyeExpressionPanel();
    refreshPlaybackControls();
    syncLookDevControls();
    applyLookDevToViewport();
    updateSceneSummary();
    const AnimationClipData& clip = scene_.animations.front();
    statusBar()->showMessage(QStringLiteral("Retarget preview: %1 -> %2, %3 channels, %4s")
                                 .arg(QFileInfo(source_path).fileName(),
                                      QFileInfo(target_path).fileName())
                                 .arg(clip.channels.size())
                                 .arg(clip.durationSeconds(), 0, 'f', 2),
                             5000);
}

void MainWindow::previewRigAnimationWithAutoMapping(const QString& target_path, const QString& source_path) {
    SkeletonExtractor extractor;
    QString target_error;
    SkeletonGraph target_skeleton = extractor.loadFromFile(target_path, &target_error);
    if (target_skeleton.empty()) {
        QMessageBox::warning(this,
                             QStringLiteral("HaoRig AI"),
                             target_error.isEmpty() ? QStringLiteral("Failed to read target skeleton.") : target_error);
        return;
    }

    QString source_error;
    SkeletonGraph source_skeleton = extractor.loadFromFile(source_path, &source_error);
    if (source_skeleton.empty()) {
        QMessageBox::warning(this,
                             QStringLiteral("HaoRig AI"),
                             source_error.isEmpty() ? QStringLiteral("Failed to read source animation skeleton.") : source_error);
        return;
    }

    AiBoneMapper mapper;
    BoneMappingResult mapping = mapper.mapSkeletons(source_skeleton, target_skeleton);
    previewRetargetedRigAnimation(target_path, source_path, mapping);
}

void MainWindow::loadDefaultModel() {
    const QString path = startup_model_path_.isEmpty() ? findDefaultModelPath() : startup_model_path_;
    if (!path.isEmpty()) {
        const bool explicit_startup_asset = !startup_model_path_.isEmpty();
        const bool keep_dxr_cornell_demo = render_pipeline_ == RenderPipeline::DxrRayTrace && !explicit_startup_asset;
        loadModel(path, !keep_dxr_cornell_demo);
    }
}

void MainWindow::saveSnapshot() {
    const QImage frame = viewport_->captureCurrentFrame();
    if (frame.isNull()) {
        QMessageBox::warning(this, QStringLiteral("HaoRender-GI"), QStringLiteral("Nothing to save yet."));
        return;
    }

    std::filesystem::create_directories("Screenshots");
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString path = QDir::toNativeSeparators(QString::fromStdString(
        (std::filesystem::path("Screenshots") / QString("haorender_gi_%1.png").arg(timestamp).toStdString()).string()));
    if (!frame.save(path)) {
        QMessageBox::warning(this, QStringLiteral("HaoRender-GI"), QStringLiteral("Failed to save snapshot."));
        return;
    }
    statusBar()->showMessage(QStringLiteral("Snapshot saved to %1").arg(path), 5000);
}

QString MainWindow::findDefaultModelPath() const {
    const QStringList candidates = {
        QStringLiteral("F:/haorender-main/Resources/MAIFU/IF.fbx"),
        QStringLiteral("F:/haorender-gi/repo/haorender-main/Resources/MAIFU/IF.fbx"),
        QStringLiteral("F:/haorender-gi/verify-final/haorender-main/Resources/MAIFU/IF.fbx")
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return QString();
}

void MainWindow::updateSceneSummary() {
    const bool has_scene = !scene_.empty() && !current_model_path_.isEmpty();
    const QFileInfo model_info(current_model_path_);
    const QString model_name = has_scene ? model_info.fileName() : uiText(QStringLiteral("No asset loaded"));
    const QString model_path = has_scene ? QDir::toNativeSeparators(current_model_path_) : uiText(QStringLiteral("Load a mesh or scene file to begin."));

    workspace_subtitle_label_->setText(has_scene ? QStringLiteral("%1 loaded").arg(model_name) : uiText(QStringLiteral("HaoRender-GI render workbench")));
    model_path_value_->setText(model_path);
    mesh_value_->setText(formatCount(scene_.meshes.size()));
    triangle_value_->setText(formatCount(scene_.triangleCount()));
    vertex_value_->setText(formatCount(scene_.vertexCount()));
    updateViewportChrome();
}

void MainWindow::updateRenderStats(const RenderStats& stats) {
    frame_value_->setText(formatMs(stats.frame_ms));
    shadow_value_->setText(formatMs(stats.shadow_ms));
    main_value_->setText(formatMs(stats.main_ms));
    note_value_->setText(stats.note.isEmpty() ? uiText(QStringLiteral("Interactive preview ready")) : stats.note);
    backend_value_->setText(viewport_ ? viewport_->backendName() : QStringLiteral("Unavailable"));
}

void MainWindow::updateViewportChrome() {
    const bool has_scene = !scene_.empty() && !current_model_path_.isEmpty();
    const bool ray_trace_cornell = (render_pipeline_ == RenderPipeline::RayTrace ||
        render_pipeline_ == RenderPipeline::DxrRayTrace) &&
        look_settings_.ray_trace.scene_mode == RayTraceSceneMode::CornellBox;
    const QString model_name = ray_trace_cornell
        ? QStringLiteral("Cornell Box")
        : (has_scene ? QFileInfo(current_model_path_).completeBaseName() : uiText(QStringLiteral("No asset loaded")));
    const QString backend_name = viewport_ ? viewport_->backendName() : QStringLiteral("Unavailable");
    const QString animation_suffix = has_scene && scene_.hasAnimations()
        ? QStringLiteral("  |  %1 anim").arg(formatCount(scene_.animations.size()))
        : QString();
    const QString subtitle = ray_trace_cornell
        ? QStringLiteral("%1  |  %2  |  procedural room  |  imported model + metal + glass")
              .arg(backend_name)
              .arg(currentShadingLabel())
        : (has_scene
        ? QStringLiteral("%1  |  %2  |  %3 meshes  |  %4 tris%5")
              .arg(backend_name)
              .arg(currentShadingLabel())
              .arg(formatCount(scene_.meshes.size()))
              .arg(formatCount(scene_.triangleCount()))
              .arg(animation_suffix)
        : QStringLiteral("%1  |  %2").arg(backend_name, currentShadingLabel()));

    viewport_title_label_->setText(model_name);
    viewport_subtitle_label_->setText(subtitle);
}

QString MainWindow::currentShadingLabel() const {
    if (render_pipeline_ == RenderPipeline::DxrRayTrace) {
        const QString scene_label = look_settings_.ray_trace.scene_mode == RayTraceSceneMode::CornellBox
            ? QStringLiteral("Cornell")
            : QStringLiteral("Model");
        return QStringLiteral("DXR / %1 / hardware RT").arg(scene_label);
    }
    if (render_pipeline_ == RenderPipeline::RayTrace) {
        const QString scene_label = look_settings_.ray_trace.scene_mode == RayTraceSceneMode::CornellBox
            ? QStringLiteral("Cornell")
            : QStringLiteral("Model");
        return QStringLiteral("Ray Trace / %1 / %2 / %3").arg(scene_label, currentRayTraceIntegratorLabel(), currentRayTraceViewLabel());
    }
    if (look_settings_.shading_model == ShadingModel::Phong) {
        return look_settings_.phong.toon.enabled ? QStringLiteral("Phong / Toon") : QStringLiteral("Phong");
    }
    return QStringLiteral("PBR");
}

QString MainWindow::currentRayTraceViewLabel() const {
    switch (look_settings_.ray_trace.view_mode) {
    case RayTraceViewMode::Hit:
        return QStringLiteral("Hit");
    case RayTraceViewMode::Normal:
        return QStringLiteral("Normal");
    case RayTraceViewMode::Albedo:
        return QStringLiteral("Albedo");
    case RayTraceViewMode::Lit:
    default:
        return QStringLiteral("Lit");
    }
}

QString MainWindow::currentRayTraceIntegratorLabel() const {
    switch (look_settings_.ray_trace.integrator) {
    case RayTraceIntegrator::Hybrid:
        return QStringLiteral("Hybrid");
    case RayTraceIntegrator::PathTrace:
        return QStringLiteral("PT");
    case RayTraceIntegrator::PathTraceNee:
        return QStringLiteral("PT+NEE");
    case RayTraceIntegrator::PhotonPath:
    default:
        return QStringLiteral("Photon");
    }
}

} // namespace haorendergi
