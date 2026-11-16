#pragma once

#include <DirectXMath.h>

// 1인칭 카메라다. 언리얼 에디터 뷰포트처럼 고개를 좌우(yaw), 상하(pitch)로만
// 돌리고 시선 방향으로 날아다닌다. FPS 카메라는 roll이 없으므로 방향은 두
// 각도로 저장하고, 회전은 그 각도로 매번 쿼터니언을 만들어 쓴다.
class Camera {
  public:
    Camera(DirectX::XMFLOAT3 position, float yaw, float pitch);

    // 카메라 로컬 축을 월드로 돌리는 회전이다. x축으로 pitch만큼 숙인 뒤
    // 월드 y축으로 yaw만큼 돈다.
    DirectX::XMVECTOR Orientation() const;
    // 카메라 모델 행렬 R(q) * T(p)의 역행렬, T(-p) * R(q의 켤레)다.
    DirectX::XMMATRIX View() const;
    DirectX::XMVECTOR Position() const;
    // 시선 방향(로컬 +z)과 오른쪽(로컬 +x)을 월드로 돌린 단위 벡터다.
    DirectX::XMVECTOR Forward() const;
    DirectX::XMVECTOR Right() const;

    float Yaw() const { return m_yaw; }
    float Pitch() const { return m_pitch; }

    // 각도(라디안)를 더한다. yaw > 0은 오른쪽, pitch > 0은 아래쪽이다.
    void Rotate(float yawDelta, float pitchDelta);
    // 시선, 오른쪽, 월드 위 방향으로 각각 이만큼 옮긴다.
    void Move(float forward, float right, float up);

  private:
    DirectX::XMFLOAT3 m_position;
    float m_yaw;
    float m_pitch;
};
