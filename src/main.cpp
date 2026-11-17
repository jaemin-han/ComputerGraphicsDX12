// 프로그램 진입점이다. 엔진을 하나 만들어 초기화하고 메시지 루프를 돌린다.

#include <exception>
#include <iostream>

#include "Engine.h"

int main() {
    // DirectXTex가 PNG, JPG를 읽을 때 쓰는 WIC는 COM이 초기화돼 있어야 한다.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        std::cerr << "Error: CoInitializeEx failed" << std::endl;
        return -1;
    }

    int exitCode = -1;
    try {
        Engine engine;
        engine.Initialize();
        exitCode = engine.Run();
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << std::endl;
    }

    // 엔진이 COM 객체를 모두 놓은 뒤에 정리한다.
    CoUninitialize();
    return exitCode;
}
