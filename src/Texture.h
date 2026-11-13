#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <filesystem>
#include <vector>

// 기본 힙에 올린 텍스처다. 셰이더에 묶는 SRV는 다음 단계에서 만든다.
class Texture {
  public:
    // 텍스처 파일(DDS, PNG, JPG)을 읽어 기본 힙에 텍스처를 만들고, 업로드
    // 버퍼에서 복사하는 명령과 배리어를 commandList에 기록한다. 실제 복사는
    // 커맨드 리스트를 실행할 때 일어나므로, 업로드 버퍼는 uploadBuffers에 넣어
    // 호출한 쪽이 GPU 완료를 기다릴 때까지 살려 둔다.
    static Texture
    Load(ID3D12Device *device, ID3D12GraphicsCommandList *commandList,
         const std::filesystem::path &path,
         std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> &uploadBuffers);

    ID3D12Resource *Resource() const { return m_resource.Get(); }

  private:
    Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
};
