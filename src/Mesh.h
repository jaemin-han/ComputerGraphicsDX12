#pragma once

#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl/client.h>

// 정점 셰이더 입력과 같은 배치다. 입력 레이아웃은 Engine::CreatePipeline()에
// 있다.
struct Vertex {
    float position[3];
    float color[3];
};

// VS의 b0(cbuffer MeshConstants)와 같은 배치다. 행렬은 HLSL이 읽는 순서에
// 맞춰 전치해서 넣는다. 만드는 일은 Engine.cpp의 MakeMeshConstants()가 한다.
struct MeshConstants {
    DirectX::XMFLOAT4X4 model;
    DirectX::XMFLOAT4X4 invTranspose;
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 projection;
};

// GPU에 올린 정점 데이터만 담는다. 만들고 그리는 일은 Engine이 맡는다.
struct Mesh {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    UINT vertexCount = 0;
};
