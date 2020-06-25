
#ifndef NLE_H
#define NLE_H

/* TODO: Fix this. */
#undef SIG_RET_TYPE
#define SIG_RET_TYPE void (*)(int)

struct nle_globals {
    FILE *ttyrec;
    char outbuf[BUFSIZ];
    char *outbuf_write_ptr;
    char *outbuf_write_end;
};

boolean nle_start();
boolean nle_step(char);
void nle_end();

extern struct nle_globals nle;

#endif /* NLE_H */
