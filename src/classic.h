#pragma once

// Simple struct to hold "Classic" mode configuration.
// Consumers (menu or other UI) set g_classic_mode_options before launching the game.
struct ClassicModeOptions {
    // If use_countdown is true and countdown_ms > 0, RunGameSDL will show a simple countdown
    // before gameplay begins.
    bool use_countdown = false;
    int countdown_ms = 0;

    // If win_lines > 0, game logic may use it as a win condition (not required here).
    int win_lines = 0;

    // Future flags could be placed here (e.g. special rules).
};

// Global options instance (defined in game.cpp).
extern ClassicModeOptions g_classic_mode_options;