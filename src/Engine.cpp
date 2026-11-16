// 위에서 아래로 읽는다. Initialize()는 객체를 만드는 순서를, Render()는 한
// 프레임 동안 GPU에 시키는 일 전체를 보여 준다. 프레임 2개를 동시에 진행해
// GPU가 이전 프레임을 실행하는 동안 CPU가 다음 프레임을 기록한다.

#include "Engine.h"

#include <d3d12sdklayers.h>

#include <climits>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "Geometry.h"
#include "Pipeline.h"
#include "Util.h"
#include "Window.h"

using namespace DirectX;

namespace {

constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
// 스텐실은 아직 쓰지 않는다.
constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;

#ifdef _DEBUG
constexpr bool kEnableDebugLayer = true;
#else
constexpr bool kEnableDebugLayer = false;
#endif

// DirectXMath는 행 우선이고 HLSL cbuffer는 기본이 열 우선이라 전치해서 넣는다.
// 법선 행렬(역행렬의 전치)을 전치하면 역행렬이 되므로 invTranspose에는
// 역행렬을 그대로 넣는다.
MeshConstants MakeMeshConstants(FXMMATRIX model, CXMMATRIX view,
                                CXMMATRIX projection, int objectIndex) {
    MeshConstants constants = {};
    XMStoreFloat4x4(&constants.model, XMMatrixTranspose(model));
    XMStoreFloat4x4(&constants.invTranspose, XMMatrixInverse(nullptr, model));
    XMStoreFloat4x4(&constants.view, XMMatrixTranspose(view));
    XMStoreFloat4x4(&constants.projection, XMMatrixTranspose(projection));
    constants.objectIndex = objectIndex;
    return constants;
}

// 레이 씬의 물체 번호다. 구는 kSpheres의 번호(0~5)를 쓰고, 바닥은 그
// 다음이다. 하늘은 레이 PS를 쓰지 않으므로 어떤 물체도 가리키지 않는다.
constexpr int kGroundIndex = kSphereCount;
constexpr int kNoObjectIndex = -1;

// Part 2 레이 씬의 구 배치와 재질이다. 중심, 반지름, 그리고 색, 반사율,
// 굴절률(0이면 불투명), 광택 순서다. 유리 구(0번, 5번)는 색 없이 굴절로만
// 보이므로 색이 검정이다.
const Sphere kSpheres[kSphereCount] = {
    {{0.0f, 0.8f, 2.0f}, 0.8f, {{0.0f, 0.0f, 0.0f}, 0.1f, 1.25f, 32.0f}},
    {{1.0f, 1.1f, 6.0f}, 1.1f, {{0.3f, 0.3f, 1.0f}, 0.1f, 0.0f, 16.0f}},
    {{-2.0f, 0.5f, 3.0f}, 0.5f, {{0.8f, 0.8f, 0.1f}, 1.0f, 0.0f, 32.0f}},
    {{1.5f, 0.8f, 3.0f}, 0.5f, {{0.0f, 1.0f, 1.0f}, 0.0f, 0.0f, 0.0f}},
    {{-0.8f, 1.0f, 4.0f}, 1.0f, {{1.0f, 0.1f, 0.1f}, 0.5f, 0.0f, 16.0f}},
    {{-2.0f, 1.0f, 7.0f}, 1.0f, {{0.0f, 0.0f, 0.0f}, 0.1f, 1.5f, 32.0f}},
};

constexpr float kGroundHalfExtent = 5.0f;
const XMFLOAT3 kGroundCenter = {0.0f, 0.0f, 5.0f};

} // namespace

Engine::~Engine() {
    // 이 객체가 해제하려는 리소스를 GPU가 아직 쓰고 있을 수 있다.
    if (m_fenceEvent) {
        try {
            WaitForGpu();
        } catch (const std::exception &error) {
            std::cerr << error.what() << std::endl;
        }
        CloseHandle(m_fenceEvent);
    }
    // ImGui가 가진 폰트 텍스처와 버퍼도 GPU가 다 쓴 뒤에 놓는다. 힙은 이 뒤에
    // 멤버 소멸 순서대로 해제된다.
    if (m_guiInitialized)
        ShutdownGui();
}

void Engine::Initialize() {
    m_window = CreateAppWindow(kWidth, kHeight, L"ComputerGraphicsDX12");
    CreateDevice();
    CreateFrameResources();
    CreateCommandObjects();
    CreateSwapChain();
    CreateRenderTargets();
    CreateDepthBuffer();
    CreatePipeline();
    // 텍스처 업로드가 GPU 완료를 기다려야 해서 펜스를 먼저 만든다.
    CreateFence();

    m_sphere = Mesh::Create(m_device.Get(), MakeSphere(1.0f, 96, 64));
    m_ground = Mesh::Create(m_device.Get(), MakeSquare());
    LoadTextures();
    CreateTextureViews();
    InitializeGui();

    m_startTime = std::chrono::steady_clock::now();
}

int Engine::Run() {
    ShowWindow(m_window, SW_SHOW);

    MSG message = {};
    while (message.message != WM_QUIT) {
        if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        } else {
            Render();
        }
    }
    return static_cast<int>(message.wParam);
}

void Engine::CreateDevice() {
    UINT factoryFlags = 0;
    if (kEnableDebugLayer) {
        // 디바이스를 만들기 전에 켜야 한다.
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        } else {
            std::cerr << "D3D12 debug layer is not available." << std::endl;
        }
    }

    Check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)),
          "CreateDXGIFactory2");

    // 어댑터가 여러 개일 수 있어서 가장 빠른 것부터 요청한다.
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapterByGpuPreference(
                         i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                         IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())) !=
                     DXGI_ERROR_NOT_FOUND;
         ++i) {
        DXGI_ADAPTER_DESC1 description;
        Check(adapter->GetDesc1(&description), "GetDesc1");
        if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&m_device)))) {
            std::wcout << L"Adapter: " << description.Description << std::endl;
            break;
        }
    }
    if (!m_device)
        throw std::runtime_error("No Direct3D 12 hardware adapter found.");

    if (kEnableDebugLayer) {
        ComPtr<ID3D12InfoQueue1> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue))) {
            DWORD cookie = 0;
            infoQueue->RegisterMessageCallback(OnD3D12Message,
                                               D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                                               nullptr, &cookie);
        }
    }
}

void Engine::CreateFrameResources() {
    // GPU가 이전 프레임의 할당자와 상수 버퍼를 아직 쓰고 있을 수 있으므로
    // 프레임마다 따로 둔다.
    for (FrameResource &frame : m_frames)
        frame.Create(m_device.Get(), kConstantBufferSize);
}

void Engine::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(
        m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)),
        "CreateCommandQueue");

    // 리스트는 하나면 충분하다. Reset할 때 붙인 할당자에 기록할 뿐이고,
    // 제출한 직후 바로 Reset할 수 있다.
    Check(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      m_frames[0].commandAllocator.Get(),
                                      nullptr, IID_PPV_ARGS(&m_commandList)),
          "CreateCommandList");

    // 커맨드 리스트는 열린 상태로 만들어지지만 Render()는 Reset()부터 한다.
    Check(m_commandList->Close(), "Close");
}

void Engine::CreateSwapChain() {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.Format = kBackBufferFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBackBufferCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    // Direct3D 12 스왑체인은 디바이스가 아니라 큐를 통해 Present한다.
    ComPtr<IDXGISwapChain1> swapChain;
    Check(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_window,
                                            &desc, nullptr, nullptr,
                                            &swapChain),
          "CreateSwapChainForHwnd");

    // Alt+Enter를 받으려면 이 프로그램에 없는 리사이즈 경로가 필요하다.
    Check(m_factory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER),
          "MakeWindowAssociation");

    Check(swapChain.As(&m_swapChain), "IDXGISwapChain3");
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void Engine::CreateRenderTargets() {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = kBackBufferCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    Check(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap)),
          "CreateDescriptorHeap");

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        Check(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])),
              "GetBuffer");
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr,
                                         handle);
        handle.ptr += m_rtvDescriptorSize;
    }
}

void Engine::CreateDepthBuffer() {
    // RTV처럼 DSV도 디스크립터 힙의 칸에 만든다. Clear와 OMSet은 리소스가
    // 아니라 이 칸(어떤 형식으로 어떻게 쓸지 적은 기록)을 받는다.
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    Check(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_dsvHeap)),
          "CreateDescriptorHeap");

    // GPU만 쓰는 텍스처라 기본 힙에 둔다. 크기는 백버퍼와 같아야 한다.
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kDepthFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    // 매 프레임 1.0으로 지우므로, 같은 값을 최적화 클리어 값으로 알려 준다.
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = kDepthFormat;
    clearValue.DepthStencil.Depth = 1.0f;

    // 깊이 쓰기 상태로 만들고 계속 그 상태로 둔다. 셰이더가 깊이를 읽는 패스가
    // 없어서 배리어가 필요 없다.
    Check(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                            D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                            &clearValue,
                                            IID_PPV_ARGS(&m_depthBuffer)),
          "CreateCommittedResource");

    m_device->CreateDepthStencilView(
        m_depthBuffer.Get(), nullptr,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void Engine::CreatePipeline() {
    // 루트 시그니처는 셰이더가 받는 인자 목록이다.
    D3D12_ROOT_PARAMETER rootParameters[3] = {};

    // 0: 드로우별 b0. 드로우마다 바뀌므로, 디스크립터를 거치지 않고 GPU
    // 주소를 바로 넘기는 루트 CBV로 둔다. VS가 행렬을, 레이 PS가 물체 번호를
    // 읽으므로 모든 단계에 보이게 한다.
    D3D12_ROOT_PARAMETER &meshConstants = rootParameters[kRootMeshConstants];
    meshConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    meshConstants.Descriptor.ShaderRegister = 0; // b0
    meshConstants.Descriptor.RegisterSpace = 0;
    meshConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // 1: PS의 t1~t3. 텍스처는 view가 있어야 읽을 수 있으므로 힙의 칸을
    // 가리키는 디스크립터 테이블로 둔다. 모든 드로우가 같은 세 장을 쓰므로
    // 연속 3칸을 테이블 하나로 묶는다.
    D3D12_DESCRIPTOR_RANGE textureRange = {};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = kTextureCount;
    textureRange.BaseShaderRegister = 1; // t1
    textureRange.RegisterSpace = 0;
    textureRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER &textures = rootParameters[kRootTextures];
    textures.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    textures.DescriptorTable.NumDescriptorRanges = 1;
    textures.DescriptorTable.pDescriptorRanges = &textureRange;
    textures.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 2: 프레임별 b1. 장면 전체의 데이터라 프레임마다 한 번 쓰고 모든
    // 드로우가 같은 주소를 읽는다. b0처럼 업로드 버퍼의 주소만 넘긴다.
    D3D12_ROOT_PARAMETER &sceneConstants = rootParameters[kRootSceneConstants];
    sceneConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    sceneConstants.Descriptor.ShaderRegister = 1; // b1
    sceneConstants.Descriptor.RegisterSpace = 0;
    sceneConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s0: 바꿀 일이 없는 샘플러라 루트 시그니처에 정적 샘플러로 넣는다.
    // 샘플러 힙과 바인딩 호출이 필요 없다.
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0; // s0
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParameters);
    rootDesc.pParameters = rootParameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3D12SerializeRootSignature(
        &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (errors) {
        std::cerr << static_cast<const char *>(errors->GetBufferPointer())
                  << std::endl;
    }
    Check(hr, "D3D12SerializeRootSignature");

    Check(m_device->CreateRootSignature(0, serialized->GetBufferPointer(),
                                        serialized->GetBufferSize(),
                                        IID_PPV_ARGS(&m_rootSignature)),
          "CreateRootSignature");

    // 두 패스가 같은 루트 시그니처를 쓰고, 상태만 다른 PSO를 하나씩 갖는다.
    // 하늘은 맨 먼저 깊이 없이 그리고, 구 안에서 보므로 안쪽 면(뒷면)을
    // 남기도록 앞면을 버린다.
    PipelineSettings sky;
    sky.vertexShader = "SkyVertexShader.cso";
    sky.pixelShader = "SkyPixelShader.cso";
    sky.cullMode = D3D12_CULL_MODE_FRONT;
    sky.depthEnable = false;
    m_skyPipeline =
        CreateGraphicsPipeline(m_device.Get(), m_rootSignature.Get(),
                               kBackBufferFormat, kDepthFormat, sky);

    PipelineSettings mesh;
    mesh.vertexShader = "MeshVertexShader.cso";
    mesh.pixelShader = "RayScenePixelShader.cso";
    m_meshPipeline =
        CreateGraphicsPipeline(m_device.Get(), m_rootSignature.Get(),
                               kBackBufferFormat, kDepthFormat, mesh);
}

void Engine::CreateFence() {
    Check(
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
        "CreateFence");

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
        Check(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");
}

void Engine::LoadTextures() {
    // 업로드는 초기화 때 한 번에 한다. 커맨드 리스트를 열어 텍스처마다 복사와
    // 배리어를 기록하고, 제출한 뒤 GPU가 끝낼 때까지 기다린다.
    FrameResource &frame = m_frames[m_frameIndex];
    Check(frame.commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
    Check(m_commandList->Reset(frame.commandAllocator.Get(), nullptr),
          "ID3D12GraphicsCommandList::Reset");

    // GPU가 복사를 끝낼 때까지 업로드 버퍼를 살려 두는 목록이다.
    std::vector<ComPtr<ID3D12Resource>> uploadBuffers;
    const std::filesystem::path folder =
        ExecutableDirectory() / "assets" / "textures";
    m_environmentMap =
        Texture::Load(m_device.Get(), m_commandList.Get(),
                      folder / "kloofendal_specular.dds", uploadBuffers);
    m_irradianceMap =
        Texture::Load(m_device.Get(), m_commandList.Get(),
                      folder / "kloofendal_diffuse.dds", uploadBuffers);
    m_groundTexture =
        Texture::Load(m_device.Get(), m_commandList.Get(),
                      folder / "cobblestone_albedo.png", uploadBuffers);

    Check(m_commandList->Close(), "Close");
    ID3D12CommandList *commandLists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, commandLists);

    // ExecuteCommandLists가 돌아온 시점에 GPU는 아직 복사 중일 수 있다. 끝날
    // 때까지 기다린 뒤에야 업로드 버퍼를 놓아도 된다.
    WaitForGpu();
}

void Engine::CreateTextureViews() {
    // 셰이더가 실행 중에 읽는 칸이라 SHADER_VISIBLE 힙이어야 한다. RTV, DSV
    // 힙과 달리 GPU 핸들로도 가리킬 수 있다.
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    // 마지막 칸(kGuiFontSlot)은 ImGui가 폰트 SRV를 쓴다.
    heapDesc.NumDescriptors = kSrvHeapSize;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)),
          "CreateDescriptorHeap");

    // 테이블이 t1부터 연속 3칸을 읽으므로 레지스터 순서대로 칸을 채운다.
    m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    const Texture *textures[kTextureCount] = {
        &m_irradianceMap,  // t1
        &m_environmentMap, // t2
        &m_groundTexture,  // t3
    };
    for (const Texture *texture : textures) {
        texture->CreateShaderResourceView(m_device.Get(), handle);
        handle.ptr += m_srvDescriptorSize;
    }
}

void Engine::InitializeGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // 창 위치를 imgui.ini로 남기지 않는다.
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(m_window);

    // 폰트 텍스처의 생성, 업로드, SRV 쓰기는 백엔드가 한다. 엔진은 SRV를 쓸 힙
    // 칸만 정해 준다. 백엔드는 그릴 때 SetDescriptorHeaps를 하지 않으므로,
    // 이 칸은 Render()가 프레임마다 묶는 m_srvHeap 안에 있어야 한다.
    ImGui_ImplDX12_InitInfo info;
    info.Device = m_device.Get();
    info.CommandQueue = m_commandQueue.Get();
    // 백엔드는 정점, 인덱스 버퍼를 이 수만큼 돌려 쓴다. 엔진이 같은 칸의 펜스
    // 값을 기다린 뒤 기록하므로 프레임 수를 맞추면 GPU가 읽는 버퍼를 덮어쓰지
    // 않는다.
    info.NumFramesInFlight = kFrameCount;
    info.RTVFormat = kBackBufferFormat;
    // 깊이 버퍼가 묶인 채로 그리므로 PSO의 DSV 형식을 맞춘다. 깊이 테스트는
    // 백엔드 PSO가 끈다.
    info.DSVFormat = kDepthFormat;
    info.UserData = this;
    info.SrvDescriptorHeap = m_srvHeap.Get();
    // 1.92 백엔드는 폰트 아틀라스를 첫 RenderDrawData에서 만들고, 아틀라스가
    // 커지면 새 텍스처의 칸을 먼저 받은 뒤 옛 텍스처를 GPU가 다 쓰고 나서
    // 돌려준다. 폰트 칸은 하나뿐이라, 쓰는 중인 칸을 또 달라고 하면 덮어쓰지
    // 않고 멈춘다. 덮어쓰면 진행 중인 프레임이 읽는 칸이 바뀐다.
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *info,
                                   D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle,
                                   D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle) {
        Engine *engine = static_cast<Engine *>(info->UserData);
        if (engine->m_guiFontSlotUsed)
            throw std::runtime_error(
                "ImGui asked for a second SRV slot; only one is reserved");
        engine->m_guiFontSlotUsed = true;
        const UINT offset = kGuiFontSlot * engine->m_srvDescriptorSize;
        *cpuHandle = engine->m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        cpuHandle->ptr += offset;
        *gpuHandle = engine->m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        gpuHandle->ptr += offset;
    };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *info,
                                  D3D12_CPU_DESCRIPTOR_HANDLE,
                                  D3D12_GPU_DESCRIPTOR_HANDLE) {
        static_cast<Engine *>(info->UserData)->m_guiFontSlotUsed = false;
    };
    if (!ImGui_ImplDX12_Init(&info))
        throw std::runtime_error("ImGui_ImplDX12_Init failed");
    m_guiInitialized = true;
}

void Engine::ShutdownGui() {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_guiInitialized = false;
}

void Engine::BuildGui() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::Text("%.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);

    // 멈춘 시각을 바꿔 가며 정답 화면과 같은 장면을 찾는다. 다시 흐르게 하면
    // 멈춘 시각에서 이어 간다.
    bool frozen = m_frozenTime.has_value();
    float time = SceneTime();
    if (ImGui::Checkbox("Freeze time", &frozen)) {
        if (frozen) {
            m_frozenTime = time;
        } else {
            m_frozenTime.reset();
            m_startTime =
                std::chrono::steady_clock::now() -
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<float>(time));
        }
    }
    if (m_frozenTime)
        ImGui::SliderFloat("Time (s)", &*m_frozenTime, 0.0f, 30.0f, "%.2f");
    else
        ImGui::Text("Time %.2f s", time);
    ImGui::End();

    ImGui::Render();
}

float Engine::SceneTime() const {
    if (m_frozenTime)
        return *m_frozenTime;
    return std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                        m_startTime)
        .count();
}

void Engine::Transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                        D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    m_commandList->ResourceBarrier(1, &barrier);
}

void Engine::Render() {
    // UI는 CPU 쪽 일이라 펜스 대기와 상관없다. 체크박스가 장면 시각을 바꾸므로
    // 장면을 기록하기 전에 만든다.
    BuildGui();

    // 이 칸은 kFrameCount 프레임 전에 마지막으로 제출됐다. GPU가 그 제출을
    // 지나간 뒤에야 할당자와 상수 버퍼를 다시 쓸 수 있다.
    FrameResource &frame = m_frames[m_frameIndex];
    WaitForFenceValue(frame.fenceValue);
    frame.ResetConstants();

    Check(frame.commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
    Check(
        m_commandList->Reset(frame.commandAllocator.Get(), m_skyPipeline.Get()),
        "ID3D12GraphicsCommandList::Reset");

    ID3D12Resource *backBuffer = m_renderTargets[m_backBufferIndex].Get();
    Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT,
               D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += SIZE_T(m_backBufferIndex) * m_rtvDescriptorSize;

    const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    // 깊이는 가장 먼 값(1.0)으로 지워서, 처음 그리는 픽셀은 항상 통과하게 한다.
    const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0,
                                         0, nullptr);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight),
        0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(kWidth),
                                static_cast<LONG>(kHeight)};
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // 텍스처 테이블은 모든 드로우가 같으므로 프레임마다 한 번만 설정한다.
    // 테이블은 묶인 셰이더 가시 힙 안의 칸을 가리키므로 힙을 먼저 묶는다.
    ID3D12DescriptorHeap *heaps[] = {m_srvHeap.Get()};
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->SetGraphicsRootDescriptorTable(
        kRootTextures, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 카메라는 Part 2 레이 씬처럼 바닥 중심 둘레를 돈다.
    const float time = SceneTime();
    const float angle = 0.6f * time;
    const XMVECTOR eye = XMVectorSet(6.0f * std::sin(angle), 2.0f,
                                     5.0f - 6.0f * std::cos(angle), 1.0f);
    const XMMATRIX view =
        XMMatrixLookAtLH(eye, XMVectorSet(0.0f, 0.8f, 5.0f, 1.0f),
                         XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    const XMMATRIX projection =
        XMMatrixPerspectiveFovLH(2.0f * std::atan(0.5f / 0.8f),
                                 float(kWidth) / float(kHeight), 0.01f, 100.0f);

    // 장면 데이터(b1)는 모든 드로우가 같으므로 프레임마다 한 번 쓰고 한 번
    // 연결한다. 루트 인자는 다시 설정할 때까지 이후 드로우에 남는다.
    SceneConstants scene = {};
    XMStoreFloat3(&scene.eyeWorld, eye);
    scene.groundHalfExtent = kGroundHalfExtent;
    scene.groundCenter = kGroundCenter;
    // 해는 Part 2처럼 아래를 향한 채 수평으로 돈다.
    const float lightAngle = 0.7f * time;
    XMStoreFloat3(&scene.lightDirection,
                  XMVector3Normalize(XMVectorSet(std::sin(lightAngle), -1.0f,
                                                 std::cos(lightAngle), 0.0f)));
    for (int i = 0; i < kSphereCount; ++i)
        scene.spheres[i] = kSpheres[i];
    m_commandList->SetGraphicsRootConstantBufferView(
        kRootSceneConstants, frame.WriteConstants(&scene, sizeof(scene)));

    // 하늘: 맨 먼저 깊이 없이 그린다. 커맨드 리스트를 Reset할 때 스카이 PSO로
    // 시작했다. 뷰에서 이동을 빼서 하늘 구가 늘 카메라 중심에 있게 하면,
    // 카메라가 움직여도 하늘은 무한히 먼 것처럼 따라온다. 반지름 1 구를 10배로
    // 키워 near(0.01)와 far(100) 사이에 둔다.
    XMMATRIX skyView = view;
    skyView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    DrawMesh(frame, m_sphere,
             MakeMeshConstants(XMMatrixScaling(10.0f, 10.0f, 10.0f), skyView,
                               projection, kNoObjectIndex));

    // 구 6개를 먼저, 바닥을 마지막에 그린다. 깊이 테스트가 있어서 그리는
    // 순서와 상관없이 카메라에 가까운 것이 남는다. 하늘은 깊이를 쓰지
    // 않았으므로 깊이 버퍼는 클리어 값 그대로이고, 이 메시들은 하늘 위에
    // 덮인다.
    m_commandList->SetPipelineState(m_meshPipeline.Get());
    // 드로우마다 b0에 물체 번호를 함께 넣어, 레이 PS가 b1에서 자기 재질을
    // 고르게 한다.
    for (int i = 0; i < kSphereCount; ++i) {
        const Sphere &sphere = kSpheres[i];
        const XMMATRIX model =
            XMMatrixScaling(sphere.radius, sphere.radius, sphere.radius) *
            XMMatrixTranslation(sphere.center.x, sphere.center.y,
                                sphere.center.z);
        DrawMesh(frame, m_sphere,
                 MakeMeshConstants(model, view, projection, i));
    }

    // 정사각형은 -z를 향하므로 x축으로 90도 돌려 위를 향하게 눕힌다.
    const XMMATRIX groundModel =
        XMMatrixScaling(kGroundHalfExtent, kGroundHalfExtent,
                        kGroundHalfExtent) *
        XMMatrixRotationX(XM_PIDIV2) *
        XMMatrixTranslation(kGroundCenter.x, kGroundCenter.y, kGroundCenter.z);
    DrawMesh(frame, m_ground,
             MakeMeshConstants(groundModel, view, projection, kGroundIndex));

    // UI는 장면 위에 마지막으로 그린다. 백엔드는 자기 PSO, 루트 시그니처,
    // 테이블을 묶으므로 이 뒤에 장면을 더 그리려면 엔진 것을 다시 묶어야 한다.
    // 힙은 위에서 묶은 m_srvHeap을 그대로 쓴다.
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

    Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_PRESENT);
    Check(m_commandList->Close(), "Close");

    ID3D12CommandList *commandLists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, commandLists);

    Check(m_swapChain->Present(1, 0), "Present");

    // 이 프레임을 제출한 번호를 기억해 두고, 기다리지 않고 넘어간다.
    frame.fenceValue = Signal();
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    m_frameIndex = (m_frameIndex + 1) % kFrameCount;
}

void Engine::DrawMesh(FrameResource &frame, const Mesh &mesh,
                      const MeshConstants &constants) {
    // 상수를 버퍼에 쓰는 일은 프레임 자원이, 그 주소를 b0에 연결하는 일은 루트
    // 시그니처를 가진 엔진이, 그리기는 메시가 한다.
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        frame.WriteConstants(&constants, sizeof(constants));
    m_commandList->SetGraphicsRootConstantBufferView(kRootMeshConstants,
                                                     address);
    mesh.Draw(m_commandList.Get());
}

// 지금까지 제출한 모든 작업 뒤에 펜스 쓰기를 큐에 넣고, GPU가 거기에 도달했을
// 때 펜스가 갖게 될 값을 돌려준다.
UINT64 Engine::Signal() {
    const UINT64 value = ++m_fenceCounter;
    Check(m_commandQueue->Signal(m_fence.Get(), value), "Signal");
    return value;
}

void Engine::WaitForFenceValue(UINT64 value) {
    if (m_fence->GetCompletedValue() >= value)
        return;

    Check(m_fence->SetEventOnCompletion(value, m_fenceEvent),
          "SetEventOnCompletion");
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

// 지금까지 제출한 모든 작업을 GPU가 끝낼 때까지 기다린다.
void Engine::WaitForGpu() { WaitForFenceValue(Signal()); }
