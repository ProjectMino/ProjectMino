#pragma once

#include <chrono>

// Blitz mode options/control. Kept minimal — Game uses these options when starting.
struct BlitzModeOptions {
    bool enabled = false;        // set true to start a blitz game
    int duration_ms = 120000;    // default 2 minutes
};

// global instance (defined in game.cpp)
extern BlitzModeOptions g_blitz_mode_options;