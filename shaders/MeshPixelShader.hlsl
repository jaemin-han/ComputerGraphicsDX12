#include "Common.hlsli"

// 구 격자 장면의 임시 셰이딩이다. 직접광 PBR이 들어오기 전까지 재질 색이
// 드로우마다 제대로 들어오는지 보는 용도로, 레이 씬 1단계의 로컬 셰이딩에서
// specular를 뺀 모양이다. metallic과 roughness는 아직 읽지 않는다.
TextureCube irradianceMap : register(t1);
Texture2D groundTexture : register(t3);
SamplerState linearWrapSampler : register(s0);

float4 main(PixelShaderInput input) : SV_TARGET
{
    float3 normal = normalize(input.normalWorld);

    // 바닥은 레이 씬과 같게 월드 xz로 텍스처를 읽어, 바닥 크기와 상관없이
    // 2단위마다 한 장씩 반복한다.
    float3 albedo = material.albedo;
    if (material.useAlbedoTexture)
        albedo = groundTexture
                     .SampleLevel(linearWrapSampler, input.posWorld.xz * 0.5f, 0)
                     .rgb;

    // 하늘빛은 노멀 방향의 irradiance로, 해는 Lambert로 더한다. 비율은 레이
    // 씬 1단계와 같은 Part 2 튜닝 값이다.
    float3 irradiance =
        irradianceMap.SampleLevel(linearWrapSampler, normal, 0).rgb;
    float diffuse = max(dot(normal, -lightDirection), 0.0f);
    float3 color = (0.35f * irradiance + 0.65f * diffuse) * albedo;
    return float4(color, 1.0f);
}
