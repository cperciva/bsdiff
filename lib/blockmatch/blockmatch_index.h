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

#ifndef _BLOCKMATCH_INDEX_H_
#define _BLOCKMATCH_INDEX_H_

#include <stdint.h>
#include <unistd.h>

/* Opaque type. */
struct blockmatch_index;

/**
 * blockmatch_index_index(buf, len, blocklen, diglen, P):
 * Split buf[0 .. len - 1] into blocklen-byte blocks, and compute length-diglen
 * digests.  Return an index which can be passed to blockmatch_index_search.
 * If len is not an exact multiple of blocklen, the final block will be in the
 * range [MIN(blocklen / 2, len), 3 * blocklen / 2) bytes.  Compute the index
 * using P threads.
 */
struct blockmatch_index * blockmatch_index_index(const uint8_t *, size_t,
    size_t, size_t, size_t);

/**
 * blockmatch_index_search(index, buf, len):
 * Compare buf[0.. len - 1] against the blocks in index.  Return the offset of
 * the start of the best-matching block or -1 on error.
 */
ssize_t blockmatch_index_search(const struct blockmatch_index *,
    const uint8_t *, size_t);

/**
 * blockmatch_index_free(index):
 * Free the provided index.
 */
void blockmatch_index_free(struct blockmatch_index *);

#endif /* !_BLOCKMATCH_PSIMM_H_ */
