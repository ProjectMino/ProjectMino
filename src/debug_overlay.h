#ifndef PROJECTMINO_DEBUG_OVERLAY_H
#define PROJECTMINO_DEBUG_OVERLAY_H

#include <SDL.h>
#include <SDL_ttf.h>

void DrawDebugInfo(SDL_Renderer* renderer, TTF_Font* font, SDL_Window* window);

// New: global toggle helpers so any UI loop can toggle the overlay
void ToggleDebugOverlay();
void SetDebugOverlay(bool visible);
bool IsDebugOverlayVisible();

#endif // PROJECTMINO_DEBUG_OVERLAY_H