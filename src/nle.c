
#include <assert.h>
#include <sys/time.h>

#include <signal.h>
#include <string.h>
#include <termios.h>

#include <fcontext/fcontext.h>

#define NEED_VARARGS
#include "hack.h"

#include "dlb.h"

#include "nle.h"

#define STACK_SIZE (1 << 15) // 32KiB

/* We are fine with whatever. */
boolean
authorize_wizard_mode()
{
    return TRUE;
}

boolean check_user_string(optstr) char *optstr;
{
    return TRUE;
}

void port_insert_pastebuf(buf) char *buf;
{
}

/* Copied from unixmain.c. */
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

/* Copied from unixmain.c. */
void sethanguphandler(handler) void FDECL((*handler), (int) );
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

void
init_nle_globals()
{
    pipe(nle.inpipe);
    pipe(nle.outpipe);

    nle.in = fdopen(nle.inpipe[0], "r");
    nle.out = fdopen(nle.inpipe[1], "w");

    nle.ttyrec = fopen("nle.ttyrec", "w");
    assert(nle.ttyrec != NULL);

    /* Set ttyrec file to be line buffered. */
    setvbuf(nle.ttyrec, NULL, _IOLBF, 0);
}

// Move to .h
fcontext_t returncontext;

void
mainloop(fcontext_transfer_t ctx_t)
{
    returncontext = ctx_t.ctx;

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
}

boolean
write_header(int length, unsigned char channel)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    int buffer[3];
    buffer[0] = tv.tv_sec;
    buffer[1] = tv.tv_usec;
    buffer[2] = length;

    /* Assumes little endianness */
    if (fwrite(buffer, sizeof(int), 3, nle.ttyrec) == 0) {
        assert(FALSE);
        return FALSE;
    }

    if (fputc((int) channel, nle.ttyrec) != (int) channel) {
        assert(FALSE);
        return FALSE;
    }

    return TRUE;
}

/*
 * This gets called via xputs a lot. We should probably override that.
 */
int nle_putchar(c) int c;
{
    /* return putc(c, stdout); */

    write_header(1, 0);
    return fputc(c, nle.ttyrec);
}

/*
 * puts seems to be called only by tty_raw_print and tty_raw_print_bold.
 * We could probably override this in winrl instead.
 */
int nle_puts(str) const char *str;
{
    /* puts includes a newline, fputs doesn't */

    int val = fputs(str, stdout);
    putc('\n', stdout);
    return val;

    /*
    write_header(strlen(str) + 1, 0);
    int val = fputs(str, nle.ttyrec);
    putc('\n', nle.ttyrec);
    return val;*/

    /*assert(FALSE);
  return 0;*/
}

/*
 * Used in place of xputs from termcap.c. Not using
 * the tputs padding logic from tclib.c.
 */
void nle_xputs(str) const char *str;
{
    int size = strlen(str);
    if (size == 0)
        return;

    write_header(size, 0);
    fputs(str, nle.ttyrec);
}

/*
int nle_putc(c) int c;
{
    return putc(c, nle.out);
}
*/

/* win/tty only calls fflush(stdout), which we ignore. */
int nle_fflush(stream) FILE *stream;
{
    fflush(nle.ttyrec);
    return 0;
}

ssize_t nle_get(buf, count) void *buf;
size_t count;
{
    return read(nle.outpipe[0], buf, count);
}

void nle_yield(done) boolean done;
{
    fflush(stdout);
    fcontext_transfer_t t = jump_fcontext(returncontext, (void *) done);

    if (!done)
        returncontext = t.ctx;
}

void nethack_exit(status) int status;
{
    nle_yield(TRUE);
}

void
nle_start()
{
    init_nle_globals();

    int oldin = dup(STDIN_FILENO);
    // int oldout = dup(STDOUT_FILENO);

    dup2(nle.inpipe[0], STDIN_FILENO);
    // dup2(nle.outpipe[1], STDOUT_FILENO);

    close(nle.inpipe[0]);
    // close(nle.outpipe[1]);

    nle.inpipe[0] = STDIN_FILENO;
    // fnle.outpipe[1] = STDOUT_FILENO;

    struct termios old, tty;
    tcgetattr((int) oldin, &old);
    tty = old;
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tcsetattr(oldin, TCSANOW, &tty);

    boolean done = FALSE;

    fcontext_stack_t stack = create_fcontext_stack(STACK_SIZE);

    fcontext_t generatorcontext =
        make_fcontext(stack.sptr, stack.ssize, mainloop);

    ssize_t size;
    char buf[BUFSIZ];

    fcontext_transfer_t t;

    for (;;) {
        t = jump_fcontext(generatorcontext, NULL);
        generatorcontext = t.ctx;
        done = (t.data != NULL);

        /*while ((size = nle_get(buf, BUFSIZ)) > 0) {
            write(oldout, buf, size);
            break; // FIXME
            }*/

        if (done)
            break;

        char i;
        read(oldin, &i, 1);
        write(nle.inpipe[1], &i, 1);

        write(STDOUT_FILENO, &i, 1);
    }

    const char *message = "nle_start: Read loop finished\n\0";
    // write(oldout, message, strlen(message));

    tcsetattr(oldin, TCSANOW, &old);

    destroy_fcontext_stack(&stack);
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
