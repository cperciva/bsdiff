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

#ifndef _BLOCKMATCH_PSIMM_H_
#define _BLOCKMATCH_PSIMM_H_

#include <stddef.h>
#include <stdint.h>

/* Opaque type. */
struct blockmatch_psimm_ctx;

/**
 * blockmatch_psimm_init(L):
 * Prepare for creating length-L digests.  Return a context which can be used
 * by future calls to blockmatch_psimm_digest, including simultaneous calls
 * from multiple threads.
 */
struct blockmatch_psimm_ctx * blockmatch_psimm_init(size_t);

/**
 * blockmatch_psimm_digest(buf, len, ctx):
 * Generate and return a digest of buf[0 .. len-1].
 */
double * blockmatch_psimm_digest(const uint8_t *, size_t,
    const struct blockmatch_psimm_ctx *);

/**
 * blockmatch_psimm_free(ctx):
 * Free the context returned by blockmatch_psimm_init.
 */
void blockmatch_psimm_free(struct blockmatch_psimm_ctx *);

/**
 * blockmatch_psimm_score(DIG1, DIG2, L):
 * Return a match score for length-L digests DIG1 and DIG2 which were
 * generated using the same context.
 */
double blockmatch_psimm_score(double *, double *, size_t);

#endif /* !_BLOCKMATCH_PSIMM_H_ */
