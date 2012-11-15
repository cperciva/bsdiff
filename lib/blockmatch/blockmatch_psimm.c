/*-
 * Copyright (c) 2012 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "entropy.h"
#include "fft_fftn.h"

#include "blockmatch_psimm.h"

/* Mapping context. */
struct map_ctx {
	size_t L;
	size_t foldlen;
	size_t fftlen;
	double * FFTLUT;
	double map[256];
};

/* Digesting context. */
struct blockmatch_psimm_ctx {
	size_t L;
	struct map_ctx ctx[3];
	size_t offsets[3];
};

/* Prepare one of the mapping contexts. */
static int
makectx(struct map_ctx * ctx, size_t L)
{
	uint8_t r[32];
	size_t i;

	/* Record the sub-digest length. */
	ctx->L = L;

	/* We're going to "fold" the input down to (2*L+1) byte lengths. */
	ctx->foldlen = 2 * L + 1;

	/* Figure out what power-of-2 FFT length will end up being used. */
	ctx->fftlen = fft_fftn_getlen(ctx->foldlen);

	/* Read 256 bits of entropy. */
	if (entropy_read(r, 32))
		goto err0;

	/* We map byte values to 1 or -1. */
	for (i = 0; i < 256; i++) {
		if (r[i / 8] & (1 << (i % 8)))
			ctx->map[i] = 1;
		else
			ctx->map[i] = -1;
	}

	/* Allocate and initialize the lookup tables for the FFTs. */
	if ((ctx->FFTLUT = malloc(4 * ctx->fftlen * sizeof(double))) == NULL)
		goto err0;
	fft_fftn_makelut(ctx->FFTLUT, ctx->foldlen);

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * blockmatch_psimm_init(L):
 * Prepare for creating length-L digests.  Return a context which can be used
 * by future calls to blockmatch_psimm_digest, including simultaneous calls
 * from multiple threads.
 */
struct blockmatch_psimm_ctx *
blockmatch_psimm_init(size_t L)
{
	struct blockmatch_psimm_ctx * ctx;
	size_t L0, L1, L2;

	/* Allocate a context structure. */
	if ((ctx = malloc(sizeof(struct blockmatch_psimm_ctx))) == NULL)
		goto err0;
	ctx->L = L;

	/* Sub-digests 0 and 1 are both [L/4, L/4 + L/8) long. */
	L0 = L / 4 + (L * drand48() * 0.125);
	L1 = L / 4 + (L * drand48() * 0.125);

	/* Sub-digest 2 is whatever's left. */
	L2 = L - (L0 + L1);

	/* Make contexts. */
	if (makectx(&ctx->ctx[0], L0))
		goto err1;
	if (makectx(&ctx->ctx[1], L1))
		goto err2;
	if (makectx(&ctx->ctx[2], L2))
		goto err3;

	/* Record where each sub-digest starts. */
	ctx->offsets[0] = 0;
	ctx->offsets[1] = L0;
	ctx->offsets[2] = L0 + L1;

	/* Success! */
	return (ctx);

err3:
	free(ctx->ctx[1].FFTLUT);
err2:
	free(ctx->ctx[0].FFTLUT);
err1:
	free(ctx);
err0:
	/* Failure! */
	return (NULL);
}

/* Compute one portion of a digest. */
static int
subdigest(const uint8_t * buf, size_t len, const size_t bfreq[256],
    const struct map_ctx * ctx, double * DIG)
{
	double map[256];
	double * TMP;
	double * FFTDAT;
	double S, T;
	size_t i, j, jmax;

	/* Compute zero-point adjustment. */
	for (T = S = 0, i = 0; i < 256; i++) {
		S += ctx->map[i] * sqrt(bfreq[i]);
		T += sqrt(bfreq[i]);
	}
	S = S / T;

	/* Compute weighted byte mappings. */
	for (i = 0; i < 256; i++) {
		if (bfreq[i] == 0)
			map[i] = 0;
		else
			map[i] = (ctx->map[i] - S) / sqrt(bfreq[i]);
	}

	/*
	 * Allocate temporary working space for the FFT, staging space for
	 * holding the FFT input and output, and the digest.
	 */
	if ((TMP = malloc(2 * ctx->fftlen * sizeof(double))) == NULL)
		goto err0;
	if ((FFTDAT = malloc(2 * ctx->foldlen * sizeof(double))) == NULL)
		goto err1;

	/* Map and project the input data. */
	memset(FFTDAT, 0, 2 * ctx->foldlen * sizeof(double));
	for (i = 0; i < len; i += ctx->foldlen) {
		jmax = ctx->foldlen;
		if (jmax + i > len)
			jmax = len - i;
		for (j = 0; j < jmax; j++)
			FFTDAT[j * 2] += map[buf[i + j]];
	}

	/* Perform the FFT. */
	fft_fftn_fft(FFTDAT, ctx->foldlen, ctx->FFTLUT, TMP);

	/* Record the energy in the first half of the AC spectrum. */
	for (i = 0; i < ctx->L; i++)
		DIG[i] = FFTDAT[2 * i + 2] * FFTDAT[2 * i + 2] +
		    FFTDAT[2 * i + 3] * FFTDAT[2 * i + 3];

	/* Normalize. */
	for (S = 0, i = 0; i < ctx->L; i++)
		S += DIG[i] * DIG[i];
	S = sqrt(ctx->L) / sqrt(S);
	for (i = 0; i < ctx->L; i++)
		DIG[i] = DIG[i] * S;

	/* Free temporary working space and staging space. */
	free(TMP);
	free(FFTDAT);

	/* Success! */
	return (0);

err1:
	free(TMP);
err0:
	/* Failure! */
	return (-1);
}

/**
 * blockmatch_psimm_digest(buf, len, ctx):
 * Generate and return a digest of buf[0 .. len-1].
 */
double *
blockmatch_psimm_digest(const uint8_t * buf, size_t len,
    const struct blockmatch_psimm_ctx * ctx)
{
	size_t bfreq[256];
	double * DIG;
	size_t i;

	/* Count how often each byte occurs. */
	memset(bfreq, 0, 256 * sizeof(size_t));
	for (i = 0; i < len; i++)
		bfreq[buf[i]]++;

	/* Allocate space for the digest. */
	if ((DIG = malloc(ctx->L * sizeof(double))) == NULL)
		goto err0;

	/* Compute sub-digests in the appropriate places. */
	for (i = 0; i < 3; i++) {
		if (subdigest(buf, len, bfreq,
		    &ctx->ctx[i], &DIG[ctx->offsets[i]]))
			goto err1;
	}

	/* Success! */
	return (DIG);

err1:
	free(DIG);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * blockmatch_psimm_free(ctx):
 * Free the context returned by blockmatch_psimm_init.
 */
void
blockmatch_psimm_free(struct blockmatch_psimm_ctx * ctx)
{
	size_t i;

	/* Free everything. */
	for (i = 0; i < 3; i++)
		free(ctx->ctx[i].FFTLUT);
	free(ctx);
}

/**
 * blockmatch_psimm_score(DIG1, DIG2, L):
 * Return a match score for length-L digests DIG1 and DIG2 which were
 * generated using the same r parameter.
 */
double
blockmatch_psimm_score(double * DIG1, double * DIG2, size_t L)
{
	double score;
	size_t i;

	/* The match score is just the dot product of the vectors. */
	for (score = 0, i = 0; i < L; i++)
		score += DIG1[i] * DIG2[i];
	return (score);
}
