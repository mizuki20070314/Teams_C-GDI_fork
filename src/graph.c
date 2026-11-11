#include "graph.h"

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* HTTP GET helper                                                     */
/* ------------------------------------------------------------------ */

typedef struct GraphResponse {
    char *body;
    size_t body_len;
    DWORD status;
} GraphResponse;

static GraphResponse GraphGet(const wchar_t *path)
{
    GraphResponse resp;
    ZeroMemory(&resp, sizeof(resp));

    if (g_auth.access_token[0] == L'\0') return resp;

    HINTERNET session = WinHttpOpen(L"TeamsCGDI/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return resp;

    HINTERNET connect = WinHttpConnect(session, L"graph.microsoft.com",
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { WinHttpCloseHandle(session); return resp; }

    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path,
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return resp;
    }

    {
        wchar_t auth_header[4200];
        swprintf(auth_header, 4200, L"Authorization: Bearer %s\r\n", g_auth.access_token);
        WinHttpAddRequestHeaders(request, auth_header, (ULONG)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }
    WinHttpAddRequestHeaders(request, L"Accept: application/json\r\n", (ULONG)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        resp.status = status;

        size_t cap = 8192, used = 0;
        char *buf = (char *)malloc(cap);
        if (buf) {
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
                if (used + avail + 1 > cap) {
                    cap = used + avail + 4096;
                    char *nb = (char *)realloc(buf, cap);
                    if (!nb) break;
                    buf = nb;
                }
                DWORD rd = 0;
                if (!WinHttpReadData(request, buf + used, avail, &rd)) break;
                used += rd;
                if (rd == 0) break;
            }
            buf[used] = '\0';
            resp.body = buf;
            resp.body_len = used;
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return resp;
}

static void FreeGraphResponse(GraphResponse *r)
{
    if (r && r->body) { free(r->body); r->body = NULL; r->body_len = 0; }
}

/* ------------------------------------------------------------------ */
/* Minimal JSON array iteration                                        */
/* ------------------------------------------------------------------ */

/* Find the next object in a JSON array after `start`. Returns pointer
   to '{' or NULL. */
static const char *FindNextObject(const char *p)
{
    if (!p) return NULL;
    while (*p && *p != '{') ++p;
    return *p == '{' ? p : NULL;
}

/* Extract a string field from a JSON object (bounded by braces). */
static BOOL ObjGetString(const char *obj, const char *key, char *out, size_t cap)
{
    /* find matching closing brace */
    const char *end = obj;
    int depth = 0;
    BOOL in_str = FALSE;
    const char *p;

    /* find end of object */
    for (p = obj; *p; ++p) {
        if (in_str) {
            if (*p == '\\') { ++p; continue; }
            if (*p == '"') in_str = FALSE;
        } else {
            if (*p == '"') in_str = TRUE;
            else if (*p == '{') ++depth;
            else if (*p == '}') { --depth; if (depth == 0) { end = p; break; } }
        }
    }

    /* search key within [obj, end] */
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *k = strstr(obj, search);
    if (!k || k > end) return FALSE;
    k = strchr(k, ':');
    if (!k || k > end) return FALSE;
    ++k;
    while (k < end && (*k == ' ' || *k == '\t' || *k == '\n' || *k == '\r')) ++k;
    if (k >= end || *k != '"') return FALSE;
    ++k;
    const char *start = k;
    while (k < end && *k) {
        if (*k == '\\') { ++k; if (k < end) ++k; continue; }
        if (*k == '"') break;
        ++k;
    }
    size_t len = (size_t)(k - start);
    if (len >= cap) len = cap - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return TRUE;
}

static int ObjGetInt(const char *obj, const char *key)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *k = strstr(obj, search);
    if (!k) return 0;
    k = strchr(k, ':');
    if (!k) return 0;
    ++k;
    while (*k == ' ' || *k == '\t' || *k == '\n' || *k == '\r') ++k;
    return atoi(k);
}

/* ------------------------------------------------------------------ */
/* Color from string hash                                              */
/* ------------------------------------------------------------------ */

static COLORREF ColorFromId(const char *id)
{
    unsigned int h = 5381;
    const char *p = id;
    while (*p) { h = h * 33 + (unsigned char)*p; ++p; }
    int r = 80 + (h % 120);
    int g = 80 + ((h >> 8) % 120);
    int b = 80 + ((h >> 16) % 120);
    return RGB(r, g, b);
}

/* ------------------------------------------------------------------ */
/* Fetch chats                                                         */
/* ------------------------------------------------------------------ */

BOOL GraphFetchChats(void)
{
    if (!AuthIsSignedIn()) return FALSE;

    GraphResponse resp = GraphGet(L"/v1.0/me/chats?$top=50");
    if (resp.status != 200 || !resp.body) {
        FreeGraphResponse(&resp);
        return FALSE;
    }

    /* Reset chat list but keep local chats */
    g_app.chat_count = 0;

    const char *p = resp.body;
    /* find "value" array */
    const char *value = strstr(p, "\"value\"");
    if (!value) { FreeGraphResponse(&resp); return FALSE; }
    const char *arr = strchr(value, '[');
    if (!arr) { FreeGraphResponse(&resp); return FALSE; }
    ++arr;

    const char *cursor = arr;
    while (*cursor) {
        const char *obj = FindNextObject(cursor);
        if (!obj) break;
        /* ensure we don't go past the array end */
        /* find matching close */
        const char *end = obj;
        int depth = 0;
        BOOL in_str = FALSE;
        const char *q;
        for (q = obj; *q; ++q) {
            if (in_str) {
                if (*q == '\\') { ++q; continue; }
                if (*q == '"') in_str = FALSE;
            } else {
                if (*q == '"') in_str = TRUE;
                else if (*q == '{') ++depth;
                else if (*q == '}') { --depth; if (depth == 0) { end = q; break; } }
            }
        }
        if (depth != 0) break;

        /* extract fields */
        char id[128], topic[256], chat_type[64];
        id[0] = topic[0] = chat_type[0] = '\0';
        ObjGetString(obj, "id", id, sizeof(id));
        ObjGetString(obj, "topic", topic, sizeof(topic));
        ObjGetString(obj, "chatType", chat_type, sizeof(chat_type));

        if (id[0] != '\0' && g_app.chat_count < MAX_CHATS) {
            Chat *chat = &g_app.chats[g_app.chat_count++];
            ZeroMemory(chat, sizeof(*chat));
            /* store graph chat id in the id field (prefixed to distinguish) */
            snprintf(chat->id, CHAT_ID_CAP, "g:%s", id);
            /* title */
            if (topic[0]) {
                MultiByteToWideChar(CP_UTF8, 0, topic, -1, chat->title, CHAT_TITLE_CAP - 1);
                chat->title[CHAT_TITLE_CAP - 1] = L'\0';
            } else {
                /* use chat type as title */
                if (strcmp(chat_type, "oneOnOne") == 0) {
                    wcscpy(chat->title, L"Direct chat");
                } else {
                    wcscpy(chat->title, L"Group chat");
                }
            }
            /* initials */
            if (chat->title[0]) {
                chat->initials[0] = chat->title[0];
                chat->initials[1] = (chat->title[1] != L'\0') ? chat->title[1] : L'\0';
                chat->initials[2] = L'\0';
            } else {
                wcscpy(chat->initials, L"G");
            }
            chat->avatar_color = ColorFromId(id);
            chat->group = (strcmp(chat_type, "oneOnOne") != 0);
            chat->online = FALSE;
            chat->unread = 0;
            wcscpy(chat->preview, L"");
            wcscpy(chat->last_time, L"");
        }

        cursor = end + 1;
        /* skip to next object */
        while (*cursor && *cursor != '{' && *cursor != ']') ++cursor;
        if (*cursor == ']') break;
    }

    if (g_app.chat_count == 0) {
        /* fallback: keep demo data? No, show empty */
    }
    if (g_app.selected_chat >= g_app.chat_count) g_app.selected_chat = 0;
    if (g_app.chat_count > 0 && g_app.selected_chat < 0) g_app.selected_chat = 0;

    FreeGraphResponse(&resp);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Fetch messages                                                     */
/* ------------------------------------------------------------------ */

BOOL GraphFetchMessages(const char *chat_id)
{
    if (!AuthIsSignedIn() || !chat_id) return FALSE;

    /* strip "g:" prefix */
    const char *gid = chat_id;
    if (strncmp(gid, "g:", 2) == 0) gid += 2;

    char path[512];
    snprintf(path, sizeof(path), "/v1.0/me/chats/%s/messages?$top=50", gid);

    /* convert to wide path */
    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);

    GraphResponse resp = GraphGet(wpath);
    if (resp.status != 200 || !resp.body) {
        FreeGraphResponse(&resp);
        return FALSE;
    }

    /* find chat index */
    int chat_index = -1;
    int i;
    for (i = 0; i < g_app.chat_count; ++i) {
        if (strcmp(g_app.chats[i].id, chat_id) == 0) { chat_index = i; break; }
    }
    if (chat_index < 0) { FreeGraphResponse(&resp); return FALSE; }

    Chat *chat = &g_app.chats[chat_index];
    chat->message_count = 0;

    const char *value = strstr(resp.body, "\"value\"");
    if (!value) { FreeGraphResponse(&resp); return FALSE; }
    const char *arr = strchr(value, '[');
    if (!arr) { FreeGraphResponse(&resp); return FALSE; }
    ++arr;

    const char *cursor = arr;
    while (*cursor) {
        const char *obj = FindNextObject(cursor);
        if (!obj) break;
        const char *end = obj;
        int depth = 0;
        BOOL in_str = FALSE;
        const char *q;
        for (q = obj; *q; ++q) {
            if (in_str) {
                if (*q == '\\') { ++q; continue; }
                if (*q == '"') in_str = FALSE;
            } else {
                if (*q == '"') in_str = TRUE;
                else if (*q == '{') ++depth;
                else if (*q == '}') { --depth; if (depth == 0) { end = q; break; } }
            }
        }
        if (depth != 0) break;

        char msg_id[128], from_name[128], content[MESSAGE_TEXT_CAP * 2],
             created[64];
        msg_id[0] = from_name[0] = content[0] = created[0] = '\0';
        ObjGetString(obj, "id", msg_id, sizeof(msg_id));
        ObjGetString(obj, "createdDateTime", created, sizeof(created));
        /* from */
        const char *from = strstr(obj, "\"from\"");
        if (from && from < end) {
            char user_obj[512];
            /* find user displayName */
            const char *ud = strstr(from, "\"displayName\"");
            if (ud && ud < end) {
                /* extract */
                const char *c = strchr(ud, ':');
                if (c) {
                    ++c;
                    while (*c == ' ' || *c == '\t') ++c;
                    if (*c == '"') {
                        ++c;
                        const char *s = c;
                        while (*c && *c != '"') ++c;
                        size_t ll = (size_t)(c - s);
                        if (ll >= sizeof(from_name)) ll = sizeof(from_name) - 1;
                        memcpy(from_name, s, ll);
                        from_name[ll] = '\0';
                    }
                }
            }
        }
        /* body content */
        const char *body = strstr(obj, "\"body\"");
        if (body && body < end) {
            const char *ct = strstr(body, "\"content\"");
            if (ct && ct < end) {
                const char *c = strchr(ct, ':');
                if (c) {
                    ++c;
                    while (*c == ' ' || *c == '\t') ++c;
                    if (*c == '"') {
                        ++c;
                        const char *s = c;
                        /* handle escaped quotes */
                        while (*c) {
                            if (*c == '\\') { c += 2; continue; }
                            if (*c == '"') break;
                            ++c;
                        }
                        size_t ll = (size_t)(c - s);
                        if (ll >= sizeof(content)) ll = sizeof(content) - 1;
                        memcpy(content, s, ll);
                        content[ll] = '\0';
                    }
                }
            }
        }

        /* unescape content (convert \n etc) */
        {
            char unescaped[MESSAGE_TEXT_CAP * 2];
            size_t ui = 0;
            size_t ci = 0;
            while (content[ci] && ui + 1 < sizeof(unescaped)) {
                if (content[ci] == '\\' && content[ci+1]) {
                    char e = content[ci+1];
                    if (e == 'n') { unescaped[ui++] = '\n'; ci += 2; continue; }
                    if (e == 't') { unescaped[ui++] = '\t'; ci += 2; continue; }
                    if (e == 'r') { ci += 2; continue; }
                    if (e == '"') { unescaped[ui++] = '"'; ci += 2; continue; }
                    if (e == '\\') { unescaped[ui++] = '\\'; ci += 2; continue; }
                }
                unescaped[ui++] = content[ci++];
            }
            unescaped[ui] = '\0';
            memcpy(content, unescaped, ui + 1);
        }

        /* strip HTML tags if present */
        /* simple: if content contains '<', strip tags */
        {
            char stripped[MESSAGE_TEXT_CAP * 2];
            size_t si = 0;
            BOOL in_tag = FALSE;
            size_t ci2;
            for (ci2 = 0; content[ci2] && si + 1 < sizeof(stripped); ++ci2) {
                if (content[ci2] == '<') { in_tag = TRUE; continue; }
                if (content[ci2] == '>') { in_tag = FALSE; continue; }
                if (!in_tag) stripped[si++] = content[ci2];
            }
            stripped[si] = '\0';
            if (si > 0) memcpy(content, stripped, si + 1);
        }

        /* time from createdDateTime (extract HH:MM) */
        wchar_t time_w[16];
        time_w[0] = L'\0';
        if (created[0]) {
            /* format: 2024-...T10:20:30Z */
            const char *t = strchr(created, 'T');
            if (t && strlen(t) >= 6) {
                char hhmm[6];
                memcpy(hhmm, t + 1, 5);
                hhmm[5] = '\0';
                MultiByteToWideChar(CP_UTF8, 0, hhmm, -1, time_w, 16);
            }
        }
        if (time_w[0] == L'\0') wcscpy(time_w, L"now");

        /* determine if mine: compare from_name with our display name */
        BOOL mine = FALSE;
        if (from_name[0] == '\0') mine = FALSE;
        else {
            wchar_t from_w[128];
            MultiByteToWideChar(CP_UTF8, 0, from_name, -1, from_w, 128);
            if (g_auth.user_name[0] && wcscmp(from_w, g_auth.user_name) == 0) mine = TRUE;
            if (g_auth.user_email[0] && wcscmp(from_w, g_auth.user_email) == 0) mine = TRUE;
        }

        wchar_t content_w[MESSAGE_TEXT_CAP];
        MultiByteToWideChar(CP_UTF8, 0, content, -1, content_w, MESSAGE_TEXT_CAP);
        content_w[MESSAGE_TEXT_CAP - 1] = L'\0';

        wchar_t sender_w[64];
        if (from_name[0]) {
            MultiByteToWideChar(CP_UTF8, 0, from_name, -1, sender_w, 64);
        } else {
            wcscpy(sender_w, mine ? L"You" : L"Unknown");
        }

        if (chat->message_count < MAX_MESSAGES) {
            ChatMessage *m = &chat->messages[chat->message_count++];
            m->mine = mine;
            wcscpy(m->sender, sender_w);
            wcscpy(m->time, time_w);
            wcscpy(m->text, content_w);
        }

        /* update preview */
        if (content_w[0]) {
            wcsncpy(chat->preview, content_w, CHAT_PREVIEW_CAP - 1);
            chat->preview[CHAT_PREVIEW_CAP - 1] = L'\0';
            /* collapse newlines */
            for (i = 0; chat->preview[i]; ++i) {
                if (chat->preview[i] == L'\n') chat->preview[i] = L' ';
            }
        }
        if (time_w[0]) wcscpy(chat->last_time, time_w);

        cursor = end + 1;
        while (*cursor && *cursor != '{' && *cursor != ']') ++cursor;
        if (*cursor == ']') break;
    }

    FreeGraphResponse(&resp);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Send message                                                       */
/* ------------------------------------------------------------------ */

BOOL GraphSendMessage(const char *chat_id, const wchar_t *text)
{
    if (!AuthIsSignedIn() || !chat_id || !text) return FALSE;

    const char *gid = chat_id;
    if (strncmp(gid, "g:", 2) == 0) gid += 2;

    /* build JSON body */
    char text_utf8[MESSAGE_TEXT_CAP * 4];
    WideCharToMultiByte(CP_UTF8, 0, text, -1, text_utf8, sizeof(text_utf8), NULL, NULL);

    /* escape for JSON */
    char escaped[MESSAGE_TEXT_CAP * 8];
    size_t ei = 0;
    for (size_t i = 0; text_utf8[i] && ei + 8 < sizeof(escaped); ++i) {
        char c = text_utf8[i];
        if (c == '"') { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
        else if (c == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
        else if (c == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
        else if (c == '\r') { /* skip */ }
        else if (c == '\t') { escaped[ei++] = '\\'; escaped[ei++] = 't'; }
        else escaped[ei++] = c;
    }
    escaped[ei] = '\0';

    char body[MESSAGE_TEXT_CAP * 8 + 64];
    snprintf(body, sizeof(body), "{\"body\":{\"content\":\"%s\"}}", escaped);

    /* build path */
    char path[512];
    snprintf(path, sizeof(path), "/v1.0/me/chats/%s/messages", gid);
    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);

    /* POST */
    HINTERNET session = WinHttpOpen(L"TeamsCGDI/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return FALSE;
    HINTERNET connect = WinHttpConnect(session, L"graph.microsoft.com",
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { WinHttpCloseHandle(session); return FALSE; }
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", wpath,
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return FALSE;
    }
    {
        wchar_t auth_header[4200];
        swprintf(auth_header, 4200, L"Authorization: Bearer %s\r\n", g_auth.access_token);
        WinHttpAddRequestHeaders(request, auth_header, (ULONG)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }
    WinHttpAddRequestHeaders(request, L"Content-Type: application/json\r\n", (ULONG)-1, WINHTTP_ADDREQ_FLAG_ADD);

    DWORD body_len = (DWORD)strlen(body);
    BOOL ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 (LPVOID)body, body_len, body_len, 0);
    if (ok) ok = WinHttpReceiveResponse(request, NULL);

    DWORD status = 0;
    if (ok) {
        DWORD sz = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    return (status >= 200 && status < 300);
}
