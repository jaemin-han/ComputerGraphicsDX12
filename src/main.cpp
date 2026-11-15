// 프로그램 진입점이다. 엔진을 하나 만들어 초기화하고 메시지 루프를 돌린다.
// `--time 3.5` 처럼 실행하면 카메라와 해를 그 시각에 멈춘다.

#include <exception>
#include <iostream>
#include <string>

#include "Engine.h"

int main(int argc, char *argv[]) {
    // DirectXTex가 PNG, JPG를 읽을 때 쓰는 WIC는 COM이 초기화돼 있어야 한다.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        std::cerr << "Error: CoInitializeEx failed" << std::endl;
        return -1;
    }

    int exitCode = -1;
    try {
        Engine engine;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--time" && i + 1 < argc)
                engine.FreezeTime(std::stof(argv[++i]));
        }
        engine.Initialize();
        exitCode = engine.Run();
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << std::endl;
    }

    // 엔진이 COM 객체를 모두 놓은 뒤에 정리한다.
    CoUninitialize();
    return exitCode;
}
