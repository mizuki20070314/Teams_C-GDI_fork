#ifndef TEAMS_C_GDI_AUTH_H
#define TEAMS_C_GDI_AUTH_H

#include "app.h"

/* ------------------------------------------------------------------ */
/* Authentication state                                                */
/* ------------------------------------------------------------------ */

typedef enum AuthState {
    AUTH_SIGNED_OUT = 0,
    AUTH_SIGNING_IN,
    AUTH_SIGNED_IN,
    AUTH_ERROR
} AuthState;

typedef struct AuthSession {
    AuthState state;
    wchar_t user_name[128];
    wchar_t user_email[256];
    wchar_t access_token[4096];
    wchar_t refresh_token[4096];
    wchar_t device_code[512];
    wchar_t user_code[64];
    wchar_t verification_uri[256];
    int expires_in;
    DWORD poll_interval_ms;
    DWORD poll_deadline_tick;
    wchar_t error_message[256];
} AuthSession;

extern AuthSession g_auth;

/* Client ID for the app registration. The user must create an app
   registration in Azure AD and put the client ID here or override at
   runtime via the TEAMS_CGDI_CLIENT_ID environment variable. */
#define DEFAULT_CLIENT_ID L""

void AuthInitialize(void);
void AuthFetchProfile(void);
void AuthStartSignIn(void);
void AuthPollSignIn(void);
void AuthSignOut(void);
void AuthLoadTokens(void);
void AuthSaveTokens(void);
void AuthClearTokens(void);
BOOL AuthIsSignedIn(void);
const wchar_t *AuthGetDisplayName(void);
const wchar_t *AuthGetEmail(void);
void AuthUpdatePolling(void);

#endif
