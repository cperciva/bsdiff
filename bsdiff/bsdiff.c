/*-
 * Copyright 2003-2005 Colin Percival
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

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <bzlib.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elasticarray.h"
#include "qsufsort.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/* Alignment segment. */
struct alignseg {
	uint64_t npos;
	uint64_t opos;
	uint64_t alen;
};
ELASTICARRAY_DECL(ALIGNMENT, alignment, struct alignseg);

static size_t
matchlen(uint8_t *old, size_t oldsize, uint8_t *new, size_t newsize)
{
	size_t i;

	for (i = 0; (i < oldsize) && (i < newsize); i++)
		if (old[i] != new[i])
			break;

	return i;
}

static size_t
search(size_t *I, uint8_t *old, size_t oldsize, uint8_t *new, size_t newsize,
    size_t st, size_t en, size_t *pos)
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

/**
 * mapfile(name, fd, len):
 * Open the file ${name} and map it into memory.  Set ${fd} to the file
 * descriptor and ${len} to the file length, and return a pointer to the
 * mapped data.
 */
static void *
mapfile(const char * name, int * fd, size_t * len)
{
	struct stat sb;
	void * ptr;
	int d;

	/* Open the file for reading. */
	if ((d = open(name, O_RDONLY)) == -1)
		goto err0;

	/* Stat the file and make sure it's not too big. */
	if (fstat(d, &sb))
		goto err1;
	if ((sb.st_size < 0) ||
	    (uintmax_t)(sb.st_size) > (uintmax_t)(SIZE_MAX)) {
		errno = EFBIG;
		goto err1;
	}

	/* Map the file into memory. */
	if ((ptr = mmap(NULL, sb.st_size, PROT_READ, 0, d, 0)) == MAP_FAILED)
		goto err1;

	/* Return the descriptor, length, and pointer. */
	*fd = d;
	*len = sb.st_size;
	return (ptr);

err1:
	close(d);
err0:
	/* Failure! */
	return (NULL);
}

/**
 * unmapfile(ptr, fd, len):
 * Tear down the file mapping created by mapfile.
 */
static int
unmapfile(void * ptr, int fd, size_t len)
{
	int rc = 0;

	/* Only unmap non-zero lengths -- POSIX stupidity. */
	if (len > 0) {
		if (munmap(ptr, len))
			rc = -1;
	}

	/* Close the file. */
	if (close(fd))
		rc = -1;

	/* Return status code. */
	return (rc);
}

int
main(int argc, char *argv[])
{
	int oldfd, newfd;
	uint8_t *old, *new;
	size_t oldsize, newsize;
	size_t *I;
	ALIGNMENT A;
	struct alignseg aseg;
	struct alignseg * asegp, * asegp2;
	size_t scan, pos, len;
	size_t lastscan, lastpos, lastoffset;
	size_t oldscore, scsc;
	size_t s, Sf, lenf, Sb, lenb;
	size_t overlap, Ss, lens;
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

	/* Suffix sort the old file. */
	if ((I = qsufsort(old, oldsize)) == NULL)
		err(1, NULL);

	/* Map the new file into memory. */
	if ((new = mapfile(argv[2], &newfd, &newsize)) == NULL)
		err(1, "Cannot map file: %s", argv[2]);

	/* Initialize empty alignment array. */
	if ((A = alignment_init(0)) == NULL)
		err(1, NULL);

	/* Scan through new, constructing an alignment against old. */
	scan = 0;
	len = 0;
	pos = 0;
	lastscan = 0;
	lastpos = 0;
	lastoffset = 0;
	while (scan < newsize) {
		/* Push this segment onto our alignment array. */
		aseg.alen = len;
		aseg.npos = scan;
		aseg.opos = pos;
		if (alignment_append(A, &aseg, 1))
			err(1, NULL);

		lastoffset = pos - scan;
		oldscore = 0;

		/*
		 * Look for the next values where new[scan .. scan + len - 1]
		 * matches old[pos .. pos + len - 1] exactly but they mismatch
		 * old[scan + lastoffset .. scan + lastoffset + len - 1] in at
		 * least 8 bytes.
		 */
		for (scsc = scan += len; scan < newsize; scan++) {
searchagain:
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
			if ((len == oldscore) && (len != 0)) {
				scan += len;
				scsc = scan;
				oldscore = 0;
				goto searchagain;
			}

			/*
			 * If the new offset matches 8 or more characters which
			 * the previous offset didn't match, exit the loop.
			 */
			if (len > oldscore + 8)
				break;

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

	/* Push a final segment onto our alignment array. */
	aseg.alen = 0;
	aseg.npos = scan;
	aseg.opos = pos;
	if (alignment_append(A, &aseg, 1))
		err(1, NULL);

	/* Scan through the alignments, extending them into gaps. */
	for (j = 0; j + 1 < alignment_getsize(A); j++) {
		asegp = alignment_get(A, j);
		asegp2 = alignment_get(A, j + 1);

		lastscan = asegp->npos;
		lastpos = asegp->opos;
		lenf = asegp->alen;
		scan = asegp2->npos;
		pos = asegp2->opos;

		/* Extend asegp forwards as long as it matches 50%. */
		s = 0;
		Sf = 0;
		for (i = lenf; (lastscan + i < scan) &&
		    (lastpos + i < oldsize); ) {
			if (old[lastpos + i] == new[lastscan + i])
				s++;
			i++;
			if (s * 2 + lenf > Sf * 2 + i) {
				Sf = s;
				lenf = i;
			}
		}

		/* Extend asegp2 backwards as long as it matches 50%. */
		lenb = 0;
		if (scan < newsize) {
			s = 0;
			Sb = 0;
			for (i = 1; (scan >= lastscan + i) &&
			    (pos >= i); i++) {
				if (old[pos - i] == new[scan - i])
					s++;
				if (s * 2 + lenb > Sb * 2 + i) {
					Sb = s;
					lenb = i;
				}
			}
		}

		/* If the extended alignments overlap, find the best split. */
		if (lastscan + lenf > scan - lenb) {
			overlap = (lastscan + lenf) - (scan - lenb);
			s = 0;
			Ss = 0;
			lens = 0;
			for (i = 0; i < overlap; i++) {
				if (new[lastscan + lenf - overlap + i] ==
				    old[lastpos + lenf - overlap + i])
					s++;
				if (new[scan - lenb + i] ==
				    old[pos - lenb + i])
					s--;
				if ((ssize_t)s > (ssize_t)Ss) {
					Ss = s;
					lens = i + 1;
				}
			}

			lenf += lens - overlap;
			lenb -= lens;
		}

		/* Adjust the regions. */
		asegp->alen = lenf;
		asegp2->alen += lenb;
		asegp2->npos -= lenb;
		asegp2->opos -= lenb;
	}

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
	free(I);

	/* Release memory mappings. */
	unmapfile(new, newfd, newsize);
	unmapfile(old, oldfd, oldsize);

	return 0;
}
