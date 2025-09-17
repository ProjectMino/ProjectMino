#include <SDL2/SDL.h>
#include <algorithm>

namespace ui {

// base logical UI size you want to design for
static int g_base_w = 1280;
static int g_base_h = 720;

// runtime state
static SDL_Window* g_window = nullptr;
static float g_scale = 1.0f;
static float g_dpi_scale = 1.0f;
static bool g_fullscreen = false;
static int g_windowed_w = 1280;
static int g_windowed_h = 720;
static int g_windowed_x = SDL_WINDOWPOS_CENTERED;
static int g_windowed_y = SDL_WINDOWPOS_CENTERED;

void Init(SDL_Window* window, int base_w = 1280, int base_h = 720) {
    g_window = window;
    g_base_w = base_w;
    g_base_h = base_h;

    // ensure window is resizable and high-dpi aware when created elsewhere
    // compute initial values
    if (g_window) {
        int w, h;
        SDL_GetWindowSize(g_window, &w, &h);
        g_windowed_w = w;
        g_windowed_h = h;
        int x, y;
        SDL_GetWindowPosition(g_window, &x, &y);
        g_windowed_x = x;
        g_windowed_y = y;
    }

    // get display DPI for DPI scaling (fallback to 96 DPI)
    float ddpi = 96.0f;
    if (g_window) {
        int displayIndex = SDL_GetWindowDisplayIndex(g_window);
        float hdpi = 0.0f, vdpi = 0.0f;
        if (SDL_GetDisplayDPI(displayIndex, &ddpi, &hdpi, &vdpi) == 0 && ddpi > 0.0f) {
            // use system DPI
        } else {
            ddpi = 96.0f;
        }
    }
    g_dpi_scale = ddpi / 96.0f;

    // initial scale calculation
    if (g_window) {
        int w, h;
        SDL_GetWindowSize(g_window, &w, &h);
        g_scale = std::min(static_cast<float>(w) / g_base_w, static_cast<float>(h) / g_base_h) * g_dpi_scale;
    } else {
        g_scale = g_dpi_scale;
    }
}

// Call this when you receive SDL_WINDOWEVENT_SIZE_CHANGED or SDL_WINDOWEVENT_RESIZED
void OnWindowResized(int new_w, int new_h) {
    // some platforms send 0 for sizes on maximize; query actual size if needed
    if (g_window && (new_w == 0 || new_h == 0)) {
        SDL_GetWindowSize(g_window, &new_w, &new_h);
    }

    // Recompute scale based on logical base and DPI
    g_scale = std::min(static_cast<float>(new_w) / g_base_w, static_cast<float>(new_h) / g_base_h) * g_dpi_scale;
    // If not fullscreen, track windowed size
    if (!g_fullscreen) {
        g_windowed_w = new_w;
        g_windowed_h = new_h;
    }
}

// Toggle fullscreen (desktop fullscreen so resolution stays same)
void ToggleFullscreen() {
    if (!g_window) return;
    Uint32 flags = (g_fullscreen ? 0u : SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!g_fullscreen) {
        // save windowed geometry
        SDL_GetWindowSize(g_window, &g_windowed_w, &g_windowed_h);
        SDL_GetWindowPosition(g_window, &g_windowed_x, &g_windowed_y);
    }
    if (SDL_SetWindowFullscreen(g_window, flags) == 0) {
        g_fullscreen = !g_fullscreen;
        // when leaving fullscreen, restore position/size
        if (!g_fullscreen) {
            SDL_SetWindowSize(g_window, g_windowed_w, g_windowed_h);
            SDL_SetWindowPosition(g_window, g_windowed_x, g_windowed_y);
        } else {
            // in fullscreen desktop, obtain new logical window size
            int w, h;
            SDL_GetWindowSize(g_window, &w, &h);
            OnWindowResized(w, h);
        }
    }
}

// Call this from your main SDL event loop to handle resizing/fullscreen hotkey
void HandleEvent(const SDL_Event& e) {
    if (!g_window) return;

    if (e.type == SDL_WINDOWEVENT) {
        switch (e.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                OnWindowResized(e.window.data1, e.window.data2);
                break;
            case SDL_WINDOWEVENT_MAXIMIZED:
            case SDL_WINDOWEVENT_RESTORED: {
                // maximize/restore may not provide size in the event -> query actual size
                int w = 0, h = 0;
                SDL_GetWindowSize(g_window, &w, &h);
                OnWindowResized(w, h);
                break;
            }
            default:
                break;
        }
    } else if (e.type == SDL_KEYDOWN) {
        // toggle fullscreen with F11
        if (e.key.keysym.sym == SDLK_F11) {
            ToggleFullscreen();
        }
    }
}

// Accessors/helpers
float GetScale() {
    return g_scale;
}

float GetDPIScale() {
    return g_dpi_scale;
}

// scale numeric values (positions, sizes)
int ScaleInt(int v) {
    return static_cast<int>(v * g_scale + 0.5f);
}

SDL_Rect ScaleRect(const SDL_Rect& r) {
    SDL_Rect out;
    out.x = ScaleInt(r.x);
    out.y = ScaleInt(r.y);
    out.w = ScaleInt(r.w);
    out.h = ScaleInt(r.h);
    return out;
}

} // namespace ui