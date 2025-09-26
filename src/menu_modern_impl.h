#pragma once
#include "menu_modern_common.h"
#include "menu_modern_modal.h"
#include "menu_modern.h" // MenuResult
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <cmath>
#include <iostream>
#include <cstdio>
#include <memory>
#include <array>
#include <sstream>
#include <sys/wait.h>

// local runtime overrides set after a successful login (preferred over "Guest")
static std::string g_local_display_name;
static std::string g_local_subtext;

// Declare AttemptLogin so RunMainMenu can call it; real implementation should live in your nexus module.
namespace nexus {
    void AttemptLogin(const std::string& user, const std::string& pass);
    // If the real nexus module exposes setters these will be replaced; otherwise they update the local UI state.
    inline void SetDisplayName(const std::string& name) {
        g_local_display_name = name;
        std::cerr << "[nexus::SetDisplayName] -> " << name << "\n";
    }
    inline void SetSubtext(const std::string& text) {
        g_local_subtext = text;
        std::cerr << "[nexus::SetSubtext] -> " << text << "\n";
    }
    inline void SetAvatarURL(const std::string& url) {
        std::cerr << "[nexus::SetAvatarURL shim] url='" << url << "'\n";
    }
    inline void SetBannerURL(const std::string& url) {
        std::cerr << "[nexus::SetBannerURL shim] url='" << url << "'\n";
    }
}

// Main menu implementation moved into a header so the logic is grouped but still usable.
// Note: do NOT repeat the default parameter here (it's in menu_modern.h) and do not declare
// the function static since menu_modern.h has the external declaration.
MenuResult RunMainMenu(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font, const std::string& bg_image_path) {
    MenuResult res{"", ""};
    if (!window || !renderer) { res.choice = "Exit"; return res; }
    // prefer project assets for menu and UI text; fallback to passed-in 'font'
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
            bg_is_tile = true;
        }
    }

    SDL_Surface* tile_surf = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(tile_surf, nullptr, SDL_MapRGBA(tile_surf->format, 245,245,220,255));
    SDL_Rect rbox = {0,0,64,64};
    SDL_FillRect(tile_surf, &rbox, SDL_MapRGBA(tile_surf->format, 245,245,220,255));
    SDL_Texture* tiletex = SDL_CreateTextureFromSurface(renderer, tile_surf);
    SDL_FreeSurface(tile_surf);

    if (!bgtex && tiletex) {
        bgtex = tiletex;
        bg_is_tile = true;
    }

    SDL_Texture* profile_avatar = LoadTexture(renderer, "src/assets/avatar_guest.png"); // optional
    bool social_target_open = false;
    float social_anim = 0.0f;
    const int social_max_width = 320;
    // local login edit state (used while the centered popup is open)
    std::string login_user_edit;
    std::string login_pass_edit;
    bool login_focus_user = true; // true => username input focused; false => password
    // Simple helper to produce a masked password display
    auto Mask = [&](const std::string &s)->std::string {
        return std::string(s.size(), '*');
    };

    nexus::Init();

    auto ComputeTopbarSocialRects = [&](int ww, int hh, TTF_Font* sf) -> std::pair<SDL_Rect, SDL_Rect> {
        int top_h = std::max(48, hh / 14);
        const int pad = 12;
        const char* label_text = "Social";
        int label_w = 64, label_h = std::max(16, top_h - 16);
        TTF_Font* usef = sf ? sf : font;
        if (usef) TTF_SizeUTF8(usef, label_text, &label_w, &label_h);
        int label_x = pad;
        int label_y = (top_h - label_h) / 2;
        int btn_h = std::max(36, top_h - (pad));
        int btn_w = std::max(48, btn_h + 8);
        int btn_x = label_x + label_w + 8;
        int btn_y = (top_h - btn_h) / 2;
        SDL_Rect lab = { label_x, label_y, label_w, label_h };
        SDL_Rect btn = { btn_x, btn_y, btn_w, btn_h };
        return { lab, btn };
    };

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

    bool need_refetch = false;

    bool exit_armed = false;
    Uint64 exit_hold_start = 0;
    float exit_hold_progress = 0.0f;
    const float exit_hold_required = 5.0f;

    bool inlined_sp = false;
    std::vector<std::string> backup_main;
    bool show_classic_popup = false;

     auto enterInlineSP = [&]() {
         if (inlined_sp) return;
         backup_main = main_items;
         main_items = sp_sub;
         main_items.push_back("Back");
         scales.assign(main_items.size(), 1.0f);
         underline.assign(main_items.size(), 0.0f);
         selected_idx = 0;
         inlined_sp = true;
         exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
     };
     auto restoreMain = [&]() {
         if (!inlined_sp) return;
         main_items = backup_main;
         scales.assign(main_items.size(), 1.0f);
         underline.assign(main_items.size(), 0.0f);
         selected_idx = 0;
         inlined_sp = false;
         exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
     };

    while (running) {
        // frame timing: ensure 'dt' is always defined for animations and FPS calculations
        static Uint32 s_last_ticks = SDL_GetTicks();
        Uint32 now_ticks = SDL_GetTicks();
        float dt = (now_ticks > s_last_ticks) ? float(now_ticks - s_last_ticks) / 1000.0f : 0.016f;
        // clamp large deltas (e.g. when pausing in debugger)
        if (dt > 0.25f) dt = 0.25f;
        s_last_ticks = now_ticks;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
                res.choice = "Exit";
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                need_refetch = true;
            } else if (e.type == SDL_TEXTINPUT) {
                // route text input to the centered login popup when open
                if (g_loginPopupOpen) {
                    if (login_focus_user) login_user_edit += e.text.text;
                    else login_pass_edit += e.text.text;
                }
            } else if (e.type == SDL_KEYDOWN) {
                // Allow Ctrl+V paste into focused login field while popup is open.
                if (g_loginPopupOpen) {
                    if ((SDL_GetModState() & KMOD_CTRL) && e.key.keysym.sym == SDLK_v) {
                        char* cb = SDL_GetClipboardText();
                        if (cb && cb[0] != '\0') {
                            if (login_focus_user) login_user_edit += cb;
                            else login_pass_edit += cb;
                        }
                        if (cb) SDL_free(cb);
                        continue;
                    }
                }
                if (g_loginPopupOpen) {
                    // backspace / tab handling when popup is open
                    if (e.key.keysym.sym == SDLK_BACKSPACE) {
                        if (login_focus_user) { if (!login_user_edit.empty()) login_user_edit.pop_back(); }
                        else { if (!login_pass_edit.empty()) login_pass_edit.pop_back(); }
                        continue; // don't also treat as general menu navigation
                    } else if (e.key.keysym.sym == SDLK_TAB) {
                        login_focus_user = !login_focus_user;
                        continue;
                    } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        // submit via keyboard
                        // forward to nexus login routine (implementation expected in nexus)
                        nexus::AttemptLogin(login_user_edit, login_pass_edit);
                        g_loginPopupOpen = false;
                        SDL_StopTextInput();
                        social_target_open = false;
                        nexus::SetDropdownVisible(false);
                        continue;
                    }
                }
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
                        if (inlined_sp) {
                            if (sel == "Back") {
                                restoreMain();
                            } else if (sel == "Classic") {
                                show_classic_popup = true;
                            } else if (sel == "Blitz") {
                                OnSelectBlitz(window, renderer,
                                              menu_font_loaded ? menu_font_loaded : font,
                                              sub_font_loaded ? sub_font_loaded : (menu_font_loaded ? menu_font_loaded : font));
                            } else {
                                RunGameSDL(window, renderer, menu_font_loaded ? menu_font_loaded : font);
                            }
                        } else {
                            if (sel == "Singleplayer") {
                                enterInlineSP();
                            } else if (sel == "Options") {
                                // TODO
                            } else if (sel == "Exit") {
                                running = false;
                                res.choice = "Exit";
                            }
                        }
                        exit_armed = false; exit_hold_start = 0; exit_hold_progress = 0.0f;
                        break;
                    }
                    case SDLK_F8:
                        ToggleDebugOverlay();
                         break;
                    default:
                        break;
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x, my = e.button.y;
                int ww = 800, hh = 600;
                SDL_GetRendererOutputSize(renderer, &ww, &hh);
                TTF_Font* hit_font = sub_font_loaded ? sub_font_loaded : (menu_font_loaded ? menu_font_loaded : font);
                auto pr = ComputeTopbarSocialRects(ww, hh, hit_font);
                SDL_Rect label_rect = pr.first;
                SDL_Rect btn_rect = pr.second;
                if (mx >= btn_rect.x && mx <= btn_rect.x + btn_rect.w && my >= btn_rect.y && my <= btn_rect.y + btn_rect.h) {
                    // If the user is not signed in, open the larger centered login prompt
                    // instead of expanding the social sidebar. Use GetDisplayName() as a
                    // lightweight check for a guest user (common in this codepath).
                    std::string disp = nexus::GetDisplayName();
                    bool signed_in = (!disp.empty() && disp != "Guest");
                    if (!signed_in) {
                        // initialize local edit buffers from nexus (if available)
                        login_user_edit = nexus::GetEditingUser();
                        login_pass_edit.clear();
                        login_focus_user = true;
                        g_loginPopupOpen = true;
                        SDL_StartTextInput(); // route keyboard to popup
                         social_target_open = false;
                         nexus::SetDropdownVisible(false);
                    } else {
                        social_target_open = !social_target_open;
                        // keep nexus dropdown state in sync so inline login renders for signed-in users
                        nexus::SetDropdownVisible(social_target_open);
                    }
                } else {
                    int visible_sw = (int)(social_max_width * social_anim + 0.5f);
                    int sx = -social_max_width + visible_sw;
                    if (social_anim > 0.001f) {
                        if (!(mx >= sx && mx <= sx + visible_sw && my >= 0 && my <= hh)) {
                            social_target_open = false;
                            // close inline dropdown when social pane is dismissed
                            nexus::SetDropdownVisible(false);
                        }
                    }
                }
             }
         }

        // forward text input/backspace to nexus when dropdown is visible / focused
        // note: event `e` consumed above; keep compatibility with original flow by polling state
        // (the original had minor duplication; keep behaviour)
        // handle refetch if needed
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

        if (exit_hold_start != 0) {
            Uint64 nowt = SDL_GetTicks64();
            float held = (nowt - exit_hold_start) / 1000.0f;
            exit_hold_progress = std::min(1.0f, held / exit_hold_required);
            if (exit_hold_progress >= 1.0f) {
                res.choice = "Exit";
                running = false;
            }
        }

        for (size_t i=0;i<main_items.size();++i) {
            float target = (int)i == selected_idx ? 1.12f : 1.0f;
            scales[i] += (target - scales[i]) * std::min(1.0f, dt * anim_speed);
            float ut = (int)i == selected_idx ? 1.0f : 0.0f;
            underline[i] += (ut - underline[i]) * std::min(1.0f, dt * (anim_speed * 1.2f));
        }

        int w,h; SDL_GetRendererOutputSize(renderer, &w, &h);
        renderWallpaperWithTint(renderer, bgtex, w, h);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0,0,0,128);
        SDL_Rect topbar = {0,0,w, std::max(48, h/14)};
        SDL_RenderFillRect(renderer, &topbar);

        // --- Social label + button in topbar (left) ---
        {
            TTF_Font* label_font = sub_font_loaded ? sub_font_loaded : (menu_font_loaded ? menu_font_loaded : font);
            const int pad = 12;
            int top_h = std::max(48, h/14);
            int px = pad;
            int profile_w = 260;
            int profile_h = 84;
            int py = (top_h - profile_h)/2;
            SDL_Rect prof_rect = { px, py, profile_w, profile_h };
            SDL_SetRenderDrawColor(renderer, 36,36,36,220);
            SDL_RenderFillRect(renderer, &prof_rect);
            SDL_SetRenderDrawColor(renderer, 80,80,80,255);
            SDL_RenderDrawRect(renderer, &prof_rect);
            int av_pad = 10;
            SDL_Rect av = { prof_rect.x + av_pad, prof_rect.y + av_pad, profile_h - av_pad*2, profile_h - av_pad*2 };
            if (profile_avatar) SDL_RenderCopy(renderer, profile_avatar, nullptr, &av);
            else { SDL_SetRenderDrawColor(renderer, 120,140,160,255); SDL_RenderFillRect(renderer, &av); }
            // prefer locally-updated display name/subtext (set after login); fall back to nexus getters.
            std::string disp = !g_local_display_name.empty() ? g_local_display_name : nexus::GetDisplayName();
            std::string sub = !g_local_subtext.empty() ? g_local_subtext : nexus::GetSubtext();
            if (label_font) {
                SDL_Surface* u_s = TTF_RenderUTF8_Blended(label_font, disp.c_str(), {240,240,240,255});
                if (u_s) { SDL_Texture* ut = SDL_CreateTextureFromSurface(renderer, u_s); SDL_Rect dst = { av.x + av.w + 12, prof_rect.y + 12, u_s->w, u_s->h }; SDL_RenderCopy(renderer, ut, nullptr, &dst); SDL_DestroyTexture(ut); SDL_FreeSurface(u_s); }
                SDL_Surface* s_s = TTF_RenderUTF8_Blended(label_font, sub.c_str(), {170,180,190,255});
                if (s_s) { SDL_Texture* st = SDL_CreateTextureFromSurface(renderer, s_s); SDL_Rect dst2 = { av.x + av.w + 12, prof_rect.y + 12 + 28, s_s->w, s_s->h }; SDL_RenderCopy(renderer, st, nullptr, &dst2); SDL_DestroyTexture(st); SDL_FreeSurface(s_s); }
            }

            if (nexus::DropdownVisible()) {
                int dd_x = prof_rect.x;
                int dd_y = prof_rect.y + prof_rect.h + 6;
                int dd_w = prof_rect.w;
                int dd_h = 160;
                SDL_Rect dd = { dd_x, dd_y, dd_w, dd_h };
                SDL_SetRenderDrawColor(renderer, 28,28,28,230); SDL_RenderFillRect(renderer, &dd);
                SDL_SetRenderDrawColor(renderer, 80,80,80,255); SDL_RenderDrawRect(renderer, &dd);
                int ix = dd_x + 12; int iy = dd_y + 12;
                SDL_Rect user_box = { ix, iy, dd_w - 24, 36 };
                SDL_Rect pass_box = { ix, iy + 46, dd_w - 24, 36 };
                SDL_Rect login_btn = { ix, iy + 92, dd_w - 24, 40 };
                SDL_SetRenderDrawColor(renderer, 50,50,50,220); SDL_RenderFillRect(renderer, &user_box);
                SDL_SetRenderDrawColor(renderer, 60,60,60,220); SDL_RenderFillRect(renderer, &pass_box);
                SDL_SetRenderDrawColor(renderer, 70,100,160,220); SDL_RenderFillRect(renderer, &login_btn);
                TTF_Font* fsmall = sub_font_loaded ? sub_font_loaded : font;
                if (fsmall) {
                    std::string user_edit = nexus::GetEditingUser();
                    std::string pass_mask = nexus::GetEditingPassMasked();
                    SDL_Surface* su = TTF_RenderUTF8_Blended(fsmall, user_edit.empty() ? "Username" : user_edit.c_str(), {200,200,200,255});
                    if (su) { SDL_Texture* tu = SDL_CreateTextureFromSurface(renderer, su); SDL_Rect d = { user_box.x + 8, user_box.y + (user_box.h - su->h)/2, su->w, su->h }; SDL_RenderCopy(renderer, tu, nullptr, &d); SDL_DestroyTexture(tu); SDL_FreeSurface(su); }
                    SDL_Surface* sp = TTF_RenderUTF8_Blended(fsmall, pass_mask.empty() ? "Password" : pass_mask.c_str(), {200,200,200,255});
                    if (sp) { SDL_Texture* tp = SDL_CreateTextureFromSurface(renderer, sp); SDL_Rect d2 = { pass_box.x + 8, pass_box.y + (pass_box.h - sp->h)/2, sp->w, sp->h }; SDL_RenderCopy(renderer, tp, nullptr, &d2); SDL_DestroyTexture(tp); SDL_FreeSurface(sp); }
                    SDL_Surface* sb = TTF_RenderUTF8_Blended(fsmall, "Login", {240,240,240,255});
                    if (sb) { SDL_Texture* tb = SDL_CreateTextureFromSurface(renderer, sb); SDL_Rect db = { login_btn.x + (login_btn.w - sb->w)/2, login_btn.y + (login_btn.h - sb->h)/2, sb->w, sb->h }; SDL_RenderCopy(renderer, tb, nullptr, &db); SDL_DestroyTexture(tb); SDL_FreeSurface(sb); }
                    std::string status = nexus::GetLoginStatus();
                    if (!status.empty()) {
                        SDL_Surface* ss = TTF_RenderUTF8_Blended(fsmall, status.c_str(), {180,200,220,255});
                        if (ss) { SDL_Texture* ts = SDL_CreateTextureFromSurface(renderer, ss); SDL_Rect ds = { dd_x + 12, login_btn.y + login_btn.h + 8, ss->w, ss->h }; SDL_RenderCopy(renderer, ts, nullptr, &ds); SDL_DestroyTexture(ts); SDL_FreeSurface(ss); }
                    }
                }
            }
        }

        // --- Main menu list (always drawn; interaction can still be blocked by g_loginPopupOpen) ---
        {
            // ensure fonts are reasonable for this window size
            int modal_base = std::max(28, h / 18);
            EnsureFontsForSize(modal_base);
            TTF_Font* mf = menu_font_loaded ? menu_font_loaded : font;
            TTF_Font* sf = sub_font_loaded ? sub_font_loaded : (menu_font_loaded ? menu_font_loaded : font);

            if (mf) {
                // layout
                int item_count = (int)main_items.size();
                int item_h_est = 0;
                TTF_SizeUTF8(mf, "Ay", nullptr, &item_h_est);
                int spacing = std::max(12, item_h_est / 2);
                int total_h = 0;
                for (int i = 0; i < item_count; ++i) {
                    total_h += int((float)item_h_est * scales[i]) + spacing;
                }
                int start_y = std::max(h/3, (h - total_h) / 2);

                for (int i = 0; i < item_count; ++i) {
                    const std::string &txt = main_items[i];
                    SDL_Color col = ((int)i == selected_idx) ? SDL_Color{245,245,245,255} : SDL_Color{200,200,200,255};
                    SDL_Surface* surf = TTF_RenderUTF8_Blended(mf, txt.c_str(), col);
                    if (!surf) continue;
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                    int dw = int(surf->w * scales[i]);
                    int dh = int(surf->h * scales[i]);
                    int dx = (w - dw) / 2;
                    int dy = start_y;

                    SDL_Rect dst = { dx, dy, dw, dh };
                    SDL_RenderCopy(renderer, tex, nullptr, &dst);

                    // underline animation
                    float u = underline[i];
                    if (u > 0.001f) {
                        SDL_SetRenderDrawColor(renderer, 80,180,240, 255);
                        SDL_Rect ul = { dx, dy + dh + 6, int(dw * u), 4 };
                        SDL_RenderFillRect(renderer, &ul);
                    }

                    SDL_DestroyTexture(tex);
                    SDL_FreeSurface(surf);

                    start_y += dh + spacing;
                }
            }
        }

        // final UI pass: render the large centered login prompt last so it is on top of everything
        // login popup rendering moved to after main/menu rendering to ensure it appears on top
        if (g_loginPopupOpen) {
            // draw full-screen fade
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
            SDL_RenderFillRect(renderer, nullptr);

            // layout sizes (centered, responsive)
            int login_w = std::min(w - 240, std::max(640, w * 55 / 100));
            int login_h = std::min(h - 240, std::max(320, h * 38 / 100));
            int lx = (w - login_w) / 2;
            int ly = (h - login_h) / 2;
            SDL_Rect box = { lx, ly, login_w, login_h };

            // modal background (very dark)
            SDL_SetRenderDrawColor(renderer, 8, 12, 18, 255);
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 40, 56, 72, 255);
            SDL_RenderDrawRect(renderer, &box);

            // padding and element sizes
            const int pad = 24;
            const int inner_x = lx + pad;
            const int inner_w = login_w - pad * 2;
            int y = ly + pad;

            // helper to render text and return height
            auto DrawText = [&](const std::string &txt, TTF_Font* f, SDL_Color col, int x, int y)->int {
                if (!f) return 0;
                SDL_Surface* s = TTF_RenderUTF8_Blended(f, txt.c_str(), col);
                if (!s) return 0;
                SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
                SDL_Rect dst = { x, y, s->w, s->h };
                SDL_RenderCopy(renderer, t, nullptr, &dst);
                SDL_DestroyTexture(t);
                int h = s->h;
                SDL_FreeSurface(s);
                return h;
            };

            SDL_Color titleCol = { 230,230,230,255 };
            SDL_Color hintCol  = { 200,200,200,255 };
            SDL_Color inputBg  = { 18,30,44,220 };
            SDL_Color inputBg2 = { 20,36,50,220 };
            SDL_Color buttonBg = { 12,64,112,255 };
            SDL_Color buttonText= { 245,245,245,255 };

            // Title
            TTF_Font* titleFont = menu_font_loaded ? menu_font_loaded : font;
            int th = DrawText("Login or Register.", titleFont, titleCol, inner_x, y);
            y += th + 12;

            // Username box
            SDL_Rect user_box = { inner_x, y, inner_w, 56 };
            SDL_SetRenderDrawColor(renderer, inputBg.r, inputBg.g, inputBg.b, inputBg.a);
            SDL_RenderFillRect(renderer, &user_box);
            // label / content
            TTF_Font* smallf = sub_font_loaded ? sub_font_loaded : (menu_font_loaded ? menu_font_loaded : font);
            std::string user_text = login_user_edit.empty() ? std::string("Username") : login_user_edit;
            SDL_Color ucol = login_focus_user ? SDL_Color{255,255,255,255} : hintCol;
            DrawText(user_text, smallf, ucol, user_box.x + 12, user_box.y + (user_box.h - 18)/2);
             y += user_box.h + 12;
 
             // Password box
             SDL_Rect pass_box = { inner_x, y, inner_w, 56 };
             SDL_SetRenderDrawColor(renderer, inputBg2.r, inputBg2.g, inputBg2.b, inputBg2.a);
             SDL_RenderFillRect(renderer, &pass_box);
             std::string pass_text = login_pass_edit.empty() ? std::string("Password") : Mask(login_pass_edit);
             SDL_Color pcol = login_focus_user ? hintCol : SDL_Color{255,255,255,255};
             DrawText(pass_text, smallf, pcol, pass_box.x + 12, pass_box.y + (pass_box.h - 18)/2);
             y += pass_box.h + 18;
 
             // Submit button (large, full width)
             SDL_Rect submit = { inner_x, ly + login_h - pad - 56, inner_w, 56 };
             SDL_SetRenderDrawColor(renderer, buttonBg.r, buttonBg.g, buttonBg.b, 255);
             SDL_RenderFillRect(renderer, &submit);
             // button label centered
             SDL_Surface* sb = TTF_RenderUTF8_Blended(smallf, "Submit", buttonText);
             if (sb) {
                 SDL_Texture* tb = SDL_CreateTextureFromSurface(renderer, sb);
                 SDL_Rect db = { submit.x + (submit.w - sb->w)/2, submit.y + (submit.h - sb->h)/2, sb->w, sb->h };
                 SDL_RenderCopy(renderer, tb, nullptr, &db);
                 SDL_DestroyTexture(tb);
                 SDL_FreeSurface(sb);
             }
 
             // Optional status line under button
             std::string status = nexus::GetLoginStatus();
             if (!status.empty()) {
                 DrawText(status, smallf, {170,190,210,255}, inner_x, submit.y + submit.h + 8);
             }
 
             // Basic click handling for submit: consume click and close modal
             int mx, my;
             Uint32 mouse = SDL_GetMouseState(&mx, &my);
             static bool mouse_was_pressed = false;
             static bool middle_was_pressed = false;
             bool pressed = (mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
             bool mid_pressed = (mouse & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
             if (pressed && !mouse_was_pressed) {
                  // button click down event
                 // allow clicking username/password boxes to focus them
                 if (mx >= user_box.x && mx <= user_box.x + user_box.w && my >= user_box.y && my <= user_box.y + user_box.h) {
                     login_focus_user = true;
                 } else if (mx >= pass_box.x && mx <= pass_box.x + pass_box.w && my >= pass_box.y && my <= pass_box.y + pass_box.h) {
                     login_focus_user = false;
                 } else if (mx >= submit.x && mx <= submit.x + submit.w && my >= submit.y && my <= submit.y + submit.h) {
                     // submit click: hand off to nexus login routine (implementation provided elsewhere)
                     nexus::AttemptLogin(login_user_edit, login_pass_edit);
                     g_loginPopupOpen = false;
                     SDL_StopTextInput(); // stop routing keyboard to popup
                     // ensure social pane stays closed
                     social_target_open = false;
                     nexus::SetDropdownVisible(false);
                 }
             }
             // Middle-click paste handling (paste into whichever box was clicked)
             if (mid_pressed && !middle_was_pressed) {
                if (mx >= user_box.x && mx <= user_box.x + user_box.w && my >= user_box.y && my <= user_box.y + user_box.h) {
                    char* cb = SDL_GetClipboardText();
                    if (cb && cb[0] != '\0') { login_user_edit += cb; }
                    if (cb) SDL_free(cb);
                } else if (mx >= pass_box.x && mx <= pass_box.x + pass_box.w && my >= pass_box.y && my <= pass_box.y + pass_box.h) {
                    char* cb = SDL_GetClipboardText();
                    if (cb && cb[0] != '\0') { login_pass_edit += cb; }
                    if (cb) SDL_free(cb);
                }
            }
             mouse_was_pressed = pressed;
             middle_was_pressed = mid_pressed;
         }

        if (IsDebugOverlayVisible()) {
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

        if (IsDebugOverlayVisible()) {
            TTF_Font* dbgfont2 = sub_font_loaded ? sub_font_loaded : (menu_font_loaded ? menu_font_loaded : font);
            DrawDebugInfo(renderer, dbgfont2, window);
        }

         if (show_classic_popup) {
             int modal_base = std::max(32, h / 12);
             EnsureFontsForSize(modal_base);
             TTF_Font* header_font = menu_font_loaded ? menu_font_loaded : font;
             TTF_Font* small_font = sub_font_loaded ? sub_font_loaded : (menu_font_loaded ? menu_font_loaded : font);
             bool play = ShowClassicMenu(window, renderer, header_font, small_font, /*isBlitz=*/false);
             show_classic_popup = false;
             if (play) {
                 RunGameSDL(window, renderer, header_font);
             }
         }

        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    } // end while

    if (bgtex && !bg_is_tile) {
        SDL_DestroyTexture(bgtex);
        bgtex = nullptr;
    }
    if (tiletex) {
        SDL_DestroyTexture(tiletex);
        tiletex = nullptr;
    }
    if (profile_avatar) {
        SDL_DestroyTexture(profile_avatar);
        profile_avatar = nullptr;
    }
    if (menu_font_loaded) { TTF_CloseFont(menu_font_loaded); menu_font_loaded = nullptr; }
    if (sub_font_loaded)  { TTF_CloseFont(sub_font_loaded);  sub_font_loaded = nullptr; }

    nexus::Shutdown();

    return res;
}

// --- Temporary shim: declare/define nexus::AttemptLogin so this TU builds.
// Replace with a real implementation inside your nexus module that contacts the backend
// and updates nexus state (display name, avatar, banner, login status, etc).
namespace nexus {
    // small helper: extract a string field value from a JSON object body (very lenient).
    inline std::string ExtractJsonStringField(const std::string &body, const std::string &key) {
        // find "key"\s*:\s*"value"
        std::string pat = "\"" + key + "\"";
        size_t p = body.find(pat);
        if (p == std::string::npos) return "";
        size_t colon = body.find(':', p + pat.size());
        if (colon == std::string::npos) return "";
        size_t first_quote = body.find('\"', colon);
        if (first_quote == std::string::npos) return "";
        size_t second_quote = body.find('\"', first_quote + 1);
        if (second_quote == std::string::npos) return "";
        return body.substr(first_quote + 1, second_quote - first_quote - 1);
    }

     // Minimal JSON-escape helper (escapes quotes, backslashes and control chars).
     inline std::string JsonEscape(const std::string& s) {
         std::string o; o.reserve(s.size());
         for (unsigned char c : s) {
             switch (c) {
                 case '\"': o += "\\\""; break;
                 case '\\': o += "\\\\"; break;
                 case '\b': o += "\\b"; break;
                 case '\f': o += "\\f"; break;
                 case '\n': o += "\\n"; break;
                 case '\r': o += "\\r"; break;
                 case '\t': o += "\\t"; break;
                 default:
                     if (c < 0x20) {
                         char buf[8];
                         snprintf(buf, sizeof(buf), "\\u%04x", c);
                         o += buf;
                     } else {
                         o += (char)c;
                     }
             }
         }
         return o;
     }
 
     inline void AttemptLogin(const std::string& user, const std::string& pass) {
         std::cerr << "[nexus::AttemptLogin] attempting login for user='" << user << "'\n";
         if (user.empty()) { std::cerr << "[nexus::AttemptLogin] login failed: username required\n"; return; }
         if (pass.empty()) { std::cerr << "[nexus::AttemptLogin] login failed: password required\n"; return; }
 
         const std::string url = "http://127.0.0.1:8000/auth/login";
         std::ostringstream payload;
         payload << "{\"username\":\"" << JsonEscape(user) << "\",\"password\":\"" << JsonEscape(pass) << "\"}";
 
         std::string cmd = "curl -s -w \"\\n%{http_code}\" -X POST -H 'Content-Type: application/json' -d '" +
                           payload.str() + "' " + url;
 
         std::array<char, 4096> buf;
         std::string output;
         FILE* pipe = popen(cmd.c_str(), "r");
         if (!pipe) {
             std::cerr << "[nexus::AttemptLogin] backend request failed: popen() error\n";
             return;
         }
         while (fgets(buf.data(), (int)buf.size(), pipe) != nullptr) output += buf.data();
         int rc = pclose(pipe);
 
         std::string body = output;
         std::string http_code;
         size_t pos = output.find_last_of('\n');
         if (pos != std::string::npos && pos + 1 < output.size()) {
             http_code = output.substr(pos + 1);
             body = output.substr(0, pos);
         }
 
         if (!http_code.empty() && (http_code == "200" || http_code == "201")) {
            std::cerr << "[nexus::AttemptLogin] login succeeded (http " << http_code << ")\n";
            if (!body.empty()) std::cerr << "[nexus::AttemptLogin] backend response: " << body << "\n";
            // Pick sensible fields and push them into nexus state so the UI updates.
            std::string uname = ExtractJsonStringField(body, "user");
            if (uname.empty()) uname = ExtractJsonStringField(body, "username");
            if (!uname.empty()) {
                std::cerr << "[nexus::AttemptLogin] setting display name -> " << uname << "\n";
                SetDisplayName(uname);
            }
            std::string country = ExtractJsonStringField(body, "country");
            if (!country.empty()) {
                SetSubtext(country);
            } else {
                std::string msg = ExtractJsonStringField(body, "message");
                if (!msg.empty()) SetSubtext(msg);
            }
            std::string avatar = ExtractJsonStringField(body, "avatar");
            if (!avatar.empty()) {
                SetAvatarURL(avatar);
            }
            std::string banner = ExtractJsonStringField(body, "banner");
            if (!banner.empty()) {
                SetBannerURL(banner);
            }
        } else {
            std::cerr << "[nexus::AttemptLogin] login FAILED (http " << (http_code.empty() ? "?" : http_code) << ")\n";
            if (!body.empty()) std::cerr << "[nexus::AttemptLogin] backend response: " << body << "\n";
            else if (rc != 0) std::cerr << "[nexus::AttemptLogin] curl exit code: " << rc << "\n";
            else std::cerr << "[nexus::AttemptLogin] no response body from backend\n";
        }
     }
 }