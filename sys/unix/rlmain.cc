
#include <cstring>
#include <termios.h>

extern "C" {
#include "hack.h"
}

extern "C" {
#include "dlb.h"
}

extern "C" {
#include "nle.h"
}

int
main(int argc, char **argv)
{
    struct termios old, tty;
    tcgetattr((int) STDIN_FILENO, &old);
    tty = old;
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);

    bool done;

    int oldin = dup(STDIN_FILENO);

    done = nle_start();

    char i;
    while (!done) {
        read(oldin, &i, 1);
        done = nle_step(i);
    }
    nle_end();

    tcsetattr(STDIN_FILENO, TCSANOW, &old);
}
