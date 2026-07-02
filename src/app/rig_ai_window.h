#pragma once

#include "rigging/ai_bone_mapper.h"
#include "rigging/rig_mapping_exporter.h"
#include "rigging/skeleton_extractor.h"
#include "scene/model_loader.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QComboBox;
class QFrame;
class QLineEdit;
class QPushButton;
class QSlider;
class QTableWidget;
class QTextBrowser;
class QVBoxLayout;

namespace haorendergi {

class RenderViewport;

class RigAiWindow final : public QDialog {
    Q_OBJECT

public:
    explicit RigAiWindow(QWidget* parent = nullptr, bool embedded = false);
    void setChineseUi(bool enabled);
    void loadTargetAndSourcePaths(const QString& target_path, const QString& source_path);

signals:
    void targetModelPreviewRequested(const QString& path);
    void sourceAnimationPreviewRequested(const QString& path);
    void retargetPreviewRequested(const QString& target_path, const QString& source_path, const BoneMappingResult& mapping_result);

private:
    void chooseTargetSkeleton();
    void chooseSourceAnimation();
    void scanAigrilAssets();
    void loadSelectedAigrilTarget();
    void loadSelectedAigrilSource();
    void loadTargetSkeletonPath(const QString& path, bool preview_target);
    void loadSourceAnimationPath(const QString& path);
    void generateMapping();
    void exportMapping();
    void configurePreviewViewport();
    void previewTargetCharacter(const QString& path);
    void previewRetargetedAnimation(const BoneMappingResult& mapping_result);
    void setPreviewStatus(const QString& text);
    void setTargetSkeleton(const SkeletonGraph& skeleton);
    void setSourceSkeleton(const SkeletonGraph& skeleton);
    void refreshSummary();
    void refreshMappingTable();
    void refreshLanguage();
    void refreshEyeExpressionPanel();
    void resetEyeExpressionControls();
    void applySoftBlinkExpressionCurve();
    void applyExpressionWeight(const QString& expression_name, double weight);
    void applyEyeGazeControl();
    void toggleAnimationPlayback();
    void restartAnimationPlayback();
    void refreshPlaybackControls();
    void appendAgentMessage(const QString& speaker, const QString& text);
    BoneMappingResult mappingFromTable() const;
    QString trText(const QString& english) const;
    QString skeletonSummaryText(const SkeletonGraph& skeleton) const;
    QString defaultAigrilRoot() const;

    struct ExpressionSliderBinding {
        QString expression_name;
        QSlider* slider = nullptr;
        QLabel* value_label = nullptr;
    };

    SkeletonExtractor extractor_;
    AiBoneMapper mapper_;
    RigMappingExporter exporter_;
    ModelLoader model_loader_;

    SkeletonGraph target_skeleton_;
    SkeletonGraph source_skeleton_;
    BoneMappingResult mapping_result_;
    SceneModel target_scene_;
    SceneModel preview_scene_;
    bool chinese_ui_ = false;
    bool embedded_ = false;

    QLabel* title_label_ = nullptr;
    QLabel* subtitle_label_ = nullptr;
    QLabel* target_label_ = nullptr;
    QLabel* source_label_ = nullptr;
    QLineEdit* target_path_edit_ = nullptr;
    QLineEdit* source_path_edit_ = nullptr;
    QLabel* target_summary_label_ = nullptr;
    QLabel* source_summary_label_ = nullptr;
    QLabel* mapping_summary_label_ = nullptr;
    QLabel* preview_title_label_ = nullptr;
    QLabel* preview_status_label_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* restart_animation_button_ = nullptr;
    RenderViewport* preview_viewport_ = nullptr;
    QLabel* aigril_library_label_ = nullptr;
    QFrame* eye_expression_section_ = nullptr;
    QLabel* eye_expression_title_label_ = nullptr;
    QVBoxLayout* eye_expression_controls_layout_ = nullptr;
    QLabel* eye_expression_empty_label_ = nullptr;
    QPushButton* eye_expression_reset_button_ = nullptr;
    QPushButton* eye_expression_blink_curve_button_ = nullptr;
    QSlider* eye_gaze_yaw_slider_ = nullptr;
    QSlider* eye_gaze_pitch_slider_ = nullptr;
    QSlider* eye_gaze_weight_slider_ = nullptr;
    QLabel* eye_gaze_yaw_value_label_ = nullptr;
    QLabel* eye_gaze_pitch_value_label_ = nullptr;
    QLabel* eye_gaze_weight_value_label_ = nullptr;
    QVector<ExpressionSliderBinding> eye_expression_sliders_;
    QComboBox* aigril_target_combo_ = nullptr;
    QComboBox* aigril_source_combo_ = nullptr;
    QTextBrowser* agent_view_ = nullptr;
    QTableWidget* mapping_table_ = nullptr;
    QPushButton* aigril_scan_button_ = nullptr;
    QPushButton* aigril_load_target_button_ = nullptr;
    QPushButton* aigril_load_source_button_ = nullptr;
    QPushButton* target_button_ = nullptr;
    QPushButton* source_button_ = nullptr;
    QPushButton* generate_button_ = nullptr;
    QPushButton* export_button_ = nullptr;
};

} // namespace haorendergi
