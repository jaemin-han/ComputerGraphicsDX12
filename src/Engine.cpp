// 위에서 아래로 읽는다. Initialize()는 객체를 만드는 순서를, Render()는 한
// 프레임 동안 GPU에 시키는 일 전체를 보여 준다. 프레임 2개를 동시에 진행해
// GPU가 이전 프레임을 실행하는 동안 CPU가 다음 프레임을 기록한다.

#include "Engine.h"

#include <d3d12sdklayers.h>

#include <climits>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>

#include "Util.h"
#include "Window.h"

using namespace DirectX;

namespace {

constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

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

void CopyToBuffer(ID3D12Resource *buffer, const void *data, size_t size) {
    void *mapped = nullptr;
    const D3D12_RANGE noRead = {0, 0};
    Check(buffer->Map(0, &noRead, &mapped), "Map");
    std::memcpy(mapped, data, size);
    buffer->Unmap(0, nullptr);
}

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
    m_window = CreateAppWindow(kWidth, kHeight, L"Hello DX12");
    CreateDevice();
    CreateCommandObjects();
    CreateSwapChain();
    CreateRenderTargets();
    CreatePipeline();
    CreateConstantBuffers();

    // Direct3D는 기본적으로 시계 방향 삼각형을 앞면으로 본다. 인덱스도 그
    // 순서로 정점을 가리킨다.
    MeshData triangle;
    triangle.vertices = {
        {{0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    };
    triangle.indices = {0, 1, 2};
    m_triangle = CreateMesh(triangle);

    CreateFence();
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

void Engine::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(
        m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)),
        "CreateCommandQueue");

    for (FrameResource &frame : m_frames) {
        Check(m_device->CreateCommandAllocator(
                  D3D12_COMMAND_LIST_TYPE_DIRECT,
                  IID_PPV_ARGS(&frame.commandAllocator)),
              "CreateCommandAllocator");
    }

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

void Engine::CreatePipeline() {
    // 루트 시그니처는 셰이더가 받는 인자 목록이다. 지금은 VS의 b0 하나다.
    // b0는 드로우마다 바뀌므로, 디스크립터를 거치지 않고 GPU 주소를 바로
    // 넘기는 루트 CBV로 둔다.
    D3D12_ROOT_PARAMETER rootParameters[1] = {};
    D3D12_ROOT_PARAMETER &meshConstants = rootParameters[kRootMeshConstants];
    meshConstants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    meshConstants.Descriptor.ShaderRegister = 0; // b0
    meshConstants.Descriptor.RegisterSpace = 0;
    meshConstants.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParameters);
    rootDesc.pParameters = rootParameters;
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
        LoadCompiledShader("TriangleVertexShader.cso");
    const std::vector<char> pixelShader =
        LoadCompiledShader("TrianglePixelShader.cso");

    const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, color),
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

void Engine::CreateConstantBuffers() {
    static_assert(sizeof(MeshConstants) <= kConstantSlotSize,
                  "MeshConstants must fit in one constant buffer slot.");

    // GPU가 이전 프레임의 칸을 아직 읽고 있을 수 있으므로 프레임마다 따로 둔다.
    for (FrameResource &frame : m_frames) {
        frame.constantBuffer =
            CreateUploadBuffer(UINT64(kConstantSlotSize) * kMaxDrawsPerFrame);

        // 업로드 힙은 Map한 채로 둬도 된다. 프로그램이 끝날 때까지 이 주소로
        // 쓴다.
        void *mapped = nullptr;
        const D3D12_RANGE noRead = {0, 0};
        Check(frame.constantBuffer->Map(0, &noRead, &mapped), "Map");
        frame.mappedConstants = static_cast<BYTE *>(mapped);
    }
}

void Engine::CreateFence() {
    Check(
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
        "CreateFence");

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
        Check(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");
}

// 업로드 힙은 CPU가 쓰고 GPU가 읽을 수 있어서, 작은 데이터는 복사 패스 없이
// 여기서 바로 읽힌다.
ComPtr<ID3D12Resource> Engine::CreateUploadBuffer(UINT64 size) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> buffer;
    Check(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ,
                                            nullptr, IID_PPV_ARGS(&buffer)),
          "CreateCommittedResource");
    return buffer;
}

Mesh Engine::CreateMesh(const MeshData &data) {
    const UINT vertexBytes =
        static_cast<UINT>(sizeof(Vertex) * data.vertices.size());
    const UINT indexBytes =
        static_cast<UINT>(sizeof(uint32_t) * data.indices.size());

    // 정점 버퍼와 인덱스 버퍼는 같은 방식으로 업로드 힙에 올린다. 차이는 뷰가
    // 알려 주는 해석뿐이다.
    Mesh mesh;
    mesh.vertexBuffer = CreateUploadBuffer(vertexBytes);
    mesh.indexBuffer = CreateUploadBuffer(indexBytes);
    CopyToBuffer(mesh.vertexBuffer.Get(), data.vertices.data(), vertexBytes);
    CopyToBuffer(mesh.indexBuffer.Get(), data.indices.data(), indexBytes);

    // 정점 버퍼는 정점 하나의 크기(Stride)로 잘라 읽는다.
    mesh.vertexBufferView.BufferLocation =
        mesh.vertexBuffer->GetGPUVirtualAddress();
    mesh.vertexBufferView.StrideInBytes = sizeof(Vertex);
    mesh.vertexBufferView.SizeInBytes = vertexBytes;

    // 인덱스 버퍼는 형식(32비트 정수)으로 읽는다.
    mesh.indexBufferView.BufferLocation =
        mesh.indexBuffer->GetGPUVirtualAddress();
    mesh.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    mesh.indexBufferView.SizeInBytes = indexBytes;

    mesh.indexCount = static_cast<UINT>(data.indices.size());
    return mesh;
}

void Engine::SetMeshConstants(FrameResource &frame, UINT drawIndex,
                              const MeshConstants &constants) {
    if (drawIndex >= kMaxDrawsPerFrame)
        throw std::runtime_error("Too many draws for the constant buffer.");

    // drawIndex번 칸에 쓰고, 다음 드로우가 b0를 그 칸에서 읽도록 주소를
    // 기록한다. GPU는 이 주소를 커맨드 리스트를 실행할 때 읽는다.
    const UINT64 offset = UINT64(drawIndex) * kConstantSlotSize;
    std::memcpy(frame.mappedConstants + offset, &constants, sizeof(constants));
    m_commandList->SetGraphicsRootConstantBufferView(
        kRootMeshConstants,
        frame.constantBuffer->GetGPUVirtualAddress() + offset);
}

void Engine::DrawMesh(const Mesh &mesh) {
    m_commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
    m_commandList->IASetIndexBuffer(&mesh.indexBufferView);
    m_commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
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

    const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    const D3D12_VIEWPORT viewport = {
        0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight),
        0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(kWidth),
                                static_cast<LONG>(kHeight)};
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 같은 삼각형을 b0만 바꿔 세 번 그린다. 드로우마다 상수 칸이 달라야 GPU가
    // 실행할 때 각자 자기 위치를 읽는다.
    const float offsetsX[kMaxDrawsPerFrame] = {-0.6f, 0.0f, 0.6f};
    for (UINT i = 0; i < kMaxDrawsPerFrame; ++i) {
        const XMMATRIX model = XMMatrixScaling(0.5f, 0.5f, 1.0f) *
                               XMMatrixTranslation(offsetsX[i], 0.0f, 0.0f);
        SetMeshConstants(
            frame, i,
            MakeMeshConstants(model, XMMatrixIdentity(), XMMatrixIdentity()));
        DrawMesh(m_triangle);
    }

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
