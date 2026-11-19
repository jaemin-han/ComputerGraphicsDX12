#include "Common.hlsli"

// 구 격자 장면의 직접광 PBR이다. 스포트라이트에서 오는 빛을 Lambert diffuse와
// Cook-Torrance specular로 셰이딩한다. BRDF는 노트(PBR 4절) 그대로 diffuse에
// 1/π를 남기고 specular에는 π를 곱하지 않는다. 그래서 광원 값은 빛을 수직으로
// 받는 면에 들어오는 irradiance다. 하늘빛(IBL)은 마감 뒤에 다루므로, 빛이 닿지
// 않는 곳(그림자 포함)은 검정이다.

// 루트 파라미터 1의 테이블(t0~t3) 중 t2, t3을 읽는다. t3은 그림자 패스가 이번
// 프레임에 깊이를 쓴 그림자 맵이다. 메시 패스 전에 배리어로 PS가 읽는 상태가
// 돼 있어야 한다.
Texture2D groundTexture : register(t2);
Texture2D shadowMap : register(t3);
SamplerState linearWrapSampler : register(s0);
// 루트 시그니처의 그림자 맵용 정적 비교 샘플러다.
SamplerComparisonState shadowCompareSampler : register(s1);

static const float PI = 3.14159265f;

// b1의 debugView 값이다. Final 말고는 중간값 하나를 그대로 색으로 보여 준다.
// 1~7은 광원 하나에 대한 BRDF 중간값으로, 광원 값과 n·l을 곱하기 전이다. 1을
// 넘는 값은 백버퍼에 쓸 때 잘려 흰색이 된다.
static const int DebugViewFinal = 0;
static const int DebugViewFresnel = 1;
static const int DebugViewDistribution = 2;
static const int DebugViewGeometry = 3;
static const int DebugViewDiffuseWeight = 4;  // kDiffuse
static const int DebugViewDiffuseBrdf = 5;    // kDiffuse * fDiff
static const int DebugViewSpecularBrdf = 6;   // fSpec
static const int DebugViewBrdf = 7;           // kDiffuse * fDiff + fSpec
static const int DebugViewSpotFactor = 8;     // SpotFactor() 결과
static const int DebugViewShadowFactor = 9;   // ShadowFactor() 결과

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

// 스포트라이트의 원뿔 감쇠다(0~1). 점광원과 스포트라이트의 차이는 이 계수
// 하나다. l은 표면에서 광원으로 가는 단위 벡터이고 direction은 광원에서 비추는
// 쪽으로 가는 벡터라, l을 뒤집어 방향을 맞춘 뒤 비교한다. 축 위에서 1이고
// 축에서 벗어날수록 줄며, 90° 이상(광원 뒤쪽)은 0이다. spotPower가 클수록 빨리
// 줄어 밝은 원이 좁아진다.
float SpotFactor(float3 l)
{
    float3 lightToPos = -l;
    float lDotD = saturate(dot(lightToPos, spotLight.direction));
    return pow(lDotD, spotLight.spotPower);
}

// 그림자 맵의 3×3 이웃에서 깊이를 비교해 스포트라이트의 가시성을 구한다.
// 1은 완전히 밝은 영역, 0은 완전히 가려진 영역이다.
//
// - 비교하는 깊이는 그림자 패스가 기록한 값과 같은 기준인 NDC z다. 클립 z는
//   w로 나누기 전이라 기준이 다르다.
// - 맵 바깥의 텍셀과 비교할 때는 s1의 테두리 깊이 1을 사용한다.
float ShadowFactor(float3 posWorld)
{
    // 월드 → 광원 클립 공간 → NDC다.
    float4 posClip = mul(float4(posWorld, 1.0f), spotLight.viewProj);
    float3 posNDC = posClip.xyz / posClip.w;
    float posDepth = posNDC.z;

    // NDC xy [-1, 1]을 텍스처 좌표 [0, 1]로 옮긴다. 텍스처는 왼쪽 위가
    // 원점이고 아래로 갈수록 v가 커져서 y를 뒤집는다.
    float2 uv = posNDC.xy * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;

    // 같은 표면이 정밀도 오차로 자기를 가리지 않도록 기준 깊이를 당긴다.
    float compareDepth = posDepth - 1e-4f;

    uint width, height;
    shadowMap.GetDimensions(width, height);
    float2 texelSize = 1.0f / float2(width, height);

    // 중심 UV를 기준으로 한 텍셀 단위의 오프셋이다.
    const int2 offsets[9] =
    {
        int2(-1, -1), int2(0, -1), int2(1, -1),
        int2(-1,  0), int2(0,  0), int2(1,  0),
        int2(-1,  1), int2(0,  1), int2(1,  1),
    };

    // 각 탭에서 선형 비교 샘플을 읽고, 9개 결과에 같은 가중치를 준다.
    float shadowFactorSum = 0.0f;
    for (int i = 0; i < 9; i++)
    {
        float2 sampleUV = uv + texelSize * offsets[i];
        shadowFactorSum +=
            shadowMap.SampleCmpLevelZero(shadowCompareSampler, sampleUV, compareDepth);
    }
    return shadowFactorSum / 9.0f;
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

    // 광원까지의 거리는 감쇠에 쓰고, 방향은 정규화해서 BRDF에 쓴다.
    float3 toLight = spotLight.position - input.posWorld;
    float lightDistance = length(toLight);
    float3 l = toLight / lightDistance;

    // 거리 감쇠는 선형 창이다(노트 3절). 물리적인 점광원은 1/d²로 줄어 0이 되지
    // 않지만, 창을 씌우면 fallOffEnd 밖에서 기여가 정확히 0이라 광원의 영향
    // 범위가 분명해진다.
    float attenuation =
        saturate((spotLight.fallOffEnd - lightDistance) /
                 (spotLight.fallOffEnd - spotLight.fallOffStart));
    float spot = SpotFactor(l);
    float shadow = ShadowFactor(input.posWorld);

    if (debugView == DebugViewSpotFactor)
        return float4(spot.xxx, 1.0f);
    if (debugView == DebugViewShadowFactor)
        return float4(shadow.xxx, 1.0f);

    // 원뿔 밖이나 그림자 속이면 이 광원의 직접광이 통째로 꺼진다. 다른 빛(IBL)이
    // 없으므로 그곳은 검정이다.
    float3 irradiance = spotLight.radiance * attenuation * spot * shadow;
    float3 color = DirectLighting(albedo, material.metallic, material.roughness,
                                  n, v, l, irradiance);
    return float4(color, 1.0f);
}
