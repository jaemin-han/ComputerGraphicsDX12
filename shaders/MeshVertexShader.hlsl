#include "Common.hlsli"

PixelShaderInput main(VertexShaderInput input)
{
    float4 position = mul(float4(input.position, 1.0f), model);

    PixelShaderInput output;
    output.posWorld = position.xyz;
    output.position = mul(mul(position, view), projection);
    // 노멀은 방향이라 w = 0으로 변환한다. 크기가 바뀐 모델에서도 표면에
    // 수직이도록 역전치 행렬을 쓴다.
    output.normalWorld =
        normalize(mul(float4(input.normal, 0.0f), invTranspose).xyz);
    output.texcoord = input.texcoord;
    return output;
}
