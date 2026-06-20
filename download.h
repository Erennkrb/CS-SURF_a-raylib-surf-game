#ifndef DOWNLOAD_H
#define DOWNLOAD_H

int Net_Download(const char *url, const char *outFile);
int Net_Unzip(const char *zipFile, const char *destDir);

#endif
