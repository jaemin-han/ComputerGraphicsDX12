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
    static constexpr UINT kWidth = 1920;
    static constexpr UINT kHeight = 1080;
    static constexpr UINT kBackBufferCount = 2;
    // CPU가 GPU를 기다리지 않고 앞서 제출할 수 있는 프레임 수다. 스왑체인이
    // 정하는 kBackBufferCount와는 별개다.
    static constexpr UINT kFrameCount = 2;
    // 프레임마다 상수를 쓰는 업로드 버퍼 크기다. 지금은 장면 b1 하나와 드로우
    // 23개(그림자 패스의 구 10개와 바닥, 메인 패스의 하늘, 구 10개, 바닥)의
    // b0(288바이트)이 256바이트 정렬로 512바이트씩, 약 12KB를 쓴다. 넘치면
    // WriteConstants가 예외를 던지므로 여유 있게 잡는다.
    static constexpr UINT64 kConstantBufferSize = 64 * 1024;
    // 루트 시그니처의 파라미터 번호다. 0은 드로우별 b0(루트 CBV, VS와 PS),
    // 1은 PS의 t0~t3(디스크립터 테이블), 2는 프레임별 b1(루트 CBV, PS)이다.
    static constexpr UINT kRootMeshConstants = 0;
    static constexpr UINT kRootTextures = 1;
    static constexpr UINT kRootSceneConstants = 2;

    // 셰이더 가시 힙의 칸 배치다. 디스크립터 테이블은 "시작 칸부터 연속 N칸"만
    // 가리킬 수 있으므로, 셰이더가 읽는 칸을 앞에 모으고 칸 번호와 t 번호를
    // 맞춘다.
    //   0: t0 irradiance   ┐
    //   1: t1 environment  │ 테이블(루트 파라미터 1) = 0번 칸부터 kTableSize칸
    //   2: t2 바닥          │
    //   3: t3 그림자 맵     ┘
    //   4: ImGui 폰트(테이블 밖, 백버퍼에 UI를 그릴 때 백엔드가 직접 가리킴)
    // 모든 칸은 초기화 때 한 번 쓰고 다시 쓰지 않는다. 그림자 맵은 내용이
    // 프레임마다 바뀌지만, SRV는 "어느 자원을 어떤 형식으로 읽나"만 적은
    // 칸이라 그대로다.
    static constexpr UINT kTextureCount = 3;
    static constexpr UINT kShadowMapSlot = kTextureCount;
    static constexpr UINT kTableSize = kShadowMapSlot + 1;
    static constexpr UINT kGuiFontSlot = kTableSize;
    static constexpr UINT kSrvHeapSize = kGuiFontSlot + 1;

    // DSV 힙의 칸 배치다. 0은 메인 패스의 깊이 버퍼, 1은 그림자 맵이다.
    static constexpr UINT kMainDepthSlot = 0;
    static constexpr UINT kShadowDepthSlot = 1;
    static constexpr UINT kDsvHeapSize = 2;

    // 그림자 맵의 가로, 세로 해상도다(정사각형). 11/19 측정 실험에서 이 값을
    // 바꿔 가며 GPU 시간과 화질을 비교한다.
    static constexpr UINT kShadowMapSize = 2048;

    void CreateDevice();
    void CreateFrameResources();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRenderTargets();
    void CreateDepthBuffer();
    void CreateShadowMap();
    void CreatePipeline();
    void CreateFence();
    void LoadTextures();
    void CreateTextureViews();
    // 그림자 맵에 DSV(그림자 패스가 깊이를 씀)와 SRV(메시 PS가 읽음)를 만든다.
    // 두 힙이 모두 만들어진 뒤 CreateTextureViews()가 부른다.
    void CreateShadowMapViews();
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
    // 구 격자와 바닥을 주어진 view, projection으로 그린다. 그림자 패스는 광원의
    // 행렬로, 메인 패스는 카메라의 행렬로 같은 장면을 그린다. 하늘은 그림자를
    // 드리우지 않으므로 여기에 넣지 않는다.
    void DrawScene(FrameResource &frame, DirectX::FXMMATRIX view,
                   DirectX::CXMMATRIX projection);
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
    UINT m_dsvDescriptorSize = 0;
    ComPtr<ID3D12Resource> m_depthBuffer;

    // 스포트라이트 시점의 깊이를 담는 텍스처다. 그림자 패스가 DSV로 깊이를
    // 쓰고, 같은 프레임의 메인 패스에서 메시 PS가 SRV(t3)로 읽는다. 메인 깊이
    // 버퍼와 같은 이유(GPU만 읽고 쓰고, 프레임들은 한 큐에서 차례로 실행된다)로
    // 프레임이 2개 진행 중이어도 하나면 된다.
    // 상태 흐름: 만들 때 DEPTH_WRITE → 그림자 패스 뒤 PIXEL_SHADER_RESOURCE →
    // 프레임 끝(Close 전)에 다시 DEPTH_WRITE. 프레임 경계에서는 늘
    // DEPTH_WRITE다.
    ComPtr<ID3D12Resource> m_shadowMap;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    // 패스별 PSO다. 그림자 패스는 광원 시점의 깊이만, 하늘은 깊이 없이 안쪽
    // 면을, 메시는 깊이 테스트를 하며 바깥 면을 그린다.
    ComPtr<ID3D12PipelineState> m_shadowPipeline;
    ComPtr<ID3D12PipelineState> m_skyPipeline;
    ComPtr<ID3D12PipelineState> m_meshPipeline;

    // 구는 반지름 1인 메시 하나를 드로우마다 모델 행렬로 옮겨 그린다.
    Mesh m_sphere;
    Mesh m_ground;

    // t1(하늘), t0(하늘의 확산 조명), t2(바닥)이다. t0은 직접광 PBR로 바꾸며
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

    // ImGui로 고치는 조명 설정이다. 프레임마다 b1에 옮겨 쓰고, 그림자 패스의
    // 광원 행렬도 여기서 만든다.
    struct LightingSettings {
        // 스포트라이트는 격자 왼쪽 앞 위에서 격자 가운데를 비스듬히 겨눈다.
        // 그림자가 오른쪽 뒤로 떨어져, 앞줄 구의 그림자가 바닥과 뒷줄 구에
        // 함께 진다. 방향은 target - position을 정규화해서 구한다.
        // 격자는 x -2.4~2.4, z 3.6~5.9, 높이 0~0.8 범위다.
        DirectX::XMFLOAT3 position = {-2.0f, 4.0f, 2.5f};
        DirectX::XMFLOAT3 target = {0.0f, 0.4f, 4.75f};
        DirectX::XMFLOAT3 color = {1.0f, 1.0f, 1.0f};
        // BRDF에 1/π를 남기므로 세기 π는 빛을 정면으로 받는 흰 Lambert 면이
        // 거의 albedo 그대로(화면 값 1 근처) 보이는 값이다. 톤 매핑이 없어 1
        // 위는 잘리므로 이것을 기준 밝기로 둔다.
        float intensity = DirectX::XM_PI;
        // 격자 가장자리 구까지 빛이 닿도록 원뿔을 넓게 둔다.
        float spotPower = 4.0f;
        float fallOffStart = 0.0f;
        float fallOffEnd = 20.0f;
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
