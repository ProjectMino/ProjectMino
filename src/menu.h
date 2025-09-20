#pragma once
#include <SDL.h>
#include <SDL_ttf.h>

// Menu view/state
enum class MenuView {
    MAIN,
    SINGLEPLAYER_SUB,
    MULTIPLAYER_SUB,
    OPTIONS
};

// Render the main menu. Animated underline uses `anim` (seconds accumulator).
// top_selected: index in main menu (0..n-1)
// sub_selected: index in current submenu (when applicable)
void RenderMainMenu(SDL_Renderer* renderer, TTF_Font* font, MenuView view, int top_selected, int sub_selected, float anim);