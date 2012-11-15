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
#include <unistd.h>

#include "bsdiff_align_multi.h"
#include "bsdiff_alignment.h"
#include "bsdiff_writepatch.h"
#include "mapfile.h"
#include "warnp.h"

static void usage(void)
{

	(void)fprintf(stderr, "usage: bsdiff-big [-B blocksize] [-L diglen] "
	    "[-P ncores] oldfile newfiles patchfile\n");
	exit(1);
}

#define OPT_EPARSE(ch, optarg) do {				\
	warnp("Error parsing argument: -%c %s", ch, optarg);	\
	exit(1);						\
} while (0)
#define OPT_ERANGE(ch, optarg, min, max) do {			\
	warnp("Argument out of range: -%c %s: Not in [%s, %s]",	\
	    ch, optarg, min, max);				\
	exit(1);						\
} while (0)

int
main(int argc, char * argv[])
{
	char * eptr;
	intmax_t optparse;
	size_t B, L, P;
	int ch;
	uint8_t *old, *new;
	size_t oldsize, newsize;
	int oldfd, newfd;
	BSDIFF_ALIGNMENT A;

	WARNP_INIT;

	/* Set default values. */
	B = 1048576;
	L = 8000;
	P = 1;

	/* Process command line. */
	while ((ch = getopt(argc, argv, "B:L:P:")) != -1) {
		switch((char)ch) {
		case 'B':
			optparse = strtoimax(optarg, &eptr, 0);
			if ((*eptr != '\0') || (optparse == 0))
				OPT_EPARSE(ch, optarg);
			if ((optparse < 0x200) || (optparse > 0x10000000))
				OPT_ERANGE(ch, optarg, "2^9", "2^28");
			B = optparse;
			break;
		case 'L':
			optparse = strtoimax(optarg, &eptr, 0);
			if ((*eptr != '\0') || (optparse == 0))
				OPT_EPARSE(ch, optarg);
			if ((optparse < 0x10) || (optparse > 0x10000))
				OPT_ERANGE(ch, optarg, "16", "65536");
			L = optparse;
			break;
		case 'P':
			optparse = strtoimax(optarg, &eptr, 0);
			if ((*eptr != '\0') || (optparse == 0))
				OPT_EPARSE(ch, optarg);
			if ((optparse < 0x1) || (optparse > 0x40))
				OPT_ERANGE(ch, optarg, "1", "64");
			P = optparse;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* We should have two arguments left. */
	if (argc != 3)
		usage();

	/* Make the two files into memory. */
	if ((old = mapfile(argv[0], &oldfd, &oldsize)) == NULL) {
		warnp("Cannot map file: %s", argv[0]);
		exit(1);
	}
	if ((new = mapfile(argv[1], &newfd, &newsize)) == NULL) {
		warnp("Cannot map file: %s", argv[1]);
		exit(1);
	}

	/* Align the files in parts. */
	if ((A = bsdiff_align_multi(new, newsize, old, oldsize,
	    B, L, P)) == NULL) {
		warnp("bsdiff_align_multi");
		exit(1);
	}

	/* Create the patch file. */
	printf("Writing out patch file...\n");
	bsdiff_writepatch(argv[2], A, new, newsize, old);

	/* Free the alignment we constructed. */
	bsdiff_alignment_free(A);

	/* Release memory mappings. */
	unmapfile(new, newfd, newsize);
	unmapfile(old, oldfd, oldsize);
}
