#ifndef TEAMS_C_GDI_APP_H
#define TEAMS_C_GDI_APP_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wchar.h>

#define APP_TITLE L"Teams C+GDI"
#define MAX_CHATS 32
#define MAX_MESSAGES 128
#define MESSAGE_TEXT_CAP 768
#define CHAT_ID_CAP 32
#define CHAT_TITLE_CAP 96
#define CHAT_PREVIEW_CAP 160

#define IDC_GLOBAL_SEARCH 1001
#define IDC_COMPOSER 1002
#define WM_APP_SEND_MESSAGE (WM_APP + 10)
#define WM_APP_NEW_CHAT (WM_APP + 11)

typedef enum NavPage {
    PAGE_ACTIVITY = 0,
    PAGE_CHAT,
    PAGE_TEAMS,
    PAGE_CALENDAR,
    PAGE_CALLS,
    PAGE_FILES,
    PAGE_SETTINGS,
    PAGE_COUNT
} NavPage;

typedef struct ChatMessage {
    BOOL mine;
    wchar_t sender[64];
    wchar_t time[16];
    wchar_t text[MESSAGE_TEXT_CAP];
} ChatMessage;

typedef struct Chat {
    char id[CHAT_ID_CAP];
    wchar_t title[CHAT_TITLE_CAP];
    wchar_t initials[8];
    wchar_t preview[CHAT_PREVIEW_CAP];
    wchar_t last_time[16];
    COLORREF avatar_color;
    BOOL online;
    BOOL favorite;
    BOOL group;
    int unread;
    ChatMessage messages[MAX_MESSAGES];
    int message_count;
} Chat;

typedef struct AppState {
    HWND hwnd;
    HWND search_edit;
    HWND composer_edit;
    WNDPROC search_proc;
    WNDPROC composer_proc;
    HFONT font_regular;
    HFONT font_small;
    HFONT font_bold;
    HFONT font_large;
    HBRUSH edit_brush;
    HBRUSH white_brush;
    int width;
    int height;
    int dpi;
    NavPage page;
    int selected_chat;
    int chat_count;
    int chat_filter;
    wchar_t search_text[128];
    int chat_list_scroll;
    int message_scroll;
    int selected_team;
    int selected_channel;
    int calendar_year;
    int calendar_month;
    int calendar_day;
    int local_chat_sequence;
    int mouse_x;
    int mouse_y;
    BOOL tracking_mouse;
    Chat chats[MAX_CHATS];
} AppState;

extern AppState g_app;

void DataInitialize(void);
void DataLoadLocalMessages(void);
void DataSaveLocalChat(const Chat *chat);
void DataSaveLocalMessage(const Chat *chat, const ChatMessage *message);
int DataCreateLocalChat(void);
void DataAddMessage(int chat_index, BOOL mine, const wchar_t *sender,
                    const wchar_t *time, const wchar_t *text);

BOOL UiInitialize(HWND hwnd);
void UiDestroy(void);
void UiLayoutControls(void);
void UiPaint(HWND hwnd);
void UiHandleClick(int x, int y);
void UiHandleWheel(HWND hwnd, WPARAM wparam, LPARAM lparam);
void UiHandleCommand(HWND hwnd, WPARAM wparam, LPARAM lparam);
void UiSendCurrentMessage(void);
void UiCreateLocalChat(void);
LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK ComposerEditProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

#endif
