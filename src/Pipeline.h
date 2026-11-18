#pragma once

#include <d3d12.h>
#include <wrl/client.h>

// 패스마다 달라지는 PSO 설정이다. 입력 레이아웃과 블렌딩은 모든 패스가 같다.
struct PipelineSettings {
    const char *vertexShader = nullptr; // 실행 파일 옆의 .cso 이름
    // nullptr이면 PS 없이 만든다. 래스터화와 깊이 테스트, 깊이 쓰기는 PS 없이도
    // 일어나므로, 색을 쓰지 않는 패스는 PS가 필요 없다.
    const char *pixelShader = nullptr;
    D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_BACK;
    // 끄면 깊이 테스트와 깊이 쓰기를 모두 하지 않는다.
    bool depthEnable = true;
};

// Vertex 입력을 받는 그래픽스 PSO를 만든다. 고정 기능 상태는 전부 이 불변
// 객체 하나에 들어가므로, 상태가 다른 패스마다 PSO를 하나씩 만든다.
// PSO는 그릴 때 묶일 타깃의 형식도 미리 알아야 한다. renderTargetFormat이
// DXGI_FORMAT_UNKNOWN이면 렌더 타깃이 0개인 PSO가 된다.
Microsoft::WRL::ComPtr<ID3D12PipelineState>
CreateGraphicsPipeline(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                       DXGI_FORMAT renderTargetFormat, DXGI_FORMAT depthFormat,
                       const PipelineSettings &settings);
