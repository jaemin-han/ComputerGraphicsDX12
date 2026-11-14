#pragma once

#include <DirectXMath.h>

// 셰이더 cbuffer와 같은 배치의 C++ 구조체를 모은다. 행렬은 HLSL이 읽는 순서에
// 맞춰 전치해서 넣는다. HLSL은 float4(16바이트) 경계를 넘지 않게 값을
// 채우므로 C++에도 같은 자리에 padding을 둔다.

// b0(cbuffer MeshConstants)다. 드로우 하나에 속하는 값이다. 같은 메시를 여러
// 번 그리면 드로우마다 다른 값이 들어간다. VS가 행렬을, 레이 PS가
// objectIndex를 읽는다. 만드는 일은 Engine.cpp의 MakeMeshConstants()가 한다.
struct MeshConstants {
    DirectX::XMFLOAT4X4 model;
    DirectX::XMFLOAT4X4 invTranspose;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;
    // 이 드로우가 그리는 물체의 번호다. 0~5는 SceneConstants::spheres의
    // 번호이고, kSphereCount는 바닥이다.
    int objectIndex;
    float padding[3];
};

// 레이 씬의 구 개수다. HLSL의 SphereCount와 같아야 한다.
constexpr int kSphereCount = 6;

// 표면의 재질이다. indexOfRefraction이 0이면 불투명한 표면이다.
struct Surface {
    DirectX::XMFLOAT3 color;
    float reflectivity;
    float indexOfRefraction;
    float shininess;
    float padding[2];
};

struct Sphere {
    DirectX::XMFLOAT3 center;
    float radius;
    Surface surface;
};

// b1(cbuffer SceneConstants)다. 한 프레임 동안 모든 드로우가 같은 값을 읽는
// 장면 전체의 데이터다. 레이 PS가 objectIndex로 자기 물체를 고르고, 이후
// 단계에서 2차 광선이 다른 물체와 교차할 때도 이 배열을 읽는다.
struct SceneConstants {
    DirectX::XMFLOAT3 eyeWorld;
    float groundHalfExtent;
    DirectX::XMFLOAT3 groundCenter;
    float padding;
    Sphere spheres[kSphereCount];
};

static_assert(sizeof(MeshConstants) == 17 * 16, "Must match HLSL b0.");
static_assert(sizeof(Sphere) == 3 * 16, "Must match HLSL Sphere.");
static_assert(sizeof(SceneConstants) == 20 * 16, "Must match HLSL b1.");
