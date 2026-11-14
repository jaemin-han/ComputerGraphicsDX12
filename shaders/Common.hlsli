// C++의 Vertex와 같은 배치다.
struct VertexShaderInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
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
