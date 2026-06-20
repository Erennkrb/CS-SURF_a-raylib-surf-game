#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "settings.h"
#include "bsp.h"
#include "download.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SV_GRAVITY        800.0f
#define SV_MAXSPEED       320.0f
#define SV_ACCELERATE      10.0f
#define SV_AIRACCELERATE  150.0f
#define SV_FRICTION         5.0f
#define SV_STOPSPEED      100.0f
#define AIR_CAP            30.0f
#define JUMP_SPEED        290.0f

#define PLAYER_RADIUS      24.0f
#define EYE_OFFSET         26.0f
#define GROUND_NY           0.70f

#define MAXS    400
#define NP      5
#define MAXTRI  7000
#define MAXCP   40

#define WX 520.0f
#define HY 640.0f
#define TILE 300.0f

typedef struct { Vector3 c, right, up, tan; } Frame;
typedef struct { Vector3 a, b, c, n, mid; } Tri;
typedef enum { ST_MENU, ST_MAPSELECT, ST_ONLINE, ST_OPTIONS, ST_GAME, ST_PAUSE } GameState;

#define MAX_ONLINE 2500

static Frame   frames[MAXS];
static Vector3 ptW[MAXS][NP];
static int     frameCount = 0;
static float   profA[NP] = { -1.00f, -0.45f, 0.0f, 0.45f, 1.00f };
static float   profB[NP] = {  1.05f,  0.55f, 0.0f, 0.55f, 1.05f };
static float   profCum[NP];
static float   pathCum[MAXS];
static Tri     tris[MAXTRI];
static int     triCount = 0;
static float   gKillY = -9999.0f;

static int     cpIdx[MAXCP];
static int     cpCount = 0;
static int     cpPassed = 0;
static double  cpSplit[MAXCP];

static Vector3 spawnPos;
static float   spawnYaw;
static Vector3 camPosGlobal = { 0 };

static Vector3 pPos, pVel;
static float   pYaw, pPitch;
static int     pGround;
static double  runStart;
static int     finished;
static double  finishTime;
static float   topSpeed;

static Color SKY_TOP    = { 60, 90, 140, 255 };
static Color SKY_BOTTOM = { 165, 185, 210, 255 };
static Color FOG_COLOR  = { 165, 185, 210, 255 };
#define FOG_START 4500.0f
#define FOG_END   20000.0f

static int   gAudio = 0;
static Sound sndClick, sndJump, sndFinish, sndWind;
static Texture2D gConcrete, gPfp;
static int gTexReady = 0, gPfpReady = 0;

static Model   gWorldModel;
static int     gUseBsp = 0;
static Tri    *wTris = NULL;
static int     wTriCount = 0;
static int    *gCellStart = NULL, *gCellItems = NULL, *wStamp = NULL, wStampCur = 0;
static float   gMinX = 0, gMinZ = 0, gCell = 256.0f;
static int     gNX = 0, gNZ = 0;
static Vector3 gWorldCenter = { 0 };
static float   gOrbitR = 2600.0f;

static Shader gLight;
static int gLightOK = 0, gLocView = -1;
static int selectedMap = 0;
static FilePathList gMaps = { 0 };
static int gMapsLoaded = 0;
static int gScroll = 0;

static char gOnline[MAX_ONLINE][48];
static int  gOnlineCount = 0;
static int  gOnlineFetched = 0;
static int  gOnlineScroll = 0;
static char gStatus[160] = "";

static Color stripePalette[5] = {
    { 200, 70, 45, 255 }, { 235, 140, 50, 255 }, { 238, 228, 195, 255 },
    { 120, 210, 170, 255 }, { 70, 145, 210, 255 }
};

static Vector3 CP[] = {
    {     0, 1200,  -400 },
    {     0, 1150,   200 },
    {     0, 1050,   900 },
    {   500,  950,  1700 },
    {   900,  850,  2600 },
    {   600,  760,  3500 },
    {  -100,  690,  4200 },
    {  -800,  610,  4900 },
    { -1100,  520,  5800 },
    {  -700,  440,  6800 },
    {     0,  370,  7500 },
    {   700,  300,  8300 },
    {  1000,  230,  9300 },
    {   500,  170, 10200 },
    {  -200,  120, 11000 },
    {  -200,   90, 11600 },
};
#define CP_COUNT ((int)(sizeof(CP) / sizeof(CP[0])))

static Sound MakeBeep(float f0, float f1, float dur, float vol)
{
    int sr = 44100, n = (int)(sr * dur);
    short *d = (short *)malloc(sizeof(short) * n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr, p = (float)i / n;
        float f = f0 + (f1 - f0) * p;
        d[i] = (short)(sinf(2.0f * PI * f * t) * sinf(PI * p) * vol * 32767.0f);
    }
    Wave w; w.frameCount = n; w.sampleRate = sr; w.sampleSize = 16; w.channels = 1; w.data = d;
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}
static Sound MakeWind(float dur, float vol)
{
    int sr = 44100, n = (int)(sr * dur);
    short *d = (short *)malloc(sizeof(short) * n);
    float last = 0.0f;
    for (int i = 0; i < n; i++) {
        float white = (float)GetRandomValue(-1000, 1000) / 1000.0f;
        last = last * 0.96f + white * 0.04f;
        d[i] = (short)(last * vol * 32767.0f);
    }
    Wave w; w.frameCount = n; w.sampleRate = sr; w.sampleSize = 16; w.channels = 1; w.data = d;
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}
static void PlayClick(void) { if (gAudio) PlaySound(sndClick); }

static Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float t2 = t * t, t3 = t2 * t;
    Vector3 r;
    r.x = 0.5f * ((2*p1.x) + (-p0.x+p2.x)*t + (2*p0.x-5*p1.x+4*p2.x-p3.x)*t2 + (-p0.x+3*p1.x-3*p2.x+p3.x)*t3);
    r.y = 0.5f * ((2*p1.y) + (-p0.y+p2.y)*t + (2*p0.y-5*p1.y+4*p2.y-p3.y)*t2 + (-p0.y+3*p1.y-3*p2.y+p3.y)*t3);
    r.z = 0.5f * ((2*p1.z) + (-p0.z+p2.z)*t + (2*p0.z-5*p1.z+4*p2.z-p3.z)*t2 + (-p0.z+3*p1.z-3*p2.z+p3.z)*t3);
    return r;
}

static void BuildTrack(void)
{
    Vector3 centers[MAXS];
    int n = 0;
    int M = 20;
    for (int i = 1; i < CP_COUNT - 2; i++) {
        for (int k = 0; k < M; k++) {
            if (n >= MAXS) break;
            float t = (float)k / (float)M;
            centers[n++] = CatmullRom(CP[i - 1], CP[i], CP[i + 1], CP[i + 2], t);
        }
    }
    if (n < MAXS) centers[n++] = CP[CP_COUNT - 2];
    frameCount = n;

    Vector3 up0 = { 0, 1, 0 };
    for (int i = 0; i < n; i++) {
        int ia = (i > 0) ? i - 1 : 0;
        int ib = (i < n - 1) ? i + 1 : n - 1;
        Vector3 tan = Vector3Normalize(Vector3Subtract(centers[ib], centers[ia]));
        Vector3 right = Vector3CrossProduct(up0, tan);
        if (Vector3Length(right) < 0.001f) right = (Vector3){ 1, 0, 0 };
        right = Vector3Normalize(right);
        Vector3 up = Vector3Normalize(Vector3CrossProduct(tan, right));

        Vector3 accel = Vector3Add(Vector3Subtract(centers[ib], Vector3Scale(centers[i], 2.0f)), centers[ia]);
        float lateral = Vector3DotProduct(accel, right);
        float bank = lateral * 0.020f;
        if (bank > 0.5f) bank = 0.5f;
        if (bank < -0.5f) bank = -0.5f;
        right = Vector3RotateByAxisAngle(right, tan, bank);
        up    = Vector3RotateByAxisAngle(up, tan, bank);

        frames[i].c = centers[i];
        frames[i].right = right;
        frames[i].up = up;
        frames[i].tan = tan;
    }

    profCum[0] = 0.0f;
    for (int j = 1; j < NP; j++) {
        float da = (profA[j] - profA[j - 1]) * WX;
        float db = (profB[j] - profB[j - 1]) * HY;
        profCum[j] = profCum[j - 1] + sqrtf(da * da + db * db);
    }
    pathCum[0] = 0.0f;
    for (int i = 1; i < n; i++)
        pathCum[i] = pathCum[i - 1] + Vector3Distance(centers[i], centers[i - 1]);

    for (int i = 0; i < n; i++)
        for (int j = 0; j < NP; j++)
            ptW[i][j] = Vector3Add(Vector3Add(frames[i].c,
                            Vector3Scale(frames[i].right, profA[j] * WX)),
                            Vector3Scale(frames[i].up, profB[j] * HY));

    triCount = 0;
    float minY = 1e9f;
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < NP - 1; j++) {
            Vector3 A = ptW[i][j], B = ptW[i][j + 1], C = ptW[i + 1][j + 1], D = ptW[i + 1][j];
            Vector3 n1 = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(B, A), Vector3Subtract(D, A)));
            if (triCount < MAXTRI) { tris[triCount].a = A; tris[triCount].b = B; tris[triCount].c = C; tris[triCount].n = n1; tris[triCount].mid = Vector3Scale(Vector3Add(Vector3Add(A, B), C), 1.0f/3.0f); triCount++; }
            if (triCount < MAXTRI) { tris[triCount].a = A; tris[triCount].b = C; tris[triCount].c = D; tris[triCount].n = n1; tris[triCount].mid = Vector3Scale(Vector3Add(Vector3Add(A, C), D), 1.0f/3.0f); triCount++; }
        }
        if (frames[i].c.y < minY) minY = frames[i].c.y;
    }
    gKillY = minY - 700.0f;

    cpCount = 0;
    for (int i = 40; i < n - 4 && cpCount < MAXCP - 1; i += 45) cpIdx[cpCount++] = i;
    cpIdx[cpCount++] = n - 3;

    spawnPos = Vector3Add(frames[2].c, Vector3Scale(frames[2].up, 230.0f));
    spawnYaw = atan2f(frames[2].tan.z, frames[2].tan.x);

    gWorldCenter = frames[frameCount / 3].c;
    gOrbitR = 2600.0f;
}

static void Friction(Vector3 *vel, float dt)
{
    float speed = sqrtf(vel->x * vel->x + vel->z * vel->z);
    if (speed < 1.0f) { vel->x = 0; vel->z = 0; return; }
    float control = (speed < SV_STOPSPEED) ? SV_STOPSPEED : speed;
    float newspeed = speed - control * SV_FRICTION * dt;
    if (newspeed < 0.0f) newspeed = 0.0f;
    float scale = newspeed / speed;
    vel->x *= scale; vel->z *= scale;
}
static void Accelerate(Vector3 *vel, Vector3 wishdir, float wishspeed, float accel, float dt)
{
    float current = Vector3DotProduct(*vel, wishdir);
    float add = wishspeed - current;
    if (add <= 0.0f) return;
    float a = accel * wishspeed * dt;
    if (a > add) a = add;
    *vel = Vector3Add(*vel, Vector3Scale(wishdir, a));
}
static void AirAccelerate(Vector3 *vel, Vector3 wishdir, float wishspeed, float dt)
{
    float wishspd = wishspeed; if (wishspd > AIR_CAP) wishspd = AIR_CAP;
    float current = Vector3DotProduct(*vel, wishdir);
    float add = wishspd - current;
    if (add <= 0.0f) return;
    float a = SV_AIRACCELERATE * wishspeed * dt;
    if (a > add) a = add;
    *vel = Vector3Add(*vel, Vector3Scale(wishdir, a));
}

static Vector3 ClosestPtTri(Vector3 p, Vector3 a, Vector3 b, Vector3 c)
{
    Vector3 ab = Vector3Subtract(b, a), ac = Vector3Subtract(c, a), ap = Vector3Subtract(p, a);
    float d1 = Vector3DotProduct(ab, ap), d2 = Vector3DotProduct(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    Vector3 bp = Vector3Subtract(p, b);
    float d3 = Vector3DotProduct(ab, bp), d4 = Vector3DotProduct(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) { float v = d1 / (d1 - d3); return Vector3Add(a, Vector3Scale(ab, v)); }
    Vector3 cp = Vector3Subtract(p, c);
    float d5 = Vector3DotProduct(ab, cp), d6 = Vector3DotProduct(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) { float w = d2 / (d2 - d6); return Vector3Add(a, Vector3Scale(ac, w)); }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return Vector3Add(b, Vector3Scale(Vector3Subtract(c, b), w));
    }
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom, w = vc * denom;
    return Vector3Add(a, Vector3Add(Vector3Scale(ab, v), Vector3Scale(ac, w)));
}
static void ResolveTri(const Tri *t, Vector3 *pos, Vector3 *vel, int *onground)
{
    Vector3 cp = ClosestPtTri(*pos, t->a, t->b, t->c);
    Vector3 diff = Vector3Subtract(*pos, cp);
    float d2 = Vector3DotProduct(diff, diff);
    if (d2 >= PLAYER_RADIUS * PLAYER_RADIUS) return;
    float d = sqrtf(d2);
    Vector3 nrm = (d > 0.0001f) ? Vector3Scale(diff, 1.0f / d) : t->n;
    *pos = Vector3Add(*pos, Vector3Scale(nrm, PLAYER_RADIUS - d));
    float vn = Vector3DotProduct(*vel, nrm);
    if (vn < 0.0f) *vel = Vector3Subtract(*vel, Vector3Scale(nrm, vn));
    if (nrm.y > GROUND_NY) *onground = 1;
}

static void BuildWorldCollision(BspData *b)
{
    wTriCount = b->triCount;
    wTris = (Tri *)malloc(sizeof(Tri) * wTriCount);
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (int i = 0; i < wTriCount; i++) {
        Vector3 a  = { b->pos[i*9+0], b->pos[i*9+1], b->pos[i*9+2] };
        Vector3 c0 = { b->pos[i*9+3], b->pos[i*9+4], b->pos[i*9+5] };
        Vector3 c1 = { b->pos[i*9+6], b->pos[i*9+7], b->pos[i*9+8] };
        Vector3 n  = { b->nrm[i*9+0], b->nrm[i*9+1], b->nrm[i*9+2] };
        wTris[i].a = a; wTris[i].b = c0; wTris[i].c = c1; wTris[i].n = n;
        wTris[i].mid = Vector3Scale(Vector3Add(Vector3Add(a, c0), c1), 1.0f/3.0f);
        Vector3 vs[3] = { a, c0, c1 };
        for (int k = 0; k < 3; k++) {
            if (vs[k].x < minX) minX = vs[k].x; if (vs[k].x > maxX) maxX = vs[k].x;
            if (vs[k].y < minY) minY = vs[k].y; if (vs[k].y > maxY) maxY = vs[k].y;
            if (vs[k].z < minZ) minZ = vs[k].z; if (vs[k].z > maxZ) maxZ = vs[k].z;
        }
    }
    gMinX = minX; gMinZ = minZ;
    float spanX = maxX - minX, spanZ = maxZ - minZ;
    gCell = 256.0f;
    while (((spanX / gCell + 1.0f) * (spanZ / gCell + 1.0f)) > 1000000.0f) gCell *= 2.0f;
    gNX = (int)(spanX / gCell) + 1; gNZ = (int)(spanZ / gCell) + 1;
    if (gNX < 1) gNX = 1; if (gNZ < 1) gNZ = 1;
    int cells = gNX * gNZ;
    gCellStart = (int *)calloc(cells + 1, sizeof(int));
    for (int i = 0; i < wTriCount; i++) {
        float mnx = fminf(fminf(wTris[i].a.x, wTris[i].b.x), wTris[i].c.x);
        float mxx = fmaxf(fmaxf(wTris[i].a.x, wTris[i].b.x), wTris[i].c.x);
        float mnz = fminf(fminf(wTris[i].a.z, wTris[i].b.z), wTris[i].c.z);
        float mxz = fmaxf(fmaxf(wTris[i].a.z, wTris[i].b.z), wTris[i].c.z);
        int x0 = (int)((mnx - gMinX) / gCell), x1 = (int)((mxx - gMinX) / gCell);
        int z0 = (int)((mnz - gMinZ) / gCell), z1 = (int)((mxz - gMinZ) / gCell);
        if (x0 < 0) x0 = 0; if (z0 < 0) z0 = 0; if (x1 >= gNX) x1 = gNX - 1; if (z1 >= gNZ) z1 = gNZ - 1;
        for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) gCellStart[z*gNX + x + 1]++;
    }
    for (int i = 0; i < cells; i++) gCellStart[i+1] += gCellStart[i];
    int total = gCellStart[cells];
    gCellItems = (int *)malloc(sizeof(int) * (total > 0 ? total : 1));
    int *cur = (int *)malloc(sizeof(int) * cells);
    for (int i = 0; i < cells; i++) cur[i] = gCellStart[i];
    for (int i = 0; i < wTriCount; i++) {
        float mnx = fminf(fminf(wTris[i].a.x, wTris[i].b.x), wTris[i].c.x);
        float mxx = fmaxf(fmaxf(wTris[i].a.x, wTris[i].b.x), wTris[i].c.x);
        float mnz = fminf(fminf(wTris[i].a.z, wTris[i].b.z), wTris[i].c.z);
        float mxz = fmaxf(fmaxf(wTris[i].a.z, wTris[i].b.z), wTris[i].c.z);
        int x0 = (int)((mnx - gMinX) / gCell), x1 = (int)((mxx - gMinX) / gCell);
        int z0 = (int)((mnz - gMinZ) / gCell), z1 = (int)((mxz - gMinZ) / gCell);
        if (x0 < 0) x0 = 0; if (z0 < 0) z0 = 0; if (x1 >= gNX) x1 = gNX - 1; if (z1 >= gNZ) z1 = gNZ - 1;
        for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) gCellItems[cur[z*gNX + x]++] = i;
    }
    free(cur);
    wStamp = (int *)calloc(wTriCount, sizeof(int));
    wStampCur = 0;
    gKillY = minY - 900.0f;
    gWorldCenter = (Vector3){ (minX+maxX)*0.5f, (minY+maxY)*0.5f, (minZ+maxZ)*0.5f };
    gOrbitR = fmaxf(spanX, spanZ) * 0.6f + 1200.0f;
}

static void ResolveWorld(Vector3 *pos, Vector3 *vel, int *onground)
{
    wStampCur++;
    int cx = (int)((pos->x - gMinX) / gCell);
    int cz = (int)((pos->z - gMinZ) / gCell);
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            int X = cx + dx, Z = cz + dz;
            if (X < 0 || Z < 0 || X >= gNX || Z >= gNZ) continue;
            int cell = Z * gNX + X;
            for (int k = gCellStart[cell]; k < gCellStart[cell+1]; k++) {
                int ti = gCellItems[k];
                if (wStamp[ti] == wStampCur) continue;
                wStamp[ti] = wStampCur;
                ResolveTri(&wTris[ti], pos, vel, onground);
            }
        }
    }
}

static void CreateLightShader(void)
{
    const char *vs =
        "#version 330\n"
        "in vec3 vertexPosition;\n"
        "in vec2 vertexTexCoord;\n"
        "in vec3 vertexNormal;\n"
        "in vec4 vertexColor;\n"
        "uniform mat4 mvp;\n"
        "uniform mat4 matModel;\n"
        "uniform mat4 matNormal;\n"
        "out vec2 fragTexCoord;\n"
        "out vec4 fragColor;\n"
        "out vec3 fragNormal;\n"
        "out vec3 fragWorld;\n"
        "void main(){\n"
        "  fragTexCoord=vertexTexCoord;\n"
        "  fragColor=vertexColor;\n"
        "  fragWorld=vec3(matModel*vec4(vertexPosition,1.0));\n"
        "  fragNormal=normalize(vec3(matNormal*vec4(vertexNormal,1.0)));\n"
        "  gl_Position=mvp*vec4(vertexPosition,1.0);\n"
        "}\n";
    const char *fs =
        "#version 330\n"
        "in vec2 fragTexCoord;\n"
        "in vec4 fragColor;\n"
        "in vec3 fragNormal;\n"
        "in vec3 fragWorld;\n"
        "uniform sampler2D texture0;\n"
        "uniform vec4 colDiffuse;\n"
        "uniform vec3 lightDir;\n"
        "uniform vec3 ambient;\n"
        "uniform vec3 viewPos;\n"
        "out vec4 finalColor;\n"
        "void main(){\n"
        "  vec4 tex=texture(texture0,fragTexCoord);\n"
        "  vec3 n=normalize(fragNormal);\n"
        "  if(!gl_FrontFacing) n=-n;\n"
        "  vec3 l=normalize(-lightDir);\n"
        "  float diff=max(dot(n,l),0.0);\n"
        "  float hemi=0.5+0.5*n.y;\n"
        "  vec3 lighting=ambient*hemi + vec3(1.0)*diff*0.85;\n"
        "  vec3 col=tex.rgb*colDiffuse.rgb*lighting;\n"
        "  float d=length(viewPos-fragWorld);\n"
        "  float fog=clamp((d-7000.0)/18000.0,0.0,1.0);\n"
        "  col=mix(col,vec3(0.62,0.70,0.80),fog);\n"
        "  finalColor=vec4(col,1.0);\n"
        "}\n";
    gLight = LoadShaderFromMemory(vs, fs);
    if (gLight.id != 0) {
        gLightOK = 1;
        gLocView = GetShaderLocation(gLight, "viewPos");
        int locL = GetShaderLocation(gLight, "lightDir");
        int locA = GetShaderLocation(gLight, "ambient");
        float ld[3] = { -0.5f, -1.0f, -0.35f };
        float am[3] = { 0.36f, 0.38f, 0.44f };
        SetShaderValue(gLight, locL, ld, SHADER_UNIFORM_VEC3);
        SetShaderValue(gLight, locA, am, SHADER_UNIFORM_VEC3);
    }
}

static void FreeWorld(void)
{
    if (!gUseBsp) return;
    gWorldModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture.id = rlGetTextureIdDefault();
    gWorldModel.materials[0].shader.id = rlGetShaderIdDefault();
    UnloadModel(gWorldModel);
    free(wTris); free(gCellStart); free(gCellItems); free(wStamp);
    wTris = NULL; gCellStart = NULL; gCellItems = NULL; wStamp = NULL;
    gUseBsp = 0;
}

static void ScanMaps(void)
{
    if (gMapsLoaded) { UnloadDirectoryFiles(gMaps); gMapsLoaded = 0; }
    gMaps = LoadDirectoryFilesEx(".", ".bsp", true);
    gMapsLoaded = 1;
    if (gScroll < 0) gScroll = 0;
}

static int LoadBspMap(int i)
{
    if (i < 0 || i >= (int)gMaps.count) return 0;
    FreeWorld();
    BspData bsp;
    if (!FileExists(gMaps.paths[i]) || !Bsp_Load(gMaps.paths[i], &bsp)) return 0;

    Mesh m = { 0 };
    m.vertexCount = bsp.triCount * 3;
    m.triangleCount = bsp.triCount;
    m.vertices = bsp.pos;
    m.normals = bsp.nrm;
    m.texcoords = bsp.uv;
    UploadMesh(&m, false);
    gWorldModel = LoadModelFromMesh(m);
    if (gTexReady) gWorldModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = gConcrete;
    if (gLightOK)  gWorldModel.materials[0].shader = gLight;

    BuildWorldCollision(&bsp);
    gUseBsp = 1;
    selectedMap = i;
    cpCount = 0;
    if (bsp.hasSpawn)
        spawnPos = (Vector3){ bsp.spawn[0], bsp.spawn[1] + 50.0f, bsp.spawn[2] };
    else
        spawnPos = (Vector3){ gWorldCenter.x, gWorldCenter.y + 300.0f, gWorldCenter.z };
    spawnYaw = 0.0f;
    return 1;
}

static void FetchOnlineIndex(void)
{
    gOnlineCount = 0;
    MakeDirectory("downloads");
    snprintf(gStatus, sizeof gStatus, "Fetching map list ...");
    if (!Net_Download("https://surfmaparchive.com/", "downloads/index.html")) {
        snprintf(gStatus, sizeof gStatus, "Could not fetch list (offline?)");
        gOnlineFetched = 1;
        return;
    }
    FILE *f = fopen("downloads/index.html", "rb");
    if (!f) { gOnlineFetched = 1; return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); gOnlineFetched = 1; return; }
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);

    const char *key = "maps/cs/";
    char *p = strstr(buf, key);
    while (p && gOnlineCount < MAX_ONLINE) {
        p += 8;
        char nm[48];
        int n = 0;
        const char *q = p;
        while (*q && n < 47) {
            char c = *q;
            if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-') { nm[n++] = c; q++; }
            else break;
        }
        nm[n] = 0;
        if (n > 0 && strncmp(q, ".zip", 4) == 0) {
            int dup = 0;
            for (int i = 0; i < gOnlineCount; i++) if (strcmp(gOnline[i], nm) == 0) { dup = 1; break; }
            if (!dup) { strcpy(gOnline[gOnlineCount], nm); gOnlineCount++; }
        }
        p = strstr(q, key);
    }
    free(buf);
    snprintf(gStatus, sizeof gStatus, "%d maps online", gOnlineCount);
    gOnlineFetched = 1;
}

static int DownloadAndPlay(const char *name)
{
    MakeDirectory("downloads");
    char url[256], zip[256], dest[256];
    snprintf(url, sizeof url, "https://surfmaparchive.com/maps/cs/%s.zip", name);
    snprintf(zip, sizeof zip, "downloads/%s.zip", name);
    snprintf(dest, sizeof dest, "downloads/%s", name);
    snprintf(gStatus, sizeof gStatus, "Downloading %s ...", name);
    if (!Net_Download(url, zip)) { snprintf(gStatus, sizeof gStatus, "Download failed: %s", name); return 0; }
    Net_Unzip(zip, dest);
    ScanMaps();
    int found = -1;
    for (int i = 0; i < (int)gMaps.count; i++) {
        char b[64];
        strncpy(b, GetFileNameWithoutExt(gMaps.paths[i]), 63);
        b[63] = 0;
        if (strcmp(b, name) == 0) { found = i; break; }
    }
    if (found < 0)
        for (int i = 0; i < (int)gMaps.count; i++)
            if (strstr(gMaps.paths[i], name)) { found = i; break; }
    if (found < 0) { snprintf(gStatus, sizeof gStatus, "No .bsp in archive: %s", name); return 0; }
    if (LoadBspMap(found)) { snprintf(gStatus, sizeof gStatus, "Loaded %s", name); return 1; }
    snprintf(gStatus, sizeof gStatus, "Load failed: %s", name);
    return 0;
}

static Color Lighten(Color c, float t)
{
    return (Color){ (unsigned char)(c.r + (255 - c.r) * t),
                    (unsigned char)(c.g + (255 - c.g) * t),
                    (unsigned char)(c.b + (255 - c.b) * t), c.a };
}
static Color ShadeFog(Color base, Vector3 normal, Vector3 worldPos)
{
    Vector3 light = Vector3Normalize((Vector3){ 0.4f, 1.0f, 0.3f });
    float ndl = Vector3DotProduct(normal, light); if (ndl < 0.0f) ndl = -ndl;
    float k = 0.5f + 0.5f * ndl;
    float r = base.r * k, g = base.g * k, bl = base.b * k;
    float fog = (Vector3Distance(camPosGlobal, worldPos) - FOG_START) / (FOG_END - FOG_START);
    if (fog < 0.0f) fog = 0.0f;
    if (fog > 1.0f) fog = 1.0f;
    r  = r  * (1.0f - fog) + FOG_COLOR.r * fog;
    g  = g  * (1.0f - fog) + FOG_COLOR.g * fog;
    bl = bl * (1.0f - fog) + FOG_COLOR.b * fog;
    return (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)bl, base.a };
}

static void DrawCubeTextured(Vector3 pos, Vector3 size, Color color)
{
    float x = pos.x, y = pos.y, z = pos.z, w = size.x, h = size.y, l = size.z;
    float tx = w / 256.0f, ty = h / 256.0f, tz = l / 256.0f;
    if (gTexReady) rlSetTexture(gConcrete.id);
    rlCheckRenderBatchLimit(36);
    rlBegin(RL_QUADS);
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlNormal3f(0, 0, 1);
        rlTexCoord2f(0, 0);   rlVertex3f(x-w/2, y-h/2, z+l/2);
        rlTexCoord2f(tx, 0);  rlVertex3f(x+w/2, y-h/2, z+l/2);
        rlTexCoord2f(tx, ty); rlVertex3f(x+w/2, y+h/2, z+l/2);
        rlTexCoord2f(0, ty);  rlVertex3f(x-w/2, y+h/2, z+l/2);
        rlNormal3f(0, 0, -1);
        rlTexCoord2f(0, 0);   rlVertex3f(x+w/2, y-h/2, z-l/2);
        rlTexCoord2f(tx, 0);  rlVertex3f(x-w/2, y-h/2, z-l/2);
        rlTexCoord2f(tx, ty); rlVertex3f(x-w/2, y+h/2, z-l/2);
        rlTexCoord2f(0, ty);  rlVertex3f(x+w/2, y+h/2, z-l/2);
        rlNormal3f(0, 1, 0);
        rlTexCoord2f(0, 0);   rlVertex3f(x-w/2, y+h/2, z+l/2);
        rlTexCoord2f(tx, 0);  rlVertex3f(x+w/2, y+h/2, z+l/2);
        rlTexCoord2f(tx, tz); rlVertex3f(x+w/2, y+h/2, z-l/2);
        rlTexCoord2f(0, tz);  rlVertex3f(x-w/2, y+h/2, z-l/2);
        rlNormal3f(0, -1, 0);
        rlTexCoord2f(0, 0);   rlVertex3f(x-w/2, y-h/2, z-l/2);
        rlTexCoord2f(tx, 0);  rlVertex3f(x+w/2, y-h/2, z-l/2);
        rlTexCoord2f(tx, tz); rlVertex3f(x+w/2, y-h/2, z+l/2);
        rlTexCoord2f(0, tz);  rlVertex3f(x-w/2, y-h/2, z+l/2);
        rlNormal3f(1, 0, 0);
        rlTexCoord2f(0, 0);   rlVertex3f(x+w/2, y-h/2, z+l/2);
        rlTexCoord2f(tz, 0);  rlVertex3f(x+w/2, y-h/2, z-l/2);
        rlTexCoord2f(tz, ty); rlVertex3f(x+w/2, y+h/2, z-l/2);
        rlTexCoord2f(0, ty);  rlVertex3f(x+w/2, y+h/2, z+l/2);
        rlNormal3f(-1, 0, 0);
        rlTexCoord2f(0, 0);   rlVertex3f(x-w/2, y-h/2, z-l/2);
        rlTexCoord2f(tz, 0);  rlVertex3f(x-w/2, y-h/2, z+l/2);
        rlTexCoord2f(tz, ty); rlVertex3f(x-w/2, y+h/2, z+l/2);
        rlTexCoord2f(0, ty);  rlVertex3f(x-w/2, y+h/2, z-l/2);
    rlEnd();
    if (gTexReady) rlSetTexture(0);
}

static void DrawTrack(void)
{
    Color tint = { 185, 178, 184, 255 };
    rlDisableBackfaceCulling();
    if (gTexReady) rlSetTexture(gConcrete.id);
    for (int i = 0; i < frameCount - 1; i++) {
        for (int j = 0; j < NP - 1; j++) {
            Vector3 A = ptW[i][j], B = ptW[i][j + 1], C = ptW[i + 1][j + 1], D = ptW[i + 1][j];
            Vector3 nrm = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(B, A), Vector3Subtract(D, A)));
            Vector3 mid = Vector3Scale(Vector3Add(Vector3Add(A, B), Vector3Add(C, D)), 0.25f);
            Color c = ShadeFog(Lighten(tint, 0.35f), nrm, mid);
            float u0 = profCum[j] / TILE, u1 = profCum[j + 1] / TILE;
            float v0 = pathCum[i] / TILE, v1 = pathCum[i + 1] / TILE;
            rlCheckRenderBatchLimit(4);
            rlBegin(RL_QUADS);
                rlColor4ub(c.r, c.g, c.b, 255);
                rlNormal3f(nrm.x, nrm.y, nrm.z);
                rlTexCoord2f(u0, v0); rlVertex3f(A.x, A.y, A.z);
                rlTexCoord2f(u1, v0); rlVertex3f(B.x, B.y, B.z);
                rlTexCoord2f(u1, v1); rlVertex3f(C.x, C.y, C.z);
                rlTexCoord2f(u0, v1); rlVertex3f(D.x, D.y, D.z);
            rlEnd();
        }
    }
    if (gTexReady) rlSetTexture(0);

    int stripeRows[2] = { 1, 3 };
    for (int i = 0; i < frameCount - 1; i++) {
        Color sc = stripePalette[(i / 10) % 5];
        for (int s = 0; s < 2; s++) {
            int j = stripeRows[s];
            Vector3 A = ptW[i][j], D = ptW[i + 1][j];
            Vector3 across = Vector3Normalize(Vector3Subtract(ptW[i][j + 1], ptW[i][j]));
            Vector3 nrm = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(D, A), across));
            Vector3 off = Vector3Scale(nrm, 3.0f);
            Vector3 wv = Vector3Scale(across, 16.0f);
            Vector3 a0 = Vector3Add(Vector3Subtract(A, wv), off);
            Vector3 a1 = Vector3Add(Vector3Add(A, wv), off);
            Vector3 d1 = Vector3Add(Vector3Add(D, wv), off);
            Vector3 d0 = Vector3Add(Vector3Subtract(D, wv), off);
            Color c = ShadeFog(sc, nrm, A); c.a = 255;
            rlCheckRenderBatchLimit(4);
            rlBegin(RL_QUADS);
                rlColor4ub(c.r, c.g, c.b, 255);
                rlNormal3f(nrm.x, nrm.y, nrm.z);
                rlVertex3f(a0.x, a0.y, a0.z);
                rlVertex3f(a1.x, a1.y, a1.z);
                rlVertex3f(d1.x, d1.y, d1.z);
                rlVertex3f(d0.x, d0.y, d0.z);
            rlEnd();
        }
    }
    rlEnableBackfaceCulling();

    for (int i = 30; i < frameCount - 5; i += 70) {
        Vector3 top = Vector3Add(frames[i].c, Vector3Scale(frames[i].up, HY * 1.7f));
        Vector3 lp = Vector3Add(frames[i].c, Vector3Scale(frames[i].right, -WX * 1.25f));
        Vector3 rp = Vector3Add(frames[i].c, Vector3Scale(frames[i].right,  WX * 1.25f));
        lp = Vector3Add(lp, Vector3Scale(frames[i].up, HY * 0.6f));
        rp = Vector3Add(rp, Vector3Scale(frames[i].up, HY * 0.6f));
        DrawCubeTextured(lp, (Vector3){ 110, HY * 1.6f, 110 }, (Color){ 150, 145, 150, 255 });
        DrawCubeTextured(rp, (Vector3){ 110, HY * 1.6f, 110 }, (Color){ 150, 145, 150, 255 });
        DrawCubeTextured(top, (Vector3){ WX * 2.8f, 120, 130 }, (Color){ 160, 120, 110, 255 });
    }
}

static void ResetRun(void)
{
    pPos = spawnPos; pVel = (Vector3){ 0, 0, 0 }; pGround = 0;
    pYaw = spawnYaw; pPitch = -0.2f;
    finished = 0; runStart = GetTime(); topSpeed = 0.0f;
    cpPassed = 0;
}
static int UiButton(Rectangle r, const char *text, int fontSize, int enabled)
{
    Vector2 m = GetMousePosition();
    int hover = enabled && CheckCollisionPointRec(m, r);
    Color bg = !enabled ? (Color){ 40, 44, 54, 255 }
             : hover    ? (Color){ 80, 105, 160, 255 }
                        : (Color){ 48, 58, 84, 255 };
    DrawRectangleRounded(r, 0.22f, 8, bg);
    DrawRectangleLinesEx(r, hover ? 2.0f : 1.0f, (Color){ 150, 175, 220, 180 });
    int tw = MeasureText(text, fontSize);
    DrawText(text, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - fontSize / 2), fontSize,
             enabled ? RAYWHITE : (Color){ 120, 120, 130, 255 });
    int clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (clicked) PlayClick();
    return clicked;
}
static void DrawGithubMark(int x, int y, int size, Color col)
{
    float cx = x + size * 0.5f, cy = y + size * 0.5f, rad = size * 0.5f;
    DrawCircle((int)cx, (int)cy, rad, col);
    DrawCircle((int)(cx - rad * 0.32f), (int)(cy - rad * 0.05f), rad * 0.16f, (Color){ 30, 34, 42, 255 });
    DrawCircle((int)(cx + rad * 0.32f), (int)(cy - rad * 0.05f), rad * 0.16f, (Color){ 30, 34, 42, 255 });
    DrawRectangle((int)(cx - rad * 0.12f), (int)(cy + rad * 0.25f), (int)(rad * 0.24f), (int)(rad * 0.55f), (Color){ 30, 34, 42, 255 });
}

static void ApplyVsync(int on) { if (on) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT); }
static void ApplyResolution(Settings *s)
{
    if (IsWindowFullscreen()) return;
    int w, h; Settings_GetResolution(s, &w, &h);
    int mon = GetCurrentMonitor();
    int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    if (w > mw - 60)  w = mw - 60;
    if (h > mh - 120) h = mh - 120;
    SetWindowSize(w, h);
    SetWindowPosition((mw - w) / 2, (mh - h) / 2);
}
static void ToggleFS(Settings *s)
{
    if (!IsWindowFullscreen()) {
        int mon = GetCurrentMonitor();
        SetWindowSize(GetMonitorWidth(mon), GetMonitorHeight(mon));
        ToggleFullscreen(); s->fullscreen = 1;
    } else {
        ToggleFullscreen(); s->fullscreen = 0; ApplyResolution(s);
    }
}

int main(void)
{
    Settings settings;
    Settings_Load(&settings);
    settings.fullscreen = 0;

    int w, h; Settings_GetResolution(&settings, &w, &h);
    unsigned int flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;
    if (settings.vsync) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);

    InitWindow(w, h, "CS SURF");
    SetExitKey(KEY_NULL);
    rlSetClipPlanes(1.0, 60000.0);
    Settings_Apply(&settings);

    int mon = GetCurrentMonitor();
    int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    if (mw > 0 && mh > 0) {
        if (w > mw - 60 || h > mh - 120) {
            settings.resolution = 0;
            Settings_GetResolution(&settings, &w, &h);
            if (w > mw - 60)  w = mw - 60;
            if (h > mh - 120) h = mh - 120;
            SetWindowSize(w, h);
        }
        SetWindowPosition((mw - w) / 2, (mh - h) / 2);
    }

    InitAudioDevice();
    if (IsAudioDeviceReady()) {
        gAudio = 1;
        SetMasterVolume(settings.masterVolume / 100.0f);
        sndClick  = MakeBeep(880.0f, 880.0f, 0.04f, 0.30f);
        sndJump   = MakeBeep(320.0f, 620.0f, 0.12f, 0.35f);
        sndFinish = MakeBeep(523.0f, 1046.0f, 0.45f, 0.40f);
        sndWind   = MakeWind(2.0f, 0.5f);
    }

    {
        const char *p1 = "concrete.png";
        const char *p2 = "textures/Poliigon_ConcreteWorn_8690/2K/Poliigon_ConcreteWorn_8690_BaseColor.jpg";
        Image base = { 0 };
        if (FileExists(p1)) base = LoadImage(p1);
        if (base.width == 0 && FileExists(p2)) base = LoadImage(p2);
        if (base.width == 0) {
            base = GenImagePerlinNoise(512, 512, 0, 0, 7.0f);
            ImageColorContrast(&base, 18.0f);
            ImageColorBrightness(&base, 55);
        }
        gConcrete = LoadTextureFromImage(base);
        UnloadImage(base);
        GenTextureMipmaps(&gConcrete);
        SetTextureFilter(gConcrete, TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(gConcrete, TEXTURE_WRAP_REPEAT);
        gTexReady = 1;
    }

    if (FileExists("pfp.png")) {
        Image a = LoadImage("pfp.png");
        if (a.data != NULL) {
            ImageResize(&a, 64, 64);
            gPfp = LoadTextureFromImage(a);
            SetTextureFilter(gPfp, TEXTURE_FILTER_BILINEAR);
            UnloadImage(a);
            gPfpReady = 1;
        }
    }

    BuildTrack();
    CreateLightShader();
    ScanMaps();
    for (int mi = 0; mi < (int)gMaps.count; mi++) { if (LoadBspMap(mi)) break; }

    ResetRun();

    GameState state = ST_MENU;
    float menuAngle = 0.0f;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;
        int SW = GetScreenWidth();
        int SH = GetScreenHeight();

        if (IsKeyPressed(KEY_F11)) ToggleFS(&settings);
        float sens = settings.mouseSens * 0.0001f;

        if (state == ST_GAME) { if (!IsCursorHidden()) DisableCursor(); }
        else                  { if (IsCursorHidden())  EnableCursor();  }

        if (state == ST_GAME)
        {
            if (IsKeyPressed(KEY_ESCAPE)) state = ST_PAUSE;

            Vector2 md = GetMouseDelta();
            pYaw   += md.x * sens;
            pPitch -= md.y * sens;
            if (pPitch >  1.55f) pPitch =  1.55f;
            if (pPitch < -1.55f) pPitch = -1.55f;

            Vector3 look = { cosf(pPitch) * cosf(pYaw), sinf(pPitch), cosf(pPitch) * sinf(pYaw) };
            Vector3 fwd  = Vector3Normalize((Vector3){ look.x, 0, look.z });
            Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, (Vector3){ 0, 1, 0 }));

            float f = (IsKeyDown(KEY_W) ? 1.0f : 0.0f) - (IsKeyDown(KEY_S) ? 1.0f : 0.0f);
            float s = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
            Vector3 wishvel = Vector3Add(Vector3Scale(fwd, f), Vector3Scale(right, s));
            float wishlen = Vector3Length(wishvel);
            Vector3 wishdir = (wishlen > 0.0001f) ? Vector3Scale(wishvel, 1.0f / wishlen) : (Vector3){ 0, 0, 0 };
            float wishspeed = (wishlen > 0.0001f) ? SV_MAXSPEED : 0.0f;

            if (pGround) {
                Friction(&pVel, dt);
                Accelerate(&pVel, wishdir, wishspeed, SV_ACCELERATE, dt);
                if (IsKeyDown(KEY_SPACE)) { pVel.y = JUMP_SPEED; pGround = 0; if (gAudio) PlaySound(sndJump); }
            } else {
                AirAccelerate(&pVel, wishdir, wishspeed, dt);
            }
            pVel.y -= SV_GRAVITY * dt;

            int wasFinished = finished;
            pGround = 0;
            float moveLen = Vector3Length(pVel) * dt;
            int steps = (int)(moveLen / (PLAYER_RADIUS * 0.5f)) + 1;
            if (steps > 16) steps = 16;
            float sdt = dt / (float)steps;
            for (int st = 0; st < steps; st++) {
                pPos = Vector3Add(pPos, Vector3Scale(pVel, sdt));
                if (gUseBsp) ResolveWorld(&pPos, &pVel, &pGround);
                else for (int i = 0; i < triCount; i++) ResolveTri(&tris[i], &pPos, &pVel, &pGround);
            }

            if (cpPassed < cpCount) {
                Vector3 cpc = frames[cpIdx[cpPassed]].c;
                if (Vector3Distance(pPos, cpc) < WX * 1.4f) {
                    cpSplit[cpPassed] = GetTime() - runStart;
                    cpPassed++;
                    if (cpPassed >= cpCount && !finished) { finished = 1; finishTime = GetTime() - runStart; }
                }
            }
            if (finished && !wasFinished && gAudio) PlaySound(sndFinish);

            float hspeed = sqrtf(pVel.x * pVel.x + pVel.z * pVel.z);
            if (!finished && hspeed > topSpeed) topSpeed = hspeed;

            if (gAudio) {
                float wv = (hspeed - 150.0f) / 450.0f;
                if (wv < 0.0f) wv = 0.0f;
                if (wv > 1.0f) wv = 1.0f;
                SetSoundVolume(sndWind, wv * 0.7f);
                SetSoundPitch(sndWind, 0.8f + hspeed / 700.0f);
                if (wv > 0.03f && !IsSoundPlaying(sndWind)) PlaySound(sndWind);
                if (wv <= 0.03f && IsSoundPlaying(sndWind)) StopSound(sndWind);
            }

            if (pPos.y < gKillY || IsKeyPressed(KEY_R)) ResetRun();
        }
        else
        {
            if (gAudio && IsSoundPlaying(sndWind)) StopSound(sndWind);
            if (state == ST_PAUSE) { if (IsKeyPressed(KEY_ESCAPE)) state = ST_GAME; }
            else menuAngle += dt * 0.06f;
        }

        Camera3D cam = { 0 };
        cam.up = (Vector3){ 0, 1, 0 };
        cam.fovy = (float)settings.fov;
        cam.projection = CAMERA_PERSPECTIVE;

        if (state == ST_GAME || state == ST_PAUSE) {
            Vector3 look = { cosf(pPitch) * cosf(pYaw), sinf(pPitch), cosf(pPitch) * sinf(pYaw) };
            Vector3 eye = (Vector3){ pPos.x, pPos.y + EYE_OFFSET, pPos.z };
            cam.position = eye;
            cam.target = Vector3Add(eye, look);
        } else {
            float R = gOrbitR;
            Vector3 center = gWorldCenter;
            cam.position = (Vector3){ center.x + cosf(menuAngle) * R, center.y + R * 0.5f, center.z + sinf(menuAngle) * R };
            cam.target = center;
            cam.fovy = 65.0f;
        }
        camPosGlobal = cam.position;

        BeginDrawing();
        ClearBackground(SKY_BOTTOM);
        DrawRectangleGradientV(0, 0, SW, SH, SKY_TOP, SKY_BOTTOM);
        BeginMode3D(cam);
            if (gUseBsp) {
                if (gLightOK) {
                    float vp[3] = { cam.position.x, cam.position.y, cam.position.z };
                    SetShaderValue(gLight, gLocView, vp, SHADER_UNIFORM_VEC3);
                }
                rlDisableBackfaceCulling();
                DrawModel(gWorldModel, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);
                rlEnableBackfaceCulling();
            } else {
                DrawTrack();
            }
        EndMode3D();

        if (state == ST_GAME)
        {
            float hspeed = sqrtf(pVel.x * pVel.x + pVel.z * pVel.z);
            const char *spd = TextFormat("%d", (int)hspeed);
            DrawText(spd, SW / 2 - MeasureText(spd, 64) / 2, SH - 120, 64, RAYWHITE);
            DrawText("u/s", SW / 2 - MeasureText("u/s", 20) / 2, SH - 50, 20, (Color){ 190, 195, 215, 255 });

            double t = finished ? finishTime : (GetTime() - runStart);
            DrawText(TextFormat("Time  %6.2f s", t), 20, 20, 24, RAYWHITE);
            DrawText(TextFormat("Top   %4d u/s", (int)topSpeed), 20, 50, 24, (Color){ 180, 200, 255, 255 });
            if (gUseBsp && selectedMap < (int)gMaps.count)
                DrawText(TextFormat("Map: %s", GetFileNameWithoutExt(gMaps.paths[selectedMap])), 20, 80, 20, (Color){ 200, 210, 230, 255 });
            if (cpCount > 0) {
                DrawText(TextFormat("Checkpoint  %d / %d", cpPassed, cpCount), 20, 106, 22, (Color){ 200, 210, 230, 255 });
                if (cpPassed > 0)
                    DrawText(TextFormat("Last split  %5.2f s", cpSplit[cpPassed - 1]), 20, 132, 20, (Color){ 170, 220, 180, 255 });
            }

            DrawLine(SW / 2 - 10, SH / 2, SW / 2 + 10, SH / 2, RAYWHITE);
            DrawLine(SW / 2, SH / 2 - 10, SW / 2, SH / 2 + 10, RAYWHITE);

            if (finished) {
                const char *msg = TextFormat("FINISH!  %.2f s", finishTime);
                DrawRectangle(0, SH / 2 - 70, SW, 140, (Color){ 0, 0, 0, 150 });
                DrawText(msg, SW / 2 - MeasureText(msg, 52) / 2, SH / 2 - 30, 52, (Color){ 120, 255, 150, 255 });
                const char *sub = "R = retry     ESC = menu";
                DrawText(sub, SW / 2 - MeasureText(sub, 22) / 2, SH / 2 + 30, 22, RAYWHITE);
            }
        }
        else if (state == ST_MENU)
        {
            DrawRectangle(0, 0, SW, SH, (Color){ 0, 0, 0, 70 });
            DrawText("CS  SURF", SW / 2 - MeasureText("CS  SURF", 90) / 2, SH / 5, 90, RAYWHITE);
            DrawText("a raylib surf game", SW / 2 - MeasureText("a raylib surf game", 24) / 2, SH / 5 + 100, 24, (Color){ 175, 190, 225, 255 });

            float bw = 320, bh = 58, bx = SW / 2 - bw / 2, by = SH / 2 - 40;
            if (UiButton((Rectangle){ bx, by,       bw, bh }, "Play",        30, 1)) state = ST_MAPSELECT;
            if (UiButton((Rectangle){ bx, by + 72,  bw, bh }, "Online Maps", 28, 1)) { state = ST_ONLINE; if (!gOnlineFetched) FetchOnlineIndex(); }
            if (UiButton((Rectangle){ bx, by + 144, bw, bh }, "Options",     30, 1)) state = ST_OPTIONS;
            if (UiButton((Rectangle){ bx, by + 216, bw, bh }, "Quit",        30, 1)) break;

            DrawText("made by eren", 24, SH - 78, 22, (Color){ 210, 215, 230, 255 });
            Rectangle gh = { 24, SH - 50, 230, 38 };
            Vector2 mpp = GetMousePosition();
            int ghHover = CheckCollisionPointRec(mpp, gh);
            DrawRectangleRounded(gh, 0.3f, 8, ghHover ? (Color){ 60, 66, 80, 255 } : (Color){ 40, 44, 54, 255 });
            DrawRectangleLinesEx(gh, ghHover ? 2.0f : 1.0f, (Color){ 150, 160, 180, 160 });
            if (gPfpReady)
                DrawTexturePro(gPfp, (Rectangle){ 0, 0, (float)gPfp.width, (float)gPfp.height },
                               (Rectangle){ 34, (float)(SH - 46), 30, 30 }, (Vector2){ 0, 0 }, 0.0f, WHITE);
            else
                DrawGithubMark(34, SH - 46, 30, RAYWHITE);
            DrawText("github.com/Erennkrb", 74, SH - 41, 18, RAYWHITE);
            if (ghHover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { PlayClick(); OpenURL("https://github.com/Erennkrb"); }
        }
        else if (state == ST_MAPSELECT)
        {
            DrawRectangle(0, 0, SW, SH, (Color){ 0, 0, 0, 130 });
            DrawText("Select Map", SW / 2 - MeasureText("Select Map", 56) / 2, 50, 56, RAYWHITE);
            DrawText(TextFormat("%d maps found  -  drop .bsp files into the game folder", (int)gMaps.count),
                     SW / 2 - MeasureText("..", 18) / 2 - 200, 116, 18, (Color){ 175, 185, 210, 255 });

            int total = (int)gMaps.count;
            const int VIS = 7;
            int maxScroll = (total > VIS) ? total - VIS : 0;
            gScroll -= (int)GetMouseWheelMove();
            if (gScroll < 0) gScroll = 0;
            if (gScroll > maxScroll) gScroll = maxScroll;

            float cw = 620, ch = 56, cx = SW / 2 - cw / 2, cy = 160, gap = 64;

            if (total == 0) {
                DrawText("No .bsp maps found", SW / 2 - MeasureText("No .bsp maps found", 28) / 2, 260, 28, (Color){ 230, 180, 120, 255 });
            }
            for (int k = 0; k < VIS && (k + gScroll) < total; k++) {
                int i = k + gScroll;
                Rectangle rc = { cx, cy + gap * k, cw, ch };
                Vector2 mm = GetMousePosition();
                int hover = CheckCollisionPointRec(mm, rc);
                int isSel = (gUseBsp && selectedMap == i);
                DrawRectangleRounded(rc, 0.15f, 8, hover ? (Color){ 70, 95, 150, 255 } : (Color){ 45, 55, 80, 255 });
                DrawRectangleLinesEx(rc, (hover || isSel) ? 2.0f : 1.0f, isSel ? (Color){ 120, 220, 150, 220 } : (Color){ 150, 175, 220, 180 });
                DrawText(GetFileNameWithoutExt(gMaps.paths[i]), (int)cx + 22, (int)(cy + gap * k) + 16, 26, RAYWHITE);
                if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    PlayClick();
                    if (LoadBspMap(i)) { ResetRun(); state = ST_GAME; }
                }
            }
            if (total > VIS)
                DrawText("scroll for more", (int)cx, (int)(cy + gap * VIS) + 2, 16, (Color){ 160, 165, 185, 255 });

            float by = cy + gap * VIS + 26;
            if (UiButton((Rectangle){ cx, by, 300, 50 }, "Refresh", 26, 1)) { ScanMaps(); }
            if (UiButton((Rectangle){ cx + 320, by, 300, 50 }, "Back", 26, 1)) state = ST_MENU;
        }
        else if (state == ST_ONLINE)
        {
            DrawRectangle(0, 0, SW, SH, (Color){ 0, 0, 0, 140 });
            DrawText("Online Maps", SW / 2 - MeasureText("Online Maps", 50) / 2, 40, 50, RAYWHITE);
            DrawText("surfmaparchive.com  (CS 1.6)", SW / 2 - MeasureText("surfmaparchive.com  (CS 1.6)", 18) / 2, 96, 18, (Color){ 175, 185, 210, 255 });
            if (gStatus[0]) DrawText(gStatus, SW / 2 - MeasureText(gStatus, 18) / 2, 120, 18, (Color){ 170, 220, 180, 255 });

            int total = gOnlineCount;
            const int VIS = 8;
            int maxScroll = (total > VIS) ? total - VIS : 0;
            gOnlineScroll -= (int)GetMouseWheelMove() * 2;
            if (gOnlineScroll < 0) gOnlineScroll = 0;
            if (gOnlineScroll > maxScroll) gOnlineScroll = maxScroll;

            float cw = 620, ch = 48, cx = SW / 2 - cw / 2, cy = 152, gap = 54;
            for (int k = 0; k < VIS && (k + gOnlineScroll) < total; k++) {
                int i = k + gOnlineScroll;
                Rectangle rc = { cx, cy + gap * k, cw, ch };
                Vector2 mm = GetMousePosition();
                int hover = CheckCollisionPointRec(mm, rc);
                DrawRectangleRounded(rc, 0.18f, 6, hover ? (Color){ 70, 95, 150, 255 } : (Color){ 45, 55, 80, 255 });
                DrawRectangleLinesEx(rc, hover ? 2.0f : 1.0f, (Color){ 150, 175, 220, 180 });
                DrawText(gOnline[i], (int)cx + 20, (int)(cy + gap * k) + 13, 24, RAYWHITE);
                DrawText("download", (int)(cx + cw - 110), (int)(cy + gap * k) + 16, 18, (Color){ 170, 200, 240, 255 });
                if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    PlayClick();
                    if (DownloadAndPlay(gOnline[i])) { ResetRun(); state = ST_GAME; }
                }
            }
            if (total > VIS)
                DrawText("scroll for more", (int)cx, (int)(cy + gap * VIS) + 2, 16, (Color){ 160, 165, 185, 255 });

            float by2 = cy + gap * VIS + 24;
            if (UiButton((Rectangle){ cx, by2, 300, 50 }, "Reload List", 24, 1)) { gOnlineFetched = 0; FetchOnlineIndex(); gOnlineScroll = 0; }
            if (UiButton((Rectangle){ cx + 320, by2, 300, 50 }, "Back", 26, 1)) state = ST_MENU;
        }
        else if (state == ST_OPTIONS)
        {
            DrawRectangle(0, 0, SW, SH, (Color){ 0, 0, 0, 130 });
            DrawText("Graphics Options", SW / 2 - MeasureText("Graphics Options", 50) / 2, 60, 50, RAYWHITE);

            float rowW = 560, rowH = 54, rx = SW / 2 - rowW / 2, ry = 170, gap = 68;
            char buf[128];

            int rw, rh; Settings_GetResolution(&settings, &rw, &rh);
            snprintf(buf, sizeof buf, "Resolution:  %d x %d", rw, rh);
            if (UiButton((Rectangle){ rx, ry, rowW, rowH }, buf, 26, !IsWindowFullscreen())) {
                settings.resolution = (settings.resolution + 1) % 3; ApplyResolution(&settings);
            }
            snprintf(buf, sizeof buf, "VSync:  %s", settings.vsync ? "On" : "Off");
            if (UiButton((Rectangle){ rx, ry + gap, rowW, rowH }, buf, 26, 1)) { settings.vsync = !settings.vsync; ApplyVsync(settings.vsync); }
            const char *capTxt = (settings.fpsCap <= 0) ? "Off" : TextFormat("%d", settings.fpsCap);
            snprintf(buf, sizeof buf, "FPS Limit:  %s", capTxt);
            if (UiButton((Rectangle){ rx, ry + gap * 2, rowW, rowH }, buf, 26, 1)) {
                int caps[5] = { 0, 60, 120, 144, 240 }, idx = 0;
                for (int i = 0; i < 5; i++) if (caps[i] == settings.fpsCap) idx = i;
                settings.fpsCap = caps[(idx + 1) % 5]; Settings_Apply(&settings);
            }
            snprintf(buf, sizeof buf, "Fullscreen:  %s   (F11)", IsWindowFullscreen() ? "On" : "Off");
            if (UiButton((Rectangle){ rx, ry + gap * 3, rowW, rowH }, buf, 26, 1)) ToggleFS(&settings);
            snprintf(buf, sizeof buf, "Show FPS:  %s", settings.showFps ? "On" : "Off");
            if (UiButton((Rectangle){ rx, ry + gap * 4, rowW, rowH }, buf, 26, 1)) settings.showFps = !settings.showFps;
            if (UiButton((Rectangle){ rx, ry + gap * 5 + 20, rowW, rowH }, "Back", 28, 1)) { Settings_Save(&settings); state = ST_MENU; }
        }
        else if (state == ST_PAUSE)
        {
            DrawRectangle(0, 0, SW, SH, (Color){ 0, 0, 0, 150 });
            DrawText("Paused", SW / 2 - MeasureText("Paused", 50) / 2, 50, 50, RAYWHITE);

            float rowW = 560, rowH = 54, rx = SW / 2 - rowW / 2, ry = 140, gap = 66;
            char buf[128];

            snprintf(buf, sizeof buf, "Mouse Sensitivity:  %.2f", settings.mouseSens * 0.1f);
            DrawText(buf, (int)rx, (int)ry + 14, 24, RAYWHITE);
            if (UiButton((Rectangle){ rx + rowW - 130, ry, 60, rowH }, "-", 30, 1)) { settings.mouseSens -= 2; if (settings.mouseSens < 3) settings.mouseSens = 3; }
            if (UiButton((Rectangle){ rx + rowW - 60,  ry, 60, rowH }, "+", 30, 1)) { settings.mouseSens += 2; if (settings.mouseSens > 200) settings.mouseSens = 200; }
            snprintf(buf, sizeof buf, "Field of View:  %d", settings.fov);
            DrawText(buf, (int)rx, (int)(ry + gap) + 14, 24, RAYWHITE);
            if (UiButton((Rectangle){ rx + rowW - 130, ry + gap, 60, rowH }, "-", 30, 1)) { settings.fov -= 5; if (settings.fov < 60) settings.fov = 60; }
            if (UiButton((Rectangle){ rx + rowW - 60,  ry + gap, 60, rowH }, "+", 30, 1)) { settings.fov += 5; if (settings.fov > 120) settings.fov = 120; }

            if (UiButton((Rectangle){ rx, ry + gap * 2 + 6, rowW, rowH }, "Resume (ESC)", 28, 1)) state = ST_GAME;
            if (UiButton((Rectangle){ rx, ry + gap * 3 + 6, rowW, rowH }, "Restart",      28, 1)) { ResetRun(); state = ST_GAME; }
            if (UiButton((Rectangle){ rx, ry + gap * 4 + 6, rowW, rowH }, "Main Menu",    28, 1)) { Settings_Save(&settings); state = ST_MENU; }

            int cty = (int)(ry + gap * 5 + 24);
            DrawText("Controls", (int)rx, cty, 24, (Color){ 200, 210, 235, 255 });
            DrawText("Mouse - look      A / D - strafe (surf)      W / S - forward / back",
                     (int)rx, cty + 34, 18, (Color){ 200, 205, 220, 255 });
            DrawText("SPACE - jump      R - restart      ESC - pause      F11 - fullscreen",
                     (int)rx, cty + 58, 18, (Color){ 200, 205, 220, 255 });
        }

        if (settings.showFps) DrawFPS(SW - 90, 20);
        EndDrawing();
    }

    FreeWorld();
    if (gMapsLoaded) UnloadDirectoryFiles(gMaps);
    if (gLightOK) UnloadShader(gLight);
    if (gTexReady) UnloadTexture(gConcrete);
    if (gPfpReady) UnloadTexture(gPfp);
    if (gAudio) {
        UnloadSound(sndClick);
        UnloadSound(sndJump);
        UnloadSound(sndFinish);
        UnloadSound(sndWind);
        CloseAudioDevice();
    }
    Settings_Save(&settings);
    CloseWindow();
    return 0;
}
