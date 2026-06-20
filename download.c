#include "download.h"
#include <windows.h>
#include <urlmon.h>
#include <stdio.h>

int Net_Download(const char *url, const char *outFile)
{
    HRESULT hr = URLDownloadToFileA(NULL, url, outFile, 0, NULL);
    return (hr == S_OK);
}

int Net_Unzip(const char *zipFile, const char *destDir)
{
    char cmd[2048];
    snprintf(cmd, sizeof cmd,
        "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"Expand-Archive -Force -LiteralPath '%s' -DestinationPath '%s'\"",
        zipFile, destDir);
    int r = system(cmd);
    return (r == 0);
}
