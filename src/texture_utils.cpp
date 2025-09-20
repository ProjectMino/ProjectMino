#include "texture_utils.h"
#include <SDL.h>
#include <SDL_image.h>

// Load image, apply color-key if requested, convert to RGBA, create blended texture.
// This avoids black "fringe" from linear filtering / sampling neighboring transparent pixels.
SDL_Texture* LoadTextureWithColorKey(SDL_Renderer* renderer, const char* path, SDL_Color key) {
    if (!renderer || !path) return nullptr;

    // Prefer nearest sampling to avoid blended borders from atlas/background pixels.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // "0" == nearest, "1" == linear

    // load with SDL_image (supports PNG with alpha)
    SDL_Surface* orig = IMG_Load(path);
    if (!orig) return nullptr;

    // If surface has no alpha channel, apply color key (make provided key transparent)
    bool has_alpha = (orig->format->Amask != 0);
    if (!has_alpha) {
        Uint32 keymap = SDL_MapRGB(orig->format, key.r, key.g, key.b);
        SDL_SetColorKey(orig, SDL_TRUE, keymap);
    }

    // Convert to a 32-bit RGBA surface so the created texture has proper alpha and no format issues.
    SDL_Surface* conv = SDL_ConvertSurfaceFormat(orig, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(orig);
    if (!conv) return nullptr;

    // Create texture from RGBA surface
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, conv);
    SDL_FreeSurface(conv);
    if (!tex) return nullptr;

    // Ensure blending is enabled
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    // Renderer should also use blend mode when drawing (call once at init):
    // SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    return tex;
}