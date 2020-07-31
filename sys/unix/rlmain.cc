
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <termios.h>
#include <unistd.h>

/*
extern "C" {
#include "hack.h"
}

extern "C" {
#include "dlb.h"
}
*/

extern "C" {
#include "nledl.h"
}

void
play(nle_ctx_t *nle)
{
    char i;
    while (!nle->done) {
        read(STDIN_FILENO, &i, 1);
        nle = nle_step(nle, i);
    }
}

void
randplay(nle_ctx_t *nle)
{
    int actions[] = {
        13, 107, 108, 106, 104, 117, 110, 98, 121,
        75, 76,  74,  72,  85,  78,  66,  89,
    };
    size_t n = sizeof(actions) / sizeof(actions[0]);

    while (!nle->done) {
        nle = nle_step(nle, actions[rand() % n]);
    }
}

void
randgame(nle_ctx_t *nle)
{
    nle_step(nle, 'y');
    nle_step(nle, 'y');
    nle_step(nle, '\n');

    for (int i = 0; i < 50; ++i) {
        randplay(nle);
        nle_reset(nle);
    }
}

int
main(int argc, char **argv)
{
    /*std::cerr << "short break before beginning: ";
      int i;
      read(STDIN_FILENO, &i, 1);
    */

    struct termios old, tty;
    tcgetattr((int) STDIN_FILENO, &old);
    tty = old;
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);

    nle_ctx_t *nle = nle_start();
    randgame(nle);
    play(nle);
    nle_reset(nle);
    play(nle);
    nle_end(nle);

    /*
    std::cerr << "short break before end: ";
    read(STDIN_FILENO, &i, 1);
    */
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
}
