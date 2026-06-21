#include "console.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Single-producer (reader thread) / single-consumer (main loop) ring of
   command lines, guarded by a critical section. Kept in its own TU so that
   <windows.h> never meets raylib.h (their type names collide). */

#define CON_Q       16
#define CON_LINE    256

static char             gQ[CON_Q][CON_LINE];
static volatile int     gHead = 0, gTail = 0;
static CRITICAL_SECTION gCS;
static int              gStarted = 0;

static DWORD WINAPI ReaderThread(LPVOID arg)
{
    (void)arg;
    char buf[CON_LINE];
    while (fgets(buf, sizeof buf, stdin)) {
        EnterCriticalSection(&gCS);
        int next = (gHead + 1) % CON_Q;
        if (next != gTail) {            /* drop the line if the queue is full */
            snprintf(gQ[gHead], CON_LINE, "%s", buf);
            gHead = next;
        }
        LeaveCriticalSection(&gCS);
    }
    return 0;   /* stdin closed -> thread ends; process owns its lifetime */
}

void Con_Start(void)
{
    if (gStarted) return;
    gStarted = 1;
    InitializeCriticalSection(&gCS);
    HANDLE h = CreateThread(NULL, 0, ReaderThread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}

int Con_Poll(char *out, int cap)
{
    int got = 0;
    if (!gStarted) return 0;
    EnterCriticalSection(&gCS);
    if (gTail != gHead) {
        strncpy(out, gQ[gTail], cap - 1);
        out[cap - 1] = 0;
        gTail = (gTail + 1) % CON_Q;
        got = 1;
    }
    LeaveCriticalSection(&gCS);
    if (got) {
        int n = (int)strlen(out);
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
    }
    return got;
}
