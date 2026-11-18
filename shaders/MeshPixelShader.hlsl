#include "Common.hlsli"

// 구 격자 장면의 직접광 PBR이다. 점광원에서 오는 빛을 Lambert diffuse와
// Cook-Torrance specular로 셰이딩한다. BRDF는 노트(PBR 4절) 그대로 diffuse에
// 1/π를 남기고 specular에는 π를 곱하지 않는다. 그래서 광원 값은 빛을 수직으로
// 받는 면에 들어오는 irradiance다. 하늘빛(IBL)은 마감 뒤에 다루므로, 빛이 닿지
// 않는 곳은 검정이다.
Texture2D groundTexture : register(t3);
SamplerState linearWrapSampler : register(s0);

static const float PI = 3.14159265f;

// b1의 debugView 값이다. Final 말고는 점광원 하나에 대한 BRDF 중간값 하나를
// 광원 값과 n·l을 곱하기 전 그대로 색으로 보여 준다. 1을 넘는 값은 백버퍼에
// 쓸 때 잘려 흰색이 된다.
static const int DebugViewFinal = 0;
static const int DebugViewFresnel = 1;
static const int DebugViewDistribution = 2;
static const int DebugViewGeometry = 3;
static const int DebugViewDiffuseWeight = 4;  // kDiffuse
static const int DebugViewDiffuseBrdf = 5;    // kDiffuse * fDiff
static const int DebugViewSpecularBrdf = 6;   // fSpec
static const int DebugViewBrdf = 7;           // kDiffuse * fDiff + fSpec

// 미세면 하나가 빛을 반사하는 비율 F다(Schlick 근사, 채널별 0~1). 정면 반사율
// F0은 metallic 워크플로로 정한다. 비금속은 0.04, 금속은 albedo다. vDotH가 1이면
// F0, 0이면 1(흰색)이다. 점광원에서 v·h는 n과 상관없어 구 전체에서 거의 같다.
float3 Fresnel(float3 albedo, float metallic, float vDotH)
{
    float3 F0 = lerp(0.04, albedo, metallic);
    float3 F = F0 + (1 - F0) * pow((1 - vDotH), 5);
    return F;
}

// 법선이 h인 미세면의 밀도 D다(GGX). 식의 α는 roughness²이고, 1을 넘을 수 있다.
// roughness 0이면 n·h = 1에서 0/0이 되므로 분모에 작은 값을 더해 NaN을 막는다.
// roughness 1이면 어디서나 1/π다.
float DistributionGGX(float nDotH, float roughness)
{
    float alphaSq = pow(roughness, 4);
    float D = alphaSq / (PI * pow(nDotH * nDotH * (alphaSq - 1) + 1, 2) + 1e-7);
    return D;
}

// 한 방향(l이나 v)에서 미세면이 가려지지 않은 비율이다(Schlick-GGX). k ≥ 0.125라
// 분모가 0이 되지 않는다.
float SchlickG1(float nDotX, float k)
{
    return nDotX / (nDotX * (1 - k) + k);
}

// 미세면이 빛 쪽과 눈 쪽 모두에서 가려지지 않은 비율 G다(Smith 분리형, 0~1). k는
// 점광원용 (roughness + 1)² / 8이다. IBL은 다른 k(α / 2)를 쓴다.
float GeometrySmith(float nDotL, float nDotV, float roughness)
{
    float k = pow(roughness + 1, 2) / 8;
    return SchlickG1(nDotL, k) * SchlickG1(nDotV, k);
}

// 광원 하나에서 와서 눈으로 나가는 radiance다. l은 표면에서 광원으로 가는 단위
// 벡터, irradiance는 감쇠까지 곱한 광원 값이다. 광원이 여럿이면 main이 더한다.
// 반사율 방정식에서 점광원 하나만 남기면 BRDF × irradiance × n·l이다. BRDF는
// 확산(kDiffuse · albedo / π)과 정반사(DFG / (4 n·l n·v))의 합이고, kDiffuse는
// 정반사하고 남은 빛 중 금속이 흡수하지 않은 몫이다. n·l을 saturate하므로 표면
// 뒤에서 오는 빛은 0이다.
float3 DirectLighting(float3 albedo, float metallic, float roughness,
                      float3 n, float3 v, float3 l, float3 irradiance)
{
    float3 halfway = normalize(l + v);
    float nDotL = saturate(dot(n, l));
    float nDotV = saturate(dot(n, v));
    float nDotH = saturate(dot(n, halfway));
    float vDotH = saturate(dot(v, halfway));

    float3 F = Fresnel(albedo, metallic, vDotH);
    float3 kDiffuse = (1 - F) * (1 - metallic);
    float3 fDiff = albedo / PI;

    float D = DistributionGGX(nDotH, roughness);
    float G = GeometrySmith(nDotL, nDotV, roughness);
    float3 fSpec = D * F * G / max(4 * nDotL * nDotV, 1e-7);

    float3 BRDF = kDiffuse * fDiff + fSpec;

    float3 radiance = irradiance * BRDF * nDotL;

    // 디버그 뷰는 위에서 구한 중간값 하나로 바꿔 돌려준다. 같은 값을 따로
    // 다시 계산하지 않으므로 화면이 곧 이 함수의 중간 결과다. 중간에 return을
    // 여러 번 두면 FXC가 초기화되지 않은 변수 경고(X4000)를 내서 값만 바꾼다.
    if (debugView == DebugViewFresnel)
        radiance = F;
    else if (debugView == DebugViewDistribution)
        radiance = D;
    else if (debugView == DebugViewGeometry)
        radiance = G;
    else if (debugView == DebugViewDiffuseWeight)
        radiance = kDiffuse;
    else if (debugView == DebugViewDiffuseBrdf)
        radiance = kDiffuse * fDiff;
    else if (debugView == DebugViewSpecularBrdf)
        radiance = fSpec;
    else if (debugView == DebugViewBrdf)
        radiance = BRDF;
    return radiance;
}

float4 main(PixelShaderInput input) : SV_TARGET
{
    float3 n = normalize(input.normalWorld);
    float3 v = normalize(eyeWorld - input.posWorld);

    // 바닥은 레이 씬과 같게 월드 xz로 텍스처를 읽어, 바닥 크기와 상관없이
    // 2단위마다 한 장씩 반복한다.
    float3 albedo = material.albedo;
    if (material.useAlbedoTexture)
        albedo = groundTexture
                     .SampleLevel(linearWrapSampler, input.posWorld.xz * 0.5f, 0)
                     .rgb;

    // 점광원까지의 거리는 감쇠에 쓰고, 방향은 정규화해서 BRDF에 쓴다.
    float3 toLight = pointLight.position - input.posWorld;
    float lightDistance = length(toLight);
    float3 l = toLight / lightDistance;

    // 감쇠는 선형 창이다(노트 3절). 물리적인 점광원은 1/d²로 줄어 0이 되지
    // 않지만, 창을 씌우면 fallOffEnd 밖에서 기여가 정확히 0이라 광원의 영향
    // 범위가 분명해진다.
    float attenuation =
        saturate((pointLight.fallOffEnd - lightDistance) /
                 (pointLight.fallOffEnd - pointLight.fallOffStart));
    float3 color = DirectLighting(albedo, material.metallic, material.roughness,
                                  n, v, l, pointLight.radiance * attenuation);
    return float4(color, 1.0f);
}
