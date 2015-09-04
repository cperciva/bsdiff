/*-
 * Copyright 2012 Colin Percival
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "warnp.h"

#include "blockmatch_index.h"
#include "bsdiff_align.h"
#include "bsdiff_alignment.h"
#include "parallel_iter.h"

#include "bsdiff_align_multi.h"

/* Alignment state; passed to doalign. */
struct state {
	/* Parameters to align_multi. */
	const uint8_t * new;
	size_t newsize;
	const uint8_t * old;
	size_t oldsize;
	size_t blocklen;

	/* Values generated in align_multi. */
	struct blockmatch_index * index;
	size_t nblocks;
	BSDIFF_ALIGNMENT * BA;
};

/* Compute one part of the alignment.  Callback from parallel_iter. */
static int
doalign(void * cookie, size_t i)
{
	struct state * state = cookie;
	size_t opos, nblocklen, oblocklen;
	ssize_t pos;
	size_t j;

	/* Block length is blocklen or "the rest of the file". */
	if (i < state->nblocks - 1)
		nblocklen = state->blocklen;
	else
		nblocklen = state->newsize - i * state->blocklen;

	/* Find the start of the best-matching old block. */
	if ((pos = blockmatch_index_search(state->index,
	    &state->new[i * state->blocklen], nblocklen)) == -1) {
		warnp("blockmatch_index_search");
		goto err0;
	} else
		opos = pos;

	/*
	 * We assume that *part* of the correct alignment of the new data
	 * against the old data falls within the block we just found, but
	 * that's all; and we'll allow a "fudge factor" of 1.5 on how far
	 * outside of the block things might match up in case some data was
	 * deleted between old and new.
	 */
	oblocklen = state->blocklen;
	if (opos > nblocklen * 3 / 2) {
		oblocklen += nblocklen * 3 / 2;
		opos -= nblocklen * 3 / 2;
	} else {
		oblocklen += opos;
		opos = 0;
	}
	if (opos + oblocklen + nblocklen * 3 / 2 < state->oldsize)
		oblocklen += nblocklen * 3 / 2;
	else
		oblocklen = state->oldsize - opos;

	/* Align the portions of the two files. */
	if ((state->BA[i] = bsdiff_align(&state->new[i * state->blocklen],
	    nblocklen, &state->old[opos], oblocklen)) == NULL) {
		warnp("align");
		goto err0;
	}

	/* Adjust offsets to be relative to the complete files. */
	for (j = 0; j < bsdiff_alignment_getsize(state->BA[i]); j++) {
		bsdiff_alignment_get(state->BA[i], j)->npos +=
		    i * state->blocklen;
		bsdiff_alignment_get(state->BA[i], j)->opos += opos;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * bsdiff_align_multi(new, newsize, old, oldsize, blocklen, digestlen, ncores):
 * Align new[0 .. newsize - 1] against old[0 .. oldsize - 1] by individually
 * matching and aligning blocklen-byte blocks using length-digestlen digests,
 * using ncores computation threads.
 */
BSDIFF_ALIGNMENT
bsdiff_align_multi(const uint8_t * new, size_t newsize, const uint8_t * old,
    size_t oldsize, size_t blocklen, size_t digestlen, size_t ncores)
{
	struct state state;
	struct blockmatch_index * index;
	size_t nblocks;
	size_t i, j;
	BSDIFF_ALIGNMENT * BA;
	BSDIFF_ALIGNMENT A;
	struct bsdiff_alignseg * asegp;

	/* Index the old file. */
	printf("Indexing old file...\n");
	if ((index = blockmatch_index_index(old, oldsize,
	    blocklen, digestlen, ncores)) == NULL) {
		warnp("blockmatch_index_index");
		goto err0;
	}

	/*
	 * We want newsize / blocklen or newsize / blocklen + 1 blocks
	 * depending on which has the sanest final block size.
	 */
	nblocks = newsize / blocklen;
	if ((nblocks == 0) || (newsize - nblocks * blocklen >= blocklen / 2))
		nblocks += 1;

	/* Allocate an array for holding sub-alignments. */
	if ((BA = malloc(nblocks * sizeof(BSDIFF_ALIGNMENT))) == NULL)
		goto err1;

	/* Construct state structure for access from compute threads. */
	state.new = new;
	state.newsize = newsize;
	state.old = old;
	state.oldsize = oldsize;
	state.blocklen = blocklen;
	state.index = index;
	state.nblocks = nblocks;
	state.BA = BA;

	/*
	 * Figure out where blocks of the new file match up.  The incomplete
	 * error-handling path is because parallel_iter can fail with function
	 * calls still in progress.
	 */
	printf("Computing alignments...\n");
	if (parallel_iter(ncores, nblocks, doalign, &state))
		goto err0;

	/* Initialize empty alignment. */
	if ((A = bsdiff_alignment_init(0)) == NULL) {
		warnp("bsdiff_alignment_init");
		goto err2;
	}

	/* Combine partial alignments. */
	printf("Combining partial alignments...\n");
	for (i = 0; i < nblocks; i++) {
		/* Add these alignment segments to the whole-file alignment. */
		for (j = 0; j < bsdiff_alignment_getsize(BA[i]); j++) {
			asegp = bsdiff_alignment_get(BA[i], j);
			if (asegp->alen) {
				if (asegp->alen > 0x8000000000000000L)
					/* Larger than 2^63 */
					warnp("bsdiff_alignment_append: BA[%lu] segment size %lu",
						  i, asegp->alen);
				if (bsdiff_alignment_append(A,
					bsdiff_alignment_get(BA[i], j), 1)) {
					warnp("bsdiff_alignment_append");
					goto err3;
				}
			} else {
				warnp("bsdiff_alignment_append: skipped zero length alignment at BA[%lu]", i);
			}
		}
	}

	/* Free partial alignments. */
	for (i = 0; i < nblocks; i++)
		bsdiff_alignment_free(BA[i]);
	free(BA);

	/* Free the index of the old file. */
	blockmatch_index_free(index);

	/* Success! */
	return (A);

err3:
	bsdiff_alignment_free(A);
err2:
	for (i = 0; i < nblocks; i++)
		bsdiff_alignment_free(BA[i]);
	free(BA);
err1:
	blockmatch_index_free(index);
err0:
	/* Failure! */
	return (NULL);
}
