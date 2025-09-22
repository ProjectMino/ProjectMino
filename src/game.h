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

// T-Spin result enum (Guideline)
enum class TSpinType { None = 0, Mini = 1, Full = 2 };

// prototype for spin detection (implemented in spins.cpp)
TSpinType detect_tspin(const Game &game, const Piece &current, Vec cur_pos);

struct Game {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    TTF_Font* font = nullptr;
    Grid grid{};

    std::vector<int> bag;
    std::vector<int> next_queue;
    int bag_index = 0;
    std::mt19937 rng;

    Piece current;
    Piece hold_piece;
    bool hold_used = false;
    Vec cur_pos{3,0};

    int score = 0;
    int level = 1;
    int lines = 0;

    bool running = true;
    bool paused = false;

    std::chrono::steady_clock::time_point last_drop;
    int drop_ms = 800;

    // input repeat (DAS/ARR) and soft-drop
    int das_ms = 150; // delay before auto-shift
    int arr_ms = 50;  // auto-repeat rate
    bool horiz_held = false; // left/right held
    int horiz_dir = 0; // -1 left, +1 right
    bool horiz_repeating = false;
    std::chrono::steady_clock::time_point last_horiz_move;

    bool down_held = false;
    int soft_ms = 50;
    std::chrono::steady_clock::time_point last_soft_move;

    // Visuals: particles and animations
    struct Particle { float x,y; float vx,vy; int life; int max_life; float size; bool streak; SDL_Color col; };
    std::vector<Particle> particles;

    // Text effects and spin detection
    struct TextEffect { std::string text; SDL_Color color; int life_ms; std::chrono::steady_clock::time_point start; int x,y; int type; };
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

    void spawn_text_effect(const std::string &txt, SDL_Color col, int life_ms, int x, int y, int type=0);
    void draw_colored_text(int x,int y,const std::string &text, SDL_Color color, float scale=1.0f, int alpha=255);

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
    void draw_text(int x,int y,const std::string &text);
    void render();
};

// Run the game using the provided SDL window/renderer/font.
// This will block until the user exits the game (or the game signals stop).
// Returns 0 normally.
int RunGameSDL(SDL_Window* window, SDL_Renderer* renderer, TTF_Font* font);
