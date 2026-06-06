#include "core/camera.h"

#include "core/math_utils.h"

#include <algorithm>
#include <cmath>

namespace haorendergi {

void OrbitCamera::setViewport(int width, int height) {
    aspect_ratio_ = static_cast<float>(std::max(width, 1)) / static_cast<float>(std::max(height, 1));
}

void OrbitCamera::setFieldOfView(float degrees) {
    fov_degrees_ = std::clamp(degrees, 20.0f, 100.0f);
}

void OrbitCamera::reset(const Bounds& bounds) {
    target_ = bounds.center();
    yaw_ = 0.0f;
    pitch_ = 0.2f;
    const float radius = std::max(bounds.radius(), 0.6f);
    distance_ = std::max(radius * 2.5f, 2.0f);
    z_near_ = std::max(radius * 0.02f, 0.05f);
    z_far_ = std::max(radius * 12.0f, 50.0f);
}

void OrbitCamera::setView(const Eigen::Vector3f& position,
                          const Eigen::Vector3f& target,
                          float fov_degrees,
                          const Bounds& bounds) {
    target_ = target;
    Eigen::Vector3f offset = position - target;
    if (offset.squaredNorm() < 1e-8f) {
        offset = Eigen::Vector3f(0.0f, 0.0f, 1.0f);
    }

    distance_ = std::clamp(offset.norm(), 0.05f, 100.0f);
    const Eigen::Vector3f direction = offset / std::max(distance_, 1e-6f);
    pitch_ = std::asin(std::clamp(direction.y(), -1.0f, 1.0f));
    yaw_ = std::atan2(direction.x(), direction.z());
    setFieldOfView(fov_degrees);

    const float radius = std::max(bounds.radius(), 0.6f);
    z_near_ = std::max(radius * 0.002f, 0.005f);
    z_far_ = std::max(radius * 12.0f, 50.0f);
}

void OrbitCamera::orbit(float delta_yaw, float delta_pitch) {
    yaw_ += delta_yaw;
    pitch_ = std::clamp(pitch_ + delta_pitch, -1.52f, 1.52f);
}

void OrbitCamera::pan(float delta_x, float delta_y) {
    const Eigen::Vector3f eye = position();
    Eigen::Vector3f forward = (target_ - eye).normalized();
    Eigen::Vector3f right = forward.cross(Eigen::Vector3f(0.0f, 1.0f, 0.0f));
    if (right.squaredNorm() < 1e-8f) {
        right = Eigen::Vector3f(1.0f, 0.0f, 0.0f);
    }
    else {
        right.normalize();
    }
    const Eigen::Vector3f up = right.cross(forward).normalized();
    const float scale = std::max(distance_, 0.5f) * 0.0025f;
    target_ += (-delta_x * right + delta_y * up) * scale;
}

void OrbitCamera::zoom(float wheel_steps) {
    const float factor = std::pow(0.88f, wheel_steps);
    distance_ = std::clamp(distance_ * factor, 0.3f, 100.0f);
}

void OrbitCamera::moveLocal(float right_delta, float up_delta, float forward_delta) {
    target_ += right() * right_delta + up() * up_delta + forward() * forward_delta;
}

Eigen::Vector3f OrbitCamera::position() const {
    const float cos_pitch = std::cos(pitch_);
    return target_ + Eigen::Vector3f(
        distance_ * cos_pitch * std::sin(yaw_),
        distance_ * std::sin(pitch_),
        distance_ * cos_pitch * std::cos(yaw_));
}

Eigen::Vector3f OrbitCamera::forward() const {
    Eigen::Vector3f value = target_ - position();
    if (value.squaredNorm() < 1e-8f) {
        return Eigen::Vector3f(0.0f, 0.0f, -1.0f);
    }
    return value.normalized();
}

Eigen::Vector3f OrbitCamera::right() const {
    Eigen::Vector3f value = forward().cross(Eigen::Vector3f(0.0f, 1.0f, 0.0f));
    if (value.squaredNorm() < 1e-8f) {
        return Eigen::Vector3f(1.0f, 0.0f, 0.0f);
    }
    return value.normalized();
}

Eigen::Vector3f OrbitCamera::up() const {
    return right().cross(forward()).normalized();
}

Eigen::Matrix4f OrbitCamera::viewMatrix() const {
    return math::lookAt(position(), target_, Eigen::Vector3f(0.0f, 1.0f, 0.0f));
}

Eigen::Matrix4f OrbitCamera::projectionMatrix() const {
    return math::perspective(fov_degrees_, aspect_ratio_, z_near_, z_far_);
}

} // namespace haorendergi
