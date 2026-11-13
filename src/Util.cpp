#include "Util.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

void Check(HRESULT hr, const char *action) {
    if (SUCCEEDED(hr))
        return;

    std::ostringstream message;
    message << action << " failed (HRESULT 0x" << std::hex
            << static_cast<unsigned long>(hr) << ")";
    throw std::runtime_error(message.str());
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device *device,
                                                          UINT64 size) {
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

    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&buffer)),
          "CreateCommittedResource");
    return buffer;
}

void CopyToBuffer(ID3D12Resource *buffer, const void *data, size_t size) {
    void *mapped = nullptr;
    const D3D12_RANGE noRead = {0, 0};
    Check(buffer->Map(0, &noRead, &mapped), "Map");
    std::memcpy(mapped, data, size);
    buffer->Unmap(0, nullptr);
}

// 셰이더는 빌드 때 컴파일되어 실행 파일 옆에 복사되므로, 작업 디렉터리와
// 상관없이 찾을 수 있다.
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

// 디버그 레이어 메시지는 보통 디버거에만 전달된다. 콘솔에도 출력해서 검증
// 오류를 바로 볼 수 있게 한다.
void CALLBACK OnD3D12Message(D3D12_MESSAGE_CATEGORY,
                             D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID,
                             LPCSTR description, void *) {
    if (severity > D3D12_MESSAGE_SEVERITY_WARNING)
        return;

    const char *label =
        severity == D3D12_MESSAGE_SEVERITY_WARNING ? "WARNING" : "ERROR";
    std::cerr << "[D3D12 " << label << "] " << description << std::endl;
}
