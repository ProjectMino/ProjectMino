#include "game.h"

// Return true if the given piece placed at cur_pos should be considered a T-Spin.
// Heuristic: only valid for T piece (id==5). Count occupied corners around the T
// rotation center. Only existing blocks in the game's grid count (exclude cells
// occupied by the current piece itself).

bool detect_tspin(const Game &game, const Piece &current, Vec cur_pos){
    if(current.id != 5) return false;

    // approximate center of rotation for the T piece in our block representation
    int cx = cur_pos.x + 1;
    int cy = cur_pos.y + 1;

    int corners[4][2] = {{cx-1,cy-1},{cx+2,cy-1},{cx-1,cy+2},{cx+2,cy+2}};

    // mark own cells so we don't count them as occupied
    bool ownCell[ROWS][COLS];
    for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++) ownCell[r][c] = false;
    for(auto &b: current.blocks){ int ox = cur_pos.x + b.x; int oy = cur_pos.y + b.y; if(ox>=0 && ox<COLS && oy>=0 && oy<ROWS) ownCell[oy][ox] = true; }

    int occ = 0;
    for(int i=0;i<4;i++){
        int rx = corners[i][0];
        int ry = corners[i][1];
        if(rx<0||rx>=COLS||ry<0||ry>=ROWS) occ++;
        else if(game.grid[ry][rx] && !ownCell[ry][rx]) occ++;
    }
    return occ >= 3;
}
