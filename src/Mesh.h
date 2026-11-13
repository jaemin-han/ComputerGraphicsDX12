#pragma once

#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

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

// CPU에서 준비한 메시 데이터다. Engine::CreateMesh()가 GPU 버퍼로 올린다.
// 인덱스는 정점 배열의 번호이고, 세 개씩 삼각형 하나가 된다.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// GPU에 올린 정점과 인덱스 데이터만 담는다. 만들고 그리는 일은 Engine이
// 맡는다.
struct Mesh {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
    UINT indexCount = 0;
};
