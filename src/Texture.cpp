// 파일 해석은 DirectXTex에 맡기고, GPU로 올리는 과정(텍스처 생성, 업로드 버퍼,
// 복사, 배리어)은 직접 기록한다.

#include "Texture.h"

// DirectXTex.h는 먼저 포함된 d3d12.h를 보고 D3D12용 선언을 켠다.
#include <DirectXTex.h>

#include <cstring>
#include <string>

#include "Util.h"

using Microsoft::WRL::ComPtr;

namespace {

DirectX::ScratchImage LoadImageFile(const std::filesystem::path &path,
                                    DirectX::TexMetadata &metadata) {
    DirectX::ScratchImage image;
    const HRESULT hr =
        path.extension() == L".dds"
            ? DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE,
                                       &metadata, image)
            : DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE,
                                       &metadata, image);
    Check(hr, ("Load texture " + path.string()).c_str());
    return image;
}

} // namespace

Texture Texture::Load(ID3D12Device *device,
                      ID3D12GraphicsCommandList *commandList,
                      const std::filesystem::path &path,
                      std::vector<ComPtr<ID3D12Resource>> &uploadBuffers) {
    DirectX::TexMetadata metadata;
    const DirectX::ScratchImage image = LoadImageFile(path, metadata);

    // 1. 기본 힙에 텍스처를 만든다. 크기, 면 수, 형식, 밉 수는 파일 정보로 지금
    // 정해지고 메모리도 이때 잡히지만, 내용은 비어 있다. 복사 명령의 목적지로
    // 쓸 것이므로 COPY_DEST 상태로 시작한다. 큐브맵은 면 6장짜리 2D 배열이다.
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = metadata.width;
    desc.Height = static_cast<UINT>(metadata.height);
    desc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
    desc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
    desc.Format = metadata.format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    Texture texture;
    texture.m_isCubemap = metadata.IsCubemap();
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr,
                                          IID_PPV_ARGS(&texture.m_resource)),
          "CreateCommittedResource");

    // 2. 서브리소스(면 × 밉)마다 업로드 버퍼 안의 위치와 줄 간격을 받는다. GPU
    // 복사는 줄의 시작이 256바이트 배수여야 해서 파일의 줄 간격과 다를 수 있다.
    // DirectXTex의 이미지 순서(면마다 밉 순서)는 D3D12 서브리소스 번호와 같다.
    const UINT subresourceCount = static_cast<UINT>(image.GetImageCount());
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresourceCount);
    std::vector<UINT> rowCounts(subresourceCount);
    std::vector<UINT64> rowSizes(subresourceCount);
    UINT64 uploadBytes = 0;
    device->GetCopyableFootprints(&desc, 0, subresourceCount, 0, layouts.data(),
                                  rowCounts.data(), rowSizes.data(),
                                  &uploadBytes);

    const ComPtr<ID3D12Resource> upload =
        CreateUploadBuffer(device, uploadBytes);

    BYTE *mapped = nullptr;
    const D3D12_RANGE noRead = {0, 0};
    Check(upload->Map(0, &noRead, reinterpret_cast<void **>(&mapped)), "Map");
    const DirectX::Image *images = image.GetImages();
    for (UINT i = 0; i < subresourceCount; ++i) {
        BYTE *destination = mapped + layouts[i].Offset;
        for (UINT row = 0; row < rowCounts[i]; ++row) {
            std::memcpy(destination + row * layouts[i].Footprint.RowPitch,
                        images[i].pixels + row * images[i].rowPitch,
                        static_cast<size_t>(rowSizes[i]));
        }
    }
    upload->Unmap(0, nullptr);

    // 3. 서브리소스마다 복사 명령을 기록한다. 실제 복사는 커맨드 리스트를
    // 실행할 때 GPU가 한다.
    for (UINT i = 0; i < subresourceCount; ++i) {
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = texture.m_resource.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = i;

        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = layouts[i];

        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }

    // 4. 이후로는 픽셀 셰이더가 읽기만 하므로 용도가 바뀌는 여기서 상태를
    // 바꾼다.
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.m_resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);

    uploadBuffers.push_back(upload);
    return texture;
}

void Texture::CreateShaderResourceView(
    ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE destination) const {
    const D3D12_RESOURCE_DESC resourceDesc = m_resource->GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = resourceDesc.Format;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (m_isCubemap) {
        // 면 6장짜리 배열을 셰이더가 방향 벡터로 읽는 큐브로 보게 한다.
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MipLevels = resourceDesc.MipLevels;
    } else {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = resourceDesc.MipLevels;
    }
    device->CreateShaderResourceView(m_resource.Get(), &desc, destination);
}
