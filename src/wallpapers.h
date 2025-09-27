#ifndef PROJECT_MINO_WALLPAPERS_H
#define PROJECT_MINO_WALLPAPERS_H

#include <SDL.h>

SDL_Texture* fetchUnsplashWallpaper(SDL_Renderer* renderer, int w, int h);
// Render the wallpaper stretched to the full window size with a black tint overlay.
// tint_alpha: 0 = no tint, 255 = solid black. Default set to ~20% (51).
void renderWallpaperWithTint(SDL_Renderer* renderer, SDL_Texture* wallpaper, int windowW, int windowH, int tint_alpha = 51);

#endif // PROJECT_MINO_WALLPAPERS_H