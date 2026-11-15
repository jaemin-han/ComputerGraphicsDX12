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
// 광선이 아무것도 맞히지 않았다는 뜻이다. C++의 kNoObjectIndex와 같다.
static const int NoObject = -1;
// 표면에서 새 광선을 쏠 때 노멀 쪽으로 띄우는 거리다. 출발한 표면에 다시
// 맞는 것(자기 그림자)을 막는다.
static const float RayBias = 1e-3f;

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

struct Ray
{
    float3 start;
    float3 direction; // 정규화된 방향이다.
};

// 광선이 맞힌 물체와 그 지점이다. 아무것도 맞히지 않으면 objectIndex가
// NoObject다.
struct Hit
{
    int objectIndex;
    float distance;
    float3 position;
    float3 normal;
};

// 광선 start + d * direction이 구와 만나는 가장 가까운 앞쪽 거리 d를
// 돌려준다. 만나지 않으면 음수다. Part 1 Sphere::CheckRayCollision과 같은 근의
// 공식이다. 출발점이 구 안이면 앞쪽 근(d2)을 쓴다(4단계 굴절 광선).
float IntersectSphere(Ray ray, Sphere sphere)
{
    float3 fromCenter = ray.start - sphere.center;
    float b = 2.0f * dot(ray.direction, fromCenter);
    float c = dot(fromCenter, fromCenter) - sphere.radius * sphere.radius;
    float det = b * b - 4.0f * c;

    // FXC는 중간에 return이 있으면 오탐 경고(X4000)를 내므로 반환을 하나로
    // 모은다.
    float d = -1.0f;
    if (det >= 0.0f)
    {
        float d1 = (-b - sqrt(det)) * 0.5f;
        float d2 = (-b + sqrt(det)) * 0.5f;
        d = d1 > 0.0f ? d1 : d2;
    }
    return d;
}

// 바닥은 높이 groundCenter.y에 수평으로 누운, x와 z로 groundHalfExtent만큼
// 뻗은 사각형이다. 평면과 직선의 교점을 이 경우에 맞춰 푼다. 노멀이 y축이라
// 거리는 y 성분만으로 정해지고, 안쪽 판정은 x, z 범위 검사가 된다.
float IntersectGround(Ray ray)
{
    // 바닥과 나란한 광선은 만나지 않는다. 0으로 나누는 것도 막는다.
    float d = -1.0f;
    if (abs(ray.direction.y) > 1e-6f)
    {
        d = (groundCenter.y - ray.start.y) / ray.direction.y;
        float3 position = ray.start + d * ray.direction;
        float2 offset = abs(position.xz - groundCenter.xz);
        if (offset.x > groundHalfExtent || offset.y > groundHalfExtent)
            d = -1.0f;
    }
    return d;
}

// 광선이 장면에서 가장 먼저 맞히는 물체를 찾는다. Part 1
// FindClosestCollision과 같은 일이다. HLSL에는 가상 함수가 없으므로 물체
// 종류마다 교차 함수를 부르고, 물체 목록은 b1의 배열이다.
Hit TraceScene(Ray ray)
{
    Hit hit = (Hit)0;
    hit.objectIndex = NoObject;
    hit.distance = 1e30f;

    for (int i = 0; i < SphereCount; ++i)
    {
        float d = IntersectSphere(ray, spheres[i]);
        if (d > 0.0f && d < hit.distance)
        {
            hit.objectIndex = i;
            hit.distance = d;
        }
    }

    float d = IntersectGround(ray);
    if (d > 0.0f && d < hit.distance)
    {
        hit.objectIndex = Ground;
        hit.distance = d;
    }

    hit.position = ray.start + hit.distance * ray.direction;
    if (hit.objectIndex == Ground)
        hit.normal = float3(0.0f, 1.0f, 0.0f);
    else if (hit.objectIndex != NoObject)
        hit.normal = normalize(hit.position - spheres[hit.objectIndex].center);
    return hit;
}

// 해까지 가는 길이 열려 있는지 본다. 해는 무한히 멀리 있으므로 앞쪽에
// 무엇이든 맞으면 가려진 것이다(Part 1 점광원의 거리 조건이 필요 없다).
bool IsLit(float3 position, float3 normal, float3 toLight)
{
    Ray shadowRay = {position + normal * RayBias, toLight};
    return TraceScene(shadowRay).objectIndex == NoObject;
}

// 한 점이 받는 빛을 해(Phong)와 하늘(diffuse IBL)로 나눠 더한다. 비율
// 0.35, 0.65, 0.35는 Part 2 값이고 보기 좋게 맞춘 튜닝 값이다.
float3 ShadeSurface(float3 position, float3 normal, Surface surface)
{
    float3 toLight = -lightDirection;
    float3 toEye = normalize(eyeWorld - position);

    // ambient 자리: 상수 대신 노멀 둘레 반구에서 들어온 하늘빛을 읽는다.
    // 무광 표면의 밝기는 보는 방향과 상관없으므로 노멀로만 읽는다. 내적
    // 가중은 이 큐브맵에 이미 들어 있다. 하늘빛은 사방에서 오므로 해가
    // 가려져도 남긴다.
    float3 irradiance =
        irradianceMap.SampleLevel(linearWrapSampler, normal, 0).rgb;
    float3 color = 0.35f * irradiance * surface.color;

    // 해를 등진 면에는 그림자 광선을 쏘지 않는다. HLSL의 &&는 양쪽을 모두
    // 계산하므로(단락 평가가 없다) 비싼 IsLit은 if를 나눠 안쪽에 둔다.
    float diffuse = saturate(dot(normal, toLight));
    if (diffuse > 0.0f)
    {
        // 해가 가려지면 해에서 오는 diffuse와 specular를 모두 끈다.
        if (IsLit(position, normal, toLight))
        {
            // diffuse: 해에서 온 빛의 양에 재질 색(반사 비율)을 곱한다.
            color += 0.65f * diffuse * surface.color;

            // specular: 표면에서 바로 튕긴 빛이라 재질 색을 곱하지 않는다
            // (흰 하이라이트). shininess가 0이면 pow가 어디서나 1이 되므로
            // 하이라이트가 없는 재질로 본다.
            if (surface.shininess > 0.0f)
            {
                float highlight =
                    saturate(dot(reflect(-toLight, normal), toEye));
                color += 0.35f * pow(highlight, surface.shininess);
            }
        }
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
