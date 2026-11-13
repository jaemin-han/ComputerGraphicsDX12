#pragma once

#include <cstdint>

#include "Mesh.h"

// 원점이 중심이고 반지름이 radius인 구를 만든다. 경도 방향으로 slices칸,
// 위도 방향으로 stacks칸으로 나눈다.
MeshData MakeSphere(float radius, uint32_t slices, uint32_t stacks);

// xy 평면에 놓인 한 변 2인 정사각형을 만든다. 앞면은 -z를 향한다.
MeshData MakeSquare();
