#include "game.h"
#include "classic.h"   // <- ensure global options symbol is defined here
#include "blitz.h"
#include <SDL2/SDL.h>
#include "wallpapers.h"
#include <algorithm>
#include <cmath>
#include <iostream>

// ensure a single definition of the globals (one TU only)
ClassicModeOptions g_classic_mode_options;
BlitzModeOptions   g_blitz_mode_options;

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
    const int preview_count = 6;
    for (int i = 0; i < preview_count; ++i) next_queue.push_back(next_from_bag());
    // spawn the first current piece (this will maintain queue size)
    spawn_from_queue();
    last_horiz_move = std::chrono::steady_clock::time_point();
    last_soft_move = std::chrono::steady_clock::time_point();
    start_time = std::chrono::steady_clock::now();

    // capture the global classic options at creation time if any were set
    classic_opts = g_classic_mode_options;

    // capture blitz options and start blitz if enabled
    blitz_opts = g_blitz_mode_options;
    if (blitz_opts.enabled){
        start_blitz();
    }
}

// apply gravity based on level: compute drop_ms from level
static int gravity_for_level(int level){
    // simple stepped gravity: base 800ms, decrease by 50ms per level, min 50ms
    int ms = std::max(50, 800 - (level-1)*50);
    return ms;
}

// scoring helpers
static int score_for_lines(int count, int level){
    int mult = (level+1);
    switch(count){
        case 1: return 100 * mult;
        case 2: return 300 * mult;
        case 3: return 500 * mult;
        case 4: return 800 * mult;
    }
    return 100 * count * mult;
}

// SRS+ floor kicks: try extra y offsets to make rotations feel smoother
static const std::vector<Vec> extra_floor_kicks = { {0,0}, {0,-1}, {0,-2}, {1,0}, {-1,0} };

// Attempt rotation with SRS + additional floor kicks
bool try_rotate_with_kicks(Game &g, const std::vector<Vec> &nb, int toOrientation, bool isI, bool clockwise){
    int from = (g.current.orientation % 4 + 4) % 4;
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
    const auto &kicks_table = isI ? kicks_i : kicks_jlstz;

    for(size_t ki=0; ki<kicks_table[from].size(); ++ki){
        Vec k = kicks_table[from][ki];
        for(const Vec &efk : extra_floor_kicks){
            Vec np = g.cur_pos;
            np.x += k.x + efk.x;
            np.y += k.y + efk.y;
            Piece p2 = g.current;
            p2.blocks = nb;
            if(!g.collides(p2, np)){
                g.current.blocks = nb;
                g.current.orientation = toOrientation;
                g.cur_pos = np;
                g.last_kick_index = (int)ki;
                g.last_kick_offset = {k.x + efk.x, k.y + efk.y};
                g.last_was_rotate = true;
                g.last_rotate_time = std::chrono::steady_clock::now();
                return true;
            }
        }
    }
    return false;
}

// step processes one frame worth of inputs and advances the game
void Game::step(const InputState &in){
    // map input to hold/edge
    // handle hold (edge-triggered)
    if(in.hold && !in.hold_pressed){
        // do nothing here; RunGameSDL will manage edge detection and call hold()
    }

    // update input-held states
    horiz_dir = 0;
    if(in.left) horiz_dir = -1;
    else if(in.right) horiz_dir = 1;
    horiz_held = (horiz_dir != 0);
    down_held = in.soft;

    // If a left/right edge (first press) happened, perform an immediate move and reset the DAS timer
    if(in.left_edge && !in.right_edge){ Vec np = cur_pos; np.x += -1; if(!collides(current,np)) { cur_pos = np; } last_horiz_move = std::chrono::steady_clock::now(); horiz_repeating = false; actions_count++; }
    if(in.right_edge && !in.left_edge){ Vec np = cur_pos; np.x += 1; if(!collides(current,np)) { cur_pos = np; } last_horiz_move = std::chrono::steady_clock::now(); horiz_repeating = false; actions_count++; }

    // rotation inputs are edge-processed here: attempt rotate immediately
    if(in.rotate_cw){
        rotate_piece(true);
        actions_count++;
        // rotation resets lock if active
        if(lock_active){ lock_resets++; lock_start = std::chrono::steady_clock::now(); if(lock_resets>=max_lock_resets) { lock_active=false; } }
    }
    if(in.rotate_ccw){
        rotate_piece(false);
        actions_count++;
        if(lock_active){ lock_resets++; lock_start = std::chrono::steady_clock::now(); if(lock_resets>=max_lock_resets) { lock_active=false; } }
    }

    // horizontal movement: implement DAS/ARR
    auto now = std::chrono::steady_clock::now();
    if(horiz_held && horiz_dir != 0){
        if(last_horiz_move.time_since_epoch().count()==0) last_horiz_move = now;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_horiz_move).count();
        if(!horiz_repeating){
            if(elapsed >= das_ms){ Vec np = cur_pos; np.x += horiz_dir; if(!collides(current,np)) { cur_pos = np; }
                horiz_repeating = true; last_horiz_move = now; if(lock_active){ lock_resets++; lock_start = now; }
            }
        } else {
            if(elapsed >= arr_ms){ Vec np = cur_pos; np.x += horiz_dir; if(!collides(current,np)) { cur_pos = np; }
                last_horiz_move = now; if(lock_active){ lock_resets++; lock_start = now; }
            }
        }
    } else {
        horiz_repeating = false;
        // reset timing so next press gets full DAS
        last_horiz_move = std::chrono::steady_clock::time_point();
    }

    // soft drop handling
    if(down_held){
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_soft_move).count();
        if(last_soft_move.time_since_epoch().count()==0) last_soft_move = now;
        if(elapsed >= soft_ms){ Vec np = cur_pos; np.y++; if(!collides(current, np)) { cur_pos = np; score += 1; } else { lock_piece(); } last_soft_move = now; if(lock_active){ lock_resets++; lock_start = now; } }
    } else {
        last_soft_move = std::chrono::steady_clock::time_point();
    }

    // hard drop input handled via RunGameSDL because it's edge-triggered there (calls hard_drop())

    // gravity (tick based on drop_ms)
    drop_ms = gravity_for_level(level);
    auto now2 = std::chrono::steady_clock::now();
    if(std::chrono::duration_cast<std::chrono::milliseconds>(now2 - last_drop).count() >= drop_ms){
        Vec np = cur_pos; np.y++;
        if(!collides(current, np)){
            cur_pos = np;
        } else {
            // start lock delay if not already
            if(!lock_active){ lock_active = true; lock_start = now2; lock_resets = 0; }
            else {
                auto ld = std::chrono::duration_cast<std::chrono::milliseconds>(now2 - lock_start).count();
                if(ld >= lock_delay_ms || lock_resets >= max_lock_resets){
                    // force lock
                    lock_piece();
                    lock_active = false;
                    spawn_pending = true;
                    spawn_time = std::chrono::steady_clock::now();
                }
            }
        }
        last_drop = now2;
    }

    // if spawn pending and ARE elapsed, spawn
    if(spawn_pending){
        if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - spawn_time).count() >= are_ms){
            spawn_from_queue();
            can_hold = true; // allow hold after spawn
            spawn_pending = false;
            // reset drop timer to avoid immediate gravity move
            last_drop = std::chrono::steady_clock::now();
        }
    }
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
    cur_pos = {3,-2}; // spawn slightly above visible area (hidden rows)
    hold_used = false;
    can_hold = true;
    last_drop = std::chrono::steady_clock::now();
    // metrics: count this spawn as a piece placed/played
    pieces_placed++;
}

bool Game::collides(const Piece &p, Vec pos){
    for(auto &b:p.blocks){
        int x = pos.x + b.x;
        int y = pos.y + b.y;
        if(x<0||x>=COLS||y>=ROWS) return true;
        if(y>=0 && grid[y][x]) return true;
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
        auto now = std::chrono::steady_clock::now();

        // general combo timeout (ms)
        const int combo_timeout_ms = 2000;

        // handle singles, doubles and quads with centered board popup + combo subtext
        if(cnt == 1 || cnt == 2 || cnt >= 4){
            std::string label;
            if(cnt == 1) label = "Single";
            else if(cnt == 2) label = "Double";
            else label = (cnt == 4) ? "Quad" : (std::to_string(cnt) + " Lines");

            // combo chaining: increment if same clear-size within timeout, otherwise reset
            if(last_clear_count_size == cnt && std::chrono::duration_cast<std::chrono::milliseconds>(now - last_clear_time).count() <= combo_timeout_ms){
                clear_combo_count++;
            } else {
                clear_combo_count = 1;
            }
            last_clear_count_size = cnt;
            last_clear_time = now;

            std::string sub;
            if(clear_combo_count > 1){
                sub = std::string("x") + std::to_string(clear_combo_count) + " lines";
                // combo particle flair
                for(int r=0;r<8;r++){
                    Particle p;
                    p.x = (COLS/2) + ((rand()%200)/100.0f - 1.0f) * 3.0f;
                    p.y = ROWS*0.22f + ((rand()%200)/100.0f - 1.0f) * 2.0f;
                    p.vx = ((rand()%200)/100.0f - 1.0f) * 8.0f;
                    p.vy = -((rand()%200)/100.0f) * 6.0f;
                    p.size = 2.0f + (rand()%100)/100.0f * 2.0f;
                    p.max_life = 400 + rand()%300;
                    p.life = p.max_life;
                    p.streak = false;
                    p.col = SDL_Color{ (Uint8)std::min(255, current.color.r+30), (Uint8)std::min(255, current.color.g+30), (Uint8)std::min(255, current.color.b+30), 255 };
                    particles.push_back(p);
                }
            }

            // choose life: longer for combo/quads
            int life = (clear_combo_count > 1 || cnt >= 4) ? 1400 : 1000;
            spawn_board_popup(label, sub, life);

            // per-cell particle accent
            for(int r: rows_to_clear){
                for(int c=0;c<COLS;c++) spawn_particles_at(c,r, SDL_Color{255,255,255,255}, 2, true);
            }
        } else {
            // non-single/double/quad behavior: reset combo
            last_clear_count_size = 0;
            clear_combo_count = 0;

            // preserve special handling for triple or other types if desired
            if(cnt==3) spawn_board_popup("Triple", "", 900); // centered popup (no moving)
            else if(cnt>4) spawn_text_effect(std::to_string(cnt) + " Lines", SDL_Color{255,200,200,255}, 900, fx + field_w/2, fy + cleared[0]*int(CELL*scale), 4);
        }

    // scoring for clears
    int cleared_count = (int)cleared.size();
    // metrics: treat cleared lines as attacks sent to opponents
    total_attacks += cleared_count;
    lines_sent += cleared_count;
    if(cleared_count > spike_size) spike_size = cleared_count;
        int base = 0;
        // T-Spin scoring
        if(tspinType == TSpinType::Full){
            if(cleared_count==1) base = 800 * (level+1);
            else if(cleared_count==2) base = 1200 * (level+1);
            else if(cleared_count==3) base = 1600 * (level+1);
        } else {
            // normal line clear scoring
            base = score_for_lines(cleared_count, level);
        }

        // back-to-back eligible for Tetris (4) and Full T-Spins
        bool this_b2b = (cleared_count==4) || (tspinType==TSpinType::Full);
        if(this_b2b && back_to_back){ base = (int)std::round(base * 1.5); }
        back_to_back = this_b2b;

        // combo: increment and award 50 * combo_chain
        if(cleared_count > 0){ combo_chain++; base += 50 * combo_chain; }
        else combo_chain = 0;

    // soft/hard drop were already added immediately when performed

        score += base;

        // all clear detection
        bool all_empty = true;
        for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++) if(grid[r][c]) all_empty = false;
        if(all_empty) spawn_text_effect("ALL CLEAR.", SDL_Color{255,215,0,255}, 1200, fx + field_w/2, fy + field_h/2, 5);
    } else {
        // no clears, but if tspin or rotation happened close to lock, show spin text
        if(tspinType != TSpinType::None){
            // use centered popup display for spins so font/position/animation match clears
            if(tspinType == TSpinType::Full){
                spawn_board_popup("T-Spin", "", 1000);
            } else {
                spawn_board_popup("T-Spin Mini", "", 900);
            }
        } else if(last_was_rotate && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_rotate_time).count() < 400){
            // generic piece spin (heuristic): show piece-specific spin text centered via board popup
            const char* names[7] = {"I spin","J spin","L spin","O spin","S spin","T-Spin","Z spin"};
            int id = current.id;
            spawn_board_popup(names[id], "", 900);
        }
        // no clears: spawn next piece after ARE
        // award tspin text if applicable
        spawn_pending = true;
        spawn_time = std::chrono::steady_clock::now();
    }
    // check game over: any block above visible field (y < 0) after lock
    for(int c=0;c<COLS;c++){
        if(grid[0][c]){ running = false; break; }
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

// helper: draw a tetromino shape centered and scaled into a destination rect
// draw a tetromino shape centered and scaled into a destination rect
// `scale_mul` multiplies the computed per-cell size so callers can request a larger visual
// size for thumbnails (e.g., the hold preview) without changing the rect itself.
static void draw_piece_in_rect(SDL_Renderer* ren, const std::vector<Vec> &blocks, SDL_Color col, const SDL_Rect &rect, float scale_mul = 0.8f){
    if(!ren) return;
    int minx=999, maxx=-999, miny=999, maxy=-999;
    for(const auto &b: blocks){ minx = std::min(minx, b.x); maxx = std::max(maxx, b.x); miny = std::min(miny, b.y); maxy = std::max(maxy, b.y); }
    int w = std::max(1, maxx - minx + 1);
    int h = std::max(1, maxy - miny + 1);
    float cellw = rect.w / float(w);
    float cellh = rect.h / float(h);
    float cs = std::min(cellw, cellh) * scale_mul; // leave padding, adjustable by caller
    int totalw = int(cs * w);
    int totalh = int(cs * h);
    int originx = rect.x + (rect.w - totalw)/2;
    int originy = rect.y + (rect.h - totalh)/2;
    for(const auto &b: blocks){
        int bx = b.x - minx;
        int by = b.y - miny;
        SDL_Rect rc{ originx + int(bx * cs), originy + int(by * cs), std::max(2,int((int)cs - 2)), std::max(2,int((int)cs - 2)) };
        SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
        SDL_RenderFillRect(ren, &rc);
        SDL_SetRenderDrawColor(ren, 0,0,0,120);
        SDL_RenderDrawRect(ren, &rc);
    }
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
    const bool isI = (current.id == 0);

    // reset last-kick metadata
    last_kick_index = -1;
    last_kick_offset = {0,0};
    last_was_rotate = false;

    // try extended kicks (SRS + floor kicks)
    if(try_rotate_with_kicks(*this, nb, to, isI, clockwise)){
        return;
    }

    // rotation failed; nothing to do
}

void Game::hold(){
    if(!can_hold) return;
    // perform hold swap
    actions_count++; // count the hold action
    if(hold_piece.blocks.empty()){
        hold_piece.id = current.id;
        hold_piece.blocks = current.blocks;
        hold_piece.orientation = current.orientation;
        hold_piece.color = current.color;
        // spawn next piece immediately (ARE handled elsewhere)
        spawn_from_queue();
    } else {
        std::swap(hold_piece.id, current.id);
        std::swap(hold_piece.blocks, current.blocks);
        std::swap(hold_piece.color, current.color);
        std::swap(hold_piece.orientation, current.orientation);
        cur_pos = {3,0};
        if(collides(current, cur_pos)) running=false;
    }
    // disable hold until next spawn
    can_hold = false;
}

void Game::soft_drop(){ Vec np = cur_pos; np.y++; if(!collides(current, np)) cur_pos = np; else { lock_piece(); } }

void Game::hard_drop(){ Vec np = cur_pos; int dist=0; while(!collides(current, {np.x, np.y+1})){ np.y++; dist++; } cur_pos = np; // score hard drop
    actions_count++; // count the hard drop action
    score += dist * 2; lock_piece(); }

void Game::start_blitz(){
    blitz_active = true;
    blitz_start_time = std::chrono::steady_clock::now();
    // optional visual cue
    spawn_text_effect("BLITZ START", SDL_Color{255,200,50,255}, 1200, 400, 5);
}

void Game::tick(){
    if(paused) return;
    auto now = std::chrono::steady_clock::now();

    // If blitz active, check timer and end game when elapsed
    if(blitz_active){
        auto elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - blitz_start_time).count();
        if(elapsed_ms >= blitz_opts.duration_ms){
            // time up: end the round and return to main menu (for now, stop the game loop)
            spawn_text_effect("TIME UP", SDL_Color{255,80,80,255}, 1400, 400, 5);
            running = false;
            return;
        }
    }

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
            int cleared = (int)clear_progress.size();
            perform_clear_collapse();
            lines += cleared;
            level = 1 + lines/10;
            drop_ms = std::max(100, 800 - (level-1)*50);
        }
    }

    // gravity is handled in step(), keep tick() focused on visuals/particles

    // fade-in effect
    if(fade_in){
        auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        screen_fade = 1.0f - std::min(1.0f, since / 800.0f);
        if(screen_fade <= 0.0f){ fade_in = false; screen_fade = 0.0f; }
    }
}

void Game::draw_text(int x,int y,const std::string &text){
    if(!this->font) return;
    SDL_Surface* surf = TTF_RenderText_Blended(this->font, text.c_str(), SDL_Color{255,255,255,255});
    SDL_Texture* tx = SDL_CreateTextureFromSurface(ren,surf);
    SDL_Rect dst{ x, y, surf->w, surf->h };
    SDL_FreeSurface(surf);
    SDL_RenderCopy(ren, tx, nullptr, &dst);
    SDL_DestroyTexture(tx);
}

void Game::draw_colored_text(int x,int y,const std::string &text, SDL_Color color, float scale, int alpha){
    if(!this->font) return;
    SDL_Color c = color; c.a = (Uint8)alpha;
    SDL_Surface* surf = TTF_RenderText_Blended(this->font, text.c_str(), c);
    SDL_Texture* tx = SDL_CreateTextureFromSurface(ren,surf);
    SDL_SetTextureAlphaMod(tx, (Uint8)alpha);
    SDL_Rect dst{ x - surf->w/2, y - surf->h/2, (int)(surf->w*scale), (int)(surf->h*scale) };
    SDL_FreeSurface(surf);
    SDL_RenderCopyEx(ren, tx, nullptr, &dst, 0, nullptr, SDL_FLIP_NONE);
    SDL_DestroyTexture(tx);
}

// Draw left-aligned colored text where (x,y) specifies the top-left corner of the text.
void Game::draw_colored_text_left(int x,int y,const std::string &text, SDL_Color color, float scale, int alpha){
    if(!this->font) return;
    SDL_Color c = color; c.a = (Uint8)alpha;
    SDL_Surface* surf = TTF_RenderText_Blended(this->font, text.c_str(), c);
    SDL_Texture* tx = SDL_CreateTextureFromSurface(ren,surf);
    SDL_SetTextureAlphaMod(tx, (Uint8)alpha);
    SDL_Rect dst{ x, y, (int)(surf->w*scale), (int)(surf->h*scale) };
    SDL_FreeSurface(surf);
    SDL_RenderCopyEx(ren, tx, nullptr, &dst, 0, nullptr, SDL_FLIP_NONE);
    SDL_DestroyTexture(tx);
}

void Game::draw_colored_text_font(int x,int y,const std::string &text, SDL_Color color, float scale, int alpha, TTF_Font* usefont){
    TTF_Font* f = usefont ? usefont : this->font;
    if(!f) return;
    SDL_Color c = color; c.a = (Uint8)alpha;
    SDL_Surface* surf = TTF_RenderText_Blended(f, text.c_str(), c);
    if(!surf) return;
    SDL_Texture* tx = SDL_CreateTextureFromSurface(ren,surf);
    SDL_SetTextureAlphaMod(tx, (Uint8)alpha);
    SDL_Rect dst{ x - surf->w/2, y - surf->h/2, (int)(surf->w*scale), (int)(surf->h*scale) };
    SDL_FreeSurface(surf);
    SDL_RenderCopyEx(ren, tx, nullptr, &dst, 0, nullptr, SDL_FLIP_NONE);
    SDL_DestroyTexture(tx);
}

// Minimal renderer so RunGameSDL can call g.render(). Replace with your full rendering later.
void Game::render(){
    if(!ren) return;

    // renderer size
    int winw = 800, winh = 720;
    SDL_GetRendererOutputSize(ren, &winw, &winh);

    // background: render wallpaper with ~20% black tint if available, otherwise clear to dark gray
    if(this->wallpaper){
        renderWallpaperWithTint(ren, this->wallpaper, winw, winh, 51); // 20% black
    } else {
        SDL_SetRenderDrawColor(ren, 12,12,12,255);
        SDL_RenderClear(ren);
    }

    // layout constants (moderate side panels)
    const int side_w = 110;             // slightly smaller mid-size width for left/right panels
    const int gutter = 8;
    const int ui_margin = 8;

    // compute playfield scale to fit between side panels and vertically
    int available_w = winw - (side_w * 2) - (gutter * 2);
    int available_h = winh - (ui_margin*2);
    float scale = std::min(1.0f, std::min(available_w / float(COLS * CELL), available_h / float(ROWS * CELL)));
    int field_w = int(COLS * CELL * scale);
    int field_h = int(ROWS * CELL * scale);

    // positions
    int fx = (winw - field_w) / 2;
    int fy = (winh - field_h) / 2;

    int left_x = fx - side_w - gutter;
    int right_x = fx + field_w + gutter;

    // --- Left/Right side panels adjacent to the board ---
    // Panel sizing: make panels taller than the playfield so previews never overlap
    int panel_w = side_w;
    int header_h = 28; // moderate header
    int panel_extra = 40; // make panels taller so preview areas have more room
    int panel_top = std::max(ui_margin, fy - panel_extra/2);
    int panel_h = field_h + panel_extra;

    // Compact header (matches the small ribbon in the reference image)
    int hdr_w = 96; // compact header width to match reference
    int hdr_x = fx - hdr_w - gutter;
    int hdr_y = panel_top + 8;
    SDL_Rect left_header{ hdr_x, hdr_y, hdr_w, header_h };
    // small black backdrop just under the header for the stacked previews
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0,0,0, (Uint8)(255 * 0.8f));
    // compute a compact preview area height (max 4 items)
    // show fewer previews so each can be larger without increasing panel height
    int max_preview = 3;
    int count = std::min<int>((int)next_queue.size(), max_preview);
    // increase thumbnail scale to make pieces look bigger (1.6x cell), but cap it
    int base_thumb = int(CELL * scale);
    int thumb_h = std::min(int(base_thumb * 1.6f), std::max(32, field_h / 5));
    int preview_area_h = thumb_h * count + 20;
    // allow preview area up to 40% of the field height so panels can be taller but not huge
    preview_area_h = std::min(preview_area_h, (field_h * 2) / 5);
    SDL_Rect left_preview_area{ hdr_x, left_header.y + header_h + 6, hdr_w, preview_area_h };
    SDL_RenderFillRect(ren, &left_preview_area);
    // white header ribbon inside left area
    SDL_SetRenderDrawColor(ren, 255,255,255,255);
    SDL_RenderFillRect(ren, &left_header);
    draw_colored_text_left(left_header.x + 8, left_header.y + 6, "Next up", SDL_Color{20,20,20,255}, 1.0f, 255);

    // stacked next-piece previews (no individual boxes) — thumbnails sized to CELL*scale
    if(count > 0){
        // make thumbnails slightly wider to emphasize size without increasing panel height
        int item_w = left_preview_area.w - 6;
        int stack_y = left_preview_area.y + 6;
        for(int i=0;i<count;i++){
            int pid = next_queue[i];
            SDL_Rect itemRect{ left_preview_area.x + 3, stack_y + i * thumb_h, item_w, thumb_h };
            draw_piece_in_rect(ren, TETROS[pid], T_COLORS[pid], itemRect);
        }
    }

    // Compact right-side header + preview to match reference
    // Make the Hold column wider so the held piece can be displayed larger
    int rhdr_w = 140;
    int rhdr_x = fx + field_w + gutter;
    int rhdr_y = panel_top + 8;
    SDL_Rect right_header{ rhdr_x, rhdr_y, rhdr_w, header_h };
    // preview area right under the header (single slot) - make it taller and allow bigger thumbnail
    int thumb_h_right = std::min(int(base_thumb * 2.0f), std::max(48, field_h / 3)); // larger thumbnail for hold
    SDL_Rect right_preview_area{ rhdr_x, right_header.y + header_h + 6, rhdr_w, thumb_h_right + 28 };
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0,0,0, (Uint8)(255 * 0.8f));
    SDL_RenderFillRect(ren, &right_preview_area);
    // draw white ribbon
    SDL_SetRenderDrawColor(ren, 255,255,255,255);
    SDL_RenderFillRect(ren, &right_header);
    draw_colored_text_left(right_header.x + 8, right_header.y + 6, "Hold", SDL_Color{20,20,20,255}, 1.0f, 255);

    // single hold thumbnail: use most of the preview area's width so the piece appears larger
    int hold_preview_size = std::min(right_preview_area.w - 8, thumb_h_right + 4);
    int hold_x = right_preview_area.x + (right_preview_area.w - hold_preview_size) / 2;
    int hold_y = right_preview_area.y + 6;
    SDL_Rect hold_preview{ hold_x, hold_y, hold_preview_size, hold_preview_size };
    // Draw held piece directly without a separate box/backdrop so it visually matches the "Next" column
    if(!hold_piece.blocks.empty()){
        // Draw held piece slightly larger than the thumbnail rect so it reads as "bigger"
        draw_piece_in_rect(ren, hold_piece.blocks, hold_piece.color, hold_preview, 1.35f);
    }

    // --- Playfield border & background ---
    SDL_Rect board_bg{ fx - 4, fy - 4, field_w + 8, field_h + 8 };
    SDL_SetRenderDrawColor(ren, 20,20,20,255);
    SDL_RenderFillRect(ren, &board_bg);
    SDL_SetRenderDrawColor(ren, 255,255,255,255);
    SDL_Rect board_border{ fx - 2, fy - 2, field_w + 4, field_h + 4 };
    SDL_RenderDrawRect(ren, &board_border);

    // inner grid background
    SDL_Rect inner{ fx, fy, field_w, field_h };
    SDL_SetRenderDrawColor(ren, 8,8,8,255);
    SDL_RenderFillRect(ren, &inner);

    // grid lines
    SDL_SetRenderDrawColor(ren, 48,48,48,220); // thinner, modern grid
     for(int c=1;c<COLS;c++){
         int x = fx + int(c * CELL * scale);
         SDL_RenderDrawLine(ren, x, fy, x, fy + field_h);
     }
     for(int r=1;r<ROWS;r++){
         int y = fy + int(r * CELL * scale);
         SDL_RenderDrawLine(ren, fx, y, fx + field_w, y);
    }

    // draw placed blocks
    for(int r=0;r<ROWS;r++){
        for(int c=0;c<COLS;c++){
            if(grid[r][c]){
                SDL_Color col = T_COLORS[(grid[r][c]-1) % 7];
                SDL_Rect rc{ fx + int(c * CELL * scale) + 2, fy + int(r * CELL * scale) + 2, int(CELL * scale) - 4, int(CELL * scale) - 4 };
                SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
                SDL_RenderFillRect(ren, &rc);
                // simple bevel
                SDL_SetRenderDrawColor(ren, 0,0,0,80);
                SDL_RenderDrawRect(ren, &rc);
            }
        }
    }

    // draw ghost / current piece
    // simple shadow: drop until collision
    Piece ghost = current;
    Vec gpos = cur_pos;
    while(!collides(ghost, {gpos.x, gpos.y+1})) gpos.y++;
    for(auto &b: ghost.blocks){
        int gx = gpos.x + b.x, gy = gpos.y + b.y;
        if(gy >= 0){
            SDL_Rect rc{ fx + int(gx * CELL * scale) + 2, fy + int(gy * CELL * scale) + 2, int(CELL * scale) - 4, int(CELL * scale) - 4 };
            SDL_SetRenderDrawColor(ren, 40,40,40,140);
            SDL_RenderFillRect(ren, &rc);
        }
    }

    // draw current piece
    for(auto &b : current.blocks){
        int cx = cur_pos.x + b.x, cy = cur_pos.y + b.y;
        if(cy >= -2){
            SDL_Rect rc{ fx + int(cx * CELL * scale) + 2, fy + int(cy * CELL * scale) + 2, int(CELL * scale) - 4, int(CELL * scale) - 4 };
            SDL_SetRenderDrawColor(ren, current.color.r, current.color.g, current.color.b, 255);
            SDL_RenderFillRect(ren, &rc);
            SDL_SetRenderDrawColor(ren, 0,0,0,120);
            SDL_RenderDrawRect(ren, &rc);
        }
    }

    // particles (simple)
    for(auto &p : particles){
        int px = fx + int(p.x * CELL * scale);
        int py = fy + int(p.y * CELL * scale);
        SDL_Rect rc{ px-2, py-2, int(p.size), int(p.size) };
        SDL_SetRenderDrawColor(ren, p.col.r, p.col.g, p.col.b, 220);
        SDL_RenderFillRect(ren, &rc);
    }

    // board popup (fixed just above the playfield) - animate scale/alpha only
    if(board_popup.active){
        auto nowt = std::chrono::steady_clock::now();
        int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(nowt - board_popup.start).count();
        if(elapsed < board_popup.life_ms){
            float t = elapsed / (float)board_popup.life_ms;
            // scale animation: start slightly larger and ease to 1.0
            float pop = 1.0f + 0.6f * (1.0f - t);
            int alpha = (int)(255 * (1.0f - t));
            int px = fx + field_w/2;
            // place popup just above the board border (fixed position)
            int py = fy - 12; // 12 px above the playfield
            // draw main label (centered) with scale animation, no vertical movement
            draw_colored_text_font(px, py, board_popup.main, SDL_Color{250,250,250,255}, pop*1.6f, alpha, popup_font);
            if(!board_popup.sub.empty()){
                // draw subtext slightly below the main label (fixed offset)
                draw_colored_text_font(px, py + 40, board_popup.sub, SDL_Color{200,200,220,255}, pop*0.9f, alpha, popup_font);
            }
        } else {
            board_popup.active = false;
        }
    }

    // OUT OF FOCUS overlay (if window not focused)
    if(win){
        Uint32 flags = SDL_GetWindowFlags(win);
        if(!(flags & SDL_WINDOW_INPUT_FOCUS)){
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0,0,0,180);
            SDL_Rect ov{ fx, fy + field_h/3, field_w, field_h/3 };
            SDL_RenderFillRect(ren, &ov);
            // red framed text
            SDL_SetRenderDrawColor(ren, 180,20,20,255);
            SDL_Rect frame{ fx + 8, fy + field_h/3 + 8, field_w - 16, field_h/3 - 16 };
            SDL_RenderDrawRect(ren, &frame);
            draw_colored_text_font(fx + field_w/2, fy + field_h/2 - 10, "OUT OF FOCUS", SDL_Color{220,40,40,255}, 1.6f, 255, popup_font);
            draw_colored_text_font(fx + field_w/2, fy + field_h/2 + 28, "CLICK TO RETURN TO THE GAME.", SDL_Color{240,240,240,255}, 0.9f, 220, popup_font);
        }
    }

    // Draw active stats below the playfield (centered)
    if(ren){
        int sx = fx + field_w/2;
        int sy = fy + field_h + 12; // just below board
    // background pill (size to fit the text so nothing overflows)
    SDL_SetRenderDrawColor(ren, 28,28,30,220);
    // measure text widths to compute an appropriate background width
    int fh = font ? TTF_FontHeight(font) : 16;
    auto text_w = [&](const std::string &s, float scale){ int tw=0, th=0; if(font) TTF_SizeText(font, s.c_str(), &tw, &th); return (int)(tw * scale); };
    // prepare the four strings used in the stats area
    char timestr_local[32];
    auto nowt2 = std::chrono::steady_clock::now();
    int elapsed_ms2 = (int)std::chrono::duration_cast<std::chrono::milliseconds>(nowt2 - start_time).count();
    int sec2 = (elapsed_ms2/1000)%60;
    int min2 = (elapsed_ms2/60000);
    snprintf(timestr_local, sizeof(timestr_local), "%02d:%02d.%03d", min2, sec2, elapsed_ms2%1000);
    std::string left_label1 = "LEVEL";
    std::string left_val1 = std::to_string(level);
    std::string right_label1 = "LINES";
    std::string right_val1 = std::to_string(lines) + "/150";
    std::string left_label2 = "TIME";
    std::string left_val2 = std::string(timestr_local);
    std::string right_label2 = "SCORE";
    std::string right_val2 = std::to_string(score);
    float label_scale = 0.9f; float val_scale1 = 1.2f; float val_scale2 = 1.0f;
    int left_col_w = std::max(text_w(left_label1, label_scale), text_w(left_val1, val_scale1));
    left_col_w = std::max(left_col_w, text_w(left_label2, label_scale));
    left_col_w = std::max(left_col_w, text_w(left_val2, val_scale2));
    int right_col_w = std::max(text_w(right_label1, label_scale), text_w(right_val1, val_scale2));
    right_col_w = std::max(right_col_w, text_w(right_label2, label_scale));
    right_col_w = std::max(right_col_w, text_w(right_val2, val_scale2));
    int inner_spacing = 24;
    int padding = 12;
    int needed_w = left_col_w + right_col_w + inner_spacing + padding*2;
    int bg_w = std::max(field_w - 16, needed_w);
    int bg_x = sx - bg_w/2;
    int label_h = (int)(fh * label_scale);
    int val_h = (int)(fh * std::max(val_scale1, val_scale2));
    int line_spacing = 8;
    int bg_h = padding*2 + (label_h + val_h + line_spacing);
    bg_h = bg_h * 2 / 1; // allow two rows (approx)
    SDL_Rect stats_bg{ bg_x, sy - 6, bg_w, bg_h };
    SDL_RenderFillRect(ren, &stats_bg);
    SDL_SetRenderDrawColor(ren, 80,80,80,180);
    SDL_RenderDrawRect(ren, &stats_bg);

    // draw stats: LEVEL LINES TIME SCORE in two columns using left-aligned text so nothing overflows
    int colx = stats_bg.x + padding;
    int coly = stats_bg.y + 8;
    int right_col_x = colx + left_col_w + inner_spacing;

    // first row
    draw_colored_text_left(colx, coly, "LEVEL", SDL_Color{180,180,180,255}, 0.9f, 255);
    draw_colored_text_left(colx, coly + 20, std::to_string(level), SDL_Color{255,255,255,255}, 1.2f, 255);
    draw_colored_text_left(right_col_x, coly, "LINES", SDL_Color{180,180,180,255}, 0.9f, 255);
    draw_colored_text_left(right_col_x, coly + 20, std::to_string(lines) + "/150", SDL_Color{255,255,255,255}, 1.0f, 255);

    // time and score on second row
    auto nowt = std::chrono::steady_clock::now();
    int elapsed_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(nowt - start_time).count();
    int sec = (elapsed_ms/1000)%60;
    int min = (elapsed_ms/60000);
    char timestr[32];
    snprintf(timestr, sizeof(timestr), "%02d:%02d.%03d", min, sec, elapsed_ms%1000);
    draw_colored_text_left(colx, coly + 44, "TIME", SDL_Color{180,180,180,255}, 0.9f, 255);
    draw_colored_text_left(colx, coly + 64, timestr, SDL_Color{255,255,255,255}, 1.0f, 255);
    draw_colored_text_left(right_col_x, coly + 44, "SCORE", SDL_Color{180,180,180,255}, 0.9f, 255);
    draw_colored_text_left(right_col_x, coly + 64, std::to_string(score), SDL_Color{255,255,255,255}, 1.0f, 255);

        // (bottom-left metrics panel removed - only centered stats remain)
    }
    // Present the composed frame after all UI is drawn
    SDL_RenderPresent(ren);
}

// Minimal RunGameSDL so menu can link and run the game loop.
// This is a simple loop; replace with your full implementation if needed.
int RunGameSDL(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font){
    if(!window || !renderer) return -1;
    Game g;
    g.win = window;
    g.ren = renderer;

    // load main in-game font (subtext.ttf) for crisp UI labels
    TTF_Font* mainf = TTF_OpenFont("src/assets/subtext.ttf", 18);
    if(mainf) g.font = mainf;
    else g.font = font; // fallback to passed font if subtext missing

    // large display font for popups
    TTF_Font* df = TTF_OpenFont("src/assets/display.otf", 72);
    if(df) g.popup_font = df;

    // Attempt to fetch a wallpaper (same behavior as the main menu). If fetch fails, use a small tile fallback.
    {
        int iw = 800, ih = 600;
        SDL_GetRendererOutputSize(renderer, &iw, &ih);
        SDL_Texture* bgtex = fetchUnsplashWallpaper(renderer, iw, ih);
        SDL_Texture* tiletex = nullptr;
        if(bgtex){
            std::cerr << "RunGameSDL: fetched wallpaper for gameplay\n";
            g.wallpaper = bgtex;
        } else {
            std::cerr << "RunGameSDL: failed to fetch wallpaper, using tile fallback\n";
            SDL_Surface* tile_surf = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32);
            if(tile_surf){
                SDL_FillRect(tile_surf, nullptr, SDL_MapRGBA(tile_surf->format, 245,245,220,255));
                tiletex = SDL_CreateTextureFromSurface(renderer, tile_surf);
                SDL_FreeSurface(tile_surf);
            }
            g.wallpaper = tiletex;
        }
    }

    // basic loop: build InputState, call step(), tick(), render()
    SDL_Event ev;
    Game::InputState in;
    // edge-detection state
    bool prev_hold = false;
    bool prev_hard = false;
    bool prev_rot_cw = false;
    bool prev_rot_ccw = false;
    bool prev_left = false;
    bool prev_right = false;
    const int target_ms = 16; // ~60fps
    while(g.running){
        // process all events first so keyboard state is up-to-date
        while(SDL_PollEvent(&ev)){
            if(ev.type == SDL_QUIT) { g.running = false; break; }
        }

        // refresh keyboard state after pumping events
        SDL_PumpEvents();
        const Uint8* state = SDL_GetKeyboardState(NULL);
        in.left = state[SDL_SCANCODE_LEFT];
        in.right = state[SDL_SCANCODE_RIGHT];
        in.soft = state[SDL_SCANCODE_DOWN];
        bool hard_pressed = state[SDL_SCANCODE_SPACE];
        bool hold_pressed = state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT];
        bool rot_ccw = state[SDL_SCANCODE_Z];
        bool rot_cw = state[SDL_SCANCODE_X] || state[SDL_SCANCODE_UP];

    // edge flags
        in.hard = hard_pressed && !prev_hard;
        in.hold = hold_pressed && !prev_hold;
        in.hold_pressed = hold_pressed;
        in.rotate_cw = rot_cw && !prev_rot_cw;
        in.rotate_ccw = rot_ccw && !prev_rot_ccw;
    in.left_edge = state[SDL_SCANCODE_LEFT] && !prev_left;
    in.right_edge = state[SDL_SCANCODE_RIGHT] && !prev_right;

        // update prevs
        prev_hold = hold_pressed;
        prev_hard = hard_pressed;
        prev_rot_cw = rot_cw;
        prev_rot_ccw = rot_ccw;
    prev_left = state[SDL_SCANCODE_LEFT];
    prev_right = state[SDL_SCANCODE_RIGHT];

        // handle edge hard drop/hold directly
        if(in.hold) g.hold();
        if(in.hard) g.hard_drop();

        // run a fixed-step update to make input timing consistent
        g.step(in);
        g.tick();
        g.render();

        SDL_Delay(target_ms);
    }

    if(g.popup_font) { TTF_CloseFont(g.popup_font); g.popup_font = nullptr; }
    if(g.font && g.font != font){ TTF_CloseFont(g.font); g.font = nullptr; }
    if(g.wallpaper){ SDL_DestroyTexture(g.wallpaper); g.wallpaper = nullptr; }
    return 0;
}

// Minimal detect_tspin fallback (if full logic exists elsewhere, remove this)
TSpinType detect_tspin(Game &g, const Piece &p, Vec pos){
    // Only relevant for T piece (id==2 in this repo? ensure mapping)
    // Our TETROS order: 0:I,1:O,2:T,3:S,4:Z,5:J,6:L  (note: in game.cpp earlier T was index 2)
    // But earlier code checks current.id == 5 for 'T' — however the canonical T index here is 2. Use shape detection instead.
    // If piece shape has a center with three corners occupied, treat as T-Spin Full. If two corners and last_kick_index>0, treat as Mini.
    // Find center (approx): compute bounding box center
    if(p.blocks.size() != 4) return TSpinType::None;
    // detect T-shape by checking one block with three neighbors
    // We'll compute the center at pos + {1,1} (matches rotate_piece earlier)
    int cx = pos.x + 1;
    int cy = pos.y + 1;
    int corner_count = 0;
    std::array<std::pair<int,int>,4> corners = {{{cx-1,cy-1},{cx+1,cy-1},{cx-1,cy+1},{cx+1,cy+1}}};
    // create occupancy map considering grid and piece own cells
    bool ownCell[ROWS][COLS] = {false};
    for(const auto &b: p.blocks){ int ox = pos.x + b.x; int oy = pos.y + b.y; if(ox>=0 && ox<COLS && oy>=0 && oy<ROWS) ownCell[oy][ox]=true; }
    for(auto &cr: corners){ int rx = cr.first, ry = cr.second; if(rx<0||rx>=COLS||ry<0||ry>=ROWS) corner_count++; else if(g.grid[ry][rx] && !ownCell[ry][rx]) corner_count++; }
    if(corner_count >= 3) return TSpinType::Full;
    if(corner_count == 2 && g.last_kick_index > 0) return TSpinType::Mini;
    return TSpinType::None;
}

// canonical 6-arg spawn_text_effect implementation (pushes into effects)
void Game::spawn_text_effect(const std::string &text, SDL_Color col, int life_ms, int x, int y, int type){
    TextEffect ef;
    ef.text = text;
    ef.color = col;
    ef.life_ms = life_ms;
    ef.start = std::chrono::steady_clock::now();
    ef.type = type;
    ef.x = x;
    ef.y = y;
    effects.push_back(std::move(ef));
}

// board-centered popup used for clears/spins (uses board_popup member)
void Game::spawn_board_popup(const std::string &main, const std::string &sub, int life_ms){
    // Expect board_popup to have members: main, sub, life_ms, start, active
    // this matches earlier usages in render/effect code
    board_popup.main = main;
    board_popup.sub = sub;
    board_popup.life_ms = life_ms;
    board_popup.start = std::chrono::steady_clock::now();
    board_popup.active = true;
}