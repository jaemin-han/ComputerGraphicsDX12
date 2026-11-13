// 삼각형은 바깥에서 볼 때 시계 방향으로 감는다. Direct3D 기본 설정에서 그쪽이
// 앞면이다.

#include "Geometry.h"

#include <cmath>

using namespace DirectX;

MeshData MakeSphere(float radius, uint32_t slices, uint32_t stacks) {
    MeshData mesh;

    // 위(phi = 0)에서 아래(phi = pi)로 내려가며 경도 theta를 0에서 2pi까지
    // 돈다. 이음매 열은 UV가 달라서 정점을 한 번 더 둔다.
    for (uint32_t stack = 0; stack <= stacks; ++stack) {
        const float phi = XM_PI * stack / stacks;
        for (uint32_t slice = 0; slice <= slices; ++slice) {
            const float theta = XM_2PI * slice / slices;
            const XMFLOAT3 normal(std::sin(phi) * std::cos(theta),
                                  std::cos(phi),
                                  std::sin(phi) * std::sin(theta));

            Vertex vertex;
            vertex.position = XMFLOAT3(normal.x * radius, normal.y * radius,
                                       normal.z * radius);
            vertex.normal = normal;
            vertex.texcoord =
                XMFLOAT2(float(slice) / slices, float(stack) / stacks);
            mesh.vertices.push_back(vertex);
        }
    }

    // theta가 커지는 쪽이 바깥에서 볼 때 오른쪽, phi가 커지는 쪽이 아래다.
    // 양 극의 첫 줄과 마지막 줄에는 넓이가 0인 삼각형이 하나씩 생기지만
    // 그려지지 않을 뿐 문제는 없다.
    const uint32_t rowLength = slices + 1;
    for (uint32_t stack = 0; stack < stacks; ++stack) {
        for (uint32_t slice = 0; slice < slices; ++slice) {
            const uint32_t topLeft = stack * rowLength + slice;
            const uint32_t topRight = topLeft + 1;
            const uint32_t bottomLeft = topLeft + rowLength;
            const uint32_t bottomRight = bottomLeft + 1;
            mesh.indices.insert(mesh.indices.end(),
                                {topLeft, topRight, bottomLeft, topRight,
                                 bottomRight, bottomLeft});
        }
    }
    return mesh;
}

MeshData MakeSquare() {
    const XMFLOAT3 normal(0.0f, 0.0f, -1.0f);

    MeshData mesh;
    mesh.vertices = {
        {XMFLOAT3(-1.0f, 1.0f, 0.0f), normal, XMFLOAT2(0.0f, 0.0f)},
        {XMFLOAT3(1.0f, 1.0f, 0.0f), normal, XMFLOAT2(1.0f, 0.0f)},
        {XMFLOAT3(1.0f, -1.0f, 0.0f), normal, XMFLOAT2(1.0f, 1.0f)},
        {XMFLOAT3(-1.0f, -1.0f, 0.0f), normal, XMFLOAT2(0.0f, 1.0f)},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}
