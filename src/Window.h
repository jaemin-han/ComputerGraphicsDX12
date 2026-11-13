#pragma once

#include <windows.h>

// 클라이언트 영역이 width x height인 창을 숨긴 상태로 만든다. 크기는
// 고정이고, 창을 닫으면 WM_QUIT이 게시된다.
HWND CreateAppWindow(UINT width, UINT height, const wchar_t *title);
