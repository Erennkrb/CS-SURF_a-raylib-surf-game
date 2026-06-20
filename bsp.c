#include "bsp.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LUMP_ENTITIES 0
#define LUMP_TEXTURES 2
#define LUMP_VERTICES 3
#define LUMP_TEXINFO  6
#define LUMP_FACES    7
#define LUMP_EDGES    12
#define LUMP_SURFEDGES 13

static int rdI(const unsigned char *p) { int v; memcpy(&v, p, 4); return v; }
static unsigned short rdU16(const unsigned char *p) { unsigned short v; memcpy(&v, p, 2); return v; }
static float rdF(const unsigned char *p) { float v; memcpy(&v, p, 4); return v; }

static int IsSkipTexture(const char *name)
{
    char n[20];
    int i = 0;
    for (; name[i] && i < 16; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        n[i] = c;
    }
    n[i] = 0;
    if (strstr(n, "sky")) return 1;
    if (strstr(n, "aaatrigger")) return 1;
    if (strstr(n, "trigger")) return 1;
    if (strstr(n, "clip")) return 1;
    if (strstr(n, "origin")) return 1;
    if (strstr(n, "hint")) return 1;
    if (strstr(n, "skip")) return 1;
    if (strstr(n, "null")) return 1;
    return 0;
}

static void ParseSpawn(const char *ents, int len, float out[3], int *has)
{
    *has = 0;
    const char *keys[3] = { "info_player_terrorist", "info_player_start", "info_player_counterterrorist" };
    for (int k = 0; k < 3 && !*has; k++) {
        const char *p = ents;
        const char *end = ents + len;
        while (p < end) {
            const char *hit = strstr(p, keys[k]);
            if (!hit) break;
            const char *bopen = hit;
            while (bopen > ents && *bopen != '{') bopen--;
            const char *bclose = hit;
            while (bclose < end && *bclose != '}') bclose++;
            const char *o = bopen;
            const char *of = NULL;
            while (o < bclose) {
                if (strncmp(o, "\"origin\"", 8) == 0) { of = o; break; }
                o++;
            }
            if (of) {
                of += 8;
                while (of < bclose && *of != '"') of++;
                if (of < bclose) {
                    of++;
                    float x = 0, y = 0, z = 0;
                    if (sscanf(of, "%f %f %f", &x, &y, &z) == 3) {
                        out[0] = x; out[1] = z; out[2] = -y;
                        *has = 1;
                        return;
                    }
                }
            }
            p = hit + 1;
        }
    }
}

int Bsp_Load(const char *path, BspData *out)
{
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f) { TraceLog(LOG_WARNING, "BSP: cannot open %s", path); return 0; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 256) { fclose(f); return 0; }
    unsigned char *buf = (unsigned char *)malloc(size);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    fclose(f);

    int version = rdI(buf);
    if (version != 30 && version != 29) {
        TraceLog(LOG_WARNING, "BSP: unsupported version %d (need 30)", version);
        free(buf);
        return 0;
    }

    int lumpOfs[16], lumpLen[16];
    for (int i = 0; i < 15; i++) {
        lumpOfs[i] = rdI(buf + 4 + i * 8);
        lumpLen[i] = rdI(buf + 4 + i * 8 + 4);
    }

    const unsigned char *vtx = buf + lumpOfs[LUMP_VERTICES];
    int vtxCount = lumpLen[LUMP_VERTICES] / 12;
    const unsigned char *edg = buf + lumpOfs[LUMP_EDGES];
    int edgCount = lumpLen[LUMP_EDGES] / 4;
    const unsigned char *sfe = buf + lumpOfs[LUMP_SURFEDGES];
    int sfeCount = lumpLen[LUMP_SURFEDGES] / 4;
    const unsigned char *fac = buf + lumpOfs[LUMP_FACES];
    int facCount = lumpLen[LUMP_FACES] / 20;
    const unsigned char *tinf = buf + lumpOfs[LUMP_TEXINFO];
    int tinfCount = lumpLen[LUMP_TEXINFO] / 40;
    const unsigned char *texL = buf + lumpOfs[LUMP_TEXTURES];

    int numMip = (lumpLen[LUMP_TEXTURES] >= 4) ? rdI(texL) : 0;

    int totalTris = 0;
    for (int i = 0; i < facCount; i++) {
        int ne = rdU16(fac + i * 20 + 8);
        if (ne >= 3) totalTris += (ne - 2);
    }
    if (totalTris <= 0) { free(buf); return 0; }

    out->pos = (float *)malloc(sizeof(float) * 9 * totalTris);
    out->nrm = (float *)malloc(sizeof(float) * 9 * totalTris);
    out->uv  = (float *)malloc(sizeof(float) * 6 * totalTris);
    if (!out->pos || !out->nrm || !out->uv) { Bsp_Free(out); free(buf); return 0; }

    int tri = 0;
    int maxEdges = 64;
    float *vx = (float *)malloc(sizeof(float) * 3 * maxEdges);

    for (int i = 0; i < facCount; i++) {
        const unsigned char *F = fac + i * 20;
        int firstedge = rdI(F + 4);
        int numedges  = rdU16(F + 8);
        int texinfo   = rdU16(F + 10);
        if (numedges < 3 || numedges > maxEdges) continue;
        if (firstedge < 0 || firstedge + numedges > sfeCount) continue;

        int skip = 0;
        float sV[3] = {0,0,0}, tV[3] = {0,0,0}, sO = 0, tO = 0;
        if (texinfo >= 0 && texinfo < tinfCount) {
            const unsigned char *TI = tinf + texinfo * 40;
            sV[0] = rdF(TI + 0);  sV[1] = rdF(TI + 4);  sV[2] = rdF(TI + 8);  sO = rdF(TI + 12);
            tV[0] = rdF(TI + 16); tV[1] = rdF(TI + 20); tV[2] = rdF(TI + 24); tO = rdF(TI + 28);
            int miptex = rdI(TI + 32);
            if (miptex >= 0 && miptex < numMip) {
                int doff = rdI(texL + 4 + miptex * 4);
                if (doff >= 0 && doff + 16 <= lumpLen[LUMP_TEXTURES]) {
                    char name[17];
                    memcpy(name, texL + doff, 16);
                    name[16] = 0;
                    if (IsSkipTexture(name)) skip = 1;
                }
            }
        }
        if (skip) continue;

        for (int e = 0; e < numedges; e++) {
            int se = rdI(sfe + (firstedge + e) * 4);
            int vi;
            if (se >= 0) {
                if (se >= edgCount) { vi = 0; }
                else vi = rdU16(edg + se * 4 + 0);
            } else {
                int a = -se;
                if (a >= edgCount) { vi = 0; }
                else vi = rdU16(edg + a * 4 + 2);
            }
            if (vi < 0 || vi >= vtxCount) vi = 0;
            float bx = rdF(vtx + vi * 12 + 0);
            float by = rdF(vtx + vi * 12 + 4);
            float bz = rdF(vtx + vi * 12 + 8);
            vx[e * 3 + 0] = bx;
            vx[e * 3 + 1] = by;
            vx[e * 3 + 2] = bz;
        }

        for (int e = 1; e + 1 < numedges; e++) {
            int idx[3] = { 0, e, e + 1 };
            float P[3][3];
            float U[3][2];
            for (int c = 0; c < 3; c++) {
                float bx = vx[idx[c] * 3 + 0];
                float by = vx[idx[c] * 3 + 1];
                float bz = vx[idx[c] * 3 + 2];
                float uu = (bx * sV[0] + by * sV[1] + bz * sV[2] + sO) / 128.0f;
                float vv = (bx * tV[0] + by * tV[1] + bz * tV[2] + tO) / 128.0f;
                P[c][0] = bx;  P[c][1] = bz;  P[c][2] = -by;
                U[c][0] = uu;  U[c][1] = vv;
            }
            float ax = P[1][0]-P[0][0], ay = P[1][1]-P[0][1], az = P[1][2]-P[0][2];
            float dx = P[2][0]-P[0][0], dy = P[2][1]-P[0][1], dz = P[2][2]-P[0][2];
            float nx = ay*dz - az*dy, ny = az*dx - ax*dz, nz = ax*dy - ay*dx;
            float ln = sqrtf(nx*nx + ny*ny + nz*nz);
            if (ln < 1e-6f) ln = 1.0f;
            nx /= ln; ny /= ln; nz /= ln;
            for (int c = 0; c < 3; c++) {
                out->pos[tri*9 + c*3 + 0] = P[c][0];
                out->pos[tri*9 + c*3 + 1] = P[c][1];
                out->pos[tri*9 + c*3 + 2] = P[c][2];
                out->nrm[tri*9 + c*3 + 0] = nx;
                out->nrm[tri*9 + c*3 + 1] = ny;
                out->nrm[tri*9 + c*3 + 2] = nz;
                out->uv[tri*6 + c*2 + 0] = U[c][0];
                out->uv[tri*6 + c*2 + 1] = U[c][1];
            }
            tri++;
        }
    }
    free(vx);

    out->triCount = tri;
    ParseSpawn((const char *)(buf + lumpOfs[LUMP_ENTITIES]), lumpLen[LUMP_ENTITIES], out->spawn, &out->hasSpawn);
    out->ok = (tri > 0);

    TraceLog(LOG_INFO, "BSP: v%d, %d faces, %d tris, spawn=%d", version, facCount, tri, out->hasSpawn);

    free(buf);
    return out->ok;
}

void Bsp_Free(BspData *out)
{
    if (out->pos) free(out->pos);
    if (out->nrm) free(out->nrm);
    if (out->uv)  free(out->uv);
    out->pos = NULL; out->nrm = NULL; out->uv = NULL;
    out->triCount = 0; out->ok = 0;
}
