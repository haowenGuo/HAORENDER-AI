#pragma once

#include "app/llm_lookdev_client.h"
#include "app/lookdev_ai.h"
#include "app/render_viewport.h"
#include "rigging/ai_bone_mapper.h"
#include "scene/model_loader.h"
#include "scene/scene_types.h"

#include <QColor>
#include <QMainWindow>

#include <array>
#include <vector>

class QAction;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QVBoxLayout;
class QWidget;

namespace haorendergi {

class SliderField;
class ColorField;
class RigAiWindow;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const QString& startup_model_path,
                        RenderPipeline initial_pipeline = RenderPipeline::Raster,
                        const QString& startup_style_preset_path = QString(),
                        const QString& startup_source_animation_path = QString(),
                        QWidget* parent = nullptr);
    void openRigAiWindow();

private:
    enum class UiLanguage {
        English = 0,
        Chinese = 1
    };

    void applyWorkspaceTheme();
    void buildUi();
    void applyLookDevToViewport();
    void syncShadingModeUi();
    void syncLookDevControls();
    void refreshUiLanguage();
    void showRenderWorkspace();
    QString uiText(const QString& english) const;
    QColor uiBackgroundColorForIndex(int index) const;
    void updateBackgroundControls();
    void chooseUiBackgroundImage();
    void chooseViewportBackgroundImage();
    void generateAiRecommendations();
    LookDevLlmConfig currentLlmConfig() const;
    void finishAiRecommendations(const LookDevLlmResult& result, const QString& prompt);
    void refreshAiCandidateCards();
    void appendAiChatMessage(const QString& speaker, const QString& text);
    void appendAiSystemMessage(const QString& text);
    void applyStylePreset(const StylePreset& preset, bool switch_pipeline);
    bool loadStylePresetFromFile(const QString& path);
    void saveStylePreset();
    void loadStylePreset();
    void loadModel(const QString& path, bool activate_loaded_ray_trace_scene = true);
    void previewRetargetedRigAnimation(const QString& target_path,
                                       const QString& source_path,
                                       const BoneMappingResult& mapping_result);
    void previewRigAnimationWithAutoMapping(const QString& target_path, const QString& source_path);
    void loadDefaultModel();
    void saveSnapshot();
    QString findDefaultModelPath() const;
    void updateSceneSummary();
    void updateRenderStats(const RenderStats& stats);
    void updateViewportChrome();
    QString currentShadingLabel() const;
    QString currentRayTraceViewLabel() const;
    QString currentRayTraceIntegratorLabel() const;
    void refreshEyeExpressionPanel();
    void resetEyeExpressionControls();
    void applySoftBlinkExpressionCurve();
    void applyExpressionWeight(const QString& expression_name, double weight);
    void applyExpressionCurveFromAgent(const std::vector<ExpressionChannelData>& channels,
                                       double duration_seconds,
                                       const QString& summary);
    void applyGazeCurveFromAgent(const std::vector<EyeGazeKeyframeData>& keys,
                                 double duration_seconds,
                                 const QString& summary);
    void applyEyeGazeControl();
    void toggleAnimationPlayback();
    void restartAnimationPlayback();
    void refreshPlaybackControls();

    struct ExpressionSliderBinding {
        QString expression_name;
        SliderField* slider = nullptr;
    };

    RenderViewport* viewport_ = nullptr;
    QLabel* workspace_subtitle_label_ = nullptr;
    QLabel* viewport_title_label_ = nullptr;
    QLabel* viewport_subtitle_label_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* restart_animation_button_ = nullptr;
    QLabel* model_path_value_ = nullptr;
    QLabel* mesh_value_ = nullptr;
    QLabel* triangle_value_ = nullptr;
    QLabel* vertex_value_ = nullptr;
    QLabel* backend_value_ = nullptr;
    QLabel* frame_value_ = nullptr;
    QLabel* shadow_value_ = nullptr;
    QLabel* main_value_ = nullptr;
    QLabel* note_value_ = nullptr;
    QFrame* eye_expression_section_ = nullptr;
    QVBoxLayout* eye_expression_controls_layout_ = nullptr;
    QLabel* eye_expression_empty_label_ = nullptr;
    QPushButton* eye_expression_reset_button_ = nullptr;
    QPushButton* eye_expression_blink_curve_button_ = nullptr;
    SliderField* eye_gaze_yaw_slider_ = nullptr;
    SliderField* eye_gaze_pitch_slider_ = nullptr;
    SliderField* eye_gaze_weight_slider_ = nullptr;
    QVector<ExpressionSliderBinding> eye_expression_sliders_;

    QComboBox* render_pipeline_combo_ = nullptr;
    QComboBox* shading_combo_ = nullptr;
    SliderField* exposure_slider_ = nullptr;
    SliderField* normal_strength_slider_ = nullptr;
    QCheckBox* shadows_check_ = nullptr;
    QCheckBox* culling_check_ = nullptr;

    QComboBox* language_combo_ = nullptr;
    QComboBox* background_combo_ = nullptr;
    QComboBox* viewport_background_combo_ = nullptr;
    QPushButton* ui_background_image_button_ = nullptr;
    QPushButton* viewport_background_image_button_ = nullptr;

    QStackedWidget* shading_detail_stack_ = nullptr;

    QCheckBox* ibl_enabled_check_ = nullptr;
    SliderField* ibl_diffuse_slider_ = nullptr;
    SliderField* ibl_specular_slider_ = nullptr;
    SliderField* sky_light_slider_ = nullptr;
    QComboBox* metallic_channel_combo_ = nullptr;
    QComboBox* roughness_channel_combo_ = nullptr;
    QComboBox* ao_channel_combo_ = nullptr;
    QComboBox* emissive_channel_combo_ = nullptr;

    QComboBox* ray_trace_view_combo_ = nullptr;
    QFrame* ray_trace_section_ = nullptr;
    QComboBox* ray_trace_scene_combo_ = nullptr;
    QComboBox* ray_trace_integrator_combo_ = nullptr;
    SliderField* ray_trace_ambient_slider_ = nullptr;
    SliderField* ray_trace_shadow_slider_ = nullptr;
    SliderField* ray_trace_bounces_slider_ = nullptr;
    SliderField* ray_trace_nee_bounces_slider_ = nullptr;
    SliderField* ray_trace_spp_slider_ = nullptr;
    QCheckBox* ray_trace_nee_check_ = nullptr;
    QCheckBox* ray_trace_photon_check_ = nullptr;
    SliderField* ray_trace_photon_radius_slider_ = nullptr;
    SliderField* ray_trace_photon_intensity_slider_ = nullptr;

    QCheckBox* phong_hard_specular_check_ = nullptr;
    QCheckBox* phong_tonemap_check_ = nullptr;
    QCheckBox* phong_primary_only_check_ = nullptr;
    SliderField* phong_secondary_slider_ = nullptr;
    SliderField* phong_diffuse_slider_ = nullptr;
    SliderField* phong_ambient_slider_ = nullptr;
    ColorField* phong_ambient_color_field_ = nullptr;
    SliderField* phong_specular_slider_ = nullptr;
    ColorField* phong_specular_tint_field_ = nullptr;
    SliderField* phong_smoothness_slider_ = nullptr;
    SliderField* phong_specular_map_weight_slider_ = nullptr;
    SliderField* phong_shininess_slider_ = nullptr;
    SliderField* phong_rim_strength_slider_ = nullptr;
    SliderField* phong_rim_power_slider_ = nullptr;
    ColorField* phong_rim_tint_field_ = nullptr;
    QCheckBox* phong_toon_enabled_check_ = nullptr;
    SliderField* phong_toon_steps_slider_ = nullptr;
    SliderField* phong_toon_softness_slider_ = nullptr;
    SliderField* phong_toon_shadow_floor_slider_ = nullptr;
    SliderField* phong_toon_lit_floor_slider_ = nullptr;
    SliderField* phong_toon_ramp_bias_slider_ = nullptr;
    SliderField* phong_toon_ramp_contrast_slider_ = nullptr;
    SliderField* phong_toon_shadow_map_strength_slider_ = nullptr;
    SliderField* phong_toon_shadow_threshold_slider_ = nullptr;
    SliderField* phong_toon_shadow_softness_slider_ = nullptr;
    ColorField* phong_toon_shadow_tint_field_ = nullptr;
    SliderField* phong_toon_highlight_threshold_slider_ = nullptr;
    SliderField* phong_toon_highlight_softness_slider_ = nullptr;
    SliderField* phong_toon_highlight_strength_slider_ = nullptr;
    ColorField* phong_toon_highlight_tint_field_ = nullptr;
    SliderField* phong_toon_rim_threshold_slider_ = nullptr;
    SliderField* phong_toon_rim_softness_slider_ = nullptr;
    QCheckBox* phong_toon_material_override_check_ = nullptr;
    SliderField* phong_toon_material_texture_strength_slider_ = nullptr;
    SliderField* phong_toon_material_lift_slider_ = nullptr;
    SliderField* phong_toon_material_saturation_slider_ = nullptr;
    SliderField* phong_toon_material_contrast_slider_ = nullptr;
    QCheckBox* phong_outline_enabled_check_ = nullptr;
    SliderField* phong_outline_width_slider_ = nullptr;
    SliderField* phong_outline_opacity_slider_ = nullptr;
    SliderField* phong_outline_depth_bias_slider_ = nullptr;
    ColorField* phong_outline_color_field_ = nullptr;

    QPlainTextEdit* ai_prompt_edit_ = nullptr;
    QLineEdit* ai_base_url_edit_ = nullptr;
    QLineEdit* ai_model_edit_ = nullptr;
    QLineEdit* ai_api_key_edit_ = nullptr;
    QTextBrowser* ai_chat_view_ = nullptr;
    QLabel* ai_reply_label_ = nullptr;
    QPushButton* ai_recommend_button_ = nullptr;
    QPushButton* ai_save_preset_button_ = nullptr;
    QPushButton* ai_load_preset_button_ = nullptr;
    std::array<QLabel*, 3> ai_candidate_slot_labels_ {};
    std::array<QLabel*, 3> ai_candidate_name_labels_ {};
    std::array<QLabel*, 3> ai_candidate_summary_labels_ {};
    std::array<QLabel*, 3> ai_candidate_reason_labels_ {};
    std::array<QPushButton*, 3> ai_candidate_apply_buttons_ {};
    std::array<QFrame*, 3> ai_candidate_cards_ {};

    QAction* open_model_action_ = nullptr;
    QAction* render_workspace_action_ = nullptr;
    QAction* open_rig_ai_action_ = nullptr;
    QAction* reset_camera_action_ = nullptr;
    QAction* save_snapshot_action_ = nullptr;
    QStackedWidget* workspace_stack_ = nullptr;
    QWidget* render_workspace_page_ = nullptr;
    RigAiWindow* rig_ai_window_ = nullptr;
    bool rig_ai_startup_paths_applied_ = false;

    ModelLoader model_loader_;
    SceneModel scene_;
    RenderPipeline render_pipeline_ = RenderPipeline::Raster;
    UiLanguage ui_language_ = UiLanguage::English;
    int ui_theme_index_ = 0;
    int viewport_background_index_ = 0;
    QColor viewport_background_color_ = QColor(23, 27, 32);
    QString ui_background_image_path_;
    QString viewport_background_image_path_;
    LookDevSettings look_settings_;
    QString startup_model_path_;
    QString startup_style_preset_path_;
    QString startup_source_animation_path_;
    QString current_model_path_;
    QString ai_dialogue_context_;
    QVector<AiRecommendationCandidate> ai_candidates_;
    LookDevLlmClient ai_client_;
};

} // namespace haorendergi
