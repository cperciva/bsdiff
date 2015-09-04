/*-
 * Copyright 2003-2005, 2012 Colin Percival
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

#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "warnp.h"

#include "bsdiff_alignment.h"
#include "sufsort_qsufsort.h"

#include "bsdiff_align.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

static size_t
matchlen(const uint8_t *old, size_t oldsize,
    const uint8_t *new, size_t newsize)
{
	size_t i;

	for (i = 0; (i < oldsize) && (i < newsize); i++)
		if (old[i] != new[i])
			break;

	return i;
}

static size_t
search(size_t *I, const uint8_t *old, size_t oldsize, const uint8_t *new,
    size_t newsize, size_t st, size_t en, size_t *pos)
{
	size_t x, y;

	if (en - st < 2) {
		x = matchlen(old + I[st], oldsize - I[st], new, newsize);
		y = matchlen(old + I[en], oldsize - I[en], new, newsize);

		if (x > y) {
			*pos = I[st];
			return x;
		} else {
			*pos = I[en];
			return y;
		}
	}

	x = st + (en - st)/2;
	if (memcmp(old + I[x], new, MIN(oldsize - I[x], newsize)) < 0) {
		return search(I, old, oldsize, new, newsize, x, en, pos);
	} else {
		return search(I, old, oldsize, new, newsize, st, x, pos);
	}
}

BSDIFF_ALIGNMENT
bsdiff_align(const uint8_t * new, size_t newsize,
    const uint8_t * old, size_t oldsize)
{
	size_t *I;
	BSDIFF_ALIGNMENT A;
	struct bsdiff_alignseg aseg;
	struct bsdiff_alignseg * asegp, * asegp2;
	size_t scan, pos, len;
	size_t lastoffset;
	size_t oldscore, scsc;
	size_t alenmax, nposmin;
	size_t s;
	size_t i, j, k;

	/* Suffix sort the old file. */
	if ((I = sufsort_qsufsort(old, oldsize)) == NULL)
		err(1, NULL);

	/* Initialize empty alignment array. */
	if ((A = bsdiff_alignment_init(0)) == NULL)
		err(1, NULL);

	/*
	 * We have no "last offset", so set a value of lastoffset such that
	 * in the loop below we'll never think that the last offset matches at
	 * the byte being considered.
	 */
	lastoffset = oldsize;

	/* Scan through new, constructing an alignment against old. */
	for (scan = 0; scan < newsize; scan += len) {
		/*
		 * Look for the next values where new[scan .. scan + len - 1]
		 * matches old[pos .. pos + len - 1] exactly but they mismatch
		 * old[scan + lastoffset .. scan + lastoffset + len - 1] in at
		 * least 8 bytes.
		 */
		for (oldscore = 0, scsc = scan; scan < newsize; scan++) {
			/*
			 * Find the position in the old string where the string
			 * new[scan .. newsize - 1] matches best.
			 */
			len = search(I, old, oldsize, new + scan, newsize-scan,
			    0, oldsize, &pos);

			/*
			 * Increment oldscore for every byte between scsc and
			 * scan + len which matches with our previous offset.
			 */
			for( ; scsc < scan + len; scsc++)
				if((scsc + lastoffset < oldsize) &&
				    (old[scsc + lastoffset] == new[scsc]))
					oldscore++;

			/*
			 * If the old offset matches for the entire length of
			 * the alignment and that length is non-zero (i.e.,
			 * the old offset is one of several optimal alignments
			 * at this position), continue looking from the end of
			 * the matched region.
			 */
			if ((len == oldscore) && (len != 0))
				break;

			/*
			 * If the new offset matches 8 or more characters which
			 * the previous offset didn't match, record this as a
			 * new alignment segment.
			 */
			if (len > oldscore + 8) {
				aseg.alen = len;
				aseg.npos = scan;
				aseg.opos = pos;
				if (bsdiff_alignment_append(A, &aseg, 1))
					err(1, NULL);
				lastoffset = pos - scan;
				break;
			}

			/*
			 * Decrement oldscore if the byte at position scan
			 * matches using the old offset.  This maintains the
			 * invariant that upon entering the loop oldscore is
			 * equal to the number of bytes in new[scan .. scsc - 1]
			 * which match using the old offset.
			 */
			if ((scan + lastoffset < oldsize) &&
			    (old[scan + lastoffset] == new[scan]))
				oldscore--;
		}
	}

	/*
	 * Delete alignments which aren't much better than their successors.
	 * The selection of segments above (using oldscore) ensures that each
	 * segment matches at least 8 bytes which don't match at the previous
	 * offset; now we turn around and apply that requirement in the
	 * opposite direction.
	 */
#if 0
	for (k = j = 0; j + 1< bsdiff_alignment_getsize(A); j++) {
		asegp = bsdiff_alignment_get(A, k);
		asegp2 = bsdiff_alignment_get(A, j + 1);

		/* Always keep the first alignment; it's an anchor. */
		if (k == 0) {
			asegp = bsdiff_alignment_get(A, ++k);
			memcpy(asegp, asegp2, sizeof(*asegp));
			continue;
		}

		/* If the new alignment doesn't fit, keep the old one. */
		if (asegp->npos + asegp2->opos < asegp2->npos) {
			asegp = bsdiff_alignment_get(A, ++k);
			memcpy(asegp, asegp2, sizeof(*asegp));
			continue;
		}

		/* Count matches within segment k using j's alignment. */
		lastoffset = asegp2->opos - asegp2->npos;
		for (s = i = 0; i < asegp->alen; i++) {
			if (old[asegp->npos + i + lastoffset] ==
			    new[asegp->npos + i])
				s++;
		}

		/* If more than 8 mismatches, keep this alignment segment. */
		if (s + 8 < asegp->alen) {
			asegp = bsdiff_alignment_get(A, ++k);
			memcpy(asegp, asegp2, sizeof(*asegp));
			continue;
		}

		/* Replace this segment with an extension of the next one. */
		asegp->alen = asegp2->npos + asegp2->alen - asegp->npos;
		asegp->opos = asegp->npos + asegp2->opos - asegp2->npos;
	}
	bsdiff_alignment_shrink(A, j - k);
#endif

	/* Scan through alignments extending them forwards. */
	for (j = 0; j < bsdiff_alignment_getsize(A); j++) {
		asegp = bsdiff_alignment_get(A, j);

		/* What's the furthest we could go? */
		if (j + 1 < bsdiff_alignment_getsize(A))
			alenmax = bsdiff_alignment_get(A, j + 1)->npos -
			    asegp->npos;
		else
			alenmax = newsize - asegp->npos;
		if (asegp->opos + alenmax > oldsize)
			alenmax = oldsize - asegp->opos;

		/* Extend as long as we match at least 50%. */
		s = 0;
		for (i = asegp->alen; i < alenmax; ) {
			if (old[asegp->opos + i] == new[asegp->npos + i])
				s++;
			i++;
			if (s * 2 > i - asegp->alen) {
				s = 0;
				asegp->alen = i;
			}
		}
	}

	/* Extend alignments backwards, resolving any resulting overlaps. */
	for (j = 0; j + 1 < bsdiff_alignment_getsize(A); j++) {
		asegp = bsdiff_alignment_get(A, j);
		asegp2 = bsdiff_alignment_get(A, j + 1);

		/* How far back can we go? */
		if (j > 0)
			nposmin = asegp->npos;
		else
			nposmin = 0;
		if (nposmin + asegp2->opos < asegp2->npos)
			nposmin = asegp2->npos - asegp2->opos;

		/* Extend as long as we match at least 50%. */
		s = 0;
		for (i = asegp2->npos; i > nposmin; ) {
			if (old[asegp2->opos - asegp2->npos + i - 1] ==
			    new[i - 1])
				s++;
			i--;
			if (s * 2 + i > asegp2->npos) {
				asegp2->alen += asegp2->npos - i;
				asegp2->opos -= asegp2->npos - i;
				asegp2->npos = i;
				s = 0;
			}
		}

		/* If the extended alignments don't overlap, move on. */
		if (asegp->npos + asegp->alen <= asegp2->npos)
			continue;

		/* Find the optimal position to split. */
		s = 0;
		for (i = asegp->npos + asegp->alen; i > asegp2->npos; ) {
			if (old[i - 1 + (asegp->opos - asegp->npos)] != new[i - 1])
				s++;
			if (old[i - 1 + (asegp2->opos - asegp2->npos)] == new[i - 1])
				s++;
			i--;
			if (i + s >= asegp->npos + asegp->alen) {
				asegp->alen = i - asegp->npos;
				s = 0;
			}
		}
		asegp2->alen -= (asegp->npos + asegp->alen) - asegp2->npos;
		asegp2->opos += (asegp->npos + asegp->alen) - asegp2->npos;
		asegp2->npos = asegp->npos + asegp->alen;
	}

	/* Delete any alignment segments which are now empty. */
	for (k = j = 0; j + 1< bsdiff_alignment_getsize(A); j++) {
		asegp = bsdiff_alignment_get(A, k);
		asegp2 = bsdiff_alignment_get(A, j + 1);

		/* Keep if non-empty. */
		if (asegp->alen)
			k++;

		/* Copy next segment into place. */
		memcpy(bsdiff_alignment_get(A, k), asegp2, sizeof(*asegp));
	}
	bsdiff_alignment_shrink(A, j - k);

	/* Warn about any bad alignment lengths */
	for (j = 0; j < bsdiff_alignment_getsize(A); j++) {
		asegp = bsdiff_alignment_get(A, j);
		if (asegp->alen == 0)
			warnp("bsdiff_align: A[%lu] asegp->alen is zero");
		else if (asegp->alen > (0x8000000000000000L))
			/* Larger than 2^63 */
			warnp("bsdiff_align: A[%lu] huge segment asegp->alen %lu",
				  j, asegp->alen);
	}

	/* Free the suffix array. */
	free(I);

	/* Success! */
	return (A);

	/* Failure! */
	return (NULL);
}
