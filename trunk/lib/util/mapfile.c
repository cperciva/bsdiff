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
#include <sys/mman.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#include "mapfile.h"

/**
 * mapfile(name, fd, len):
 * Open the file ${name} and map it into memory.  Set ${fd} to the file
 * descriptor and ${len} to the file length, and return a pointer to the
 * mapped data.
 */
void *
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
	if ((ptr = mmap(NULL, sb.st_size, PROT_READ,
#ifdef MAP_NOCORE
	    MAP_PRIVATE | MAP_NOCORE,
#else
	    MAP_PRIVATE,
#endif
	    d, 0)) == MAP_FAILED)
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
int
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
