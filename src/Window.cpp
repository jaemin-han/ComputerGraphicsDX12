#include "Window.h"

#include "Util.h"

namespace {

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam,
                            LPARAM lParam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(window, message, wParam, lParam);
}

} // namespace

HWND CreateAppWindow(UINT width, UINT height, const wchar_t *title) {
    const HINSTANCE instance = GetModuleHandle(nullptr);
    const wchar_t *className = L"HelloDX12Window";

    WNDCLASSEX windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = className;
    if (!RegisterClassEx(&windowClass))
        Check(HRESULT_FROM_WIN32(GetLastError()), "RegisterClassEx");

    // 스왑체인을 리사이즈하지 않으므로 창 크기도 고정한다.
    const DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT rect = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&rect, style, FALSE);

    const HWND window =
        CreateWindowEx(0, className, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                       rect.right - rect.left, rect.bottom - rect.top, nullptr,
                       nullptr, instance, nullptr);
    if (!window)
        Check(HRESULT_FROM_WIN32(GetLastError()), "CreateWindowEx");
    return window;
}
