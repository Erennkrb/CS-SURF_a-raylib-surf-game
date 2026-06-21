#ifndef NET_H
#define NET_H

/* Steam networking façade. All Steamworks (C++) code lives in net_steam.cpp;
   the C game only ever sees this flat API. If Steam isn't running, Net_Init
   returns 0 and every call is an inert no-op so the game still works. */

#ifdef __cplusplus
extern "C" {
#endif

int         Net_Init(void);        /* 1 if Steam initialised, else 0          */
void        Net_Shutdown(void);
void        Net_Update(void);      /* run callbacks + pump packets, each frame */
int         Net_Available(void);   /* Steam up?                                */
const char *Net_SelfName(void);

/* ---- Host (sharing your view) ------------------------------------------ */
void Net_SetMap(const char *mapName);                       /* advertise current map */
void Net_BroadcastView(float x, float y, float z, float yaw, float pitch);
int  Net_SpectatorCount(void);

/* ---- Spectator (watching someone) -------------------------------------- */
int         Net_PollSpectateStart(char *mapOut, int cap);   /* 1 once when a join lands */
int         Net_GetView(float *x, float *y, float *z, float *yaw, float *pitch); /* 1 if fresh */
const char *Net_HostName(void);
void        Net_LeaveSpectate(void);

#ifdef __cplusplus
}
#endif

#endif
