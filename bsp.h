#ifndef BSP_H
#define BSP_H

typedef struct {
    float  *pos;
    float  *nrm;
    float  *uv;
    int     triCount;
    float   spawn[3];
    int     hasSpawn;
    int     ok;
} BspData;

int  Bsp_Load(const char *path, BspData *out);
void Bsp_Free(BspData *out);

#endif
