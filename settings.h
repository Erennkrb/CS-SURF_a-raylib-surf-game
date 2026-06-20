#ifndef SETTINGS_H
#define SETTINGS_H

#include "raylib.h"

typedef struct {
    int vsync;
    int fpsCap;
    int resolution;
    int masterVolume;
    int musicVolume;
    int sfxVolume;
    int showFps;
    int particles;
    int ultraMode;
    int fullscreen;
    int mouseSens;
    int fov;
} Settings;

void Settings_Load(Settings *s);
void Settings_Save(const Settings *s);
void Settings_Apply(const Settings *s);
void Settings_GetResolution(const Settings *s, int *width, int *height);

#endif
