#include "app.h"
#include "auth.h"

#include <windowsx.h>

AppState g_app;

static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message,
                                       WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        g_app.hwnd = hwnd;
        g_app.page = PAGE_CHAT;
        g_app.selected_chat = 0;
        g_app.mouse_x = -1;
        g_app.mouse_y = -1;
        AuthInitialize();
        DataInitialize();
        if (!UiInitialize(hwnd)) {
            return -1;
        }
        UiLayoutControls();
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;

    case WM_SIZE:
        g_app.width = LOWORD(lparam);
        g_app.height = HIWORD(lparam);
        UiLayoutControls();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *limits = (MINMAXINFO *)lparam;
        limits->ptMinTrackSize.x = 900;
        limits->ptMinTrackSize.y = 620;
        return 0;
    }

    case WM_COMMAND:
        UiHandleCommand(hwnd, wparam, lparam);
        return 0;

    case WM_APP_SEND_MESSAGE:
        UiSendCurrentMessage();
        return 0;

    case WM_APP_NEW_CHAT:
        UiCreateLocalChat();
        return 0;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        UiHandleClick(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;

    case WM_MOUSEWHEEL:
        UiHandleWheel(hwnd, wparam, lparam);
        return 0;

    case WM_MOUSEMOVE:
        if (!g_app.tracking_mouse) {
            TRACKMOUSEEVENT tracking;
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = hwnd;
            tracking.dwHoverTime = HOVER_DEFAULT;
            TrackMouseEvent(&tracking);
            g_app.tracking_mouse = TRUE;
        }
        g_app.mouse_x = GET_X_LPARAM(lparam);
        g_app.mouse_y = GET_Y_LPARAM(lparam);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_MOUSELEAVE:
        g_app.tracking_mouse = FALSE;
        g_app.mouse_x = -1;
        g_app.mouse_y = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wparam;
        SetTextColor(dc, RGB(36, 36, 36));
        SetBkColor(dc, RGB(255, 255, 255));
        return (LRESULT)g_app.edit_brush;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        UiPaint(hwnd);
        return 0;

    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            if (wparam == 'N') {
                SendMessageW(hwnd, WM_APP_NEW_CHAT, 0, 0);
                return 0;
            }
            if (wparam == 'E') {
                SetFocus(g_app.search_edit);
                SendMessageW(g_app.search_edit, EM_SETSEL, 0, -1);
                return 0;
            }
            if (wparam >= '1' && wparam <= '7') {
                g_app.page = (NavPage)(wparam - '1');
                if (g_app.page == PAGE_CHAT && g_app.selected_chat < 0 &&
                    g_app.chat_count > 0) {
                    g_app.selected_chat = 0;
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        if (wparam == VK_ESCAPE) {
            SetFocus(hwnd);
            return 0;
        }
        return 0;

    case WM_TIMER:
        if (wparam == 1) {
            AuthUpdatePolling();
            if (g_auth.state == AUTH_SIGNED_IN && g_auth.user_name[0] == L'\0') {
                AuthFetchProfile();
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        UiDestroy();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance,
                   LPSTR command_line, int show_command)
{
    WNDCLASSEXW window_class;
    HWND hwnd;
    MSG message;
    RECT window_rect = {0, 0, 1360, 860};
    DWORD style = WS_OVERLAPPEDWINDOW;

    (void)previous_instance;
    (void)command_line;

    SetProcessDPIAware();

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = MainWindowProc;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = L"TeamsCGDIWindow";
    window_class.hIconSm = window_class.hIcon;
    if (!RegisterClassExW(&window_class)) {
        MessageBoxW(NULL, L"ウィンドウクラスを登録できませんでした。",
                    APP_TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }

    AdjustWindowRectEx(&window_rect, style, FALSE, 0);
    hwnd = CreateWindowExW(0, window_class.lpszClassName, APP_TITLE, style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           window_rect.right - window_rect.left,
                           window_rect.bottom - window_rect.top,
                           NULL, NULL, instance, NULL);
    if (hwnd == NULL) {
        MessageBoxW(NULL, L"メインウィンドウを作成できませんでした。",
                    APP_TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
