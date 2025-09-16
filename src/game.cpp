#include "game.h"

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
    SDL_Color{0,255,255,255}, SDL_Color{0,0,255,255}, SDL_Color{255,165,0,255},
    SDL_Color{255,255,0,255}, SDL_Color{0,255,0,255}, SDL_Color{128,0,128,255}, SDL_Color{255,0,0,255}
};

Game::Game(){
    rng.seed((unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    refill_bag();
    for (int i=0;i<5;i++) next_queue.push_back(next_from_bag());
    spawn_from_queue();
    last_horiz_move = std::chrono::steady_clock::now();
    last_soft_move = std::chrono::steady_clock::now();
    start_time = std::chrono::steady_clock::now();
}

void Game::refill_bag(){
    bag.clear();
    for(int i=0;i<7;i++) bag.push_back(i);
    std::shuffle(bag.begin(), bag.end(), rng);
    bag_index=0;
}

int Game::next_from_bag(){
    if(bag_index >= (int)bag.size()) refill_bag();
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
    // compute playfield origin to place text effects
    int winw=800, winh=720;
    int field_w = COLS*CELL;
    int field_h = ROWS*CELL;
    int fx = (winw-field_w)/2;
    int fy = (winh-field_h)/2;

    // detect clears and spins/effects
    auto cleared = detect_full_rows();
    bool tspin = false;
    if(current.id == 5 && last_was_rotate){
        tspin = detect_tspin(*this, current, cur_pos);
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
    if(cnt==1) spawn_text_effect("Single", SDL_Color{255,255,255,255}, 900, fx + field_w/2, fy + cleared[0]*CELL, 1);
    else if(cnt==2) spawn_text_effect("Double", SDL_Color{200,255,200,255}, 900, fx + field_w/2, fy + cleared[0]*CELL, 2);
    else if(cnt==3) spawn_text_effect("Triple", SDL_Color{200,200,255,255}, 900, fx + field_w/2, fy + cleared[0]*CELL, 3);
    else if(cnt>=4) spawn_text_effect("Quad", SDL_Color{255,200,200,255}, 900, fx + field_w/2, fy + cleared[0]*CELL, 4);

        // all clear detection
        bool all_empty = true;
        for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++) if(grid[r][c]) all_empty = false;
    if(all_empty) spawn_text_effect("ALL CLEAR.", SDL_Color{255,215,0,255}, 1200, fx + field_w/2, fy + field_h/2, 5);
    } else {
        // no clears, but if tspin or rotation happened close to lock, show spin text
        if(tspin){
            spawn_text_effect("T-Spin", SDL_Color{255,100,255,255}, 1000, fx + cur_pos.x*CELL + CELL, fy + cur_pos.y*CELL, 10);
        } else if(last_was_rotate && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_rotate_time).count() < 400){
            // generic piece spin (heuristic): show piece-specific spin text
            const char* names[7] = {"I spin","J spin","L spin","O spin","S spin","T-Spin","Z spin"};
            int id = current.id;
            std::string label = names[id];
            SDL_Color col = current.color;
            spawn_text_effect(label, col, 900, fx + cur_pos.x*CELL + CELL, fy + cur_pos.y*CELL, 11+id);
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
    // SRS kick tables (offsets) for J,L,S,Z,T (common) and I separately
    const std::vector<std::vector<Vec>> kicks_common = {
        {{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}}, // 0->1
        {{0,0},{1,0},{1,-1},{0,2},{1,2}},     // 1->2
        {{0,0},{1,0},{1,1},{0,-2},{1,-2}},    // 2->3
        {{0,0},{-1,0},{-1,-1},{0,2},{-1,2}}    // 3->0
    };
    const std::vector<std::vector<Vec>> kicks_i = {
        {{0,0},{-2,0},{1,0},{-2,-1},{1,2}},
        {{0,0},{-1,0},{2,0},{-1,2},{2,-1}},
        {{0,0},{2,0},{-1,0},{2,1},{-1,-2}},
        {{0,0},{1,0},{-2,0},{1,-2},{-2,1}}
    };

    int from = current.orientation % 4;
    int to = (from + (clockwise?1:3)) % 4;

    std::vector<Vec> nb = rotate_blocks(current.blocks, clockwise);

    const bool isI = (current.id == 0);
    const auto &kicks = isI ? kicks_i : kicks_common;

    // try kick offsets
    for(const Vec &k : kicks[from]){
        Vec np = cur_pos; np.x += k.x; np.y -= k.y; // note: y sign adjustments due to coord system
        Piece p2 = current; p2.blocks = nb;
        if(!collides(p2, np)){
            current.blocks = nb;
            current.orientation = to;
            last_was_rotate = true;
            last_rotate_time = std::chrono::steady_clock::now();
            cur_pos = np;
            return;
        }
    }
    // if all kicks fail, no rotation
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
    SDL_SetRenderDrawColor(ren, 0,0,0,255);
    SDL_RenderClear(ren);

    int winw=800, winh=720;
    int field_w = COLS*CELL;
    int field_h = ROWS*CELL;
    int fx = (winw-field_w)/2;
    int fy = (winh-field_h)/2;

    SDL_Rect fieldRect{fx, fy, field_w, field_h};
    SDL_SetRenderDrawColor(ren, 40,40,40,255);
    SDL_RenderFillRect(ren, &fieldRect);

    // draw grid
    for(int r=0;r<ROWS;r++){
        for(int c=0;c<COLS;c++){
            SDL_Rect cell{fx + c*CELL, fy + r*CELL, CELL-1, CELL-1};
            int val = grid[r][c];
            if(val){
                SDL_Color col = T_COLORS[val-1];
                SDL_SetRenderDrawColor(ren, std::max(0, col.r-50), std::max(0, col.g-50), std::max(0, col.b-50), 255);
                SDL_RenderFillRect(ren, &cell);
                SDL_Rect inner{cell.x+2, cell.y+2, cell.w-4, cell.h-4};
                SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
                SDL_RenderFillRect(ren, &inner);
            } else {
                SDL_SetRenderDrawColor(ren, 20,20,20,255);
                SDL_RenderFillRect(ren, &cell);
            }
        }
    }

    // ghost
    {
        Vec gp = cur_pos;
        while(!collides(current, {gp.x, gp.y+1})) gp.y++;
        for(auto &b: current.blocks){
            int x = fx + (gp.x + b.x) * CELL;
            int y = fy + (gp.y + b.y) * CELL;
            SDL_Rect rc{x,y,CELL-1,CELL-1};
            SDL_SetRenderDrawColor(ren, current.color.r/3, current.color.g/3, current.color.b/3,120);
            SDL_RenderFillRect(ren, &rc);
        }
    }

    // draw current grid with possible per-cell clearing visuals
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
                int px = fx + c*CELL;
                int py = fy + r*CELL;
                // drop shadow
                SDL_SetRenderDrawColor(ren, 0,0,0,90);
                SDL_Rect sh{px+4, py+4, CELL-1, CELL-1};
                SDL_RenderFillRect(ren, &sh);

                if(isClearing){
                    // during stomp compress vertically
                    float scale = 1.0f - prog * 0.8f; // compress to 20%
                    int h = std::max(1, int((CELL-1) * scale));
                    int oy = ((CELL-1) - h)/2;
                    SDL_Rect cellRect{px, py + oy, CELL-1, h};
                    SDL_SetRenderDrawColor(ren, std::max(0, col.r-60), std::max(0, col.g-60), std::max(0, col.b-60), 255);
                    SDL_RenderFillRect(ren, &cellRect);
                    SDL_Rect inner{cellRect.x+3, cellRect.y+3, std::max(0, cellRect.w-6), std::max(0, cellRect.h-6)};
                    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
                    SDL_RenderFillRect(ren, &inner);
                } else {
                    SDL_Rect cellRect{px, py, CELL-1, CELL-1};
                    SDL_SetRenderDrawColor(ren, std::max(0, col.r-60), std::max(0, col.g-60), std::max(0, col.b-60), 255);
                    SDL_RenderFillRect(ren, &cellRect);
                    SDL_Rect inner{cellRect.x+3, cellRect.y+3, std::max(0, cellRect.w-6), std::max(0, cellRect.h-6)};
                    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
                    SDL_RenderFillRect(ren, &inner);
                }
            }
        }
    }

    // current piece
    for(auto &b: current.blocks){
        int x = fx + (cur_pos.x + b.x) * CELL;
        int y = fy + (cur_pos.y + b.y) * CELL;
        SDL_Rect rc{x,y,CELL-1,CELL-1};
        SDL_SetRenderDrawColor(ren, current.color.r, current.color.g, current.color.b,255);
        SDL_RenderFillRect(ren, &rc);
    }

    // particles
    for(auto &p: particles){
        int px = int(fx + (p.x * CELL));
        int py = int(fy + (p.y * CELL));
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

    // Next panel
    SDL_Rect nextPanel{fx + field_w + 12, fy, 160, 220};
    SDL_SetRenderDrawColor(ren, 30,30,30,220);
    SDL_RenderFillRect(ren, &nextPanel);
    draw_text(nextPanel.x + 12, nextPanel.y + 8, "Next");
    for(size_t i=0;i<next_queue.size();i++){
        int id = next_queue[i];
        int bx = nextPanel.x + 12;
        int by = nextPanel.y + 32 + i*38;
        for(auto &bb: TETROS[id]){
            SDL_Rect rc{ bx + bb.x*CELL/2, by + bb.y*CELL/2, CELL/2 -1, CELL/2 -1 };
            SDL_Color col = T_COLORS[id];
            SDL_SetRenderDrawColor(ren, col.r, col.g, col.b,255);
            SDL_RenderFillRect(ren, &rc);
        }
    }

    // Hold
    SDL_Rect holdPanel{fx - 176, fy, 160, 120};
    SDL_SetRenderDrawColor(ren, 30,30,30,220);
    SDL_RenderFillRect(ren, &holdPanel);
    draw_text(holdPanel.x + 12, holdPanel.y + 8, "Hold");
    if(!hold_piece.blocks.empty()){
        int bx = holdPanel.x + 12;
        int by = holdPanel.y + 32;
        for(auto &bb: hold_piece.blocks){
            SDL_Rect rc{ bx + bb.x*CELL/2, by + bb.y*CELL/2, CELL/2 -1, CELL/2 -1 };
            SDL_Color col = hold_piece.color;
            SDL_SetRenderDrawColor(ren, col.r, col.g, col.b,255);
            SDL_RenderFillRect(ren, &rc);
        }
    }

    // Stats
    SDL_Rect statsPanel{fx - 176, fy + field_h - 160, 160, 160};
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

    // draw stomp highlight for cleared rows (short visual)
    if(clearing && !rows_to_clear.empty()){
        for(size_t i=0;i<rows_to_clear.size();++i){
            int r = rows_to_clear[i];
            float progress = clear_progress.size() > i ? clear_progress[i] : 0.0f;
            float cf = 1.0f - progress * 0.8f; // compress factor
            int panel_h = std::max(1, int(CELL * cf));
            int yoff = (CELL - panel_h) / 2;
            SDL_SetRenderDrawColor(ren, 220,220,220, 200);
            SDL_Rect stompRect{fx, fy + r*CELL + yoff, field_w, panel_h};
            SDL_RenderFillRect(ren, &stompRect);
        }
    }

    // render text effects (unique per type)
    for(auto it = effects.begin(); it != effects.end();){
        auto now = std::chrono::steady_clock::now();
        int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - it->start).count();
        if(elapsed >= it->life_ms){ it = effects.erase(it); continue; }
        float t = elapsed / (float)it->life_ms; // 0..1

        // effect variants by type
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
                // draw subtle darkened backdrop to keep readability (no red)
                SDL_SetRenderDrawColor(ren, 10,10,10, (Uint8)(160 * (1.0f - t)));
                SDL_Rect backdrop{it->x - 140, it->y - 34, 280, 68};
                SDL_RenderFillRect(ren, &backdrop);

                // larger, centered label (force uppercase QUAD)
                std::string label = it->text;
                for(auto &ch: label) ch = toupper(ch);
                draw_colored_text(it->x, it->y, label, SDL_Color{240,240,240,255}, 1.6f - 0.5f * t, (int)(255 * (1.0f - t)) );

                // spawn a ring of small particles around the label for the first third of the lifetime
                if(t < 0.35f){
                    int ringCount = 18;
                    float ringT = t / 0.35f; // 0..1
                    for(int pi=0; pi<ringCount; ++pi){
                        float ang = (pi / (float)ringCount) * 6.2831853f;
                        float radius = 40.0f * (0.6f + 0.8f * ringT);
                        Particle p;
                        p.x = (it->x) / (float)CELL + std::cos(ang) * (radius / CELL);
                        p.y = (it->y) / (float)CELL + std::sin(ang) * (radius / CELL);
                        float speed = 40.0f * (0.6f + 0.6f * (1.0f - ringT)) / (float)CELL;
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
                // gold shimmer + scale
                draw_colored_text(it->x, it->y, it->text, it->color, 1.6f - 0.4f * t, 255);
                break;
            case 10: // T-Spin
                draw_colored_text(it->x, it->y - int(20 * (1.0f - t)), it->text, it->color, 1.3f, 255);
                // swirling small particles
                for(int i=0;i<4;i++){
                    int sx = it->x + (int)(std::sin((t + i*0.25f) * 6.28f) * 30);
                    int sy = it->y + (int)(std::cos((t + i*0.25f) * 6.28f) * 8);
                    SDL_SetRenderDrawColor(ren, it->color.r, it->color.g, it->color.b, (Uint8)(200 * (1.0f - t)) );
                    SDL_Rect pr{sx-2, sy-2, 4,4}; SDL_RenderFillRect(ren, &pr);
                }
                break;
            default: // piece spins and others
                draw_colored_text(it->x, it->y - int(20 * t), it->text, it->color, 1.0f + 0.3f * (1.0f - t), (int)(255 * (1.0f - t)));
                break;
        }
        ++it;
    }

    SDL_RenderPresent(ren);
}
