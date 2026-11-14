#include "Common.hlsli"

// 루트 파라미터 1의 테이블(t1~t3) 중 t2만 읽는다.
TextureCube environmentMap : register(t2);
SamplerState linearWrapSampler : register(s0);

// 큐브맵은 UV가 아니라 방향 벡터로 읽는다.
float4 main(SkyPixelShaderInput input) : SV_TARGET
{
    float3 color =
        environmentMap.SampleLevel(linearWrapSampler, input.direction, 0).rgb;
    return float4(color, 1.0f);
}
