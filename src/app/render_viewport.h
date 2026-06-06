#pragma once

#include "core/camera.h"
#include "rendering/render_backend.h"

#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QImage>
#include <QPoint>
#include <QSet>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace haorendergi {

class RenderViewport final : public QOpenGLWidget {
public:
    explicit RenderViewport(QWidget* parent = nullptr);
    ~RenderViewport() override;

    void setScene(const SceneModel* scene);
    void resetCamera();
    void setRenderPipeline(RenderPipeline pipeline);
    RenderPipeline renderPipeline() const { return render_pipeline_; }
    void setLookDevSettings(const LookDevSettings& settings);
    const LookDevSettings& lookDevSettings() const { return look_settings_; }
    void setShadingModel(ShadingModel shading_model);
    void setShadowsEnabled(bool enabled);
    void setBackfaceCullingEnabled(bool enabled);
    void setExposure(float exposure);
    void setNormalStrength(float strength);
    void setClearColor(const QColor& color);
    QColor clearColor() const { return clear_color_; }
    void setBackgroundImage(const QImage& image);
    void clearBackgroundImage();
    void setAnimationPlaybackEnabled(bool enabled);
    bool animationPlaybackEnabled() const { return animation_playback_enabled_; }
    void restartAnimationPlayback();
    bool setExpressionWeight(const std::string& name, float weight);
    void clearExpressionWeights();
    void setExpressionCurvePreview(std::vector<ExpressionChannelData> channels, double duration_seconds);
    void clearExpressionCurvePreview();
    void setEyeGaze(float yaw_degrees, float pitch_degrees, float weight);
    void clearEyeGaze();
    void setEyeGazeCurvePreview(std::vector<EyeGazeKeyframeData> keys, double duration_seconds);
    void clearEyeGazeCurvePreview();
    void setStatsCallback(std::function<void(const RenderStats&)> callback);

    QImage captureCurrentFrame();
    const RenderStats& lastStats() const { return last_stats_; }
    QString backendName() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    bool usesCornellCamera() const;
    void resetCornellCamera();
    bool globalPointInViewport(const QPoint& global_position) const;
    bool handleViewportMousePress(QMouseEvent* event);
    bool handleViewportMouseRelease(QMouseEvent* event);
    bool handleViewportMouseMove(QMouseEvent* event);
    bool handleViewportWheel(QWheelEvent* event);
    bool handleViewportKeyPress(QKeyEvent* event);
    bool handleViewportKeyRelease(QKeyEvent* event);
    bool applyKeyboardNavigation();
    void uploadSceneIfNeeded();
    const SceneModel* currentRenderScene();
    bool updateAnimatedScene();
    void resetBackend();
    Eigen::Matrix4f buildModelMatrix(const Bounds& bounds) const;

    std::unique_ptr<IRenderBackend> backend_;
    RenderPipeline render_pipeline_ = RenderPipeline::Raster;
    const SceneModel* scene_ = nullptr;
    SceneModel animated_scene_;
    bool animated_scene_valid_ = false;
    OrbitCamera camera_;
    Eigen::Matrix4f model_matrix_ = Eigen::Matrix4f::Identity();
    RenderStats last_stats_;
    std::function<void(const RenderStats&)> stats_callback_;
    QPoint last_mouse_position_;
    QSet<int> pressed_keys_;
    QElapsedTimer navigation_timer_;
    QElapsedTimer animation_timer_;
    double animation_time_seconds_ = 0.0;
    int active_animation_index_ = 0;
    bool animation_playback_enabled_ = true;
    bool rotating_ = false;
    bool panning_ = false;
    bool scene_upload_pending_ = false;
    std::vector<ExpressionWeightData> expression_overrides_;
    std::vector<ExpressionChannelData> expression_curve_channels_;
    double expression_curve_duration_seconds_ = 1.0;
    EyeGazeControlData eye_gaze_override_;
    std::vector<EyeGazeKeyframeData> eye_gaze_curve_keys_;
    double eye_gaze_curve_duration_seconds_ = 1.0;
    LookDevSettings look_settings_;
    QColor clear_color_ = QColor(23, 27, 32);
    QImage background_image_;
    QString initialization_error_;
};

} // namespace haorendergi
