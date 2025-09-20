#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>

struct MenuResult {
    std::string choice; // e.g. "Singleplayer", "Exit", "Singleplayer:Classic", etc.
    std::string slot;   // optional (empty if none)
};

// Runs a blocking modern-styled main menu loop using the provided window/renderer/font.
// bg_image_path: optional path to background image (use empty to use solid tile/gray)
// Returns MenuResult when user chooses an item (Exit returns choice="Exit")
MenuResult RunMainMenu(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font,
                       const std::string& bg_image_path = "");