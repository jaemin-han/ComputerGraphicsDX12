// 장면은 Shadertoy fsGcW3(modern_shinobi)에서 가져왔다.
#include "Common.hlsli"

// 루트 파라미터 1의 디스크립터 테이블이 t1~t3을, 정적 샘플러가 s0을 채운다.
// irradianceMap과 environmentMap은 이후 단계(조명, 반사)에서 읽는다.
TextureCube irradianceMap : register(t1);
TextureCube environmentMap : register(t2);
Texture2D groundTexture : register(t3);
SamplerState linearWrapSampler : register(s0);

// C++의 kSphereCount와 같아야 한다. 바닥은 구 다음 번호를 쓴다.
static const int SphereCount = 6;
static const int Ground = SphereCount;

// C++의 Surface, Sphere와 같은 배치다.
struct Surface
{
    float3 color;
    float reflectivity;
    float indexOfRefraction; // 0이면 불투명하다.
    float shininess;
    float2 padding;
};

struct Sphere
{
    float3 center;
    float radius;
    Surface surface;
};

// C++의 SceneConstants와 같은 배치다. 루트 파라미터 2가 프레임마다 한 번
// 이 버퍼의 주소를 넘기고, 모든 드로우가 같은 값을 읽는다.
cbuffer SceneConstants : register(b1)
{
    float3 eyeWorld;
    float groundHalfExtent;
    float3 groundCenter;
    float padding;
    Sphere spheres[SphereCount];
};

// 물체 번호로 그 위치의 재질을 읽는다. 바닥은 배열에 없으므로 월드 xz로
// 텍스처를 읽는다. 이후 2차 광선이 맞은 지점에는 UV가 없으므로 래스터로 그린
// 바닥도 같은 방법으로 읽는다.
Surface GetSurface(int index, float3 position)
{
    Surface surface = (Surface)0;
    if (index == Ground)
    {
        surface.color =
            groundTexture.SampleLevel(linearWrapSampler, position.xz * 0.5f, 0)
                .rgb;
        surface.reflectivity = 0.2f;
        surface.shininess = 16.0f;
    }
    else
    {
        surface = spheres[index].surface;
    }
    return surface;
}

// 0단계: 래스터가 찾은 1차 교차점의 재질 색만 출력한다. 이 PS가 실행됐다는
// 것은 깊이 테스트를 통과했다는 것, 곧 이 픽셀에서 눈에서 나간 광선이 처음
// 맞은 물체가 objectIndex라는 뜻이다.
float4 main(PixelShaderInput input) : SV_TARGET
{
    Surface surface = GetSurface(objectIndex, input.posWorld);
    return float4(surface.color, 1.0f);
}
