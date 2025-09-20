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

// Replace this with your real game start function (call when user picks Classic).
void StartGamePlaceholder(SDL_Renderer* renderer, TTF_Font* font) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Start Game", "Classic game would start now (replace StartGamePlaceholder).", nullptr);
}

// Open font at a size scaled for the current display/window.
// base_point_size is the nominal size (e.g. 28).
static TTF_Font* OpenScaledFont(const std::string& font_path, int base_point_size, SDL_Window* window) {
    if (font_path.empty()) return nullptr;

    // Try DPI-based scaling first
    float ddpi = 96.0f;
    float hdpi = 0.0f, vdpi = 0.0f;
    float scale = 1.0f;
    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) == 0) {
        // ddpi is dots-per-inch for the display; treat 96 DPI as baseline
        scale = ddpi / 96.0f;
    } else {
        // Fallback: scale by window size relative to a base resolution
        int w = 1280, h = 720;
        SDL_GetWindowSize(window, &w, &h);
        float sx = float(w) / 1280.0f;
        float sy = float(h) / 720.0f;
        scale = std::min(sx, sy);
    }

    // Clamp reasonable scaling
    scale = std::max(0.75f, std::min(scale, 3.0f));
    int font_px = std::max(8, int(base_point_size * scale + 0.5f));

    TTF_Font* f = TTF_OpenFont(font_path.c_str(), font_px);
    if (!f) {
        fprintf(stderr, "TTF_OpenFont('%s', %d) failed: %s\n", font_path.c_str(), font_px, TTF_GetError());
    } else {
        fprintf(stderr, "Loaded menu font: %s at size %d (scale %.2f)\n", font_path.c_str(), font_px, scale);
    }
    return f;
}

// Draw a small debug overlay at the bottom of the window with 50% alpha.
// Shows three vertical lines: platform, version, compiler version.
void DrawDebugInfo(SDL_Renderer* renderer, TTF_Font* font, SDL_Window* window) {
    if (!renderer) return;

    // Build platform string
    std::string platform;
#if defined(_WIN32)
    platform = "Windows";
#elif defined(__APPLE__)
    platform = "macOS";
#elif defined(__linux__)
    platform = "Linux";
#elif defined(__unix__)
    platform = "Unix";
#else
    platform = "Unknown";
#endif

    // Version (fixed per request)
    std::string version = "0.0.1";

    // Compiler/version string
    std::ostringstream comp;
#if defined(__clang__)
    comp << "clang " << __clang_version__;
#elif defined(__GNUC__)
    comp << "gcc " << __VERSION__;
#elif defined(_MSC_VER)
    comp << "msvc " << _MSC_VER;
#else
    comp << "unknown-compiler";
#endif
    std::string compiler = comp.str();

    std::vector<std::string> lines = { platform, version, compiler };

    // If no font available, skip drawing (can't render text)
    if (!font) return;

    // Color white; alpha will be applied to texture.
    SDL_Color color = { 255, 255, 255, 255 };

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window, &win_w, &win_h);

    // Calculate total height
    int line_spacing = 2;
    int total_h = 0;
    std::vector<SDL_Texture*> textures;
    std::vector<SDL_Rect> rects;
    textures.reserve(lines.size());
    rects.reserve(lines.size());

    for (const auto& ln : lines) {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, ln.c_str(), color);
        if (!surf) {
            rects.push_back({0,0,0,0});
            textures.push_back(nullptr);
            continue;
        }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_FreeSurface(surf);
        if (!tex) {
            textures.push_back(nullptr);
            rects.push_back({0,0,0,0});
            continue;
        }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(tex, 255); // fully opaque white
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
        textures.push_back(tex);
        rects.push_back({ 10, 0, tw, th }); // x will be 10 (left margin)
        total_h += th;
    }
    total_h += int(line_spacing * (lines.size() - 1));

    int start_y = win_h - total_h - 10; // 10 px bottom margin
    int y = start_y;
    for (size_t i = 0; i < textures.size(); ++i) {
        SDL_Texture* tex = textures[i];
        if (!tex) continue;
        SDL_Rect dst = rects[i];
        dst.y = y;
        // keep left margin at 10
        dst.x = 10;
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        y += dst.h + line_spacing;
        SDL_DestroyTexture(tex);
    }
}

int main(int argc, char** argv) {
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
