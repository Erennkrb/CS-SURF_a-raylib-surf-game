#include "settings.h"
#include <stdio.h>

void Settings_Load(Settings *s)
{
    s->vsync = 1;
    s->fpsCap = 0;
    s->resolution = 1;
    s->masterVolume = 100;
    s->musicVolume = 70;
    s->sfxVolume = 100;
    s->showFps = 1;
    s->particles = 1;
    s->ultraMode = 0;
    s->fullscreen = 0;
    s->mouseSens = 30;
    s->fov = 90;

    FILE *f = fopen("settings.cfg", "r");
    if (f)
    {
        fscanf(f, "%d %d %d %d %d %d %d %d %d %d %d %d",
               &s->vsync, &s->fpsCap, &s->resolution,
               &s->masterVolume, &s->musicVolume, &s->sfxVolume,
               &s->showFps, &s->particles, &s->ultraMode,
               &s->fullscreen, &s->mouseSens, &s->fov);
        fclose(f);
    }

    if (s->resolution < 0 || s->resolution > 2) s->resolution = 1;
    if (s->mouseSens < 3)   s->mouseSens = 3;
    if (s->mouseSens > 200) s->mouseSens = 200;
    if (s->fov < 60)  s->fov = 60;
    if (s->fov > 120) s->fov = 120;
}

void Settings_Save(const Settings *s)
{
    FILE *f = fopen("settings.cfg", "w");
    if (f)
    {
        fprintf(f, "%d %d %d %d %d %d %d %d %d %d %d %d\n",
                s->vsync, s->fpsCap, s->resolution,
                s->masterVolume, s->musicVolume, s->sfxVolume,
                s->showFps, s->particles, s->ultraMode,
                s->fullscreen, s->mouseSens, s->fov);
        fclose(f);
    }
}

void Settings_GetResolution(const Settings *s, int *width, int *height)
{
    switch (s->resolution)
    {
        case 0: *width = 1280; *height = 720;  break;
        case 1: *width = 1600; *height = 900;  break;
        case 2: *width = 1920; *height = 1080; break;
        default: *width = 1280; *height = 720; break;
    }
}

void Settings_Apply(const Settings *s)
{
    if (s->fpsCap > 0) SetTargetFPS(s->fpsCap);
    else               SetTargetFPS(0);
}
