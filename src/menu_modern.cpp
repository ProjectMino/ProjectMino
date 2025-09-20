#include "menu_modern.h"
#include "debug_overlay.h"
#include "wallpapers.h"     // <- replace forward decls with this include
#include <SDL2/SDL_image.h>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <cmath>
#include <iostream>   // added for debug logs
#include <SDL2/SDL_ttf.h>

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
            if (e.type == SDL_QUIT) { res.choice = "Exit"; running = false; }
            else if (e.type == SDL_KEYDOWN) {
                // If the Classic modal is open, handle keys here and skip the normal menu key handling.
                if (show_classic_popup) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        show_classic_popup = false;
                    } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        res.choice = "Singleplayer:Classic";
                        running = false;
                    }
                    // consume this event
                    continue;
                }
                 if (e.key.keysym.sym == SDLK_ESCAPE) {
                     if (view == View::MAIN) {
                         if (inlined_sp) { restoreMain(); }
                         else { res.choice = "Exit"; running = false; }
                     } else { view = View::MAIN; sub_selected = 0; }
                 } else if (e.key.keysym.sym == SDLK_UP) {
                     if (view == View::MAIN) {
                         selected_idx = (selected_idx - 1 + main_items.size()) % main_items.size();
                         // moving selection cancels exit state
                         exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
                     } else {
                         int mx = (view==View::SP_SUB) ? (int)sp_sub.size() : (int)mp_sub.size();
                         sub_selected = (sub_selected - 1 + mx) % mx;
                     }
                 } else if (e.key.keysym.sym == SDLK_DOWN) {
                     if (view == View::MAIN) {
                         selected_idx = (selected_idx + 1) % main_items.size();
                         // moving selection cancels exit state
                         exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
                     } else {
                         int mx = (view==View::SP_SUB) ? (int)sp_sub.size() : (int)mp_sub.size();
                         sub_selected = (sub_selected + 1) % mx;
                     }
                 } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                     // handle enter press start (keydown)
                     std::string curLabel = (view == View::MAIN && selected_idx >= 0 && selected_idx < (int)main_items.size()) ? main_items[selected_idx] : "";
                     if (view == View::MAIN && curLabel == "Exit") {
                         // If already armed, a second Enter press instantly quits
                         if (!e.key.repeat && exit_armed) {
                             res.choice = "Exit";
                             running = false;
                         } else {
                             // first press arms; start hold timer to allow long hold auto-quit
                             exit_armed = true;
                             if (exit_hold_start == 0) exit_hold_start = SDL_GetTicks64();
                             // do not immediately quit here
                         }
                     } else {
                         // not the exit special handling; keep existing behavior
                         if (view == View::MAIN) {
                             if (!inlined_sp) {
                                if (selected_idx == 0) { // Singleplayer
                                    enterInlineSP();
                                } else if (selected_idx == 1) { view = View::MP_SUB; sub_selected = 0; }
                                else if (selected_idx == 2) {
                                    // options placeholder
                                } else { res.choice = "Exit"; running = false; }
                             } else {
                                const std::string &label = main_items[selected_idx];
                                if (label == "Back") {
                                    restoreMain();
                                } else if (label == "Classic") {
                                    // Open the modal instead of instantly selecting when using keyboard
                                    show_classic_popup = true;
                                    classic_play_hover = false;
                                } else {
                                    res.choice = std::string("Singleplayer:") + label;
                                    running = false;
                                }
                             }
                         } else if (view == View::SP_SUB) {
                             if (sub_selected == 0) { res.choice = "Singleplayer:Classic"; running = false; }
                             else { res.choice = std::string("Singleplayer:") + sp_sub[sub_selected]; running = false; }
                         } else if (view == View::MP_SUB) {
                             res.choice = std::string("Multiplayer:") + mp_sub[sub_selected];
                             running = false;
                         }
                     }
                 }
             } else if (e.type == SDL_KEYUP) {
                // stop hold progress when Enter released; if progress reached 1.0 the quit would already have fired
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    if (exit_hold_start != 0) {
                        exit_hold_start = 0;
                        exit_hold_progress = 0.0f;
                        // keep exit_armed true so a quick second press still works until selection changes
                    }
                }
            } else if (e.type == SDL_MOUSEWHEEL) {
                if (e.wheel.y < 0) { // down
                    if (view == View::MAIN) {
                        selected_idx = (selected_idx + 1) % main_items.size();
                        exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
                    }
                    else {
                        int mx = (view==View::SP_SUB) ? (int)sp_sub.size() : (int)mp_sub.size();
                        sub_selected = (sub_selected + 1) % mx;
                    }
                } else {
                    if (view == View::MAIN) {
                        selected_idx = (selected_idx - 1 + main_items.size()) % main_items.size();
                        exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
                    }
                    else {
                        int mx = (view==View::SP_SUB) ? (int)sp_sub.size() : (int)mp_sub.size();
                        sub_selected = (sub_selected - 1 + mx) % mx;
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                // if modal open let it handle clicks first
                if (show_classic_popup) {
                    int mx = e.button.x, my = e.button.y;
                    int w,h; SDL_GetRendererOutputSize(renderer, &w, &h);
                    // modal rect
                    int mw = std::min(1000, w - 160);
                    int mh = std::min(680, h - 140);
                    int mx0 = (w - mw)/2;
                    int my0 = (h - mh)/2;
                    // Play button area (left column center)
                    int play_w = mw*60/100 - 80;
                    int play_h = mh*18/100;
                    int left_x = mx0 + 40;
                    int left_y = my0 + 80;
                    SDL_Rect play_rect = { left_x + 20, left_y + 70, play_w, play_h };
                    // click Play
                    if (mx >= play_rect.x && mx <= play_rect.x + play_rect.w && my >= play_rect.y && my <= play_rect.y + play_rect.h) {
                        res.choice = "Singleplayer:Classic";
                        running = false;
                    } else {
                        // clicking outside modal closes it
                        if (!(mx >= mx0 && mx <= mx0 + mw && my >= my0 && my <= my0 + mh)) {
                            show_classic_popup = false;
                        }
                    }
                    // consume this click
                    // any click resets exit arm
                    exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
                    continue;
                }
                 int mx = e.button.x, my = e.button.y;
                 int w,h; SDL_GetRendererOutputSize(renderer, &w, &h);
                // compute vertical layout like below and test clicks
                int base_font = std::max(24, h/12);
                // ensure asset fonts match sensible sizes for this window
                EnsureFontsForSize(base_font);
                TTF_Font* f = menu_font_loaded ? menu_font_loaded : font;
                 std::vector<int> heights;
                 int gap = 18;
                 int total_h = 0;
                 for (auto &s : main_items) {
                     if (f) {
                        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), {230,230,230,255});
                        heights.push_back(surf ? surf->h : base_font);
                        if (surf) SDL_FreeSurface(surf);
                     } else heights.push_back(base_font);
                     total_h += heights.back();
                 }
                 total_h += (main_items.size()-1) * gap;
                int start_y = (h - total_h)/2;
                int cx = w/2;
                int y = start_y;
                bool clicked = false;
                for (size_t i=0;i<main_items.size();++i) {
                     int th = heights[i];
                     SDL_Rect rect = { cx - 300, y, 600, th }; // padded rect
                     SDL_Rect pr = {rect.x - 20, rect.y - 8, rect.w + 40, rect.h + 16};
                     if (mx >= pr.x && mx <= pr.x+pr.w && my >= pr.y && my <= pr.y+pr.h) {
                         // clicked main item
                         const std::string &label = main_items[i];
                         if (label == "Singleplayer" && !inlined_sp) {
                             enterInlineSP();
                         } else if (label == "Back") {
                             restoreMain();
                         } else if (label == "Multiplayer") {
                             view = View::MP_SUB; sub_selected = 0;
                         } else if (label == "Options") {
                             // placeholder
                         } else if (label == "Exit") {
                             // mouse click exits immediately (explicit)
                             res.choice = "Exit"; running = false;
                         } else {
                             // if we're inlined_sp, any other label is a singleplayer choice
                             if (inlined_sp) {
                                 if (label == "Classic") {
                                     // open modal instead of instant selection
                                     show_classic_popup = true;
                                     classic_play_hover = false;
                                     classic_closed = false;
                                 } else {
                                     res.choice = std::string("Singleplayer:") + label;
                                     running = false;
                                 }
                             }
                         }
                         // any click resets keyboard exit hold/arm state (except immediate exit)
                         exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
                         clicked = true;
                         break;
                     }
                     y += th + gap;
                 }
                if (clicked) continue;
                // if in submenu, test sub items area (to the right)
                if (view != View::MAIN) {
                    std::vector<std::string> &sub = (view==View::SP_SUB) ? sp_sub : mp_sub;
                    // measure sub heights
                    std::vector<int> sh;
                    int total_sh = 0;
                    // prefer the small in-game font for subitems when available
                    TTF_Font* subf = sub_font_loaded ? sub_font_loaded : (menu_font_loaded ? menu_font_loaded : font);
                    for (auto &s : sub) {
                        SDL_Surface* surf = TTF_RenderUTF8_Blended(subf, s.c_str(), {230,230,230,255});
                        sh.push_back(surf ? surf->h : base_font);
                        if (surf) SDL_FreeSurface(surf);
                        total_sh += sh.back();
                    }
                    total_sh += (sub.size()-1) * gap;
                    int sub_x = w/2 + 240;
                    int sy = (h - total_sh)/2;
                    for (size_t i=0;i<sub.size();++i) {
                        SDL_Rect pr = { sub_x - 20, sy - 8, 400, sh[i] + 16 };
                        if (mx >= pr.x && mx <= pr.x+pr.w && my >= pr.y && my <= pr.y+pr.h) {
                            if (view==View::SP_SUB) {
                                if (i==0) { res.choice = "Singleplayer:Classic"; running = false; }
                                else { res.choice = std::string("Singleplayer:")+sub[i]; running = false; }
                            } else {
                                res.choice = std::string("Multiplayer:")+sub[i];
                                running = false;
                            }
                            break;
                        }
                        sy += sh[i] + gap;
                    }
                }
            } else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED)) {
                // request refetch for new size when using Unsplash
                if (!use_image) need_refetch = true;
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

                // layout columns: left ~60%, right ~40%
                int left_w = mw * 60 / 100;
                int right_w = mw - left_w - 40;
                SDL_Rect left_panel = { mx0 + 20, my0 + 60, left_w, mh - 80 };
                SDL_Rect right_panel = { mx0 + 20 + left_w + 40, my0 + 60, right_w, mh - 80 };

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

                // draw panel backgrounds
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 10,18,26, 220);
                SDL_RenderFillRect(renderer, &left_panel);
                SDL_RenderFillRect(renderer, &right_panel);

                // left sub-boxes: pb (top), play (mid), replays (rest)
                int pb_h = left_panel.h * 28 / 100;
                int play_h = left_panel.h * 22 / 100;
                int replays_h = left_panel.h - (pb_h + play_h + 32);
                SDL_Rect pb_box = { left_panel.x + 16, left_panel.y + 8, left_panel.w - 32, pb_h };
                SDL_Rect play_box = { left_panel.x + 16, pb_box.y + pb_box.h + 8, left_panel.w - 32, play_h };
                SDL_Rect replays_box = { left_panel.x + 16, play_box.y + play_box.h + 8, left_panel.w - 32, replays_h };

                SDL_SetRenderDrawColor(renderer, 6,12,18,220);
                SDL_RenderFillRect(renderer, &pb_box);
                SDL_RenderFillRect(renderer, &play_box);
                SDL_RenderFillRect(renderer, &replays_box);
                SDL_SetRenderDrawColor(renderer, 15,40,60,255);
                SDL_RenderDrawRect(renderer, &pb_box);
                SDL_RenderDrawRect(renderer, &play_box);
                SDL_RenderDrawRect(renderer, &replays_box);
                SDL_RenderDrawRect(renderer, &right_panel);

                // Play button area: sized relative to play_box
                SDL_Rect play_button_inner = { play_box.x + 8, play_box.y + (play_box.h / 8), play_box.w - 16, play_box.h - (play_box.h / 4) };
                int mouse_x, mouse_y;
                SDL_GetMouseState(&mouse_x, &mouse_y);
                classic_play_hover = (mouse_x >= play_button_inner.x && mouse_x <= play_button_inner.x + play_button_inner.w && mouse_y >= play_button_inner.y && mouse_y <= play_button_inner.y + play_button_inner.h);
                if (classic_play_hover) {
                    SDL_SetRenderDrawColor(renderer, 24,110,200, 200);
                } else {
                    SDL_SetRenderDrawColor(renderer, 10,20,30, 160);
                }
                SDL_RenderFillRect(renderer, &play_button_inner);

                // triangle icon scaled to button height
                int tri_h = std::min( (int)(play_button_inner.h * 0.7f), 48 );
                int tri_w = std::max(12, tri_h * 2 / 3);
                int tx = play_button_inner.x + 18;
                int ty = play_button_inner.y + (play_button_inner.h - tri_h) / 2;
                SDL_SetRenderDrawColor(renderer, 255,255,255,255);
                for (int r = 0; r < tri_h; ++r) {
                    float frac = (float)r / (float)tri_h;
                    int line_w = (int)(frac * tri_w);
                    SDL_RenderDrawLine(renderer, tx, ty + r, tx + line_w, ty + r);
                }

                // Play text scaled from header_font metrics
                if (header_font) {
                    SDL_Surface* sp = TTF_RenderUTF8_Blended(header_font, "Play", {255,255,255,255});
                    if (sp) {
                        SDL_Texture* tp = SDL_CreateTextureFromSurface(renderer, sp);
                        SDL_Rect dst = { play_button_inner.x + tri_w + 36, play_button_inner.y + (play_button_inner.h - sp->h) / 2, sp->w, sp->h };
                        SDL_RenderCopy(renderer, tp, nullptr, &dst);
                        SDL_DestroyTexture(tp);
                        SDL_FreeSurface(sp);
                    }
                }

                // Personal Best area (use small font)
                if (small_font) {
                    SDL_Surface* s0 = TTF_RenderUTF8_Blended(small_font, "0 in 0.00.00", {240,240,240,255});
                    if (s0) { SDL_Texture* t0 = SDL_CreateTextureFromSurface(renderer, s0); SDL_Rect d0 = { pb_box.x + 20, pb_box.y + 12, s0->w, s0->h }; SDL_RenderCopy(renderer, t0, nullptr, &d0); SDL_DestroyTexture(t0); SDL_FreeSurface(s0); }
                    SDL_Surface* s1 = TTF_RenderUTF8_Blended(small_font, "Personal Best", {170,190,200,255});
                    if (s1) { SDL_Texture* t1 = SDL_CreateTextureFromSurface(renderer, s1); SDL_Rect d1 = { pb_box.x + 20, pb_box.y + 12 + ( (pb_box.h > 60) ? 44 : 28 ), s1->w, s1->h }; SDL_RenderCopy(renderer, t1, nullptr, &d1); SDL_DestroyTexture(t1); SDL_FreeSurface(s1); }
                }

                // Replays heading (scaled)
                if (header_font) {
                    SDL_Surface* rh = TTF_RenderUTF8_Blended(header_font, "Replays", {230,230,230,255});
                    if (rh) {
                        SDL_Texture* trh = SDL_CreateTextureFromSurface(renderer, rh);
                        SDL_Rect drh = { replays_box.x + 14, replays_box.y + 8, rh->w, rh->h };
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
                        SDL_Rect drem = { right_panel.x + (right_panel.w - rem->w)/2, right_panel.y + (right_panel.h - rem->h)/2, rem->w, rem->h };
                        SDL_RenderCopy(renderer, trem, nullptr, &drem);
                        SDL_DestroyTexture(trem);
                        SDL_FreeSurface(rem);
                    }
                }
         }

        SDL_RenderPresent(renderer);
    }

    // cleanup
    // Cleanup: avoid double-destroy if bgtex points to tiletex
    if (bgtex && !bg_is_tile) SDL_DestroyTexture(bgtex);
    if (tiletex) SDL_DestroyTexture(tiletex);
    // close any fonts we opened here (do not close the 'font' passed in)
    if (menu_font_loaded) { TTF_CloseFont(menu_font_loaded); menu_font_loaded = nullptr; }
    if (sub_font_loaded)  { TTF_CloseFont(sub_font_loaded);  sub_font_loaded = nullptr; }

    return res;
}
