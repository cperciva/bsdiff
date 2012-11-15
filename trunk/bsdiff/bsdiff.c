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

#include "mapfile.h"

#include "bsdiff_align.h"
#include "bsdiff_alignment.h"
#include "bsdiff_writepatch.h"

int
main(int argc, char *argv[])
{
	int oldfd, newfd;
	uint8_t *old, *new;
	size_t oldsize, newsize;
	BSDIFF_ALIGNMENT A;

	if (argc != 4)
		errx(1, "usage: %s oldfile newfile patchfile\n", argv[0]);

	/* Map the old file into memory. */
	if ((old = mapfile(argv[1], &oldfd, &oldsize)) == NULL)
		err(1, "Cannot map file: %s", argv[1]);

	/* Map the new file into memory. */
	if ((new = mapfile(argv[2], &newfd, &newsize)) == NULL)
		err(1, "Cannot map file: %s", argv[2]);

	/* Compute an alignment of the two files. */
	if ((A = bsdiff_align(new, newsize, old, oldsize)) == NULL)
		err(1, "Error aligning files");

	/* Create the patch file. */
	bsdiff_writepatch(argv[3], A, new, newsize, old);

	/* Free the alignment we constructed. */
	bsdiff_alignment_free(A);

	/* Release memory mappings. */
	unmapfile(new, newfd, newsize);
	unmapfile(old, oldfd, oldsize);

	return 0;
}
