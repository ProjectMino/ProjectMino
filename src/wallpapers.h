#ifndef PROJECT_MINO_WALLPAPERS_H
#define PROJECT_MINO_WALLPAPERS_H

#include <SDL.h>

SDL_Texture* fetchUnsplashWallpaper(SDL_Renderer* renderer, int w, int h);
void renderWallpaperWithTint(SDL_Renderer* renderer, SDL_Texture* wallpaper, int windowW, int windowH);

#endif // PROJECT_MINO_WALLPAPERS_H