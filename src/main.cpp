// 프로그램 진입점이다. 엔진을 하나 만들어 초기화하고 메시지 루프를 돌린다.

#include <exception>
#include <iostream>

#include "Engine.h"

int main() {
    try {
        Engine engine;
        engine.Initialize();
        return engine.Run();
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << std::endl;
        return -1;
    }
}
