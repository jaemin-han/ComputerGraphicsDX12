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

namespace {

constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

#ifdef _DEBUG
constexpr bool kEnableDebugLayer = true;
#else
constexpr bool kEnableDebugLayer = false;
#endif

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

    // Direct3D는 기본적으로 시계 방향 삼각형을 앞면으로 본다.
    m_triangle = CreateMesh({
        {{0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    });

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
    // 삼각형은 상수도 텍스처도 쓰지 않으므로 루트 시그니처는 입력 어셈블러만
    // 허용하면 된다.
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
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

void Engine::CreateFence() {
    Check(
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)),
        "CreateFence");

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
        Check(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");
}

Mesh Engine::CreateMesh(const std::vector<Vertex> &vertices) {
    const UINT size = static_cast<UINT>(sizeof(Vertex) * vertices.size());

    // 업로드 힙은 CPU가 쓰고 GPU가 읽을 수 있어서, 작은 정점 데이터를 위해
    // 복사 패스를 따로 둘 필요가 없다.
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

    Mesh mesh;
    Check(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                            D3D12_RESOURCE_STATE_GENERIC_READ,
                                            nullptr,
                                            IID_PPV_ARGS(&mesh.vertexBuffer)),
          "CreateCommittedResource");

    void *mapped = nullptr;
    const D3D12_RANGE noRead = {0, 0};
    Check(mesh.vertexBuffer->Map(0, &noRead, &mapped), "Map");
    std::memcpy(mapped, vertices.data(), size);
    mesh.vertexBuffer->Unmap(0, nullptr);

    mesh.vertexBufferView.BufferLocation =
        mesh.vertexBuffer->GetGPUVirtualAddress();
    mesh.vertexBufferView.StrideInBytes = sizeof(Vertex);
    mesh.vertexBufferView.SizeInBytes = size;
    mesh.vertexCount = static_cast<UINT>(vertices.size());
    return mesh;
}

void Engine::DrawMesh(const Mesh &mesh) {
    m_commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
    m_commandList->DrawInstanced(mesh.vertexCount, 1, 0, 0);
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
    DrawMesh(m_triangle);

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
