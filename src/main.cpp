// Minimal Direct3D 12 program: one window, one triangle, nothing else.
//
// Read it top to bottom. Initialize() shows the order objects are created in,
// and Render() shows everything the GPU is asked to do in one frame.

#include <windows.h>

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <climits>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kWidth = 1280;
constexpr UINT kHeight = 720;
constexpr UINT kBackBufferCount = 2;
constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

#ifdef _DEBUG
constexpr bool kEnableDebugLayer = true;
#else
constexpr bool kEnableDebugLayer = false;
#endif

struct Vertex {
    float position[3];
    float color[3];
};

void Check(HRESULT hr, const char *action) {
    if (SUCCEEDED(hr))
        return;

    std::ostringstream message;
    message << action << " failed (HRESULT 0x" << std::hex
            << static_cast<unsigned long>(hr) << ")";
    throw std::runtime_error(message.str());
}

// Shaders are compiled by the build and copied next to the executable, so the
// lookup does not depend on the working directory.
std::vector<char> LoadCompiledShader(const char *fileName) {
    wchar_t executablePath[MAX_PATH];
    GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    const std::filesystem::path path =
        std::filesystem::path(executablePath).parent_path() / fileName;

    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open shader: " + path.string());

    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}

// Debug layer messages normally reach only a debugger. Printing them keeps
// validation errors visible in the console as well.
void CALLBACK OnD3D12Message(D3D12_MESSAGE_CATEGORY,
                             D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID,
                             LPCSTR description, void *) {
    if (severity > D3D12_MESSAGE_SEVERITY_WARNING)
        return;

    const char *label =
        severity == D3D12_MESSAGE_SEVERITY_WARNING ? "WARNING" : "ERROR";
    std::cerr << "[D3D12 " << label << "] " << description << std::endl;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam,
                            LPARAM lParam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

class HelloTriangleApp {
  public:
    ~HelloTriangleApp() {
        // The GPU may still be using the resources this object is about to
        // release.
        if (m_fenceEvent) {
            try {
                WaitForGpu();
            } catch (const std::exception &error) {
                std::cerr << error.what() << std::endl;
            }
            CloseHandle(m_fenceEvent);
        }
    }

    void Initialize() {
        InitWindow();
        CreateDevice();
        CreateCommandObjects();
        CreateSwapChain();
        CreateRenderTargets();
        CreatePipeline();
        CreateVertexBuffer();
        CreateFence();
    }

    int Run() {
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

  private:
    void InitWindow() {
        const HINSTANCE instance = GetModuleHandle(nullptr);
        const wchar_t *className = L"HelloDX12Window";

        WNDCLASSEX windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.lpszClassName = className;
        if (!RegisterClassEx(&windowClass))
            Check(HRESULT_FROM_WIN32(GetLastError()), "RegisterClassEx");

        // The swap chain is never resized, so the window size stays fixed.
        const DWORD style =
            WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        RECT rect = {0, 0, static_cast<LONG>(kWidth),
                     static_cast<LONG>(kHeight)};
        AdjustWindowRect(&rect, style, FALSE);

        m_window = CreateWindowEx(
            0, className, L"Hello DX12", style, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
            instance, nullptr);
        if (!m_window)
            Check(HRESULT_FROM_WIN32(GetLastError()), "CreateWindowEx");
    }

    void CreateDevice() {
        UINT factoryFlags = 0;
        if (kEnableDebugLayer) {
            // This has to happen before the device is created.
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

        // A machine can expose several adapters, so ask for the fastest first.
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

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(),
                                            D3D_FEATURE_LEVEL_11_0,
                                            IID_PPV_ARGS(&m_device)))) {
                std::wcout << L"Adapter: " << description.Description
                           << std::endl;
                break;
            }
        }
        if (!m_device)
            throw std::runtime_error("No Direct3D 12 hardware adapter found.");

        if (kEnableDebugLayer) {
            ComPtr<ID3D12InfoQueue1> infoQueue;
            if (SUCCEEDED(m_device.As(&infoQueue))) {
                DWORD cookie = 0;
                infoQueue->RegisterMessageCallback(
                    OnD3D12Message, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr,
                    &cookie);
            }
        }
    }

    void CreateCommandObjects() {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(m_device->CreateCommandQueue(&queueDesc,
                                           IID_PPV_ARGS(&m_commandQueue)),
              "CreateCommandQueue");

        Check(
            m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&m_commandAllocator)),
            "CreateCommandAllocator");

        Check(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          m_commandAllocator.Get(), nullptr,
                                          IID_PPV_ARGS(&m_commandList)),
              "CreateCommandList");

        // A command list starts open, but Render() expects to Reset() it.
        Check(m_commandList->Close(), "Close");
    }

    void CreateSwapChain() {
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = kWidth;
        desc.Height = kHeight;
        desc.Format = kBackBufferFormat;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = kBackBufferCount;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        // Direct3D 12 swap chains present through the queue, not the device.
        ComPtr<IDXGISwapChain1> swapChain;
        Check(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_window,
                                                &desc, nullptr, nullptr,
                                                &swapChain),
              "CreateSwapChainForHwnd");

        // Alt+Enter would need the resize path this program does not have.
        Check(m_factory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER),
              "MakeWindowAssociation");

        Check(swapChain.As(&m_swapChain), "IDXGISwapChain3");
        m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    }

    void CreateRenderTargets() {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = kBackBufferCount;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(
            m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap)),
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

    void CreatePipeline() {
        // The triangle uses no constants or textures, so the root signature
        // only has to allow the input assembler.
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
             offsetof(Vertex, position),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
             offsetof(Vertex, color),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        // Every fixed-function state lives in this one immutable object.
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

        // Blending is off, but these are the values Direct3D treats as opaque.
        D3D12_RENDER_TARGET_BLEND_DESC &blend = desc.BlendState.RenderTarget[0];
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ZERO;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        Check(m_device->CreateGraphicsPipelineState(
                  &desc, IID_PPV_ARGS(&m_pipelineState)),
              "CreateGraphicsPipelineState");
    }

    void CreateVertexBuffer() {
        // Direct3D treats clockwise triangles as front-facing by default.
        const Vertex vertices[] = {
            {{0.0f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
            {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        };

        // An upload heap is CPU-writable and GPU-readable, which avoids a copy
        // pass for these three vertices.
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = sizeof(vertices);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Check(m_device->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &desc,
                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                  IID_PPV_ARGS(&m_vertexBuffer)),
              "CreateCommittedResource");

        void *mapped = nullptr;
        const D3D12_RANGE noRead = {0, 0};
        Check(m_vertexBuffer->Map(0, &noRead, &mapped), "Map");
        std::memcpy(mapped, vertices, sizeof(vertices));
        m_vertexBuffer->Unmap(0, nullptr);

        m_vertexBufferView.BufferLocation =
            m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = sizeof(vertices);
    }

    void CreateFence() {
        Check(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                    IID_PPV_ARGS(&m_fence)),
              "CreateFence");

        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!m_fenceEvent)
            Check(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent");
    }

    void Transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        m_commandList->ResourceBarrier(1, &barrier);
    }

    void Render() {
        // Safe because WaitForGpu() ran at the end of the previous frame.
        Check(m_commandAllocator->Reset(), "ID3D12CommandAllocator::Reset");
        Check(m_commandList->Reset(m_commandAllocator.Get(),
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
        m_commandList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
        m_commandList->DrawInstanced(3, 1, 0, 0);

        Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
                   D3D12_RESOURCE_STATE_PRESENT);
        Check(m_commandList->Close(), "Close");

        ID3D12CommandList *commandLists[] = {m_commandList.Get()};
        m_commandQueue->ExecuteCommandLists(1, commandLists);

        Check(m_swapChain->Present(1, 0), "Present");

        WaitForGpu();
        m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    }

    // Only one frame is ever in flight: the CPU waits here until the GPU has
    // finished everything submitted so far.
    void WaitForGpu() {
        const UINT64 value = ++m_fenceValue;
        Check(m_commandQueue->Signal(m_fence.Get(), value), "Signal");

        if (m_fence->GetCompletedValue() < value) {
            Check(m_fence->SetEventOnCompletion(value, m_fenceEvent),
                  "SetEventOnCompletion");
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

    HWND m_window = nullptr;

    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;

    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12Resource> m_renderTargets[kBackBufferCount];
    UINT m_rtvDescriptorSize = 0;
    UINT m_backBufferIndex = 0;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};

    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0;
    HANDLE m_fenceEvent = nullptr;
};

} // namespace

int main() {
    try {
        HelloTriangleApp app;
        app.Initialize();
        return app.Run();
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << std::endl;
        return -1;
    }
}
