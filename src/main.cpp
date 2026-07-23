#include "game/game.h"
#include <cstring>
#include <cstdlib>

int main(int argc, char** argv) {
    Game game;
    game.init();
    if (argc > 1 && strcmp(argv[1], "--smoke") == 0) {
        int frames = argc > 2 ? atoi(argv[2]) : 600;
        game.smokeTest(frames);
        game.shutdown();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--play-test") == 0) {
        int fails = game.playTest();
        game.shutdown();
        return fails == 0 ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--menu-shot") == 0) {
        bool setup = argc > 2 && strcmp(argv[2], "setup") == 0;
        game.debugMenuShot(setup ? "menu_setup.png" : "menu_main.png", setup);
        game.shutdown();
        return 0;
    }
    game.run();
    game.shutdown();
    return 0;
}
