#pragma once

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>

namespace haorendergi::math {

inline Eigen::Matrix4f perspective(float fov_degrees, float aspect_ratio, float z_near, float z_far) {
    const float clamped_aspect = std::max(aspect_ratio, 0.001f);
    const float radians = fov_degrees * 3.14159265358979323846f / 180.0f;
    const float tan_half = std::tan(radians * 0.5f);

    Eigen::Matrix4f matrix = Eigen::Matrix4f::Zero();
    matrix(0, 0) = 1.0f / (clamped_aspect * tan_half);
    matrix(1, 1) = 1.0f / tan_half;
    matrix(2, 2) = -(z_far + z_near) / (z_far - z_near);
    matrix(2, 3) = -(2.0f * z_far * z_near) / (z_far - z_near);
    matrix(3, 2) = -1.0f;
    return matrix;
}

inline Eigen::Matrix4f lookAt(const Eigen::Vector3f& eye, const Eigen::Vector3f& center, const Eigen::Vector3f& up_hint) {
    Eigen::Vector3f forward = (center - eye).normalized();
    Eigen::Vector3f right = forward.cross(up_hint);
    if (right.squaredNorm() < 1e-8f) {
        right = Eigen::Vector3f(1.0f, 0.0f, 0.0f);
    }
    else {
        right.normalize();
    }
    Eigen::Vector3f up = right.cross(forward).normalized();

    Eigen::Matrix4f matrix = Eigen::Matrix4f::Identity();
    matrix.block<1, 3>(0, 0) = right.transpose();
    matrix.block<1, 3>(1, 0) = up.transpose();
    matrix.block<1, 3>(2, 0) = (-forward).transpose();
    matrix(0, 3) = -right.dot(eye);
    matrix(1, 3) = -up.dot(eye);
    matrix(2, 3) = forward.dot(eye);
    return matrix;
}

inline Eigen::Matrix4f orthographic(float left, float right, float bottom, float top, float z_near, float z_far) {
    Eigen::Matrix4f matrix = Eigen::Matrix4f::Identity();
    matrix(0, 0) = 2.0f / (right - left);
    matrix(1, 1) = 2.0f / (top - bottom);
    matrix(2, 2) = -2.0f / (z_far - z_near);
    matrix(0, 3) = -(right + left) / (right - left);
    matrix(1, 3) = -(top + bottom) / (top - bottom);
    matrix(2, 3) = -(z_far + z_near) / (z_far - z_near);
    return matrix;
}

inline Eigen::Matrix4f identity4() {
    return Eigen::Matrix4f::Identity();
}

} // namespace haorendergi::math
