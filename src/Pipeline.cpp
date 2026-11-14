#include "Pipeline.h"

#include <climits>
#include <cstddef>
#include <vector>

#include "Mesh.h"
#include "Util.h"

Microsoft::WRL::ComPtr<ID3D12PipelineState>
CreateGraphicsPipeline(ID3D12Device *device, ID3D12RootSignature *rootSignature,
                       DXGI_FORMAT renderTargetFormat, DXGI_FORMAT depthFormat,
                       const PipelineSettings &settings) {
    const std::vector<char> vertexShader =
        LoadCompiledShader(settings.vertexShader);
    const std::vector<char> pixelShader =
        LoadCompiledShader(settings.pixelShader);

    const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
         0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, texcoord),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = rootSignature;
    desc.VS = {vertexShader.data(), vertexShader.size()};
    desc.PS = {pixelShader.data(), pixelShader.size()};
    desc.InputLayout = {inputLayout, _countof(inputLayout)};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = renderTargetFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = UINT_MAX;

    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = settings.cullMode;
    desc.RasterizerState.DepthClipEnable = TRUE;

    // 깊이를 켜면 이미 그린 것보다 가까운 픽셀만 남기고 그 깊이를 기록한다.
    // 깊이를 꺼도 같은 DSV가 묶여 있으므로 형식은 맞춰 둔다.
    desc.DepthStencilState.DepthEnable = settings.depthEnable ? TRUE : FALSE;
    desc.DepthStencilState.DepthWriteMask = settings.depthEnable
                                                ? D3D12_DEPTH_WRITE_MASK_ALL
                                                : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.DSVFormat = depthFormat;

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

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    Check(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)),
          "CreateGraphicsPipelineState");
    return pipeline;
}
