
#ifndef NLE_H
#define NLE_H

/* TODO: Fix this. */
#undef SIG_RET_TYPE
#define SIG_RET_TYPE void (*)(int)

struct nle_globals {
    int inpipe[2];  /* pipe replacing stdin */
    int outpipe[2]; /* pipe replacing stdout */
    FILE *in;
    FILE *out;
    FILE *ttyrec;
};

void nle_start();

extern struct nle_globals nle;

#endif /* NLE_H */
