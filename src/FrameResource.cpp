#include "FrameResource.h"

#include <cstring>
#include <stdexcept>

#include "Util.h"

void FrameResource::Create(ID3D12Device *device, UINT64 bufferSize) {
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&commandAllocator)),
          "CreateCommandAllocator");

    constantBufferSize = bufferSize;
    constantBuffer = CreateUploadBuffer(device, bufferSize);

    // 업로드 힙은 Map한 채로 둬도 된다. 프로그램이 끝날 때까지 이 주소로 쓴다.
    void *mapped = nullptr;
    const D3D12_RANGE noRead = {0, 0};
    Check(constantBuffer->Map(0, &noRead, &mapped), "Map");
    mappedConstants = static_cast<BYTE *>(mapped);
}

void FrameResource::ResetConstants() { constantOffset = 0; }

D3D12_GPU_VIRTUAL_ADDRESS FrameResource::WriteConstants(const void *data,
                                                        size_t size) {
    // 상수 버퍼 주소 규칙에 맞게 쓸 위치를 256바이트 배수로 올림한다.
    const UINT64 offset = (constantOffset + kConstantAlignment - 1) /
                          kConstantAlignment * kConstantAlignment;
    if (offset + size > constantBufferSize)
        throw std::runtime_error("Constants do not fit in the frame's buffer.");

    // GPU는 이 위치를 커맨드 리스트를 실행할 때 읽는다. 그때까지 덮어쓰지
    // 않도록 다음 상수는 이 뒤에 쓴다.
    std::memcpy(mappedConstants + offset, data, size);
    constantOffset = offset + size;
    return constantBuffer->GetGPUVirtualAddress() + offset;
}
