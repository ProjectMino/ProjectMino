#include "menu_modern.h"
#include "debug_overlay.h"
#include "wallpapers.h"     // <- replace forward decls with this include
#include "classic.h"        // <- new: classic-mode options
#include "game.h"           // <- new: run the game in-place
#include "blitz.h"
#include <SDL2/SDL_image.h>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <cmath>
#include <iostream>   // added for debug logs
#include <SDL2/SDL_ttf.h>

// forward declaration so RunMainMenu can call it before the definition below
void OnSelectBlitz(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font);

// small clamp helper for pre-C++17 compatibility
template<typename T>
static T clamp_val(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Minimal helper: load texture or nullptr
static SDL_Texture* LoadTexture(SDL_Renderer* r, const std::string& path) {
    if (path.empty()) return nullptr;
    SDL_Surface* s = IMG_Load(path.c_str());
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

MenuResult RunMainMenu(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font, const std::string& bg_image_path) {
    MenuResult res{"", ""};
    if (!window || !renderer) { res.choice = "Exit"; return res; }
    // prefer project assets for menu and UI text; fallback to passed-in 'font'
    // use fonts found under src/assets (project layout shown in your screenshot)
    const std::string menu_font_path = "src/assets/display.otf";
    const std::string sub_font_path  = "src/assets/subtext.ttf";
    TTF_Font* menu_font_loaded = nullptr;
    TTF_Font* sub_font_loaded = nullptr;
    int last_menu_size = 0;
    auto EnsureFontsForSize = [&](int base_size) {
        int menu_size = std::max(24, base_size);
        int sub_size = std::max(14, base_size * 2 / 3);
        if (menu_font_loaded && last_menu_size == menu_size && sub_font_loaded) return;
        if (menu_font_loaded) { TTF_CloseFont(menu_font_loaded); menu_font_loaded = nullptr; }
        if (sub_font_loaded)  { TTF_CloseFont(sub_font_loaded);  sub_font_loaded = nullptr; }
        menu_font_loaded = TTF_OpenFont(menu_font_path.c_str(), menu_size);
        if (!menu_font_loaded) std::cerr << "menu font load failed: " << menu_font_path << "\n";
        sub_font_loaded = TTF_OpenFont(sub_font_path.c_str(), sub_size);
        if (!sub_font_loaded) std::cerr << "subtext font load failed: " << sub_font_path << "\n";
        last_menu_size = menu_size;
    };

    bool use_image = !bg_image_path.empty();
    SDL_Texture* bgtex = nullptr;
    bool bg_is_tile = false;

    // if a path was provided, load it; otherwise fetch from Unsplash sized to window
    if (use_image) {
        bgtex = LoadTexture(renderer, bg_image_path);
        if (bgtex) bg_is_tile = false;
    } else {
        int iw = 800, ih = 600;
        SDL_GetRendererOutputSize(renderer, &iw, &ih);
        bgtex = fetchUnsplashWallpaper(renderer, iw, ih);
        if (bgtex) {
            std::cerr << "fetchUnsplashWallpaper: got texture\n";
            bg_is_tile = false;
        } else {
            std::cerr << "fetchUnsplashWallpaper: failed, will use tile fallback\n";
            bg_is_tile = true; // tile will be assigned below
        }
    }

    // create small tile fallback
    SDL_Surface* tile_surf = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(tile_surf, nullptr, SDL_MapRGBA(tile_surf->format, 245,245,220,255));
    SDL_Rect rbox = {0,0,64,64};
    SDL_FillRect(tile_surf, &rbox, SDL_MapRGBA(tile_surf->format, 245,245,220,255));
    SDL_Texture* tiletex = SDL_CreateTextureFromSurface(renderer, tile_surf);
    SDL_FreeSurface(tile_surf);

    // if fetch failed earlier, use tile texture as background
    if (!bgtex && tiletex) {
        bgtex = tiletex;
        bg_is_tile = true;
    }

    // menu data
    std::vector<std::string> main_items = { "Singleplayer", "Multiplayer", "Options", "Exit" };
    std::vector<std::string> sp_sub = { "Classic", "Blitz", "40 Lines", "Cheese" };
    std::vector<std::string> mp_sub = { "Ranked", "Casual", "Custom Room" };
    std::vector<float> scales(main_items.size(), 1.0f);
    std::vector<float> underline(main_items.size(), 0.0f);
    const float anim_speed = 8.0f;
    int selected_idx = 0;
    int sub_selected = 0;
    enum class View { MAIN, SP_SUB, MP_SUB } view = View::MAIN;
    bool running = true;
    Uint64 last = SDL_GetTicks64();
    SDL_ShowCursor(SDL_ENABLE);

    bool need_refetch = false; // when window resized, refetch if using Unsplash

    // Exit confirmation / hold state
    bool exit_armed = false;              // first Enter press arms exit
    Uint64 exit_hold_start = 0;           // non-zero if Enter is held
    float exit_hold_progress = 0.0f;      // 0..1 progress for 5s hold
    const float exit_hold_required = 5.0f; // seconds to hold to auto-quit

    // NEW: inline singleplayer replacement state
    bool inlined_sp = false;
    std::vector<std::string> backup_main;
    // Classic-mode modal popup state
    bool show_classic_popup = false;
    bool classic_play_hover = false;
    bool classic_closed = false;
     auto enterInlineSP = [&]() {
         if (inlined_sp) return;
         backup_main = main_items;
         main_items = sp_sub;
         main_items.push_back("Back");
         scales.assign(main_items.size(), 1.0f);
         underline.assign(main_items.size(), 0.0f);
         selected_idx = 0;
         inlined_sp = true;
         // reset exit state when changing menu
         exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
     };
     auto restoreMain = [&]() {
         if (!inlined_sp) return;
         main_items = backup_main;
         scales.assign(main_items.size(), 1.0f);
         underline.assign(main_items.size(), 0.0f);
         selected_idx = 0;
         inlined_sp = false;
         // reset exit state when restoring
         exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
     };

    while (running) {
        Uint64 now = SDL_GetTicks64();
        float dt = (now - last) / 1000.0f;
        last = now;
        // no grid update

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
                res.choice = "Exit";
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                // when resized, refetch wallpaper if using dynamic source
                need_refetch = true;
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_UP:
                        selected_idx = (selected_idx - 1 + (int)main_items.size()) % (int)main_items.size();
                        exit_armed = false;
                        break;
                    case SDLK_DOWN:
                        selected_idx = (selected_idx + 1) % (int)main_items.size();
                        exit_armed = false;
                        break;
                    case SDLK_ESCAPE:
                        if (inlined_sp) restoreMain();
                        else { running = false; res.choice = "Exit"; }
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER: {
                        const std::string sel = main_items[selected_idx];
                        // If we've inlined the singleplayer submenu, handle its entries
                        if (inlined_sp) {
                            if (sel == "Back") {
                                restoreMain();
                            } else if (sel == "Classic") {
                                // show classic modal
                                show_classic_popup = true;
                            } else if (sel == "Blitz") {
                                // Launch the blitz flow (shows modal + starts game if Play pressed)
                                OnSelectBlitz(window, renderer, font);
                                // keep menu visible afterwards
                            } else {
                                // other singleplayer modes could start games in-place
                                // placeholder: run a normal game for now
                                RunGameSDL(window, renderer, font);
                            }
                        } else {
                            // main menu selections
                            if (sel == "Singleplayer") {
                                enterInlineSP();
                            } else if (sel == "Options") {
                                // TODO: options panel
                            } else if (sel == "Exit") {
                                running = false;
                                res.choice = "Exit";
                            }
                        }
                        // reset any exit hold state when making a selection
                        exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
                        break;
                    }
                    default:
                        break;
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                // basic mouse click navigation could be added here if desired
            }
        }

        // handle refetch if needed (do it outside event loop to avoid reentrancy)
        if (need_refetch && !use_image) {
            int iw = 800, ih = 600;
            SDL_GetRendererOutputSize(renderer, &iw, &ih);
            if (bgtex && !bg_is_tile) { SDL_DestroyTexture(bgtex); bgtex = nullptr; }
            bgtex = fetchUnsplashWallpaper(renderer, iw, ih);
            if (bgtex) {
                bg_is_tile = false;
                std::cerr << "refetch: got new wallpaper\n";
            } else {
                bg_is_tile = true;
                std::cerr << "refetch: failed, still using tile\n";
                if (tiletex) bgtex = tiletex;
            }
            need_refetch = false;
        }

        // Update exit hold progress (if user is holding Enter)
        if (exit_hold_start != 0) {
            Uint64 nowt = SDL_GetTicks64();
            float held = (nowt - exit_hold_start) / 1000.0f;
            exit_hold_progress = std::min(1.0f, held / exit_hold_required);
            if (exit_hold_progress >= 1.0f) {
                // completed hold -> quit
                res.choice = "Exit";
                running = false;
            }
        }

        // animations
        for (size_t i=0;i<main_items.size();++i) {
            float target = (int)i == selected_idx ? 1.12f : 1.0f;
            scales[i] += (target - scales[i]) * std::min(1.0f, dt * anim_speed);
            float ut = (int)i == selected_idx ? 1.0f : 0.0f;
            underline[i] += (ut - underline[i]) * std::min(1.0f, dt * (anim_speed * 1.2f));
        }

        // render background
        int w,h; SDL_GetRendererOutputSize(renderer, &w, &h);
        // use wallpapers helper to draw image stretched + 40% black tint
        renderWallpaperWithTint(renderer, bgtex, w, h);

        // top 50% transparent bar
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0,0,0,128);
        SDL_Rect topbar = {0,0,w, std::max(48, h/14)};
        SDL_RenderFillRect(renderer, &topbar);

        // draw menu stack (centered vertical)
         int base_font = std::max(24, h/12);
        
        // ensure fonts at appropriate size and pick menu font (fallback to provided font)
        EnsureFontsForSize(base_font);
        TTF_Font* f = menu_font_loaded ? menu_font_loaded : font;
         std::vector<SDL_Texture*> text_tex;
         std::vector<int> tws, ths;
         int gap = 18;
         for (auto &s : main_items) {
             SDL_Surface* surf = f ? TTF_RenderUTF8_Blended(f, s.c_str(), {230,230,230,255}) : nullptr;
             SDL_Texture* t = surf ? SDL_CreateTextureFromSurface(renderer, surf) : nullptr;
             if (surf) { tws.push_back(surf->w); ths.push_back(surf->h); SDL_FreeSurface(surf); }
             else { tws.push_back(200); ths.push_back(base_font); } // fallback sizes
             text_tex.push_back(t);
         }
         int total_menu_h = 0;
         for (size_t i=0;i<ths.size();++i) total_menu_h += ths[i] + gap;
         if (!tws.empty()) total_menu_h -= gap; // adjust last gap
         int start_y = (h - total_menu_h) / 2;
         for (size_t i=0;i<main_items.size();++i) {
             float scale = scales[i];
             SDL_Rect dst = { (w - (int)(tws[i] * scale)) / 2, start_y, (int)(tws[i] * scale), (int)(ths[i] * scale) };
             if (text_tex[i]) SDL_RenderCopy(renderer, text_tex[i], nullptr, &dst);

             // draw underline animation under each item
             float u = underline[i];
             if (u > 0.001f) {
                 int ul_w = (int)(dst.w * u);
                 int ul_h = std::max(3, (int)(4 * u));
                 int ul_x = dst.x + (dst.w - ul_w) / 2;
                 int ul_y = dst.y + dst.h + 8;
                 SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                 SDL_SetRenderDrawColor(renderer, 20,120,200, 220);
                 SDL_Rect ul = { ul_x, ul_y, ul_w, ul_h };
                 SDL_RenderFillRect(renderer, &ul);
                 // subtle glow
                 SDL_SetRenderDrawColor(renderer, 20,120,200, 64);
                 SDL_Rect glow = { ul_x - 6, ul_y - 6, ul_w + 12, ul_h + 12 };
                 SDL_RenderFillRect(renderer, &glow);
             }

             start_y += (int)(ths[i] * scale) + gap;
         }
         for (auto t : text_tex) if (t) SDL_DestroyTexture(t);
         text_tex.clear();

        // debug info (FPS + window size) top-left
        {
            float fps = dt > 0.0f ? (1.0f / dt) : 0.0f;
            char buf[128];
            snprintf(buf, sizeof(buf), "FPS: %.0f  %dx%d", fps, w, h);
            TTF_Font* dbgfont = sub_font_loaded ? sub_font_loaded : font;
            if (dbgfont) {
                SDL_Surface* s = TTF_RenderUTF8_Blended(dbgfont, buf, {200,200,200,255});
                if (s) {
                    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
                    SDL_Rect dst = { 8, 8, s->w, s->h };
                    SDL_RenderCopy(renderer, t, nullptr, &dst);
                    SDL_DestroyTexture(t);
                    SDL_FreeSurface(s);
                }
            }
        }

        // Classic modal popup rendering (draw on top of everything if requested)
         if (show_classic_popup) {
                // pick a larger base size for modal so header/play scale with window
                int modal_base = std::max(32, h / 12); // larger font on tall windows
                EnsureFontsForSize(modal_base);
                TTF_Font* header_font = menu_font_loaded ? menu_font_loaded : font;
                TTF_Font* small_font = sub_font_loaded ? sub_font_loaded : (menu_font_loaded ? menu_font_loaded : font);

                // modal sizing (bounded by sensible max)
                int mw = std::min(w - 160, std::max(600, w * 7 / 10));
                int mh = std::min(h - 140, std::max(420, h * 7 / 10));
                int mx0 = (w - mw) / 2;
                int my0 = (h - mh) / 2;

                // Draw a slightly larger near-black background behind the modal
                // that extends past the bottom of the window for a "goes off the page" effect.
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(renderer, 12, 12, 12, 255);
                SDL_Rect bg_modal = { mx0 - 40, my0 - 24, mw + 80, mh + (h / 3) };
                SDL_RenderFillRect(renderer, &bg_modal);

                // layout columns: left ~60%, right ~40% with a consistent gap so panels don't overlap
                const int panel_gap = 28;
                // same vertical offset used for click-hit tests: push panels down inside the modal
                const int modal_y_offset = 160;
                int left_w = std::max(200, (mw - panel_gap) * 60 / 100);
                int right_w = std::max(180, mw - left_w - panel_gap);
                SDL_Rect left_panel = { mx0 + 20, my0 + modal_y_offset, left_w, mh - (modal_y_offset + 20) };
                SDL_Rect right_panel = { left_panel.x + left_panel.w + panel_gap, my0 + modal_y_offset, right_w, mh - (modal_y_offset + 20) };

                // header (centered above panels)
                if (header_font) {
                    SDL_Surface* sh = TTF_RenderUTF8_Blended(header_font, "Classic", {240,240,240,255});
                    if (sh) {
                        SDL_Texture* th = SDL_CreateTextureFromSurface(renderer, sh);
                        SDL_Rect hdr = { mx0 + (mw - sh->w) / 2, my0 + 12, sh->w, sh->h };
                        SDL_RenderCopy(renderer, th, nullptr, &hdr);
                        SDL_DestroyTexture(th);
                        SDL_FreeSurface(sh);
                    }
                }

                // compute inner boxes on the left panel (match mouse-hit calculations)
                const int inner_vpad = 12;
                const int inner_hpad = 16;
                const int box_spacing = 10;
                int left_h = left_panel.h;
                int avail_h = left_h - (inner_vpad * 2) - (box_spacing * 2);
                int pb_h = clamp_val<int>((int)(avail_h * 0.30f), 56, std::max(56, avail_h - 80));
                int play_h = clamp_val<int>((int)(avail_h * 0.20f), 48, std::max(48, avail_h - pb_h - 40));
                int pb_box_x = left_panel.x + inner_hpad;
                int pb_box_y = left_panel.y + inner_vpad;
                int pb_box_w = left_panel.w - inner_hpad * 2;
                SDL_Rect pb_box = { pb_box_x, pb_box_y, pb_box_w, pb_h };
                int play_box_x = pb_box_x;
                int play_box_y = pb_box_y + pb_h + box_spacing;
                int play_box_w = pb_box_w;
                int play_box_h = play_h;
                SDL_Rect play_box = { play_box_x, play_box_y, play_box_w, play_box_h };
                int replays_box_x = pb_box_x;
                int replays_box_y = play_box_y + play_box_h + box_spacing;
                int replays_box_h = (left_panel.y + left_panel.h) - replays_box_y - inner_vpad;
                if (replays_box_h < 40) replays_box_h = std::max(40, (left_panel.y + left_panel.h) - replays_box_y);
                SDL_Rect replays_box = { replays_box_x, replays_box_y, pb_box_w, replays_box_h };

                // draw panel backgrounds using black/gray theme
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                // slightly lighter dark-gray panels
                SDL_SetRenderDrawColor(renderer, 36,36,36, 230);
                SDL_RenderFillRect(renderer, &left_panel);
                SDL_RenderFillRect(renderer, &right_panel);

                // inner boxes: slightly darker than panels for subtle contrast
                SDL_SetRenderDrawColor(renderer, 24,24,24,220);
                SDL_RenderFillRect(renderer, &pb_box);
                SDL_RenderFillRect(renderer, &play_box);
                SDL_RenderFillRect(renderer, &replays_box);

                // borders in medium gray
                SDL_SetRenderDrawColor(renderer, 80,80,80,255);
                SDL_RenderDrawRect(renderer, &pb_box);
                SDL_RenderDrawRect(renderer, &play_box);
                SDL_RenderDrawRect(renderer, &replays_box);
                SDL_RenderDrawRect(renderer, &right_panel);

                // Play button area: use tighter inner padding to ensure it stays inside play_box
                SDL_Rect play_button_inner = { play_box.x + 10, play_box.y + (play_box.h / 8), play_box.w - 20, play_box.h - (play_box.h / 4) };
                if (play_button_inner.h < 34) { play_button_inner.h = std::max(34, play_box.h - 8); play_button_inner.y = play_box.y + (play_box.h - play_button_inner.h)/2; }
                int mouse_x, mouse_y;
                SDL_GetMouseState(&mouse_x, &mouse_y);
                classic_play_hover = (mouse_x >= play_button_inner.x && mouse_x <= play_button_inner.x + play_button_inner.w && mouse_y >= play_button_inner.y && mouse_y <= play_button_inner.y + play_button_inner.h);
                // Play button colors for black/gray theme (hover: light gray, normal: medium gray)
                if (classic_play_hover) {
                    SDL_SetRenderDrawColor(renderer, 200,200,200, 220);
                } else {
                    SDL_SetRenderDrawColor(renderer, 70,70,70, 160);
                }
                SDL_RenderFillRect(renderer, &play_button_inner);

                // triangle icon scaled to button height (keep margin so it doesn't touch edges)
                int tri_h = std::min( (int)(play_button_inner.h * 0.7f), 48 );
                int tri_w = std::max(12, tri_h * 2 / 3);
                int tx = play_button_inner.x + 14;
                int ty = play_button_inner.y + (play_button_inner.h - tri_h) / 2;
                SDL_SetRenderDrawColor(renderer, 255,255,255,255);
                for (int r = 0; r < tri_h; ++r) {
                    float frac = (float)r / (float)tri_h;
                    int line_w = (int)(frac * tri_w);
                    SDL_RenderDrawLine(renderer, tx, ty + r, tx + line_w, ty + r);
                }

                // Play text scaled from header_font metrics and placed so it doesn't overlap the triangle
                if (header_font) {
                    SDL_Surface* sp = TTF_RenderUTF8_Blended(header_font, "Play", {255,255,255,255});
                    if (sp) {
                        SDL_Texture* tp = SDL_CreateTextureFromSurface(renderer, sp);
                        int text_x = play_button_inner.x + tri_w + 30;
                        int text_y = play_button_inner.y + (play_button_inner.h - sp->h) / 2;
                        // clamp so text doesn't overflow the button
                        int max_text_w = play_button_inner.w - (tri_w + 44);
                        int draw_w = std::min(sp->w, max_text_w);
                        SDL_Rect dst = { text_x, text_y, draw_w, sp->h };
                        SDL_RenderCopy(renderer, tp, nullptr, &dst);
                        SDL_DestroyTexture(tp);
                        SDL_FreeSurface(sp);
                    }
                }

                // Personal Best area (use small font) - ensure positions fit in pb_box
                if (small_font) {
                    SDL_Surface* s0 = TTF_RenderUTF8_Blended(small_font, "0 in 0.00.00", {240,240,240,255});
                    if (s0) {
                        SDL_Texture* t0 = SDL_CreateTextureFromSurface(renderer, s0);
                        SDL_Rect d0 = { pb_box.x + 14, pb_box.y + 12, s0->w, s0->h };
                        if (d0.x + d0.w > pb_box.x + pb_box.w - 8) d0.w = std::max(8, (pb_box.x + pb_box.w - 8) - d0.x);
                        SDL_RenderCopy(renderer, t0, nullptr, &d0);
                        SDL_DestroyTexture(t0);
                        SDL_FreeSurface(s0);
                    }
                    SDL_Surface* s1 = TTF_RenderUTF8_Blended(small_font, "Personal Best", {170,190,200,255});
                    if (s1) {
                        SDL_Texture* t1 = SDL_CreateTextureFromSurface(renderer, s1);
                        SDL_Rect d1 = { pb_box.x + 14, pb_box.y + 12 + ((pb_box.h > 60) ? 44 : 28), s1->w, s1->h };
                        if (d1.y + d1.h > pb_box.y + pb_box.h - 8) d1.y = pb_box.y + pb_box.h - 8 - d1.h;
                        SDL_RenderCopy(renderer, t1, nullptr, &d1);
                        SDL_DestroyTexture(t1);
                        SDL_FreeSurface(s1);
                    }
                }

                // Replays heading (scaled) - fit inside replays_box
                if (header_font) {
                    SDL_Surface* rh = TTF_RenderUTF8_Blended(header_font, "Replays", {230,230,230,255});
                    if (rh) {
                        SDL_Texture* trh = SDL_CreateTextureFromSurface(renderer, rh);
                        SDL_Rect drh = { replays_box.x + 10, replays_box.y + 8, rh->w, rh->h };
                        SDL_RenderCopy(renderer, trh, nullptr, &drh);
                        SDL_DestroyTexture(trh);
                        SDL_FreeSurface(rh);
                    }
                }
                if (small_font) {
                    SDL_Surface* nop = TTF_RenderUTF8_Blended(small_font, "No replays in this mode", {130,140,150,255});
                    if (nop) {
                        SDL_Texture* tn = SDL_CreateTextureFromSurface(renderer, nop);
                        SDL_Rect dn = { replays_box.x + (replays_box.w - nop->w)/2, replays_box.y + (replays_box.h - nop->h)/2, nop->w, nop->h };
                        if (dn.x < replays_box.x + 8) dn.x = replays_box.x + 8;
                        SDL_RenderCopy(renderer, tn, nullptr, &dn);
                        SDL_DestroyTexture(tn);
                        SDL_FreeSurface(nop);
                    }
                }

                // Rankings title + placeholder on right panel (scaled)
                if (header_font) {
                    SDL_Surface* rh = TTF_RenderUTF8_Blended(header_font, "Rankings", {230,230,230,255});
                    if (rh) {
                        SDL_Texture* trh = SDL_CreateTextureFromSurface(renderer, rh);
                        SDL_Rect drh = { right_panel.x + 18, right_panel.y + 10, rh->w, rh->h };
                        SDL_RenderCopy(renderer, trh, nullptr, &drh);
                        SDL_DestroyTexture(trh);
                        SDL_FreeSurface(rh);
                    }
                }
                if (small_font) {
                    SDL_Surface* rem = TTF_RenderUTF8_Blended(small_font, "Can't fetch rankings for this mode.", {120,140,155,255});
                    if (rem) {
                        SDL_Texture* trem = SDL_CreateTextureFromSurface(renderer, rem);
                        const float rank_scale = 0.80f;
                        SDL_Rect drem = { right_panel.x + (right_panel.w - (int)(rem->w*rank_scale))/2,
                                          right_panel.y + (right_panel.h - (int)(rem->h*rank_scale))/2,
                                          (int)(rem->w*rank_scale),
                                          (int)(rem->h*rank_scale) };
                        SDL_RenderCopy(renderer, trem, nullptr, &drem);
                        SDL_DestroyTexture(trem);
                        SDL_FreeSurface(rem);
                    }
                }
         }

        // present frame
        SDL_RenderPresent(renderer);

        // tiny delay to avoid pegging CPU (and allow SDL to schedule)
        SDL_Delay(8);
    } // end while (running)

    // cleanup
    if (bgtex && !bg_is_tile) SDL_DestroyTexture(bgtex);
    if (tiletex) SDL_DestroyTexture(tiletex);
    if (menu_font_loaded) { TTF_CloseFont(menu_font_loaded); menu_font_loaded = nullptr; }
    if (sub_font_loaded)  { TTF_CloseFont(sub_font_loaded);  sub_font_loaded = nullptr; }

    return res;
}

// Minimal modal that mimics the classic modal but specialized for Blitz.
// Returns true if the user clicked "Play", false for Back/Cancel.
static bool ShowClassicMenu(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font, bool isBlitz) {
    if (!window || !renderer) return false;
    int w = 800, h = 600;
    SDL_GetRendererOutputSize(renderer, &w, &h);

    bool running = true;
    bool play_pressed = false;
    SDL_ShowCursor(SDL_ENABLE);

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) { play_pressed = true; running = false; break; }
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x, my = e.button.y;
                // modal sizing
                int mw = std::min(w - 160, std::max(600, w * 7 / 10));
                int mh = std::min(h - 140, std::max(360, h * 5 / 10));
                int mx0 = (w - mw) / 2;
                int my0 = (h - mh) / 2;
                // play button rect (simple centered button near bottom)
                int pb_w = std::max(160, mw/3);
                int pb_h = 56;
                SDL_Rect play_rect = { mx0 + (mw - pb_w)/2, my0 + mh - pb_h - 28, pb_w, pb_h };
                // back button to the left of play
                int bb_w = std::max(120, mw/5);
                SDL_Rect back_rect = { mx0 + 28, my0 + mh - pb_h - 28, bb_w, pb_h };

                if (mx >= play_rect.x && mx <= play_rect.x + play_rect.w && my >= play_rect.y && my <= play_rect.y + play_rect.h) {
                    play_pressed = true; running = false; break;
                }
                if (mx >= back_rect.x && mx <= back_rect.x + back_rect.w && my >= back_rect.y && my <= back_rect.y + back_rect.h) {
                    running = false; break;
                }
                // click outside modal closes it
                if (!(mx >= mx0 && mx <= mx0 + mw && my >= my0 && my <= my0 + mh)) {
                    running = false;
                    break;
                }
            }
        }

        // render simple modal
        SDL_SetRenderDrawColor(renderer, 0,0,0,200);
        SDL_RenderFillRect(renderer, nullptr);

        // modal box
        int mw = std::min(w - 160, std::max(600, w * 7 / 10));
        int mh = std::min(h - 140, std::max(360, h * 5 / 10));
        int mx0 = (w - mw) / 2;
        int my0 = (h - mh) / 2;
        SDL_Rect modal = { mx0, my0, mw, mh };
        SDL_SetRenderDrawColor(renderer, 36,36,36, 230);
        SDL_RenderFillRect(renderer, &modal);
        SDL_SetRenderDrawColor(renderer, 80,80,80,255);
        SDL_RenderDrawRect(renderer, &modal);

        // header
        std::string header = isBlitz ? "Blitz" : "Classic";
        if (font) {
            SDL_Surface* sh = TTF_RenderUTF8_Blended(font, header.c_str(), {240,240,240,255});
            if (sh) {
                SDL_Texture* th = SDL_CreateTextureFromSurface(renderer, sh);
                SDL_Rect hdr = { mx0 + 20, my0 + 16, sh->w, sh->h };
                SDL_RenderCopy(renderer, th, nullptr, &hdr);
                SDL_DestroyTexture(th);
                SDL_FreeSurface(sh);
            }
        }

        // body text: show Blitz-specific info when isBlitz
        std::string body = isBlitz ? "Score as many points as you can within 2:00.\nThe game will end when time expires." :
                                     "Play a standard game of Tetris.";
        // render body with basic line splitting
        int by = my0 + 72;
        int line_h = 20;
        size_t start = 0;
        while (start < body.size()) {
            size_t nl = body.find('\n', start);
            std::string line = (nl==std::string::npos) ? body.substr(start) : body.substr(start, nl-start);
            if (font) {
                SDL_Surface* sb = TTF_RenderUTF8_Blended(font, line.c_str(), {200,200,200,255});
                if (sb) {
                    SDL_Texture* tb = SDL_CreateTextureFromSurface(renderer, sb);
                    SDL_Rect db = { mx0 + 20, by, sb->w, sb->h };
                    SDL_RenderCopy(renderer, tb, nullptr, &db);
                    SDL_DestroyTexture(tb);
                    SDL_FreeSurface(sb);
                    by += sb->h + 6;
                } else {
                    by += line_h;
                }
            } else {
                by += line_h;
            }
            if (nl==std::string::npos) break;
            start = nl + 1;
        }

        // play & back buttons
        int pb_w = std::max(160, mw/3);
        int pb_h = 56;
        SDL_Rect play_rect = { mx0 + (mw - pb_w)/2, my0 + mh - pb_h - 28, pb_w, pb_h };
        SDL_Rect back_rect = { mx0 + 28, my0 + mh - pb_h - 28, std::max(120, mw/5), pb_h };

        // draw back
        SDL_SetRenderDrawColor(renderer, 70,70,70, 200);
        SDL_RenderFillRect(renderer, &back_rect);
        SDL_SetRenderDrawColor(renderer, 255,255,255,255);
        if (font) {
            SDL_Surface* sb = TTF_RenderUTF8_Blended(font, "Back", {240,240,240,255});
            if (sb) {
                SDL_Texture* tb = SDL_CreateTextureFromSurface(renderer, sb);
                SDL_Rect db = { back_rect.x + (back_rect.w - sb->w)/2, back_rect.y + (back_rect.h - sb->h)/2, sb->w, sb->h };
                SDL_RenderCopy(renderer, tb, nullptr, &db);
                SDL_DestroyTexture(tb);
                SDL_FreeSurface(sb);
            }
        }

        // draw play
        SDL_SetRenderDrawColor(renderer, 200,200,200, 230);
        SDL_RenderFillRect(renderer, &play_rect);
        if (font) {
            SDL_Surface* sp = TTF_RenderUTF8_Blended(font, "Play", {12,12,12,255});
            if (sp) {
                SDL_Texture* tp = SDL_CreateTextureFromSurface(renderer, sp);
                SDL_Rect dp = { play_rect.x + (play_rect.w - sp->w)/2, play_rect.y + (play_rect.h - sp->h)/2, sp->w, sp->h };
                SDL_RenderCopy(renderer, tp, nullptr, &dp);
                SDL_DestroyTexture(tp);
                SDL_FreeSurface(sp);
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(12);
    }

    return play_pressed;
}

// Small standalone Blitz stats modal that reuses the visual layout of the larger
// "Classic" modal in RunMainMenu but shows Blitz-specific stats.
// Returns true when the user clicks Play, false for Back/cancel.
static bool ShowBlitzStatsModal(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font) {
    if (!window || !renderer) return false;
    int w = 800, h = 600;
    SDL_GetRendererOutputSize(renderer, &w, &h);

    bool running = true;
    bool play_pressed = false;
    SDL_ShowCursor(SDL_ENABLE);

    // read blitz duration from global options
    int dur_ms = std::max(0, g_blitz_mode_options.duration_ms);
    int dur_s = dur_ms / 1000;
    int mm = dur_s / 60;
    int ss = dur_s % 60;
    char dur_buf[32];
    snprintf(dur_buf, sizeof(dur_buf), "%02d:%02d", mm, ss);

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) { play_pressed = true; running = false; break; }
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x, my = e.button.y;
                // modal sizing
                int mw = std::min(w - 160, std::max(600, w * 7 / 10));
                int mh = std::min(h - 140, std::max(420, h * 7 / 10));
                int mx0 = (w - mw) / 2;
                int my0 = (h - mh) / 2;

                // left/right panels and play/back locations mimic RunMainMenu's layout
                const int panel_gap = 28;
                const int modal_y_offset = 160;
                int left_w = std::max(200, (mw - panel_gap) * 60 / 100);
                int right_w = std::max(180, mw - left_w - panel_gap);
                SDL_Rect left_panel = { mx0 + 20, my0 + modal_y_offset, left_w, mh - (modal_y_offset + 20) };
                SDL_Rect right_panel = { left_panel.x + left_panel.w + panel_gap, my0 + modal_y_offset, right_w, mh - (modal_y_offset + 20) };

                // Play & Back buttons
                int pb_w = std::max(160, mw/3);
                int pb_h = 56;
                SDL_Rect play_rect = { mx0 + (mw - pb_w)/2, my0 + mh - pb_h - 28, pb_w, pb_h };
                SDL_Rect back_rect = { mx0 + 28, my0 + mh - pb_h - 28, std::max(120, mw/5), pb_h };

                if (mx >= play_rect.x && mx <= play_rect.x + play_rect.w && my >= play_rect.y && my <= play_rect.y + play_rect.h) {
                    play_pressed = true; running = false; break;
                }
                if (mx >= back_rect.x && mx <= back_rect.x + back_rect.w && my >= back_rect.y && my <= back_rect.y + back_rect.h) {
                    running = false; break;
                }

                // click outside modal closes it
                if (!(mx >= mx0 && mx <= mx0 + mw && my >= my0 && my <= my0 + mh)) {
                    running = false;
                    break;
                }
            }
        }

        // render background fade
        SDL_SetRenderDrawColor(renderer, 0,0,0,200);
        SDL_RenderFillRect(renderer, nullptr);

        // modal box
        int mw = std::min(w - 160, std::max(600, w * 7 / 10));
        int mh = std::min(h - 140, std::max(420, h * 7 / 10));
        int mx0 = (w - mw) / 2;
        int my0 = (h - mh) / 2;
        SDL_Rect modal = { mx0, my0, mw, mh };
        SDL_SetRenderDrawColor(renderer, 36,36,36, 230);
        SDL_RenderFillRect(renderer, &modal);
        SDL_SetRenderDrawColor(renderer, 80,80,80,255);
        SDL_RenderDrawRect(renderer, &modal);

        // header
        std::string header = "Blitz";
        if (font) {
            SDL_Surface* sh = TTF_RenderUTF8_Blended(font, header.c_str(), {240,240,240,255});
            if (sh) {
                SDL_Texture* th = SDL_CreateTextureFromSurface(renderer, sh);
                SDL_Rect hdr = { mx0 + 20, my0 + 16, sh->w, sh->h };
                SDL_RenderCopy(renderer, th, nullptr, &hdr);
                SDL_DestroyTexture(th);
                SDL_FreeSurface(sh);
            }
        }

        // layout panels (copy of RunMainMenu's layout)
        const int panel_gap = 28;
        const int modal_y_offset = 160;
        int left_w = std::max(200, (mw - panel_gap) * 60 / 100);
        int right_w = std::max(180, mw - left_w - panel_gap);
        SDL_Rect left_panel = { mx0 + 20, my0 + modal_y_offset, left_w, mh - (modal_y_offset + 20) };
        SDL_Rect right_panel = { left_panel.x + left_panel.w + panel_gap, my0 + modal_y_offset, right_w, mh - (modal_y_offset + 20) };

        // panel backgrounds and inner boxes
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 36,36,36, 230);
        SDL_RenderFillRect(renderer, &left_panel);
        SDL_RenderFillRect(renderer, &right_panel);

        const int inner_vpad = 12;
        const int inner_hpad = 16;
        const int box_spacing = 10;
        int left_h = left_panel.h;
        int avail_h = left_h - (inner_vpad * 2) - (box_spacing * 2);
        int pb_h = clamp_val<int>((int)(avail_h * 0.30f), 56, std::max(56, avail_h - 80));
        int play_h = clamp_val<int>((int)(avail_h * 0.20f), 48, std::max(48, avail_h - pb_h - 40));
        int pb_box_x = left_panel.x + inner_hpad;
        int pb_box_y = left_panel.y + inner_vpad;
        int pb_box_w = left_panel.w - inner_hpad * 2;
        SDL_Rect pb_box = { pb_box_x, pb_box_y, pb_box_w, pb_h };
        int play_box_x = pb_box_x;
        int play_box_y = pb_box_y + pb_h + box_spacing;
        int play_box_w = pb_box_w;
        int play_box_h = play_h;
        SDL_Rect play_box = { play_box_x, play_box_y, play_box_w, play_box_h };
        int replays_box_x = pb_box_x;
        int replays_box_y = play_box_y + play_box_h + box_spacing;
        int replays_box_h = (left_panel.y + left_panel.h) - replays_box_y - inner_vpad;
        if (replays_box_h < 40) replays_box_h = std::max(40, (left_panel.y + left_panel.h) - replays_box_y);
        SDL_Rect replays_box = { replays_box_x, replays_box_y, pb_box_w, replays_box_h };

        SDL_SetRenderDrawColor(renderer, 24,24,24,220);
        SDL_RenderFillRect(renderer, &pb_box);
        SDL_RenderFillRect(renderer, &play_box);
        SDL_RenderFillRect(renderer, &replays_box);

        SDL_SetRenderDrawColor(renderer, 80,80,80,255);
        SDL_RenderDrawRect(renderer, &pb_box);
        SDL_RenderDrawRect(renderer, &play_box);
        SDL_RenderDrawRect(renderer, &replays_box);
        SDL_RenderDrawRect(renderer, &right_panel);

        // Play button in left area
        SDL_Rect play_button_inner = { play_box.x + 10, play_box.y + (play_box.h / 8), play_box.w - 20, play_box.h - (play_box.h / 4) };
        if (play_button_inner.h < 34) { play_button_inner.h = std::max(34, play_box.h - 8); play_button_inner.y = play_box.y + (play_box.h - play_button_inner.h)/2; }
        int mouse_x, mouse_y;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        bool play_hover = (mouse_x >= play_button_inner.x && mouse_x <= play_button_inner.x + play_button_inner.w && mouse_y >= play_button_inner.y && mouse_y <= play_button_inner.y + play_button_inner.h);
        if (play_hover) SDL_SetRenderDrawColor(renderer, 200,200,200, 220); else SDL_SetRenderDrawColor(renderer, 70,70,70, 160);
        SDL_RenderFillRect(renderer, &play_button_inner);

        // Draw small triangle icon for play button
        int tri_h = std::min( (int)(play_button_inner.h * 0.7f), 48 );
        int tri_w = std::max(12, tri_h * 2 / 3);
        int tx = play_button_inner.x + 14;
        int ty = play_button_inner.y + (play_button_inner.h - tri_h) / 2;
        SDL_SetRenderDrawColor(renderer, 255,255,255,255);
        for (int r = 0; r < tri_h; ++r) {
            float frac = (float)r / (float)tri_h;
            int line_w = (int)(frac * tri_w);
            SDL_RenderDrawLine(renderer, tx, ty + r, tx + line_w, ty + r);
        }
        if (font) {
            SDL_Surface* sp = TTF_RenderUTF8_Blended(font, "Play", {255,255,255,255});
            if (sp) {
                SDL_Texture* tp = SDL_CreateTextureFromSurface(renderer, sp);
                int text_x = play_button_inner.x + tri_w + 30;
                int text_y = play_button_inner.y + (play_button_inner.h - sp->h) / 2;
                int max_text_w = play_button_inner.w - (tri_w + 44);
                int draw_w = std::min(sp->w, max_text_w);
                SDL_Rect dst = { text_x, text_y, draw_w, sp->h };
                SDL_RenderCopy(renderer, tp, nullptr, &dst);
                SDL_DestroyTexture(tp);
                SDL_FreeSurface(sp);
            }
        }

        // Draw Blitz-specific stats into the Personal Best box (pb_box)
        if (font) {
            // Header
            SDL_Surface* s0 = TTF_RenderUTF8_Blended(font, "Blitz Stats", {240,240,240,255});
            if (s0) {
                SDL_Texture* t0 = SDL_CreateTextureFromSurface(renderer, s0);
                SDL_Rect d0 = { pb_box.x + 14, pb_box.y + 8, s0->w, s0->h };
                SDL_RenderCopy(renderer, t0, nullptr, &d0);
                SDL_DestroyTexture(t0);
                SDL_FreeSurface(s0);
            }
            // Example stat lines: Personal Best, Games Played, Duration
            char buf1[64]; snprintf(buf1, sizeof(buf1), "Personal Best: %d", 0);
            char buf2[64]; snprintf(buf2, sizeof(buf2), "Games Played: %d", 0);
            char buf3[64]; snprintf(buf3, sizeof(buf3), "Duration: %s", dur_buf);

            SDL_Surface* s1 = TTF_RenderUTF8_Blended(font, buf1, {200,200,200,255});
            SDL_Surface* s2 = TTF_RenderUTF8_Blended(font, buf2, {200,200,200,255});
            SDL_Surface* s3 = TTF_RenderUTF8_Blended(font, buf3, {200,200,200,255});
            int sy = pb_box.y + 36;
            if (s1) { SDL_Texture* t1 = SDL_CreateTextureFromSurface(renderer, s1); SDL_Rect d = { pb_box.x + 14, sy, s1->w, s1->h }; SDL_RenderCopy(renderer, t1, nullptr, &d); SDL_DestroyTexture(t1); sy += s1->h + 6; SDL_FreeSurface(s1); }
            if (s2) { SDL_Texture* t2 = SDL_CreateTextureFromSurface(renderer, s2); SDL_Rect d = { pb_box.x + 14, sy, s2->w, s2->h }; SDL_RenderCopy(renderer, t2, nullptr, &d); SDL_DestroyTexture(t2); sy += s2->h + 6; SDL_FreeSurface(s2); }
            if (s3) { SDL_Texture* t3 = SDL_CreateTextureFromSurface(renderer, s3); SDL_Rect d = { pb_box.x + 14, sy, s3->w, s3->h }; SDL_RenderCopy(renderer, t3, nullptr, &d); SDL_DestroyTexture(t3); SDL_FreeSurface(s3); }
        }

        // Right panel placeholder content (leaderboard / rankings)
        if (font) {
            SDL_Surface* rh = TTF_RenderUTF8_Blended(font, "Leaderboard", {230,230,230,255});
            if (rh) {
                SDL_Texture* trh = SDL_CreateTextureFromSurface(renderer, rh);
                SDL_Rect drh = { right_panel.x + 18, right_panel.y + 10, rh->w, rh->h };
                SDL_RenderCopy(renderer, trh, nullptr, &drh);
                SDL_DestroyTexture(trh);
                SDL_FreeSurface(rh);
            }
            SDL_Surface* rem = TTF_RenderUTF8_Blended(font, "No leaderboard data available.", {120,140,155,255});
            if (rem) {
                SDL_Texture* trem = SDL_CreateTextureFromSurface(renderer, rem);
                SDL_Rect drem = { right_panel.x + (right_panel.w - rem->w)/2, right_panel.y + (right_panel.h - rem->h)/2, rem->w, rem->h };
                SDL_RenderCopy(renderer, trem, nullptr, &drem);
                SDL_DestroyTexture(trem);
                SDL_FreeSurface(rem);
            }
        }

        // Back & Play buttons (bottom)
        SDL_Rect back_rect = { mx0 + 28, my0 + mh - 56 - 28, std::max(120, mw/5), 56 };
        SDL_Rect play_rect = { mx0 + (mw - std::max(160, mw/3))/2, my0 + mh - 56 - 28, std::max(160, mw/3), 56 };

        SDL_SetRenderDrawColor(renderer, 70,70,70, 200);
        SDL_RenderFillRect(renderer, &back_rect);
        if (font) {
            SDL_Surface* sb = TTF_RenderUTF8_Blended(font, "Back", {240,240,240,255});
            if (sb) {
                SDL_Texture* tb = SDL_CreateTextureFromSurface(renderer, sb);
                SDL_Rect db = { back_rect.x + (back_rect.w - sb->w)/2, back_rect.y + (back_rect.h - sb->h)/2, sb->w, sb->h };
                SDL_RenderCopy(renderer, tb, nullptr, &db);
                SDL_DestroyTexture(tb);
                SDL_FreeSurface(sb);
            }
        }

        SDL_SetRenderDrawColor(renderer, 200,200,200, 230);
        SDL_RenderFillRect(renderer, &play_rect);
        if (font) {
            SDL_Surface* sp = TTF_RenderUTF8_Blended(font, "Play", {12,12,12,255});
            if (sp) {
                SDL_Texture* tp = SDL_CreateTextureFromSurface(renderer, sp);
                SDL_Rect dp = { play_rect.x + (play_rect.w - sp->w)/2, play_rect.y + (play_rect.h - sp->h)/2, sp->w, sp->h };
                SDL_RenderCopy(renderer, tp, nullptr, &dp);
                SDL_DestroyTexture(tp);
                SDL_FreeSurface(sp);
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(12);
    }

    return play_pressed;
}

// user selected "Blitz" in main menu:
void OnSelectBlitz(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font){
    // configure global Blitz options
    g_blitz_mode_options.enabled = true;
    g_blitz_mode_options.duration_ms = 120000; // 2 minutes; change if you expose duration in UI

    // show a larger Blitz stats modal (layout copied from the Classic modal)
    bool user_pressed_play = ShowBlitzStatsModal(window, renderer, font);

    if(user_pressed_play){
        // Start a brand new Blitz game. RunGameSDL will construct Game,
        // which reads g_blitz_mode_options in its constructor and starts blitz if enabled.
        RunGameSDL(window, renderer, font);

        // clear the flag after the game ends so normal games are unaffected
        g_blitz_mode_options.enabled = false;
    }
}
