// C++의 Vertex와 같은 배치다.
struct VertexShaderInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};

// C++의 MeshConstants와 같은 배치다. 드로우마다 루트 파라미터 0이 이 버퍼의
// 주소를 바꾼다. VS는 행렬을, 레이 PS는 objectIndex를 읽는다.
cbuffer MeshConstants : register(b0)
{
    matrix model;
    matrix invTranspose;
    matrix view;
    matrix projection;
    int objectIndex;
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
