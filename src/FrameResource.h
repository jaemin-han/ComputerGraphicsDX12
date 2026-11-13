#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>

// 프레임 하나가 소유하는 자원이다. GPU가 그 프레임을 실행하는 동안 유지돼야
// 한다.
struct FrameResource {
    // 상수 버퍼 주소는 256바이트 배수여야 하므로 칸 크기도 256바이트다.
    static constexpr UINT kConstantSlotSize =
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

    // 기록한 명령은 GPU가 끝낼 때까지 이 메모리에 남는다.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    // 이 프레임을 제출한 뒤 Signal한 값이다. 펜스가 이 값에 도달하면 할당자와
    // 상수 버퍼를 다시 쓸 수 있다.
    UINT64 fenceValue = 0;
    // 드로우마다 칸 하나를 쓰는 업로드 버퍼와, 한 번 Map해 둔 CPU 주소다.
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer;
    BYTE *mappedConstants = nullptr;
    UINT constantSlotCount = 0;

    // 커맨드 할당자와 slotCount칸짜리 상수 버퍼를 만든다.
    void Create(ID3D12Device *device, UINT slotCount);

    // slot번 칸에 data를 쓰고 그 칸의 GPU 주소를 돌려준다. 그 주소를 어느 루트
    // 파라미터에 연결할지는 루트 시그니처를 가진 쪽이 정한다.
    D3D12_GPU_VIRTUAL_ADDRESS WriteConstants(UINT slot, const void *data,
                                             size_t size);
};
