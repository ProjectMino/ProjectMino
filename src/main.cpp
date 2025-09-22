#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include "game.h"
#include "menu.h"
#include "menu_modern.h"
#include "debug_overlay.h"
#include "social/discord.h"

// Forward declarations (replace with the real headers if available)
TTF_Font* OpenScaledFont(const std::string& path, int size, SDL_Window* window);
void StartGamePlaceholder(SDL_Renderer* renderer, TTF_Font* font);

int main() {
    // initialize Discord RPC with app id and the large image key (asset key)
    social::InitDiscordRPC(social::kDiscordAppId, "main");
    // replace "main_image_key" with your Discord asset key for the large image

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("ProjectMino", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // load the menu font: try a few likely paths and print diagnostics
    const std::vector<std::string> try_paths = {
        "src/assets/font.ttf",
        "assets/font.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    };
    std::string font_path;
    for (const auto &p : try_paths) {
        if (std::filesystem::exists(p)) { font_path = p; break; }
    }
    if (font_path.empty()) {
        fprintf(stderr, "Menu font not found. Tried:\n");
        for (auto &p : try_paths) fprintf(stderr, "  %s\n", p.c_str());
    }

    // Open font scaled to display/window
    TTF_Font* font = OpenScaledFont(font_path, 28, window);

    // Draw debug overlay once before the menu (50% transparent, bottom-left)
    // Menu may repaint itself; if you want the debug text visible during the menu,
    // consider integrating DrawDebugInfo into the menu's render loop.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderClear(renderer);
    DrawDebugInfo(renderer, font, window);
    SDL_RenderPresent(renderer);

    // Show the modern main menu first (blocking). Pass a background image path or empty string to use tiled fallback.
    MenuResult mr = RunMainMenu(window, renderer, font, /*bg image path*/ "");
    if (mr.choice.empty() || mr.choice == "Exit") {
        // user chose to exit from the menu
        if (font) TTF_CloseFont(font);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 0;
    }

    // If user selected Singleplayer:Classic, start the classic game (replace StartGamePlaceholder with your real game start)
    if (mr.choice.rfind("Singleplayer:Classic", 0) == 0) {
        StartGamePlaceholder(renderer, font);
    } else {
        // For other choices, show a placeholder message for now
        std::string msg = "Menu choice: " + mr.choice;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Menu Choice", msg.c_str(), window);
    }

    // After the game or messagebox, exit (or loop back to menu if you prefer)

    if (font) TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
