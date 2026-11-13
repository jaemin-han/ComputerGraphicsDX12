#include "Common.hlsli"

// 레이 PS를 넣기 전까지 쓰는 확인용 셰이더다. 월드 노멀을 0~1 색으로 바꾼다.
float4 main(PixelShaderInput input) : SV_TARGET
{
    return float4(normalize(input.normalWorld) * 0.5f + 0.5f, 1.0f);
}
