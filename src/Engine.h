#pragma once

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <chrono>
#include <cmath>

#include "Camera.h"
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
    // 프레임마다 상수를 쓰는 업로드 버퍼 크기다. 지금은 장면 b1 하나와 드로우
    // 12개(하늘, 구 10개, 바닥)의 b0(288바이트)이 256바이트 정렬로 512바이트씩,
    // 약 6KB를 쓴다. 넘치면 WriteConstants가 예외를 던지므로 여유 있게 잡는다.
    static constexpr UINT64 kConstantBufferSize = 64 * 1024;
    // 루트 시그니처의 파라미터 번호다. 0은 드로우별 b0(루트 CBV, VS와 PS),
    // 1은 PS의 t1~t3(디스크립터 테이블), 2는 프레임별 b1(루트 CBV, PS)이다.
    static constexpr UINT kRootMeshConstants = 0;
    static constexpr UINT kRootTextures = 1;
    static constexpr UINT kRootSceneConstants = 2;
    // 장면 텍스처 수다. 셰이더 가시 힙의 0~2번 칸에 t1, t2, t3 순서로
    // 연속해서 두고, 디스크립터 테이블이 힙 시작부터 이 3칸을 읽는다.
    static constexpr UINT kTextureCount = 3;
    // ImGui 폰트 SRV는 텍스처 다음 칸에 둔다. 테이블이 읽는 0~2번 칸은
    // 그대로라 테이블 설정을 바꾸지 않아도 된다.
    static constexpr UINT kGuiFontSlot = kTextureCount;
    static constexpr UINT kSrvHeapSize = kGuiFontSlot + 1;

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
    void InitializeGui();
    void ShutdownGui();
    // ImGui 새 프레임을 시작하고 창을 만든 뒤 그릴 데이터를 확정한다. 커맨드
    // 리스트 기록은 Render() 끝에서 한다.
    void BuildGui();
    // 마우스(오른쪽 버튼을 누른 동안)와 키보드 입력으로 1인칭 카메라를
    // 움직인다. ImGui가 입력을 받은 뒤에 부른다.
    void UpdateCamera(float deltaSeconds);

    void Transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after);
    void Render();
    void DrawMesh(FrameResource &frame, const Mesh &mesh,
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

    // t2(하늘), t1(하늘의 확산 조명), t3(바닥)이다. t1은 직접광 PBR로 바꾸며
    // 읽는 셰이더가 없어졌고, IBL 단계에서 다시 쓴다.
    Texture m_environmentMap;
    Texture m_irradianceMap;
    Texture m_groundTexture;
    // 위 텍스처와 ImGui 폰트의 SRV를 담는 셰이더 가시 힙이다. 초기화 때 한 번
    // 쓰고 다시 쓰지 않으므로 프레임마다 나누지 않는다.
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    UINT m_srvDescriptorSize = 0;
    // ImGui 백엔드를 초기화했으면 소멸자에서 정리한다.
    bool m_guiInitialized = false;
    // ImGui가 kGuiFontSlot 칸에 SRV를 쓰고 아직 돌려주지 않았는지다.
    bool m_guiFontSlotUsed = false;

    // ImGui로 고치는 조명 설정이다. 프레임마다 b1에 옮겨 쓴다.
    struct LightingSettings {
        // 점광원은 구 격자 앞 위쪽, 카메라와 앞줄 사이에 둔다. BRDF에 1/π를
        // 남기므로 세기 π는 빛을 정면으로 받는 흰 Lambert 면이 거의 albedo
        // 그대로(화면 값 1 근처) 보이는 값이다. 톤 매핑이 없어 1 위는
        // 잘리므로 이것을 기준 밝기로 둔다.
        DirectX::XMFLOAT3 pointPosition = {0.0f, 2.0f, 3.0f};
        DirectX::XMFLOAT3 pointColor = {1.0f, 1.0f, 1.0f};
        float pointIntensity = DirectX::XM_PI;
        float pointFallOffStart = 0.0f;
        float pointFallOffEnd = 20.0f;
        int debugView = 0;
    };
    LightingSettings m_lighting;

    // 구 격자 앞에서 두 줄 사이 (0, 0.4, 4.75)를 내려다본다. 높이 차 1.6,
    // 수평 거리 3.75다.
    Camera m_camera{{0.0f, 2.0f, 1.0f}, 0.0f, std::atan2(1.6f, 3.75f)};
    // 오른쪽 버튼을 누르고 있는 동안 커서를 숨기고 이 위치에 붙잡아 둔다.
    // 프레임마다 여기서 움직인 양만큼 돈다.
    bool m_mouseLook = false;
    POINT m_mouseLookAnchor = {};
    std::chrono::steady_clock::time_point m_lastFrameTime;

    ComPtr<ID3D12Fence> m_fence;
    // 마지막으로 Signal()에 넘긴 값이다. 각 프레임은 자기 값을
    // FrameResource::fenceValue에 따로 보관한다.
    UINT64 m_fenceCounter = 0;
    HANDLE m_fenceEvent = nullptr;
};
