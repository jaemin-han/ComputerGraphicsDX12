#include "FrameResource.h"

#include <cstring>
#include <stdexcept>

#include "Util.h"

void FrameResource::Create(ID3D12Device *device, UINT slotCount) {
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&commandAllocator)),
          "CreateCommandAllocator");

    constantSlotCount = slotCount;
    constantBuffer =
        CreateUploadBuffer(device, UINT64(kConstantSlotSize) * slotCount);

    // 업로드 힙은 Map한 채로 둬도 된다. 프로그램이 끝날 때까지 이 주소로 쓴다.
    void *mapped = nullptr;
    const D3D12_RANGE noRead = {0, 0};
    Check(constantBuffer->Map(0, &noRead, &mapped), "Map");
    mappedConstants = static_cast<BYTE *>(mapped);
}

D3D12_GPU_VIRTUAL_ADDRESS
FrameResource::WriteConstants(UINT slot, const void *data, size_t size) {
    if (slot >= constantSlotCount || size > kConstantSlotSize)
        throw std::runtime_error("Constants do not fit in the frame's slots.");

    // GPU는 이 칸을 커맨드 리스트를 실행할 때 읽는다. 그때까지 같은 칸을
    // 덮어쓰지 않도록 드로우마다 다른 칸을 쓴다.
    const UINT64 offset = UINT64(slot) * kConstantSlotSize;
    std::memcpy(mappedConstants + offset, data, size);
    return constantBuffer->GetGPUVirtualAddress() + offset;
}
