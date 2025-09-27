#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <array>
#include <vector>
#include <random>
#include <chrono>
#include <string>
#include <algorithm>

#include "classic.h"   // <- new: provide ClassicModeOptions to Game
#include "blitz.h"     // <- add Blitz mode options

const int CELL = 24; // size of a cell in pixels
const int COLS = 10;
const int ROWS = 20;

struct Vec { int x,y; };

using Grid = std::array<std::array<int, COLS>, ROWS>;

struct Piece {
    std::vector<Vec> blocks;
    SDL_Color color;
    int id;
    int orientation = 0; // 0..3
};

// Tetromino definitions (relative coords)
extern const std::array<std::vector<Vec>,7> TETROS;
extern const std::array<SDL_Color,7> T_COLORS;

// forward-declare so prototype can appear before full class definition
class Game;

// effect / particle types used across game.cpp
enum class TSpinType { None=0, Mini=1, Full=2 };

struct Particle {
    float x,y;
    float vx,vy;
    float size;
    int life;
    int max_life;
    bool streak;
    SDL_Color col;
};

struct TextEffect {
    std::string text;
    SDL_Color color;
    int life_ms;
    std::chrono::steady_clock::time_point start;
    int type; // effect type
    int x,y;
};

// Forward/aux declarations (detect_tspin implemented elsewhere)
TSpinType detect_tspin(Game &g, const Piece &p, Vec pos);

// Game class (excerpt showing added/modified members & APIs)
class Game {
public:
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;

    Grid grid{};

    std::vector<int> bag;
    std::vector<int> next_queue;
    int bag_index = 0;
    std::mt19937 rng;

    Piece current;
    Piece hold_piece;
    bool hold_used = false;
    bool can_hold = true; // allow hold when true; disabled until next spawn after use
    Vec cur_pos{3,0};

    int score = 0;
    int level = 1;
    int lines = 0;

    bool running = true;
    bool paused = false;

    std::chrono::steady_clock::time_point last_drop;
    int drop_ms = 800;

    // input repeat (DAS/ARR) and soft-drop
    int das_ms = 120; // delay before auto-shift (recommended default)
    int arr_ms = 12;  // auto-repeat rate (default requested)
    bool horiz_held = false; // left/right held
    int horiz_dir = 0; // -1 left, +1 right
    bool horiz_repeating = false;
    std::chrono::steady_clock::time_point last_horiz_move;

    bool down_held = false;
    int soft_ms = 50;
    std::chrono::steady_clock::time_point last_soft_move;

    // Lock / spawn timing rules
    int lock_delay_ms = 500; // lock delay (ms)
    bool lock_active = false;
    std::chrono::steady_clock::time_point lock_start;
    int lock_resets = 0;
    int max_lock_resets = 15; // prevent infinite stalling

    int are_ms = 20; // spawn delay between pieces
    bool spawn_pending = false;
    std::chrono::steady_clock::time_point spawn_time;

    // Visuals: particles and animations
    std::vector<Particle> particles;

    // Text effects and spin detection
    std::vector<TextEffect> effects;

    // rotation / spin helpers
    int last_kick_index = -1;        // index in the wall-kick table (0 == no offset)
    Vec last_kick_offset = {0,0};    // actual offset applied on last successful rotation
    bool last_was_rotate = false;
    std::chrono::steady_clock::time_point last_rotate_time;

    // new: corner count before the last rotation (used to make T-spin detection stricter)
    int last_pre_rot_corner_count = 0;

    // store the classic-mode options that were active when the game was started
    ClassicModeOptions classic_opts;

    // Blitz mode support
    BlitzModeOptions blitz_opts;
    bool blitz_active = false;
    std::chrono::steady_clock::time_point blitz_start_time;
    void start_blitz();

    // fonts (ensure these match uses in game.cpp)
    TTF_Font* font = nullptr;        // regular in-game font
    TTF_Font* popup_font = nullptr;  // big display font (display.otf)
    // optional header texture used for UI ribbons (e.g. costume1.png)
    SDL_Texture* header_tex = nullptr;
    // optional wallpaper texture used as gameplay background
    SDL_Texture* wallpaper = nullptr;

    // text drawing helpers used by game.cpp
    void draw_text(int x,int y,const std::string &text);
    void draw_colored_text(int x,int y,const std::string &text, SDL_Color color, float scale, int alpha);
        void draw_colored_text_left(int x,int y,const std::string &text, SDL_Color color, float scale, int alpha);
    void draw_colored_text_font(int x,int y,const std::string &text, SDL_Color color, float scale, int alpha, TTF_Font* usefont);

    // board popup helper (used for centered clears/spins)
    void spawn_board_popup(const std::string &main, const std::string &sub, int life_ms);

    // convenience overload so older 5-arg calls compile (for spawn_text_effect)
    // canonical 6-arg declaration (used throughout game.cpp)
    void spawn_text_effect(const std::string &text, SDL_Color col, int life_ms, int x, int y, int type);
    // convenience 5-arg overload that forwards to the canonical form with type=0
    inline void spawn_text_effect(const std::string &text, SDL_Color col, int life_ms, int x, int y) {
        spawn_text_effect(text, col, life_ms, x, y, 0);
    }

    // line clear animation
    std::vector<int> rows_to_clear;
    bool clearing = false;
    std::chrono::steady_clock::time_point clear_start;
    int clear_delay_ms = 40; // small particle delay before stomp
    int clear_anim_ms = 80; // stomp duration (short, non-annoying)
    // store animation progress per row in a map-like vector
    std::vector<float> clear_progress; // same size as rows_to_clear when active

    // screen fade (for quick transitions)
    float screen_fade = 0.0f; // 1.0 = black overlay, 0 = none
    bool fade_in = true;
    std::chrono::steady_clock::time_point start_time;

    Game();

    // Performance / attack metrics (display-only)
    uint64_t pieces_placed = 0; // counts spawns (proxy for pieces played)
    uint64_t actions_count = 0; // discrete input actions (edges, rotates, holds, hard-drops)
    uint64_t total_attacks = 0; // total attacks (sum of cleared lines treated as attacks)
    uint64_t lines_sent = 0;    // same as total_attacks for now (no networking present)
    int spike_size = 0;         // largest single attack (max cleared lines)

    void refill_bag();
    int next_from_bag();
    void spawn_from_queue();
    bool collides(const Piece &p, Vec pos);
    void lock_piece();
    std::vector<int> detect_full_rows();
    void perform_clear_collapse();
    void start_clear_animation();
    void spawn_particles_at(int cellx, int celly, SDL_Color col, int count, bool streak=false);
    void rotate_piece(bool clockwise);
    void hold();
    void soft_drop();
    void hard_drop();
    void tick();
    void render();

    // combo state for consecutive clears (handles singles, doubles and quads)
    int clear_combo_count = 0;                 // consecutive clear combo count
    int last_clear_count_size = 0;             // last clear's line count (1,2,3,4...)
    std::chrono::steady_clock::time_point last_clear_time;

    // scoring helpers
    bool back_to_back = false; // indicates last clear was B2B-eligible
    int combo_chain = 0;       // combo counter (consecutive clears on successive locks)

    // simple input state suitable for a deterministic step() API
    struct InputState {
        bool left = false;
        bool right = false;
        bool left_edge = false;  // true on key-down edge
        bool right_edge = false; // true on key-down edge
        bool soft = false;
        bool hard = false;
        bool rotate_cw = false;
        bool rotate_ccw = false;
        bool hold = false;
        bool hold_pressed = false; // edge detect
    };

    // Process one frame worth of inputs and advance the game by one tick.
    // This implements the requested `step(frameInputs)` style API.
    void step(const InputState &in);

    struct BoardPopup {
        std::string main;
        std::string sub;
        int life_ms = 0;
        std::chrono::steady_clock::time_point start;
        float base_scale = 1.0f;
        bool active = false;
    } board_popup;
};

// Run the game using the provided SDL window/renderer/font.
// This will block until the user exits the game (or the game signals stop).
// Returns 0 normally.
int RunGameSDL(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font);
