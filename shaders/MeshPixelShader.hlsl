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

float3 Fresnel(float3 albedo, float metallic, float vDotH)
{
    // TODO(학습) 1: 미세면 하나가 빛을 반사하는 비율 F를 구한다.
    // 입력: albedo(base color), metallic(0~1), vDotH(v·h, 0~1로 잘라서 들어온다).
    // 출력: 채널별 반사율(0~1). 먼저 metallic 워크플로로 F0을 정하고, 그
    //       F0에서 Schlick 근사로 F를 구한다. 5제곱 항을 그대로 쓸지 노트 7절의
    //       근사로 바꿀지는 고른다.
    // 조건: vDotH = 1이면 F0, vDotH = 0이면 1(흰색)이다.
    // 확인: View를 F로 둔다. 빨간 구는 거의 검은 회색, 금 구는 금색이다.
    //       Light position을 구 너머(z 8 근처)로 옮겨 l과 v가 마주 보게 하면
    //       테두리만이 아니라 구 전체가 흰색 쪽으로 밝아진다.
    float3 F0 = lerp(0.04, albedo, metallic);
    float3 F = F0 + (1 - F0) * pow((1 - vDotH), 5);
    
    return F;
}

float DistributionGGX(float nDotH, float roughness)
{
    // TODO(학습) 2: 법선이 h인 미세면의 밀도 D(GGX)를 구한다.
    // 입력: nDotH(n·h, 0~1), roughness(재질의 슬라이더 값, 0~1).
    // 출력: 0 이상의 밀도. 1을 넘을 수 있다.
    // 조건: roughness를 식의 α로 바꾸는 방식은 노트 7절에서 정한 것을 쓴다.
    //       roughness 0, nDotH 1에서도 NaN이 나오면 안 된다.
    // 확인: View를 D로 둔다(1을 넘으면 흰색으로 잘린다). 맨 왼쪽 구는 검정,
    //       0.25 구는 작고 하얀 점, 맨 오른쪽 구는 어디서나 회색 0.318이다.
    
    float alphaSq = pow(roughness, 4);
    
    float D = alphaSq / (PI * pow(nDotH * nDotH * (alphaSq - 1) + 1, 2) + 1e-7);
    
    return D;
}

float SchlickG1(float nDotX, float k)
{
    return nDotX / (nDotX * (1 - k) + k);

}

float GeometrySmith(float nDotL, float nDotV, float roughness)
{
    // TODO(학습) 3: 미세면이 빛 쪽과 눈 쪽에서 가려지지 않은 비율 G를 구한다.
    // 입력: nDotL(n·l), nDotV(n·v), 둘 다 0~1로 잘라서 들어온다.
    //       roughness(재질의 슬라이더 값, 0~1).
    // 출력: 0~1.
    // 조건: Smith 분리형이다. 근사식과 그 안의 상수는 노트 7절에서 정한 점광원
    //       용을 쓴다.
    // 확인: View를 G로 둔다. 대부분 흰색이고, 광원 반대쪽 경계와 실루엣
    //       근처에서만 어두워진다. 오른쪽(거친) 구일수록 더 넓게 어둡다.
    float k = pow(roughness + 1, 2) / 8;
    
    return SchlickG1(nDotL, k) * SchlickG1(nDotV, k);
}

float3 DirectLighting(float3 albedo, float metallic, float roughness,
                      float3 n, float3 v, float3 l, float3 irradiance)
{
    // TODO(학습) 4: 광원 하나에서 와서 눈으로 나가는 radiance를 구한다.
    // 입력: albedo, metallic, roughness(재질), n, v, l(단위 벡터, l은 표면에서
    //       광원으로), irradiance(감쇠까지 곱한 광원 값).
    // 출력: 이 광원의 기여(radiance). 광원이 여럿이면 main이 더한다.
    // 조건: 위 세 함수를 쓴다. diffuse와 specular의 에너지 분배와 관례(②: BRDF
    //       에 1/π를 남긴다)는 노트대로다. 표면 뒤에서 오는 빛은 0이고, 분모가
    //       0이 되지 않게 한다.
    // 확인: View를 Final로 둔다. 빨간 구는 빨간 몸통에 흰 하이라이트, 금 구는
    //       하이라이트 말고는 거의 검정이다. 오른쪽으로 갈수록 하이라이트가
    //       넓고 어두워지고, 맨 왼쪽 열의 두 구에는 하이라이트가 없다. 바닥은
    //       광원 아래가 밝다.
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
