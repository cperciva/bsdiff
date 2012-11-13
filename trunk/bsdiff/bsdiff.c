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
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "align.h"
#include "alignment.h"
#include "mapfile.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

static void
offtout(int64_t x, uint8_t *buf)
{
	uint64_t y;

	if (x < 0)
		y = -x;
	else
		y = x;

		buf[0]=y%256;y-=buf[0];
	y=y/256;buf[1]=y%256;y-=buf[1];
	y=y/256;buf[2]=y%256;y-=buf[2];
	y=y/256;buf[3]=y%256;y-=buf[3];
	y=y/256;buf[4]=y%256;y-=buf[4];
	y=y/256;buf[5]=y%256;y-=buf[5];
	y=y/256;buf[6]=y%256;y-=buf[6];
	y=y/256;buf[7]=y%256;

	if (x<0)
		buf[7] |= 0x80;
}

int
main(int argc, char *argv[])
{
	int oldfd, newfd;
	uint8_t *old, *new;
	size_t oldsize, newsize;
	ALIGNMENT A;
	struct alignseg * asegp, * asegp2;
	size_t len;
	size_t lastscan, lastpos, lastoffset;
	size_t i, j;
	size_t dblen, eblen;
	uint8_t *db = NULL, *eb = NULL;
	uint8_t buf[24];
	uint8_t header[32];
	FILE * pf;
	BZFILE * pfbz2;
	int bz2err;

	if (argc != 4)
		errx(1, "usage: %s oldfile newfile patchfile\n", argv[0]);

	/* Map the old file into memory. */
	if ((old = mapfile(argv[1], &oldfd, &oldsize)) == NULL)
		err(1, "Cannot map file: %s", argv[1]);

	/* Map the new file into memory. */
	if ((new = mapfile(argv[2], &newfd, &newsize)) == NULL)
		err(1, "Cannot map file: %s", argv[2]);

	/* Compute an alignment of the two files. */
	if ((A = align(new, newsize, old, oldsize)) == NULL)
		err(1, "Error aligning files");

	/* Create the patch file */
	if ((pf = fopen(argv[3], "wb")) == NULL)
		err(1, "%s", argv[3]);

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
	offtout(0, header + 8);
	offtout(0, header + 16);
	offtout(newsize, header + 24);
	if (fwrite(header, 32, 1, pf) != 1)
		err(1, "fwrite(%s)", argv[3]);

	/* Start writing control tuples. */
	if ((pfbz2 = BZ2_bzWriteOpen(&bz2err, pf, 9, 0, 0)) == NULL)
		errx(1, "BZ2_bzWriteOpen, bz2err = %d", bz2err);

	/* Read through the alignment array, writing out control tuples. */
	lastscan = 0;
	lastpos = 0;
	lastoffset = 0;
	for (i = 0; i + 1 < alignment_getsize(A); i++) {
		asegp = alignment_get(A, i);
		asegp2 = alignment_get(A, i + 1);

		/* Diff length is the length of the aligned region. */
		offtout(asegp->alen, &buf[0]);

		/* Extra length is the gap between this and the next region. */
		offtout(asegp2->npos - (asegp->npos + asegp->alen), &buf[8]);

		/* Seek length is the difference between positions in old. */
		offtout(asegp2->opos - (asegp->opos + asegp->alen), &buf[16]);

		BZ2_bzWrite(&bz2err, pfbz2, buf, 24);
		if (bz2err != BZ_OK)
			errx(1, "BZ2_bzWrite, bz2err = %d", bz2err);
	}

	/* We've finished writing control tuples. */
	BZ2_bzWriteClose(&bz2err, pfbz2, 0, NULL, NULL);
	if (bz2err != BZ_OK)
		errx(1, "BZ2_bzWriteClose, bz2err = %d", bz2err);

	/* Compute size of compressed ctrl data */
	if ((len = ftello(pf)) == (size_t)(-1))
		err(1, "ftello");
	offtout(len - 32, header + 8);

	/* Construct diff block. */
	for (dblen = i = 0; i + 1 < alignment_getsize(A); i++) {
		asegp = alignment_get(A, i);
		dblen += asegp->alen;
	}
	if (dblen > 0) {
		/* Allocate memory for the block. */
		if ((db = malloc(dblen)) == NULL)
			err(1, NULL);

		/* Construct it one segment at a time. */
		for (dblen = i = 0; i + 1 < alignment_getsize(A); i++) {
			asegp = alignment_get(A, i);
			for (j = 0; j < asegp->alen; j++)
				db[dblen + j] = new[asegp->npos + j] -
				    old[asegp->opos + j];
			dblen += asegp->alen;
		}
	}

	/* Write compressed diff data */
	if ((pfbz2 = BZ2_bzWriteOpen(&bz2err, pf, 9, 0, 0)) == NULL)
		errx(1, "BZ2_bzWriteOpen, bz2err = %d", bz2err);
	if (dblen > 0)
		BZ2_bzWrite(&bz2err, pfbz2, db, dblen);
	if (bz2err != BZ_OK)
		errx(1, "BZ2_bzWrite, bz2err = %d", bz2err);
	BZ2_bzWriteClose(&bz2err, pfbz2, 0, NULL, NULL);
	if (bz2err != BZ_OK)
		errx(1, "BZ2_bzWriteClose, bz2err = %d", bz2err);

	/* Compute size of compressed diff data */
	if ((newsize = ftello(pf)) == (size_t)(-1))
		err(1, "ftello");
	offtout(newsize - len, header + 16);

	/* Construct extra block. */
	for (eblen = i = 0; i + 1 < alignment_getsize(A); i++) {
		asegp = alignment_get(A, i);
		asegp2 = alignment_get(A, i + 1);
		eblen += asegp2->npos - (asegp->npos + asegp->alen);
	}
	if (eblen > 0) {
		/* Allocate memory for the block. */
		if ((eb = malloc(eblen)) == NULL)
			err(1, NULL);

		/* Construct it one segment at a time. */
		for (eblen = i = 0; i + 1 < alignment_getsize(A); i++) {
			asegp = alignment_get(A, i);
			asegp2 = alignment_get(A, i + 1);
			for (j = asegp->npos + asegp->alen; j < asegp2->npos; )
				eb[eblen++] = new[j++];
		}
	}

	/* Write compressed extra data */
	if ((pfbz2 = BZ2_bzWriteOpen(&bz2err, pf, 9, 0, 0)) == NULL)
		errx(1, "BZ2_bzWriteOpen, bz2err = %d", bz2err);
	if (eblen > 0)
		BZ2_bzWrite(&bz2err, pfbz2, eb, eblen);
	if (bz2err != BZ_OK)
		errx(1, "BZ2_bzWrite, bz2err = %d", bz2err);
	BZ2_bzWriteClose(&bz2err, pfbz2, 0, NULL, NULL);
	if (bz2err != BZ_OK)
		errx(1, "BZ2_bzWriteClose, bz2err = %d", bz2err);

	/* Seek to the beginning, write the header, and close the file */
	if (fseeko(pf, 0, SEEK_SET))
		err(1, "fseeko");
	if (fwrite(header, 32, 1, pf) != 1)
		err(1, "fwrite(%s)", argv[3]);
	if (fclose(pf))
		err(1, "fclose");

	/* Free the memory we used */
	alignment_free(A);
	free(db);
	free(eb);

	/* Release memory mappings. */
	unmapfile(new, newfd, newsize);
	unmapfile(old, oldfd, oldsize);

	return 0;
}
