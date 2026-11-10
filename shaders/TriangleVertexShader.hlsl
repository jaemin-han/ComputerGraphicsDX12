#include "Common.hlsli"

PixelShaderInput main(VertexShaderInput input)
{
    PixelShaderInput output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    return output;
}
