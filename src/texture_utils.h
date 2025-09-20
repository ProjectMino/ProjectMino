#ifndef PROJECTMINO_TEXTURE_UTILS_H
#define PROJECTMINO_TEXTURE_UTILS_H

#include <SDL2/SDL.h>

/// Loads an image and makes `key` become transparent (falls back to keeping existing alpha).
SDL_Texture* LoadTextureWithColorKey(SDL_Renderer* renderer, const char* path, SDL_Color key);

#endif // PROJECTMINO_TEXTURE_UTILS_H