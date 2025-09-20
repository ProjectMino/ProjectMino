#include "menu.h"
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

static SDL_Color COL_BG = {18,18,18,255};
static SDL_Color COL_PANEL = {28,28,28,220};
static SDL_Color COL_TEXT = {230,230,230,255};
static SDL_Color COL_MUTED = {160,160,160,255};
static SDL_Color COL_HIGHLIGHT = {12,150,170,255};
static SDL_Color COL_EXIT = {220,60,60,255};

static void DrawText(SDL_Renderer* r, TTF_Font* f, const std::string& text, SDL_Color c, int x, int y, SDL_Texture** out_tex = nullptr, int* out_w = nullptr, int* out_h = nullptr) {
    if (!f) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(f, text.c_str(), c);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    if (out_tex) *out_tex = t;
    int w = s->w, h = s->h;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    SDL_FreeSurface(s);
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

void RenderMainMenu(SDL_Renderer* renderer, TTF_Font* font, MenuView view, int top_selected, int sub_selected, float anim) {
    if (!renderer) return;

    int w=1280,h=720;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    const float base_w = 1280.0f, base_h = 720.0f;
    float scale = std::min(static_cast<float>(w)/base_w, static_cast<float>(h)/base_h);

    // layout
    int left_x = static_cast<int>(60 * scale);
    int left_w = static_cast<int>(300 * scale);
    int left_y = static_cast<int>(80 * scale);
    int item_h = static_cast<int>(48 * scale);
    int gap = static_cast<int>(10 * scale);

    // top transparent bar (50% transparent)
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0,0,0,128); // 50% black
    SDL_Rect topbar = {0,0,w, static_cast<int>(56*scale)};
    SDL_RenderFillRect(renderer, &topbar);

    // full background (dark)
    SDL_SetRenderDrawColor(renderer, COL_BG.r, COL_BG.g, COL_BG.b, COL_BG.a);
    SDL_Rect full = {0,0,w,h};
    SDL_RenderFillRect(renderer, &full);

    // left column panel
    SDL_Rect leftPanel = { left_x - static_cast<int>(12*scale), left_y - static_cast<int>(12*scale), left_w + static_cast<int>(24*scale), static_cast<int>((7*(item_h+gap))+24*scale) };
    SDL_SetRenderDrawColor(renderer, COL_PANEL.r, COL_PANEL.g, COL_PANEL.b, COL_PANEL.a);
    SDL_RenderFillRect(renderer, &leftPanel);

    std::vector<std::string> main_items = { "SINGLEPLAYER", "MULTIPLAYER", "OPTIONS", "EXIT" };
    int start_y = left_y;
    // render each menu item
    std::vector<SDL_Rect> itemRects;
    for (size_t i=0;i<main_items.size();++i) {
        int ix = left_x;
        int iy = start_y + static_cast<int>(i)*(item_h + gap);
        // text width to compute underline
        int tw=0, th=0;
        if (font) {
            SDL_Surface* surf = TTF_RenderUTF8_Blended(font, main_items[i].c_str(), COL_TEXT);
            if (surf) { tw = surf->w; th = surf->h; SDL_FreeSurface(surf); }
        }
        SDL_Rect r = { ix, iy, left_w - static_cast<int>(8*scale), item_h };
        itemRects.push_back(r);

        // background for hovered/selected: slightly darker overlay
        if (static_cast<int>(i) == top_selected) {
            if (main_items[i] == "EXIT") {
                // red overlay for exit selected
                SDL_SetRenderDrawColor(renderer, COL_EXIT.r, COL_EXIT.g, COL_EXIT.b, 48);
            } else {
                SDL_SetRenderDrawColor(renderer, COL_HIGHLIGHT.r, COL_HIGHLIGHT.g, COL_HIGHLIGHT.b, 32);
            }
            SDL_RenderFillRect(renderer, &r);
        } else {
            SDL_SetRenderDrawColor(renderer, 50,50,50,30);
            SDL_RenderFillRect(renderer, &r);
        }

        // draw text (left aligned)
        SDL_Color textc = (main_items[i]=="EXIT" && static_cast<int>(i)==top_selected) ? COL_EXIT : COL_TEXT;
        if (font) {
            DrawText(renderer, font, main_items[i], textc, ix + static_cast<int>(12*scale), iy + (item_h - (th?th:16))/2);
        }
    }

    // animated underline under selected main item
    if (!itemRects.empty()) {
        SDL_Rect sel = itemRects[std::min<int>(top_selected, (int)itemRects.size()-1)];
        int underline_w = static_cast<int>((sel.w - 24*scale) * (0.6f + 0.4f * std::sin(anim * 4.0f)));
        int underline_h = std::max(2, static_cast<int>(3*scale));
        int ux = sel.x + static_cast<int>(12*scale);
        int uy = sel.y + sel.h - underline_h - static_cast<int>(6*scale);
        SDL_SetRenderDrawColor(renderer, COL_HIGHLIGHT.r, COL_HIGHLIGHT.g, COL_HIGHLIGHT.b, 220);
        SDL_Rect urect = { ux, uy, underline_w, underline_h };
        SDL_RenderFillRect(renderer, &urect);
    }

    // render submenu when applicable to the right of the main list
    if (view == MenuView::SINGLEPLAYER_SUB || view == MenuView::MULTIPLAYER_SUB) {
        std::vector<std::string> sub;
        if (view == MenuView::SINGLEPLAYER_SUB) sub = { "Classic", "Blitz", "40 Lines", "Cheese" };
        else sub = { "Ranked", "Casual", "Custom Room" };

        int sub_x = left_x + left_w + static_cast<int>(40*scale);
        int sub_y = start_y;
        int sub_w = static_cast<int>(320*scale);
        SDL_Rect subPanel = { sub_x - static_cast<int>(12*scale), sub_y - static_cast<int>(12*scale), sub_w + static_cast<int>(24*scale), static_cast<int>((sub.size()*(item_h+gap))+24*scale) };
        SDL_SetRenderDrawColor(renderer, COL_PANEL.r, COL_PANEL.g, COL_PANEL.b, COL_PANEL.a);
        SDL_RenderFillRect(renderer, &subPanel);

        std::vector<SDL_Rect> subRects;
        for (size_t i=0;i<sub.size();++i) {
            int ix = sub_x;
            int iy = sub_y + static_cast<int>(i)*(item_h + gap);
            SDL_Rect r = { ix, iy, sub_w - static_cast<int>(8*scale), item_h };
            subRects.push_back(r);

            // highlight current submenu selection
            if (static_cast<int>(i) == sub_selected) {
                SDL_SetRenderDrawColor(renderer, COL_HIGHLIGHT.r, COL_HIGHLIGHT.g, COL_HIGHLIGHT.b, 28);
                SDL_RenderFillRect(renderer, &r);
            } else {
                SDL_SetRenderDrawColor(renderer, 50,50,50,30);
                SDL_RenderFillRect(renderer, &r);
            }

            // draw text
            if (font) {
                DrawText(renderer, font, sub[i], COL_TEXT, ix + static_cast<int>(12*scale), iy + (item_h - 18)/2);
            }
        }

        // animated underline for submenu
        if (!subRects.empty()) {
            SDL_Rect ssel = subRects[std::min<int>(sub_selected, (int)subRects.size()-1)];
            int underline_w = static_cast<int>((ssel.w - 24*scale) * (0.5f + 0.5f * std::fabs(std::sin(anim * 3.0f))));
            int underline_h = std::max(2, static_cast<int>(3*scale));
            int ux = ssel.x + static_cast<int>(12*scale);
            int uy = ssel.y + ssel.h - underline_h - static_cast<int>(6*scale);
            SDL_SetRenderDrawColor(renderer, COL_HIGHLIGHT.r, COL_HIGHLIGHT.g, COL_HIGHLIGHT.b, 200);
            SDL_Rect urect = { ux, uy, underline_w, underline_h };
            SDL_RenderFillRect(renderer, &urect);
        }
    }

    // center area small logo/title
    int center_w = static_cast<int>(480*scale);
    int center_h = static_cast<int>(240*scale);
    int center_x = (w - center_w)/2;
    int center_y = static_cast<int>(h*0.4f) - center_h/2;
    SDL_Rect centerRect = { center_x, center_y, center_w, center_h };
    SDL_SetRenderDrawColor(renderer, 20,20,20,220);
    SDL_RenderFillRect(renderer, &centerRect);
    if (font) {
        DrawText(renderer, font, "Tetris Grid Board", COL_MUTED, center_x + (center_w/2 - 180), center_y + (center_h/2 - 12));
    }
}