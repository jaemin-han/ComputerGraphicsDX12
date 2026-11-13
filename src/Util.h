#pragma once

#include <windows.h>

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>

#include <cstddef>
#include <filesystem>
#include <vector>

// hr이 실패 코드면 동작 이름을 담은 std::runtime_error를 던진다.
void Check(HRESULT hr, const char *action);

// 업로드 힙에 size바이트 버퍼를 만든다. CPU가 쓰고 GPU가 읽을 수 있어서,
// 작은 데이터는 복사 패스 없이 여기서 바로 읽힌다.
Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device *device,
                                                          UINT64 size);

// 업로드 버퍼를 잠깐 Map해 data를 복사한다.
void CopyToBuffer(ID3D12Resource *buffer, const void *data, size_t size);

// 실행 파일이 있는 폴더다. 빌드가 셰이더와 자산을 이 폴더 아래로 복사하므로
// 작업 디렉터리와 상관없이 찾을 수 있다.
std::filesystem::path ExecutableDirectory();

// 빌드가 실행 파일 옆에 둔 .cso 파일을 읽는다.
std::vector<char> LoadCompiledShader(const char *fileName);

// 경고와 오류를 stderr로 출력하는 디버그 레이어 콜백이다.
void CALLBACK OnD3D12Message(D3D12_MESSAGE_CATEGORY category,
                             D3D12_MESSAGE_SEVERITY severity,
                             D3D12_MESSAGE_ID id, LPCSTR description,
                             void *context);
