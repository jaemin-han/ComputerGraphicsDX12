// 장면은 Shadertoy fsGcW3(modern_shinobi)에서 가져왔다.
#include "Common.hlsli"

// 루트 파라미터 1의 디스크립터 테이블이 t1~t3을, 정적 샘플러가 s0을 채운다.
// irradianceMap은 환경광에서, environmentMap은 이후 반사 단계에서 읽는다.
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
    float padding0;
    float3 lightDirection; // 해의 빛이 나아가는 방향이다.
    float padding1;
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

// 한 점이 받는 빛을 해(Phong)와 하늘(diffuse IBL)로 나눠 더한다. 비율
// 0.35, 0.65, 0.35는 Part 2 값이고 보기 좋게 맞춘 튜닝 값이다.
float3 ShadeSurface(float3 position, float3 normal, Surface surface)
{
    float3 toLight = -lightDirection;
    float3 toEye = normalize(eyeWorld - position);

    // ambient 자리: 상수 대신 노멀 둘레 반구에서 들어온 하늘빛을 읽는다.
    // 무광 표면의 밝기는 보는 방향과 상관없으므로 노멀로만 읽는다. 내적
    // 가중은 이 큐브맵에 이미 들어 있다.
    float3 irradiance =
        irradianceMap.SampleLevel(linearWrapSampler, normal, 0).rgb;
    float3 color = 0.35f * irradiance * surface.color;

    // diffuse: 해에서 온 빛의 양에 재질 색(반사 비율)을 곱한다.
    float diffuse = saturate(dot(normal, toLight));
    color += 0.65f * diffuse * surface.color;

    // specular: 표면에서 바로 튕긴 빛이라 재질 색을 곱하지 않는다(흰 하이라이트).
    // 해가 비추는 면에만 생긴다. shininess가 0이면 pow가 어디서나 1이 되므로
    // 하이라이트가 없는 재질로 본다.
    if (diffuse > 0.0f && surface.shininess > 0.0f)
    {
        float highlight = saturate(dot(reflect(-toLight, normal), toEye));
        color += 0.35f * pow(highlight, surface.shininess);
    }
    return color;
}

// 1단계: 래스터가 찾은 1차 교차점을 로컬 셰이딩한다. 이 PS가 실행됐다는 것은
// 깊이 테스트를 통과했다는 것, 곧 이 픽셀에서 눈에서 나간 광선이 처음 맞은
// 물체가 objectIndex라는 뜻이다.
float4 main(PixelShaderInput input) : SV_TARGET
{
    float3 position = input.posWorld;
    float3 normal = normalize(input.normalWorld);
    if (objectIndex < Ground)
    {
        // 메시는 구를 삼각형으로 근사해서 보간된 위치가 수식 구보다 조금
        // 안쪽에 있다. 이후 단계의 2차 광선은 수식 구와 교차하므로, 같은
        // 표면에서 출발하도록 위치를 수식 구 표면으로 옮긴다.
        position = spheres[objectIndex].center +
                   normal * spheres[objectIndex].radius;
    }

    Surface surface = GetSurface(objectIndex, position);
    return float4(ShadeSurface(position, normal, surface), 1.0f);
}
