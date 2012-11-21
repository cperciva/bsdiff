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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsdiff_ra_read.h"
#include "warnp.h"

static void usage(void)
{

	(void)fprintf(stderr,
	    "usage: bspatch-ra oldfile patchfile START LEN\n");
	exit(1);
}

int
main(int argc, char * argv[])
{
	size_t start, len;
	ssize_t lenread;
	struct bsdiff_ra_read_file * f;
	uint8_t * buf;

	WARNP_INIT;

	/* We should have four arguments. */
	if (argc != 5)
		usage();

	/* Open the patch file and the old file. */
	if ((f = bsdiff_ra_read_open(argv[2], argv[1])) == NULL) {
		warnp("Cannot open patching context");
		exit(1);
	}

	/* Parse start and length, and allocate a buffer. */
	start = strtoumax(argv[3], NULL, 0);
	len = strtoumax(argv[4], NULL, 0);
	if ((buf = malloc(len)) == NULL) {
		warnp("Cannot allocate %zu-byte buffer", len);
		exit(1);
	}

	/* Perform patching. */
	if ((lenread = bsdiff_ra_read_pread(f, buf, len, start)) == -1) {
		warnp("Patching failed");
		exit(1);
	}
	if ((size_t)(lenread) < len)
		warn0("Reached EOF, read %zu / %zu bytes",
		    (size_t)lenread, len);

	/* Write new file data out. */
	if (fwrite(buf, lenread, 1, stdout) != 1) {
		warnp("fwrite");
		exit(1);
	}

	/* Free allocated buffer. */
	free(buf);

	/* Close the patching context. */
	bsdiff_ra_read_close(f);
}
