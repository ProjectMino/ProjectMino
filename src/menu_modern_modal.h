#pragma once
#include "menu_modern_common.h"
#include "classic.h"        // classic modal uses game UI strings
#include "game.h"           // RunGameSDL
#include "blitz.h"
#include <vector>
#include <string>

// ShowClassicMenu is used for both Classic and Blitz modals.
// Keep as static inline so header inclusion across TU is safe.
static bool ShowClassicMenu(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* header_font, TTF_Font* small_font, bool isBlitz) {
     if (!window || !renderer) return false;
     int w = 800, h = 600;
     SDL_GetRendererOutputSize(renderer, &w, &h);
 
     bool running = true;
     bool play_pressed = false;
     SDL_ShowCursor(SDL_ENABLE);
 
     // Prepare Blitz duration text if needed
     char dur_buf[32] = "02:00";
     if (isBlitz) {
         int dur_ms = std::max(0, g_blitz_mode_options.duration_ms);
         int dur_s = dur_ms / 1000;
         int mm = dur_s / 60;
         int ss = dur_s % 60;
         snprintf(dur_buf, sizeof(dur_buf), "%02d:%02d", mm, ss);
     }
 
     while (running) {
         SDL_Event e;
         while (SDL_PollEvent(&e)) {
             if (e.type == SDL_QUIT) { running = false; break; }
             if (e.type == SDL_KEYDOWN) {
                 if (e.key.keysym.sym == SDLK_F8) { ToggleDebugOverlay(); continue; }
                 if (e.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
                 if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) { play_pressed = true; running = false; break; }
             } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                 int mx = e.button.x, my = e.button.y;
                 int mw = std::min(w - 160, std::max(600, w * 7 / 10));
                 int mh = std::min(h - 140, std::max(420, h * 7 / 10));
                 int mx0 = (w - mw) / 2;
                 int my0 = (h - mh) / 2;
                 const int panel_gap = 28;
                 const int modal_y_offset = 160;
                 int left_w = std::max(200, (mw - panel_gap) * 60 / 100);
                 int right_w = std::max(180, mw - left_w - panel_gap);
                 SDL_Rect left_panel = { mx0 + 20, my0 + modal_y_offset, left_w, mh - (modal_y_offset + 20) };
                 SDL_Rect right_panel = { left_panel.x + left_panel.w + panel_gap, my0 + modal_y_offset, right_w, mh - (modal_y_offset + 20) };
 
                // compute inner boxes so hit tests match rendering
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
                SDL_Rect play_button_inner = { play_box.x + 10, play_box.y + (play_box.h / 8), play_box.w - 20, play_box.h - (play_box.h / 4) };
                if (play_button_inner.h < 34) { play_button_inner.h = std::max(34, play_box.h - 8); play_button_inner.y = play_box.y + (play_box.h - play_button_inner.h)/2; }
 
                 SDL_Rect back_rect = { mx0 + 28, my0 + mh - 56 - 28, std::max(120, mw/5), 56 };
 
                 if (mx >= play_button_inner.x && mx <= play_button_inner.x + play_button_inner.w && my >= play_button_inner.y && my <= play_button_inner.y + play_button_inner.h) {
                     play_pressed = true; running = false; break;
                 }
                 if (mx >= back_rect.x && mx <= back_rect.x + back_rect.w && my >= back_rect.y && my <= back_rect.y + back_rect.h) {
                     running = false; break;
                 }
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
        std::string header = isBlitz ? "Blitz" : "Classic";
        if (header_font) {
            SDL_Surface* sh = TTF_RenderUTF8_Blended(header_font, header.c_str(), {240,240,240,255});
            if (sh) {
                SDL_Texture* th = SDL_CreateTextureFromSurface(renderer, sh);
                SDL_Rect hdr = { mx0 + (mw - sh->w) / 2, my0 + 12, sh->w, sh->h };
                SDL_RenderCopy(renderer, th, nullptr, &hdr);
                SDL_DestroyTexture(th);
                SDL_FreeSurface(sh);
            }
        }
 
        // layout panels
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
 
         // Play button inside left panel
         SDL_Rect play_button_inner = { play_box.x + 10, play_box.y + (play_box.h / 8), play_box.w - 20, play_box.h - (play_box.h / 4) };
         if (play_button_inner.h < 34) { play_button_inner.h = std::max(34, play_box.h - 8); play_button_inner.y = play_box.y + (play_box.h - play_button_inner.h)/2; }
         int mouse_x, mouse_y;
         SDL_GetMouseState(&mouse_x, &mouse_y);
         bool play_hover = (mouse_x >= play_button_inner.x && mouse_x <= play_button_inner.x + play_button_inner.w && mouse_y >= play_button_inner.y && mouse_y <= play_button_inner.y + play_button_inner.h);
         if (play_hover) SDL_SetRenderDrawColor(renderer, 200,200,200, 220); else SDL_SetRenderDrawColor(renderer, 70,70,70, 160);
         SDL_RenderFillRect(renderer, &play_button_inner);
 
         // triangle icon and Play text
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
         if (header_font) {
             SDL_Surface* sp = TTF_RenderUTF8_Blended(header_font, "Play", {255,255,255,255});
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
 
         // Left panel content: either Classic personal best or Blitz stats
         if (small_font) {
             if (isBlitz) {
                 SDL_Surface* s0 = TTF_RenderUTF8_Blended(small_font, "Blitz Stats", {240,240,240,255});
                 if (s0) {
                     SDL_Texture* t0 = SDL_CreateTextureFromSurface(renderer, s0);
                     SDL_Rect d0 = { pb_box.x + 14, pb_box.y + 8, s0->w, s0->h };
                     SDL_RenderCopy(renderer, t0, nullptr, &d0);
                     SDL_DestroyTexture(t0);
                     SDL_FreeSurface(s0);
                 }
                 char buf1[64]; snprintf(buf1, sizeof(buf1), "Personal Best: %d", 0);
                 char buf2[64]; snprintf(buf2, sizeof(buf2), "Games Played: %d", 0);
                 char buf3[64]; snprintf(buf3, sizeof(buf3), "Duration: %s", dur_buf);
                 SDL_Surface* s1 = TTF_RenderUTF8_Blended(small_font, buf1, {200,200,200,255});
                 SDL_Surface* s2 = TTF_RenderUTF8_Blended(small_font, buf2, {200,200,200,255});
                 SDL_Surface* s3 = TTF_RenderUTF8_Blended(small_font, buf3, {200,200,200,255});
                 int sy = pb_box.y + 36;
                 if (s1) { SDL_Texture* t1 = SDL_CreateTextureFromSurface(renderer, s1); SDL_Rect d = { pb_box.x + 14, sy, s1->w, s1->h }; SDL_RenderCopy(renderer, t1, nullptr, &d); SDL_DestroyTexture(t1); sy += s1->h + 6; SDL_FreeSurface(s1); }
                 if (s2) { SDL_Texture* t2 = SDL_CreateTextureFromSurface(renderer, s2); SDL_Rect d = { pb_box.x + 14, sy, s2->w, s2->h }; SDL_RenderCopy(renderer, t2, nullptr, &d); SDL_DestroyTexture(t2); sy += s2->h + 6; SDL_FreeSurface(s2); }
                 if (s3) { SDL_Texture* t3 = SDL_CreateTextureFromSurface(renderer, s3); SDL_Rect d = { pb_box.x + 14, sy, s3->w, s3->h }; SDL_RenderCopy(renderer, t3, nullptr, &d); SDL_DestroyTexture(t3); SDL_FreeSurface(s3); }
             } else {
                 SDL_Surface* s0 = TTF_RenderUTF8_Blended(small_font, "0 in 0.00.00", {240,240,240,255});
                 if (s0) {
                     SDL_Texture* t0 = SDL_CreateTextureFromSurface(renderer, s0);
                     SDL_Rect d0 = { pb_box.x + 14, pb_box.y + 12, s0->w, s0->h };
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
         }
 
         // Replays and Rankings placeholders
         if (header_font) {
             SDL_Surface* rh = TTF_RenderUTF8_Blended(header_font, "Replays", {230,230,230,255});
             if (rh) {
                 SDL_Texture* trh = SDL_CreateTextureFromSurface(renderer, rh);
                 SDL_Rect drh = { replays_box.x + 10, replays_box.y + 8, rh->w, rh->h };
                 SDL_RenderCopy(renderer, trh, nullptr, &drh);
                 SDL_DestroyTexture(trh);
                 SDL_FreeSurface(rh);
             }
             SDL_Surface* rh2 = TTF_RenderUTF8_Blended(header_font, "Rankings", {230,230,230,255});
             if (rh2) {
                 SDL_Texture* trh2 = SDL_CreateTextureFromSurface(renderer, rh2);
                 SDL_Rect drh2 = { right_panel.x + 18, right_panel.y + 10, rh2->w, rh2->h };
                 SDL_RenderCopy(renderer, trh2, nullptr, &drh2);
                 SDL_DestroyTexture(trh2);
                 SDL_FreeSurface(rh2);
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
 
         // Bottom Back button only (large Play is inside left panel)
         SDL_Rect back_rect = { mx0 + 28, my0 + mh - 56 - 28, std::max(120, mw/5), 56 };
         SDL_SetRenderDrawColor(renderer, 70,70,70, 200);
         SDL_RenderFillRect(renderer, &back_rect);
         if (header_font) {
             SDL_Surface* sb = TTF_RenderUTF8_Blended(header_font, "Back", {240,240,240,255});
             if (sb) {
                 SDL_Texture* tb = SDL_CreateTextureFromSurface(renderer, sb);
                 SDL_Rect db = { back_rect.x + (back_rect.w - sb->w)/2, back_rect.y + (back_rect.h - sb->h)/2, sb->w, sb->h };
                 SDL_RenderCopy(renderer, tb, nullptr, &db);
                 SDL_DestroyTexture(tb);
                 SDL_FreeSurface(sb);
             }
         }
 
         SDL_RenderPresent(renderer);
         SDL_Delay(12);
     }
 
     return play_pressed;
 }

// user selected "Blitz" in main menu:
static void OnSelectBlitz(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* header_font, TTF_Font* small_font){
    // configure global Blitz options
    g_blitz_mode_options.enabled = true;
    g_blitz_mode_options.duration_ms = 120000; // 2 minutes; change if you expose duration in UI

    // reuse the Classic modal code but request Blitz data
    bool user_pressed_play = ShowClassicMenu(window, renderer, header_font, small_font, /*isBlitz=*/true);

    if(user_pressed_play){
        // Start a brand new Blitz game. RunGameSDL will construct Game,
        // which reads g_blitz_mode_options in its constructor and starts blitz if enabled.
        RunGameSDL(window, renderer, header_font);

        // clear the flag after the game ends so normal games are unaffected
        g_blitz_mode_options.enabled = false;
    }
}