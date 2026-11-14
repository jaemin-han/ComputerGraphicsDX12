#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <chrono>

#include "ConstantBuffers.h"
#include "FrameResource.h"
#include "Mesh.h"
#include "Texture.h"

using Microsoft::WRL::ComPtr;

// 프로그램이 도는 동안 main()에서 하나만 만든다. 디바이스부터 펜스까지 GPU
// 객체를 갖고, 루트 시그니처와 PSO를 정하고, 한 프레임의 드로우를 기록한다.
class Engine {
  public:
    ~Engine();

    void Initialize();
    int Run();

  private:
    static constexpr UINT kWidth = 1280;
    static constexpr UINT kHeight = 720;
    static constexpr UINT kBackBufferCount = 2;
    // CPU가 GPU를 기다리지 않고 앞서 제출할 수 있는 프레임 수다. 스왑체인이
    // 정하는 kBackBufferCount와는 별개다.
    static constexpr UINT kFrameCount = 2;
    // 한 프레임에 그릴 수 있는 드로우 수다(하늘, 구 6개, 바닥). 드로우마다
    // 상수 버퍼 칸을 하나씩 쓴다.
    static constexpr UINT kMaxDrawsPerFrame = 8;
    // 루트 시그니처의 파라미터 번호다. 0은 VS의 b0(루트 CBV), 1은 PS의
    // t1~t3(디스크립터 테이블)이다.
    static constexpr UINT kRootMeshConstants = 0;
    static constexpr UINT kRootTextures = 1;
    // 셰이더 가시 힙의 칸 수다. t1, t2, t3 순서로 연속해서 둔다.
    static constexpr UINT kTextureCount = 3;

    void CreateDevice();
    void CreateFrameResources();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRenderTargets();
    void CreateDepthBuffer();
    void CreatePipeline();
    void CreateFence();
    void LoadTextures();
    void CreateTextureViews();

    void Transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after);
    void Render();
    void DrawMesh(FrameResource &frame, UINT drawIndex, const Mesh &mesh,
                  const MeshConstants &constants);

    UINT64 Signal();
    void WaitForFenceValue(UINT64 value);
    void WaitForGpu();

    HWND m_window = nullptr;

    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    FrameResource m_frames[kFrameCount];
    UINT m_frameIndex = 0;

    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12Resource> m_renderTargets[kBackBufferCount];
    UINT m_rtvDescriptorSize = 0;
    UINT m_backBufferIndex = 0;

    // GPU만 읽고 쓰고 프레임들은 큐에서 차례로 실행되므로, 프레임이 2개 진행
    // 중이어도 하나면 된다.
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12Resource> m_depthBuffer;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    // 패스별 PSO다. 하늘은 깊이 없이 안쪽 면을, 메시는 깊이 테스트를 하며
    // 바깥 면을 그린다.
    ComPtr<ID3D12PipelineState> m_skyPipeline;
    ComPtr<ID3D12PipelineState> m_meshPipeline;

    // 구는 반지름 1인 메시 하나를 드로우마다 모델 행렬로 옮겨 그린다.
    Mesh m_sphere;
    Mesh m_ground;

    // 레이 씬의 t2(하늘과 반사), t1(확산 조명), t3(바닥)이다.
    Texture m_environmentMap;
    Texture m_irradianceMap;
    Texture m_groundTexture;
    // 위 텍스처의 SRV를 담는 셰이더 가시 힙이다. 초기화 때 한 번 쓰고 다시
    // 쓰지 않으므로 프레임마다 나누지 않는다.
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    // 카메라는 이 시각부터 흐른 시간에 따라 돈다.
    std::chrono::steady_clock::time_point m_startTime;

    ComPtr<ID3D12Fence> m_fence;
    // 마지막으로 Signal()에 넘긴 값이다. 각 프레임은 자기 값을
    // FrameResource::fenceValue에 따로 보관한다.
    UINT64 m_fenceCounter = 0;
    HANDLE m_fenceEvent = nullptr;
};
