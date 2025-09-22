#include "game.h"
#include "classic.h"   // <- ensure global options symbol is defined here
#include <SDL2/SDL.h>
#include <algorithm>
#include <iostream>

// define the global options instance declared in classic.h
ClassicModeOptions g_classic_mode_options;

const std::array<std::vector<Vec>,7> TETROS = {
    std::vector<Vec>{ {0,1},{1,1},{2,1},{3,1} },
    std::vector<Vec>{ {0,0},{0,1},{1,1},{2,1} },
    std::vector<Vec>{ {2,0},{0,1},{1,1},{2,1} },
    std::vector<Vec>{ {1,0},{2,0},{1,1},{2,1} },
    std::vector<Vec>{ {1,0},{2,0},{0,1},{1,1} },
    std::vector<Vec>{ {1,0},{0,1},{1,1},{2,1} },
    std::vector<Vec>{ {0,0},{1,0},{1,1},{2,1} }
};

const std::array<SDL_Color,7> T_COLORS = {
    SDL_Color{159,239,247,255}, // I - #9FEFF7
    SDL_Color{255,227,159,255}, // O - #FFE39F
    SDL_Color{214,179,255,255}, // T - #D6B3FF
    SDL_Color{191,247,177,255}, // S - #BFF7B1
    SDL_Color{255,179,179,255}, // Z - #FFB3B3
    SDL_Color{175,203,255,255}, // J - #AFCBFF
    SDL_Color{255,214,168,255}  // L - #FFD6A8
};

Game::Game(){
    rng.seed((unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    refill_bag();
    // ensure the next queue is seeded before spawning the first piece
    for (int i = 0; i < 5; ++i) next_queue.push_back(next_from_bag());
    // spawn the first current piece and keep queue filled
    spawn_from_queue();
    for (int i = 1; i < 5; ++i) spawn_from_queue(); // if original logic expected 5 total calls
    last_horiz_move = std::chrono::steady_clock::now();
    last_soft_move = std::chrono::steady_clock::now();
    start_time = std::chrono::steady_clock::now();

    // capture the global classic options at creation time if any were set
    classic_opts = g_classic_mode_options;
}

void Game::refill_bag(){
    bag.clear();
    // fill with 0..6 then shuffle
    for (int i = 0; i < 7; ++i) bag.push_back(i);
    std::shuffle(bag.begin(), bag.end(), rng);
    bag_index = 0;
}

int Game::next_from_bag(){
    if (bag_index >= (int)bag.size()) {
        refill_bag();
    }
    return bag[bag_index++];
}

void Game::spawn_from_queue(){
    int id = next_queue.front();
    next_queue.erase(next_queue.begin());
    next_queue.push_back(next_from_bag());
    current.id = id;
    current.blocks = TETROS[id];
    current.orientation = 0;
    current.color = T_COLORS[id];
    cur_pos = {3,0};
    hold_used = false;
    last_drop = std::chrono::steady_clock::now();
}

bool Game::collides(const Piece &p, Vec pos){
    for(auto &b:p.blocks){
        int x = pos.x + b.x;
        int y = pos.y + b.y;
        if(x<0||x>=COLS||y<0||y>=ROWS) return true;
        if(grid[y][x]) return true;
    }
    return false;
}

void Game::lock_piece(){
    for(auto &b: current.blocks){
        int x = cur_pos.x + b.x;
        int y = cur_pos.y + b.y;
        if(y>=0 && y<ROWS && x>=0 && x<COLS) grid[y][x] = current.id+1;
        spawn_particles_at(x,y, current.color, 3, false);
    }

    // compute the renderer/window size and scaled playfield origin so text/effects are positioned correctly
    int winw = 800, winh = 720;
    if(ren) SDL_GetRendererOutputSize(ren, &winw, &winh);

    // base field size (unscaled)
    int base_field_w = COLS * CELL;
    int base_field_h = ROWS * CELL;
    // scale down only if window is smaller than base field; otherwise keep 1.0
    float scale = std::min(1.0f, std::min(winw / (float)base_field_w, winh / (float)base_field_h));
    int field_w = int(COLS * CELL * scale);
    int field_h = int(ROWS * CELL * scale);
    int fx = (winw - field_w) / 2;
    int fy = (winh - field_h) / 2;

    // detect clears and spins/effects
    auto cleared = detect_full_rows();
    // default no spin
    TSpinType tspinType = TSpinType::None;
    if(current.id == 5 && last_was_rotate){
        tspinType = detect_tspin(*this, current, cur_pos);
    }

    // spawn text effects based on cleared rows and spins
    if(!cleared.empty()){
        rows_to_clear = cleared;
        clearing = true;
        clear_start = std::chrono::steady_clock::now();
        start_clear_animation();
        for(int r: rows_to_clear){
            for(int c=0;c<COLS;c++) spawn_particles_at(c,r, SDL_Color{255,255,255,255}, 4, true);
        }

        // choose text for clear count
        int cnt = (int)cleared.size();
        if(cnt==1) spawn_text_effect("Single", SDL_Color{255,255,255,255}, 900, fx + field_w/2, fy + cleared[0]*int(CELL*scale), 1);
        else if(cnt==2) spawn_text_effect("Double", SDL_Color{200,255,200,255}, 900, fx + field_w/2, fy + cleared[0]*int(CELL*scale), 2);
        else if(cnt==3) spawn_text_effect("Triple", SDL_Color{200,200,255,255}, 900, fx + field_w/2, fy + cleared[0]*int(CELL*scale), 3);
        else if(cnt>=4) spawn_text_effect("Quad", SDL_Color{255,200,200,255}, 900, fx + field_w/2, fy + cleared[0]*int(CELL*scale), 4);

        // all clear detection
        bool all_empty = true;
        for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++) if(grid[r][c]) all_empty = false;
        if(all_empty) spawn_text_effect("ALL CLEAR.", SDL_Color{255,215,0,255}, 1200, fx + field_w/2, fy + field_h/2, 5);
    } else {
        // no clears, but if tspin or rotation happened close to lock, show spin text
        if(tspinType != TSpinType::None){
            if(tspinType == TSpinType::Full){
                spawn_text_effect("T-Spin", SDL_Color{255,100,255,255}, 1000, fx + int(cur_pos.x*CELL*scale) + int(CELL*scale)/2, fy + int(cur_pos.y*CELL*scale), 10);
            } else {
                spawn_text_effect("T-Spin Mini", SDL_Color{200,120,200,255}, 900, fx + int(cur_pos.x*CELL*scale) + int(CELL*scale)/2, fy + int(cur_pos.y*CELL*scale), 10);
            }
        } else if(last_was_rotate && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_rotate_time).count() < 400){
            // generic piece spin (heuristic): show piece-specific spin text
            const char* names[7] = {"I spin","J spin","L spin","O spin","S spin","T-Spin","Z spin"};
            int id = current.id;
            std::string label = names[id];
            SDL_Color col = current.color;
            spawn_text_effect(label, col, 900, fx + int(cur_pos.x*CELL*scale) + int(CELL*scale)/2, fy + int(cur_pos.y*CELL*scale), 11+id);
        }
        spawn_from_queue();
    }
    last_was_rotate = false;
}

std::vector<int> Game::detect_full_rows(){
    std::vector<int> out;
    for(int r=0;r<ROWS;r++){
        bool full=true;
        for(int c=0;c<COLS;c++) if(grid[r][c]==0) { full=false; break; }
        if(full) out.push_back(r);
    }
    return out;
}

void Game::perform_clear_collapse(){
    std::sort(rows_to_clear.begin(), rows_to_clear.end());
    for(int rr_idx = (int)rows_to_clear.size()-1; rr_idx>=0; --rr_idx){
        int r = rows_to_clear[rr_idx];
        for(int row=r; row>0; --row) grid[row] = grid[row-1];
        grid[0].fill(0);
    }
    rows_to_clear.clear();
    clear_progress.clear();
    clearing = false;
    spawn_from_queue();
}

void Game::start_clear_animation(){
    clear_progress.clear();
    for(size_t i=0;i<rows_to_clear.size();++i) clear_progress.push_back(0.0f);
    clear_start = std::chrono::steady_clock::now();
}

void Game::spawn_particles_at(int cellx, int celly, SDL_Color col, int count, bool streak){
    for(int i=0;i<count;i++){
        Particle p;
        p.x = cellx + 0.5f + ((rand()%100)/100.0f - 0.5f);
        p.y = celly + 0.5f + ((rand()%100)/100.0f - 0.5f);
        if(!streak){
            p.vx = ((rand()%200)/100.0f - 1.0f) * 2.0f;
            p.vy = -((rand()%200)/100.0f) * 3.0f - 0.5f;
            p.size = 1.5f + (rand()%200)/100.0f;
            p.max_life = 160 + (rand()%120);
        } else {
            float dir = ((rand()%200)/100.0f - 1.0f);
            p.vx = dir * 5.0f;
            p.vy = ((rand()%100)/100.0f - 0.5f) * 0.6f;
            p.size = 2.0f + (rand()%200)/100.0f;
            p.max_life = 110 + (rand()%80);
        }
        p.life = p.max_life;
        p.streak = streak;
        p.col = SDL_Color{ (Uint8)std::min(255, int(col.r) + (rand()%40 - 10)), (Uint8)std::min(255, int(col.g) + (rand()%40 - 10)), (Uint8)std::min(255, int(col.b) + (rand()%40 - 10)), 255 };
        particles.push_back(p);
    }
}

// SRS rotation helper: rotate block coords around origin
static std::vector<Vec> rotate_blocks(const std::vector<Vec> &blocks, bool cw){
    std::vector<Vec> out = blocks;
    for(auto &b: out){
        int x=b.x, y=b.y;
        if(cw){ b.x =  y; b.y = -x; } else { b.x = -y; b.y =  x; }
    }
    // normalize to min coords >= 0
    int minx=999, miny=999;
    for(auto &b:out){ minx = std::min(minx, b.x); miny = std::min(miny, b.y); }
    for(auto &b:out){ b.x -= minx; b.y -= miny; }
    return out;
}

void Game::rotate_piece(bool clockwise){
    // compute and store corner-occupancy before attempting rotation (strictness helper)
    last_pre_rot_corner_count = 0;
    if(current.id == 5){ // only relevant for T piece
        int cx = cur_pos.x + 1;
        int cy = cur_pos.y + 1;
        bool ownCell[ROWS][COLS] = {false};
        for (const auto &b : current.blocks){
            int ox = cur_pos.x + b.x;
            int oy = cur_pos.y + b.y;
            if (ox>=0 && ox<COLS && oy>=0 && oy<ROWS) ownCell[oy][ox] = true;
        }
        std::array<std::pair<int,int>,4> corners = {{
            {cx - 1, cy - 1}, {cx + 1, cy - 1},
            {cx - 1, cy + 1}, {cx + 1, cy + 1}
        }};
        for (const auto &cr : corners) {
            int rx = cr.first, ry = cr.second;
            if (rx < 0 || rx >= COLS || ry < 0 || ry >= ROWS) last_pre_rot_corner_count++;
            else if (grid[ry][rx] && !ownCell[ry][rx]) last_pre_rot_corner_count++;
        }
    } else {
        last_pre_rot_corner_count = 0;
    }

    // pre-rotate block coords
    std::vector<Vec> nb = rotate_blocks(current.blocks, clockwise);

    int from = (current.orientation % 4 + 4) % 4;
    int to = (from + (clockwise ? 1 : 3)) % 4;

    // Standard SRS kick tables (JLSTZ and I) ...
    const std::vector<std::vector<Vec>> kicks_jlstz = {
        { {0,0}, {-1,0}, {-1,1}, {0,-2}, {-1,-2} },
        { {0,0}, {1,0}, {1,-1}, {0,2}, {1,2} },
        { {0,0}, {1,0}, {1,1}, {0,-2}, {1,-2} },
        { {0,0}, {-1,0}, {-1,-1}, {0,2}, {-1,2} }
    };
    const std::vector<std::vector<Vec>> kicks_i = {
        { {0,0}, {-2,0}, {1,0}, {-2,-1}, {1,2} },
        { {0,0}, {-1,0}, {2,0}, {-1,2}, {2,-1} },
        { {0,0}, {2,0}, {-1,0}, {2,1}, {-1,-2} },
        { {0,0}, {1,0}, {-2,0}, {1,-2}, {-2,1} }
    };

    const bool isI = (current.id == 0);

    // reset last-kick metadata
    last_kick_index = -1;
    last_kick_offset = {0,0};
    last_was_rotate = false;

    const auto &kicks_table = isI ? kicks_i : kicks_jlstz;

    // Try standard kicks in order; store index and offset when one succeeds
    for (size_t i = 0; i < kicks_table[from].size(); ++i) {
        Vec k = kicks_table[from][i];
        Vec np = cur_pos;
        np.x += k.x;
        np.y += k.y;

        Piece p2 = current;
        p2.blocks = nb;
        if (!collides(p2, np)) {
            // commit rotation
            current.blocks = nb;
            current.orientation = to;
            cur_pos = np;
            last_kick_index = (int)i;   // 0 == no offset, >0 == a kick was used
            last_kick_offset = k;
            last_was_rotate = true;
            last_rotate_time = std::chrono::steady_clock::now();
            return;
        }
    }

    // rotation failed; leave last_kick_index = -1 and do not change piece
}

void Game::hold(){
    if(hold_used) return;
    if(hold_piece.blocks.empty()){
    hold_piece.id = current.id;
    hold_piece.blocks = current.blocks;
    hold_piece.orientation = current.orientation;
        hold_piece.color = current.color;
        spawn_from_queue();
    } else {
    std::swap(hold_piece.id, current.id);
    std::swap(hold_piece.blocks, current.blocks);
    std::swap(hold_piece.color, current.color);
    std::swap(hold_piece.orientation, current.orientation);
        cur_pos = {3,0};
        if(collides(current, cur_pos)) running=false;
    }
    hold_used = true;
}

void Game::soft_drop(){ Vec np = cur_pos; np.y++; if(!collides(current, np)) cur_pos = np; else { lock_piece(); } }

void Game::hard_drop(){ Vec np = cur_pos; while(!collides(current, {np.x, np.y+1})) np.y++; cur_pos = np; lock_piece(); }

void Game::tick(){
    if(paused) return;
    auto now = std::chrono::steady_clock::now();

    // horizontal DAS/ARR
    if(horiz_held && horiz_dir != 0){
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_horiz_move).count();
        if(!horiz_repeating){
            if(elapsed >= das_ms){ Vec np = cur_pos; np.x += horiz_dir; if(!collides(current,np)) cur_pos = np; horiz_repeating = true; last_horiz_move = now; }
        } else {
            if(elapsed >= arr_ms){ Vec np = cur_pos; np.x += horiz_dir; if(!collides(current,np)) cur_pos = np; last_horiz_move = now; }
        }
    }

    // continuous soft drop
    if(down_held){
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_soft_move).count();
        if(elapsed >= soft_ms){ Vec np = cur_pos; np.y++; if(!collides(current, np)) cur_pos = np; else { lock_piece(); } last_soft_move = now; }
    }

    // update particles
    for(auto it = particles.begin(); it != particles.end();){
        it->life -= 16;
        float t = 16.0f / 1000.0f;
        it->x += it->vx * t;
        it->y += it->vy * t;
        if(!it->streak) it->vy += 9.8f * t * 0.5f;
        if(it->life <= 0) it = particles.erase(it);
        else ++it;
    }

    // handle quick clear stomp timing
    if(clearing){
        auto el = std::chrono::duration_cast<std::chrono::milliseconds>(now - clear_start).count();
        float progress = std::min(1.0f, el / (float)clear_anim_ms);
        for(size_t i=0;i<clear_progress.size();++i) clear_progress[i] = progress;
        if(el >= clear_anim_ms){
            // collapse immediately after stomp
            perform_clear_collapse();
            int cleared = (int)clear_progress.size();
            lines += cleared;
            score += cleared * 100;
            level = 1 + lines/10;
            drop_ms = std::max(100, 800 - (level-1)*50);
        }
    }

    auto now2 = std::chrono::steady_clock::now();
    if(std::chrono::duration_cast<std::chrono::milliseconds>(now2-last_drop).count() >= drop_ms){
        Vec np = cur_pos; np.y++;
        if(!collides(current,np)) cur_pos = np;
        else lock_piece();
        last_drop = now2;
    }

    // fade-in effect
    if(fade_in){
        auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        screen_fade = 1.0f - std::min(1.0f, since / 800.0f);
        if(screen_fade <= 0.0f){ fade_in = false; screen_fade = 0.0f; }
    }
}

void Game::draw_text(int x,int y,const std::string &text){
    if(!font) return;
    SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), SDL_Color{255,255,255,255});
    SDL_Texture* tx = SDL_CreateTextureFromSurface(ren,surf);
    SDL_Rect dst{ x, y, surf->w, surf->h };
    SDL_FreeSurface(surf);
    SDL_RenderCopy(ren, tx, nullptr, &dst);
    SDL_DestroyTexture(tx);
}

void Game::draw_colored_text(int x,int y,const std::string &text, SDL_Color color, float scale, int alpha){
    if(!font) return;
    SDL_Color c = color; c.a = (Uint8)alpha;
    SDL_Surface* surf = TTF_RenderText_Blended(font, text.c_str(), c);
    SDL_Texture* tx = SDL_CreateTextureFromSurface(ren,surf);
    SDL_SetTextureAlphaMod(tx, (Uint8)alpha);
    SDL_Rect dst{ x - surf->w/2, y - surf->h/2, (int)(surf->w*scale), (int)(surf->h*scale) };
    SDL_FreeSurface(surf);
    SDL_RenderCopyEx(ren, tx, nullptr, &dst, 0, nullptr, SDL_FLIP_NONE);
    SDL_DestroyTexture(tx);
}

void Game::spawn_text_effect(const std::string &txt, SDL_Color col, int life_ms, int x, int y, int type){
    TextEffect te;
    te.text = txt;
    te.color = col;
    te.life_ms = life_ms;
    te.start = std::chrono::steady_clock::now();
    te.x = x; te.y = y; te.type = type;
    effects.push_back(te);
}

void Game::render(){
    // clear
    SDL_SetRenderDrawColor(ren, 0,0,0,255);
    SDL_RenderClear(ren);

    // get actual renderer/window size so everything is centered and can scale
    int winw = 800, winh = 720;
    if(ren) SDL_GetRendererOutputSize(ren, &winw, &winh);

    // compute scale so playfield fits into current window (do not upscale beyond 1.0)
    int base_field_w = COLS * CELL;
    int base_field_h = ROWS * CELL;
    float scale = std::min(1.0f, std::min(winw / (float)base_field_w, winh / (float)base_field_h));

    // scaled cell size used only for rendering/layout in this call (keeps logic non-invasive)
    int sCELL = std::max(4, int(CELL * scale));

    int field_w = COLS * sCELL;
    int field_h = ROWS * sCELL;
    int fx = (winw - field_w) / 2;
    int fy = (winh - field_h) / 2;

    SDL_Rect fieldRect{fx, fy, field_w, field_h};
    SDL_SetRenderDrawColor(ren, 40,40,40,255);
    SDL_RenderFillRect(ren, &fieldRect);

    // draw grid (placed blocks & empty)
    for(int r=0;r<ROWS;r++){
        for(int c=0;c<COLS;c++){
            SDL_Rect cell{fx + c*sCELL, fy + r*sCELL, sCELL-1, sCELL-1};
            int val = grid[r][c];
            if(val){
                SDL_Color col = T_COLORS[val-1];
                int px = cell.x;
                int py = cell.y;
                int w = cell.w;
                int h = cell.h;

                // soft neon glow (reduced intensity tuned for scaled rendering)
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
                const int PLACED_GLOW_LAYERS = 3;
                for(int li = PLACED_GLOW_LAYERS; li >= 1; --li){
                    int ext = li * 2;
                    Uint8 a = (Uint8)std::max(3, 24 / li);
                    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, a);
                    SDL_Rect g{ px - ext, py - ext, w + ext*2, h + ext*2 };
                    SDL_RenderFillRect(ren, &g);
                }
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

                // solid block core
                SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
                SDL_Rect blockRect{ px, py, w, h };
                SDL_RenderFillRect(ren, &blockRect);

            } else {
                SDL_SetRenderDrawColor(ren, 20,20,20,255);
                SDL_RenderFillRect(ren, &cell);
            }
        }
    }

    // ghost piece (subtle neon)
    {
        Vec gp = cur_pos;
        while(!collides(current, {gp.x, gp.y+1})) gp.y++;
        for(auto &b: current.blocks){
            int x = fx + (gp.x + b.x) * sCELL;
            int y = fy + (gp.y + b.y) * sCELL;
            SDL_Rect rc{x,y,sCELL-1,sCELL-1};

            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
            const int GHOST_LAYERS = 2;
            for(int li = GHOST_LAYERS; li >= 1; --li){
                int ext = li * 2;
                Uint8 a = (Uint8)std::max(4, 18 / li);
                SDL_SetRenderDrawColor(ren, current.color.r/2, current.color.g/2, current.color.b/2, a);
                SDL_Rect g{ rc.x - ext, rc.y - ext, rc.w + ext*2, rc.h + ext*2 };
                SDL_RenderFillRect(ren, &g);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

            SDL_SetRenderDrawColor(ren, current.color.r, current.color.g, current.color.b, 140);
            SDL_RenderFillRect(ren, &rc);
        }
    }

    // draw current grid with possible per-row clearing visuals
    for(int r=0;r<ROWS;r++){
        for(int c=0;c<COLS;c++){
            int cellVal = grid[r][c];
            bool isClearing = false;
            float prog = 0.0f;
            auto it = std::find(rows_to_clear.begin(), rows_to_clear.end(), r);
            if(it != rows_to_clear.end()){
                size_t idx = std::distance(rows_to_clear.begin(), it);
                isClearing = true;
                prog = clear_progress.size() > idx ? clear_progress[idx] : 0.0f;
            }
            if(cellVal!=0){
                SDL_Color col = T_COLORS[cellVal-1];
                int px = fx + c*sCELL;
                int py = fy + r*sCELL;

                // drop shadow
                SDL_SetRenderDrawColor(ren, 0,0,0,65);
                SDL_Rect sh{px + std::max(1, sCELL/6), py + std::max(1, sCELL/6), sCELL-1, sCELL-1};
                SDL_RenderFillRect(ren, &sh);

                // clear compression visual
                int drawX = px;
                int drawY = py;
                int drawW = sCELL-1;
                int drawH = sCELL-1;
                if(isClearing){
                    float scaleY = 1.0f - prog * 0.8f; // compress to 20%
                    drawH = std::max(1, int((sCELL-1) * scaleY));
                    drawY = py + ((sCELL-1) - drawH)/2;
                }

                // glow
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
                const int CLR_GLOW_LAYERS = 3;
                for(int li = CLR_GLOW_LAYERS; li >= 1; --li){
                    int ext = li * 2;
                    Uint8 a = (Uint8)std::max(4, 20 / li);
                    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, a);
                    SDL_Rect g{ drawX - ext, drawY - ext, drawW + ext*2, drawH + ext*2 };
                    SDL_RenderFillRect(ren, &g);
                }
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

                // core
                SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
                SDL_Rect cellRect{drawX, drawY, drawW, drawH};
                SDL_RenderFillRect(ren, &cellRect);
            }
        }
    }

    // current piece
    for(auto &b: current.blocks){
        int x = fx + (cur_pos.x + b.x) * sCELL;
        int y = fy + (cur_pos.y + b.y) * sCELL;
        SDL_Rect rc{x,y,sCELL-1,sCELL-1};

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
        const int PIECE_GLOW_LAYERS = 3;
        for(int li = PIECE_GLOW_LAYERS; li >= 1; --li){
            int ext = li * 2;
            Uint8 a = (Uint8)std::max(6, 24 / li);
            SDL_SetRenderDrawColor(ren, current.color.r, current.color.g, current.color.b, a);
            SDL_Rect g{ rc.x - ext, rc.y - ext, rc.w + ext*2, rc.h + ext*2 };
            SDL_RenderFillRect(ren, &g);
        }
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

        SDL_SetRenderDrawColor(ren, current.color.r, current.color.g, current.color.b,255);
        SDL_RenderFillRect(ren, &rc);
    }

    // particles (convert cell-space to pixel using sCELL)
    for(auto &p: particles){
        int px = int(fx + (p.x * sCELL));
        int py = int(fy + (p.y * sCELL));
        float life_ratio = p.max_life > 0 ? (float)p.life / (float)p.max_life : 0.0f;
        Uint8 alpha = (Uint8)(255 * std::max(0.0f, std::min(1.0f, life_ratio)));
        if(p.streak){
            int len = int(6 * (1.0f + (1.0f-life_ratio)*2.0f));
            int w = std::max(1, int(p.size));
            SDL_Rect sr{px - len, py - w/2, len, w};
            SDL_SetRenderDrawColor(ren, p.col.r, p.col.g, p.col.b, alpha);
            SDL_RenderFillRect(ren, &sr);
        } else {
            int size = std::max(1, int(p.size));
            SDL_Rect pr{px-size/2, py-size/2, size, size};
            SDL_SetRenderDrawColor(ren, p.col.r, p.col.g, p.col.b, alpha);
            SDL_RenderFillRect(ren, &pr);
        }
    }

    // Next panel (position relative to scaled field)
    SDL_Rect nextPanel{fx + field_w + std::max(8, sCELL/2), fy, std::max(120, int(160 * scale)), std::max(120, int(220 * scale))};
    SDL_SetRenderDrawColor(ren, 30,30,30,220);
    SDL_RenderFillRect(ren, &nextPanel);
    draw_text(nextPanel.x + 12, nextPanel.y + 8, "Next");
    for(size_t i=0;i<next_queue.size();i++){
        int id = next_queue[i];
        int bx = nextPanel.x + 12;
        int by = nextPanel.y + 32 + int(i * (sCELL * 1.1f));
        for(auto &bb: TETROS[id]){
            SDL_Rect rc{ bx + bb.x*(sCELL/2), by + bb.y*(sCELL/2), sCELL/2 -1, sCELL/2 -1 };
            SDL_Color col = T_COLORS[id];
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
            for(int li=2; li>=1; --li){
                int ext = li * 1;
                Uint8 a = (Uint8)std::max(10, 45 / li);
                SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, a);
                SDL_Rect g{ rc.x - ext, rc.y - ext, rc.w + ext*2, rc.h + ext*2 };
                SDL_RenderFillRect(ren, &g);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, col.r, col.g, col.b,255);
            SDL_RenderFillRect(ren, &rc);
            SDL_SetRenderDrawColor(ren, std::max(0,col.r-90), std::max(0,col.g-90), std::max(0,col.b-90),220);
            SDL_RenderDrawRect(ren, &rc);
        }
    }

    // Hold panel
    SDL_Rect holdPanel{fx - std::max(176, int(176 * scale)), fy, std::max(120, int(160 * scale)), std::max(80, int(120 * scale))};
    SDL_SetRenderDrawColor(ren, 30,30,30,220);
    SDL_RenderFillRect(ren, &holdPanel);
    draw_text(holdPanel.x + 12, holdPanel.y + 8, "Hold");
    if(!hold_piece.blocks.empty()){
        int bx = holdPanel.x + 12;
        int by = holdPanel.y + 32;
        for(auto &bb: hold_piece.blocks){
            SDL_Rect rc{ bx + bb.x*(sCELL/2), by + bb.y*(sCELL/2), sCELL/2 -1, sCELL/2 -1 };
            SDL_Color col = hold_piece.color;
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_ADD);
            for(int li=2; li>=1; --li){
                int ext = li * 1;
                Uint8 a = (Uint8)std::max(3, 15 / li);
                SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, a);
                SDL_Rect g{ rc.x - ext, rc.y - ext, rc.w + ext*2, rc.h + ext*2 };
                SDL_RenderFillRect(ren, &g);
            }
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, col.r, col.g, col.b,255);
            SDL_RenderFillRect(ren, &rc);
        }
    }

    // Stats panel (scaled)
    SDL_Rect statsPanel{fx - std::max(176, int(176 * scale)), fy + field_h - std::max(160, int(160 * scale)), std::max(120, int(160 * scale)), std::max(120, int(160 * scale))};
    SDL_SetRenderDrawColor(ren, 30,30,30,220);
    SDL_RenderFillRect(ren, &statsPanel);
    int sx = statsPanel.x + 12;
    int sy = statsPanel.y + 12;
    draw_text(sx, sy, "Stats");
    draw_text(sx, sy+24, std::string("Score: ")+std::to_string(score));
    draw_text(sx, sy+48, std::string("Lines: ")+std::to_string(lines));
    draw_text(sx, sy+72, std::string("Level: ")+std::to_string(level));

    // screen fade
    if(screen_fade > 0.001f){
        Uint8 a = (Uint8)(screen_fade * 220);
        SDL_SetRenderDrawColor(ren, 0,0,0,a);
        SDL_Rect full{0,0,winw,winh};
        SDL_RenderFillRect(ren, &full);
    }

    // stomp highlight for cleared rows
    if(clearing && !rows_to_clear.empty()){
        for(size_t i=0;i<rows_to_clear.size();++i){
            int r = rows_to_clear[i];
            float progress = clear_progress.size() > i ? clear_progress[i] : 0.0f;
            float cf = 1.0f - progress * 0.8f; // compress factor
            int panel_h = std::max(1, int(sCELL * cf));
            int yoff = (sCELL - panel_h) / 2;
            SDL_SetRenderDrawColor(ren, 220,220,220, 200);
            SDL_Rect stompRect{fx, fy + r*sCELL + yoff, field_w, panel_h};
            SDL_RenderFillRect(ren, &stompRect);
        }
    }

    // render text effects (they were positioned using same fx/fy & scaled cell pos when spawned)
    for(auto it = effects.begin(); it != effects.end();){
        auto now = std::chrono::steady_clock::now();
        int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - it->start).count();
        if(elapsed >= it->life_ms){ it = effects.erase(it); continue; }
        float t = elapsed / (float)it->life_ms; // 0..1

        // effect variants by type (re-use draw_colored_text/draw_text which use g.font)
        switch(it->type){
            case 1: // Single - white pop
                draw_colored_text(it->x, it->y - int(40 * (1.0f - t)), it->text, it->color, 1.0f + 0.2f * (1.0f - t), (int)(255 * (1.0f - t)) );
                break;
            case 2: // Double - green burst
                draw_colored_text(it->x, it->y - int(48 * (1.0f - t)), it->text, it->color, 1.1f, (int)(255 * (1.0f - t)) );
                break;
            case 3: // Triple - purple sweep
                draw_colored_text(it->x, it->y - int(56 * t), it->text, it->color, 1.2f - 0.2f * t, (int)(255 * (1.0f - t)) );
                break;
            case 4: // Quad - modern particle ring + centered text
            {
                SDL_SetRenderDrawColor(ren, 10,10,10, (Uint8)(160 * (1.0f - t)));
                SDL_Rect backdrop{it->x - 140, it->y - 34, 280, 68};
                SDL_RenderFillRect(ren, &backdrop);

                std::string label = it->text;
                for(auto &ch: label) ch = toupper(ch);
                draw_colored_text(it->x, it->y, label, SDL_Color{240,240,240,255}, 1.6f - 0.5f * t, (int)(255 * (1.0f - t)) );

                if(t < 0.35f){
                    int ringCount = 18;
                    float ringT = t / 0.35f; // 0..1
                    for(int pi=0; pi<ringCount; ++pi){
                        float ang = (pi / (float)ringCount) * 6.2831853f;
                        float radius = 40.0f * (0.6f + 0.8f * ringT);
                        Particle p;
                        p.x = (it->x) / (float)sCELL + std::cos(ang) * (radius / sCELL);
                        p.y = (it->y) / (float)sCELL + std::sin(ang) * (radius / sCELL);
                        float speed = 40.0f * (0.6f + 0.6f * (1.0f - ringT)) / (float)sCELL;
                        p.vx = std::cos(ang) * speed;
                        p.vy = std::sin(ang) * speed * 0.6f - 0.6f;
                        p.size = 2.0f + (rand()%100)/100.0f * 2.0f;
                        p.max_life = 300 + rand()%200;
                        p.life = p.max_life;
                        p.streak = false;
                        p.col = SDL_Color{ (Uint8)std::min(255, it->color.r+30), (Uint8)std::min(255, it->color.g+30), (Uint8)std::min(255, it->color.b+30), 255 };
                        particles.push_back(p);
                    }
                }
                break;
            }
            case 5: // ALL CLEAR
                draw_colored_text(it->x, it->y, it->text, it->color, 1.6f - 0.4f * t, 255);
                break;
            case 10: // T-Spin
                draw_colored_text(it->x, it->y - int(20 * (1.0f - t)), it->text, it->color, 1.3f, 255);
                for(int i=0;i<4;i++){
                    int sx = it->x + (int)(std::sin((t + i*0.25f) * 6.28f) * 30);
                    int sy = it->y + (int)(std::cos((t + i*0.25f) * 6.28f) * 8);
                    SDL_SetRenderDrawColor(ren, it->color.r, it->color.g, it->color.b, (Uint8)(200 * (1.0f - t)) );
                    SDL_Rect pr{sx-2, sy-2, 4,4}; SDL_RenderFillRect(ren, &pr);
                }
                break;
            default:
                draw_colored_text(it->x, it->y - int(20 * t), it->text, it->color, 1.0f + 0.3f * (1.0f - t), (int)(255 * (1.0f - t)));
                break;
        }
        ++it;
    }

    SDL_RenderPresent(ren);
}

// Run a small in-place game loop. This uses Game::tick/render and basic input mapping.
// It is intentionally minimal — it supports left/right, soft/hard drop, rotate, hold and pause/quit.
int RunGameSDL(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font){
    if(!window || !renderer) return -1;
    Game g;
    g.win = window;
    g.ren = renderer;
    g.font = font;

    // Prefer subtext.ttf for in-game rendering if available. If loading fails, fall back to provided font.
    bool loaded_subtext = false;
    if(g.ren){
        // try to load a reasonable default size; TTF_OpenFont returns nullptr if not found.
        TTF_Font* sub = TTF_OpenFont("src/assets/subtext.ttf", 18);
        if(sub){
            // replace font for the game's runtime
            g.font = sub;
            loaded_subtext = true;
        }
    }

    // If a countdown is requested, show it before allowing normal tick progression.
    if(g.classic_opts.use_countdown && g.classic_opts.countdown_ms > 0){
        int total_ms = g.classic_opts.countdown_ms;
        int seconds = std::max(1, total_ms / 1000);
        for(int s = seconds; s >= 1; --s){
            // render the current field behind a big number
            g.render();
            // overlay big number in center (if font available)
            if(g.font){
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", s);
                SDL_Surface* surf = TTF_RenderText_Blended(g.font, buf, SDL_Color{255,255,255,255});
                if(surf){
                    SDL_Texture* tx = SDL_CreateTextureFromSurface(renderer, surf);
                    int ww=0, hh=0; SDL_GetRendererOutputSize(renderer, &ww, &hh);
                    SDL_Rect dst{ (ww - surf->w)/2, (hh - surf->h)/2, surf->w, surf->h };
                    SDL_SetTextureBlendMode(tx, SDL_BLENDMODE_BLEND);
                    SDL_RenderCopy(renderer, tx, nullptr, &dst);
                    SDL_DestroyTexture(tx);
                    SDL_FreeSurface(surf);
                }
            }
            SDL_RenderPresent(renderer);
            SDL_Delay(1000);
        }
    }

    // main in-game loop
    Uint64 last = SDL_GetTicks64();
    bool quit = false;
    while(!quit && g.running){
        Uint64 now = SDL_GetTicks64();
        float dt = (now - last) / 1000.0f;
        last = now;

        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT){ g.running = false; quit = true; break; }
            else if(e.type == SDL_KEYDOWN){
                if(e.key.keysym.sym == SDLK_ESCAPE){ g.running = false; quit = true; break; }
                else if(e.key.keysym.sym == SDLK_LEFT){
                    g.horiz_held = true; g.horiz_dir = -1; g.horiz_repeating = false; g.last_horiz_move = std::chrono::steady_clock::now();
                    // immediate one-step
                    Vec np = g.cur_pos; np.x += g.horiz_dir; if(!g.collides(g.current,np)) g.cur_pos = np;
                } else if(e.key.keysym.sym == SDLK_RIGHT){
                    g.horiz_held = true; g.horiz_dir = 1; g.horiz_repeating = false; g.last_horiz_move = std::chrono::steady_clock::now();
                    Vec np = g.cur_pos; np.x += g.horiz_dir; if(!g.collides(g.current,np)) g.cur_pos = np;
                } else if(e.key.keysym.sym == SDLK_DOWN){
                    g.down_held = true; g.last_soft_move = std::chrono::steady_clock::now();
                } else if(e.key.keysym.sym == SDLK_UP){
                    // rotate by pressing the Up arrow (clockwise)
                    g.rotate_piece(true);
                } else if(e.key.keysym.sym == SDLK_SPACE){
                    g.hard_drop();
                } else if(e.key.keysym.sym == SDLK_z){
                    g.rotate_piece(false); // CCW
                } else if(e.key.keysym.sym == SDLK_x){
                    g.rotate_piece(true);  // CW
                } else if(e.key.keysym.sym == SDLK_c){
                    g.hold();
                } else if(e.key.keysym.sym == SDLK_p){
                    g.paused = !g.paused;
                }
            } else if(e.type == SDL_KEYUP){
                if(e.key.keysym.sym == SDLK_LEFT || e.key.keysym.sym == SDLK_RIGHT){
                    // releasing either left/right turns off horizontal hold only if that direction was the active one
                    if((e.key.keysym.sym == SDLK_LEFT && g.horiz_dir == -1) || (e.key.keysym.sym == SDLK_RIGHT && g.horiz_dir == 1)){
                        g.horiz_held = false; g.horiz_dir = 0; g.horiz_repeating = false;
                    }
                } else if(e.key.keysym.sym == SDLK_DOWN){
                    g.down_held = false;
                }
            } else if(e.type == SDL_MOUSEBUTTONDOWN){
                // optional: allow clicking to rotate/hold etc in future
            }
        }

        // tick + render
        g.tick();
        g.render();

        // simple cap ~60 FPS
        SDL_Delay(16);
    }

    // if we loaded the subtext font here, free it now
    if(loaded_subtext && g.font){
        TTF_CloseFont(g.font);
        g.font = nullptr;
    }

    return 0;
}
