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

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#define DONEMASK ((size_t)(1) << (sizeof(size_t) * 8 - 1))
#define SWAP(x, y, tmp) do {	\
	(tmp) = (x);		\
	(x) = (y);		\
	(y) = (tmp);		\
} while (0)

static void
split(size_t *I, size_t *V, size_t start, size_t len, size_t h)
{
	size_t i, j, k, x, tmp, jj, kk;

	if (len < 16) {
		for (k = start; k < start + len; k += j) {
			j = 1;
			x = V[I[k] + h];
			for (i = 1; k + i < start + len; i++) {
				if (V[I[k + i] + h] < x) {
					x = V[I[k + i] + h];
					j = 0;
				};
				if (V[I[k + i] + h] == x) {
					SWAP(I[k + i], I[k + j], tmp);
					j++;
				};
			};
			for (i = 0; i < j; i++)
				V[I[k + i]] = k + j - 1;
			if (j == 1)
				I[k] = 1 | DONEMASK;
		};
		return;
	};

	x = V[I[start + len/2] + h];
	jj = 0;
	kk = 0;
	for (i = start; i < start + len; i++) {
		if (V[I[i] + h] < x)
			jj++;
		if (V[I[i] + h] == x)
			kk++;
	};
	jj += start;
	kk += jj;

	i = start;
	j = 0;
	k = 0;
	while (i < jj) {
		if (V[I[i] + h] < x) {
			i++;
		} else if (V[I[i] + h] == x) {
			SWAP(I[i], I[jj + j], tmp);
			j++;
		} else {
			SWAP(I[i], I[kk + k], tmp);
			k++;
		};
	};

	while (jj + j < kk) {
		if(V[I[jj + j] + h] == x) {
			j++;
		} else {
			SWAP(I[jj + j], I[kk + k], tmp);
			k++;
		};
	};

	if (jj > start)
		split(I, V, start, jj - start, h);

	for (i = 0; i < kk - jj; i++)
		V[I[jj + i]] = kk - 1;
	if (jj == kk - 1)
		I[jj] = 1 | DONEMASK;

	if (start + len > kk)
		split(I, V, kk, start + len - kk, h);
}

/*
 * qsufsort(buf, buflen):
 * Return the suffix sort of the array ${buf}.
 */
size_t *
qsufsort(uint8_t *buf, size_t buflen)
{
	size_t *I, *V;
	size_t buckets[256];
	size_t i, h, len;

	/* Sanity check buflen. */
	if (buflen + 1 > SIZE_MAX / sizeof(size_t)) {
		errno = ENOMEM;
		goto err0;
	}

	/* Allocate I and V arrays. */
	if ((I = malloc((buflen + 1) * sizeof(size_t))) == NULL)
		goto err0;
	if ((V = malloc((buflen + 1) * sizeof(size_t))) == NULL)
		goto err1;

	for (i = 0; i < 256; i++)
		buckets[i] = 0;
	for (i = 0; i < buflen; i++)
		buckets[buf[i]]++;
	for (i = 1; i < 256; i++)
		buckets[i] += buckets[i - 1];
	for (i = 255; i > 0; i--)
		buckets[i] = buckets[i - 1];
	buckets[0] = 0;

	for (i = 0; i < buflen; i++)
		I[++buckets[buf[i]]] = i;
	I[0] = buflen;
	for (i = 0; i < buflen; i++)
		V[i] = buckets[buf[i]];
	V[buflen] = 0;
	for (i = 1; i < 256; i++)
		if (buckets[i] == buckets[i - 1] + 1)
			I[buckets[i]] = 1 | DONEMASK;
	I[0] = 1 | DONEMASK;

	for (h = 1; I[0] != ((buflen + 1) | DONEMASK); h += h) {
		len = 0;
		for (i = 0; i < buflen + 1; ) {
			if (I[i] & DONEMASK) {
				len += I[i] ^ DONEMASK;
				i += I[i] ^ DONEMASK;
			} else {
				if (len)
					I[i - len] = len | DONEMASK;
				len = V[I[i]] + 1 - i;
				split(I, V, i, len, h);
				i += len;
				len = 0;
			};
		};
		if (len)
			I[i - len] = len | DONEMASK;
	};

	for (i = 0; i < buflen + 1; i++)
		I[V[i]] = i;

	/* Don't need this any more. */
	free(V);

	/* Return the suffix sorted array. */
	return (I);

err1:
	free(I);
err0:
	/* Failure! */
	return (NULL);
}
