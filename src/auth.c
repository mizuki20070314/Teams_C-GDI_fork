#include "auth.h"

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

AuthSession g_auth;

static wchar_t g_client_id[128];

static void CopyWide(wchar_t *dst, size_t cap, const wchar_t *src)
{
    if (cap == 0) return;
    if (src == NULL) { dst[0] = L'\0'; return; }
    wcsncpy(dst, src, cap - 1);
    dst[cap - 1] = L'\0';
}

static void WideToUtf8Buf(const wchar_t *src, char *dst, int cap)
{
    if (src == NULL || dst == NULL || cap <= 0) { if (dst && cap>0) dst[0]='\0'; return; }
    WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, cap, NULL, NULL);
    dst[cap-1] = '\0';
}

static void Utf8ToWideBuf(const char *src, int len, wchar_t *dst, size_t cap)
{
    if (src == NULL || dst == NULL || cap == 0) return;
    if (len < 0) len = (int)strlen(src);
    MultiByteToWideChar(CP_UTF8, 0, src, len, dst, (int)(cap - 1));
    dst[cap-1] = L'\0';
}

/* ------------------------------------------------------------------ */
/* HTTP helper using WinHTTP                                            */
/* ------------------------------------------------------------------ */

typedef struct HttpResponse {
    char *body;
    size_t body_len;
    DWORD status;
} HttpResponse;

static HttpResponse HttpPost(const wchar_t *host, const wchar_t *path,
                            const char *body, size_t body_len)
{
    HttpResponse resp;
    ZeroMemory(&resp, sizeof(resp));

    HINTERNET session = WinHttpOpen(L"TeamsCGDI/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return resp;

    HINTERNET connect = WinHttpConnect(session, host,
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { WinHttpCloseHandle(session); return resp; }

    HINTERNET request = WinHttpOpenRequest(connect, L"POST", path,
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return resp;
    }

    /* headers */
    WinHttpAddRequestHeaders(request,
        L"Content-Type: application/x-www-form-urlencoded\r\n",
        (ULONG)-1, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 (LPVOID)body, (DWORD)body_len, (DWORD)body_len, 0);
    if (ok) ok = WinHttpReceiveResponse(request, NULL);

    if (ok) {
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        resp.status = status;

        /* read body */
        size_t cap = 4096;
        size_t used = 0;
        char *buf = (char *)malloc(cap);
        if (buf) {
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                if (used + available + 1 > cap) {
                    cap = used + available + 4096;
                    char *nb = (char *)realloc(buf, cap);
                    if (!nb) break;
                    buf = nb;
                }
                DWORD read = 0;
                if (!WinHttpReadData(request, buf + used, available, &read)) break;
                used += read;
                if (read == 0) break;
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

static void FreeResponse(HttpResponse *r)
{
    if (r && r->body) { free(r->body); r->body = NULL; r->body_len = 0; }
}

/* ------------------------------------------------------------------ */
/* Minimal JSON string extraction                                      */
/* ------------------------------------------------------------------ */

static BOOL JsonGetString(const char *json, const char *key,
                          char *out, size_t out_cap)
{
    char search[128];
    const char *p;
    const char *start;
    const char *end;
    size_t len;

    if (!json || !key || !out || out_cap == 0) return FALSE;
    out[0] = '\0';
    snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(json, search);
    if (!p) return FALSE;
    p = strchr(p, ':');
    if (!p) return FALSE;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (*p != '"') return FALSE;
    ++p;
    start = p;
    /* find end, handling escapes */
    while (*p && *p != '"') {
        if (*p == '\\') ++p;
        if (*p) ++p;
    }
    if (*p != '"') return FALSE;
    end = p;
    len = (size_t)(end - start);
    if (len >= out_cap) len = out_cap - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    /* unescape minimal */
    {
        char tmp[4096];
        size_t ti = 0;
        size_t i;
        for (i = 0; i < len && ti + 1 < sizeof(tmp); ++i) {
            if (out[i] == '\\' && i + 1 < len) {
                char c = out[i+1];
                if (c == 'n') { tmp[ti++] = '\n'; ++i; }
                else if (c == 't') { tmp[ti++] = '\t'; ++i; }
                else if (c == 'r') { tmp[ti++] = '\r'; ++i; }
                else if (c == '"') { tmp[ti++] = '"'; ++i; }
                else if (c == '\\') { tmp[ti++] = '\\'; ++i; }
                else { tmp[ti++] = out[i]; }
            } else {
                tmp[ti++] = out[i];
            }
        }
        tmp[ti] = '\0';
        memcpy(out, tmp, ti + 1);
    }
    return TRUE;
}

static int JsonGetInt(const char *json, const char *key)
{
    char search[128];
    const char *p;
    if (!json || !key) return 0;
    snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(json, search);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return atoi(p);
}

/* ------------------------------------------------------------------ */
/* Token storage                                                       */
/* ------------------------------------------------------------------ */

static BOOL GetTokenPath(wchar_t *path, size_t cap)
{
    if (cap < MAX_PATH) return FALSE;
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                NULL, SHGFP_TYPE_CURRENT, path))) return FALSE;
    {
        wchar_t sub[MAX_PATH];
        swprintf(sub, MAX_PATH, L"%s\\TeamsCGDI", path);
        CreateDirectoryW(sub, NULL);
        swprintf(path, cap, L"%s\\TeamsCGDI\\auth.tsv", sub);
    }
    return TRUE;
}

void AuthLoadTokens(void)
{
    wchar_t path[MAX_PATH];
    HANDLE file;
    LARGE_INTEGER sz;
    DWORD read;
    char *buf;
    char access[4096], refresh[4096], email[256], name[256];

    if (!GetTokenPath(path, MAX_PATH)) return;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    if (!GetFileSizeEx(file, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 65536) {
        CloseHandle(file); return;
    }
    buf = (char *)malloc((size_t)sz.QuadPart + 1);
    if (!buf) { CloseHandle(file); return; }
    if (!ReadFile(file, buf, (DWORD)sz.QuadPart, &read, NULL)) {
        free(buf); CloseHandle(file); return;
    }
    CloseHandle(file);
    buf[read] = '\0';

    access[0] = refresh[0] = email[0] = name[0] = '\0';
    {
        char *line = strtok(buf, "\n");
        while (line) {
            /* parse key=value */
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                if (strcmp(line, "access") == 0) strncpy(access, eq+1, sizeof(access)-1);
                else if (strcmp(line, "refresh") == 0) strncpy(refresh, eq+1, sizeof(refresh)-1);
                else if (strcmp(line, "email") == 0) strncpy(email, eq+1, sizeof(email)-1);
                else if (strcmp(line, "name") == 0) strncpy(name, eq+1, sizeof(name)-1);
            }
            /* strip trailing \r */
            {
                size_t l = strlen(access); if (l && access[l-1]=='\r') access[l-1]='\0';
            }
            line = strtok(NULL, "\n");
        }
    }
    if (access[0]) {
        Utf8ToWideBuf(access, -1, g_auth.access_token, 4096);
        if (refresh[0]) Utf8ToWideBuf(refresh, -1, g_auth.refresh_token, 4096);
        if (email[0]) Utf8ToWideBuf(email, -1, g_auth.user_email, 256);
        if (name[0]) Utf8ToWideBuf(name, -1, g_auth.user_name, 128);
        g_auth.state = AUTH_SIGNED_IN;
    }
    free(buf);
}

void AuthSaveTokens(void)
{
    wchar_t path[MAX_PATH];
    HANDLE file;
    char access[4096], refresh[4096], email[256], name[256];
    char buf[12000];
    int len;
    DWORD written;

    if (!GetTokenPath(path, MAX_PATH)) return;
    WideToUtf8Buf(g_auth.access_token, access, sizeof(access));
    WideToUtf8Buf(g_auth.refresh_token, refresh, sizeof(refresh));
    WideToUtf8Buf(g_auth.user_email, email, sizeof(email));
    WideToUtf8Buf(g_auth.user_name, name, sizeof(name));

    len = snprintf(buf, sizeof(buf),
                   "access=%s\nrefresh=%s\nemail=%s\nname=%s\n",
                   access, refresh, email, name);
    if (len <= 0) return;

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, buf, (DWORD)len, &written, NULL);
    CloseHandle(file);
}

void AuthClearTokens(void)
{
    wchar_t path[MAX_PATH];
    if (GetTokenPath(path, MAX_PATH)) DeleteFileW(path);
    ZeroMemory(&g_auth, sizeof(g_auth));
    g_auth.state = AUTH_SIGNED_OUT;
}

void AuthSignOut(void)
{
    AuthClearTokens();
}

/* ------------------------------------------------------------------ */
/* Initialization                                                      */
/* ------------------------------------------------------------------ */

void AuthInitialize(void)
{
    ZeroMemory(&g_auth, sizeof(g_auth));
    g_auth.state = AUTH_SIGNED_OUT;

    /* client id from env or default */
    {
        wchar_t env[256];
        DWORD n = GetEnvironmentVariableW(L"TEAMS_CGDI_CLIENT_ID", env, 256);
        if (n > 0 && n < 256) CopyWide(g_client_id, 128, env);
        else if (DEFAULT_CLIENT_ID[0]) CopyWide(g_client_id, 128, DEFAULT_CLIENT_ID);
        else g_client_id[0] = L'\0';
    }

    AuthLoadTokens();
}

BOOL AuthIsSignedIn(void) { return g_auth.state == AUTH_SIGNED_IN; }
const wchar_t *AuthGetDisplayName(void) {
    if (g_auth.user_name[0]) return g_auth.user_name;
    if (g_auth.user_email[0]) return g_auth.user_email;
    return L"Signed in";
}
const wchar_t *AuthGetEmail(void) { return g_auth.user_email; }

/* ------------------------------------------------------------------ */
/* Device code flow                                                    */
/* ------------------------------------------------------------------ */

static void BuildFormBody(char *body, size_t cap, const char *fmt, ...)
{
    /* simple: just use vsnprintf */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, cap, fmt, ap);
    va_end(ap);
}

void AuthStartSignIn(void)
{
    char body[1024];
    char client_id_utf8[256];
    HttpResponse resp;

    if (g_client_id[0] == L'\0') {
        g_auth.state = AUTH_ERROR;
        CopyWide(g_auth.error_message, 256,
                 L"Client ID が設定されていません。環境変数 TEAMS_CGDI_CLIENT_ID を設定するか、auth.c の DEFAULT_CLIENT_ID を設定してください。");
        return;
    }

    WideToUtf8Buf(g_client_id, client_id_utf8, sizeof(client_id_utf8));

    /* URL-encode is minimal; client ids are GUID-like so safe */
    snprintf(body, sizeof(body),
             "client_id=%s&scope=%s",
             client_id_utf8,
             "User.Read%20Chat.Read%20Chat.ReadWrite%20offline_access");

    resp = HttpPost(L"login.microsoftonline.com",
                    L"/common/oauth2/v2.0/devicecode",
                    body, strlen(body));

    if (resp.status == 200 && resp.body) {
        char dc[512], uc[64], vu[256];
        int ei, iv;
        if (JsonGetString(resp.body, "device_code", dc, sizeof(dc)) &&
            JsonGetString(resp.body, "user_code", uc, sizeof(uc)) &&
            JsonGetString(resp.body, "verification_uri", vu, sizeof(vu))) {
            ei = JsonGetInt(resp.body, "expires_in");
            iv = JsonGetInt(resp.body, "interval");
            if (ei <= 0) ei = 900;
            if (iv <= 0) iv = 5;

            Utf8ToWideBuf(dc, -1, g_auth.device_code, 512);
            Utf8ToWideBuf(uc, -1, g_auth.user_code, 64);
            Utf8ToWideBuf(vu, -1, g_auth.verification_uri, 256);
            g_auth.expires_in = ei;
            g_auth.poll_interval_ms = (DWORD)iv * 1000;
            g_auth.poll_deadline_tick = GetTickCount() + (DWORD)ei * 1000;
            g_auth.state = AUTH_SIGNING_IN;
        } else {
            g_auth.state = AUTH_ERROR;
            CopyWide(g_auth.error_message, 256, L"デバイスコードの取得に失敗しました。");
        }
    } else {
        g_auth.state = AUTH_ERROR;
        if (resp.body) {
            char err[256];
            if (JsonGetString(resp.body, "error_description", err, sizeof(err))) {
                Utf8ToWideBuf(err, -1, g_auth.error_message, 256);
            } else {
                CopyWide(g_auth.error_message, 256, L"サインイン要求に失敗しました。");
            }
        } else {
            CopyWide(g_auth.error_message, 256, L"ネットワークエラーが発生しました。");
        }
    }
    FreeResponse(&resp);
}

void AuthPollSignIn(void)
{
    char body[2048];
    char client_id_utf8[256], device_code_utf8[512];
    HttpResponse resp;

    if (g_auth.state != AUTH_SIGNING_IN) return;
    if (GetTickCount() > g_auth.poll_deadline_tick) {
        g_auth.state = AUTH_ERROR;
        CopyWide(g_auth.error_message, 256, L"サインインがタイムアウトしました。");
        return;
    }

    WideToUtf8Buf(g_client_id, client_id_utf8, sizeof(client_id_utf8));
    WideToUtf8Buf(g_auth.device_code, device_code_utf8, sizeof(device_code_utf8));

    snprintf(body, sizeof(body),
             "grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code&client_id=%s&device_code=%s",
             client_id_utf8, device_code_utf8);

    resp = HttpPost(L"login.microsoftonline.com",
                    L"/common/oauth2/v2.0/token",
                    body, strlen(body));

    if (resp.status == 200 && resp.body) {
        char access[4096], refresh[4096];
        if (JsonGetString(resp.body, "access_token", access, sizeof(access))) {
            /* success */
            Utf8ToWideBuf(access, -1, g_auth.access_token, 4096);
            if (JsonGetString(resp.body, "refresh_token", refresh, sizeof(refresh))) {
                Utf8ToWideBuf(refresh, -1, g_auth.refresh_token, 4096);
            }
            /* fetch user profile */
            {
                /* simple: decode from token? skip; set placeholder name */
                CopyWide(g_auth.user_name, 128, L"Microsoft User");
            }
            g_auth.state = AUTH_SIGNED_IN;
            g_auth.device_code[0] = L'\0';
            g_auth.user_code[0] = L'\0';
            AuthSaveTokens();
        }
    } else if (resp.body) {
        char err[128];
        if (JsonGetString(resp.body, "error", err, sizeof(err))) {
            if (strcmp(err, "authorization_pending") == 0) {
                /* keep polling */
            } else if (strcmp(err, "slow_down") == 0) {
                g_auth.poll_interval_ms += 5000;
            } else {
                char desc[256];
                if (JsonGetString(resp.body, "error_description", desc, sizeof(desc))) {
                    Utf8ToWideBuf(desc, -1, g_auth.error_message, 256);
                } else {
                    Utf8ToWideBuf(err, -1, g_auth.error_message, 256);
                }
                g_auth.state = AUTH_ERROR;
            }
        }
    }
    FreeResponse(&resp);
}

void AuthUpdatePolling(void)
{
    static DWORD last_poll = 0;
    if (g_auth.state != AUTH_SIGNING_IN) return;
    if (last_poll == 0) last_poll = GetTickCount();
    if (GetTickCount() - last_poll >= g_auth.poll_interval_ms) {
        last_poll = GetTickCount();
        AuthPollSignIn();
    }
}

void AuthFetchProfile(void)
{
    if (g_auth.state != AUTH_SIGNED_IN || g_auth.access_token[0] == L'\0') return;

    /* Use WinHTTP to call Graph /me */
    {
        char token_utf8[4096];
        WideToUtf8Buf(g_auth.access_token, token_utf8, sizeof(token_utf8));

        HINTERNET session = WinHttpOpen(L"TeamsCGDI/1.0",
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) return;
        HINTERNET connect = WinHttpConnect(session, L"graph.microsoft.com",
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) { WinHttpCloseHandle(session); return; }
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", L"/v1.0/me",
                                               NULL, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE);
        if (!request) {
            WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return;
        }
        {
            wchar_t auth_header[4200];
            swprintf(auth_header, 4200, L"Authorization: Bearer %s\r\n",
                     g_auth.access_token);
            WinHttpAddRequestHeaders(request, auth_header, (ULONG)-1,
                                    WINHTTP_ADDREQ_FLAG_ADD);
        }
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               NULL, 0, 0, 0) &&
            WinHttpReceiveResponse(request, NULL)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                WINHTTP_NO_HEADER_INDEX);
            if (status == 200) {
                size_t cap = 4096, used = 0;
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
                    {
                        char dn[128], mail[256];
                        if (JsonGetString(buf, "displayName", dn, sizeof(dn))) {
                            Utf8ToWideBuf(dn, -1, g_auth.user_name, 128);
                        }
                        if (JsonGetString(buf, "mail", mail, sizeof(mail)) ||
                            JsonGetString(buf, "userPrincipalName", mail, sizeof(mail))) {
                            Utf8ToWideBuf(mail, -1, g_auth.user_email, 256);
                        }
                    }
                    free(buf);
                }
            }
        }
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
    }
    AuthSaveTokens();
}
