#include "Common.hlsli"

// 레이 씬과 같은 레지스터다. 루트 파라미터 1의 디스크립터 테이블이 t1~t3을,
// 루트 시그니처의 정적 샘플러가 s0을 채운다.
TextureCube irradianceMap : register(t1);
TextureCube environmentMap : register(t2);
Texture2D groundTexture : register(t3);
SamplerState linearWrapSampler : register(s0);

// 텍스처 연결을 확인하는 임시 셰이더다. 모든 메시에 바닥 텍스처를 UV로
// 입힌다. 레이 PS가 들어오면 교체한다.
float4 main(PixelShaderInput input) : SV_TARGET
{
    return float4(groundTexture.Sample(linearWrapSampler, input.texcoord).rgb,
                  1.0f);
}
