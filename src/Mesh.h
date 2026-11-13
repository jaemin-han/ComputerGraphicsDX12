#pragma once

#include <d3d12.h>
#include <wrl/client.h>

// 정점 셰이더 입력과 같은 배치다. 입력 레이아웃은 Engine::CreatePipeline()에
// 있다.
struct Vertex {
    float position[3];
    float color[3];
};

// GPU에 올린 정점 데이터만 담는다. 만들고 그리는 일은 Engine이 맡는다.
struct Mesh {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    UINT vertexCount = 0;
};
