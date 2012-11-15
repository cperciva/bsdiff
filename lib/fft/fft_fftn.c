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

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "fft_fft.h"
#include "fft_fftconv.h"

#include "fft_fftn.h"

/* getloglen: Return the log of the power-of-two FFT length needed. */
static size_t
getloglen(size_t N)
{
	size_t i;

	/* Find the least power of 2 greater than N. */
	for (i = 0; ((size_t)1 << i) <= N; i++)
		continue;

	/* Add another bit, since we need to fit into *half* the FFT. */
	return (i + 1);
}

/**
 * fft_fftn_getlen(N):
 * Return the power-of-2 FFT length used to compute length-N transforms.  The
 * value N must be less than 2^26.
 */
size_t
fft_fftn_getlen(size_t N)
{

	/* Sanity-check. */
	assert(N < (1 << 26));

	return (1 << getloglen(N));
}

/**
 * fft_fftn_makelut(LUT, N):
 * Inititalize a look-up table suitable for making calls to fftn_fft and
 * fftn_ifft.  LUT must be a pointer to sufficient space to store 4 * len
 * doubles, where len = fftn_getlen(N).
 */
void
fft_fftn_makelut(double * LUT, size_t N)
{
	size_t len, llen, k, k2;
	double theta;

	/* Figure out the size we're dealing with. */
	llen = getloglen(N);
	len = 1 << llen;

	/* The first quarter is the look-up table needed for regular FFTs. */
	fft_fft_makelut(&LUT[0], llen);

	/* The next quarter is the values exp(-k^2 pi i / N) for k = 0..N-1. */
	for (k2 = k = 0; k < N; k2 = (k2 + 2 * k + 1) % (2 * N), k++) {
		/* k2 = k^2 mod (2*N). */
		theta = -1.0 * k2 * M_PI / N;
		(&LUT[len])[k*2] = cos(theta);
		(&LUT[len])[k*2 + 1] = sin(theta);
	}
	for (; k < len / 2; k++) {
		(&LUT[len])[k*2] = 0;
		(&LUT[len])[k*2 + 1] = 0;
	}

	/*
	 * The final half is the FFT of the vector [exp(k^2 pi i / N)] for
	 * k = -(N-1) .. (N-1).  This is the vector we convolute with the
	 * weighted inputs...
	 */
	LUT[2 * len] = 1.0;
	LUT[2 * len + 1] = 0.0;
	for (k = 1; k < len / 2; k++) {
		(&LUT[2 * len])[k * 2] = (&LUT[len])[k * 2];
		(&LUT[2 * len])[k * 2 + 1] = - (&LUT[len])[k * 2 + 1];
		(&LUT[4 * len])[- k * 2] = (&LUT[len])[k * 2];
		(&LUT[4 * len])[- k * 2 + 1] = - (&LUT[len])[k * 2 + 1];
	}
	LUT[3 * len] = 0.0;
	LUT[3 * len + 1] = 0.0;

	/* ... we compute the FFT now rather than later as an optimization. */
	fft_fft_fft(&LUT[2 * len], llen, &LUT[0]);
}

/**
 * fft_fftn_fft(DAT, N, LUT, TMP):
 * Perform a length-N transform of the values z[k] = DAT[2*k] + DAT[2*k+1] i.
 * The table LUT must have been initialized by fftn_makelut(LUT, N), and TMP
 * must be a pointer to sufficient space to store 2 * len doubles, where len =
 * fftn_getlen(N).
 */
void
fft_fftn_fft(double * restrict DAT, size_t N, double * restrict LUT,
    double * restrict TMP)
{
	size_t llen, len;

	/* Figure out the size we're dealing with. */
	llen = getloglen(N);
	len = 1 << llen;

	/* Copy into temporary space. */
	memcpy(TMP, DAT, 2 * N * sizeof(double));
	memset(&TMP[2 * N], 0, 2 * (len - N) * sizeof(double));

	/* Weight the inputs. */
	fft_fftconv_mulpw(TMP, &LUT[len], llen - 1);

	/* Perform the convolution. */
	fft_fft_fft(TMP, llen, &LUT[0]);
	fft_fftconv_mulpw(TMP, &LUT[2 * len], llen);
	fft_fft_ifft(TMP, llen, &LUT[0]);
	fft_fftconv_scale(TMP, llen);

	/* Weight the outputs. */
	fft_fftconv_mulpw(TMP, &LUT[len], llen - 1);

	/* Copy result out. */
	memcpy(DAT, TMP, 2 * N * sizeof(double));
}

/**
 * fft_fftn_ifft(DAT, N, LUT, TMP):
 * Perform an inverse transform, as per fftn_fft.
 */
void
fft_fftn_ifft(double * restrict DAT, size_t N, double * restrict LUT,
    double * restrict TMP)
{
	size_t i;

	/* The inverse FFT is the conjugate of the FFT of the conjugate. */
	for (i = 0; i < N; i++)
		DAT[2 * i + 1] *= -1;
	fft_fftn_fft(DAT, N, LUT, TMP);
	for (i = 0; i < N; i++)
		DAT[2 * i + 1] *= -1;
}
