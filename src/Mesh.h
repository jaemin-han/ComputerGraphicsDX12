#pragma once

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

// CPU에서 준비한 메시 데이터다. Mesh::Create()가 GPU 버퍼로 올린다.
// 인덱스는 정점 배열의 번호이고, 세 개씩 삼각형 하나가 된다.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// GPU에 올린 정점과 인덱스 버퍼다. 앱이 끝날 때까지 바뀌지 않는다. 디바이스와
// 커맨드 리스트는 갖지 않고 필요할 때 인자로 받는다. 드로우마다 바뀌는 상수는
// 메시가 아니라 FrameResource에 쓴다.
class Mesh {
  public:
    static Mesh Create(ID3D12Device *device, const MeshData &data);

    // 정점, 인덱스 버퍼를 묶고 드로우를 기록한다.
    void Draw(ID3D12GraphicsCommandList *commandList) const;

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};
    UINT m_indexCount = 0;
};
