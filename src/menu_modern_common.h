#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <algorithm>
#include <iostream>

// project helpers
#include "wallpapers.h"
#include "debug_overlay.h"
#include "social/nexus.h"

// Minimal helper: load texture or nullptr
static SDL_Texture* LoadTexture(SDL_Renderer* r, const std::string& path) {
    if (path.empty()) return nullptr;
    SDL_Surface* s = IMG_Load(path.c_str());
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

// small clamp helper for pre-C++17 compatibility
template<typename T>
static T clamp_val(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }