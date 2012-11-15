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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "parallel_iter.h"

#include "blockmatch_psimm.h"

#include "blockmatch_index.h"

/* Index structure. */
struct blockmatch_index {
	struct blockmatch_psimm_ctx * psimm_ctx;
	const uint8_t * buf;	/* Valid only during indexing stage. */
	size_t len;
	size_t blocklen;
	size_t diglen;
	size_t nblocks;
	double ** digests;
};

/* Compute one part of the index.  Callback from parallel_iter. */
static int
dodigest(void * cookie, size_t i)
{
	struct blockmatch_index * index = cookie;
	size_t offset = i * index->blocklen;
	size_t blocklen = index->blocklen;

#if 0
	/* Print progress message. */
	if ((i % 10) == 0)
		fprintf(stderr, "%zu", i);
	else if ((i % 2) == 0)
		fprintf(stderr, ".");
#endif

	/* The last block is a different size. */
	if (i == index->nblocks - 1)
		blocklen = index->len - offset;

	/* Compute the digest of this block. */
	if ((index->digests[i] = blockmatch_psimm_digest(&index->buf[offset],
	    blocklen, index->psimm_ctx)) == NULL)
		goto err0;

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * blockmatch_index_index(buf, len, blocklen, diglen, P):
 * Split buf[0 .. len - 1] into blocklen-byte blocks, and compute length-diglen
 * digests.  Return an index which can be passed to blockmatch_index_search.
 * If len is not an exact multiple of blocklen, the final block will be in the
 * range [MIN(blocklen / 2, len), 3 * blocklen / 2) bytes.  Compute the index
 * using P threads.
 */
struct blockmatch_index *
blockmatch_index_index(const uint8_t * buf, size_t len, size_t blocklen,
    size_t diglen, size_t P)
{
	struct blockmatch_index * index;

	/* Sanity-check. */
	assert(blocklen > 0);
	assert(diglen > 0);

	/* Allocate index structure. */
	if ((index = malloc(sizeof(struct blockmatch_index))) == NULL)
		goto err0;
	index->buf = buf;
	index->len = len;
	index->blocklen = blocklen;
	index->diglen = diglen;

	/* Create context for producing length-diglen digests. */
	if ((index->psimm_ctx = blockmatch_psimm_init(diglen)) == NULL)
		goto err1;

	/*
	 * Figure out how many blocks we want.  We'll have len / blocklen
	 * blocks of blocklen or more bytes; and if what's left is blocklen / 2
	 * or more we'll make that another block (if not, the remainder is
	 * included in the final block, making it more than blocklen bytes in
	 * length).
	 */
	index->nblocks = len / blocklen;
	if ((index->nblocks == 0) ||
	    (len - index->nblocks * blocklen >= blocklen / 2))
		index->nblocks += 1;

	/* Allocate an array for pointers to digests. */
	if ((index->digests =
	    malloc(index->nblocks * sizeof(double *))) == NULL)
		goto err2;

	/*
	 * Compute digests; last one separately due to different length.  The
	 * incomplete error-handling path is because parallel_iter can fail
	 * with function calls still in progress.
	 */
	if (parallel_iter(P, index->nblocks, dodigest, index))
		goto err0;

	/* Success! */
	return (index);

err2:
	blockmatch_psimm_free(index->psimm_ctx);
err1:
	free(index);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * blockmatch_index_search(index, buf, len):
 * Compare buf[0.. len - 1] against the blocks in index.  Return the offset of
 * the start of the best-matching block or -1 on error.
 */
ssize_t
blockmatch_index_search(const struct blockmatch_index * index,
    const uint8_t * buf, size_t len)
{
	double * DIG;
	double score, bestscore;
	size_t i, besti;

	/* Compute the digest of the provided data. */
	if ((DIG =
	    blockmatch_psimm_digest(buf, len, index->psimm_ctx)) == NULL)
		goto err0;

	/* Find the best block. */
	bestscore = -1;
	besti = 0;
	for (i = 0; i < index->nblocks; i++) {
		score = blockmatch_psimm_score(DIG, index->digests[i],
		    index->diglen);
		if (score > bestscore) {
			bestscore = score;
			besti = i;
		}
	}

	/* Free the digest. */
	free(DIG);

	/* Return the position where the best block starts. */
	return (besti * index->blocklen);

err0:
	/* Failure! */
	return (-1);
}

/**
 * blockmatch_index_free(index):
 * Free the provided index.
 */
void
blockmatch_index_free(struct blockmatch_index * index)
{
	size_t i;

	/* Free the digests. */
	for (i = 0; i < index->nblocks; i++)
		free(index->digests[i]);
	free(index->digests);

	/* Release the digesting context. */
	blockmatch_psimm_free(index->psimm_ctx);

	/* Free our structure. */
	free(index);
}
