#ifndef TEAMS_C_GDI_GRAPH_H
#define TEAMS_C_GDI_GRAPH_H

#include "app.h"
#include "auth.h"

/* Fetch the signed-in user's chats from Microsoft Graph and populate
   g_app.chats. Returns TRUE on success. */
BOOL GraphFetchChats(void);

/* Fetch messages for a given chat id into the corresponding Chat.
   Returns TRUE on success. */
BOOL GraphFetchMessages(const char *chat_id);

/* Send a message to a chat. Returns TRUE on success. */
BOOL GraphSendMessage(const char *chat_id, const wchar_t *text);

#endif
