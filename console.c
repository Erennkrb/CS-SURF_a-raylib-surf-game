#include "console.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* ----------------------- last-chance crash reporter ---------------------- */
static void CrashModLine(void *addr, char *out, int cap)
{
    HMODULE m = NULL;
    if (GetModuleHandleExA(0x4 /*FROM_ADDRESS*/ | 0x2 /*UNCHANGED_REFCOUNT*/,
                           (LPCSTR)addr, &m) && m) {
        char path[MAX_PATH] = "";
        GetModuleFileNameA(m, path, MAX_PATH);
        const char *base = strrchr(path, '\\');
        base = base ? base + 1 : path;
        snprintf(out, cap, "%s + 0x%llx", base,
                 (unsigned long long)((char *)addr - (char *)m));
    } else {
        snprintf(out, cap, "%p", addr);
    }
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *ep)
{
    void *addr = ep->ExceptionRecord->ExceptionAddress;
    unsigned long code = (unsigned long)ep->ExceptionRecord->ExceptionCode;
    char where[320];
    CrashModLine(addr, where, sizeof where);
    FILE *f = fopen("crash.txt", "w");
    printf("\n*** CRASH ***  code=0x%08lX  at %s\n", code, where);
    if (f) fprintf(f, "*** CRASH ***  code=0x%08lX  at %s\n", code, where);

    void *frames[24];
    USHORT n = RtlCaptureStackBackTrace(0, 24, frames, NULL);
    for (USHORT i = 0; i < n; i++) {
        char b[320];
        CrashModLine(frames[i], b, sizeof b);
        printf("  [%2u] %s\n", i, b);
        if (f) fprintf(f, "  [%2u] %s\n", i, b);
    }
    fflush(stdout);
    if (f) fclose(f);
    return EXCEPTION_EXECUTE_HANDLER;
}

void Con_InstallCrashHandler(void)
{
    SetUnhandledExceptionFilter(CrashFilter);
}

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
