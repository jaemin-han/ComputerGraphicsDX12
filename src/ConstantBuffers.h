#pragma once

#include <DirectXMath.h>

// 셰이더 cbuffer와 같은 배치의 C++ 구조체를 모은다. 행렬은 HLSL이 읽는 순서에
// 맞춰 전치해서 넣는다. HLSL은 float4(16바이트) 경계를 넘지 않게 값을
// 채우므로 C++에도 같은 자리에 padding을 둔다.

// 표면의 재질이다. 픽셀은 자기 표면의 재질만 읽으므로 드로우별 b0에 둔다.
// metallic과 roughness는 직접광 PBR 단계에서 쓴다.
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

// b1(cbuffer SceneConstants)다. 한 프레임 동안 모든 드로우가 같은 값을 읽는
// 장면 전체의 데이터다.
struct SceneConstants {
    DirectX::XMFLOAT3 eyeWorld;
    float padding0;
    // 해(방향광)의 빛이 나아가는 방향이다. 정규화해서 넣는다.
    DirectX::XMFLOAT3 lightDirection;
    float padding1;
};

static_assert(sizeof(Material) == 2 * 16, "Must match HLSL Material.");
static_assert(sizeof(MeshConstants) == 18 * 16, "Must match HLSL b0.");
static_assert(sizeof(SceneConstants) == 2 * 16, "Must match HLSL b1.");
