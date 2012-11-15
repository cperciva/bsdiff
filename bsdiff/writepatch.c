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

#include <bzlib.h>
#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sysendian.h"
#include "warnp.h"

#include "alignment.h"

#include "writepatch.h"

/* Write to a BZ2-encoded stream. */
static int
bzwrite(BZFILE * fbz2, const uint8_t * buf, size_t len)
{
	void * _buf;
	int bz2err;

	/*
	 * The bz2 library API is broken, in that it takes a write buffer as a
	 * "void *" instead of a "const void *".  Since we're writing const
	 * buffers, we need to "de-const-ify" the pointer.
	 */
	_buf = (void *)(intptr_t)(buf);

	/* Perform the write. */
	BZ2_bzWrite(&bz2err, fbz2, _buf, len);

	/* Check for error. */
	if (bz2err != BZ_OK) {
		warn0("BZ2_bzWrite failed: %d", bz2err);
		return (-1);
	} else
		return (0);
}

/* Encode an int64_t as a sequence of 8 bytes. */
static void
encval(int64_t x, uint8_t buf[8])
{
	uint64_t y;

	/* Convert from 2's-complement to sign-magnitude. */
	y = x;
	if (y & ((uint64_t)(1) << 63)) {
		y = (~y) + 1;
		y |= ((uint64_t)(1) << 63);
	}

	/* Encode value. */
	le64enc(buf, y);
}

/* Write an encoded int64_t to the bz2 stream. */
static int
writeval(BZFILE * fbz2, int64_t val)
{
	uint8_t buf[8];

	/* Expand the value into a buffer. */
	encval(val, buf);

	/* Write it out. */
	return (bzwrite(fbz2, buf, 8));
}

/* Append the (compressed) control block to the the file. */
static int
writectrl(FILE * f, ALIGNMENT A, size_t newsize)
{
	struct alignseg * asegp;
	BZFILE * fbz2;
	size_t opos, npos;
	size_t i;
	int bz2err;

	/* Start writing control tuples. */
	if ((fbz2 = BZ2_bzWriteOpen(&bz2err, f, 9, 0, 0)) == NULL) {
		warn0("BZ2_bzWriteOpen failed: %d", bz2err);
		goto err0;
	}

	/*
	 * The control block starts with "copy X bytes from position 0 in the
	 * old file to position 0 in the new file".  If we have a first
	 * alignment segment and it aligns position 0 to position 0, this is
	 * great -- we can emit the number of bytes to copy and move on to the
	 * next alignment segment.  Otherwise, we need to say "copy zero bytes"
	 * and start processing at the start of the alignment list.
	 */
	if ((alignment_getsize(A) > 0) &&
	    (alignment_get(A, 0)->npos == 0) &&
	    (alignment_get(A, 0)->opos == 0)) {
		asegp = alignment_get(A, 0);
		if (writeval(fbz2, asegp->alen))
			goto err1;
		npos = opos = asegp->alen;
		i = 1;
	} else {
		if (writeval(fbz2, 0))
			goto err1;
		npos = opos = 0;
		i = 0;
	}

	/* Read through the alignment array, processing segments. */
	for ( ; i < alignment_getsize(A); i++) {
		asegp = alignment_get(A, i);

		/* Extra length is the gap before this segment starts. */
		if (writeval(fbz2, asegp->npos - npos))
			goto err1;

		/* Seek length is the difference between positions in old. */
		if (writeval(fbz2, asegp->opos - opos))
			goto err1;

		/* Diff length is the length of the aligned region. */
		if (writeval(fbz2, asegp->alen))
			goto err1;

		/* Update our pointers within the two files. */
		npos = asegp->npos + asegp->alen;
		opos = asegp->opos + asegp->alen;
	}

	/* Extra length is the rest up to the end of the file. */
	if (writeval(fbz2, newsize - npos))
		goto err1;

	/* Seek length is zero; no point seeking after we're finished. */
	if (writeval(fbz2, 0))
		goto err1;

	/* We've finished writing control tuples. */
	BZ2_bzWriteClose(&bz2err, fbz2, 0, NULL, NULL);
	if (bz2err != BZ_OK) {
		warn0("BZ2_bzWriteClose failed: %d", bz2err);
		goto err0;
	}

	/* Success! */
	return (0);

err1:
	BZ2_bzWriteClose(&bz2err, fbz2, 0, NULL, NULL);
err0:
	/* Failure! */
	return (-1);
}

/* Write a segment of diff. */
static int
writediffseg(BZFILE * fbz2, const uint8_t * new, const uint8_t * old,
    size_t len)
{
	uint8_t buf[4096];
	size_t i;

	/* Loop until we've written everything. */
	while (len > 0) {
		/* Diff until we fill the buffer or run out of data. */
		for (i = 0; i < 4096 && len > 0; i++, len--)
			buf[i] = *new++ - *old++;

		/* Write out the diffed data. */
		if (bzwrite(fbz2, buf, i))
			goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/* Append the (compressed) diff block to the the file. */
static int
writediff(FILE * f, ALIGNMENT A, const uint8_t * new, const uint8_t * old)
{
	struct alignseg * asegp;
	BZFILE * fbz2;
	size_t i;
	int bz2err;

	/* Start writing diff segments. */
	if ((fbz2 = BZ2_bzWriteOpen(&bz2err, f, 9, 0, 0)) == NULL) {
		warn0("BZ2_bzWriteOpen failed: %d", bz2err);
		goto err0;
	}

	/* Write segments one by one. */
	for (i = 0; i < alignment_getsize(A); i++) {
		asegp = alignment_get(A, i);

		if (writediffseg(fbz2, &new[asegp->npos], &old[asegp->opos],
		    asegp->alen))
			goto err1;
	}

	/* We've finished writing the diff block. */
	BZ2_bzWriteClose(&bz2err, fbz2, 0, NULL, NULL);
	if (bz2err != BZ_OK) {
		warn0("BZ2_bzWriteClose failed: %d", bz2err);
		goto err0;
	}

	/* Success! */
	return (0);

err1:
	BZ2_bzWriteClose(&bz2err, fbz2, 0, NULL, NULL);
err0:
	/* Failure! */
	return (-1);
}

/* Append the (compressed) extra block to the file. */
static int
writeextra(FILE * f, ALIGNMENT A, const uint8_t * new, size_t newsize)
{
	struct alignseg * asegp;
	BZFILE * fbz2;
	size_t npos;
	size_t i;
	int bz2err;

	/* Start writing extra segments. */
	if ((fbz2 = BZ2_bzWriteOpen(&bz2err, f, 9, 0, 0)) == NULL) {
		warn0("BZ2_bzWriteOpen failed: %d", bz2err);
		goto err0;
	}

	/* Write segments one by one. */
	npos = 0;
	for (i = 0; i < alignment_getsize(A); i++) {
		asegp = alignment_get(A, i);

		/* Write data up to the start of the next aligned section. */
		if (bzwrite(fbz2, &new[npos], asegp->npos - npos))
			goto err1;

		/* The next unaligned section starts here. */
		npos = asegp->npos + asegp->alen;
	}

	/* Write extra data from the end of the last aligned section to EOF. */
	BZ2_bzWrite(&bz2err, fbz2, (void *)(uintptr_t)&new[npos],
	    newsize - npos);
	if (bz2err != BZ_OK) {
		warn0("BZ2_bzWrite failed: %d", bz2err);
		goto err1;
	}

	/* We've finished writing the extra block. */
	BZ2_bzWriteClose(&bz2err, fbz2, 0, NULL, NULL);
	if (bz2err != BZ_OK) {
		warn0("BZ2_bzWriteClose failed: %d", bz2err);
		goto err0;
	}

	/* Success! */
	return (0);

err1:
	BZ2_bzWriteClose(&bz2err, fbz2, 0, NULL, NULL);
err0:
	/* Failure! */
	return (-1);
}

/**
 * writepatch(name, A, new, newsize, old):
 * Write a patch with the specified name based on the alignment A of the new
 * data new[0 .. newsize - 1] with the old data old[].
 */
void
writepatch(const char * name, ALIGNMENT A, const uint8_t * new, size_t newsize,
    const uint8_t * old)
{
	size_t len;
	size_t flen;
	uint8_t header[32];
	FILE * f;

	/* Open the patch file for writing. */
	if ((f = fopen(name, "wb")) == NULL)
		err(1, "%s", name);

	/* Header is
		0	8	 "BSDIFF40"
		8	8	length of bzip2ed ctrl block
		16	8	length of bzip2ed diff block
		24	8	length of new file */
	/* File is
		0	32	Header
		32	??	Bzip2ed ctrl block
		??	??	Bzip2ed diff block
		??	??	Bzip2ed extra block */
	memcpy(header, "BSDIFF40", 8);
	encval(0, header + 8);
	encval(0, header + 16);
	encval(newsize, header + 24);
	if (fwrite(header, 32, 1, f) != 1)
		err(1, "fwrite(%s)", name);

	/* Write control block. */
	if (writectrl(f, A, newsize))
		exit(1);

	/* Compute size of compressed ctrl data */
	if ((len = ftello(f)) == (size_t)(-1))
		err(1, "ftello");
	encval(len - 32, header + 8);

	/* Write diff block. */
	if (writediff(f, A, new, old))
		exit(1);

	/* Compute size of compressed diff data */
	if ((flen = ftello(f)) == (size_t)(-1))
		err(1, "ftello");
	encval(flen - len, header + 16);

	/* Write extra block. */
	if (writeextra(f, A, new, newsize))
		exit(1);

	/* Seek to the beginning, write the header, and close the file */
	if (fseeko(f, 0, SEEK_SET))
		err(1, "fseeko");
	if (fwrite(header, 32, 1, f) != 1)
		err(1, "fwrite(%s)", name);
	if (fclose(f))
		err(1, "fclose");
}
