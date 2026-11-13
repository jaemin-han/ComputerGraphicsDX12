#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <vector>

#include "Mesh.h"

using Microsoft::WRL::ComPtr;

// 프레임 하나가 소유하는 자원이다. GPU가 그 프레임을 실행하는 동안 유지돼야
// 한다.
struct FrameResource {
    // 기록한 명령은 GPU가 끝낼 때까지 이 메모리에 남는다.
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    // 이 프레임을 제출한 뒤 Signal한 값이다. 펜스가 이 값에 도달하면 할당자를
    // Reset할 수 있다.
    UINT64 fenceValue = 0;
};

// 프로그램이 도는 동안 main()에서 하나만 만든다. 디바이스부터 펜스까지 GPU
// 객체를 모두 갖고, 메시를 만들고 그린다.
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

    void CreateDevice();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRenderTargets();
    void CreatePipeline();
    void CreateFence();

    Mesh CreateMesh(const std::vector<Vertex> &vertices);
    void DrawMesh(const Mesh &mesh);

    void Transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after);
    void Render();

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

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    Mesh m_triangle;

    ComPtr<ID3D12Fence> m_fence;
    // 마지막으로 Signal()에 넘긴 값이다. 각 프레임은 자기 값을
    // FrameResource::fenceValue에 따로 보관한다.
    UINT64 m_fenceCounter = 0;
    HANDLE m_fenceEvent = nullptr;
};
