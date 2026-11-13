#pragma once

#include <DirectXMath.h>

// 셰이더 cbuffer와 같은 배치의 C++ 구조체를 모은다. 행렬은 HLSL이 읽는 순서에
// 맞춰 전치해서 넣는다.

// VS의 b0(cbuffer MeshConstants)다. 이름과 달리 메시가 아니라 드로우 하나에
// 속한다. 같은 메시를 여러 번 그리면 드로우마다 다른 값이 들어간다. 만드는
// 일은 Engine.cpp의 MakeMeshConstants()가 한다.
struct MeshConstants {
    DirectX::XMFLOAT4X4 model;
    DirectX::XMFLOAT4X4 invTranspose;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;
};
