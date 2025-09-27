#include <SDL.h>
#include <SDL_ttf.h>
#include <string>

// Minimal stubs to satisfy linker for missing helper functions used by main.cpp.
// Replace with your real implementations when available.

TTF_Font* OpenScaledFont(const std::string& path, int size, SDL_Window* /*window*/) {
    if (path.empty()) return nullptr;
    // Try to open the requested font size.
    TTF_Font* f = TTF_OpenFont(path.c_str(), size);
    return f;
}

void StartGamePlaceholder(SDL_Renderer* /*renderer*/, TTF_Font* /*font*/) {
    // Simple no-op placeholder used during linking.
    // Replace with your actual game start/rendering function.
    return;
}

namespace social {
    // keep signature matching the undefined symbol
    void InitDiscordRPC(unsigned long /*appid*/, const std::string & /*appStr*/){}
}

namespace nexus {
    void Init(){}
    std::string GetSubtext(){ return std::string(); }
    bool DropdownVisible(){ return false; }
    std::string GetEditingPassMasked(){ return std::string(); }
    std::string GetLoginStatus(){ return std::string(); }
    void Shutdown(){}
}