#include "Common.hlsli"

// C++의 MeshConstants와 같은 배치다. 드로우마다 루트 파라미터 0이 이 버퍼의
// 주소를 바꾼다.
cbuffer MeshConstants : register(b0)
{
    matrix model;
    matrix invTranspose;
    matrix view;
    matrix projection;
};

PixelShaderInput main(VertexShaderInput input)
{
    float4 position = mul(float4(input.position, 1.0f), model);
    position = mul(position, view);

    PixelShaderInput output;
    output.position = mul(position, projection);
    output.color = input.color;
    return output;
}
