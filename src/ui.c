#include "app.h"

#include <windowsx.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Layout metrics                                                       */
/* ------------------------------------------------------------------ */

static int Dpx(int value) { return (value * g_app.dpi + 48) / 96; }

static int NavWidth(void) { return Dpx(64); }
static int ChatListWidth(void) { return Dpx(320); }
static int ConversationHeaderHeight(void) { return Dpx(76); }
static int ComposerHeight(void) { return Dpx(78); }
static int ChatItemHeight(void) { return Dpx(76); }

/* ------------------------------------------------------------------ */
/* Colors                                                               */
/* ------------------------------------------------------------------ */

#define COL_APP_BG RGB(243, 242, 241)
#define COL_RAIL_BG RGB(248, 248, 248)
#define COL_RAIL_SELECTED RGB(237, 237, 237)
#define COL_PANEL_BG RGB(255, 255, 255)
#define COL_BORDER RGB(225, 225, 225)
#define COL_TEXT RGB(36, 36, 36)
#define COL_TEXT_MUTED RGB(97, 97, 97)
#define COL_ACCENT RGB(86, 114, 245)
#define COL_ACCENT_DARK RGB(72, 96, 205)
#define COL_COMPOSER_BG RGB(247, 247, 247)
#define COL_MESSAGE_MINE RGB(238, 241, 253)
#define COL_MESSAGE_OTHER RGB(243, 242, 241)
#define COL_UNREAD_BG RGB(229, 80, 80)

/* ------------------------------------------------------------------ */
/* Drawing helpers                                                      */
/* ------------------------------------------------------------------ */

static void FillRectColor(HDC dc, RECT rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

static void DrawRoundedRect(HDC dc, RECT rect, int radius, COLORREF fill,
                            COLORREF border)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
              radius, radius);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void DrawCircle(HDC dc, int cx, int cy, int radius, COLORREF fill)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    Ellipse(dc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void DrawTextLine(HDC dc, const wchar_t *text, RECT rect,
                         HFONT font, COLORREF color, UINT format)
{
    HGDIOBJ old_font;
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    old_font = SelectObject(dc, font);
    DrawTextW(dc, text, -1, &rect, format | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old_font);
}

/* ------------------------------------------------------------------ */
/* Icon drawing                                                         */
/* ------------------------------------------------------------------ */

static void DrawIconNav(HDC dc, int cx, int cy, NavPage page,
                        COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    int s = Dpx(10);

    switch (page) {
    case PAGE_ACTIVITY:
        MoveToEx(dc, cx, cy - s, NULL);
        LineTo(dc, cx + s, cy - s + s / 2);
        LineTo(dc, cx, cy + s);
        LineTo(dc, cx - s, cy - s + s / 2);
        LineTo(dc, cx, cy - s);
        break;
    case PAGE_CHAT:
        RoundRect(dc, cx - s, cy - s, cx + s, cy + s - Dpx(3),
                  Dpx(6), Dpx(6));
        MoveToEx(dc, cx - Dpx(4), cy + s - Dpx(3), NULL);
        LineTo(dc, cx - Dpx(8), cy + s + Dpx(2));
        LineTo(dc, cx - Dpx(2), cy + s - Dpx(3));
        break;
    case PAGE_TEAMS:
        DrawCircle(dc, cx, cy - Dpx(4), Dpx(5), color);
        DrawCircle(dc, cx - Dpx(8), cy + Dpx(4), Dpx(4), color);
        DrawCircle(dc, cx + Dpx(8), cy + Dpx(4), Dpx(4), color);
        break;
    case PAGE_CALENDAR:
        Rectangle(dc, cx - s, cy - s + Dpx(2), cx + s, cy + s);
        MoveToEx(dc, cx - s, cy - s + Dpx(6), NULL);
        LineTo(dc, cx + s, cy - s + Dpx(6));
        MoveToEx(dc, cx - Dpx(4), cy - s, NULL);
        LineTo(dc, cx - Dpx(4), cy - s + Dpx(4));
        MoveToEx(dc, cx + Dpx(4), cy - s, NULL);
        LineTo(dc, cx + Dpx(4), cy - s + Dpx(4));
        break;
    case PAGE_CALLS:
        Arc(dc, cx - s, cy - s, cx + s, cy + s,
            cx + s, cy, cx, cy + s);
        break;
    case PAGE_FILES:
        MoveToEx(dc, cx - s, cy - s + Dpx(2), NULL);
        LineTo(dc, cx - Dpx(3), cy - s + Dpx(2));
        LineTo(dc, cx - Dpx(1), cy - s);
        LineTo(dc, cx + s, cy - s);
        LineTo(dc, cx + s, cy + s);
        LineTo(dc, cx - s, cy + s);
        LineTo(dc, cx - s, cy - s + Dpx(2));
        break;
    case PAGE_SETTINGS:
        DrawCircle(dc, cx, cy, Dpx(6), color);
        DrawCircle(dc, cx, cy, Dpx(3), COL_PANEL_BG);
        break;
    default:
        break;
    }

    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

/* ------------------------------------------------------------------ */
/* Avatar drawing                                                       */
/* ------------------------------------------------------------------ */

static void DrawAvatar(HDC dc, int cx, int cy, int radius, COLORREF color,
                       const wchar_t *initials, BOOL online)
{
    DrawCircle(dc, cx, cy, radius, color);
    if (wcslen(initials) > 0) {
        RECT text_rect = {cx - radius, cy - radius, cx + radius, cy + radius};
        HFONT old_font = (HFONT)SelectObject(dc, g_app.font_small);
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, initials, -1, &text_rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old_font);
    }
    if (online) {
        int dot_radius = Dpx(5);
        DrawCircle(dc, cx + radius - Dpx(2), cy + radius - Dpx(2),
                   dot_radius, RGB(103, 194, 58));
        HPEN border = CreatePen(PS_SOLID, 1, COL_PANEL_BG);
        HGDIOBJ old_pen = SelectObject(dc, border);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx + radius - Dpx(2) - dot_radius,
                cy + radius - Dpx(2) - dot_radius,
                cx + radius - Dpx(2) + dot_radius,
                cy + radius - Dpx(2) + dot_radius);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(border);
    }
}

/* ------------------------------------------------------------------ */
/* Region helpers                                                       */
/* ------------------------------------------------------------------ */

static RECT NavRect(void)
{
    RECT rect = {0, 0, NavWidth(), g_app.height};
    return rect;
}

static RECT ChatListRect(void)
{
    RECT rect = {NavWidth(), 0, NavWidth() + ChatListWidth(), g_app.height};
    return rect;
}

static RECT MainRect(void)
{
    RECT rect = {NavWidth() + ChatListWidth(), 0, g_app.width, g_app.height};
    return rect;
}

static int NavItemIndexAt(int y)
{
    int item_height = Dpx(56);
    int top = Dpx(16);
    int index = (y - top) / item_height;
    if (index < 0 || index >= PAGE_COUNT) return -1;
    if (y < top) return -1;
    return index;
}

/* ------------------------------------------------------------------ */
/* Initialization / cleanup                                            */
/* ------------------------------------------------------------------ */

BOOL UiInitialize(HWND hwnd)
{
    HDC dc;
    NONCLIENTMETRICSW metrics;

    g_app.dpi = 96;
    dc = GetDC(hwnd);
    if (dc != NULL) {
        g_app.dpi = GetDeviceCaps(dc, LOGPIXELSY);
        if (g_app.dpi == 0) g_app.dpi = 96;
        ReleaseDC(hwnd, dc);
    }

    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                              &metrics, 0)) {
        g_app.font_regular = CreateFontIndirectW(&metrics.lfMessageFont);
    } else {
        g_app.font_regular = CreateFontW(Dpx(14), 0, 0, 0, FW_NORMAL, FALSE,
                                        FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                        L"Segoe UI");
    }
    g_app.font_small = CreateFontW(Dpx(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                   FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH, L"Segoe UI");
    g_app.font_bold = CreateFontW(Dpx(14), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                                  FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH, L"Segoe UI");
    g_app.font_large = CreateFontW(Dpx(20), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                                   FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH, L"Segoe UI");

    g_app.white_brush = CreateSolidBrush(RGB(255, 255, 255));
    g_app.edit_brush = CreateSolidBrush(RGB(255, 255, 255));
    g_app.search_edit = NULL;
    g_app.composer_edit = NULL;

    /* Search edit */
    g_app.search_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd, (HMENU)IDC_GLOBAL_SEARCH,
        GetModuleHandleW(NULL), NULL);
    if (g_app.search_edit != NULL) {
        SendMessageW(g_app.search_edit, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);
        SetWindowTextW(g_app.search_edit, L"Search");
        g_app.search_proc = (WNDPROC)SetWindowLongPtrW(
            g_app.search_edit, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);
    }

    /* Composer edit */
    g_app.composer_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0, 0, 0, 0, hwnd, (HMENU)IDC_COMPOSER,
        GetModuleHandleW(NULL), NULL);
    if (g_app.composer_edit != NULL) {
        SendMessageW(g_app.composer_edit, WM_SETFONT, (WPARAM)g_app.font_regular, TRUE);
        g_app.composer_proc = (WNDPROC)SetWindowLongPtrW(
            g_app.composer_edit, GWLP_WNDPROC, (LONG_PTR)ComposerEditProc);
    }

    g_app.width = 1360;
    g_app.height = 860;
    return TRUE;
}

void UiDestroy(void)
{
    if (g_app.search_edit != NULL) DestroyWindow(g_app.search_edit);
    if (g_app.composer_edit != NULL) DestroyWindow(g_app.composer_edit);
    if (g_app.font_regular != NULL) DeleteObject(g_app.font_regular);
    if (g_app.font_small != NULL) DeleteObject(g_app.font_small);
    if (g_app.font_bold != NULL) DeleteObject(g_app.font_bold);
    if (g_app.font_large != NULL) DeleteObject(g_app.font_large);
    if (g_app.white_brush != NULL) DeleteObject(g_app.white_brush);
    if (g_app.edit_brush != NULL) DeleteObject(g_app.edit_brush);
}

void UiLayoutControls(void)
{
    RECT chat_rect = ChatListRect();
    int search_height = Dpx(34);
    int search_margin = Dpx(12);
    int composer_margin = Dpx(16);
    int composer_height = ComposerHeight() - Dpx(16);

    if (g_app.search_edit != NULL) {
        MoveWindow(g_app.search_edit,
                   chat_rect.left + search_margin,
                   Dpx(62),
                   ChatListWidth() - search_margin * 2,
                   search_height, TRUE);
    }

    if (g_app.composer_edit != NULL) {
        RECT main_rect = MainRect();
        MoveWindow(g_app.composer_edit,
                   main_rect.left + composer_margin,
                   main_rect.bottom - ComposerHeight() + Dpx(8),
                   main_rect.right - main_rect.left - composer_margin * 2 - Dpx(90),
                   composer_height, TRUE);
    }
}

/* ------------------------------------------------------------------ */
/* Painting                                                            */
/* ------------------------------------------------------------------ */

static void DrawRail(HDC dc)
{
    RECT rail = NavRect();
    int item_height = Dpx(56);
    int top = Dpx(16);
    int i;

    FillRectColor(dc, rail, COL_RAIL_BG);

    /* App icon */
    DrawCircle(dc, NavWidth() / 2, Dpx(28), Dpx(16), COL_ACCENT);
    {
        RECT ir = {NavWidth() / 2 - Dpx(16), Dpx(12),
                   NavWidth() / 2 + Dpx(16), Dpx(44)};
        DrawTextLine(dc, L"T", ir, g_app.font_large, RGB(255, 255, 255),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    for (i = 0; i < PAGE_COUNT; ++i) {
        int cy = top + i * item_height + item_height / 2;
        int cx = NavWidth() / 2;
        RECT item_rect = {0, top + i * item_height, NavWidth(),
                          top + (i + 1) * item_height};
        if (i == (int)g_app.page) {
            FillRectColor(dc, item_rect, COL_RAIL_SELECTED);
            RECT bar = {0, item_rect.top + Dpx(12), Dpx(3),
                        item_rect.bottom - Dpx(12)};
            FillRectColor(dc, bar, COL_ACCENT);
        }
        DrawIconNav(dc, cx, cy, (NavPage)i,
                    i == (int)g_app.page ? COL_ACCENT : COL_TEXT_MUTED);
    }
}

static void DrawChatList(HDC dc)
{
    RECT rect = ChatListRect();
    int y;
    int i;

    FillRectColor(dc, rect, COL_PANEL_BG);

    /* Header */
    {
        RECT header = {rect.left + Dpx(16), Dpx(16), rect.right - Dpx(16), Dpx(52)};
        DrawTextLine(dc, L"Chat", header, g_app.font_large, COL_TEXT,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        /* New chat button */
        {
            RECT btn = {rect.right - Dpx(44), Dpx(20), rect.right - Dpx(12), Dpx(48)};
            DrawRoundedRect(dc, btn, Dpx(8), COL_ACCENT, COL_ACCENT);
            DrawTextLine(dc, L"+", btn, g_app.font_large, RGB(255,255,255),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    /* divider */
    {
        RECT line = {rect.left, Dpx(56), rect.right, Dpx(57)};
        FillRectColor(dc, line, COL_BORDER);
    }

    /* Chat items */
    y = Dpx(70) - g_app.chat_list_scroll;
    for (i = 0; i < g_app.chat_count; ++i) {
        Chat *chat = &g_app.chats[i];
        int item_top = y;
        int item_bottom = y + ChatItemHeight();

        /* Filter by search text */
        if (g_app.search_text[0] != L'\0') {
            if (wcsstr(chat->title, g_app.search_text) == NULL &&
                wcsstr(chat->preview, g_app.search_text) == NULL) {
                y += ChatItemHeight();
                continue;
            }
        }

        if (item_bottom < Dpx(57) || item_top > g_app.height) {
            y += ChatItemHeight();
            continue;
        }

        if (i == g_app.selected_chat && g_app.page == PAGE_CHAT) {
            RECT sel = {rect.left + Dpx(8), item_top, rect.right - Dpx(8),
                        item_bottom};
            DrawRoundedRect(dc, sel, Dpx(8), COL_RAIL_SELECTED, COL_RAIL_SELECTED);
        }

        DrawAvatar(dc, rect.left + Dpx(32), item_top + ChatItemHeight() / 2,
                   Dpx(22), chat->avatar_color, chat->initials, chat->online);

        {
            RECT title_rect = {rect.left + Dpx(64), item_top + Dpx(12),
                               rect.right - Dpx(16), item_top + Dpx(32)};
            DrawTextLine(dc, chat->title, title_rect, g_app.font_bold, COL_TEXT,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        {
            RECT preview_rect = {rect.left + Dpx(64), item_top + Dpx(34),
                                 rect.right - Dpx(16), item_top + Dpx(54)};
            DrawTextLine(dc, chat->preview, preview_rect, g_app.font_small,
                         COL_TEXT_MUTED,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        {
            RECT time_rect = {rect.right - Dpx(60), item_top + Dpx(12),
                              rect.right - Dpx(12), item_top + Dpx(30)};
            DrawTextLine(dc, chat->last_time, time_rect, g_app.font_small,
                         COL_TEXT_MUTED,
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        if (chat->unread > 0) {
            int badge_cx = rect.right - Dpx(20);
            int badge_cy = item_top + Dpx(44);
            DrawCircle(dc, badge_cx, badge_cy, Dpx(10), COL_UNREAD_BG);
            {
                wchar_t badge_text[8];
                RECT badge_rect = {badge_cx - Dpx(10), badge_cy - Dpx(10),
                                   badge_cx + Dpx(10), badge_cy + Dpx(10)};
                swprintf(badge_text, 8, L"%d", chat->unread);
                DrawTextLine(dc, badge_text, badge_rect, g_app.font_small,
                             RGB(255, 255, 255),
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        y += ChatItemHeight();
    }
}

static void DrawConversation(HDC dc)
{
    RECT main_rect = MainRect();
    Chat *chat;
    int i;
    int y;
    int content_top;
    int content_bottom;

    if (g_app.selected_chat < 0 || g_app.selected_chat >= g_app.chat_count) {
        FillRectColor(dc, main_rect, COL_PANEL_BG);
        {
            RECT empty = {main_rect.left, main_rect.top, main_rect.right,
                          main_rect.bottom};
            DrawTextLine(dc, L"会話を選択してください", empty, g_app.font_large,
                         COL_TEXT_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return;
    }

    chat = &g_app.chats[g_app.selected_chat];

    /* Header */
    {
        RECT header = {main_rect.left, main_rect.top, main_rect.right,
                       main_rect.top + ConversationHeaderHeight()};
        FillRectColor(dc, header, COL_PANEL_BG);

        DrawAvatar(dc, main_rect.left + Dpx(32),
                   main_rect.top + ConversationHeaderHeight() / 2,
                   Dpx(22), chat->avatar_color, chat->initials, chat->online);

        {
            RECT title_rect = {main_rect.left + Dpx(64), main_rect.top + Dpx(14),
                               main_rect.right - Dpx(180),
                               main_rect.top + Dpx(40)};
            DrawTextLine(dc, chat->title, title_rect, g_app.font_large, COL_TEXT,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        {
            RECT status_rect = {main_rect.left + Dpx(64),
                                main_rect.top + Dpx(40),
                                main_rect.right - Dpx(180),
                                main_rect.top + Dpx(62)};
            const wchar_t *status = chat->online ? L"オンライン" : L"オフライン";
            DrawTextLine(dc, status, status_rect, g_app.font_small,
                         chat->online ? RGB(103, 194, 58) : COL_TEXT_MUTED,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        /* action buttons */
        {
            const wchar_t *actions[] = {L"📞", L"📹", L"⋯"};
            int ax = main_rect.right - Dpx(150);
            int i;
            for (i = 0; i < 3; ++i) {
                RECT btn = {ax, main_rect.top + Dpx(20), ax + Dpx(40),
                            main_rect.top + Dpx(56)};
                DrawTextLine(dc, actions[i], btn, g_app.font_large, COL_TEXT_MUTED,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                ax += Dpx(42);
            }
        }

        /* divider */
        {
            RECT line = {main_rect.left, main_rect.top + ConversationHeaderHeight() - 1,
                         main_rect.right, main_rect.top + ConversationHeaderHeight()};
            FillRectColor(dc, line, COL_BORDER);
        }
    }

    /* Messages area */
    content_top = main_rect.top + ConversationHeaderHeight() + Dpx(8);
    content_bottom = main_rect.bottom - ComposerHeight();

    {
        RECT bg = {main_rect.left, content_top, main_rect.right, content_bottom};
        FillRectColor(dc, bg, COL_PANEL_BG);
    }

    /* Compute total message height for scroll clamping */
    {
        int total = Dpx(8);
        int mi;
        for (mi = 0; mi < chat->message_count; ++mi) {
            ChatMessage *m = &chat->messages[mi];
            int bw = (main_rect.right - main_rect.left) * 2 / 3;
            int mtw = bw - Dpx(24);
            RECT tr = {0, 0, mtw, 0};
            HDC mdc = dc;
            HGDIOBJ of = SelectObject(mdc, g_app.font_regular);
            DrawTextW(mdc, m->text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(mdc, of);
            total += (tr.bottom - tr.top < Dpx(18) ? Dpx(18) : (tr.bottom - tr.top)) + Dpx(24);
            if (!m->mine) total += Dpx(14);
        }
        {
            int viewport = content_bottom - content_top;
            int max_scroll = total - viewport;
            if (max_scroll < 0) max_scroll = 0;
            if (g_app.message_scroll > max_scroll) g_app.message_scroll = max_scroll;
        }
    }

    y = content_top + Dpx(8) - g_app.message_scroll;
    for (i = 0; i < chat->message_count; ++i) {
        ChatMessage *msg = &chat->messages[i];
        int bubble_width = (main_rect.right - main_rect.left) * 2 / 3;
        int bubble_x;
        int bubble_y = y;
        RECT bubble_rect;
        RECT text_rect;
        int text_height;
        int max_text_width = bubble_width - Dpx(24);

        {
            HDC measure_dc = dc;
            HGDIOBJ old_font = SelectObject(measure_dc, g_app.font_regular);
            text_rect.left = 0;
            text_rect.top = 0;
            text_rect.right = max_text_width;
            text_rect.bottom = 0;
            DrawTextW(measure_dc, msg->text, -1, &text_rect,
                      DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(measure_dc, old_font);
        }
        text_height = text_rect.bottom - text_rect.top;
        if (text_height < Dpx(18)) text_height = Dpx(18);

        if (msg->mine) {
            bubble_x = main_rect.right - bubble_width - Dpx(16);
        } else {
            bubble_x = main_rect.left + Dpx(16);
        }

        bubble_rect.left = bubble_x;
        bubble_rect.top = bubble_y;
        bubble_rect.right = bubble_x + bubble_width;
        bubble_rect.bottom = bubble_y + text_height + Dpx(16);

        if (bubble_rect.bottom < content_top || bubble_rect.top > content_bottom) {
            y += text_height + Dpx(24);
            if (!msg->mine) y += Dpx(14);
            continue;
        }

        if (!msg->mine) {
            RECT sender_rect = {bubble_x, bubble_y, bubble_x + bubble_width,
                                bubble_y + Dpx(16)};
            DrawTextLine(dc, msg->sender, sender_rect, g_app.font_small,
                         COL_TEXT_MUTED, DT_LEFT | DT_TOP | DT_SINGLELINE);
            bubble_y += Dpx(16);
            bubble_rect.top = bubble_y;
            bubble_rect.bottom = bubble_y + text_height + Dpx(16);
        }

        DrawRoundedRect(dc, bubble_rect, Dpx(12),
                        msg->mine ? COL_MESSAGE_MINE : COL_MESSAGE_OTHER,
                        msg->mine ? COL_MESSAGE_MINE : COL_MESSAGE_OTHER);

        text_rect.left = bubble_rect.left + Dpx(12);
        text_rect.top = bubble_rect.top + Dpx(8);
        text_rect.right = bubble_rect.right - Dpx(12);
        text_rect.bottom = bubble_rect.bottom - Dpx(8);
        DrawTextLine(dc, msg->text, text_rect, g_app.font_regular, COL_TEXT,
                     DT_LEFT | DT_TOP | DT_WORDBREAK);

        {
            RECT time_rect;
            time_rect.left = bubble_rect.left;
            time_rect.right = bubble_rect.right;
            time_rect.top = bubble_rect.bottom + Dpx(2);
            time_rect.bottom = time_rect.top + Dpx(14);
            DrawTextLine(dc, msg->time, time_rect, g_app.font_small,
                         COL_TEXT_MUTED,
                         msg->mine ? DT_RIGHT : DT_LEFT);
        }

        y = bubble_rect.bottom + Dpx(20);
    }

    /* Composer area */
    {
        RECT composer_bg = {main_rect.left, main_rect.bottom - ComposerHeight(),
                            main_rect.right, main_rect.bottom};
        FillRectColor(dc, composer_bg, COL_PANEL_BG);

        {
            RECT line = {main_rect.left, main_rect.bottom - ComposerHeight(),
                         main_rect.right, main_rect.bottom - ComposerHeight() + 1};
            FillRectColor(dc, line, COL_BORDER);
        }

        {
            RECT send_btn = {main_rect.right - Dpx(96),
                             main_rect.bottom - ComposerHeight() + Dpx(18),
                             main_rect.right - Dpx(16),
                             main_rect.bottom - Dpx(18)};
            DrawRoundedRect(dc, send_btn, Dpx(8), COL_ACCENT, COL_ACCENT);
            DrawTextLine(dc, L"Send", send_btn, g_app.font_bold,
                         RGB(255, 255, 255),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        {
            RECT hint = {main_rect.left + Dpx(16),
                         main_rect.bottom - ComposerHeight() + Dpx(8),
                         main_rect.left + Dpx(200),
                         main_rect.bottom - ComposerHeight() + Dpx(28)};
            DrawTextLine(dc, L"\U0001F4CE  Type a message...", hint, g_app.font_small,
                         COL_TEXT_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

static void DrawPlaceholderPage(HDC dc, const wchar_t *title,
                                const wchar_t *subtitle)
{
    RECT main_rect = MainRect();
    int cx = (main_rect.left + main_rect.right) / 2;
    int cy = (main_rect.top + main_rect.bottom) / 2;
    (void)cx;

    FillRectColor(dc, main_rect, COL_PANEL_BG);
    {
        RECT t = {main_rect.left, cy - Dpx(40), main_rect.right, cy};
        DrawTextLine(dc, title, t, g_app.font_large, COL_TEXT,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    {
        RECT s = {main_rect.left + Dpx(40), cy, main_rect.right - Dpx(40),
                  cy + Dpx(60)};
        DrawTextLine(dc, subtitle, s, g_app.font_regular, COL_TEXT_MUTED,
                     DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    }
}

static void DrawActivityPage(HDC dc)
{
    RECT main_rect = MainRect();
    FillRectColor(dc, main_rect, COL_PANEL_BG);

    {
        RECT title = {main_rect.left + Dpx(24), main_rect.top + Dpx(20),
                      main_rect.right - Dpx(24), main_rect.top + Dpx(56)};
        DrawTextLine(dc, L"Activity", title, g_app.font_large, COL_TEXT,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    {
        const wchar_t *items[] = {
            L"Naoki Sato がメッセージを送信しました",
            L"Product launch で新しい投稿があります",
            L"Alex Morgan があなたをメンションしました",
            L"IT Helpdesk がチケットを更新しました",
            L"Weekly sync の議事メモが共有されました"
        };
        int i;
        int y = main_rect.top + Dpx(70);
        for (i = 0; i < 5; ++i) {
            RECT item = {main_rect.left + Dpx(16), y,
                         main_rect.right - Dpx(16), y + Dpx(56)};
            DrawRoundedRect(dc, item, Dpx(8), COL_PANEL_BG, COL_BORDER);
            DrawAvatar(dc, item.left + Dpx(24), (item.top + item.bottom) / 2,
                       Dpx(18), RGB(86, 114, 245), L"A", (i % 2 == 0));
            {
                RECT text_rect = {item.left + Dpx(52), item.top + Dpx(8),
                                  item.right - Dpx(16), item.bottom - Dpx(8)};
                DrawTextLine(dc, items[i], text_rect, g_app.font_regular, COL_TEXT,
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            y += Dpx(64);
        }
    }
}

static void DrawTeamsPage(HDC dc)
{
    RECT main_rect = MainRect();
    FillRectColor(dc, main_rect, COL_PANEL_BG);

    {
        RECT title = {main_rect.left + Dpx(24), main_rect.top + Dpx(20),
                      main_rect.right - Dpx(24), main_rect.top + Dpx(56)};
        DrawTextLine(dc, L"Teams", title, g_app.font_large, COL_TEXT,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    {
        const wchar_t *teams[] = {L"Product launch", L"UX Design",
                                   L"Engineering", L"General"};
        const wchar_t *channels[][4] = {
            {L"General", L"Marketing", L"Design review", NULL},
            {L"General", L"Research", NULL, NULL},
            {L"General", L"Builds", L"Incidents", NULL},
            {L"General", NULL, NULL, NULL}
        };
        int i;
        int y = main_rect.top + Dpx(70);
        for (i = 0; i < 4; ++i) {
            RECT item = {main_rect.left + Dpx(16), y,
                         main_rect.right - Dpx(16), y + Dpx(48)};
            if (i == g_app.selected_team) {
                DrawRoundedRect(dc, item, Dpx(8), COL_RAIL_SELECTED,
                                COL_RAIL_SELECTED);
            }
            DrawAvatar(dc, item.left + Dpx(24), (item.top + item.bottom) / 2,
                       Dpx(16), RGB(91, 95, 199), L"T", FALSE);
            {
                RECT name_rect = {item.left + Dpx(52), item.top,
                                  item.right, item.bottom};
                DrawTextLine(dc, teams[i], name_rect, g_app.font_bold, COL_TEXT,
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            y += Dpx(52);
            if (i == g_app.selected_team) {
                int ci = 0;
                while (channels[i][ci] != NULL) {
                    RECT ch = {main_rect.left + Dpx(40), y,
                               main_rect.right - Dpx(16), y + Dpx(36)};
                    if (ci == g_app.selected_channel) {
                        DrawRoundedRect(dc, ch, Dpx(6), COL_RAIL_SELECTED,
                                        COL_RAIL_SELECTED);
                    }
                    {
                        RECT chname = {ch.left + Dpx(20), ch.top, ch.right, ch.bottom};
                        DrawTextLine(dc, channels[i][ci], chname, g_app.font_regular,
                                     COL_TEXT_MUTED,
                                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    }
                    y += Dpx(38);
                    ++ci;
                }
                y += Dpx(8);
            }
        }
    }
}

static void DrawCalendarPage(HDC dc)
{
    RECT main_rect = MainRect();
    FillRectColor(dc, main_rect, COL_PANEL_BG);

    /* Title */
    {
        wchar_t title[64];
        swprintf(title, 64, L"%d年 %d月", g_app.calendar_year, g_app.calendar_month);
        RECT t = {main_rect.left + Dpx(24), main_rect.top + Dpx(20),
                  main_rect.right - Dpx(24), main_rect.top + Dpx(56)};
        DrawTextLine(dc, title, t, g_app.font_large, COL_TEXT,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    /* Navigation buttons */
    {
        RECT prev = {main_rect.right - Dpx(120), main_rect.top + Dpx(24),
                     main_rect.right - Dpx(80), main_rect.top + Dpx(52)};
        RECT next = {main_rect.right - Dpx(70), main_rect.top + Dpx(24),
                     main_rect.right - Dpx(30), main_rect.top + Dpx(52)};
        DrawTextLine(dc, L"◀", prev, g_app.font_large, COL_TEXT_MUTED,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextLine(dc, L"▶", next, g_app.font_large, COL_TEXT_MUTED,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    /* Weekday header */
    {
        const wchar_t *days[] = {L"日", L"月", L"火", L"水", L"木", L"金", L"土"};
        int i;
        int cell_w = (main_rect.right - main_rect.left - Dpx(48)) / 7;
        int x0 = main_rect.left + Dpx(24);
        int y = main_rect.top + Dpx(70);
        for (i = 0; i < 7; ++i) {
            RECT r = {x0 + i * cell_w, y, x0 + (i + 1) * cell_w, y + Dpx(28)};
            DrawTextLine(dc, days[i], r, g_app.font_bold, COL_TEXT_MUTED,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    /* Day grid */
    {
        int cell_w = (main_rect.right - main_rect.left - Dpx(48)) / 7;
        int cell_h = Dpx(64);
        int x0 = main_rect.left + Dpx(24);
        int y0 = main_rect.top + Dpx(100);
        int first_weekday;
        int days_in_month;
        int day;
        SYSTEMTIME st;
        /* compute first weekday of month */
        {
            st.wYear = (WORD)g_app.calendar_year;
            st.wMonth = (WORD)g_app.calendar_month;
            st.wDay = 1;
            st.wHour = 0; st.wMinute = 0; st.wSecond = 0; st.wMilliseconds = 0;
            /* Use FileTime conversion */
            {
                FILETIME ft;
                if (SystemTimeToFileTime(&st, &ft)) {
                    /* weekday: compute via GetDateFormat? use a simple known: 2026-01-01 is Thursday */
                }
            }
        }
        /* Simple: use GetDateFormat to format? Instead compute with mktime-like via Windows */
        {
            /* Use time_t via _mkgmtime not available in MinGW? Use FileTimeToSystemTime roundtrip not needed.
               Compute weekday using Zeller-like formula */
            int y = g_app.calendar_year;
            int m = g_app.calendar_month;
            if (m < 3) { m += 12; --y; }
            first_weekday = (1 + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
            /* Convert: above gives 0=Saturday? Adjust: standard Zeller: 0=Saturday. Convert to 0=Sunday */
            first_weekday = (first_weekday + 1) % 7;
        }
        /* days in month */
        {
            static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
            days_in_month = dim[g_app.calendar_month - 1];
            if (g_app.calendar_month == 2) {
                int yy = g_app.calendar_year;
                if ((yy % 4 == 0 && yy % 100 != 0) || (yy % 400 == 0)) {
                    days_in_month = 29;
                }
            }
        }

        for (day = 1; day <= days_in_month; ++day) {
            int idx = first_weekday + (day - 1);
            int row = idx / 7;
            int col = idx % 7;
            int cx = x0 + col * cell_w;
            int cy = y0 + row * cell_h;
            RECT cell = {cx, cy, cx + cell_w, cy + cell_h};
            if (day == g_app.calendar_day) {
                DrawRoundedRect(dc, cell, Dpx(8), COL_ACCENT, COL_ACCENT);
                {
                    wchar_t dtext[8];
                    swprintf(dtext, 8, L"%d", day);
                    RECT dr = cell;
                    DrawTextLine(dc, dtext, dr, g_app.font_bold,
                                 RGB(255,255,255),
                                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            } else {
                wchar_t dtext[8];
                swprintf(dtext, 8, L"%d", day);
                RECT dr = cell;
                DrawTextLine(dc, dtext, dr, g_app.font_regular, COL_TEXT,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }
}

static void DrawCallsPage(HDC dc)
{
    DrawPlaceholderPage(dc, L"Calls",
        L"通話・会議機能はリアルタイム通信の設計後に実装します。");
}

static void DrawFilesPage(HDC dc)
{
    RECT main_rect = MainRect();
    FillRectColor(dc, main_rect, COL_PANEL_BG);

    {
        RECT title = {main_rect.left + Dpx(24), main_rect.top + Dpx(20),
                      main_rect.right - Dpx(24), main_rect.top + Dpx(56)};
        DrawTextLine(dc, L"Files", title, g_app.font_large, COL_TEXT,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    {
        const wchar_t *files[] = {
            L"📄  Review deck.pptx",
            L"📊  Q4 metrics.xlsx",
            L"📝  Meeting notes.docx",
            L"🖼  Design draft.png",
            L"📁  Shared documents"
        };
        int i;
        int y = main_rect.top + Dpx(70);
        for (i = 0; i < 5; ++i) {
            RECT item = {main_rect.left + Dpx(16), y,
                         main_rect.right - Dpx(16), y + Dpx(44)};
            if (i % 2 == 0) {
                FillRectColor(dc, item, COL_RAIL_SELECTED);
            }
            {
                RECT text_rect = {item.left + Dpx(16), item.top, item.right, item.bottom};
                DrawTextLine(dc, files[i], text_rect, g_app.font_regular, COL_TEXT,
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            y += Dpx(48);
        }
    }
}

static void DrawSettingsPage(HDC dc)
{
    DrawPlaceholderPage(dc, L"Settings",
        L"アカウント設定・テーマ・通知設定は今後の実装予定です。");
}

void UiPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    HDC memory_dc;
    HBITMAP memory_bitmap;
    HGDIOBJ old_bitmap;
    RECT client_rect;

    GetClientRect(hwnd, &client_rect);

    memory_dc = CreateCompatibleDC(dc);
    memory_bitmap = CreateCompatibleBitmap(dc, client_rect.right,
                                          client_rect.bottom);
    old_bitmap = SelectObject(memory_dc, memory_bitmap);

    FillRectColor(memory_dc, client_rect, COL_APP_BG);

    DrawRail(memory_dc);
    DrawChatList(memory_dc);

    switch (g_app.page) {
    case PAGE_CHAT:     DrawConversation(memory_dc); break;
    case PAGE_ACTIVITY: DrawActivityPage(memory_dc); break;
    case PAGE_TEAMS:    DrawTeamsPage(memory_dc); break;
    case PAGE_CALENDAR: DrawCalendarPage(memory_dc); break;
    case PAGE_CALLS:    DrawCallsPage(memory_dc); break;
    case PAGE_FILES:    DrawFilesPage(memory_dc); break;
    case PAGE_SETTINGS: DrawSettingsPage(memory_dc); break;
    default: break;
    }

    BitBlt(dc, 0, 0, client_rect.right, client_rect.bottom,
           memory_dc, 0, 0, SRCCOPY);

    SelectObject(memory_dc, old_bitmap);
    DeleteObject(memory_bitmap);
    DeleteDC(memory_dc);

    EndPaint(hwnd, &ps);
}

/* ------------------------------------------------------------------ */
/* Interaction                                                         */
/* ------------------------------------------------------------------ */

void UiHandleClick(int x, int y)
{
    if (x < NavWidth()) {
        int idx = NavItemIndexAt(y);
        if (idx >= 0 && idx < PAGE_COUNT) {
            g_app.page = (NavPage)idx;
            if (g_app.page == PAGE_CHAT && g_app.selected_chat < 0 &&
                g_app.chat_count > 0) {
                g_app.selected_chat = 0;
            }
            InvalidateRect(g_app.hwnd, NULL, FALSE);
        }
        return;
    }

    if (x >= NavWidth() && x < NavWidth() + ChatListWidth()) {
        /* New chat button */
        if (y >= Dpx(20) && y <= Dpx(48) && x >= NavWidth() + ChatListWidth() - Dpx(44)) {
            UiCreateLocalChat();
            return;
        }
        int item_height = ChatItemHeight();
        int top = Dpx(70) - g_app.chat_list_scroll;
        int i;
        for (i = 0; i < g_app.chat_count; ++i) {
            int item_top = top + i * item_height;
            int item_bottom = item_top + item_height;
            if (y >= item_top && y < item_bottom) {
                g_app.selected_chat = i;
                g_app.page = PAGE_CHAT;
                g_app.message_scroll = 1000000;
                if (g_app.chats[i].unread > 0) {
                    g_app.chats[i].unread = 0;
                }
                InvalidateRect(g_app.hwnd, NULL, FALSE);
                SetFocus(g_app.composer_edit);
                return;
            }
        }
        return;
    }

    {
        RECT main_rect = MainRect();
        if (x >= main_rect.left) {
            RECT send_btn = {main_rect.right - Dpx(96),
                             main_rect.bottom - ComposerHeight() + Dpx(18),
                             main_rect.right - Dpx(16),
                             main_rect.bottom - Dpx(18)};
            if (x >= send_btn.left && x <= send_btn.right &&
                y >= send_btn.top && y <= send_btn.bottom) {
                UiSendCurrentMessage();
                return;
            }
            if (g_app.page == PAGE_CALENDAR) {
                RECT prev = {main_rect.right - Dpx(120), main_rect.top + Dpx(24),
                             main_rect.right - Dpx(80), main_rect.top + Dpx(52)};
                RECT next = {main_rect.right - Dpx(70), main_rect.top + Dpx(24),
                             main_rect.right - Dpx(30), main_rect.top + Dpx(52)};
                if (x >= prev.left && x <= prev.right && y >= prev.top && y <= prev.bottom) {
                    --g_app.calendar_month;
                    if (g_app.calendar_month < 1) { g_app.calendar_month = 12; --g_app.calendar_year; }
                    InvalidateRect(g_app.hwnd, NULL, FALSE);
                    return;
                }
                if (x >= next.left && x <= next.right && y >= next.top && y <= next.bottom) {
                    ++g_app.calendar_month;
                    if (g_app.calendar_month > 12) { g_app.calendar_month = 1; ++g_app.calendar_year; }
                    InvalidateRect(g_app.hwnd, NULL, FALSE);
                    return;
                }
            }
            if (g_app.page == PAGE_TEAMS) {
                const wchar_t *channels[][4] = {
                    {L"General", L"Marketing", L"Design review", NULL},
                    {L"General", L"Research", NULL, NULL},
                    {L"General", L"Builds", L"Incidents", NULL},
                    {L"General", NULL, NULL, NULL}
                };
                int i;
                int item_y = main_rect.top + Dpx(70);
                for (i = 0; i < 4; ++i) {
                    if (y >= item_y && y < item_y + Dpx(48)) {
                        g_app.selected_team = i;
                        g_app.selected_channel = 0;
                        InvalidateRect(g_app.hwnd, NULL, FALSE);
                        return;
                    }
                    item_y += Dpx(52);
                    if (i == g_app.selected_team) {
                        int ci = 0;
                        while (channels[i][ci] != NULL) {
                            if (y >= item_y && y < item_y + Dpx(36)) {
                                g_app.selected_channel = ci;
                                InvalidateRect(g_app.hwnd, NULL, FALSE);
                                return;
                            }
                            item_y += Dpx(38);
                            ++ci;
                        }
                        item_y += Dpx(8);
                    }
                }
            }
        }
    }
}

void UiHandleWheel(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
    int delta = GET_WHEEL_DELTA_WPARAM(wparam);
    POINT pt;
    (void)hwnd;
    (void)lparam;

    GetCursorPos(&pt);
    ScreenToClient(g_app.hwnd, &pt);

    if (pt.x >= NavWidth() && pt.x < NavWidth() + ChatListWidth()) {
        g_app.chat_list_scroll -= delta / 2;
        if (g_app.chat_list_scroll < 0) g_app.chat_list_scroll = 0;
        InvalidateRect(g_app.hwnd, NULL, FALSE);
    } else if (pt.x >= NavWidth() + ChatListWidth()) {
        g_app.message_scroll -= delta / 2;
        if (g_app.message_scroll < 0) g_app.message_scroll = 0;
        InvalidateRect(g_app.hwnd, NULL, FALSE);
    }
}

void UiHandleCommand(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
    int control_id = LOWORD(wparam);
    (void)hwnd;
    (void)lparam;

    if (control_id == IDC_GLOBAL_SEARCH) {
        GetWindowTextW(g_app.search_edit, g_app.search_text, 128);
        InvalidateRect(g_app.hwnd, NULL, FALSE);
    }
}

void UiSendCurrentMessage(void)
{
    wchar_t text[MESSAGE_TEXT_CAP];
    SYSTEMTIME now;
    wchar_t time_text[16];
    int chat_index = g_app.selected_chat;

    if (chat_index < 0 || chat_index >= g_app.chat_count) return;

    GetWindowTextW(g_app.composer_edit, text, MESSAGE_TEXT_CAP);
    if (wcslen(text) == 0) return;

    GetLocalTime(&now);
    swprintf(time_text, 16, L"%02d:%02d", now.wHour, now.wMinute);

    DataAddMessage(chat_index, TRUE, L"You", time_text, text);
    DataSaveLocalMessage(&g_app.chats[chat_index],
                        &g_app.chats[chat_index].messages[
                            g_app.chats[chat_index].message_count - 1]);

    SetWindowTextW(g_app.composer_edit, L"");
    g_app.message_scroll = 1000000;
    InvalidateRect(g_app.hwnd, NULL, FALSE);
}

void UiCreateLocalChat(void)
{
    int index = DataCreateLocalChat();
    if (index >= 0) {
        g_app.selected_chat = index;
        g_app.page = PAGE_CHAT;
        g_app.message_scroll = 0;
        InvalidateRect(g_app.hwnd, NULL, FALSE);
        SetFocus(g_app.composer_edit);
    }
}

LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT message, WPARAM wparam,
                                LPARAM lparam)
{
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        SetFocus(g_app.composer_edit);
        return 0;
    }
    return CallWindowProcW(g_app.search_proc, hwnd, message, wparam, lparam);
}

LRESULT CALLBACK ComposerEditProc(HWND hwnd, UINT message, WPARAM wparam,
                                  LPARAM lparam)
{
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        if ((GetKeyState(VK_SHIFT) & 0x8000) == 0) {
            UiSendCurrentMessage();
            return 0;
        }
    }
    return CallWindowProcW(g_app.composer_proc, hwnd, message, wparam, lparam);
}
