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

#include "Geometry.h"
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
                                CXMMATRIX projection) {
    MeshConstants constants;
    XMStoreFloat4x4(&constants.model, XMMatrixTranspose(model));
    XMStoreFloat4x4(&constants.invTranspose, XMMatrixInverse(nullptr, model));
    XMStoreFloat4x4(&constants.view, XMMatrixTranspose(view));
    XMStoreFloat4x4(&constants.projection, XMMatrixTranspose(projection));
    return constants;
}

static_assert(sizeof(MeshConstants) <= FrameResource::kConstantSlotSize,
              "MeshConstants must fit in one constant buffer slot.");

// Part 2 레이 씬의 구 배치다. 색, 반사, 굴절 값은 레이 PS를 넣을 때 더한다.
struct SphereDesc {
    XMFLOAT3 center;
    float radius;
};

const SphereDesc kSpheres[] = {
    {{0.0f, 0.8f, 2.0f}, 0.8f},  {{1.0f, 1.1f, 6.0f}, 1.1f},
    {{-2.0f, 0.5f, 3.0f}, 0.5f}, {{1.5f, 0.8f, 3.0f}, 0.5f},
    {{-0.8f, 1.0f, 4.0f}, 1.0f}, {{-2.0f, 1.0f, 7.0f}, 1.0f},
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
    // GPU가 이전 프레임의 할당자와 상수 칸을 아직 쓰고 있을 수 있으므로
    // 프레임마다 따로 둔다.
    for (FrameResource &frame : m_frames)
        frame.Create(m_device.Get(), kMaxDrawsPerFrame);
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
    D3D12_ROOT_PARAMETER rootParameters[2] = {};

    // 0: VS의 b0. 드로우마다 바뀌므로, 디스크립터를 거치지 않고 GPU 주소를
    // 바로 넘기는 루트 CBV로 둔다.
    D3D12_ROOT_PARAMETER &meshConstants = rootParameters[kRootMeshConstants];
    meshConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    meshConstants.Descriptor.ShaderRegister = 0; // b0
    meshConstants.Descriptor.RegisterSpace = 0;
    meshConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

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

    const std::vector<char> vertexShader =
        LoadCompiledShader("MeshVertexShader.cso");
    const std::vector<char> pixelShader =
        LoadCompiledShader("TexturePixelShader.cso");

    const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, texcoord),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // 고정 기능 상태는 전부 이 불변 객체 하나에 들어간다.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = m_rootSignature.Get();
    desc.VS = {vertexShader.data(), vertexShader.size()};
    desc.PS = {pixelShader.data(), pixelShader.size()};
    desc.InputLayout = {inputLayout, _countof(inputLayout)};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = kBackBufferFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;

    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    desc.RasterizerState.DepthClipEnable = TRUE;

    // 이미 그린 것보다 가까운 픽셀만 남기고, 남긴 픽셀의 깊이를 기록한다.
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.DSVFormat = kDepthFormat;

    // 블렌딩은 꺼져 있지만, Direct3D가 불투명으로 취급하는 값을 채운다.
    D3D12_RENDER_TARGET_BLEND_DESC &blend = desc.BlendState.RenderTarget[0];
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_ZERO;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    Check(m_device->CreateGraphicsPipelineState(&desc,
                                                IID_PPV_ARGS(&m_pipelineState)),
          "CreateGraphicsPipelineState");
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
    heapDesc.NumDescriptors = kTextureCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)),
          "CreateDescriptorHeap");

    // 테이블이 t1부터 연속 3칸을 읽으므로 레지스터 순서대로 칸을 채운다.
    const UINT descriptorSize = m_device->GetDescriptorHandleIncrementSize(
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
        handle.ptr += descriptorSize;
    }
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
    // 이 칸은 kFrameCount 프레임 전에 마지막으로 제출됐다. GPU가 그 제출을
    // 지나간 뒤에야 할당자를 다시 쓸 수 있다.
    FrameResource &frame = m_frames[m_frameIndex];
    WaitForFenceValue(frame.fenceValue);

    Check(frame.commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
    Check(m_commandList->Reset(frame.commandAllocator.Get(),
                               m_pipelineState.Get()),
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
    const float time = std::chrono::duration<float>(
                           std::chrono::steady_clock::now() - m_startTime)
                           .count();
    const float angle = 0.6f * time;
    const XMVECTOR eye = XMVectorSet(6.0f * std::sin(angle), 2.0f,
                                     5.0f - 6.0f * std::cos(angle), 1.0f);
    const XMMATRIX view =
        XMMatrixLookAtLH(eye, XMVectorSet(0.0f, 0.8f, 5.0f, 1.0f),
                         XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    const XMMATRIX projection =
        XMMatrixPerspectiveFovLH(2.0f * std::atan(0.5f / 0.8f),
                                 float(kWidth) / float(kHeight), 0.01f, 100.0f);

    // 구 6개를 먼저, 바닥을 마지막에 그린다. 깊이 테스트가 있어서 그리는
    // 순서와 상관없이 카메라에 가까운 것이 남는다.
    UINT drawIndex = 0;
    for (const SphereDesc &sphere : kSpheres) {
        const XMMATRIX model =
            XMMatrixScaling(sphere.radius, sphere.radius, sphere.radius) *
            XMMatrixTranslation(sphere.center.x, sphere.center.y,
                                sphere.center.z);
        DrawMesh(frame, drawIndex++, m_sphere,
                 MakeMeshConstants(model, view, projection));
    }

    // 정사각형은 -z를 향하므로 x축으로 90도 돌려 위를 향하게 눕힌다.
    const XMMATRIX groundModel =
        XMMatrixScaling(kGroundHalfExtent, kGroundHalfExtent,
                        kGroundHalfExtent) *
        XMMatrixRotationX(XM_PIDIV2) *
        XMMatrixTranslation(kGroundCenter.x, kGroundCenter.y, kGroundCenter.z);
    DrawMesh(frame, drawIndex++, m_ground,
             MakeMeshConstants(groundModel, view, projection));

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

void Engine::DrawMesh(FrameResource &frame, UINT drawIndex, const Mesh &mesh,
                      const MeshConstants &constants) {
    // 칸에 쓰는 일은 프레임 자원이, 그 주소를 b0에 연결하는 일은 루트
    // 시그니처를 가진 엔진이, 그리기는 메시가 한다.
    const D3D12_GPU_VIRTUAL_ADDRESS address =
        frame.WriteConstants(drawIndex, &constants, sizeof(constants));
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
