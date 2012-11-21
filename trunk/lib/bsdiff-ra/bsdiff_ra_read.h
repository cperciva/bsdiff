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

#ifndef _BSDIFF_RA_READ_H_
#define _BSDIFF_RA_READ_H_

#include <sys/types.h>

#include <stddef.h>

/* Opaque type. */
struct bsdiff_ra_read_file;

/**
 * bsdiff_ra_read_open(patchname, oldname):
 * Open the patch file and the "old" file and return a context.
 */
struct bsdiff_ra_read_file * bsdiff_ra_read_open(const char *, const char *);

/**
 * bsdiff_ra_read_pread(ctx, buf, nbytes, offset):
 * Starting from the specified offset in the "new file", read nbytes into buf.
 * (The "new file" is the file which was used along with the "old file" to
 * construct the patch file.)  Return -1 on error or the number of bytes read
 * (which will be nbytes unless the read hits EOF).
 */
ssize_t bsdiff_ra_read_pread(struct bsdiff_ra_read_file *, void *, size_t, off_t);

/**
 * bsdiff_ra_read_close(ctx):
 * Close the patching context.
 */
void bsdiff_ra_read_close(struct bsdiff_ra_read_file *);

#endif /* !_BSDIFF_RA_READ_H_ */
