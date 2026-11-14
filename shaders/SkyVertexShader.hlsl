#include "Common.hlsli"

// 메시와 같은 b0(Common.hlsli)을 쓴다. 하늘에서는 view에 이동이 빠져 있어
// 구가 늘 카메라 중심에 있고, invTranspose와 objectIndex는 쓰지 않는다.

SkyPixelShaderInput main(VertexShaderInput input)
{
    SkyPixelShaderInput output;
    float4 position = mul(float4(input.position, 1.0f), model);
    output.position = mul(mul(position, view), projection);
    // 구가 카메라 중심에 있으므로 모델 좌표 위치가 곧 카메라에서 그 하늘
    // 점을 보는 방향이다.
    output.direction = input.position;
    return output;
}
