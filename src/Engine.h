#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <chrono>

#include "ConstantBuffers.h"
#include "FrameResource.h"
#include "Mesh.h"

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
    // 한 프레임에 그릴 수 있는 드로우 수다(구 6개와 바닥). 드로우마다 상수
    // 버퍼 칸을 하나씩 쓴다.
    static constexpr UINT kMaxDrawsPerFrame = 7;
    // 루트 시그니처의 파라미터 번호다.
    static constexpr UINT kRootMeshConstants = 0;

    void CreateDevice();
    void CreateFrameResources();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRenderTargets();
    void CreateDepthBuffer();
    void CreatePipeline();
    void CreateFence();

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
    ComPtr<ID3D12PipelineState> m_pipelineState;

    // 구는 반지름 1인 메시 하나를 드로우마다 모델 행렬로 옮겨 그린다.
    Mesh m_sphere;
    Mesh m_ground;
    // 카메라는 이 시각부터 흐른 시간에 따라 돈다.
    std::chrono::steady_clock::time_point m_startTime;

    ComPtr<ID3D12Fence> m_fence;
    // 마지막으로 Signal()에 넘긴 값이다. 각 프레임은 자기 값을
    // FrameResource::fenceValue에 따로 보관한다.
    UINT64 m_fenceCounter = 0;
    HANDLE m_fenceEvent = nullptr;
};
