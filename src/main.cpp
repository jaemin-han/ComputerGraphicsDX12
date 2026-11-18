// 프로그램 진입점이다. 엔진을 하나 만들어 초기화하고 메시지 루프를 돌린다.
//
// 실행 인자:
//   --view <번호>      시작할 때의 디버그 뷰다(0 Final, 1 F, 2 D, 3 G,
//                      4 kDiffuse, 5 확산 BRDF, 6 정반사 BRDF, 7 BRDF).
//   --capture <경로>   몇 프레임 뒤 장면(UI 제외)을 PNG로 저장하고 종료한다.
// 경로에 한글이 들어가도 되도록 wmain으로 넓은 문자 인자를 받는다.

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Engine.h"

namespace {

Engine::StartupOptions ParseArguments(int argc, wchar_t **argv) {
    Engine::StartupOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring name = argv[i];
        if (i + 1 >= argc)
            throw std::runtime_error("Missing value for a command-line option");
        const std::wstring value = argv[++i];
        if (name == L"--view")
            options.debugView = std::stoi(value);
        else if (name == L"--capture")
            options.capturePath = value;
        else
            throw std::runtime_error("Unknown command-line option");
    }
    return options;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
    // DirectXTex가 PNG, JPG를 읽고 쓸 때 쓰는 WIC는 COM이 초기화돼 있어야 한다.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        std::cerr << "Error: CoInitializeEx failed" << std::endl;
        return -1;
    }

    int exitCode = -1;
    try {
        Engine engine;
        engine.Initialize(ParseArguments(argc, argv));
        exitCode = engine.Run();
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << std::endl;
    }

    // 엔진이 COM 객체를 모두 놓은 뒤에 정리한다.
    CoUninitialize();
    return exitCode;
}
