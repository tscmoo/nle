/*
 * Wrapper for dlopen.
 */

#ifndef NLEDL_H
#define NLEDL_H

#include <stdio.h>

typedef struct nledl_ctx {
    void *dlhandle;
    void *nle_ctx;
    void (*step)(void *, int, int *);
    int done;
    FILE *outfile;
} nle_ctx_t;

nle_ctx_t *nle_start();
nle_ctx_t *nle_step(nle_ctx_t *, int);

void nle_reset(nle_ctx_t *);
void nle_end(nle_ctx_t *);

#endif /* NLEDL_H */
