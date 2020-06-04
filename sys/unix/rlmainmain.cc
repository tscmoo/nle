
#include <cstring>

extern "C" {
#include "hack.h"
}

extern "C" {
#include "dlb.h"
}

#undef SIG_RET_TYPE
#define SIG_RET_TYPE void (*)(int)

int
main(int argc, char **argv)
{
    early_init();

    g.hname = argv[0];
    g.hackpid = getpid();

    choose_windows(DEFAULT_WINDOW_SYS);

    const char *dir = HACKDIR;
    if (dir && chdir(dir) < 0) {
        perror(dir);
        error("Cannot chdir to %s.", dir);
    }

    strncpy(g.plname, "Agent", sizeof g.plname - 1);

#ifdef _M_UNIX
    check_sco_console();
#endif
#ifdef __linux__
    check_linux_console();
#endif
    initoptions();

    u.uhp = 1; /* prevent RIP on early quits */
    g.program_state.preserve_locks = 1;

    init_nhwindows(&argc, argv); /* now we can set up window system */

#ifndef NO_SIGNAL
    sethanguphandler((SIG_RET_TYPE) hangup);
#endif

#ifdef _M_UNIX
    init_sco_cons();
#endif
#ifdef __linux__
    init_linux_cons();
#endif

    set_playmode(); /* sets plname to "wizard" for wizard mode */
    /* hide any hyphens from plnamesuffix() */
    g.plnamelen = (int) strlen(g.plname);

    /* strip role,race,&c suffix; calls askname() if plname[] is empty
       or holds a generic user name like "player" or "games" */
    plnamesuffix();

    dlb_init(); /* must be before newgame() */

    /*
     * Initialize the vision system.  This must be before mklev() on a
     * new game or before a level restore on a saved game.
     */
    vision_init();

    display_gamewindows();

    boolean resuming = false;

    if (*g.plname) {
        getlock();
        g.program_state.preserve_locks = 0; /* after getlock() */
    }

    if (restore_saved_game() != 0) {
        pline("Not restoring save file...");
        if (yn("Do you want to keep the save file?") == 'n') {
            (void) delete_savefile();
        }
    }

    if (!resuming) {
        player_selection();
        newgame();
    }

    moveloop(resuming);

    exit(EXIT_SUCCESS);
    /*NOTREACHED*/
    return 0;
}
