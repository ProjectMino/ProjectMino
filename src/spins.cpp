#include "game.h"

// Detect Guideline SRS+ T-Spin: distinguish Mini vs Full.
// Uses game.last_kick_index (which rotation offset was applied).
TSpinType detect_tspin(const Game &game, const Piece &current, Vec cur_pos) {
    if (current.id != 5) return TSpinType::None; // only T piece
    if (!game.last_was_rotate) return TSpinType::None; // must result from rotation

    // Rotation center for T in SRS (3x3 box) is at (1,1) relative to piece origin
    int cx = cur_pos.x + 1;
    int cy = cur_pos.y + 1;

    // mark own piece cells so they aren't considered blockers
    bool ownCell[ROWS][COLS];
    for (int r=0;r<ROWS;++r) for (int c=0;c<COLS;++c) ownCell[r][c] = false;
    for (const auto &b : current.blocks) {
        int ox = cur_pos.x + b.x;
        int oy = cur_pos.y + b.y;
        if (ox >= 0 && ox < COLS && oy >= 0 && oy < ROWS) ownCell[oy][ox] = true;
    }

    // count occupied corners after rotation
    std::array<std::pair<int,int>,4> corners = {{
        {cx - 1, cy - 1}, {cx + 1, cy - 1},
        {cx - 1, cy + 1}, {cx + 1, cy + 1}
    }};
    int occ_after = 0;
    for (const auto &cr : corners) {
        int rx = cr.first;
        int ry = cr.second;
        if (rx < 0 || rx >= COLS || ry < 0 || ry >= ROWS) {
            occ_after++;
        } else if (game.grid[ry][rx] && !ownCell[ry][rx]) {
            occ_after++;
        }
    }

    if (occ_after < 3) return TSpinType::None;

    // determine "front" cell (the direction the T points after rotation)
    int orient = (current.orientation % 4 + 4) % 4;
    int fx = cx;
    int fy = cy;
    switch (orient) {
        case 0: fy = cy + 1; break; // up -> front is below
        case 1: fx = cx - 1; break; // right -> front is left
        case 2: fy = cy - 1; break; // down -> front is above
        case 3: fx = cx + 1; break; // left -> front is right
    }

    bool frontBlocked = false;
    if (fx < 0 || fx >= COLS || fy < 0 || fy >= ROWS) frontBlocked = true;
    else if (game.grid[fy][fx] && !ownCell[fy][fx]) frontBlocked = true;

    // STRICTER RULE:
    // - If occ_after >= 3 AND (a non-zero kick was used OR occ_before < 3) -> it's a T-Spin.
    // - If occ_before was already >= 3 and no non-zero kick was used -> do NOT count as T-Spin.
    int occ_before = game.last_pre_rot_corner_count;
    if (game.last_kick_index > 0) {
        // kick used -> can be Mini or Full depending on front cell
        if (!frontBlocked) return TSpinType::Mini;
        return TSpinType::Full;
    } else {
        // no kick used: only count as spin if the rotation caused corners to increase from <3 to >=3
        if (occ_before >= 3) return TSpinType::None;
        return TSpinType::Full;
    }
}
