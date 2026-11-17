// C++의 Vertex와 같은 배치다.
struct VertexShaderInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};

// C++의 Material과 같은 배치다.
struct Material
{
    float3 albedo;
    float metallic;
    float roughness;
    int useAlbedoTexture;
};

// C++의 MeshConstants와 같은 배치다. 드로우마다 루트 파라미터 0이 이 버퍼의
// 주소를 바꾼다. VS는 행렬을, PS는 자기 표면의 재질을 읽는다.
cbuffer MeshConstants : register(b0)
{
    matrix model;
    matrix invTranspose;
    matrix view;
    matrix projection;
    Material material;
};

// C++의 PointLight와 같은 배치다.
struct PointLight
{
    float3 position;
    float fallOffStart;
    float3 radiance; // 빛을 수직으로 받는 면에 들어오는 irradiance다.
    float fallOffEnd;
};

// C++의 SceneConstants와 같은 배치다. 프레임마다 한 번 쓰고 모든 드로우가
// 같은 값을 읽는다.
cbuffer SceneConstants : register(b1)
{
    float3 eyeWorld;
    int debugView;
    PointLight pointLight;
};

// 하늘 패스의 VS 출력이다. 큐브맵을 읽을 방향만 넘긴다.
struct SkyPixelShaderInput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float3 posWorld : POSITION;
    float3 normalWorld : NORMAL;
    float2 texcoord : TEXCOORD;
};
