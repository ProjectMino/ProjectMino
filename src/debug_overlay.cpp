#include "debug_overlay.h"
#include <SDL.h>
#include <SDL_ttf.h>

// Minimal stub for the debug overlay used in main.cpp.
// Replace with real rendering code when available.

static bool g_debug_overlay_visible = false;

void DrawDebugInfo(SDL_Renderer* renderer, TTF_Font* font, SDL_Window* window) {
    if (!renderer) return;
    // simple visible marker so toggle is obvious
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 128);
    SDL_Rect r = { 6, 6, 220, 44 };
    SDL_RenderFillRect(renderer, &r);
    if (font) {
        SDL_Surface* s = TTF_RenderUTF8_Blended(font, "DEBUG OVERLAY (F8 to hide)", {255,255,255,255});
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_Rect dst = { 12, 12, s->w, s->h };
            SDL_RenderCopy(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_FreeSurface(s);
        }
    }
    (void)window;
}

void ToggleDebugOverlay() {
    g_debug_overlay_visible = !g_debug_overlay_visible;
}

void SetDebugOverlay(bool visible) {
    g_debug_overlay_visible = visible;
}

bool IsDebugOverlayVisible() {
    return g_debug_overlay_visible;
}