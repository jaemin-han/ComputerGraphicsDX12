#include "Camera.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {

// 수직을 넘어 뒤집히지 않도록 고개를 ±89도에서 멈춘다.
constexpr float kMaxPitch = XMConvertToRadians(89.0f);

} // namespace

Camera::Camera(XMFLOAT3 position, float yaw, float pitch)
    : m_position(position), m_yaw(yaw),
      m_pitch(std::clamp(pitch, -kMaxPitch, kMaxPitch)) {}

XMVECTOR Camera::Orientation() const {
    const XMVECTOR pitch =
        XMQuaternionRotationAxis(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), m_pitch);
    const XMVECTOR yaw =
        XMQuaternionRotationAxis(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), m_yaw);
    // XMQuaternionMultiply(Q1, Q2)는 Q1 다음 Q2를 적용하는 회전이다.
    return XMQuaternionMultiply(pitch, yaw);
}

XMMATRIX Camera::View() const {
    // 단위 쿼터니언의 켤레(x, y, z 부호만 뒤집은 것)가 반대 회전이다.
    const XMVECTOR inverse = XMQuaternionConjugate(Orientation());
    return XMMatrixTranslation(-m_position.x, -m_position.y, -m_position.z) *
           XMMatrixRotationQuaternion(inverse);
}

XMVECTOR Camera::Position() const { return XMLoadFloat3(&m_position); }

XMVECTOR Camera::Forward() const {
    return XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), Orientation());
}

XMVECTOR Camera::Right() const {
    return XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), Orientation());
}

void Camera::Rotate(float yawDelta, float pitchDelta) {
    m_yaw = std::remainder(m_yaw + yawDelta, XM_2PI);
    m_pitch = std::clamp(m_pitch + pitchDelta, -kMaxPitch, kMaxPitch);
}

void Camera::Move(float forward, float right, float up) {
    XMVECTOR position = Position();
    position += forward * Forward();
    position += right * Right();
    position += XMVectorSet(0.0f, up, 0.0f, 0.0f);
    XMStoreFloat3(&m_position, position);
}
