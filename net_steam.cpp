// Steamworks integration for CS SURF.
//
// Kept entirely separate from the raylib/C side (the SDK is C++ and its type
// names collide with raylib's). The C game talks to this through net.h only.
//
// Flow:
//   * On init the host creates a friends-only lobby and sets rich presence, so
//     the Steam overlay (Shift+Tab -> a friend -> "Invite to Game") can invite.
//   * When a friend accepts, GameLobbyJoinRequested fires on their client; they
//     join the lobby, read the host's SteamID + current map, and open a P2P
//     session by sending HELLO. They then enter spectate and render the host's
//     streamed camera. ESC -> Net_LeaveSpectate() sends BYE and tears it down.
//   * The host streams its eye position + yaw/pitch to every spectator each
//     frame over ISteamNetworkingMessages (unreliable, channel 0).

#include "steam_api.h"
#include "isteammatchmaking.h"
#include "isteamfriends.h"
#include "isteamuser.h"
#include "isteamnetworkingmessages.h"
#include "isteamnetworkingutils.h"

#include "net.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <vector>

// Step logging to the game's terminal so a crash points at the last line.
static void NLog(const char *fmt, ...) {
    va_list a; va_start(a, fmt);
    printf("[net] "); vprintf(fmt, a); printf("\n"); fflush(stdout);
    va_end(a);
}

#define NET_CHANNEL 0

#pragma pack(push, 1)
struct Pkt {
    uint8_t type;                 // 1 = VIEW, 2 = HELLO, 3 = BYE
    float   x, y, z, yaw, pitch;  // only meaningful for VIEW
};
#pragma pack(pop)
enum { PKT_VIEW = 1, PKT_HELLO = 2, PKT_BYE = 3 };

class Net {
public:
    bool     m_ok        = false;
    uint64_t m_hostLobby = 0;     // lobby we created as host
    char     m_mapName[64] = "";

    // host side
    std::vector<uint64_t> m_specs;

    // spectator side
    uint64_t m_specHost     = 0;
    uint64_t m_joinLobby    = 0;
    bool     m_specPending  = false;
    char     m_specMap[64]  = "";
    bool     m_haveView     = false;
    bool     m_viewDirty    = false;
    bool     m_loggedView   = false;
    double   m_lastView     = 0.0;
    double   m_lastHello    = -1.0;
    Pkt      m_view         = {};

    Net() {}

    static SteamNetworkingIdentity Ident(uint64_t id) {
        SteamNetworkingIdentity n; n.Clear(); n.SetSteamID64(id); return n;
    }

    void CreateHostLobby() {
        if (SteamMatchmaking())
            SteamMatchmaking()->CreateLobby(k_ELobbyTypeFriendsOnly, 8);
    }

    void Advertise() {
        if (m_hostLobby && SteamMatchmaking())
            SteamMatchmaking()->SetLobbyData(CSteamID(m_hostLobby), "map", m_mapName);
    }

    void AddSpec(uint64_t id) {
        for (size_t i = 0; i < m_specs.size(); i++) if (m_specs[i] == id) return;
        m_specs.push_back(id);
    }
    void DropSpec(uint64_t id) {
        for (size_t i = 0; i < m_specs.size(); i++)
            if (m_specs[i] == id) { m_specs.erase(m_specs.begin() + i); return; }
    }

    void Send(uint64_t to, const Pkt &p, int len, int flags) {
        if (SteamNetworkingMessages_SteamAPI())
            SteamNetworkingMessages_SteamAPI()->SendMessageToUser(Ident(to), &p, len, flags, NET_CHANNEL);
    }

    void Pump() {
        ISteamNetworkingMessages *m = SteamNetworkingMessages_SteamAPI();
        if (!m) return;
        SteamNetworkingMessage_t *in[16];
        int n = m->ReceiveMessagesOnChannel(NET_CHANNEL, in, 16);
        for (int i = 0; i < n; i++) {
            uint64_t peer = in[i]->m_identityPeer.GetSteamID64();
            if (in[i]->m_cbSize >= 1) {
                const uint8_t *d = (const uint8_t *)in[i]->m_pData;
                if (d[0] == PKT_HELLO)      AddSpec(peer);
                else if (d[0] == PKT_BYE)   DropSpec(peer);
                else if (d[0] == PKT_VIEW && in[i]->m_cbSize >= (int)sizeof(Pkt)) {
                    memcpy(&m_view, in[i]->m_pData, sizeof(Pkt));
                    m_haveView = true;
                    m_viewDirty = true;
                }
            }
            in[i]->Release();
        }
    }

    STEAM_CALLBACK(Net, OnLobbyCreated,  LobbyCreated_t);
    STEAM_CALLBACK(Net, OnLobbyEnter,    LobbyEnter_t);
    STEAM_CALLBACK(Net, OnJoinRequested, GameLobbyJoinRequested_t);
    STEAM_CALLBACK(Net, OnSessionReq,    SteamNetworkingMessagesSessionRequest_t);
    STEAM_CALLBACK(Net, OnSessionFail,   SteamNetworkingMessagesSessionFailed_t);
};

void Net::OnLobbyCreated(LobbyCreated_t *p) {
    if (p->m_eResult != k_EResultOK) { NLog("host lobby create failed (%d)", (int)p->m_eResult); return; }
    m_hostLobby = p->m_ulSteamIDLobby;
    NLog("host lobby created %llu", (unsigned long long)m_hostLobby);
    Advertise();
    if (SteamFriends()) {
        SteamFriends()->SetRichPresence("status", "Surfing");
        char c[64];
        snprintf(c, sizeof c, "+connect_lobby %llu", (unsigned long long)m_hostLobby);
        SteamFriends()->SetRichPresence("connect", c);
    }
}

void Net::OnJoinRequested(GameLobbyJoinRequested_t *p) {
    NLog("invite accepted -> joining lobby %llu", (unsigned long long)p->m_steamIDLobby.ConvertToUint64());
    if (SteamMatchmaking())
        SteamMatchmaking()->JoinLobby(p->m_steamIDLobby);
}

void Net::OnLobbyEnter(LobbyEnter_t *p) {
    uint64_t lobby = p->m_ulSteamIDLobby;
    if (lobby == m_hostLobby) { NLog("entered own host lobby (ignore)"); return; }
    m_joinLobby = lobby;
    m_specHost  = SteamMatchmaking() ? SteamMatchmaking()->GetLobbyOwner(CSteamID(lobby)).ConvertToUint64() : 0;
    const char *map = SteamMatchmaking() ? SteamMatchmaking()->GetLobbyData(CSteamID(lobby), "map") : "";
    strncpy(m_specMap, map ? map : "", sizeof m_specMap - 1);
    m_specMap[sizeof m_specMap - 1] = 0;
    m_specPending = true;
    m_lastHello   = -1.0;   // HELLO is sent from Net_Update (off the callback stack)
    NLog("entered lobby %llu host=%llu map='%s'", (unsigned long long)lobby, (unsigned long long)m_specHost, m_specMap);
}

void Net::OnSessionReq(SteamNetworkingMessagesSessionRequest_t *p) {
    NLog("session request from %llu -> accept", (unsigned long long)p->m_identityRemote.GetSteamID64());
    if (SteamNetworkingMessages_SteamAPI())
        SteamNetworkingMessages_SteamAPI()->AcceptSessionWithUser(p->m_identityRemote);
}

void Net::OnSessionFail(SteamNetworkingMessagesSessionFailed_t *p) {
    NLog("session failed with %llu", (unsigned long long)p->m_info.m_identityRemote.GetSteamID64());
    DropSpec(p->m_info.m_identityRemote.GetSteamID64());
}

// ----------------------------- flat C API ---------------------------------

static Net   *g    = nullptr;
static double gNow = 0.0;   // seconds, advanced by Net_Update via a frame clock

extern "C" {

int Net_Init(void) {
    if (g) return g->m_ok ? 1 : 0;
    if (SteamAPI_RestartAppIfNecessary(k_uAppIdInvalid)) { /* dev build: ignore */ }
    if (!SteamAPI_Init()) return 0;
    g = new Net();
    g->m_ok = true;
    if (SteamNetworkingUtils()) SteamNetworkingUtils()->InitRelayNetworkAccess();
    g->CreateHostLobby();
    NLog("init ok");
    return 1;
}

void Net_Shutdown(void) {
    if (!g) return;
    if (g->m_ok) {
        if (g->m_specHost) { Pkt b; memset(&b, 0, sizeof b); b.type = PKT_BYE; g->Send(g->m_specHost, b, 1, k_nSteamNetworkingSend_Reliable); }
        SteamAPI_Shutdown();
    }
    delete g; g = nullptr;
}

void Net_Update(void) {
    if (!g || !g->m_ok) return;
    gNow += 1.0 / 60.0;           // coarse clock; only used for view-freshness
    SteamAPI_RunCallbacks();
    g->Pump();
    // Open/keep the session to the host from the main loop (never inside a
    // Steam callback). Resend HELLO until the host's view starts flowing.
    if (g->m_specHost && !g->m_haveView && (gNow - g->m_lastHello) > 0.4) {
        Pkt h; memset(&h, 0, sizeof h); h.type = PKT_HELLO;
        g->Send(g->m_specHost, h, 1, k_nSteamNetworkingSend_Reliable);
        g->m_lastHello = gNow;
        NLog("HELLO -> host %llu", (unsigned long long)g->m_specHost);
    }
    if (g->m_viewDirty) {
        g->m_lastView = gNow; g->m_viewDirty = false;
        if (!g->m_loggedView) { g->m_loggedView = true; NLog("receiving host view"); }
    }
}

int  Net_Available(void)       { return (g && g->m_ok) ? 1 : 0; }
const char *Net_SelfName(void) { return (g && g->m_ok && SteamFriends()) ? SteamFriends()->GetPersonaName() : ""; }

void Net_SetMap(const char *mapName) {
    if (!g || !g->m_ok) return;
    strncpy(g->m_mapName, mapName ? mapName : "", sizeof g->m_mapName - 1);
    g->m_mapName[sizeof g->m_mapName - 1] = 0;
    g->Advertise();
}

void Net_BroadcastView(float x, float y, float z, float yaw, float pitch) {
    if (!g || !g->m_ok || g->m_specs.empty()) return;
    Pkt p; p.type = PKT_VIEW; p.x = x; p.y = y; p.z = z; p.yaw = yaw; p.pitch = pitch;
    for (size_t i = 0; i < g->m_specs.size(); i++)
        g->Send(g->m_specs[i], p, sizeof p, k_nSteamNetworkingSend_UnreliableNoDelay);
}

int Net_SpectatorCount(void) { return (g && g->m_ok) ? (int)g->m_specs.size() : 0; }

int Net_PollSpectateStart(char *mapOut, int cap) {
    if (!g || !g->m_ok || !g->m_specPending) return 0;
    g->m_specPending = false;
    if (mapOut && cap > 0) { strncpy(mapOut, g->m_specMap, cap - 1); mapOut[cap - 1] = 0; }
    return 1;
}

int Net_GetView(float *x, float *y, float *z, float *yaw, float *pitch) {
    if (!g || !g->m_ok || !g->m_haveView) return 0;
    if ((gNow - g->m_lastView) > 3.0) return 0; // stale: host stopped sending
    if (x)     *x = g->m_view.x;
    if (y)     *y = g->m_view.y;
    if (z)     *z = g->m_view.z;
    if (yaw)   *yaw = g->m_view.yaw;
    if (pitch) *pitch = g->m_view.pitch;
    return 1;
}

const char *Net_HostName(void) {
    if (!g || !g->m_ok || !g->m_specHost || !SteamFriends()) return "host";
    return SteamFriends()->GetFriendPersonaName(CSteamID(g->m_specHost));
}

void Net_LeaveSpectate(void) {
    if (!g || !g->m_ok) return;
    if (g->m_specHost) {
        Pkt b; memset(&b, 0, sizeof b); b.type = PKT_BYE;
        g->Send(g->m_specHost, b, 1, k_nSteamNetworkingSend_Reliable);
        if (SteamNetworkingMessages_SteamAPI())
            SteamNetworkingMessages_SteamAPI()->CloseSessionWithUser(Net::Ident(g->m_specHost));
    }
    if (g->m_joinLobby && SteamMatchmaking())
        SteamMatchmaking()->LeaveLobby(CSteamID(g->m_joinLobby));
    g->m_specHost = 0; g->m_joinLobby = 0; g->m_haveView = false;
    g->m_specPending = false; g->m_specMap[0] = 0;
    g->m_loggedView = false; g->m_lastHello = -1.0;
    NLog("left spectate");
}

} // extern "C"
