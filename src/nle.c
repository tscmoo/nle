// Should be called nle.c

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <termios.h>

#define NEED_VARARGS
#include "hack.h"

#include "dlb.h"

#include "nle.h"

// We are fine with whatever.
boolean
authorize_wizard_mode()
{
    return TRUE;
}

boolean
check_user_string(char *optstr)
{
    return TRUE;
}

void
port_insert_pastebuf(char *buf)
{
}

// Copied from unixmain.c.
unsigned long
sys_random_seed()
{
    unsigned long seed = 0L;
    unsigned long pid = (unsigned long) getpid();
    boolean no_seed = TRUE;
#ifdef DEV_RANDOM
    FILE *fptr;

    fptr = fopen(DEV_RANDOM, "r");
    if (fptr) {
        fread(&seed, sizeof(long), 1, fptr);
        has_strong_rngseed = TRUE; /* decl.c */
        no_seed = FALSE;
        (void) fclose(fptr);
    } else {
        /* leaves clue, doesn't exit */
        paniclog("sys_random_seed", "falling back to weak seed");
    }
#endif
    if (no_seed) {
        seed = (unsigned long) getnow(); /* time((TIME_type) 0) */
        /* Quick dirty band-aid to prevent PRNG prediction */
        if (pid) {
            if (!(pid & 3L))
                pid -= 1L;
            seed *= pid;
        }
    }
    return seed;
}

// Copied from unixmain.c.
void
sethanguphandler(void (*handler)(int))
{
#ifdef SA_RESTART
    /* don't want reads to restart.  If SA_RESTART is defined, we know
     * sigaction exists and can be used to ensure reads won't restart.
     * If it's not defined, assume reads do not restart.  If reads restart
     * and a signal occurs, the game won't do anything until the read
     * succeeds (or the stream returns EOF, which might not happen if
     * reading from, say, a window manager). */
    struct sigaction sact;

    (void) memset((genericptr_t) &sact, 0, sizeof sact);
    sact.sa_handler = (SIG_RET_TYPE) handler;
    (void) sigaction(SIGHUP, &sact, (struct sigaction *) 0);
#ifdef SIGXCPU
    (void) sigaction(SIGXCPU, &sact, (struct sigaction *) 0);
#endif
#else /* !SA_RESTART */
    (void) signal(SIGHUP, (SIG_RET_TYPE) handler);
#ifdef SIGXCPU
    (void) signal(SIGXCPU, (SIG_RET_TYPE) handler);
#endif
#endif /* ?SA_RESTART */
}

struct nle_globals nle;

FILE *nle_stdout;

void
init_nle_globals()
{
    pipe(nle.inpipe);
    pipe(nle.outpipe);
    nle.in = fdopen(nle.inpipe[0], "r");
    nle.out = fdopen(nle.inpipe[1], "w");

    nle_stdout = nle.out;
}

int
nle_putchar(int c)
{
    return putc(c, nle.out);
}

int
nle_puts(const char *str)
{
    return fputs(str, nle.out);
}

void *mainloop(unused) void *unused;
{
    early_init();

    g.hname = "nethack";
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

    init_nhwindows(0, NULL); /* now we can set up window system */

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

    boolean resuming = FALSE;

    if (*g.plname) {
        /* TODO(heiner): Remove locks entirely.
           By default, this also checks that we're on a pty... */
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

    return 0;
}

ssize_t nle_get(buf, count) void *buf;
size_t count;
{
    return read(nle.outpipe[0], buf, count);
}

int oldin;
int oldout;

int read_stop_pipe[2];

void *
read_thread(void *fdp)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(oldin, &rfds);
    FD_SET(read_stop_pipe[0], &rfds);

    char i;
    while (select(read_stop_pipe[0] + 1, &rfds, NULL, NULL, NULL) != -1) {
        if (FD_ISSET(read_stop_pipe[0], &rfds))
            break;
        read(oldin, &i, 1);
        write(nle.inpipe[1], &i, 1);

        FD_SET(read_stop_pipe[0], &rfds);
    }
    return 0;
}

void
nethack_exit(int status)
{
    close(STDOUT_FILENO);
    pthread_exit(&status);
}

void
nle_start()
{
    init_nle_globals();

    oldin = dup(STDIN_FILENO);
    oldout = dup(STDOUT_FILENO);

    dup2(nle.inpipe[0], STDIN_FILENO);
    dup2(nle.outpipe[1], STDOUT_FILENO);

    pipe(read_stop_pipe);

    close(nle.inpipe[0]);
    close(nle.outpipe[1]);

    nle.inpipe[0] = STDIN_FILENO;
    nle.outpipe[1] = STDOUT_FILENO;

    struct termios old, tty;
    tcgetattr((int) oldin, &old);
    tty = old;
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tcsetattr(oldin, TCSANOW, &tty);

    pthread_t thread, input_thread;
    int rc =
        pthread_create(&input_thread, NULL, read_thread, (void *) &oldin);
    rc = pthread_create(&thread, NULL, mainloop, (void *) 0);

    ssize_t size;
    char buf[BUFSIZ];
    while ((size = nle_get(buf, BUFSIZ)) > 0) {
        write(oldout, buf, size);
    }

    const char *message = "nle_start: Read loop finished\n\0";
    write(oldout, message, strlen(message));

    pthread_join(thread, NULL);

    write(read_stop_pipe[1], buf, 1);

    pthread_join(input_thread, NULL);
    tcsetattr(oldin, TCSANOW, &old);
}

/* From unixtty.c */
/* fatal error */
/*VARARGS1*/
void error
VA_DECL(const char *, s)
{
    VA_START(s);
    VA_INIT(s, const char *);

    if (iflags.window_inited)
        exit_nhwindows((char *) 0); /* for tty, will call settty() */

    Vprintf(s, VA_ARGS);
    (void) putchar('\n');
    VA_END();
    exit(EXIT_FAILURE);
}

/* From unixtty.c */
char erase_char, intr_char, kill_char;

void
gettty()
{
    /* Should set erase_char, intr_char, kill_char */
}

void settty(s) const char *s;
{
    end_screen();
    if (s)
        raw_print(s);
}

void
setftty()
{
    start_screen();
}

void
intron()
{
}

void
introff()
{
}
