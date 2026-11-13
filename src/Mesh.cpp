#include "Mesh.h"

#include "Util.h"

Mesh Mesh::Create(ID3D12Device *device, const MeshData &data) {
    const UINT vertexBytes =
        static_cast<UINT>(sizeof(Vertex) * data.vertices.size());
    const UINT indexBytes =
        static_cast<UINT>(sizeof(uint32_t) * data.indices.size());

    // 정점 버퍼와 인덱스 버퍼는 같은 방식으로 업로드 힙에 올린다. 차이는 뷰가
    // 알려 주는 해석뿐이다.
    Mesh mesh;
    mesh.m_vertexBuffer = CreateUploadBuffer(device, vertexBytes);
    mesh.m_indexBuffer = CreateUploadBuffer(device, indexBytes);
    CopyToBuffer(mesh.m_vertexBuffer.Get(), data.vertices.data(), vertexBytes);
    CopyToBuffer(mesh.m_indexBuffer.Get(), data.indices.data(), indexBytes);

    // 정점 버퍼는 정점 하나의 크기(Stride)로 잘라 읽는다.
    mesh.m_vertexBufferView.BufferLocation =
        mesh.m_vertexBuffer->GetGPUVirtualAddress();
    mesh.m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    mesh.m_vertexBufferView.SizeInBytes = vertexBytes;

    // 인덱스 버퍼는 형식(32비트 정수)으로 읽는다.
    mesh.m_indexBufferView.BufferLocation =
        mesh.m_indexBuffer->GetGPUVirtualAddress();
    mesh.m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    mesh.m_indexBufferView.SizeInBytes = indexBytes;

    mesh.m_indexCount = static_cast<UINT>(data.indices.size());
    return mesh;
}

void Mesh::Draw(ID3D12GraphicsCommandList *commandList) const {
    commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList->IASetIndexBuffer(&m_indexBufferView);
    commandList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
}
