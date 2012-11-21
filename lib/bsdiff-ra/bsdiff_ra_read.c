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

#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <bzlib.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sysendian.h"
#include "warnp.h"

#include "bsdiff_ra_read.h"

/* Patch reader structure. */
struct bsdiff_ra_read_file {
	int fdp;		/* Patch file. */
	int fdo;		/* Old file. */
	off_t newsize;		/* Size of new file. */
	uint32_t b;		/* Segment length. */
	struct seghdr * SH;	/* Header block. */
};

/* Segment header. */
struct seghdr {
	off_t opos;		/* Offset of old data segment start. */
	off_t ppos;		/* Offset of patch data segment start. */
	uint32_t olen;		/* Length of old data segment. */
	uint32_t plen;		/* Length of patch data segment. */
};

/* Decode a sign-magnitude 32-bit integer. */
static int32_t
decval(uint8_t buf[4])
{
	uint32_t y;

	/* Parse byte sequence. */
	y = be32dec(buf);

	/* Convert from sign-magnitude to 2's-complement. */
	if (y & ((uint32_t)(1) << 31)) {
		y &= ~ ((uint32_t)(1) << 31);
		return (-y);
	} else {
		return (y);
	}
}

/* Decompress a buffer into a buffer of the correct size. */
static int
decompress(const uint8_t * in, size_t inlen, uint8_t * out, size_t outlen)
{
	int bz2err;
	unsigned int _outlen;

	/* Perform the decompression. */
	_outlen = outlen;
	if ((bz2err = BZ2_bzBuffToBuffDecompress(out, &_outlen,
	    (uint8_t *)(uintptr_t)(in), inlen, 0, 0)) != BZ_OK) {
		warn0("error in BZ2_bzBuffToBuffDecompress: %d", bz2err);
		goto err0;
	}
	if (_outlen != outlen) {
		warn0("decompressed data is wrong size (%u, expected %zu)",
		    _outlen, outlen);
		goto err0;
	}

	/* Success! */
	return (0);

err0:
	/* Failure! */
	return (-1);
}

/**
 * bsdiff_ra_read_open(patchname, oldname):
 * Open the patch file and the "old" file and return a context.
 */
struct bsdiff_ra_read_file *
bsdiff_ra_read_open(const char * patchname, const char * oldname)
{
	struct bsdiff_ra_read_file * ctx;
	struct stat sb;
	uint8_t hbuf[32];
	size_t hblenc;
	uint64_t pdlen;
	size_t nsegs;
	uint8_t * hbc;
	uint8_t * hb;
	off_t ppos;
	size_t i;

	/* Allocate a structure. */
	if ((ctx = malloc(sizeof(struct bsdiff_ra_read_file))) == NULL)
		goto err0;

	/* Open the patch file. */
	if ((ctx->fdp = open(patchname, O_RDONLY)) == -1) {
		warnp("cannot open patch file: %s", patchname);
		goto err1;
	}

	/* Get the patch file size for sanity checking. */
	if (fstat(ctx->fdp, &sb)) {
		warnp("cannot stat patch file: %s", patchname);
		goto err2;
	}

	/* Open the old file. */
	if ((ctx->fdo = open(oldname, O_RDONLY)) == -1) {
		warnp("cannot open old file: %s", oldname);
		goto err2;
	}

	/* Read the patch header. */
	if (sb.st_size < 32) {
		warn0("patch file is truncated: %s", patchname);
		goto err3;
	}
	if (pread(ctx->fdp, hbuf, 32, 0) != 32) {
		warnp("cannot read patch header: %s", patchname);
		goto err3;
	}

	/* Parse the patch header. */
	if (memcmp(&hbuf[0], "BSDIFFSX", 8)) {
		warn0("patch file has bad magic: %s", patchname);
		goto err3;
	}
	ctx->newsize = be64dec(&hbuf[8]);
	ctx->b = be32dec(&hbuf[16]);
	hblenc = be32dec(&hbuf[20]);
	pdlen = be64dec(&hbuf[24]);

	/* Sanity-check the patch file size. */
	if ((uint64_t)(sb.st_size) != 32 + hblenc + pdlen) {
		warn0("patch file has wrong size (%" PRIu64
		    ", should be %" PRIu64 "): %s",
		    (uint64_t)(sb.st_size), 32 + hblenc + pdlen, patchname);
		goto err3;
	}

	/* Sanity-check the new size and block length. */
	if (ctx->newsize / (1 << 30) > ctx->b) {
		warn0("patch file has too many segments: %s", patchname);
		goto err3;
	}

	/* Allocate memory for and read the compressed header block. */
	if ((hbc = malloc(hblenc)) == NULL)
		goto err3;
	if (pread(ctx->fdp, hbc, hblenc, 32) != (ssize_t)hblenc) {
		warnp("cannot read patch header block: %s", patchname);
		goto err4;
	}

	/* Decompress the header block. */
	nsegs = (ctx->newsize + ctx->b - 1) / ctx->b;
	if ((hb = malloc(nsegs * 16)) == NULL)
		goto err4;
	if (decompress(hbc, hblenc, hb, nsegs * 16))
		goto err5;

	/* Parse the header block. */
	if ((ctx->SH = malloc(nsegs * sizeof(struct seghdr))) == NULL)
		goto err5;
	ppos = 32 + hblenc;
	for (i = 0; i < nsegs; i++) {
		/* Parse and record values. */
		ctx->SH[i].opos = be64dec(&hb[i * 16]);
		ctx->SH[i].ppos = ppos;
		ctx->SH[i].olen = be32dec(&hb[i * 16 + 8]);
		ppos += ctx->SH[i].plen = be32dec(&hb[i * 16 + 12]);

		/* Sanity-check. */
		if (ctx->SH[i].plen < 16) {
			warn0("patch file is corrupt: %s", patchname);
			goto err6;
		}
	}
	if (ppos != sb.st_size) {
		warn0("patch file is corrupt: %s", patchname);
		goto err6;
	}

	/* Clean up temporary buffers. */
	free(hb);
	free(hbc);

	/* Success! */
	return (ctx);

err6:
	free(ctx->SH);
err5:
	free(hb);
err4:
	free(hbc);
err3:
	close(ctx->fdo);
err2:
	close(ctx->fdp);
err1:
	free(ctx);
err0:
	/* Failure! */
	return (NULL);
}

/* Patch obuf[olen] with pbuf[plen] and write from start into buf[len]. */
static int
patchseg(uint8_t * pbuf, size_t plen, uint8_t * obuf, size_t olen,
    size_t start, size_t len, uint8_t * buf)
{
	size_t ctrllen, ctrllenc;
	size_t difflen, difflenc;
	size_t extralen, extralenc;
	uint8_t * ctrl, * diff, * extra;
	size_t opos, dpos, epos;
	size_t rlen, slen;
	size_t i, j;

	/* Sanity-check. */
	assert(plen >= 16);

	/* Parse header. */
	ctrllenc = be32dec(&pbuf[0]);
	ctrllen = be32dec(&pbuf[4]);
	difflenc = be32dec(&pbuf[8]);
	extralenc = be32dec(&pbuf[12]);

	/* Make sure the lengths add up correctly. */
	if (16 + ctrllenc + difflenc + extralenc != plen) {
		warn0("patch file is corrupt");
		goto err0;
	}

	/* The ctrl block should have a positive integer number of records. */
	if ((ctrllen == 0) || (ctrllen % 12)) {
		warn0("patch file is corrupt");
		goto err0;
	}

	/* Allocate buffer and decompress ctrl block. */
	if ((ctrl = malloc(ctrllen)) == NULL)
		goto err0;
	if (decompress(&pbuf[16], ctrllenc, ctrl, ctrllen))
		goto err1;

	/* Compute sum of diff and extra blocks. */
	difflen = extralen = 0;
	for (i = 0; i < ctrllen; i += 12) {
		difflen += be32dec(&ctrl[i + 4]);
		extralen += be32dec(&ctrl[i + 8]);
	}

	/* Allocate space for decompressed diff and extra blocks. */
	if ((diff = malloc(difflen + extralen)) == NULL)
		goto err1;
	extra = &diff[difflen];

	/* Decompress diff and extra blocks. */
	if (decompress(&pbuf[16 + ctrllenc], difflenc, diff, difflen))
		goto err2;
	if (decompress(&pbuf[16 + ctrllenc + difflenc], extralenc,
	    extra, extralen))
		goto err2;

	/* Do the patching. */
	for (dpos = epos = opos = i = 0; i < ctrllen; i += 12) {
		/* Seek. */
		opos += decval(&ctrl[i]);

		/* We need to copy-and-add a region. */
		rlen = be32dec(&ctrl[i + 4]);

		/* Check sanity. */
		if (opos + rlen > olen) {
			warn0("patch file is corrupt");
			goto err2;
		}

		/* Skip part of the region if necessary. */
		if (start > rlen)
			slen = rlen;
		else
			slen = start;
		start -= slen;
		rlen -= slen;
		opos += slen;
		dpos += slen;

		/* Stop early if necessary. */
		if (rlen > len)
			rlen = len;

		/* If we have anything left, copy-and-add. */
		for (j = 0; j < rlen; j++)
			*buf++ = obuf[opos++] + diff[dpos++];
		len -= rlen;

		/* We need to insert a region. */
		rlen = be32dec(&ctrl[i + 8]);

		/* Skip part of the region if necessary. */
		if (start > rlen)
			slen = rlen;
		else
			slen = start;
		start -= slen;
		rlen -= slen;
		epos += slen;

		/* Stop early if necessary. */
		if (rlen > len)
			rlen = len;

		/* Insert extra bytes. */
		memcpy(buf, &extra[epos], rlen);
		buf += rlen;
		epos += rlen;
		len -= rlen;
	}

	/* Sanity-check. */
	if ((start != 0) || (len != 0)) {
		warn0("patch file is corrupt");
		goto err2;
	}

	/* Free allocated buffers. */
	free(diff);
	free(ctrl);

	/* Success! */
	return (0);

err2:
	free(diff);
err1:
	free(ctrl);
err0:
	/* Failure! */
	return (-1);
}

/**
 * bsdiff_ra_read_pread(ctx, buf, nbytes, offset):
 * Starting from the specified offset in the "new file", read nbytes into buf.
 * (The "new file" is the file which was used along with the "old file" to
 * construct the patch file.)  Return -1 on error or the number of bytes read
 * (which will be nbytes unless the read hits EOF).
 */
ssize_t
bsdiff_ra_read_pread(struct bsdiff_ra_read_file * ctx, void * buf,
    size_t nbytes, off_t offset)
{
	off_t epos;
	off_t i;
	size_t segoff, seglen;
	uint8_t * pbuf, * obuf;

	/* Figure out how far we can read in total. */
	epos = offset + nbytes;
	if (epos > ctx->newsize)
		epos = ctx->newsize;

	/* Perform one segment of patching at once. */
	for (i = offset / ctx->b; i * ctx->b < epos; i++) {
		/* Where in the segment do we start? */
		if (i * ctx->b >= offset)
			segoff = 0;
		else
			segoff = offset - i * ctx->b;

		/* How far do we read? */
		if ((i + 1) * ctx->b < epos)
			seglen = ctx->b - segoff;
		else
			seglen = epos - (i * ctx->b + segoff);

		/* Allocate a buffer and read patch data. */
		if ((pbuf = malloc(ctx->SH[i].plen)) == NULL)
			goto err0;
		if (pread(ctx->fdp, pbuf, ctx->SH[i].plen,
		    ctx->SH[i].ppos) != ctx->SH[i].plen)
			goto err1;

		/* Allocate a buffer and read old file data. */
		if ((obuf = malloc(ctx->SH[i].olen)) == NULL)
			goto err1;
		if (pread(ctx->fdo, obuf, ctx->SH[i].olen,
		    ctx->SH[i].opos) != ctx->SH[i].olen)
			goto err2;

		/* Perform the segment of patching. */
		if (patchseg(pbuf, ctx->SH[i].plen, obuf, ctx->SH[i].olen,
		    segoff, seglen,
		    (uint8_t *)(buf) + (i * ctx->b + segoff - offset)))
			goto err2;

		/* Free patch and old file data buffers. */
		free(obuf);
		free(pbuf);
	}

	/* Success!  Return bytes read (truncated if we reached EOF). */
	return (epos - offset);

err2:
	free(obuf);
err1:
	free(pbuf);
err0:
	/* Failure! */
	return (-1);
}

/**
 * bsdiff_ra_read_close(ctx):
 * Close the patching context.
 */
void
bsdiff_ra_read_close(struct bsdiff_ra_read_file * ctx)
{

	/* Close files. */
	close(ctx->fdo);
	close(ctx->fdp);

	/* Free headers and context structure. */
	free(ctx->SH);
	free(ctx);
}
