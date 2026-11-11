#include "app.h"
#include "auth.h"
#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

static void CopyWide(wchar_t *destination, size_t capacity, const wchar_t *source)
{
    if (capacity == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = L'\0';
        return;
    }
    wcsncpy(destination, source, capacity - 1);
    destination[capacity - 1] = L'\0';
}

static void AppendWide(wchar_t *destination, size_t capacity, const wchar_t *suffix)
{
    size_t used;
    size_t available;

    if (capacity == 0 || suffix == NULL) {
        return;
    }
    used = wcslen(destination);
    if (used >= capacity - 1) {
        return;
    }
    available = capacity - used;
    wcsncat(destination, suffix, available - 1);
}

static BOOL GetHistoryPath(wchar_t *path, size_t capacity)
{
    HRESULT result;

    if (capacity < MAX_PATH) {
        return FALSE;
    }
    path[0] = L'\0';
    result = SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                              NULL, SHGFP_TYPE_CURRENT, path);
    if (FAILED(result)) {
        return FALSE;
    }
    AppendWide(path, capacity, L"\\TeamsCGDI");
    if (!CreateDirectoryW(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return FALSE;
    }
    AppendWide(path, capacity, L"\\messages.tsv");
    return TRUE;
}

static BOOL WideToUtf8(const wchar_t *source, char *destination, int capacity)
{
    int bytes;

    if (source == NULL || destination == NULL || capacity <= 0) {
        return FALSE;
    }
    bytes = WideCharToMultiByte(CP_UTF8, 0, source, -1, destination,
                                capacity, NULL, NULL);
    if (bytes == 0) {
        destination[0] = '\0';
        return FALSE;
    }
    return TRUE;
}

static BOOL Utf8ToWide(const char *source, int length, wchar_t *destination,
                       size_t capacity)
{
    int characters;

    if (source == NULL || destination == NULL || capacity == 0 || length < 0) {
        return FALSE;
    }
    characters = MultiByteToWideChar(CP_UTF8, 0, source, length, destination,
                                     (int)(capacity - 1));
    if (characters <= 0) {
        destination[0] = L'\0';
        return FALSE;
    }
    destination[characters] = L'\0';
    return TRUE;
}

static Chat *FindChatById(const char *id)
{
    int index;

    for (index = 0; index < g_app.chat_count; ++index) {
        if (strcmp(g_app.chats[index].id, id) == 0) {
            return &g_app.chats[index];
        }
    }
    return NULL;
}

static Chat *AddChat(const char *id, const wchar_t *title,
                     const wchar_t *initials, COLORREF color,
                     BOOL group, BOOL favorite, BOOL online)
{
    Chat *chat;

    if (g_app.chat_count >= MAX_CHATS) {
        return NULL;
    }

    chat = &g_app.chats[g_app.chat_count++];
    ZeroMemory(chat, sizeof(*chat));
    snprintf(chat->id, sizeof(chat->id), "%s", id);
    CopyWide(chat->title, _countof(chat->title), title);
    CopyWide(chat->initials, _countof(chat->initials), initials);
    chat->avatar_color = color;
    chat->group = group;
    chat->favorite = favorite;
    chat->online = online;
    return chat;
}

void DataAddMessage(int chat_index, BOOL mine, const wchar_t *sender,
                    const wchar_t *time, const wchar_t *text)
{
    Chat *chat;
    ChatMessage *message;
    const wchar_t *line_break;

    if (chat_index < 0 || chat_index >= g_app.chat_count) {
        return;
    }
    chat = &g_app.chats[chat_index];
    if (chat->message_count >= MAX_MESSAGES) {
        memmove(&chat->messages[0], &chat->messages[1],
                sizeof(ChatMessage) * (MAX_MESSAGES - 1));
        chat->message_count = MAX_MESSAGES - 1;
    }
    message = &chat->messages[chat->message_count++];
    ZeroMemory(message, sizeof(*message));
    message->mine = mine;
    CopyWide(message->sender, _countof(message->sender), sender);
    CopyWide(message->time, _countof(message->time), time);
    CopyWide(message->text, _countof(message->text), text);

    line_break = wcschr(text != NULL ? text : L"", L'\n');
    if (line_break != NULL) {
        size_t preview_length = (size_t)(line_break - text);
        if (preview_length >= _countof(chat->preview)) {
            preview_length = _countof(chat->preview) - 1;
        }
        wcsncpy(chat->preview, text, preview_length);
        chat->preview[preview_length] = L'\0';
    } else {
        CopyWide(chat->preview, _countof(chat->preview), text);
    }
    CopyWide(chat->last_time, _countof(chat->last_time), time);
}

static BOOL AppendEscapedUtf8(char *destination, size_t capacity,
                              size_t *used, const char *source)
{
    const unsigned char *cursor = (const unsigned char *)source;

    while (*cursor != 0) {
        const char *escape = NULL;
        size_t escape_length = 0;

        switch (*cursor) {
        case '\\': escape = "\\\\"; escape_length = 2; break;
        case '\t':  escape = "\\t";  escape_length = 2; break;
        case '\r':  escape = "\\r";  escape_length = 2; break;
        case '\n':  escape = "\\n";  escape_length = 2; break;
        default: break;
        }

        if (escape != NULL) {
            if (*used + escape_length >= capacity) {
                return FALSE;
            }
            memcpy(destination + *used, escape, escape_length);
            *used += escape_length;
        } else {
            if (*used + 1 >= capacity) {
                return FALSE;
            }
            destination[(*used)++] = (char)*cursor;
        }
        ++cursor;
    }
    destination[*used] = '\0';
    return TRUE;
}

static BOOL AppendHistoryLine(const char *line, size_t length)
{
    wchar_t path[MAX_PATH];
    HANDLE file;
    DWORD written = 0;
    BOOL result;

    if (!GetHistoryPath(path, _countof(path)) || length > MAXDWORD) {
        return FALSE;
    }
    file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    result = WriteFile(file, line, (DWORD)length, &written, NULL) &&
             written == (DWORD)length;
    CloseHandle(file);
    return result;
}

void DataSaveLocalChat(const Chat *chat)
{
    char line[1024];
    char title[512];
    char initials[64];
    int length;
    int color;

    if (chat == NULL || strncmp(chat->id, "local-", 6) != 0 ||
        !WideToUtf8(chat->title, title, (int)sizeof(title)) ||
        !WideToUtf8(chat->initials, initials, (int)sizeof(initials))) {
        return;
    }

    color = (int)chat->avatar_color;
    length = snprintf(line, sizeof(line), "C\t%s\t%06X\t%s\t%s\n",
                      chat->id, color & 0xFFFFFF, initials, title);
    if (length > 0 && (size_t)length < sizeof(line)) {
        AppendHistoryLine(line, (size_t)length);
    }
}

void DataSaveLocalMessage(const Chat *chat, const ChatMessage *message)
{
    char utf8[MESSAGE_TEXT_CAP * 4];
    char escaped[MESSAGE_TEXT_CAP * 8];
    char line[MESSAGE_TEXT_CAP * 8 + 128];
    size_t escaped_length = 0;
    int prefix_length;
    size_t line_length;

    if (chat == NULL || message == NULL ||
        !WideToUtf8(message->text, utf8, (int)sizeof(utf8))) {
        return;
    }
    if (!AppendEscapedUtf8(escaped, sizeof(escaped), &escaped_length, utf8)) {
        return;
    }

    prefix_length = snprintf(line, sizeof(line), "M\t%s\t", chat->id);
    if (prefix_length <= 0 || (size_t)prefix_length >= sizeof(line)) {
        return;
    }
    {
        char time_utf8[32];
        if (!WideToUtf8(message->time, time_utf8, (int)sizeof(time_utf8))) {
            return;
        }
        prefix_length += snprintf(line + prefix_length,
                                  sizeof(line) - (size_t)prefix_length,
                                  "%s\t", time_utf8);
    }
    if (prefix_length <= 0 || (size_t)prefix_length + escaped_length + 2 >= sizeof(line)) {
        return;
    }
    memcpy(line + prefix_length, escaped, escaped_length);
    line_length = (size_t)prefix_length + escaped_length;
    line[line_length++] = '\n';
    line[line_length] = '\0';
    AppendHistoryLine(line, line_length);
}

static void AddLoadedLocalChat(const char *id, unsigned long color,
                               const char *initials_utf8,
                               const char *title_utf8)
{
    wchar_t title[CHAT_TITLE_CAP];
    wchar_t initials[8];
    Chat *chat;

    if (FindChatById(id) != NULL || strncmp(id, "local-", 6) != 0 ||
        g_app.chat_count >= MAX_CHATS) {
        return;
    }
    if (!Utf8ToWide(title_utf8, -1, title, _countof(title)) ||
        !Utf8ToWide(initials_utf8, -1, initials, _countof(initials))) {
        return;
    }
    chat = AddChat(id, title, initials, (COLORREF)color, FALSE, FALSE, FALSE);
    if (chat != NULL && g_app.selected_chat < 0) {
        g_app.selected_chat = g_app.chat_count - 1;
    }
}

static void UnescapeUtf8(const char *source, char *destination, size_t capacity)
{
    size_t used = 0;
    size_t index = 0;

    while (source[index] != '\0' && used + 1 < capacity) {
        if (source[index] == '\\' && source[index + 1] != '\0') {
            ++index;
            switch (source[index]) {
            case 'n': destination[used++] = '\n'; break;
            case 'r': destination[used++] = '\r'; break;
            case 't': destination[used++] = '\t'; break;
            case '\\': destination[used++] = '\\'; break;
            default:
                destination[used++] = '\\';
                if (used + 1 < capacity) {
                    destination[used++] = source[index];
                }
                break;
            }
        } else {
            destination[used++] = source[index];
        }
        ++index;
    }
    destination[used] = '\0';
}

static void LoadHistoryLine(char *line)
{
    char *field1;
    char *field2;
    char *field3;

    if (line[0] == 'C' && line[1] == '\t') {
        unsigned long color;
        char *end;
        field1 = strchr(line + 2, '\t');
        if (field1 == NULL) return;
        *field1++ = '\0';
        field2 = strchr(field1, '\t');
        if (field2 == NULL) return;
        *field2++ = '\0';
        field3 = strchr(field2, '\t');
        if (field3 == NULL) return;
        *field3++ = '\0';
        color = strtoul(field1, &end, 16);
        if (end == field1 || *end != '\0') return;
        AddLoadedLocalChat(line + 2, color, field2, field3);
        return;
    }

    if (line[0] == 'M' && line[1] == '\t') {
        char *id = line + 2;
        Chat *chat;
        char *time;
        char *escaped;
        char text_utf8[MESSAGE_TEXT_CAP * 4];
        wchar_t time_wide[16];
        wchar_t text_wide[MESSAGE_TEXT_CAP];
        int chat_index;
        int index;

        field1 = strchr(id, '\t');
        if (field1 == NULL) return;
        *field1++ = '\0';
        time = field1;
        field2 = strchr(time, '\t');
        if (field2 == NULL) return;
        *field2++ = '\0';
        escaped = field2;
        UnescapeUtf8(escaped, text_utf8, sizeof(text_utf8));
        if (!Utf8ToWide(time, -1, time_wide, _countof(time_wide)) ||
            !Utf8ToWide(text_utf8, -1, text_wide, _countof(text_wide))) {
            return;
        }
        chat = FindChatById(id);
        if (chat == NULL) return;
        chat_index = (int)(chat - g_app.chats);
        DataAddMessage(chat_index, TRUE, L"You", time_wide, text_wide);
        for (index = 0; index < g_app.chat_count; ++index) {
            if (&g_app.chats[index] == chat) {
                chat->unread = 0;
                break;
            }
        }
    }
}

void DataLoadLocalMessages(void)
{
    wchar_t path[MAX_PATH];
    HANDLE file;
    LARGE_INTEGER file_size;
    DWORD bytes_read = 0;
    char *contents;
    size_t position = 0;
    const size_t maximum_file_size = 4u * 1024u * 1024u;

    if (!GetHistoryPath(path, _countof(path))) {
        return;
    }
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0 ||
        (unsigned long long)file_size.QuadPart > maximum_file_size) {
        CloseHandle(file);
        return;
    }
    contents = (char *)malloc((size_t)file_size.QuadPart + 1);
    if (contents == NULL) {
        CloseHandle(file);
        return;
    }
    if (!ReadFile(file, contents, (DWORD)file_size.QuadPart, &bytes_read, NULL)) {
        free(contents);
        CloseHandle(file);
        return;
    }
    CloseHandle(file);
    contents[bytes_read] = '\0';

    while (position < bytes_read) {
        size_t start = position;
        while (position < bytes_read && contents[position] != '\n') {
            ++position;
        }
        if (position > start) {
            char saved = contents[position];
            contents[position] = '\0';
            if (position > start && contents[position - 1] == '\r') {
                contents[position - 1] = '\0';
            }
            LoadHistoryLine(contents + start);
            contents[position] = saved;
        }
        if (position < bytes_read) {
            ++position;
        }
    }
    free(contents);
}

static void AddSampleMessage(int chat_index, BOOL mine, const wchar_t *sender,
                             const wchar_t *time, const wchar_t *text)
{
    DataAddMessage(chat_index, mine, sender, time, text);
}

void DataInitialize(void)
{
    SYSTEMTIME now;

    g_app.chat_count = 0;
    ZeroMemory(g_app.chats, sizeof(g_app.chats));

    if (AuthIsSignedIn()) {
        /* Load real chats from Graph */
        GraphFetchChats();
        if (g_app.chat_count == 0) {
            /* fallback demo data */
        } else {
            /* fetch messages for first chat */
            if (g_app.selected_chat >= 0 && g_app.selected_chat < g_app.chat_count) {
                GraphFetchMessages(g_app.chats[g_app.selected_chat].id);
            }
            GetLocalTime(&now);
            g_app.calendar_year = now.wYear;
            g_app.calendar_month = now.wMonth;
            g_app.calendar_day = now.wDay;
            g_app.selected_team = 0;
            g_app.selected_channel = 0;
            g_app.chat_filter = 0;
            g_app.chat_list_scroll = 0;
            g_app.message_scroll = 1000000;
            g_app.local_chat_sequence = 0;
            return;
        }
    }

    /* Demo data (offline) */
    /* ... keep existing demo data ... */

    AddChat("naoki", L"Naoki Sato", L"NS", RGB(112, 143, 176),
            FALSE, TRUE, TRUE);
    AddSampleMessage(0, FALSE, L"Naoki Sato", L"10:16",
                     L"お疲れさまです。来週のレビュー資料を共有しました。\n時間があるときに確認をお願いします。\n\n特に2ページ目のレイアウトを見てもらえると助かります。\nありがとうございます。\n\nこちらの案で進めてよさそうですか？\n確認お願いします。\n\n昨日のコメントも反映しておきました。\n必要なら午後に短く相談しましょう。\n\n最後の図だけ差し替えています。\nまた見てください。\n\n更新版を置いておきました。\n前のものよりシンプルになっています。\n\nお疲れさまです！");
    AddSampleMessage(0, TRUE, L"You", L"10:20",
                     L"ありがとう。午後の定例までに確認します。");
    AddSampleMessage(0, FALSE, L"Naoki Sato", L"10:24",
                     L"助かります。気になるところがあればコメントしてください。");
    AddSampleMessage(0, TRUE, L"You", L"10:28",
                     L"了解です。全体の流れも合わせて見ておきます。");

    AddChat("launch-team", L"Product launch", L"PL", RGB(91, 95, 199),
            TRUE, TRUE, FALSE);
    AddSampleMessage(1, FALSE, L"Aya Tanaka", L"10:42",
                     L"明日のチェックインは10時からです。更新した議題を投稿しました。");
    AddSampleMessage(1, FALSE, L"Leo Chen", L"10:44",
                     L"デザイン案を確認しました。細部は会議で相談しましょう。");

    AddChat("ux-team", L"UX Design", L"UX", RGB(157, 108, 183),
            TRUE, FALSE, FALSE);
    AddSampleMessage(2, FALSE, L"Mina Park", L"09:18",
                     L"ユーザーインタビューのメモをまとめました。");
    AddSampleMessage(2, TRUE, L"You", L"09:26",
                     L"ありがとう。共有フォルダーで確認します。");

    AddChat("alex", L"Alex Morgan", L"AM", RGB(91, 151, 131),
            FALSE, FALSE, TRUE);
    AddSampleMessage(3, FALSE, L"Alex Morgan", L"昨日",
                     L"The prototype looks great. Let's review the last flow tomorrow.");

    AddChat("it-help", L"IT Helpdesk", L"IT", RGB(190, 124, 77),
            TRUE, FALSE, FALSE);
    AddSampleMessage(4, FALSE, L"IT Helpdesk", L"火曜日",
                     L"お問い合わせを受け付けました。担当者から連絡します。");

    AddChat("weekly", L"Weekly sync", L"WS", RGB(77, 128, 164),
            TRUE, FALSE, FALSE);
    AddSampleMessage(5, FALSE, L"Riku Ito", L"月曜日",
                     L"週次ミーティングの議事メモです。アクション項目を確認してください。");

    g_app.chats[1].unread = 2;
    g_app.chats[4].unread = 1;
    g_app.selected_chat = 0;
    g_app.chat_filter = 0;
    g_app.chat_list_scroll = 0;
    g_app.message_scroll = 0;
    g_app.local_chat_sequence = 0;

    GetLocalTime(&now);
    g_app.calendar_year = now.wYear;
    g_app.calendar_month = now.wMonth;
    g_app.calendar_day = now.wDay;
    g_app.selected_team = 0;
    g_app.selected_channel = 0;

    DataLoadLocalMessages();
}

int DataCreateLocalChat(void)
{
    Chat *chat;
    char id[CHAT_ID_CAP];
    wchar_t title[CHAT_TITLE_CAP];
    unsigned long tick = (unsigned long)GetTickCount();
    int chat_index;

    if (g_app.chat_count >= MAX_CHATS) {
        return -1;
    }
    ++g_app.local_chat_sequence;
    snprintf(id, sizeof(id), "local-%08lX-%02d", tick,
             g_app.local_chat_sequence % 100);
    swprintf(title, _countof(title), L"New conversation %d",
             g_app.local_chat_sequence);
    chat = AddChat(id, title, L"NC", RGB(117, 105, 191),
                   FALSE, FALSE, FALSE);
    if (chat == NULL) {
        return -1;
    }
    chat_index = (int)(chat - g_app.chats);
    DataSaveLocalChat(chat);
    return chat_index;
}
