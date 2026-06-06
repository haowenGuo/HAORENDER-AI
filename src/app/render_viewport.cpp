#include "app/render_viewport.h"

#include "rendering/dxr_ray_tracer.h"
#include "rendering/opengl_ray_tracer.h"
#include "rendering/opengl_rasterizer.h"
#include "scene/animation_sampler.h"
#include "scene/vrm_expression_controller.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace haorendergi {
namespace {

Bounds normalizedBounds() {
    Bounds bounds;
    bounds.include(Eigen::Vector3f(-1.0f, -1.0f, -1.0f));
    bounds.include(Eigen::Vector3f(1.0f, 1.0f, 1.0f));
    return bounds;
}

std::unique_ptr<IRenderBackend> createBackend(RenderPipeline pipeline) {
    if (pipeline == RenderPipeline::DxrRayTrace) {
        return std::make_unique<DxrRayTracer>();
    }
    if (pipeline == RenderPipeline::RayTrace) {
        return std::make_unique<OpenGLRayTracer>();
    }
    return std::make_unique<OpenGLRasterizer>();
}

bool sameExpressionName(const std::string& lhs, const std::string& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

ExpressionChannelData expressionCurveInTicks(const ExpressionChannelData& channel, double ticks_per_second) {
    ExpressionChannelData converted = channel;
    const double scale = ticks_per_second > 0.0 ? ticks_per_second : 1.0;
    for (ScalarKeyframe& key : converted.weights) {
        key.time_ticks *= scale;
    }
    return converted;
}

void mergeExpressionCurveChannels(AnimationClipData* clip,
                                  const std::vector<ExpressionChannelData>& curve_channels,
                                  double duration_seconds) {
    if (!clip || curve_channels.empty()) {
        return;
    }

    const double tps = clip->ticks_per_second > 0.0 ? clip->ticks_per_second : 1.0;
    for (const ExpressionChannelData& source_channel : curve_channels) {
        clip->expression_channels.erase(
            std::remove_if(clip->expression_channels.begin(),
                           clip->expression_channels.end(),
                           [&](const ExpressionChannelData& existing) {
                               return sameExpressionName(existing.name, source_channel.name);
                           }),
            clip->expression_channels.end());
        clip->expression_channels.push_back(expressionCurveInTicks(source_channel, tps));
    }
    clip->duration_ticks = std::max(clip->duration_ticks, std::max(duration_seconds, 0.1) * tps);
}

EyeGazeControlData sampleEyeGazeKeys(const std::vector<EyeGazeKeyframeData>& keys,
                                     double time_seconds,
                                     double duration_seconds) {
    EyeGazeControlData control;
    if (keys.empty()) {
        return control;
    }

    const double duration = std::max(duration_seconds, 0.1);
    double t = std::fmod(std::max(0.0, time_seconds), duration);
    if (t < 0.0) {
        t += duration;
    }

    const auto lerp = [](float a, float b, float alpha) {
        return (1.0f - alpha) * a + alpha * b;
    };

    const EyeGazeKeyframeData* first = &keys.front();
    const EyeGazeKeyframeData* last = &keys.back();
    if (keys.size() == 1 || t <= first->time_ticks) {
        control.enabled = first->weight > 1e-5f;
        control.yaw_degrees = first->yaw_degrees;
        control.pitch_degrees = first->pitch_degrees;
        control.weight = first->weight;
        return control;
    }
    if (t >= last->time_ticks) {
        control.enabled = last->weight > 1e-5f;
        control.yaw_degrees = last->yaw_degrees;
        control.pitch_degrees = last->pitch_degrees;
        control.weight = last->weight;
        return control;
    }

    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const EyeGazeKeyframeData& a = keys[i];
        const EyeGazeKeyframeData& b = keys[i + 1];
        if (t >= a.time_ticks && t <= b.time_ticks) {
            const double span = std::max(1e-8, b.time_ticks - a.time_ticks);
            const float alpha = static_cast<float>((t - a.time_ticks) / span);
            control.yaw_degrees = lerp(a.yaw_degrees, b.yaw_degrees, alpha);
            control.pitch_degrees = lerp(a.pitch_degrees, b.pitch_degrees, alpha);
            control.weight = std::clamp(lerp(a.weight, b.weight, alpha), 0.0f, 1.0f);
            control.enabled = control.weight > 1e-5f;
            return control;
        }
    }
    return control;
}

} // namespace

RenderViewport::RenderViewport(QWidget* parent)
    : QOpenGLWidget(parent),
      backend_(createBackend(render_pipeline_)) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    qApp->installEventFilter(this);
    navigation_timer_.start();
    animation_timer_.start();
}

RenderViewport::~RenderViewport() {
    qApp->removeEventFilter(this);
    if (context()) {
        makeCurrent();
        if (backend_) {
            backend_->shutdown();
        }
        doneCurrent();
    }
}

void RenderViewport::setScene(const SceneModel* scene) {
    scene_ = scene;
    animated_scene_ = scene_ ? *scene_ : SceneModel();
    animated_scene_valid_ = false;
    animation_time_seconds_ = 0.0;
    active_animation_index_ = 0;
    expression_overrides_.clear();
    expression_curve_channels_.clear();
    expression_curve_duration_seconds_ = 1.0;
    eye_gaze_override_ = EyeGazeControlData();
    eye_gaze_curve_keys_.clear();
    eye_gaze_curve_duration_seconds_ = 1.0;
    animation_timer_.restart();
    scene_upload_pending_ = true;
    model_matrix_ = (scene_ && scene_->bounds.valid()) ? buildModelMatrix(scene_->bounds) : Eigen::Matrix4f::Identity();
    if (usesCornellCamera()) {
        resetCornellCamera();
    } else if (scene_ && scene_->has_camera) {
        const auto transformPoint = [this](const Eigen::Vector3f& point) {
            const Eigen::Vector4f p(point.x(), point.y(), point.z(), 1.0f);
            return (model_matrix_ * p).hnormalized();
        };
        camera_.setView(transformPoint(scene_->camera_position),
                        transformPoint(scene_->camera_target),
                        scene_->camera_fov_degrees,
                        normalizedBounds());
    } else {
        resetCamera();
    }
    update();
}

void RenderViewport::resetCamera() {
    if (usesCornellCamera()) {
        resetCornellCamera();
    }
    else if (scene_ && !scene_->empty()) {
        camera_.reset(normalizedBounds());
    }
    else {
        camera_.reset(normalizedBounds());
    }
    update();
}

void RenderViewport::setRenderPipeline(RenderPipeline pipeline) {
    if (render_pipeline_ == pipeline) {
        return;
    }
    render_pipeline_ = pipeline;
    if ((pipeline == RenderPipeline::RayTrace || pipeline == RenderPipeline::DxrRayTrace) &&
        look_settings_.ray_trace.scene_mode == RayTraceSceneMode::CornellBox) {
        resetCornellCamera();
    }
    resetBackend();
    scene_upload_pending_ = true;
    update();
}

void RenderViewport::setLookDevSettings(const LookDevSettings& settings) {
    const bool was_cornell = usesCornellCamera();
    look_settings_ = settings;
    const bool is_cornell = usesCornellCamera();
    if (!was_cornell && is_cornell) {
        resetCornellCamera();
    }
    update();
}

void RenderViewport::setShadingModel(ShadingModel shading_model) {
    look_settings_.shading_model = shading_model;
    update();
}

void RenderViewport::setShadowsEnabled(bool enabled) {
    look_settings_.enable_shadows = enabled;
    update();
}

void RenderViewport::setBackfaceCullingEnabled(bool enabled) {
    look_settings_.enable_backface_culling = enabled;
    update();
}

void RenderViewport::setExposure(float exposure) {
    look_settings_.exposure = exposure;
    update();
}

void RenderViewport::setNormalStrength(float strength) {
    look_settings_.normal_strength = strength;
    update();
}

void RenderViewport::setClearColor(const QColor& color) {
    if (clear_color_ == color) {
        return;
    }
    clear_color_ = color;
    update();
}

void RenderViewport::setBackgroundImage(const QImage& image) {
    background_image_ = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    clear_color_ = QColor(0, 0, 0, 0);
    update();
}

void RenderViewport::clearBackgroundImage() {
    if (background_image_.isNull()) {
        return;
    }
    background_image_ = QImage();
    update();
}

void RenderViewport::setAnimationPlaybackEnabled(bool enabled) {
    if (animation_playback_enabled_ == enabled) {
        return;
    }
    animation_playback_enabled_ = enabled;
    animation_timer_.restart();
    update();
}

void RenderViewport::restartAnimationPlayback() {
    animation_time_seconds_ = 0.0;
    animation_timer_.restart();
    animated_scene_valid_ = false;
    scene_upload_pending_ = true;
    update();
}

bool RenderViewport::setExpressionWeight(const std::string& name, float weight) {
    if (!scene_ || !findVrmExpression(*scene_, name)) {
        return false;
    }

    expression_curve_channels_.clear();
    const float clamped_weight = std::clamp(weight, 0.0f, 1.0f);
    auto found = std::find_if(expression_overrides_.begin(), expression_overrides_.end(), [&](const ExpressionWeightData& item) {
        return item.name == name;
    });
    if (found == expression_overrides_.end()) {
        if (clamped_weight > 1e-5f) {
            expression_overrides_.push_back(ExpressionWeightData{ name, clamped_weight });
        }
    } else if (clamped_weight <= 1e-5f) {
        expression_overrides_.erase(found);
    } else {
        found->weight = clamped_weight;
    }

    scene_upload_pending_ = true;
    update();
    return true;
}

void RenderViewport::clearExpressionWeights() {
    if (expression_overrides_.empty()) {
        return;
    }
    expression_overrides_.clear();
    if (scene_) {
        animated_scene_ = *scene_;
    }
    animated_scene_valid_ = false;
    scene_upload_pending_ = true;
    update();
}

void RenderViewport::setExpressionCurvePreview(std::vector<ExpressionChannelData> channels, double duration_seconds) {
    expression_overrides_.clear();
    expression_curve_channels_ = std::move(channels);
    expression_curve_duration_seconds_ = std::max(duration_seconds, 0.1);
    animation_time_seconds_ = 0.0;
    animation_timer_.restart();
    scene_upload_pending_ = true;
    update();
}

void RenderViewport::clearExpressionCurvePreview() {
    if (expression_curve_channels_.empty()) {
        return;
    }
    expression_curve_channels_.clear();
    expression_curve_duration_seconds_ = 1.0;
    if (scene_) {
        animated_scene_ = *scene_;
    }
    animated_scene_valid_ = false;
    scene_upload_pending_ = true;
    update();
}

void RenderViewport::setEyeGaze(float yaw_degrees, float pitch_degrees, float weight) {
    eye_gaze_curve_keys_.clear();
    eye_gaze_override_.yaw_degrees = yaw_degrees;
    eye_gaze_override_.pitch_degrees = pitch_degrees;
    eye_gaze_override_.weight = std::clamp(weight, 0.0f, 1.0f);
    eye_gaze_override_.enabled = eye_gaze_override_.weight > 1e-5f &&
        (std::abs(yaw_degrees) > 1e-4f || std::abs(pitch_degrees) > 1e-4f);
    scene_upload_pending_ = true;
    update();
}

void RenderViewport::clearEyeGaze() {
    eye_gaze_override_ = EyeGazeControlData();
    if (scene_) {
        animated_scene_ = *scene_;
    }
    animated_scene_valid_ = false;
    scene_upload_pending_ = true;
    update();
}

void RenderViewport::setEyeGazeCurvePreview(std::vector<EyeGazeKeyframeData> keys, double duration_seconds) {
    eye_gaze_override_ = EyeGazeControlData();
    std::sort(keys.begin(), keys.end(), [](const EyeGazeKeyframeData& lhs, const EyeGazeKeyframeData& rhs) {
        return lhs.time_ticks < rhs.time_ticks;
    });
    eye_gaze_curve_keys_ = std::move(keys);
    eye_gaze_curve_duration_seconds_ = std::max(duration_seconds, 0.1);
    animation_time_seconds_ = 0.0;
    animation_timer_.restart();
    scene_upload_pending_ = true;
    update();
}

void RenderViewport::clearEyeGazeCurvePreview() {
    if (eye_gaze_curve_keys_.empty()) {
        return;
    }
    eye_gaze_curve_keys_.clear();
    eye_gaze_curve_duration_seconds_ = 1.0;
    if (scene_) {
        animated_scene_ = *scene_;
    }
    animated_scene_valid_ = false;
    scene_upload_pending_ = true;
    update();
}

void RenderViewport::setStatsCallback(std::function<void(const RenderStats&)> callback) {
    stats_callback_ = std::move(callback);
}

QImage RenderViewport::captureCurrentFrame() {
    return grabFramebuffer();
}

QString RenderViewport::backendName() const {
    return backend_ ? backend_->backendName() : QStringLiteral("Unavailable");
}

bool RenderViewport::eventFilter(QObject* watched, QEvent* event) {
    if (watched == this) {
        return false;
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (globalPointInViewport(mouse_event->globalPos())) {
            return handleViewportMousePress(mouse_event);
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (rotating_ || panning_ || globalPointInViewport(mouse_event->globalPos())) {
            return handleViewportMouseRelease(mouse_event);
        }
        break;
    }
    case QEvent::MouseMove: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (rotating_ || panning_ || globalPointInViewport(mouse_event->globalPos())) {
            return handleViewportMouseMove(mouse_event);
        }
        break;
    }
    case QEvent::Wheel: {
        auto* wheel_event = static_cast<QWheelEvent*>(event);
        if (globalPointInViewport(wheel_event->globalPos())) {
            return handleViewportWheel(wheel_event);
        }
        break;
    }
    case QEvent::KeyPress:
        if (hasFocus() || underMouse()) {
            return handleViewportKeyPress(static_cast<QKeyEvent*>(event));
        }
        break;
    case QEvent::KeyRelease:
        if (hasFocus() || underMouse() || !pressed_keys_.isEmpty()) {
            return handleViewportKeyRelease(static_cast<QKeyEvent*>(event));
        }
        break;
    default:
        break;
    }

    return QOpenGLWidget::eventFilter(watched, event);
}

void RenderViewport::initializeGL() {
    if (!backend_) {
        backend_ = createBackend(render_pipeline_);
    }
    QString error_message;
    if (!backend_->initialize(&error_message)) {
        initialization_error_ = error_message;
    }
    setFocus(Qt::OtherFocusReason);
    scene_upload_pending_ = true;
}

void RenderViewport::resizeGL(int width, int height) {
    camera_.setViewport(width, height);
    if (backend_) {
        backend_->resize(std::max(1, static_cast<int>(std::round(width * devicePixelRatioF()))),
                         std::max(1, static_cast<int>(std::round(height * devicePixelRatioF()))));
    }
}

void RenderViewport::paintGL() {
    if (!backend_) {
        return;
    }
    if (!initialization_error_.isEmpty()) {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(25, 28, 33));
        painter.setPen(Qt::white);
        painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, initialization_error_);
        return;
    }

    backend_->resize(std::max(1, static_cast<int>(std::round(width() * devicePixelRatioF()))),
                     std::max(1, static_cast<int>(std::round(height() * devicePixelRatioF()))));
    const SceneModel* render_scene = currentRenderScene();
    uploadSceneIfNeeded();
    applyKeyboardNavigation();

    FrameRenderSettings settings;
    settings.scene = render_scene;
    settings.model_matrix = model_matrix_;
    settings.view_matrix = camera_.viewMatrix();
    settings.projection_matrix = camera_.projectionMatrix();
    settings.camera_position = camera_.position();
    settings.camera_target = camera_.target();
    settings.clear_color = clear_color_;
    settings.look_dev = look_settings_;

    last_stats_ = backend_->render(settings);
    if (!background_image_.isNull()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOver);
        painter.drawImage(rect(), background_image_);
    }
    if (stats_callback_) {
        stats_callback_(last_stats_);
    }
    const bool animated_raster = render_scene && render_scene->hasAnimations() && render_pipeline_ == RenderPipeline::Raster && animation_playback_enabled_;
    const bool progressive_ray_trace = (render_pipeline_ == RenderPipeline::RayTrace ||
                                        render_pipeline_ == RenderPipeline::DxrRayTrace) &&
        look_settings_.ray_trace.view_mode == RayTraceViewMode::Lit &&
        look_settings_.ray_trace.integrator != RayTraceIntegrator::Hybrid;
    if (animated_raster || progressive_ray_trace) {
        update();
    }
}

void RenderViewport::mousePressEvent(QMouseEvent* event) {
    handleViewportMousePress(event);
}

void RenderViewport::mouseReleaseEvent(QMouseEvent* event) {
    handleViewportMouseRelease(event);
}

void RenderViewport::mouseMoveEvent(QMouseEvent* event) {
    handleViewportMouseMove(event);
}

void RenderViewport::wheelEvent(QWheelEvent* event) {
    handleViewportWheel(event);
}

void RenderViewport::keyPressEvent(QKeyEvent* event) {
    handleViewportKeyPress(event);
}

void RenderViewport::keyReleaseEvent(QKeyEvent* event) {
    handleViewportKeyRelease(event);
}

void RenderViewport::focusOutEvent(QFocusEvent* event) {
    pressed_keys_.clear();
    rotating_ = false;
    panning_ = false;
    releaseMouse();
    releaseKeyboard();
    QOpenGLWidget::focusOutEvent(event);
}

bool RenderViewport::usesCornellCamera() const {
    return (render_pipeline_ == RenderPipeline::RayTrace ||
            render_pipeline_ == RenderPipeline::DxrRayTrace) &&
           look_settings_.ray_trace.scene_mode == RayTraceSceneMode::CornellBox;
}

void RenderViewport::resetCornellCamera() {
    camera_.setView(Eigen::Vector3f(0.0f, 0.10f, 3.75f),
                    Eigen::Vector3f(0.0f, -0.58f, -0.18f),
                    58.0f,
                    normalizedBounds());
}

bool RenderViewport::globalPointInViewport(const QPoint& global_position) const {
    if (!isVisible()) {
        return false;
    }
    const QPoint local = mapFromGlobal(global_position);
    return rect().contains(local);
}

bool RenderViewport::handleViewportMousePress(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    grabMouse();
    grabKeyboard();
    if (event->button() == Qt::LeftButton) {
        rotating_ = true;
    }
    else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        panning_ = true;
    }
    last_mouse_position_ = mapFromGlobal(event->globalPos());
    event->accept();
    return true;
}

bool RenderViewport::handleViewportMouseRelease(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        rotating_ = false;
    }
    else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        panning_ = false;
    }
    if (!rotating_ && !panning_) {
        releaseMouse();
        releaseKeyboard();
    }
    event->accept();
    return true;
}

bool RenderViewport::handleViewportMouseMove(QMouseEvent* event) {
    const QPoint local_position = mapFromGlobal(event->globalPos());
    const QPoint delta = local_position - last_mouse_position_;
    last_mouse_position_ = local_position;

    if (rotating_ && (event->buttons() & Qt::LeftButton)) {
        camera_.orbit(-static_cast<float>(delta.x()) * 0.008f, -static_cast<float>(delta.y()) * 0.008f);
        update();
    }
    else if (panning_ && (event->buttons() & (Qt::RightButton | Qt::MiddleButton))) {
        camera_.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
        update();
    }
    event->accept();
    return rotating_ || panning_ || globalPointInViewport(event->globalPos());
}

bool RenderViewport::handleViewportWheel(QWheelEvent* event) {
    setFocus(Qt::MouseFocusReason);
    if (event->angleDelta().y() != 0) {
        camera_.zoom(static_cast<float>(event->angleDelta().y()) / 120.0f);
        update();
    }
    event->accept();
    return true;
}

bool RenderViewport::handleViewportKeyPress(QKeyEvent* event) {
    if (!event->isAutoRepeat()) {
        pressed_keys_.insert(event->key());
    }

    if (event->key() == Qt::Key_R) {
        resetCamera();
        event->accept();
        return true;
    }

    update();
    event->accept();
    return true;
}

bool RenderViewport::handleViewportKeyRelease(QKeyEvent* event) {
    if (!event->isAutoRepeat()) {
        pressed_keys_.remove(event->key());
    }
    update();
    event->accept();
    return true;
}

bool RenderViewport::applyKeyboardNavigation() {
    const qint64 elapsed_ms = navigation_timer_.isValid() ? navigation_timer_.restart() : 16;
    if (!navigation_timer_.isValid()) {
        navigation_timer_.start();
    }
    if (pressed_keys_.isEmpty()) {
        return false;
    }

    const float dt = std::clamp(static_cast<float>(elapsed_ms) / 1000.0f, 0.001f, 0.05f);
    float move_speed = std::max(camera_.distance() * 0.85f, 0.65f);
    float orbit_speed = 1.65f;
    if (pressed_keys_.contains(Qt::Key_Shift)) {
        move_speed *= 3.0f;
        orbit_speed *= 1.8f;
    }
    if (pressed_keys_.contains(Qt::Key_Control)) {
        move_speed *= 0.25f;
        orbit_speed *= 0.45f;
    }

    float right_delta = 0.0f;
    float up_delta = 0.0f;
    float forward_delta = 0.0f;
    float yaw_delta = 0.0f;
    float pitch_delta = 0.0f;

    if (pressed_keys_.contains(Qt::Key_W)) {
        forward_delta += move_speed * dt;
    }
    if (pressed_keys_.contains(Qt::Key_S)) {
        forward_delta -= move_speed * dt;
    }
    if (pressed_keys_.contains(Qt::Key_D)) {
        right_delta += move_speed * dt;
    }
    if (pressed_keys_.contains(Qt::Key_A)) {
        right_delta -= move_speed * dt;
    }
    if (pressed_keys_.contains(Qt::Key_E)) {
        up_delta += move_speed * dt;
    }
    if (pressed_keys_.contains(Qt::Key_Q)) {
        up_delta -= move_speed * dt;
    }
    if (pressed_keys_.contains(Qt::Key_Left)) {
        yaw_delta += orbit_speed * dt;
    }
    if (pressed_keys_.contains(Qt::Key_Right)) {
        yaw_delta -= orbit_speed * dt;
    }
    if (pressed_keys_.contains(Qt::Key_Up)) {
        pitch_delta += orbit_speed * dt;
    }
    if (pressed_keys_.contains(Qt::Key_Down)) {
        pitch_delta -= orbit_speed * dt;
    }

    const bool moved = std::abs(right_delta) > 0.0f ||
                       std::abs(up_delta) > 0.0f ||
                       std::abs(forward_delta) > 0.0f ||
                       std::abs(yaw_delta) > 0.0f ||
                       std::abs(pitch_delta) > 0.0f;
    if (!moved) {
        return false;
    }

    camera_.moveLocal(right_delta, up_delta, forward_delta);
    camera_.orbit(yaw_delta, pitch_delta);
    update();
    return true;
}

void RenderViewport::uploadSceneIfNeeded() {
    if (!scene_upload_pending_ || !scene_) {
        return;
    }
    const SceneModel* render_scene = animated_scene_valid_ ? &animated_scene_ : scene_;
    backend_->uploadScene(*render_scene);
    scene_upload_pending_ = false;
}

const SceneModel* RenderViewport::currentRenderScene() {
    if (!scene_) {
        return nullptr;
    }
    if (updateAnimatedScene()) {
        return &animated_scene_;
    }
    return animated_scene_valid_ ? &animated_scene_ : scene_;
}

bool RenderViewport::updateAnimatedScene() {
    const bool has_expression_controls = !expression_overrides_.empty() || !expression_curve_channels_.empty();
    const bool has_gaze_controls = eye_gaze_override_.enabled || !eye_gaze_curve_keys_.empty();
    if (!scene_ ||
        !scene_->hasSkinnedMeshes() ||
        render_pipeline_ != RenderPipeline::Raster ||
        (!scene_->hasAnimations() && !has_expression_controls && !has_gaze_controls)) {
        return false;
    }

    if (animation_playback_enabled_) {
        const qint64 elapsed_ms = animation_timer_.isValid() ? animation_timer_.restart() : 16;
        animation_time_seconds_ += std::clamp(static_cast<double>(elapsed_ms) / 1000.0, 0.0, 0.1);
    } else if (!animation_timer_.isValid()) {
        animation_timer_.start();
    } else {
        animation_timer_.restart();
    }

    SceneModel runtime_scene = *scene_;
    if (!expression_overrides_.empty()) {
        runtime_scene.expression_weights = expression_overrides_;
    }
    if (!eye_gaze_curve_keys_.empty()) {
        runtime_scene.eye_gaze = sampleEyeGazeKeys(eye_gaze_curve_keys_, animation_time_seconds_, eye_gaze_curve_duration_seconds_);
    } else if (eye_gaze_override_.enabled) {
        runtime_scene.eye_gaze = eye_gaze_override_;
    }
    int animation_index = active_animation_index_;
    if (runtime_scene.animations.empty()) {
        AnimationClipData expression_clip;
        expression_clip.name = "Expression Preview";
        expression_clip.ticks_per_second = 1.0;
        expression_clip.duration_ticks = std::max({ expression_curve_duration_seconds_, eye_gaze_curve_duration_seconds_, 1.0 });
        expression_clip.expression_channels = expression_curve_channels_;
        runtime_scene.animations.push_back(std::move(expression_clip));
        animation_index = 0;
    } else if (!expression_curve_channels_.empty()) {
        animation_index = std::clamp(active_animation_index_, 0, static_cast<int>(runtime_scene.animations.size()) - 1);
        mergeExpressionCurveChannels(&runtime_scene.animations[animation_index],
                                     expression_curve_channels_,
                                     expression_curve_duration_seconds_);
    }

    const bool sampled = sampleSceneAnimation(runtime_scene, animation_index, animation_time_seconds_, &animated_scene_);
    if (sampled) {
        animated_scene_valid_ = true;
        scene_upload_pending_ = true;
    } else {
        animated_scene_valid_ = false;
    }
    return sampled;
}

void RenderViewport::resetBackend() {
    initialization_error_.clear();
    if (context()) {
        makeCurrent();
        if (backend_) {
            backend_->shutdown();
        }
        backend_ = createBackend(render_pipeline_);
        QString error_message;
        if (!backend_->initialize(&error_message)) {
            initialization_error_ = error_message;
        }
        backend_->resize(std::max(1, static_cast<int>(std::round(width() * devicePixelRatioF()))),
                         std::max(1, static_cast<int>(std::round(height() * devicePixelRatioF()))));
        doneCurrent();
    } else {
        backend_ = createBackend(render_pipeline_);
    }
}

Eigen::Matrix4f RenderViewport::buildModelMatrix(const Bounds& bounds) const {
    const Eigen::Vector3f center = bounds.center();
    const Eigen::Vector3f extent = bounds.extent();
    const float max_extent = std::max({ extent.x(), extent.y(), extent.z(), 1e-4f });
    const float scale = 2.0f / max_extent;

    Eigen::Matrix4f matrix = Eigen::Matrix4f::Identity();
    matrix(0, 0) = scale;
    matrix(1, 1) = scale;
    matrix(2, 2) = scale;
    matrix(0, 3) = -center.x() * scale;
    matrix(1, 3) = -center.y() * scale;
    matrix(2, 3) = -center.z() * scale;
    return matrix;
}

} // namespace haorendergi
