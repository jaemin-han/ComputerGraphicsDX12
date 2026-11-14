#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>

// 프레임 하나가 소유하는 자원이다. GPU가 그 프레임을 실행하는 동안 유지돼야
// 한다.
struct FrameResource {
    // 상수 버퍼 주소는 256바이트 배수여야 하므로 쓸 위치를 이 값으로 올림한다.
    static constexpr UINT64 kConstantAlignment =
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;

    // 기록한 명령은 GPU가 끝낼 때까지 이 메모리에 남는다.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
    // 이 프레임을 제출한 뒤 Signal한 값이다. 펜스가 이 값에 도달하면 할당자와
    // 상수 버퍼를 다시 쓸 수 있다.
    UINT64 fenceValue = 0;
    // 상수를 앞에서부터 차례로 쓰는 업로드 버퍼와, 한 번 Map해 둔 CPU 주소다.
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer;
    BYTE *mappedConstants = nullptr;
    UINT64 constantBufferSize = 0;
    // 이번 프레임에 상수 버퍼를 어디까지 썼는지다. 다음 상수는 이 위치를
    // 256바이트 배수로 올림한 곳에 쓴다.
    UINT64 constantOffset = 0;

    // 커맨드 할당자와 bufferSize바이트짜리 상수 버퍼를 만든다.
    void Create(ID3D12Device *device, UINT64 bufferSize);

    // 상수 버퍼를 처음부터 다시 쓰게 한다. GPU가 이 프레임의 이전 제출을 다
    // 읽은 뒤(펜스 대기 뒤)에만 부른다.
    void ResetConstants();

    // 다음 빈 위치에 data를 쓰고 그 GPU 주소를 돌려준다. 크기가 다른 상수가
    // 섞여도 쓴 만큼만 앞으로 나간다. 그 주소를 어느 루트 파라미터에 연결할지는
    // 루트 시그니처를 가진 쪽이 정한다.
    D3D12_GPU_VIRTUAL_ADDRESS WriteConstants(const void *data, size_t size);
};
