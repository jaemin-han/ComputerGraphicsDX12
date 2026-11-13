#pragma once

#include <windows.h>

#include <d3d12.h>
#include <d3d12sdklayers.h>

#include <vector>

// hr이 실패 코드면 동작 이름을 담은 std::runtime_error를 던진다.
void Check(HRESULT hr, const char *action);

// 빌드가 실행 파일 옆에 둔 .cso 파일을 읽는다.
std::vector<char> LoadCompiledShader(const char *fileName);

// 경고와 오류를 stderr로 출력하는 디버그 레이어 콜백이다.
void CALLBACK OnD3D12Message(D3D12_MESSAGE_CATEGORY category,
                             D3D12_MESSAGE_SEVERITY severity,
                             D3D12_MESSAGE_ID id, LPCSTR description,
                             void *context);
