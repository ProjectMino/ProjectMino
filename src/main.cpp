#include "game.h"

int main(int argc, char **argv){
    if(SDL_Init(SDL_INIT_VIDEO)<0) return 1;
    if(TTF_Init()<0) return 1;

    SDL_Window* win = SDL_CreateWindow("Tetris", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 720, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    TTF_Font* font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18);

    Game game;
    game.win = win; game.ren = ren; game.font = font;

    bool quit=false;
    SDL_Event e;
    while(!quit && game.running){
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT) { quit=true; break; }
            if(e.type==SDL_KEYDOWN){
                switch(e.key.keysym.sym){
                    case SDLK_LEFT: { Vec np = game.cur_pos; np.x--; if(!game.collides(game.current,np)) game.cur_pos = np; } break;
                    case SDLK_RIGHT:{ Vec np = game.cur_pos; np.x++; if(!game.collides(game.current,np)) game.cur_pos = np; } break;
                    case SDLK_DOWN: game.soft_drop(); break;
                    case SDLK_SPACE: game.hard_drop(); break;
                    case SDLK_z: game.rotate_piece(false); break;
                    case SDLK_x: game.rotate_piece(true); break;
                    case SDLK_UP: game.rotate_piece(true); break;
                    case SDLK_c: game.hold(); break;
                    case SDLK_p: game.paused = !game.paused; break;
                }
            }
            if(e.type==SDL_KEYDOWN){
                // set hold flags for continuous input
                if(e.key.keysym.sym==SDLK_LEFT){ game.horiz_held = true; game.horiz_dir = -1; game.horiz_repeating = false; game.last_horiz_move = std::chrono::steady_clock::now(); }
                if(e.key.keysym.sym==SDLK_RIGHT){ game.horiz_held = true; game.horiz_dir = 1; game.horiz_repeating = false; game.last_horiz_move = std::chrono::steady_clock::now(); }
                if(e.key.keysym.sym==SDLK_DOWN){ game.down_held = true; game.last_soft_move = std::chrono::steady_clock::now(); }
            }
            if(e.type==SDL_KEYUP){
                if(e.key.keysym.sym==SDLK_LEFT){ if(game.horiz_dir==-1) { game.horiz_held=false; game.horiz_dir=0; game.horiz_repeating=false; } }
                if(e.key.keysym.sym==SDLK_RIGHT){ if(game.horiz_dir==1) { game.horiz_held=false; game.horiz_dir=0; game.horiz_repeating=false; } }
                if(e.key.keysym.sym==SDLK_DOWN){ game.down_held=false; }
            }
        }

        game.tick();
        game.render();
        SDL_Delay(8);
    }


    if(font) TTF_CloseFont(font);
    if(ren) SDL_DestroyRenderer(ren);
    if(win) SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
