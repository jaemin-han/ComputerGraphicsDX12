#pragma once

#include <DirectXMath.h>

// 셰이더 cbuffer와 같은 배치의 C++ 구조체를 모은다. 행렬은 HLSL이 읽는 순서에
// 맞춰 전치해서 넣는다. HLSL은 float4(16바이트) 경계를 넘지 않게 값을
// 채우므로 C++에도 같은 자리에 padding을 둔다.

// 표면의 재질이다. 픽셀은 자기 표면의 재질만 읽으므로 드로우별 b0에 둔다.
// roughness는 슬라이더 값(0~1) 그대로이고, 식에 넣을 α로 바꾸는 일은 셰이더가
// 한다.
struct Material {
    DirectX::XMFLOAT3 albedo;
    float metallic;
    float roughness;
    // 0이 아니면 albedo 대신 t3(바닥 텍스처)를 월드 xz로 읽는다.
    int useAlbedoTexture;
    float padding[2];
};

// b0(cbuffer MeshConstants)다. 드로우 하나에 속하는 값이다. 같은 메시를 여러
// 번 그리면 드로우마다 다른 값이 들어간다. VS가 행렬을, PS가 재질을 읽는다.
// 만드는 일은 Engine.cpp의 MakeMeshConstants()가 한다.
struct MeshConstants {
    DirectX::XMFLOAT4X4 model;
    DirectX::XMFLOAT4X4 invTranspose;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;
    Material material;
};

// 스포트라이트다. 점광원에 "비추는 방향"이 더해져, 방향 축에서 벗어날수록
// 빛이 약해진다. 광원 값(radiance)은 빛을 수직으로 받는 면에 들어오는
// irradiance이고, 셰이더가 거리 감쇠와 원뿔 감쇠를 곱한다. 거리 감쇠는 선형
// 창으로, fallOffStart까지 1이고 fallOffEnd에서 0이 된다.
//
// 그림자 맵 한 장은 투영 하나로 장면을 담는다. 사방으로 빛을 내는 점광원은
// 투영 하나로 담을 수 없어서(큐브 6면이 필요하다) 13.4처럼 스포트라이트로
// 바꿨다.
struct SpotLight {
    DirectX::XMFLOAT3 position;
    float fallOffStart;
    DirectX::XMFLOAT3 radiance;
    float fallOffEnd;
    // 광원에서 비추는 쪽으로 가는 단위 벡터다.
    DirectX::XMFLOAT3 direction;
    // 클수록 원뿔이 좁아진다.
    float spotPower;
    // 그림자 맵을 그릴 때 쓴 광원의 view * projection이다(전치해서 넣는다).
    // 메시 PS가 픽셀의 월드 위치를 그림자 맵과 같은 기준으로 다시 투영할 때
    // 쓴다. 그림자 패스는 같은 두 행렬을 드로우별 b0의 view, projection으로
    // 받는다(13.4의 [읽기 1]처럼 한 행렬이 두 곳에 들어간다).
    DirectX::XMFLOAT4X4 viewProj;
};

// b1(cbuffer SceneConstants)다. 한 프레임 동안 모든 드로우가 같은 값을 읽는
// 장면 전체의 데이터다. 광원이 움직이면 바뀌는 값(광원 행렬)도 여기에 둔다.
// 그림자 맵 SRV는 "어느 자원을 어떤 형식으로 읽나"만 적은 칸이라, 그림자 맵의
// 내용이 프레임마다 바뀌어도 다시 쓰지 않는다.
struct SceneConstants {
    DirectX::XMFLOAT3 eyeWorld;
    // 0이면 최종 색, 1부터는 중간값 하나(F, D, G, kDiffuse, 확산 BRDF, 정반사
    // BRDF, BRDF 합, 원뿔 감쇠, 그림자 계수)를 보여 준다. 번호는
    // MeshPixelShader.hlsl의 DebugView 상수와 같다.
    int debugView;
    SpotLight spotLight;
};

static_assert(sizeof(Material) == 2 * 16, "Must match HLSL Material.");
static_assert(sizeof(MeshConstants) == 18 * 16, "Must match HLSL b0.");
static_assert(sizeof(SpotLight) == 7 * 16, "Must match HLSL SpotLight.");
static_assert(sizeof(SceneConstants) == 8 * 16, "Must match HLSL b1.");
