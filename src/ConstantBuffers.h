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

// 점광원이다. 광원 값(radiance)은 빛을 수직으로 받는 면에 들어오는
// irradiance이고, 셰이더가 거리에 따른 감쇠를 곱한다. 감쇠는 선형 창으로,
// fallOffStart까지 1이고 fallOffEnd에서 0이 된다.
struct PointLight {
    DirectX::XMFLOAT3 position;
    float fallOffStart;
    DirectX::XMFLOAT3 radiance;
    float fallOffEnd;
};

// b1(cbuffer SceneConstants)다. 한 프레임 동안 모든 드로우가 같은 값을 읽는
// 장면 전체의 데이터다.
struct SceneConstants {
    DirectX::XMFLOAT3 eyeWorld;
    // 0이면 최종 색, 1부터는 점광원 하나에 대한 BRDF 중간값 하나(F, D, G,
    // kDiffuse, 확산 BRDF, 정반사 BRDF, BRDF 합)를 보여 준다. 번호는
    // MeshPixelShader.hlsl의 DebugView 상수와 같다.
    int debugView;
    PointLight pointLight;
};

static_assert(sizeof(Material) == 2 * 16, "Must match HLSL Material.");
static_assert(sizeof(MeshConstants) == 18 * 16, "Must match HLSL b0.");
static_assert(sizeof(PointLight) == 2 * 16, "Must match HLSL PointLight.");
static_assert(sizeof(SceneConstants) == 3 * 16, "Must match HLSL b1.");
