#include "Util.h"

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
