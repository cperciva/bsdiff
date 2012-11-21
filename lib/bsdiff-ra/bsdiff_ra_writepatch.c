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

#include <assert.h>
#include <bzlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdiff_alignment.h"
#include "sysendian.h"
#include "warnp.h"

#include "bsdiff_ra_writepatch.h"

struct seghdr {
	size_t ostart;
	size_t olen;
	size_t plen;
};

/* Encode a sign-magnitude 32-bit integer. */
static void
encval(uint8_t buf[4], int32_t x)
{
	uint32_t y;

	/* Convert from 2's-complement to sign-magnitude. */
	y = x;
	if (y & ((uint32_t)(1) << 31)) {
		y = (~y) + 1;
		y |= ((uint32_t)(1) << 31);
	}

	/* Encode value. */
	be32enc(buf, y);
}

/* Compress a block and return a pointer. */
static uint8_t *
acompress(const uint8_t * in, size_t inlen, size_t * outlen)
{
	uint8_t * out;
	unsigned int olen;
	int bz2err;

	/* Allocate out buffer. */
	olen = inlen + inlen / 100 + 600;
	if ((out = malloc(olen)) == NULL)
		goto err0;

	/* Perform the compression. */
	if ((bz2err = BZ2_bzBuffToBuffCompress(out, &olen,
	    (char *)(uintptr_t)(in), inlen, 9, 0, 0)) != BZ_OK) {
		warn0("BZ2_bzBuffToBuffCompress error: %d", bz2err);
		goto err1;
	}

	/* Success! */
	*outlen = olen;
	return (out);

err1:
	free(out);
err0:
	/* Failure! */
	return (NULL);
}

/* Construct and write a patch data segment and record its length. */
static int
writeseg(FILE * f, BSDIFF_ALIGNMENT A, const uint8_t * new, size_t newsize,
    const uint8_t * old, size_t * plen)
{
	uint8_t hbuf[16];
	struct bsdiff_alignseg * asegp;
	uint8_t * ctrl, * diff, * extra;
	uint8_t * ctrlc, * diffc, * extrac;
	size_t ctrllen, difflen, extralen;
	size_t ctrllenc, difflenc, extralenc;
	size_t ctrlpos, diffpos, extrapos, npos, opos;
	size_t i, j;

	/*
	 * If new[0] is aligned, we have one ctrl tuple for each alignment
	 * segment (a copy-and-add region followed by an insert region).  If
	 * not, we have an extra ctrl tuple to say "zero bytes of diff and X
	 * bytes of extra" first.
	 */
	if ((bsdiff_alignment_getsize(A) > 0) &&
	    (bsdiff_alignment_get(A, 0)->npos == 0))
		ctrllen = bsdiff_alignment_getsize(A) * 12;
	else
		ctrllen = (bsdiff_alignment_getsize(A) + 1) * 12;

	/* The diff length is the sum of the lengths of aligned regions. */
	for (difflen = i = 0; i < bsdiff_alignment_getsize(A); i++)
		difflen += bsdiff_alignment_get(A, i)->alen;

	/* The extra length is what's left. */
	extralen = newsize - difflen;

	/* Allocate buffers for the ctrl, diff, and extra blocks. */
	if (((ctrl = malloc(ctrllen)) == NULL) && (ctrllen > 0))
		goto err0;
	if (((diff = malloc(difflen)) == NULL) && (difflen > 0))
		goto err1;
	if (((extra = malloc(extralen)) == NULL) && (extralen > 0))
		goto err2;

	/* Construct ctrl block. */
	ctrlpos = npos = opos = 0;
	if ((bsdiff_alignment_getsize(A) > 0) &&
	    (bsdiff_alignment_get(A, 0)->npos == 0)) {
		asegp = bsdiff_alignment_get(A, 0);

		/* Seek offset. */
		encval(&ctrl[ctrlpos], asegp->opos - opos);
		ctrlpos += 4;
		opos = asegp->opos;

		/* Diff length. */
		be32enc(&ctrl[ctrlpos], asegp->alen);
		ctrlpos += 4;
		opos += asegp->alen;
		npos += asegp->alen;

		/* We've handled this segment. */
		i = 1;
	} else {
		/* Seek zero bytes. */
		be32enc(&ctrl[ctrlpos], 0);
		ctrlpos += 4;

		/* Diff zero bytes. */
		be32enc(&ctrl[ctrlpos], 0);
		ctrlpos += 4;

		/* We need to handle the first segment. */
		i = 0;
	}

	/* Handle segments one by one. */
	for ( ; i < bsdiff_alignment_getsize(A); i++) {
		asegp = bsdiff_alignment_get(A, i);

		/* Extra length. */
		be32enc(&ctrl[ctrlpos], asegp->npos - npos);
		ctrlpos += 4;
		npos = asegp->npos;

		/* Seek length. */
		encval(&ctrl[ctrlpos], asegp->opos - opos);
		ctrlpos += 4;
		opos = asegp->opos;

		/* Diff length. */
		be32enc(&ctrl[ctrlpos], asegp->alen);
		ctrlpos += 4;
		opos += asegp->alen;
		npos += asegp->alen;
	}

	/* What's left is all extra. */
	be32enc(&ctrl[ctrlpos], newsize - npos);
	ctrlpos += 4;

	/* Sanity-check. */
	assert(ctrlpos == ctrllen);

	/* Construct diff block. */
	diffpos = 0;
	for (i = 0; i < bsdiff_alignment_getsize(A); i++) {
		asegp = bsdiff_alignment_get(A, i);

		/* Diff byte-by-byte. */
		for (j = 0; j < asegp->alen; j++)
			diff[diffpos++] = new[asegp->npos + j] -
			    old[asegp->opos + j];
	}

	/* Sanity-check. */
	assert(diffpos == difflen);

	/* Construct extra block. */
	extrapos = npos = 0;
	for (i = 0; i < bsdiff_alignment_getsize(A); i++) {
		asegp = bsdiff_alignment_get(A, i);

		/* Copy block. */
		memcpy(&extra[extrapos], &new[npos], asegp->npos - npos);
		extrapos += asegp->npos - npos;
		npos = asegp->npos + asegp->alen;
	}
	memcpy(&extra[extrapos], &new[npos], newsize - npos);
	extrapos += newsize - npos;

	/* Sanity-check. */
	assert(extrapos == extralen);

	/* Compress the three blocks. */
	if ((ctrlc = acompress(ctrl, ctrllen, &ctrllenc)) == NULL)
		goto err3;
	if ((diffc = acompress(diff, difflen, &difflenc)) == NULL)
		goto err4;
	if ((extrac = acompress(extra, extralen, &extralenc)) == NULL)
		goto err5;

	/* Construct patch data segment. */
	be32enc(&hbuf[0], ctrllenc);
	be32enc(&hbuf[4], ctrllen);
	be32enc(&hbuf[8], difflenc);
	be32enc(&hbuf[12], extralenc);

	/* Write out the patch data segment. */
	if ((fwrite(hbuf, 16, 1, f) != 1) ||
	    (fwrite(ctrlc, ctrllenc, 1, f) != 1) ||
	    (fwrite(diffc, difflenc, 1, f) != 1) ||
	    (fwrite(extrac, extralenc, 1, f) != 1)) {
		warnp("fwrite");
		goto err6;
	}

	/* Record the total size of the patch data segment. */
	*plen = 16 + ctrllenc + difflenc + extralenc;

	/* Free allocated buffers. */
	free(extrac);
	free(diffc);
	free(ctrlc);
	free(extra);
	free(diff);
	free(ctrl);

	/* Success! */
	return (0);

err6:
	free(extrac);
err5:
	free(diffc);
err4:
	free(ctrlc);
err3:
	free(extra);
err2:
	free(diff);
err1:
	free(ctrl);
err0:
	/* Failure! */
	return (-1);
}

/**
 * bsdiff_ra_writepatch(name, b, A, new, newsize, old):
 * Write a seekable patch with the specified name, using b-byte patch segments,
 * based on the alignment A of the new data new[0 .. newsize - 1] with the old
 * data old[].
 */
int
bsdiff_ra_writepatch(const char * name, size_t b, BSDIFF_ALIGNMENT A,
    const uint8_t * new, size_t newsize, const uint8_t * old)
{
	uint8_t buf[4096];
	uint8_t hbuf[32];
	BSDIFF_ALIGNMENT * SA;
	struct seghdr * SH;
	FILE * tmpf, * f;
	uint8_t * hb;
	uint8_t * hbc;
	size_t hblenc, pdblen;
	struct bsdiff_alignseg aseg;
	struct bsdiff_alignseg * asegp;
	size_t nsegs;
	size_t omin, omax;
	size_t copylen;
	size_t i, j;

	/* Sanity-check. */
	assert(b > 0);

	/* Compute the number of alignment segments. */
	nsegs = (newsize + b - 1) / b;

	/* Allocate space for sub-alignments. */
	if (((SA = malloc(nsegs * sizeof(BSDIFF_ALIGNMENT))) == NULL) &&
	    (nsegs > 0))
		goto err0;
	for (i = 0; i < nsegs; i++)
		SA[i] = NULL;

	/* Construct sub-alignments relative to new[i * b] and old[0]. */
	for (j = i = 0; i < nsegs; i++) {
		/* Initialize the sub-alignment. */
		if ((SA[i] = bsdiff_alignment_init(0)) == NULL)
			goto err1;

		/* Slice up and copy alignment segments. */
		while (j < bsdiff_alignment_getsize(A)) {
			/* Grab a pointer to the segment. */
			asegp = bsdiff_alignment_get(A, j);

			/* If this segment starts too late, stop. */
			if (asegp->npos >= (i + 1) * b)
				break;

			/*
			 * If this alignment segment ends before the section
			 * we're constructing the sub-alignment for starts,
			 * something went wrong -- we should have advanced
			 * past this segment before we got to this section of
			 * sub-aligning.
			 */
			assert(asegp->npos + asegp->alen > i * b);

			/* Copy the alignment segment. */
			aseg.npos = asegp->npos;
			aseg.opos = asegp->opos;
			aseg.alen = asegp->alen;

			/* Chop off any portion before npos = i * b. */
			if (aseg.npos < i * b) {
				aseg.alen -= i * b - aseg.npos;
				aseg.opos += i * b - aseg.npos;
				aseg.npos += i * b - aseg.npos;
			}

			/* Make relative to npos = i * b. */
			aseg.npos -= i * b;

			/* Chop off the end if it goes too far. */
			if (aseg.npos + aseg.alen > b)
				aseg.alen = b - aseg.npos;

			/* Sanity-check. */
			assert(aseg.alen > 0);

			/* Add to the sub-alignment. */
			if (bsdiff_alignment_append(SA[i], &aseg, 1))
				goto err1;

			/*
			 * If this alignment segment fits within the section
			 * we're sub-aligning, move on to the next segment;
			 * otherwise, we've finished the sub-alignment.
			 */
			if (asegp->npos + asegp->alen <= (i + 1) * b)
				j++;
			else
				break;
		}
	}

	/* Allocate array for header block data. */
	if (((SH = malloc(nsegs * sizeof(struct seghdr))) == NULL) &&
	    (nsegs > 0))
		goto err1;

	/*
	 * Fill in "old segment start" and "old segment length" values for
	 * each patch segment, and make sub-alignments relative to old[ostart].
	 */
	for (i = 0; i < nsegs; i++) {
		/* Find the minimum and maximum old positions used. */
		omin = SIZE_MAX;
		omax = 0;
		for (j = 0; j < bsdiff_alignment_getsize(SA[i]); j++) {
			asegp = bsdiff_alignment_get(SA[i], j);
			if (asegp->opos < omin)
				omin = asegp->opos;
			if (asegp->opos + asegp->alen > omax)
				omax = asegp->opos + asegp->alen;
		}

		/* If there are no segments, use [0, 0) as the range. */
		if (omin == SIZE_MAX)
			omin = 0;

		/* Record old segment start and length values. */
		SH[i].ostart = omin;
		SH[i].olen = omax - omin;

		/* Sanity-check. */
		assert(SH[i].olen <= (1 << 30));

		/* Make sub-alignment relative to ostart. */
		for (j = 0; j < bsdiff_alignment_getsize(SA[i]); j++)
			bsdiff_alignment_get(SA[i], j)->opos -= SH[i].ostart;
	}

	/* Open a temporary file for storing patch data segments. */
	if ((tmpf = tmpfile()) == NULL) {
		warnp("tmpfile");
		goto err2;
	}

	/* Generate patch data blocks and add up their lengths. */
	for (pdblen = i = 0; i < nsegs; i++) {
		if (writeseg(tmpf, SA[i], &new[i * b],
		    (i < nsegs - 1) ? b : (newsize - i * b),
		    &old[SH[i].ostart], &SH[i].plen))
			goto err3;
		pdblen += SH[i].plen;
	}

	/* Construct header block. */
	if (((hb = malloc(nsegs * 16)) == NULL) &&
	    (nsegs > 0))
		goto err3;
	for (i = 0; i < nsegs; i++) {
		be64enc(&hb[i * 16], SH[i].ostart);
		be32enc(&hb[i * 16 + 8], SH[i].olen);
		be32enc(&hb[i * 16 + 12], SH[i].plen);
	}

	/* Compress header block. */
	if ((hbc = acompress(hb, nsegs * 16, &hblenc)) == NULL)
		goto err4;

	/* Construct patch header. */
	memcpy(&hbuf[0], "BSDIFFSX", 8);
	be64enc(&hbuf[8], newsize);
	be32enc(&hbuf[16], b);
	be32enc(&hbuf[20], hblenc);
	be64enc(&hbuf[24], pdblen);

	/* Open patch file. */
	if ((f = fopen(name, "w")) == NULL) {
		warnp("fopen(%s)", name);
		goto err5;
	}

	/* Write patch header. */
	if (fwrite(hbuf, 32, 1, f) != 1) {
		warnp("failed to write patch header");
		goto err6;
	}

	/* Write compressed header block. */
	if (fwrite(hbc, hblenc, 1, f) != 1) {
		warnp("failed to write compressed header block");
		goto err6;
	}

	/* Copy patch data block. */
	if (fseek(tmpf, 0, SEEK_SET)) {
		warnp("fseek on temporary file");
		goto err6;
	}
	for (i = 0; i < pdblen; i += 4096) {
		/* Copy a buffer or whatever's left. */
		if (i + 4096 < pdblen)
			copylen = 4096;
		else
			copylen = pdblen - i;

		/* Read some data. */
		if (fread(buf, copylen, 1, tmpf) != 1) {
			warnp("failed to read patch data from temporary file");
			goto err6;
		}

		/* Write it back out. */
		if (fwrite(buf, copylen, 1, f) != 1) {
			warnp("failed to write patch data");
			goto err6;
		}
	}

	/* We've finished writing the patch. */
	if (fclose(f)) {
		warnp("fclose(%s)", name);
		goto err5;
	}

	/* Close the temporary file. */
	fclose(tmpf);

	/* Free buffers and arrays. */
	free(hbc);
	free(hb);
	free(SH);
	for (i = 0; i < nsegs; i++) {
		if (SA[i] != NULL)
			bsdiff_alignment_free(SA[i]);
	}
	free(SA);

	/* Success! */
	return (0);

err6:
	fclose(f);
err5:
	free(hbc);
err4:
	free(hb);
err3:
	fclose(tmpf);
err2:
	free(SH);
err1:
	for (i = 0; i < nsegs; i++) {
		if (SA[i] != NULL)
			bsdiff_alignment_free(SA[i]);
	}
	free(SA);
err0:
	/* Failure! */
	return (-1);
}
