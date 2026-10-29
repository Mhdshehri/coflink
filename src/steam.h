/* steam.h - the Steamworks flat C API functions CoFLink uses, as a table of
 * function pointers. On Windows the table is filled from steam_api.dll with
 * GetProcAddress (steam_win.c); the Linux test build fills it with a mock
 * (steam_mock.c). Names, ids and struct offsets: coop-anything
 * playbook/networking-steam-spacewar.md. */
#pragma once
#include "mini.h"

typedef struct {
    int32_t user;
    int32_t callback;
    uint8_t *param;
    int32_t size;
} CallbackMsg;

typedef struct {
    int (*InitFlat)(char *err1024);
    bool (*IsSteamRunning)(void);
    void (*Shutdown)(void);
    int32_t (*GetHSteamPipe)(void);
    void (*MD_Init)(void);
    void (*MD_RunFrame)(int32_t pipe);
    bool (*MD_GetNextCallback)(int32_t pipe, CallbackMsg *msg);
    void (*MD_FreeLastCallback)(int32_t pipe);
    bool (*MD_GetAPICallResult)(int32_t pipe, u64 call, void *buf, int size, int cb, bool *failed);

    void *(*SteamUser)(void);
    void *(*SteamFriends)(void);
    void *(*SteamMatchmaking)(void);
    void *(*SteamNetMsgs)(void);
    void *(*SteamNetUtils)(void);
    void *(*SteamApps)(void);
    uint32_t (*Apps_GetAppInstallDir)(void *self, uint32_t app, char *buf, uint32_t n);
    int (*MM_AddFavoriteGame)(void *self, uint32_t app, uint32_t ip, uint16_t conn_port, uint16_t query_port,
                              uint32_t flags, uint32_t last_played);

    u64 (*User_GetSteamID)(void *self);
    bool (*User_BLoggedOn)(void *self);

    const char *(*Friends_GetPersonaName)(void *self);
    int (*Friends_GetFriendCount)(void *self, int flags);
    u64 (*Friends_GetFriendByIndex)(void *self, int i, int flags);
    int (*Friends_GetFriendPersonaState)(void *self, u64 id);
    const char *(*Friends_GetFriendPersonaName)(void *self, u64 id);
    bool (*Friends_GetFriendGamePlayed)(void *self, u64 id, void *info24);
    bool (*Friends_HasFriend)(void *self, u64 id, int flags);
    const char *(*Friends_GetFriendRichPresence)(void *self, u64 id, const char *key);
    void (*Friends_RequestFriendRichPresence)(void *self, u64 id);
    bool (*Friends_SetRichPresence)(void *self, const char *k, const char *v);
    void (*Friends_ClearRichPresence)(void *self);

    u64 (*MM_CreateLobby)(void *self, int type, int max);
    u64 (*MM_JoinLobby)(void *self, u64 lobby);
    void (*MM_LeaveLobby)(void *self, u64 lobby);
    bool (*MM_SetLobbyData)(void *self, u64 lobby, const char *k, const char *v);
    const char *(*MM_GetLobbyData)(void *self, u64 lobby, const char *k);
    u64 (*MM_GetLobbyOwner)(void *self, u64 lobby);
    int (*MM_GetNumLobbyMembers)(void *self, u64 lobby);
    u64 (*MM_GetLobbyMemberByIndex)(void *self, u64 lobby, int i);
    bool (*MM_InviteUserToLobby)(void *self, u64 lobby, u64 user);
    bool (*MM_SetLobbyJoinable)(void *self, u64 lobby, bool joinable);

    int (*NM_SendMessageToUser)(void *self, const void *ident, const void *data, uint32_t n, int flags, int ch);
    int (*NM_ReceiveMessagesOnChannel)(void *self, int ch, void **out, int max);
    bool (*NM_AcceptSessionWithUser)(void *self, const void *ident);
    bool (*NM_CloseSessionWithUser)(void *self, const void *ident);
    void (*Ident_SetSteamID64)(void *ident, u64 id);
    u64 (*Ident_GetSteamID64)(void *ident);
    void (*Msg_Release)(void *msg);
    int (*NU_InitRelayNetworkAccess)(void *self);
} SteamApi;

extern SteamApi S;

/* Load the API. Returns NULL on success or an error message. */
const char *steam_load(void);

/* SteamNetworkingMessage_t accessors (layout differs per platform). */
const void *msg_data(void *m);
int msg_size(void *m);
u64 msg_sender(void *m);

/* Callback ids */
enum {
    CB_GameLobbyJoinRequested = 333,
    CB_GameRichPresenceJoinRequested = 337,
    CB_LobbyInvite = 503,   /* someone invited you (not accepted yet) */
    CB_LobbyEnter = 504,
    CB_LobbyChatUpdate = 506,
    CB_LobbyCreated = 513,
    CB_SteamAPICallCompleted = 703,
    CB_NetMsgSessionRequest = 1251,
    CB_NetMsgSessionFailed = 1252,
};

enum { SEND_UNRELIABLE = 0, SEND_NONAGLE = 1, SEND_RELIABLE = 8 };
enum { LOBBY_FRIENDS_ONLY = 1 };
enum { FRIEND_IMMEDIATE = 4 };
enum { CHAT_LEFT = 0x2, CHAT_DISCONNECTED = 0x4, CHAT_KICKED = 0x8, CHAT_BANNED = 0x10 };

#define IDENT_SIZE 160 /* SteamNetworkingIdentity is 136 bytes; spare room */
