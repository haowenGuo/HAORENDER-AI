#pragma once

#include "scene/scene_types.h"

#include <Eigen/Dense>

namespace haorendergi {

class OrbitCamera {
public:
    void setViewport(int width, int height);
    void setFieldOfView(float degrees);
    void reset(const Bounds& bounds);
    void setView(const Eigen::Vector3f& position, const Eigen::Vector3f& target, float fov_degrees, const Bounds& bounds);

    void orbit(float delta_yaw, float delta_pitch);
    void pan(float delta_x, float delta_y);
    void zoom(float wheel_steps);
    void moveLocal(float right_delta, float up_delta, float forward_delta);

    Eigen::Vector3f position() const;
    Eigen::Vector3f forward() const;
    Eigen::Vector3f right() const;
    Eigen::Vector3f up() const;
    Eigen::Matrix4f viewMatrix() const;
    Eigen::Matrix4f projectionMatrix() const;

    const Eigen::Vector3f& target() const { return target_; }
    float distance() const { return distance_; }
    float fieldOfView() const { return fov_degrees_; }

private:
    Eigen::Vector3f target_ = Eigen::Vector3f::Zero();
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float distance_ = 3.0f;
    float fov_degrees_ = 55.0f;
    float aspect_ratio_ = 16.0f / 9.0f;
    float z_near_ = 0.05f;
    float z_far_ = 100.0f;
};

} // namespace haorendergi
