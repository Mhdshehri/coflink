/* steam_win.c - load steam_api.dll from CoFLink's own folder, but only the exact
 * pinned build (Steamworks.NET 2025.164.1, signed by Valve Corp.). Any other
 * file with that name is refused, so a swapped DLL can never run. */
#include "steam.h"

#define WINAPI __attribute__((stdcall))
#define IMP __declspec(dllimport)
IMP void *WINAPI LoadLibraryExA(const char *path, void *file, unsigned long flags);
IMP void *WINAPI GetProcAddress(void *mod, const char *name);

bool sha256_file(const char *path, char hex[65]);
const char *exe_dir(void);

#define PINNED_SHA256 "fdafb7c65ec7d4182b42bf92cc8671c68e4dc547cc6e613899d2618a47eb65da"
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x8

SteamApi S;

static void *g_dll;
static const char *g_missing;

static void *get(const char *name)
{
    void *p = GetProcAddress(g_dll, name);
    if (!p && !g_missing) g_missing = name;
    return p;
}

#define GET(field, name) (*(void **)&S.field = get(name))

const char *steam_load(void)
{
    static char path[700], err[900];
    sfmt(path, sizeof path, "%s\\steam_api.dll", exe_dir());
    char hex[65];
    if (!sha256_file(path, hex)) {
        sfmt(err, sizeof err, "steam_api.dll is missing. It must be in the same folder as CoFLink.exe (%s).", exe_dir());
        return err;
    }
    if (strcmp(hex, PINNED_SHA256) != 0) {
        sfmt(err, sizeof err,
             "steam_api.dll is not the expected file, so it was NOT loaded.\n"
             "  expected SHA-256 %s\n  found            %s\n"
             "Use the steam_api.dll that came in the CoFLink zip.", PINNED_SHA256, hex);
        return err;
    }
    /* Full path: Windows will not search other folders for it. */
    g_dll = LoadLibraryExA(path, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_dll) return "steam_api.dll could not be loaded.";

    GET(InitFlat, "SteamAPI_InitFlat");
    GET(IsSteamRunning, "SteamAPI_IsSteamRunning");
    GET(Shutdown, "SteamAPI_Shutdown");
    GET(GetHSteamPipe, "SteamAPI_GetHSteamPipe");
    GET(MD_Init, "SteamAPI_ManualDispatch_Init");
    GET(MD_RunFrame, "SteamAPI_ManualDispatch_RunFrame");
    GET(MD_GetNextCallback, "SteamAPI_ManualDispatch_GetNextCallback");
    GET(MD_FreeLastCallback, "SteamAPI_ManualDispatch_FreeLastCallback");
    GET(MD_GetAPICallResult, "SteamAPI_ManualDispatch_GetAPICallResult");
    GET(SteamUser, "SteamAPI_SteamUser_v023");
    GET(SteamFriends, "SteamAPI_SteamFriends_v018");
    GET(SteamMatchmaking, "SteamAPI_SteamMatchmaking_v009");
    GET(SteamNetMsgs, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
    GET(SteamNetUtils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    GET(SteamApps, "SteamAPI_SteamApps_v009");
    GET(Apps_GetAppInstallDir, "SteamAPI_ISteamApps_GetAppInstallDir");
    GET(User_GetSteamID, "SteamAPI_ISteamUser_GetSteamID");
    GET(User_BLoggedOn, "SteamAPI_ISteamUser_BLoggedOn");
    GET(Friends_GetPersonaName, "SteamAPI_ISteamFriends_GetPersonaName");
    GET(Friends_GetFriendCount, "SteamAPI_ISteamFriends_GetFriendCount");
    GET(Friends_GetFriendByIndex, "SteamAPI_ISteamFriends_GetFriendByIndex");
    GET(Friends_GetFriendPersonaState, "SteamAPI_ISteamFriends_GetFriendPersonaState");
    GET(Friends_GetFriendPersonaName, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    GET(Friends_GetFriendGamePlayed, "SteamAPI_ISteamFriends_GetFriendGamePlayed");
    GET(Friends_HasFriend, "SteamAPI_ISteamFriends_HasFriend");
    GET(Friends_GetFriendRichPresence, "SteamAPI_ISteamFriends_GetFriendRichPresence");
    GET(Friends_RequestFriendRichPresence, "SteamAPI_ISteamFriends_RequestFriendRichPresence");
    GET(Friends_SetRichPresence, "SteamAPI_ISteamFriends_SetRichPresence");
    GET(Friends_ClearRichPresence, "SteamAPI_ISteamFriends_ClearRichPresence");
    GET(MM_CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby");
    GET(MM_JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby");
    GET(MM_LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby");
    GET(MM_SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData");
    GET(MM_GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData");
    GET(MM_GetLobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    GET(MM_GetNumLobbyMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    GET(MM_GetLobbyMemberByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    GET(MM_SetLobbyJoinable, "SteamAPI_ISteamMatchmaking_SetLobbyJoinable");
    GET(NM_SendMessageToUser, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    GET(NM_ReceiveMessagesOnChannel, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    GET(NM_AcceptSessionWithUser, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    GET(NM_CloseSessionWithUser, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser");
    GET(Ident_SetSteamID64, "SteamAPI_SteamNetworkingIdentity_SetSteamID64");
    GET(Ident_GetSteamID64, "SteamAPI_SteamNetworkingIdentity_GetSteamID64");
    GET(Msg_Release, "SteamAPI_SteamNetworkingMessage_t_Release");
    GET(NU_InitRelayNetworkAccess, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
    GET(NU_SetGlobalConfigInt32, "SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32");
    if (g_missing) {
        sfmt(err, sizeof err, "steam_api.dll has no function %s.", g_missing);
        return err;
    }
    return 0;
}

/* SteamNetworkingMessage_t in the 32-bit steam_api.dll: data at 0, size at 4,
   sender identity at 12 (confirmed between two real accounts; the headers
   suggest 16, which reads sender 0). Try 12, then 16. */
const void *msg_data(void *m) { return *(void **)m; }
int msg_size(void *m) { return *(int *)((char *)m + 4); }
u64 msg_sender(void *m)
{
    u64 id = S.Ident_GetSteamID64((char *)m + 12);
    if (!id) id = S.Ident_GetSteamID64((char *)m + 16);
    return id;
}
